#include "api/net.hpp"
#include "utils/progress_event.hpp"

#include <curl/curl.h>
#include <borealis.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <array>
#include <cstring>

namespace net {
    std::chrono::_V2::steady_clock::time_point time_old;
    double dlold;

    bool checkConnection() {
        brls::Logger::info("checkConnection: checking internet connectivity...");

        auto curl = curl_easy_init();
        if (!curl) {
            brls::Logger::error("checkConnection: failed to initialize curl");
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, "http://connectivitycheck.gstatic.com/generate_204");
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

        auto res = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            brls::Logger::error("checkConnection: curl error: {}", curl_easy_strerror(res));
            return false;
        }

        brls::Logger::info("checkConnection: HTTP {}, connected={}", httpCode, httpCode == 204);
        return httpCode == 204;
    }

    size_t WriteCallback(void* content, size_t size, size_t nmemb, std::string* buffer) {
        buffer->append((char*)content, size * nmemb);
        return size * nmemb;
    }

    nlohmann::json downloadRequest(std::string url) {
        auto curl = curl_easy_init();

        brls::Logger::debug("Requesting: " + url);
        if(!curl) {
            brls::Logger::error("Failed to initialize curl");
            return nlohmann::json();
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        std::string response;

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "UAModDownloader");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        auto res = curl_easy_perform(curl);

        nlohmann::json json;

        if (res != CURLE_OK) {
            brls::Logger::error("downloadRequest: curl error: {}", curl_easy_strerror(res));
        } else {
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            brls::Logger::debug("downloadRequest: HTTP {}", httpCode);

            if (httpCode != 200) {
                brls::Logger::error("downloadRequest: unexpected HTTP {}", httpCode);
            } else {
                try {
                    json = nlohmann::json::parse(response);
                } catch (const std::exception& e) {
                    brls::Logger::error("downloadRequest: JSON parse error: {}", e.what());
                }
            }
        }

        curl_easy_cleanup(curl);

        return json;
    }

    size_t WriteCallbackImage(char* ptr, size_t size, size_t nmemb, void* userdata) {
        if (ProgressEvent::instance().getInterupt()) {
            return 0;
        }
        std::vector<unsigned char>* buffer = static_cast<std::vector<unsigned char>*>(userdata);
        size_t total_size = size * nmemb;
        buffer->insert(buffer->end(), ptr, ptr + total_size);
        return total_size;
    }

    // Callback to capture the HTML response into a string
    static size_t WriteCallbackString(void* contents, size_t size, size_t nmemb, std::string* output) {
        output->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

    std::string resolveGoogleDriveUrl(const std::string& url) {
        // Extract file ID from URL
        std::string fileId;

        // Handle formats:
        // https://drive.google.com/uc?export=download&id=FILE_ID
        // https://drive.google.com/file/d/FILE_ID/view
        // https://drive.usercontent.google.com/download?id=FILE_ID
        auto idPos = url.find("id=");
        if (idPos != std::string::npos) {
            fileId = url.substr(idPos + 3);
            auto endPos = fileId.find_first_of("&/");
            if (endPos != std::string::npos)
                fileId = fileId.substr(0, endPos);
        } else {
            auto dPos = url.find("/d/");
            if (dPos != std::string::npos) {
                fileId = url.substr(dPos + 3);
                auto endPos = fileId.find('/');
                if (endPos != std::string::npos)
                    fileId = fileId.substr(0, endPos);
            }
        }

        if (fileId.empty()) {
            brls::Logger::error("Could not extract Google Drive file ID from URL");
            return "";
        }

        brls::Logger::debug("Extracted Google Drive file ID: {}", fileId);

        // --- First request: fetch the warning page to extract UUID ---
        std::string warningPageUrl = "https://drive.google.com/uc?export=download&id=" + fileId;
        std::string responseHtml;

        auto curl = curl_easy_init();
        if (!curl) return "";

        curl_easy_setopt(curl, CURLOPT_URL, warningPageUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");  // Enable cookie engine
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseHtml);

        auto res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            brls::Logger::error("Failed to fetch Google Drive warning page: {}", curl_easy_strerror(res));
            return "";
        }

        // --- Extract UUID from the HTML response ---
        // Looking for: name="uuid" value="XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
        std::string uuid;
        auto uuidPos = responseHtml.find("name=\"uuid\"");
        if (uuidPos != std::string::npos) {
            auto valuePos = responseHtml.find("value=\"", uuidPos);
            if (valuePos != std::string::npos) {
                valuePos += 7; // skip 'value="'
                auto endPos = responseHtml.find("\"", valuePos);
                if (endPos != std::string::npos)
                    uuid = responseHtml.substr(valuePos, endPos - valuePos);
            }
        }

        if (uuid.empty()) {
            // No virus warning page — file is small enough, direct download works
            brls::Logger::debug("No UUID found, using direct download URL");
            return "https://drive.usercontent.google.com/download?id=" + fileId + "&export=download&confirm=t";
        }

        brls::Logger::debug("Extracted UUID: {}", uuid);

        // --- Build the final confirmed download URL ---
        return "https://drive.usercontent.google.com/download?id=" + fileId +
               "&export=download&confirm=t&uuid=" + uuid;
    }

    void downloadImage(const std::string& url, std::vector<unsigned char>& buffer) {
        auto curl = curl_easy_init();

        brls::Logger::debug("Downloading image: {}", url);
        if(!curl) {
            brls::Logger::error("Failed to initialize curl");
            return;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackImage);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 120000L);

        auto res = curl_easy_perform(curl);
        if(res != CURLE_OK)
            brls::Logger::error(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
    }

    // Ring-buffer async writer — pre-allocates POOL_SIZE slots of SLOT_SIZE bytes once,
    // then reuses them forever. No heap alloc/dealloc per chunk, so no fragmentation.
    struct AsyncWriter {
        static constexpr size_t SLOT_SIZE = 524288; // must match CURLOPT_BUFFERSIZE
        static constexpr size_t POOL_SIZE = 4;

        FILE* fp;
        bool done = false;
        bool writeError = false;

        struct Slot { std::vector<char> buf; size_t used = 0; };
        std::array<Slot, POOL_SIZE> pool;
        size_t readIdx  = 0;
        size_t writeIdx = 0;
        size_t count    = 0; // number of filled slots

        std::mutex mtx;
        std::condition_variable cv_data;
        std::condition_variable cv_space;
        std::thread writerThread;

        explicit AsyncWriter(FILE* f) : fp(f) {
            for (auto& slot : pool)
                slot.buf.resize(SLOT_SIZE);
            writerThread = std::thread([this] { run(); });
        }

        // Called from curl thread — blocks if all slots are full (backpressure)
        bool push(const char* data, size_t n) {
            std::unique_lock<std::mutex> lock(mtx);
            cv_space.wait(lock, [this] { return count < POOL_SIZE || done; });
            if (done) return false;
            auto& slot = pool[writeIdx];
            std::memcpy(slot.buf.data(), data, n);
            slot.used = n;
            writeIdx = (writeIdx + 1) % POOL_SIZE;
            ++count;
            cv_data.notify_one();
            return true;
        }

        void finish() {
            { std::lock_guard<std::mutex> lock(mtx); done = true; }
            cv_data.notify_one();
            cv_space.notify_all();
            writerThread.join();
        }

    private:
        void run() {
            while (true) {
                size_t idx, sz;
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    cv_data.wait(lock, [this] { return count > 0 || done; });
                    if (count == 0) break;
                    idx = readIdx;
                    sz  = pool[readIdx].used;
                    // count not decremented yet — keeps producer from overwriting this slot
                }
                fwrite(pool[idx].buf.data(), 1, sz, fp);
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    readIdx = (readIdx + 1) % POOL_SIZE;
                    --count;
                }
                cv_space.notify_one();
            }
        }
    };

    size_t WriteCallbackFile(void* ptr, size_t size, size_t nmemb, AsyncWriter* writer) {
        if (ProgressEvent::instance().getInterupt()) {
            return 0;  // Return 0 to abort the transfer
        }
        size_t total = size * nmemb;
        return writer->push(static_cast<char*>(ptr), total) ? total : 0;
    }

    int downloadFileProgress(void* p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
        if (ProgressEvent::instance().getInterupt()) {
            return 1;  // Return non-zero to abort the transfer
        }

        if (dltotal <= 0) return 0;

        double progress = (double)dlnow / (double)dltotal;
        int counter = (int)(progress * ProgressEvent::instance().getMax());
        ProgressEvent::instance().setStep(std::min(ProgressEvent::instance().getMax() - 1, counter));
        ProgressEvent::instance().setNow((double)dlnow);
        ProgressEvent::instance().setTotalCount((double)dltotal);
        auto time_now = std::chrono::steady_clock::now();
        double elapsed_time = ((std::chrono::duration<double>)(time_now - time_old)).count();
        if (elapsed_time > 1.2f) {
            ProgressEvent::instance().setSpeed(((double)dlnow - dlold) / elapsed_time);
            dlold = (double)dlnow;
            time_old = time_now;
        }

        return 0;
    }

    bool downloadFile(const std::string& url, const std::string& path, const std::string& token) {
        brls::Logger::debug("Downloading file: {}, in the location : {}", url, path);

        // --- Step 1: Check if it's a Google Drive URL ---
        std::string finalUrl = url;

        if (url.find("drive.google.com") != std::string::npos ||
            url.find("drive.usercontent.google.com") != std::string::npos) {
            finalUrl = resolveGoogleDriveUrl(url);
            if (finalUrl.empty()) {
                brls::Logger::error("Failed to resolve Google Drive URL");
                return false;
            }
            brls::Logger::debug("Resolved Google Drive URL: {}", finalUrl);
        }

        // --- Step 2: Download the actual file ---
        auto curl = curl_easy_init();
        if (!curl) {
            brls::Logger::error("Failed to initialize curl");
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, finalUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, downloadFileProgress);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // Set cookies (required by Google Drive for large files)
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

        struct curl_slist* headers = nullptr;
        if (!token.empty()) {
            std::string authHeader = "Authorization: token " + token;
            headers = curl_slist_append(headers, authHeader.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        // Large receive buffer — reduces callback frequency from ~40k calls to ~600 for a 630MB file
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 524288L); // 512 KB

        // Try HTTP/2 for better throughput; falls back to HTTP/1.1 if unsupported
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);

        FILE* fp = fopen(path.c_str(), "wb");
        if (!fp) {
            brls::Logger::error("downloadFile: failed to open {}", path);
            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return false;
        }
        // 1 MB kernel write buffer — writer thread flushes large sequential chunks
        std::vector<char> fileBuf(1024 * 1024);
        setvbuf(fp, fileBuf.data(), _IOFBF, fileBuf.size());

        AsyncWriter writer(fp);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writer);

        auto res = curl_easy_perform(curl);
        writer.finish(); // flush remaining chunks and join writer thread
        fclose(fp);
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            brls::Logger::error("downloadFile: curl error: {}", curl_easy_strerror(res));
            std::filesystem::remove(path);
            return false;
        }
        return true;
    }

} // namespace net
