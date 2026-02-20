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
        // Fallback for SWUK images or relative URLs
        imageUrl = fmt::format("https://swuk.com.ua/images/{}", imageUrls[index]);
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
    // Only load custom Ukrainian mods, no SWUK fallback
    if (this->ID == -1) {
        brls::Logger::info("Custom Ukrainian translation mod - skipping API call");
        return;
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
    // Only support custom mods with assumed romfs structure
    if (this->fileID == "-1") {
        this->romfs = true;
        brls::Logger::info("Custom mod - assuming romfs structure");
        return;
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
    
    brls::Logger::info("========================================");
    brls::Logger::info("ModList::updatePage called");
    brls::Logger::info("Game TID: '{}'", game.getTid());
    brls::Logger::info("========================================");
    
    // Load custom Ukrainian API only
    if (game.getSwukID() == 1) {
        this->mods.clear();
        
        std::string apiUrl = "https://swuk.com.ua/wp-json/custom-api/v1/title-translations?titleId=" + game.getTid();
        brls::Logger::info("Fetching mods from SWUK API: {}", apiUrl);

        try {
            auto response = net::downloadRequest(apiUrl);
            
            if (response.empty() || !response.is_array()) {
                brls::Logger::warning("API returned empty or invalid JSON for TID: {}", game.getTid());
                return;
            }

            brls::Logger::info("Loaded {} translation(s) from SWUK API", response.size());
            
            for (const auto& entry : response) {
                auto getSafeString = [](const nlohmann::json& j, const std::string& key, const std::string& fallback) {
                    return (j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : fallback;
                };

                std::string authorName = getSafeString(entry, "author_name", "Unknown");
                std::string portAuthor = getSafeString(entry, "port_author", "");
                std::string descriptionText = getSafeString(entry, "description", "No description provided.");
                
                std::string modName = "Ukrainian Translation (" + authorName + ")";
                
                Mod newMod(modName, -1, {}, authorName, game);
                newMod.port_author = portAuthor;

                if (entry.contains("author_id") && entry["author_id"].is_string()) {
                    newMod.author_id = entry["author_id"].get<std::string>();
                }

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

                if (entry.contains("download_url") && entry["download_url"].is_string()) {
                    std::string downloadUrl = entry["download_url"].get<std::string>();
                    
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
                        
                        return;
                        
                    } catch (const std::exception& e) {
                        brls::Logger::error("Error loading translations from SWUK API: {}", e.what());
                        brls::Application::notify("Не вдалося завантажити дані з API.");
                        return;
                    }
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