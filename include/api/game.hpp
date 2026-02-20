#pragma once 

#include <string>
#include <borealis.hpp>
#include <nlohmann/json.hpp>

class Category {
public:
    Category(const std::string& name, const int& ID, const int& index) : name(name), ID(ID), index(index) {}
    Category() {}
    std::string getName() { return name; }
    int getID() { return ID; }
    int getIndex() { return index; }
private:
    std::string name = "";
    int ID = 0;
    int index = 0;
};

class Game {
    public:
        Game(const std::string& title, const std::string& tid);
        Game() {}
        
        std::string getTitle() const { return title; }
        int getSwukID() { return swukID; }
        std::string getTid() const { return tid; }
        brls::Image* getIcon() { return icon; }
        std::string getBannerUrl() { return bannerURL; }
        std::vector<Category> getCategories() { return categories; }
    private:
        void loadCategories();
        void searchGame();
        void parseJson();

        nlohmann::json json;
        nlohmann::json json_categories;

        std::string title;
        std::string bannerURL;
        int swukID = 0;
        std::string tid;
        std::vector<Category> categories;
        brls::Image* icon;
};