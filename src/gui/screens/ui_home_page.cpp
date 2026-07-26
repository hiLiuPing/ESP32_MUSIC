#include "gui/screens/ui_home_page.h"

#include "MyIMG.h"
#include "bsp/bsp_display.h"
#include "gui/gui_common.h"
#include "task/task_system.h"

namespace {
void init() {}
void enter() {}
void exit() {}

bool key_consume(const KeyEvent &) {
    return false;
}

bool service() {
    return false;
}

bool update_status(const PlayerStatus &) {
    return false;
}

void render() {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    gui_draw_header("HOME");

    constexpr int16_t xs[] = {20, 114, 208, 302};
    const unsigned char *icons[] = {tianqi, yinyue, yuedu, shezhi};
    const char *labels[] = {"HOME", "MUSIC", "READ", "SETTING"};
    for (size_t index = 0; index < 4; ++index) {
        display.drawBitmap(xs[index], 35, icons[index], 64, 64, 1);
        const int16_t text_width = bsp_fonts().getUTF8Width(labels[index]);
        gui_draw_text(xs[index] + (64 - text_width) / 2, 125, labels[index]);
    }

    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    if (!(bits & HW_EVENT_SD_READY)) {
        gui_draw_text(8, 158, "SD ERROR");
    } else if (!(bits & HW_EVENT_CODEC_READY)) {
        gui_draw_text(8, 158, "CODEC ERROR");
    } else {
        gui_draw_text(8, 158, "SYSTEM READY");
    }
}

GuiPageDescriptor descriptor = {
    UiPage::Home, init, enter, exit, key_consume, service, update_status, render,
    "home", true, false,
};
}

GuiPageDescriptor &ui_home_page_descriptor() {
    return descriptor;
}
