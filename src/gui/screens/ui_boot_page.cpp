#include "gui/screens/ui_boot_page.h"

#include <Arduino.h>
#include <cstdio>

#include "bsp/bsp_display.h"
#include "gui/gui_common.h"
#include "gui/page_manager.h"
#include "task/task_system.h"

namespace {
uint32_t started_ms = 0;
uint32_t frame = 0;

void init() {
    started_ms = 0;
    frame = 0;
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

void render() {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    char progress[32];
    const uint8_t completed = ((bits & HW_EVENT_DISPLAY_READY) ? 1 : 0) +
                              ((bits & HW_EVENT_SD_READY) ? 1 : 0) +
                              ((bits & HW_EVENT_CODEC_READY) ? 1 : 0);
    std::snprintf(progress, sizeof(progress), "Hardware %u/3", completed);

    gui_draw_text(119, 58, "ESP32-S3 MUSIC");
    display.drawRectangle(91, 77, 292, 92, ST7305_COLOR_BLACK);
    const int fill_width = static_cast<int>((frame % 17) * 12);
    if (fill_width > 0) {
        display.drawFilledRectangle(94, 80, 94 + fill_width, 89, ST7305_COLOR_BLACK);
    }
    gui_draw_text(145, 118, progress);
    if ((bits & HW_EVENT_INIT_DONE) && !(bits & HW_EVENT_SD_READY)) {
        gui_draw_text(132, 143, "SD unavailable");
    }
}

GuiPageDescriptor descriptor = {
    UiPage::Boot, init, enter, exit, key_consume, service, update_status, render,
    "boot", false, false,
};
}

GuiPageDescriptor &ui_boot_page_descriptor() {
    return descriptor;
}
