#include "api/net.hpp"
#include "utils/progress_event.hpp"

#include <curl/curl.h>
#include <borealis.hpp>
#include <fstream>

namespace net {
    std::chrono::_V2::steady_clock::time_point time_old;
    double dlold;

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

        if(res != CURLE_OK) {
            brls::Logger::error("Failed to perform request: " + std::string(curl_easy_strerror(res)));
        } else {
            json = nlohmann::json::parse(response);
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

    size_t WriteCallbackFile(void* ptr, size_t size, size_t nmemb, std::ofstream *stream) {
        if (ProgressEvent::instance().getInterupt()) {
            return 0;  // Return 0 to abort the transfer
        }
        stream->write(static_cast<char *>(ptr), size * nmemb);
        return size * nmemb;
    }

    int downloadFileProgress(void* p, double dltotal, double dlnow, double ultotal, double ulnow) {
        if (ProgressEvent::instance().getInterupt()) {
            return 1;  // Return non-zero to abort the transfer
        }
        
        if(dltotal < 0.0) return 0;

        double progress = dlnow / dltotal;
        int counter = (int)(progress * ProgressEvent::instance().getMax());
        ProgressEvent::instance().setStep(std::min(ProgressEvent::instance().getMax() - 1, counter));
        ProgressEvent::instance().setNow(dlnow);
        ProgressEvent::instance().setTotalCount(dltotal);
        auto time_now = std::chrono::steady_clock::now();
        double elapsed_time = ((std::chrono::duration<double>)(time_now - time_old)).count();
        if(elapsed_time > 1.2f) {
            ProgressEvent::instance().setSpeed((dlnow - dlold) / elapsed_time);
            dlold = dlnow;
            time_old = time_now;
        }

        return 0;
    }

    void downloadFile(const std::string& url, const std::string& path) {
        brls::Logger::debug("Downloading file: {}, in the location : {}", url, path);

        // --- Step 1: Check if it's a Google Drive URL ---
        std::string finalUrl = url;

        if (url.find("drive.google.com") != std::string::npos ||
            url.find("drive.usercontent.google.com") != std::string::npos) {
            finalUrl = resolveGoogleDriveUrl(url);
            if (finalUrl.empty()) {
                brls::Logger::error("Failed to resolve Google Drive URL");
                return;
            }
            brls::Logger::debug("Resolved Google Drive URL: {}", finalUrl);
        }

        // --- Step 2: Download the actual file ---
        auto curl = curl_easy_init();
        if (!curl) {
            brls::Logger::error("Failed to initialize curl");
            return;
        }

        curl_easy_setopt(curl, CURLOPT_URL, finalUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, downloadFileProgress);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // Set cookies (required by Google Drive for large files)
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

        std::ofstream ofs(path, std::ios::binary);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);

        auto res = curl_easy_perform(curl);
        ofs.close();
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            brls::Logger::error(curl_easy_strerror(res));
            std::filesystem::remove(path);
        }
    }

} // namespace net
