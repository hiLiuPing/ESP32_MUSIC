#include "gui/screens/ui_home_page.h"

#include <Arduino.h>

#include "app/app_data.h"
#include "app/player_types.h"
#include "task/task_system.h"
#include "lvgl_ui_common.h"

namespace {
lv_obj_t *root = nullptr;
lv_obj_t *clock_label = nullptr;
lv_obj_t *date_label = nullptr;
lv_obj_t *weather_label = nullptr;
lv_obj_t *environment_label = nullptr;
lv_obj_t *battery_label = nullptr;
lv_obj_t *scene_label = nullptr;
GuiPageDescriptor page = {};

void refresh() {
    AppDataSnapshot data = {};
    if (!app_data_get_snapshot(&data)) return;
    lv_label_set_text(clock_label, data.time_text);
    lv_label_set_text(date_label, data.date_text);
    lv_label_set_text_fmt(weather_label, "%s  %s", data.weather.text, data.temp_range_text);
    lv_label_set_text(environment_label, data.env_text);
    lv_label_set_text_fmt(battery_label, "BAT %u%%%s", data.battery_percent, data.charging ? " +" : "");
    static const char *scenes[] = {"WEATHER", "CLEAR", "CLOUDY", "LIGHT RAIN", "RAIN", "HEAVY RAIN", "SNOW"};
    const uint8_t scene = data.weather_scene < 7 ? data.weather_scene : 0;
    lv_label_set_text_fmt(scene_label, "[%s]", scenes[scene]);
}
void create_page() {
    root = lvgl_page_create();
    clock_label = lvgl_label(root, "--:--", 8, 2, 96, &lv_font_montserrat_28);
    date_label = lvgl_label(root, "", 108, 7, 130, &lv_font_montserrat_14);
    battery_label = lvgl_label(root, "", 285, 8, 90, &lv_font_montserrat_14);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    scene_label = lvgl_label(root, "", 80, 60, 224, &lv_font_montserrat_20);
    lv_obj_set_style_text_align(scene_label, LV_TEXT_ALIGN_CENTER, 0);
    weather_label = lvgl_label(root, "", 28, 102, 330, &lv_font_montserrat_16);
    lv_obj_set_style_text_align(weather_label, LV_TEXT_ALIGN_CENTER, 0);
    environment_label = lvgl_label(root, "", 20, 142, 344, &lv_font_montserrat_16);
    lv_obj_set_style_text_align(environment_label, LV_TEXT_ALIGN_CENTER, 0);
    page.view = root;
}
void enter() { refresh(); }
bool service() { refresh(); return true; }
bool consume(const KeyEvent &event) {
    if (event.gesture != KeyGesture::Click) return false;
    if (event.id == KeyId::Left) return task_post_player_command(PlayerCommandType::Previous, 0, true);
    if (event.id == KeyId::Right) return task_post_player_command(PlayerCommandType::Next, 0, true);
    if (event.id == KeyId::Middle) return task_post_player_command(PlayerCommandType::Toggle, 0, true);
    return false;
}
bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_home_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Home, create_page, enter, nullptr, consume, service, status_update, root, "home", true, false, nullptr};
    return page;
}
