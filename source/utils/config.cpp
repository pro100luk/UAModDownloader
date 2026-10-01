#include "utils/config.hpp"

#include <switch.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include <borealis.hpp>

bool cp(const char *filein, const char *fileout) {
    FILE *exein, *exeout;
    exein = fopen(filein, "rb");
    if (exein == NULL) {
        perror("file open for reading");
        return false;
    }
    exeout = fopen(fileout, "wb");
    if (exeout == NULL) {
        fclose(exein);
        perror("file open for writing");
        return false;
    }
    size_t n, m;
    unsigned char buff[8192];
    do {
        n = fread(buff, 1, sizeof buff, exein);
        if (n) m = fwrite(buff, 1, n, exeout);
        else   m = 0;
    }
    while ((n > 0) && (n == m));
    if (m) {
        perror("copy");
        return false;
    }
    if (fclose(exeout)) {
        perror("close output file");
        return false;
    }
    if (fclose(exein)) {
        perror("close input file");
        return false;
    }
    return true;
} 

namespace cfg {
    Config::Config() {
        this->loadConfig();
        this->parseConfig();
    }

    void Config::loadConfig() {
        const char* configPath = "sdmc:/config/UAModDownloader/settings.json";
        
        if(!std::filesystem::exists(configPath)) {
            chdir("sdmc:/");
            std::filesystem::create_directories("sdmc:/config/UAModDownloader/");
            cp("romfs:/json/settings.json", configPath);
        }

        std::ifstream file(configPath);
        if (!file.is_open()) {
            brls::Logger::error("Failed to open config file: {}", configPath);
            config = nlohmann::json::object();
            return;
        }
        
        config = nlohmann::json::parse(file);
        file.close();
    }

    void Config::parseConfig() {
        try {
            // REMOVED: app_language parsing
            is_strict = config.contains("is_strict") ? config["is_strict"].get<bool>() : true;
            wireframe = config.contains("wireframe") ? config["wireframe"].get<bool>() : false;
        } catch (const std::exception& e) {
            brls::Logger::error("Error parsing config: {}", e.what());
            is_strict = true;
            wireframe = false;
        }
    }

    // REMOVED: getAppLanguage() and setAppLanguage()

    bool Config::getStrictSearch() {
        return this->is_strict;
    }
    
    void Config::setStringSearch(bool strict) {
        this->is_strict = strict;
    }

    void Config::setWireframe(bool wireframeEnabled) {
        this->wireframe = wireframeEnabled;
    }

    void Config::saveConfig() {
        // REMOVED: language saving
        this->config["is_strict"] = is_strict;
        this->config["wireframe"] = wireframe;

        std::ofstream file("sdmc:/config/UAModDownloader/settings.json");
        file << this->config.dump(4);
        file.close();
    }
}