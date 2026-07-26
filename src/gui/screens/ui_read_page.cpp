#include "gui/screens/ui_read_page.h"

#include "gui/egui_port.h"
#include "gui/gui_common.h"

namespace {
GuiEguiView page_view;

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "READ");
    egui_canvas_draw_rectangle(canvas, 119, 49, 146, 70, 2,
                               EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    gui_draw_text(canvas, 137, 77, "Reader reserved");
}

void init() { gui_egui_view_init(&page_view, egui_port_core(), draw); }
void enter() {}
void exit() {}
bool key_consume(const KeyEvent &) { return false; }
bool service() { return false; }
bool update_status(const PlayerStatus &) { return false; }

GuiPageDescriptor descriptor = {
    UiPage::Read, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&page_view), "read", true, false,
};
}

GuiPageDescriptor &ui_read_page_descriptor() {
    return descriptor;
}
