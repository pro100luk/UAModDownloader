#include "views/installation_view.hpp"
#include "views/mods_list.hpp"
#include "api/game.hpp"
#include "api/net.hpp"
#include "utils/config.hpp"

#include <borealis.hpp>

using namespace brls::literals;

// ─────────────────────────────────────────────
// TitleCell
// ─────────────────────────────────────────────

TitleCell::TitleCell()
{
    this->inflateFromXMLRes("xml/cells/title_cell.xml");
}

TitleCell* TitleCell::create()
{
    return new TitleCell();
}

// ─────────────────────────────────────────────
// TitleData
// ─────────────────────────────────────────────

brls::RecyclerCell* TitleData::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath indexPath)
{
    auto cell = (TitleCell*)recycler->dequeueReusableCell("TitleCell");
    if (cell && cell->titleLabel && indexPath.row < (int)titles.size()) {
        cell->titleLabel->setText(titles[indexPath.row].first);
        brls::Logger::debug("Set title for cell {}: {}", indexPath.row, titles[indexPath.row].first);
    } else {
        brls::Logger::error("Failed to set up cell: row={}, size={}", indexPath.row, titles.size());
    }
    return cell;
}

void TitleData::didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath)
{
    const auto& [name, titleId] = titles[indexPath.row];
    brls::Logger::debug("Selected title: {} ({})", name, titleId);
    Game game(name, titleId);
    recycler->present(new ModListTab(game, true));
}

int TitleData::numberOfSections(brls::RecyclerFrame* recycler) { return 1; }

int TitleData::numberOfRows(brls::RecyclerFrame* recycler, int section)
{
    return (int)titles.size();
}

std::string TitleData::titleForHeader(brls::RecyclerFrame* recycler, int section) { return ""; }

TitleData::TitleData(const std::vector<std::pair<std::string, std::string>>& preloadedTitles)
{
    this->titles = preloadedTitles;
}

// ─────────────────────────────────────────────
// InstallationView
// ─────────────────────────────────────────────

static bool s_titlesLoaded = false;
static std::vector<std::pair<std::string, std::string>> s_cachedTitles; // (name, title_id)

InstallationView::InstallationView()
{
    this->inflateFromXMLRes("xml/tabs/installation_tab.xml");

    recycler->estimatedRowHeight = 50;
    recycler->registerCell("TitleCell", []() { return TitleCell::create(); });

    if (s_titlesLoaded) {
        loading_spinner->setVisibility(brls::Visibility::GONE);
        titleData = new TitleData(s_cachedTitles);
        recycler->setDataSource(titleData, false);
        recycler->setVisibility(brls::Visibility::VISIBLE);
        brls::Logger::debug("InstallationView: using cached {} title(s)", s_cachedTitles.size());
        return;
    }

    loading_spinner->setVisibility(brls::Visibility::VISIBLE);
    recycler->setVisibility(brls::Visibility::GONE);

    // Capture alive flag and raw pointer — aliveFlag gates whether self is safe to use
    std::shared_ptr<bool> aliveFlag = this->alive;
    InstallationView* self = this;

    brls::async([aliveFlag, self]() {
        std::vector<std::pair<std::string, std::string>> loadedTitles;

        try {
            brls::Logger::debug("Fetching titles from API");
            auto response = net::downloadRequest("https://swuk.com.ua/wp-json/custom-api/v1/titles");

            auto parseTitles = [&](const nlohmann::json& arr) {
                for (const auto& item : arr) {
                    if (item.contains("name") && item["name"].is_string() &&
                        item.contains("title_id") && item["title_id"].is_string()) {
                        std::string name    = item["name"].get<std::string>();
                        std::string titleId = item["title_id"].get<std::string>();
                        if (!name.empty() && !titleId.empty())
                            loadedTitles.emplace_back(name, titleId);
                    }
                }
            };

            if (response.is_array())
                parseTitles(response);
            else if (response.contains("data") && response["data"].is_array())
                parseTitles(response["data"]);

            std::sort(loadedTitles.begin(), loadedTitles.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
            brls::Logger::debug("Found {} titles after filtering", loadedTitles.size());

        } catch (const std::exception& e) {
            brls::Logger::error("InstallationView: fetch failed: {}", e.what());
        }

        brls::sync([aliveFlag, loadedTitles, self]() {
            s_cachedTitles = loadedTitles;
            s_titlesLoaded = true;

            // View was destroyed while loading — cache is saved, skip UI update
            if (!*aliveFlag) {
                brls::Logger::debug("InstallationView: view gone, cache stored for next visit");
                return;
            }

            self->loading_spinner->setVisibility(brls::Visibility::GONE);
            self->titleData = new TitleData(s_cachedTitles);
            self->recycler->setDataSource(self->titleData, false);
            self->recycler->setVisibility(brls::Visibility::VISIBLE);

            brls::Logger::debug("InstallationView: loaded {} title(s)", s_cachedTitles.size());
        });
    });

#ifndef NDEBUG
    cfg::Config config;
    if (config.getWireframe()) {
        this->setWireframeEnabled(true);
        for (auto& view : this->getChildren())
            view->setWireframeEnabled(true);
    }
#endif
}

InstallationView::~InstallationView()
{
    *alive = false;
}
