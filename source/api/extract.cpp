#include "api/extract.hpp"
#include "utils/progress_event.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <cstdio>
#include <switch.h>
#include <borealis.hpp>
#include <unistd.h>

namespace fs = std::filesystem;

namespace extract {

    // Допоміжна функція для повного видалення
    void forceDelete(const std::string& path) {
        std::error_code ec;
        if (!fs::exists(path)) return;
        
        fs::remove_all(path, ec);
        if (fs::exists(path)) {
            // Якщо не видалилося, пробуємо rmdir (тільки для порожніх папок)
            rmdir(path.c_str());
        }
    }

    std::string findModRoot(const std::string& startPath) {
        try {
            if (!fs::exists(startPath)) return "";
            for (const auto& entry : fs::recursive_directory_iterator(startPath)) {
                if (entry.is_directory()) {
                    std::string name = entry.path().filename().string();
                    if (name == "romfs" || name == "exefs") return entry.path().parent_path().string();
                }
            }
        } catch (...) { }
        return "";
    }

    int getFileCount(const std::string& archivePath) {
        struct archive* a = archive_read_new();
        struct archive_entry* entry;
        int count = 0;
        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);
        if (archive_read_open_filename(a, archivePath.c_str(), 10240) == ARCHIVE_OK) {
            while (archive_read_next_header(a, &entry) == ARCHIVE_OK) count++;
            archive_read_close(a);
        }
        archive_read_free(a);
        return count;
    }

    bool extractEntry(const std::string& archiveFile, const std::string& outputDir, const std::string& tid) {
        brls::Logger::info("extract: starting for TID: {}", tid);

        std::string baseDir = "sdmc:/config/UAModDownloader";
        std::string tempDir = baseDir + "/temp_" + tid;
        std::string atmospherePath = "sdmc:/atmosphere/contents/" + tid;
        std::error_code ec;

        try {
            // --- CLEANUP OLD TEMP DIRS ---
            for (const auto& entry : fs::directory_iterator(baseDir, ec)) {
                std::string name = entry.path().filename().string();
                if (name.find("temp_") == 0 || name.find(".garbage_") == 0)
                    fs::remove_all(entry.path(), ec);
            }

            fs::create_directories(tempDir, ec);
            brls::Logger::info("extract: tempDir created: {}", tempDir);

            int totalFiles = getFileCount(archiveFile);
            brls::Logger::info("extract: archive contains {} entries", totalFiles);
            ProgressEvent::instance().setTotalSteps(totalFiles);

            // --- EXTRACT ---
            struct archive* a = archive_read_new();
            archive_read_support_format_all(a);
            archive_read_support_filter_all(a);

            if (archive_read_open_filename(a, archiveFile.c_str(), 10240) != ARCHIVE_OK) {
                brls::Logger::error("extract: failed to open archive: {}", archive_error_string(a));
                archive_read_free(a);
                return false;
            }

            struct archive_entry* entry;
            while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
                if (ProgressEvent::instance().getInterupt()) break;

                std::string entryName = archive_entry_pathname(entry);
                brls::Logger::debug("extract: entry: {}", entryName);
                fs::path outputPath = fs::path(tempDir) / entryName;

                if (archive_entry_filetype(entry) == AE_IFDIR) {
                    fs::create_directories(outputPath, ec);
                } else {
                    fs::create_directories(outputPath.parent_path(), ec);
                    FILE* outFile = fopen(outputPath.c_str(), "wb");
                    if (outFile) {
                        const void* buff;
                        size_t size;
                        la_int64_t offset;
                        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK)
                            fwrite(buff, 1, size, outFile);
                        fclose(outFile);
                    } else {
                        brls::Logger::error("extract: failed to open output file: {}", outputPath.string());
                    }
                }
                ProgressEvent::instance().incrementStep(1);
            }
            archive_read_close(a);
            archive_read_free(a);
            brls::Logger::info("extract: archive extraction complete");

            // --- INSTALL ---
            std::string modRoot = findModRoot(tempDir);
            brls::Logger::info("extract: modRoot: '{}'", modRoot);

            if (!modRoot.empty()) {
                fs::create_directories(atmospherePath, ec);
                brls::Logger::info("extract: installing to: {}", atmospherePath);

                for (const auto& item : fs::directory_iterator(modRoot, ec)) {
                    std::string itemName = item.path().filename().string();
                    fs::path dest = fs::path(atmospherePath) / itemName;
                    brls::Logger::info("extract: moving {} -> {}", item.path().string(), dest.string());

                    if (fs::exists(dest, ec)) {
                        std::string old = dest.string() + ".old";
                        fs::remove_all(old, ec);
                        fs::rename(dest, old, ec);
                        brls::Logger::info("extract: backed up existing {} to .old", itemName);
                        fs::remove_all(old, ec);
                    }
                    fs::rename(item.path(), dest, ec);
                    if (ec) {
                        brls::Logger::error("extract: rename failed for {}: {}", itemName, ec.message());
                    }
                }
            } else {
                brls::Logger::error("extract: modRoot not found in tempDir");
            }

            // --- CLEANUP ---
            if (fs::exists(archiveFile, ec)) fs::remove(archiveFile, ec);

            if (fs::exists(tempDir, ec)) {
                std::string garbage = baseDir + "/.garbage_" + tid;
                fs::remove_all(garbage, ec);
                fs::rename(tempDir, garbage, ec);
                forceDelete(garbage);
            }

            brls::Logger::info("extract: done");
            ProgressEvent::instance().setStep(ProgressEvent::instance().getMax());
            return true;

        } catch (const std::exception& e) {
            brls::Logger::error("extract: exception: {}", e.what());
            return false;
        } catch (...) {
            brls::Logger::error("extract: unknown exception");
            return false;
        }
    }
}