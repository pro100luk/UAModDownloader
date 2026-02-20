#include "activity/main_activity.hpp"

using namespace brls::literals;

MainActivity::MainActivity() {
    // Register a custom back action to show exit dialog with correct translation
    this->registerAction(
        "", 
        brls::ControllerButton::BUTTON_B, 
        [](brls::View* view) {
            // Create custom exit dialog
            auto dialog = new brls::Dialog("hints/exit_hint"_i18n);
            
            // Cancel button - don't exit
            dialog->addButton("hints/cancel"_i18n, []() {
                // Do nothing
            });
            
            // Exit button - quit the app
            dialog->addButton("hints/exit"_i18n, []() {
                brls::Application::quit();
            });
            
            dialog->open();
            return true;
        },
        true  // Use true for allow repetition
    );
}
