#include "gui/screens/ui_read_page.h"

#include "gui/gui_common.h"

namespace {
void init() {}
void enter() {}
void exit() {}
bool key_consume(const KeyEvent &) { return false; }
bool service() { return false; }
bool update_status(const PlayerStatus &) { return false; }

void render() {
    gui_draw_header("READ");
    gui_draw_text(126, 82, "Reader reserved");
}

GuiPageDescriptor descriptor = {
    UiPage::Read, init, enter, exit, key_consume, service, update_status, render,
    "read", true, false,
};
}

GuiPageDescriptor &ui_read_page_descriptor() {
    return descriptor;
}
