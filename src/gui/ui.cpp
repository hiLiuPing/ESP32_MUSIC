#include "gui/ui.h"

#include <Arduino.h>

#include "gui/page_manager.h"
#include "gui/screens/ui_boot_page.h"
#include "gui/screens/ui_home_page.h"
#include "gui/screens/ui_music_page.h"
#include "gui/screens/ui_read_page.h"
#include "gui/screens/ui_setting_page.h"
#include "gui/screens/ui_weather_page.h"
#include "gui/screens/ui_poetry_page.h"
#include "gui/ui_popups.h"
#include "app/settings_app.h"

void gui_init() {
    gui_page_manager_init();

    GuiPageDescriptor *pages[] = {
        &ui_boot_page_descriptor(),
        &ui_home_page_descriptor(),
        &ui_music_page_descriptor(),
        &ui_read_page_descriptor(),
        &ui_setting_page_descriptor(),
        &ui_weather_page_descriptor(),
        &ui_poetry_page_descriptor(),
    };
    for (GuiPageDescriptor *page : pages) {
        if (!gui_page_manager_register(page)) {
            Serial.printf("[GUI] failed to register page %s\n", page->name);
            abort();
        }
    }
    if (!gui_page_manager_load(UiPage::Boot)) {
        Serial.println("[GUI] failed to load boot page");
        abort();
    }
    settings_app_init();
    ui_popups_init();
}
