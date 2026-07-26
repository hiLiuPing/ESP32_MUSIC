#include "gui/screens/ui_setting_page.h"

#include "gui/egui_port.h"
#include "gui/gui_common.h"

namespace {
GuiEguiView page_view;

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "SETTING");
}

void init() {
    gui_egui_view_init(&page_view, egui_port_core(), draw);
}

void enter() {
    egui_view_invalidate_full(EGUI_VIEW_OF(&page_view));
}

void exit() {}

bool key_consume(const KeyEvent &) { return false; }

bool service() { return false; }
bool update_status(const PlayerStatus &) { return false; }

GuiPageDescriptor descriptor = {
    UiPage::Setting, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&page_view), "setting", true, false,
};
}

GuiPageDescriptor &ui_setting_page_descriptor() {
    return descriptor;
}
