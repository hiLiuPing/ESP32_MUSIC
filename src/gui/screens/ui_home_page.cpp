#include "gui/screens/ui_home_page.h"

#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "task/task_system.h"

namespace {
GuiEguiView page_view;

void draw_icon(egui_canvas_t *canvas, int16_t x, uint8_t index) {
    egui_canvas_draw_rectangle(canvas, x, 38, 72, 72, 2,
                               EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    if (index == 0) {
        egui_canvas_draw_rectangle(canvas, x + 21, 55, 30, 34, 2,
                                   EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, x + 17, 58, x + 36, 46, 2,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, x + 36, 46, x + 55, 58, 2,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    } else if (index == 1) {
        egui_canvas_draw_line(canvas, x + 38, 51, x + 38, 85, 3,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, x + 38, 51, x + 56, 47, 3,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle_fill(canvas, x + 25, 81, 13, 8,
                                        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle_fill(canvas, x + 44, 75, 13, 8,
                                        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    } else if (index == 2) {
        egui_canvas_draw_rectangle(canvas, x + 15, 51, 42, 42, 2,
                                   EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, x + 36, 51, x + 36, 93, 1,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    } else {
        egui_canvas_draw_rectangle(canvas, x + 22, 55, 28, 28, 3,
                                   EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle_fill(canvas, x + 31, 64, 10, 10,
                                        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
}

void draw(egui_canvas_t *canvas) {
    static const int16_t xs[] = {8, 102, 196, 290};
    static const char *labels[] = {"HOME", "MUSIC", "READ", "SETTING"};

    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "HOME");
    for (uint8_t index = 0; index < 4; ++index) {
        draw_icon(canvas, xs[index], index);
        gui_draw_text(canvas, xs[index] + 12, 119, labels[index]);
    }

    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    if (!(bits & HW_EVENT_SD_READY)) {
        gui_draw_text(canvas, 8, 150, "SD ERROR");
    } else if (!(bits & HW_EVENT_CODEC_READY)) {
        gui_draw_text(canvas, 8, 150, "CODEC ERROR");
    } else {
        gui_draw_text(canvas, 8, 150, "SYSTEM READY");
    }
}

void init() {
    gui_egui_view_init(&page_view, egui_port_core(), draw);
}
void enter() {}
void exit() {}
bool key_consume(const KeyEvent &) { return false; }
bool service() { return false; }
bool update_status(const PlayerStatus &) { return false; }

GuiPageDescriptor descriptor = {
    UiPage::Home, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&page_view), "home", true, false,
};
}

GuiPageDescriptor &ui_home_page_descriptor() {
    return descriptor;
}
