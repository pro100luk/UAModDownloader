#include "views/game_list_tab.hpp"
#include "utils/utils.hpp"
#include "utils/config.hpp"
#include "views/mods_list.hpp"
#include "custom_mod_loader.hpp"
#include "api/net.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <set>

using namespace brls::literals;

// ─────────────────────────────────────────────
// GameCell
// ─────────────────────────────────────────────

GameCell::GameCell()
{
    this->inflateFromXMLRes("xml/cells/cell.xml");
}

GameCell* GameCell::create()
{
    return new GameCell();
}

// ─────────────────────────────────────────────
// GameData
// ─────────────────────────────────────────────

brls::RecyclerCell* GameData::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath indexPath)
{
    auto cell = (GameCell*)recycler->dequeueReusableCell("Cell");

    std::string gameName = games[indexPath.row].first;
    std::string titleId  = games[indexPath.row].second;

    cell->label->setText(gameName);
    cell->subtitle->setText(fmt::format("TitleID: {}", titleId));

    uint8_t* icon = utils::getIconFromTitleId(titleId);
    if (icon != nullptr)
        cell->image->setImageFromMem(icon, 0x20000);

    return cell;
}

void GameData::didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath)
{
    Game game(games[indexPath.row].first, games[indexPath.row].second);

    if (game.getGamebananaID() == 1) {
        brls::Logger::info("Opening Ukrainian translation list for: {}", game.getTitle());
        recycler->present(new ModListTab(game));
        return;
    }

    if (game.getGamebananaID() == 0) {
        auto dialog = new brls::Dialog("menu/notify/no_games_gamebanana"_i18n);
        dialog->addButton("hints/select"_i18n, []() {});
        dialog->open();
        return;
    }

    recycler->present(new ModListTab(game));
}

GameData::GameData(const std::vector<std::pair<std::string, std::string>>& preloadedGames)
{
    this->games = preloadedGames;
}

int GameData::numberOfSections(brls::RecyclerFrame* recycler) { return 1; }

int GameData::numberOfRows(brls::RecyclerFrame* recycler, int section)
{
    return (int)games.size();
}

std::string GameData::titleForHeader(brls::RecyclerFrame* recycler, int section) { return ""; }

// ─────────────────────────────────────────────
// GameListTab
// ─────────────────────────────────────────────

// Static cache — survives tab switches since GameListTab is recreated each time
static bool s_cacheLoaded = false;
static std::vector<std::pair<std::string, std::string>> s_cachedGames; // API order: oldest first
static SortOrder s_sortOrder = SortOrder::BY_DATE;

static std::vector<std::pair<std::string, std::string>> getSortedGames()
{
    auto games = s_cachedGames;
    if (s_sortOrder == SortOrder::BY_NAME) {
        std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
    } else {
        // BY_DATE: newest first = reverse API order
        std::reverse(games.begin(), games.end());
    }
    return games;
}

