#include <switch.h>
#include <string>
#include <vector>
#include <tuple>
#include <borealis.hpp>

using namespace brls::literals;

namespace version {

    // Helper to convert hex string to u64
    u64 stringToTitleId(const std::string& tidStr) {
        try {
            return std::stoull(tidStr, nullptr, 16);
        } catch (const std::exception& e) {
            brls::Logger::error("Failed to convert TID: {}", e.what());
            return 0;
        }
    }

    // Get display version string from NACP
    std::string getInstalledGameVersion(const std::string& titleIdStr) {
        try {
            brls::Logger::info("--- VERSION DEBUG START ---");
            
            u64 titleId = stringToTitleId(titleIdStr);
            if (titleId == 0) return "";

            // Allocate buffer on heap (approx 140KB)
            std::vector<u8> buffer(sizeof(NsApplicationControlData));
            u64 actual_size = 0;

            // Query system for application metadata
            Result rc = nsGetApplicationControlData(
                NsApplicationControlSource_Storage,
                titleId,
                (NsApplicationControlData*)buffer.data(),
                buffer.size(),
                &actual_size
            );

            brls::Logger::info("NS Result: 0x{:X}, Size received: {}", rc, actual_size);

            if (R_FAILED(rc)) {
                brls::Logger::error("nsGetApplicationControlData failed for {}: 0x{:X}", titleIdStr, rc);
                return "";
            }

            // Check if we received at least the NacpStruct (the metadata header)
            if (actual_size < sizeof(NacpStruct)) {
                brls::Logger::error("Incomplete NACP data received for {}", titleIdStr);
                return "";
            }

            const NsApplicationControlData* controlData = reinterpret_cast<NsApplicationControlData*>(buffer.data());
            const char* displayVersion = controlData->nacp.display_version;

            // DEBUG: See if the memory is actually filled with data
            brls::Logger::info("Raw version bytes: {:02X} {:02X} {:02X} {:02X}", 
                (u8)displayVersion[0], (u8)displayVersion[1], (u8)displayVersion[2], (u8)displayVersion[3]);

            // Ensure string is null-terminated and valid
            std::string versionStr(displayVersion, strnlen(displayVersion, sizeof(controlData->nacp.display_version)));
            
            brls::Logger::info("TID: {} | Detected Version: '{}'", titleIdStr, versionStr.empty() ? "None" : versionStr);
            brls::Logger::info("--- VERSION DEBUG END ---");
            
            return versionStr.empty() ? "1.0.0" : versionStr;
            
        } catch (const std::exception& e) {
            brls::Logger::error("Exception in getInstalledGameVersion: {}", e.what());
            return "";
        }
    }

    bool isGameInstalled(const std::string& titleIdStr) {
        try {
            u64 titleId = stringToTitleId(titleIdStr);
            if (titleId == 0) return false;

            std::vector<u8> buffer(sizeof(NsApplicationControlData));
            u64 actual_size = 0;

            Result rc = nsGetApplicationControlData(
                NsApplicationControlSource_Storage, 
                titleId, 
                (NsApplicationControlData*)buffer.data(), 
                buffer.size(), 
                &actual_size
            );
            
            return R_SUCCEEDED(rc);
        } catch (...) {
            return false;
        }
    }

    int compareVersions(const std::string& v1, const std::string& v2) {
        try {
            auto parseVersion = [](const std::string& v) -> std::tuple<int, int, int> {
                int major = 0, minor = 0, patch = 0;
                if (v.empty()) return {0, 0, 0};
                sscanf(v.c_str(), "%d.%d.%d", &major, &minor, &patch);
                return {major, minor, patch};
            };
            
            auto [maj1, min1, pat1] = parseVersion(v1);
            auto [maj2, min2, pat2] = parseVersion(v2);
            
            if (maj1 != maj2) return maj1 > maj2 ? 1 : -1;
            if (min1 != min2) return min1 > min2 ? 1 : -1;
            if (pat1 != pat2) return pat1 > pat2 ? 1 : -1;
            return 0;
        } catch (...) {
            return 0;
        }
    }

    bool checkVersionCompatibility(const std::string& titleIdStr, const std::string& requiredVersion) {
        if (!isGameInstalled(titleIdStr)) {
            brls::sync([titleIdStr]() {
                auto dialog = new brls::Dialog(fmt::format(
                    "Game not installed\n\n"
                    "Title ID: {}\n\n"
                    "Please install the game before installing this mod.",
                    titleIdStr
                ));
                dialog->addButton("hints/select"_i18n, []() {});
                dialog->open();
            });
            return false;
        }

        std::string installed = getInstalledGameVersion(titleIdStr);
        if (compareVersions(installed, requiredVersion) < 0) {
            brls::sync([installed, requiredVersion]() {
                auto dialog = new brls::Dialog(fmt::format(
                    "Update Required\n\n"
                    "Installed: v{}\n"
                    "Required:  v{}\n\n"
                    "Please update your game to use this mod.",
                    installed, requiredVersion
                ));
                dialog->addButton("hints/select"_i18n, []() {});
                dialog->open();
            });
            return false;
        }
        
        return true;
    }

    void showVersionInfo(const std::string& titleIdStr, const std::string& requiredVersion) {
        if (!isGameInstalled(titleIdStr)) {
            brls::Application::notify("Game not installed");
            return;
        }
        
        std::string installedVersion = getInstalledGameVersion(titleIdStr);
        brls::Application::notify(fmt::format("Required: {} | Installed: {}", requiredVersion, installedVersion));
    }

} // namespace version