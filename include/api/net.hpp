#pragma once

#include <nlohmann/json.hpp>

namespace net {
    bool checkConnection();
    nlohmann::json downloadRequest(std::string url);
    void downloadImage(const std::string& url, std::vector<unsigned char>& buffer);
    bool downloadFile(const std::string& url, const std::string& path, const std::string& token = "");
}