#include "api/game.hpp"
#include "api/net.hpp"
#include "utils/config.hpp"
#include <curl/curl.h>

#include <regex>

// Define the URL to your JSON database
const std::string DB_URL = "https://swuk.com.ua/wp-json/custom-api/v1/titles";

// Static cache for the DB so we don't download it for every single game instance
static nlohmann::json customDb;
static bool dbLoaded = false;

Game::Game(const std::string& m_title, const std::string& m_tid) {
    title = m_title;
    tid = m_tid;

    // Load the Custom DB from SWUK API
    if (!dbLoaded) {
        try {
            customDb = net::downloadRequest(DB_URL); 
            
            if (!customDb.empty()) {
                dbLoaded = true;
                brls::Logger::info("Successfully loaded Custom Translation DB from SWUK API");
            }
        } catch (const std::exception& e) {
            brls::Logger::error("Failed to download Custom DB: {}", e.what());
        }
    }

    // Check if this TitleID exists in the Custom DB
    if (dbLoaded && customDb.is_array()) {
        try {
            for (const auto& modEntry : customDb) {
                if (modEntry.contains("title_id") && modEntry["title_id"].get<std::string>() == tid) {
                    
                    if (modEntry.contains("name")) {
                        title = modEntry["name"].get<std::string>();
                    }

                    if (modEntry.contains("image_url")) {
                        bannerURL = modEntry["image_url"].get<std::string>();
                    }

                    swukID = 1; 
                    categories.push_back(Category("Українізатори", 1, 0));

                    brls::Logger::info("Знайдено українізатори для: {} ({})", title, tid);
                    break; 
                }
            }
        } catch (const std::exception& e) {
            brls::Logger::error("Error parsing custom DB entry for {}: {}", tid, e.what());
        }
    }
}
