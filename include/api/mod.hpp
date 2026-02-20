#pragma once

#include <string>
#include <vector>
#include <borealis.hpp>

#include "api/game.hpp"

class File {
    public:
        File(const std::string &name, const int &size, const std::string &url, const std::string &checkSum, const std::string& modName, const int& date, const std::string& fileID, const Game& game);
        void loadFile();
        
        // Getters
        std::string getPath() { return path; }
        std::string getName() { return name; }
        std::string getUrl() { return url; }
        std::string getCheckSum() { return checkSum; }
        int getDate() { return date; }
        int getSize() { return size; }
        Game getGame() { return game; }
        std::string getModName() { return modName; }
        bool getRomfs() { return romfs; }
        std::string getFileID() { return fileID; }
        
        // Version getters and setters
        std::string getGameVersion() { return game_version; }
        std::string getTranslationVersion() { return translation_version; }
        void setGameVersion(const std::string& ver) { game_version = ver; }
        void setTranslationVersion(const std::string& ver) { translation_version = ver; }

        // Keeping this public as per your custom mod requirements
        bool romfs = false; 

    private:
        bool findRomfsRecursive(const nlohmann::json& obj);

        std::string name;
        int size;
        std::string url;
        std::string checkSum;
        int date;
        std::string path;
        const Game& game;
        std::string modName;
        std::string fileID;
        
        // Version information for custom mods
        std::string game_version;
        std::string translation_version;
};

class Mod {
    public:
        Mod(const std::string &name, int ID, const std::vector<std::string>& imageUrls, const std::string &author, const Game& game);

        brls::Image* getImage(const int& index);
        std::vector<unsigned char> getImageBuffer(const int& index) { return imageBuffers[index]; }

        // UI handling
        std::vector<unsigned char> downloadImage(const int& index);
        void loadImage(const int& index);

        // Standard Getters
        const Game& getGame() const { return this->game; }
        std::string getName() { return name; }
        int getID() { return ID; }
        std::string getDescription() { return description; }
        std::vector<File> getFiles() { return files; }
        std::vector<std::string> getImagesUrl() { return imageUrls; }
        std::string getAuthor() { return author; }

        void loadMod();
        
        // --- PUBLIC MEMBERS FOR CUSTOM MOD DATA ---
        // These need to be public so ModList can populate them and ModPreview can read them
        std::string description;           
        std::vector<File> files;             
        std::vector<std::string> imageUrls; 
        std::string author_id;
        std::string port_author;
        std::string game_version;
        
    private:
        std::string name;
        int ID = 0;
        std::string author;
        const Game& game;

        std::vector<brls::Image*> images;
        std::vector<std::vector<unsigned char>> imageBuffers;
        std::vector<unsigned char> imageBuffer;

        // Allows ModList to handle the "custom loading" logic safely
        friend class ModList;
};  

class ModList {
    public:
        ModList(Game game);
        std::vector<Mod> getMods() { return mods; }

        void nextPage();
        void previousPage();
        void search(const std::string& search);
        void setCategory(const Category& category);

        Category getCategory() {return currentCategory;}
        
    private:
        void updatePage();
        
        std::vector<Mod> mods;
        int currentPage = 1;
        std::string currentSearch = "";
        Game game;
        Category currentCategory;
};
