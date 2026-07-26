#include "gui/screens/ui_setting_page.h"

#include "gui/gui_common.h"

namespace {
void init() {}
void enter() {}
void exit() {}
bool key_consume(const KeyEvent &) { return false; }
bool service() { return false; }
bool update_status(const PlayerStatus &) { return false; }

void render() {
    gui_draw_header("SETTING");
    gui_draw_text(120, 82, "Settings reserved");
}

GuiPageDescriptor descriptor = {
    UiPage::Setting, init, enter, exit, key_consume, service, update_status, render,
    "setting", true, false,
};
}

GuiPageDescriptor &ui_setting_page_descriptor() {
    return descriptor;
}
