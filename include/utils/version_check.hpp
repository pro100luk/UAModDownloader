#pragma once

#include <string>

namespace version {
    // Get installed game version from Title ID
    std::string getInstalledGameVersion(const std::string& titleIdStr);
    
    // Check if game is installed
    bool isGameInstalled(const std::string& titleIdStr);
    
    // Compare version strings
    int compareVersions(const std::string& v1, const std::string& v2);
    
    // Check compatibility and show dialogs
    bool checkVersionCompatibility(const std::string& titleIdStr, const std::string& requiredVersion);
    
    // Show version info notification
    void showVersionInfo(const std::string& titleIdStr, const std::string& requiredVersion);
}