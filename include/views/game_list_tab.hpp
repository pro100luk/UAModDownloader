#pragma once

#include <borealis.hpp>
#include <memory>

class GameCell : public brls::RecyclerCell
{
  public:
    GameCell();

    BRLS_BIND(brls::Rectangle, accent, "brls/sidebar/item_accent");
    BRLS_BIND(brls::Label, label, "title");
    BRLS_BIND(brls::Label, subtitle, "subtitle");
    BRLS_BIND(brls::Image, image, "image");

    static GameCell* create();
};

class GameData
    : public brls::RecyclerDataSource
{
  public:
    GameData();
    explicit GameData(const std::vector<std::pair<std::string, std::string>>& preloadedGames);
    int numberOfSections(brls::RecyclerFrame* recycler) override;
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) override;
    std::string titleForHeader(brls::RecyclerFrame* recycler, int section) override;
  private:
    std::vector<std::pair<std::string, std::string>> games;
};

class GameListTab : public brls::Box {
public:
    GameListTab();
    ~GameListTab();

    static brls::View* create();
private:
    BRLS_BIND(brls::RecyclerFrame, recycler,        "recycler");
    BRLS_BIND(brls::Box,           loading_spinner, "loading_spinner");

    GameData* gameData = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
