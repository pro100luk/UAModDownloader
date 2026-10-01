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

#ifdef BOTW_BUILD
    gamebananaID = 1;
    return;
#endif

    // --- STEP 1: Load the Custom DB from the new API ---
    if (!dbLoaded) {
        try {
            // downloadRequest returns the JSON (Array or Object)
            customDb = net::downloadRequest(DB_URL); 
            
            // Check if we got a valid response
            if (!customDb.empty()) {
                dbLoaded = true;
                brls::Logger::info("Successfully loaded Custom Translation DB from API");
            }
        } catch (const std::exception& e) {
            brls::Logger::error("Failed to download Custom DB: {}", e.what());
        }
    }

    // --- STEP 2: Check if this TitleID exists in the Custom DB ---
    // NEW LOGIC: The API returns a direct Array, so we check .is_array() on the root
    if (dbLoaded && customDb.is_array()) {
        try {
            bool found = false;
            
            // Iterate directly over the root array
            for (const auto& modEntry : customDb) {
                // CHANGED: "id" is now "title_id"
                if (modEntry.contains("title_id") && modEntry["title_id"].get<std::string>() == tid) {
                    
                    if (modEntry.contains("name")) {
                        title = modEntry["name"].get<std::string>();
                    }

                    if (modEntry.contains("image_url")) {
                        bannerURL = modEntry["image_url"].get<std::string>();
                    }

                    gamebananaID = 1; 
                    categories.push_back(Category("Українізатори", 1, 0));

                    brls::Logger::info("Знайдено українізатори для: {} ({})", title, tid);
                    found = true;
                    break; 
                }
            }

            if (found) return; // Exit and don't call GameBanana
            
        } catch (const std::exception& e) {
            brls::Logger::error("Error parsing custom DB entry for {}: {}", tid, e.what());
        }
    }
    
    // --- STEP 3: Fallback (GameBanana) ---
    searchGame();
    parseJson();
    if (gamebananaID > 0)
        loadCategories();
}

void Game::searchGame() {
    auto curl = curl_easy_init();
    std::string title_url;
    title_url = curl_easy_escape(curl, title.c_str(), title.length());
    curl_easy_cleanup(curl);

    cfg::Config config;
    std::string endpoint = config.getStrictSearch() ?
        "https://gamebanana.com/apiv11/Util/Game/NameMatch?_sName={}" :
        "https://gamebanana.com/apiv11/Util/Search/Results?_sModelName=Game&_sOrder=best_match&_sSearchString={}%20%28Switch%29";

    try {
        json = net::downloadRequest(fmt::format(endpoint, title_url));
    } catch (const std::exception& e) {
        brls::Logger::error("Failed to search for game: " + title + " - " + e.what());
        gamebananaID = -1;
        return;
    }
    
    
    if(json.empty()) {
        brls::Logger::error("Failed to search for game: " + title);
    }
}

void Game::parseJson() {
    if(json.empty() || json.at("_aMetadata").at("_nRecordCount").get<int>() == 0) {
        return;
    }

    int pos = 0;
    cfg::Config config;
    if(config.getStrictSearch()) {
        const auto& records = json.at("_aRecords");
        for (size_t i = 0; i < records.size(); ++i) {
            std::string sName = records[i].at("_sName").get<std::string>();
            if (sName.find("Switch") != std::string::npos) { // Disembiguation for multiplat titles
                pos = i;
                break;
            }
        }
    }

    const auto& record = json.at("_aRecords")[pos];
    title = record.at("_sName").get<std::string>();
    gamebananaID = record.at("_idRow").get<int>();
}

void Game::loadCategories() {
    json = net::downloadRequest(fmt::format("https://gamebanana.com/apiv11/Game/{}/ProfilePage", gamebananaID));

    if(json.empty() || json.find("_aModRootCategories") == json.end()) {
        brls::Logger::error("Failed to load tags for game: {}", title);
        return;
    }

    for (const auto& image : json.at("_aPreviewMedia").at("_aImages")) {
        if (image.at("_sType").get<std::string>() == "banner") {
            bannerURL = image.at("_sUrl").get<std::string>();
            break;
        }
    }

    int index = 0;
    for(auto tag : json.at("_aModRootCategories")) {
        categories.push_back(Category(tag.at("_sName").get<std::string>(), tag.at("_idRow").get<int>(), index));
        index++;
    }
}