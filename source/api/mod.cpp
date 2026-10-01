#include "api/mod.hpp"
#include "api/net.hpp"
#include "utils/utils.hpp"
#include "custom_mod_loader.hpp"
#include "utils/version_check.hpp"

Mod::Mod(const std::string &name, int ID, const std::vector<std::string>& imageUrls, const std::string &author, const Game& game): game(game) {
    this->name = name;
    this->ID = ID;
    this->imageUrls = imageUrls;
    this->author = author;
}

std::vector<unsigned char> Mod::downloadImage(const int& index) {
    std::vector<unsigned char> buffer;

    std::string imageUrl;
    if (imageUrls[index].find("http") == 0) {
        imageUrl = imageUrls[index];
    } else {
        imageUrl = fmt::format("https://images.gamebanana.com/img/ss/mods/{}", imageUrls[index]);
    }

    net::downloadImage(imageUrl, buffer);
       
    if(buffer.size() <= 0) {
        brls::Logger::error("Failed to download image: {}", imageUrls[index]);
        return buffer;
    } 
    imageBuffer = buffer;
    return buffer;
}

void Mod::loadImage(const int& index) {
    std::vector<unsigned char> buffer = imageBuffer;

    if(buffer.size() <= 0) {
        brls::Logger::error("Failed to download image: {}", imageUrls[index]);
        return;
    }
    auto image = new brls::Image();
    image->setImageFromMem(buffer.data(), buffer.size());
    images.push_back(image);
    imageBuffers.push_back(buffer);
    imageBuffer.clear();
}

brls::Image* Mod::getImage(const int& index) {
    return images[index];
}

void Mod::loadMod() {
    // MODIFIED: Check for ID == -1 (assigned to custom mods) instead of hardcoded TID
    if (this->ID == -1) {
        brls::Logger::info("Custom Ukrainian translation mod - skipping GameBanana API");
        return;
    }

    try {
        nlohmann::json mod_json = net::downloadRequest(fmt::format("https://gamebanana.com/apiv11/Mod/{}?_csvProperties=_sText,_aFiles,_aPreviewMedia", std::to_string(this->ID)));

        this->description = mod_json.at("_sText");
        this->description = utils::removeHtmlTags(this->description);

        for(auto file : mod_json.at("_aFiles")) {
            std::string name = file.at("_sFile");
            std::string url = file.at("_sDownloadUrl");
            int size = file.at("_nFilesize");
            std::string checkSum = file.at("_sMd5Checksum");
            int date = file.at("_tsDateAdded");
            std::string id = std::to_string(file.at("_idRow").get<int>());

            brls::Logger::debug("File details: Name: {}, URL: {}, Size: {}, Checksum: {}, Date: {}, ID: {}", name, url, size, checkSum, date, id);

            files.push_back(File(name, size, url, checkSum, this->getName(), date, id, game));
        }

        for(auto image : mod_json.at("_aPreviewMedia").at("_aImages")) {
            std::string url = image.at("_sFile");
            brls::Logger::debug("Image URL: {}", url);
            imageUrls.push_back(url);
        }
    } catch (const std::exception& e) {
        brls::Logger::error("Error in loadMod: {}", e.what());
    }
}

File::File(const std::string &name, const int &size, const std::string &url, const std::string &checkSum, const std::string& modName,const int& date, const std::string& fileID,const Game& game): game(game) {            
    this->name = name;
    this->size = size;
    this->url = url;
    this->checkSum = checkSum;
    this->date = date;
    this->path = fmt::format("sdmc:/config/UAModDownloader/{}", name);
    this->modName = modName;
    this->fileID = fileID;
}

bool File::findRomfsRecursive(const nlohmann::json& obj) {
    for (const auto& item : obj.items()) {
        if (item.key() == "romfs" || item.key() == "exefs" || item.key() == "exefs_patches"){
            brls::Logger::debug("found romfs");
            return true;
        }

        if (item.value().is_object()) {
            if (findRomfsRecursive(item.value())) {
                return true;
            }
        }
    }

    return false;
}