GameListTab::GameListTab()
{
    this->inflateFromXMLRes("xml/tabs/game_list_tab.xml");

    recycler->estimatedRowHeight = 100;
    recycler->registerCell("Cell", []() { return GameCell::create(); });

#ifdef BOTW_BUILD
    loading_spinner->setVisibility(brls::Visibility::GONE);
    if (!s_cacheLoaded) {
        s_cachedGames = {{"The Legend of Zelda: Breath of the Wild", "01007EF00011E000"}};
        s_cacheLoaded = true;
    }
    gameData = new GameData(s_cachedGames);
    recycler->setDataSource(gameData, false);
    recycler->setVisibility(brls::Visibility::VISIBLE);
    return;
#endif

    // If already loaded, skip spinner and show data immediately
    if (s_cacheLoaded) {
        loading_spinner->setVisibility(brls::Visibility::GONE);
        showList();
        brls::Logger::debug("GameListTab: using cached {} game(s)", s_cachedGames.size());
        return;
    }

    // Show spinner, hide list
    loading_spinner->setVisibility(brls::Visibility::VISIBLE);
    recycler->setVisibility(brls::Visibility::GONE);

    std::shared_ptr<bool> aliveFlag = this->alive;
    GameListTab* self = this;

    // Step 1 — background thread: connectivity check + network fetch
    brls::async([aliveFlag, self]() {
        if (!net::checkConnection()) {
            brls::Logger::warning("GameListTab: no internet connection");
            brls::sync([aliveFlag, self]() {
                if (!*aliveFlag) return;
                self->loading_spinner->setVisibility(brls::Visibility::GONE);
                self->no_internet_box->setVisibility(brls::Visibility::VISIBLE);
            });
            return;
        }

        std::vector<CustomModLoader::ModInfo> customMods;
        bool fetchFailed = false;

        try {
            customMods = CustomModLoader::DataLoader::fetchAndParse();
        } catch (const std::exception& e) {
            brls::Logger::error("GameListTab: fetchAndParse failed: {}", e.what());
            fetchFailed = true;
        }

        if (fetchFailed) {
            brls::sync([aliveFlag, self]() {
                if (!*aliveFlag) return;
                self->loading_spinner->setVisibility(brls::Visibility::GONE);
                self->no_internet_box->setVisibility(brls::Visibility::VISIBLE);
            });
            return;
        }

        // Step 2 — main thread: icon lookups via ns/pl are safe here
        brls::sync([aliveFlag, customMods, self]() {
            // Build a set of currently installed title IDs from the application record list.
            // nsGetApplicationControlData (used by getIconFromTitleId) can return data for
            // deleted games because the Switch keeps metadata cached after uninstallation.
            // nsListApplicationRecord (used by getInstalledGames) only returns installed games.
            auto installedGames = utils::getInstalledGames();
            std::set<std::string> installedIds;
            for (const auto& [name, tid] : installedGames)
                installedIds.insert(tid);

            // Preserve API order (oldest first) while deduplicating
            std::vector<std::string> seenIds;
            for (const auto& mod : customMods) {
                if (std::find(seenIds.begin(), seenIds.end(), mod.id) != seenIds.end())
                    continue;
                if (installedIds.find(mod.id) == installedIds.end())
                    continue;
                uint8_t* icon = utils::getIconFromTitleId(mod.id);
                if (icon != nullptr) {
                    seenIds.push_back(mod.id);
                    s_cachedGames.emplace_back(mod.name, mod.id);
                }
            }

            s_cacheLoaded = true;

            if (s_cachedGames.empty())
                brls::Logger::warning("GameListTab: no installed games found in API response");

            // View was destroyed while loading — cache is saved, skip UI update
            if (!*aliveFlag) {
                brls::Logger::debug("GameListTab: view gone, cache stored for next visit");
                return;
            }

            self->loading_spinner->setVisibility(brls::Visibility::GONE);
            self->showList();

            brls::Logger::debug("GameListTab: loaded {} game(s)", s_cachedGames.size());
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

void GameListTab::showList()
{
    recycler->setVisibility(brls::Visibility::VISIBLE);

    this->registerAction("menu/label/sort"_i18n, brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        auto dialog = new brls::Dialog("menu/label/sort"_i18n);
        dialog->addButton("menu/label/sort_by_date"_i18n, [this]() {
            s_sortOrder = SortOrder::BY_DATE;
            refreshList();
        });
        dialog->addButton("menu/label/sort_by_name"_i18n, [this]() {
            s_sortOrder = SortOrder::BY_NAME;
            refreshList();
        });
        dialog->open();
        return true;
    });

    refreshList();
}

void GameListTab::refreshList()
{
    gameData = new GameData(getSortedGames());
    recycler->setDataSource(gameData, false);
}

GameListTab::~GameListTab()
{
    *alive = false;
}

brls::View* GameListTab::create()
{
    return new GameListTab();
}
