#pragma once

#include <borealis.hpp>
#include <vector>
#include <algorithm>
#include <memory>

class TitleCell : public brls::RecyclerCell
{
public:
    TitleCell();

    BRLS_BIND(brls::Label, titleLabel, "title");

    static TitleCell* create();
};

class TitleData : public brls::RecyclerDataSource
{
public:
    explicit TitleData(const std::vector<std::pair<std::string, std::string>>& preloadedTitles);
    int numberOfSections(brls::RecyclerFrame* recycler) override;
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) override;
    std::string titleForHeader(brls::RecyclerFrame* recycler, int section) override;

private:
    std::vector<std::pair<std::string, std::string>> titles; // (name, title_id)
};

class InstallationView : public brls::Box {
public:
    InstallationView();
    ~InstallationView();

    static brls::View* create() {
        return new InstallationView();
    }
private:
    BRLS_BIND(brls::RecyclerFrame, recycler,        "recycler");
    BRLS_BIND(brls::Box,           loading_spinner, "loading_spinner");

    TitleData* titleData = nullptr;

    // Shared flag — set to false in destructor so in-flight async callbacks
    // know not to touch this view's members after it's been destroyed
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