void File::loadFile() {
    // MODIFIED: Check for fileID == "-1" (assigned to custom files)
    if (this->fileID == "-1") {
        this->romfs = true;
        brls::Logger::info("Custom mod - assuming romfs structure");
        return;
    }

    auto json = net::downloadRequest(fmt::format("https://gamebanana.com/apiv11/File/{}", fileID));
    nlohmann::json archiveFileTree = json;
    
    if (json.contains("_aArchiveFileTree") && json["_aArchiveFileTree"].is_object()) {
        archiveFileTree = json["_aArchiveFileTree"];
    } else if (json.contains("_aMetadata") &&
               json["_aMetadata"].contains("_aArchiveFileTree") &&
               json["_aMetadata"]["_aArchiveFileTree"].is_object()) {
        archiveFileTree = json["_aMetadata"]["_aArchiveFileTree"];
    } else {
        brls::Logger::error("Could not find correct JSON object: {}", json.dump(2));
        return;
    }

    for (const auto& item : archiveFileTree.items()) {
        if (item.value().is_object()) {
            if (findRomfsRecursive(item.value())) {
                this->romfs = true;
                break;
            }
        }
    }
}

ModList::ModList(Game m_game): game(m_game) {
    updatePage();
}

std::string fixGitHubImageUrl(const std::string& url) {
    std::string fixed = url;
    size_t blobPos = fixed.find("/blob/");
    if (blobPos != std::string::npos) {
        fixed.replace(blobPos, 6, "/raw/");
    }
    return fixed;
}

std::string fixGoogleDriveUrl(const std::string& url) {
    size_t pos = url.find("/file/d/");
    if (pos != std::string::npos) {
        pos += 8;
        size_t endPos = url.find("/", pos);
        if (endPos != std::string::npos) {
            std::string fileId = url.substr(pos, endPos - pos);
            return "https://drive.google.com/uc?export=download&id=" + fileId;
        }
    }
    return url;
}

