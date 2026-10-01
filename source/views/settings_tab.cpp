#include "views/settings_tab.hpp"
#include "utils/config.hpp"

#include <filesystem>

using namespace brls::literals;

SettingsTab::SettingsTab() {
    this->inflateFromXMLRes("xml/tabs/settings_tab.xml");
    cfg::Config config;

    // REMOVED: Language selector code (lines 8-30)
    
    this->strict_cell->init("menu/settings_tab/strict"_i18n, config.getStrictSearch(), [](bool value){
        cfg::Config config;
        config.setStringSearch(value);
    });

    #ifndef NDEBUG
    auto debug_cell = new brls::BooleanCell();
    debug_cell->init("menu/settings_tab/debug"_i18n, brls::Application::isDebuggingViewEnabled(), [](bool value){
        brls::Application::enableDebuggingView(value);
        brls::sync([value](){
            brls::Logger::info("{} the debug layer", value ? "Open" : "Close");
        });
    });
    settings_box->addView(debug_cell);

    auto wireframe_cell = new brls::BooleanCell();
    wireframe_cell->init("Wireframe", config.getWireframe(), [](bool value){
        brls::sync([value](){
            brls::Logger::info("{} wireframe", value ? "Enable" : "Disable");
        });
        cfg::Config config;
        config.setWireframe(value);
    });
    settings_box->addView(wireframe_cell);

    if (config.getWireframe()) {
        brls::sync([this]{
            this->setWireframeEnabled(true);
            for (auto& view : this->getChildren()) {
                view->setWireframeEnabled(true);
            }
            for (auto& view : this->settings_box->getChildren()) {
                view->setWireframeEnabled(true);
            }
        });
    }
    #endif
}

brls::View* SettingsTab::create()
{
    return new SettingsTab;
}