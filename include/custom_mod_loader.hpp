#ifndef CUSTOM_MOD_LOADER_HPP
#define CUSTOM_MOD_LOADER_HPP

#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <borealis.hpp>

namespace CustomModLoader {

struct ModInfo {
    std::string id;
    std::string name;
    std::string description;
    std::string author;
    std::string category;
    std::string game_version;
    std::string translation_version;
    std::string download_url;
    std::string download_filename;
    std::string image_url;
    std::string title_id;
    std::string title_name;
    int file_size;
    std::string updated_date;
    std::vector<std::string> tags;
};

class DataLoader {
private:
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }

public:
    static std::vector<ModInfo> fetchAndParse() {
        std::vector<ModInfo> mods;
        
        // GitHub raw URL - this is important!
        const std::string jsonUrl = "https://swuk.com.ua/wp-json/custom-api/v1/titles";
        
        std::string jsonData;
        CURL* curl = curl_easy_init();
        
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, jsonUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &jsonData);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "SimpleUkrDownloader/1.0");
            
            CURLcode res = curl_easy_perform(curl);
            
            if (res != CURLE_OK) {
                // Handle error
                curl_easy_cleanup(curl);
                return mods; // Return empty vector
            }
            
            curl_easy_cleanup(curl);
        }
        
        // Parse JSON
        try {
            nlohmann::json jsonObj = nlohmann::json::parse(jsonData);
            
            // Ваш API повертає масив прямо в корені, а не в полі "mods"
            if (jsonObj.is_array()) {
                for (const auto& item : jsonObj) {
                    ModInfo mod;
                    
                    // title_id правильний
                    mod.id = item.value("title_id", "Unknown ID");
                    
                    // ЗМІНІТЬ ЦЕ: ключ у вашому JSON - просто "name"
                    mod.name = item.value("name", "Unknown Game"); 

                    // Додатково можна витягнути картинку, якщо потрібно
                    mod.image_url = item.value("image_url", "");
                    
                    mods.push_back(mod);
                }
            }
        } catch (const nlohmann::json::exception& e) {
            
            // Handle JSON parsing error
            // Consider logging: e.what()
        }
        
        return mods;
    }
    
    // Alternative: Load from local cached file
    static std::vector<ModInfo> loadFromFile(const std::string& filepath) {
        std::vector<ModInfo> mods;
        
        try {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                return mods;
            }
            
            nlohmann::json jsonObj;
            file >> jsonObj;
            file.close();
            
            if (jsonObj.contains("mods") && jsonObj["mods"].is_array()) {
                for (const auto& modJson : jsonObj["mods"]) {
                    ModInfo mod;
                    mod.id = modJson.value("id", "");
                    mod.name = modJson.value("name", "");
                    mod.description = modJson.value("description", "");
                    mod.author = modJson.value("author", "");
                    mod.category = modJson.value("category", "");
                    mod.game_version = modJson.value("game_version", "");
                    mod.translation_version = modJson.value("translation_version", "");
                    mod.download_url = modJson.value("download_url", "");
                    mod.image_url = modJson.value("image_url", "");
                    mod.file_size = modJson.value("file_size", 0);
                    mod.updated_date = modJson.value("updated_date", "");
                    
                    if (modJson.contains("tags") && modJson["tags"].is_array()) {
                        for (const auto& tag : modJson["tags"]) {
                            mod.tags.push_back(tag.get<std::string>());
                        }
                    }
                    
                    mods.push_back(mod);
                }
            }
        } catch (const std::exception& e) {
            // Handle error
        }
        
        return mods;
    }
};

} // namespace CustomModLoader

#endif // CUSTOM_MOD_LOADER_HPP