void ModList::updatePage() {
    
    nlohmann::json mod_json;
    
    brls::Logger::info("========================================");
    brls::Logger::info("ModList::updatePage called");
    brls::Logger::info("Game TID: '{}'", game.getTid());
    brls::Logger::info("========================================");
    
    // --- CUSTOM UKRAINIAN API LOGIC ---
    if (game.getGamebananaID() == 1) {
        this->mods.clear();

#ifdef BOTW_BUILD
        Mod newMod("Українська локалізація", -1, {}, "спільнота короків", game);
        newMod.description =
            "Фанатський переклад The Legend of Zelda: Breath of the Wild українською мовою.\n\n"
            "Автор: спільнота короків\n\n"
            "Файл буде встановлено у:\n"
            "sdmc:/atmosphere/contents/01007EF00011E000/romfs/Pack/Bootup_EUen.pack";

        File botwFile(
            "Bootup_EUen.pack",
            0,
            "https://raw.githubusercontent.com/jgavrus/BOTW_ua_translate/main/compiled_paks/switch/01007EF00011E000/romfs/Pack/Bootup_EUen.pack",
            "",
            "Українська локалізація",
            0,
            "-1",
            game
        );
        botwFile.romfs = true;
        newMod.files.push_back(botwFile);
        mods.push_back(newMod);
        return;
#endif

        // 1. Build the API URL specifically for this Title ID
        std::string apiUrl = "https://swuk.com.ua/wp-json/custom-api/v1/title-translations?titleId=" + game.getTid();
        brls::Logger::info("Fetching mods from API: {}", apiUrl);

        try {
            // 2. Perform Network Request
            auto response = net::downloadRequest(apiUrl);
            
            if (response.empty() || !response.is_array()) {
                brls::Logger::warning("API returned empty or invalid JSON for TID: {}", game.getTid());
                return;
            }

            brls::Logger::info("Loaded {} translation(s) from API", response.size());
            
            for (const auto& entry : response) {
                // 1. Helper to safely extract strings
                auto getSafeString = [](const nlohmann::json& j, const std::string& key, const std::string& fallback) {
                    return (j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : fallback;
                };

                // 2. Extract Basic Info safely
                std::string authorName = getSafeString(entry, "author_name", "Unknown");
                std::string portAuthor = getSafeString(entry, "port_author", "");
                std::string descriptionText = getSafeString(entry, "description", "No description provided.");
                
                // Construct a display name
                std::string modName = "Ukrainian Translation (" + authorName + ")";
                
                // 3. Create Mod Object
                Mod newMod(modName, -1, {}, authorName, game);
                newMod.port_author = portAuthor;

                // 4. Extract Author ID (Critical for the next screen)
                if (entry.contains("author_id") && entry["author_id"].is_string()) {
                    newMod.author_id = entry["author_id"].get<std::string>();
                }

                // 5. Build Description & Metadata
                std::string desc = descriptionText;
                desc += "\n\n--- Інформація ---";
                desc += "\nАвтор перекладу: " + authorName;
                
                if (!portAuthor.empty()) {
                    desc += "\nАвтор порту: " + portAuthor;
                }
                
                if (entry.contains("contact") && entry["contact"].is_string()) {
                    desc += "\nКонтакт: " + entry["contact"].get<std::string>();
                }
                if (entry.contains("source") && entry["source"].is_string()) {
                    desc += "\nДжерело: " + entry["source"].get<std::string>();
                }

                // 6. Version Comparison Logic
                if (entry.contains("game_version") && entry["game_version"].is_string()) {
                    std::string modGameVer = entry["game_version"].get<std::string>();
                    newMod.game_version = modGameVer;
                    
                    desc += "\nВерсія гри мода: " + modGameVer;
                    desc += "\n\n--- СУМІСНІСТЬ ---";
                    
                    try {
                        if (version::isGameInstalled(game.getTid())) {
                            std::string installedVer = version::getInstalledGameVersion(game.getTid());
                            if (!installedVer.empty() && installedVer != "0.0.0") {
                                int versionMatch = version::compareVersions(installedVer, modGameVer);
                                if (versionMatch == 0) {
                                    desc += "\n Ваша версія гри: " + installedVer + " (Ідеальна відповідність!)";
                                } else if (versionMatch < 0) {
                                    desc += "\n Ваша версія гри: " + installedVer + " (Потрібне оновлення)";
                                } else {
                                    desc += "\n Ваша версія гри: " + installedVer + " (Новіша за мод)";
                                }
                            } else {
                                desc += "\n Game Installed: Yes (Version Unknown)";
                            }
                        } else {
                            desc += "\n Game Not Installed";
                        }
                    } catch (const std::exception& e) {
                        brls::Logger::error("Version check failed: {}", e.what());
                    }
                    desc += "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━";
                }
                
                newMod.description = desc;

                // 7. Handle File / Download Link (only if available)
                // No need to load download_url on mod_list view - it will be loaded in preview if needed
                if (entry.contains("download_url") && entry["download_url"].is_string()) {
                    std::string downloadUrl = entry["download_url"].get<std::string>();
                    
                    // Use default values if filename or size are missing/null
                    std::string fileName = getSafeString(entry, "download_filename", "translation.zip");
                    
                    int fileSize = 0;
                    if (entry.contains("file_size") && entry["file_size"].is_number()) {
                        fileSize = entry["file_size"].get<int>() * 1024 * 1024;
                    }
                    
                    File file(fileName, fileSize, downloadUrl, "", modName, 0, "-1", game);
                    file.romfs = true;
                    newMod.files.push_back(file);
                }

                mods.push_back(newMod);
            }
                        
                        return; // Exit successfully
                        
                    } catch (const std::exception& e) {
                        brls::Logger::error("Error loading translations from API: {}", e.what());
                        brls::Application::notify("Не вдалося завантажити дані з API.");
                        return;
                    }
                }
                
                // --- ORIGINAL GAMEBANANA LOGIC BELOW (Unchanged) ---
                if (currentCategory.getName() != "") {
                    mod_json = net::downloadRequest(fmt::format("https://gamebanana.com/apiv11/Mod/Index?_nPerpage=15&_aFilters[Generic_Category]={}&_nPage={}", currentCategory.getID(), currentPage));
                }
                else if(currentSearch == "") {
                    mod_json = net::downloadRequest(fmt::format("https://gamebanana.com/apiv11/Game/{}/Subfeed?_nPage={}?_nPerpage=50&_csvModelInclusions=Mod", game.getGamebananaID(), currentPage));
                } 
                else {
                    mod_json = net::downloadRequest(fmt::format("https://gamebanana.com/apiv11/Game/{}/Subfeed?_nPage={}&_nPerpage=50&_sName={}&_csvModelInclusions=Mod", game.getGamebananaID(), currentPage, currentSearch));
                }

                if(mod_json.empty()) {
                    return;
                }

                mods.clear();
                
                for(auto mod : mod_json.at("_aRecords")) {
                    std::string name = mod.at("_sName");
                    int ID = mod.at("_idRow");
                    
                    std::string author = mod.at("_aSubmitter").at("_sName");

                    std::vector<std::string> images;

                    Mod newMod(name, ID, images, author, game);

                    mods.push_back(newMod);
                }
            }

void ModList::nextPage() {
    currentPage++;
    updatePage();
}

void ModList::previousPage() {
    if(currentPage > 1) {
        currentPage--;
        updatePage();
    }
}

void ModList::search(const std::string& search) {
    if(search.size() < 3) {
        return;
    }
    this->currentSearch = search;
    currentPage = 1;
    updatePage();
}

void ModList::setCategory(const Category& category) {
    this->currentCategory = category;
    currentPage = 1;
    updatePage();
}