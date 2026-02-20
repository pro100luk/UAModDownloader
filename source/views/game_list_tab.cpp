#include "views/game_list_tab.hpp"
#include "utils/utils.hpp"
#include "utils/config.hpp"
#include "views/mods_list.hpp"
#include "custom_mod_loader.hpp"

#include <borealis.hpp>
#include <switch.h>

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

    if (game.getSwukID() == 1) {
        brls::Logger::info("Opening Ukrainian translation list for: {}", game.getTitle());
        recycler->present(new ModListTab(game));
        return;
    }

    if (game.getSwukID() == 0) {
        auto dialog = new brls::Dialog("menu/notify/no_games_swuk"_i18n);
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
static std::vector<std::pair<std::string, std::string>> s_cachedGames;

GameListTab::GameListTab()
{
    this->inflateFromXMLRes("xml/tabs/game_list_tab.xml");

    recycler->estimatedRowHeight = 100;
    recycler->registerCell("Cell", []() { return GameCell::create(); });

    // If already loaded, skip spinner and show data immediately
    if (s_cacheLoaded) {
        
        loading_spinner->setVisibility(brls::Visibility::GONE);

        gameData = new GameData(s_cachedGames);
        recycler->setDataSource(gameData, false);
        recycler->setVisibility(brls::Visibility::VISIBLE);

        brls::Logger::debug("GameListTab: using cached {} game(s)", s_cachedGames.size());
        return;
    }

    // Show spinner, hide list
    loading_spinner->setVisibility(brls::Visibility::VISIBLE);
    recycler->setVisibility(brls::Visibility::GONE);

    std::shared_ptr<bool> aliveFlag = this->alive;
    GameListTab* self = this;

    // Step 1 — background thread: network fetch ONLY
    brls::async([aliveFlag, self]() {
        std::vector<CustomModLoader::ModInfo> customMods;

        try {
            customMods = CustomModLoader::DataLoader::fetchAndParse();
        } catch (const std::exception& e) {
            brls::Logger::error("GameListTab: fetchAndParse failed: {}", e.what());
        }

        // Step 2 — main thread: icon lookups via ns/pl are safe here
        brls::sync([aliveFlag, customMods, self]() {
            std::map<std::string, std::string> uniqueGames;
            for (const auto& mod : customMods) {
                // Skip games that are not actually installed (save data leftovers
                // still have icons via pl:u, so we use ns to confirm installation)
                u64 tid = std::stoull(mod.id, nullptr, 16);
                NsApplicationRecord record;
                bool isInstalled = false;
                s32 outCount = 0;
                // Walk ns application records to find a matching title ID
                for (s32 offset = 0; ; offset += outCount) {
                    outCount = 0;
                    Result rc = nsListApplicationRecord(&record, 1, offset, &outCount);
                    if (R_FAILED(rc) || outCount == 0) break;
                    if (record.application_id == tid) {
                        isInstalled = true;
                        break;
                    }
                }
                if (!isInstalled) continue;

                uint8_t* icon = utils::getIconFromTitleId(mod.id);
                if (icon != nullptr) {
                    if (uniqueGames.find(mod.id) == uniqueGames.end())
                        uniqueGames[mod.id] = mod.name;
                }
            }

            for (const auto& [tid, name] : uniqueGames)
                s_cachedGames.emplace_back(name, tid);

            s_cacheLoaded = true;

            if (s_cachedGames.empty())
                brls::Logger::warning("GameListTab: no installed games found in API response");

            // View was destroyed while loading — cache is saved, skip UI update
            if (!*aliveFlag) {
                brls::Logger::debug("GameListTab: view gone, cache stored for next visit");
                return;
            }

            self->loading_spinner->setVisibility(brls::Visibility::GONE);
            self->gameData = new GameData(s_cachedGames);
            self->recycler->setDataSource(self->gameData, false);
            self->recycler->setVisibility(brls::Visibility::VISIBLE);

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

GameListTab::~GameListTab()
{
    *alive = false;
}

brls::View* GameListTab::create()
{
    return new GameListTab();
}
