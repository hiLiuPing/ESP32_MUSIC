#include "gui/screens/ui_boot_page.h"

#include <Arduino.h>
#include <cstdio>

#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/page_manager.h"
#include "task/task_system.h"

namespace {
GuiEguiView page_view;
uint32_t started_ms = 0;
uint32_t frame = 0;

void draw(egui_canvas_t *canvas) {
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    char progress[32];
    const uint8_t completed = ((bits & HW_EVENT_DISPLAY_READY) ? 1 : 0) +
                              ((bits & HW_EVENT_SD_READY) ? 1 : 0) +
                              ((bits & HW_EVENT_CODEC_READY) ? 1 : 0);
    std::snprintf(progress, sizeof(progress), "Hardware %u/3", completed);

    gui_draw_page_background(canvas);
    gui_draw_text(canvas, 126, 48, "ESP32-S3 MUSIC");
    egui_canvas_draw_rectangle(canvas, 91, 76, 202, 16, 1,
                               EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    const int16_t fill_width = static_cast<int16_t>((frame % 17U) * 12U);
    if (fill_width > 0) {
        egui_canvas_draw_rectangle_fill(canvas, 94, 79, fill_width, 10,
                                        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
    gui_draw_text(canvas, 145, 108, progress);
    if ((bits & HW_EVENT_INIT_DONE) && !(bits & HW_EVENT_SD_READY)) {
        gui_draw_text(canvas, 139, 135, "SD unavailable");
    }
}

void init() {
    started_ms = 0;
    frame = 0;
    gui_egui_view_init(&page_view, egui_port_core(), draw);
}

void enter() {
    started_ms = millis();
    frame = 0;
}

void exit() {}

bool key_consume(const KeyEvent &) {
    return true;
}

bool service() {
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    if (((millis() - started_ms) >= 1200U) && ((bits & HW_EVENT_INIT_DONE) != 0)) {
        (void)gui_page_manager_load(UiPage::Home);
        return false;
    }
    ++frame;
    return true;
}

bool update_status(const PlayerStatus &) {
    return false;
}

GuiPageDescriptor descriptor = {
    UiPage::Boot, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&page_view), "boot", false, false,
};
}

GuiPageDescriptor &ui_boot_page_descriptor() {
    return descriptor;
}
