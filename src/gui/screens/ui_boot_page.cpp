#include "gui/screens/ui_boot_page.h"

#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/page_manager.h"
#include "task/task_system.h"

namespace {
GuiEguiView page_view;
uint32_t started_ms = 0;

constexpr uint32_t BOOT_DURATION_MS = 3000U;
constexpr uint32_t BOOT_EXIT_START_MS = 2820U;
constexpr float BOOT_PI = 3.14159265359F;

uint8_t hardware_ready_count(EventBits_t bits) {
    return static_cast<uint8_t>(((bits & HW_EVENT_DISPLAY_READY) ? 1 : 0) +
                                 ((bits & HW_EVENT_SD_READY) ? 1 : 0) +
                                 ((bits & HW_EVENT_CODEC_READY) ? 1 : 0));
}

void draw(egui_canvas_t *canvas) {
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    const uint32_t elapsed = std::min<uint32_t>(millis() - started_ms, BOOT_DURATION_MS);
    const int16_t sweep = static_cast<int16_t>((elapsed / 8U) % 360U);
    const int16_t pulse = static_cast<int16_t>((elapsed / 90U) % 4U);
    const uint8_t completed = hardware_ready_count(bits);
    char progress[32] = {};
    std::snprintf(progress, sizeof(progress), "INITIALIZING %u/3", completed);

    gui_draw_page_background(canvas);
    gui_draw_text(canvas, 136, 18, "ESP32-S3 MUSIC");

    // A monochrome radar-like boot mark: the rotating arc and orbiting dot
    // keep the display visibly alive while the hardware task is working.
    egui_canvas_draw_circle(canvas, 192, 75, 35, 1, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_canvas_draw_circle(canvas, 192, 75, 25, 1, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_canvas_draw_arc_sweep(canvas, 192, 75, 35, sweep, 92, 3,
                                EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_canvas_draw_arc_sweep(canvas, 192, 75, 25, 180 - sweep, 52, 2,
                                EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_canvas_draw_circle_fill(canvas, 192, 75, static_cast<int16_t>(5 + pulse),
                                 EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    const float angle = static_cast<float>(sweep) * BOOT_PI / 180.0F;
    const int16_t orbit_x = static_cast<int16_t>(192 + std::cos(angle) * 31.0F);
    const int16_t orbit_y = static_cast<int16_t>(75 + std::sin(angle) * 31.0F);
    egui_canvas_draw_circle_fill(canvas, orbit_x, orbit_y, 2,
                                 EGUI_COLOR_BLACK, EGUI_ALPHA_100);

    egui_canvas_draw_round_rectangle(canvas, 80, 123, 224, 12, 5, 1,
                                     EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    const int16_t fill_width = static_cast<int16_t>((220U * elapsed) / BOOT_DURATION_MS);
    if (fill_width > 0) {
        egui_canvas_draw_round_rectangle_fill(canvas, 82, 125, fill_width, 8, 3,
                                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
    gui_draw_text(canvas, 139, 143, progress);

    const int16_t status_x = 145;
    const int16_t status_y = 161;
    const int16_t status_step = 30;
    const EventBits_t status_bits[] = {HW_EVENT_DISPLAY_READY, HW_EVENT_SD_READY,
                                       HW_EVENT_CODEC_READY};
    for (uint8_t index = 0; index < 3; ++index) {
        const bool ready = (bits & status_bits[index]) != 0;
        egui_canvas_draw_circle(canvas, status_x + index * status_step, status_y,
                                3, 1, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        if (ready) {
            egui_canvas_draw_circle_fill(canvas, status_x + index * status_step,
                                         status_y, 2, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
    }
    if ((bits & HW_EVENT_INIT_DONE) && !(bits & HW_EVENT_SD_READY)) {
        gui_draw_text(canvas, 139, 108, "SD UNAVAILABLE");
    }
}

void init() {
    started_ms = 0;
    gui_egui_view_init(&page_view, egui_port_core(), draw);
}

void enter() {
    started_ms = millis();
}

void exit() {}

bool key_consume(const KeyEvent &) {
    return true;
}

bool service() {
    if ((millis() - started_ms) >= BOOT_EXIT_START_MS) {
        (void)gui_page_manager_load(UiPage::Home);
        return false;
    }
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
