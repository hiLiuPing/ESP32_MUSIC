#include "gui/screens/ui_boot_page.h"

#include <Arduino.h>

#include "gui/page_manager.h"
#include "lvgl_ui_common.h"
#include "task/task_system.h"

namespace {
lv_obj_t *root = nullptr;
lv_obj_t *progress = nullptr;
lv_obj_t *status = nullptr;
uint32_t started = 0;
GuiPageDescriptor page = {};

void create_page() {
    root = lvgl_page_create();
    lv_obj_t *title = lvgl_label(root, "ESP32-S3 MUSIC", 118, 18, 180, &lv_font_montserrat_20);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *ring = lv_obj_create(root);
    lv_obj_set_pos(ring, 156, 48); lv_obj_set_size(ring, 72, 72);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_black(), 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    progress = lv_bar_create(root);
    lv_obj_set_pos(progress, 80, 126); lv_obj_set_size(progress, 224, 10);
    lv_bar_set_range(progress, 0, 100); lv_bar_set_value(progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(progress, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(progress, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress, lv_color_black(), LV_PART_INDICATOR);
    status = lvgl_label(root, "INITIALIZING 0/4", 112, 144, 180);
    page.view = root;
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
}

void enter() { started = millis(); }
bool service() {
    const uint32_t elapsed = millis() - started;
    lv_bar_set_value(progress, elapsed >= 3000U ? 100 : elapsed / 30U, LV_ANIM_OFF);
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    const uint8_t ready = ((bits & HW_EVENT_DISPLAY_READY) ? 1 : 0) + ((bits & HW_EVENT_LITTLEFS_READY) ? 1 : 0) +
                          ((bits & HW_EVENT_SD_READY) ? 1 : 0) + ((bits & HW_EVENT_CODEC_READY) ? 1 : 0);
    lv_label_set_text_fmt(status, "INITIALIZING %u/4", ready);
    if (elapsed >= 2820U && (bits & (HW_EVENT_LITTLEFS_READY | HW_EVENT_INIT_DONE))) (void)gui_page_manager_load(UiPage::Home);
    return true;
}
bool consume(const KeyEvent &) { return true; }
bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_boot_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Boot, create_page, enter, nullptr, consume, service, status_update, root, "boot", false, false, nullptr};
    return page;
}
