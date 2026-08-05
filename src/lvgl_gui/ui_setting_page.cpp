#include "gui/screens/ui_setting_page.h"

#include <cstdio>

#include "app/settings_app.h"
#include "app/system_notify.h"
#include "lvgl_ui_common.h"
#include "task/file_manager_task.h"
#include "task/weather_sync_task.h"

namespace {
constexpr uint8_t kItems = 7;
const char *labels[kItems] = {"POETRY POPUP", "POETRY DURATION", "WEATHER SYNC", "SYNC NOW", "WIFI CONFIG", "FILE MANAGER", "SCREEN SLEEP"};
lv_obj_t *root = nullptr; lv_obj_t *title = nullptr; lv_obj_t *rows[kItems] = {}; lv_obj_t *hint = nullptr;
GuiPageDescriptor page = {}; AppSettings settings = {}; uint8_t selected = 0; bool editing = false; bool navigation = false; uint32_t manual_sync_at = 0;

uint16_t interval(uint16_t value, int delta) { if (value == 0) return delta > 0 ? 30 : 0; int next = static_cast<int>(value) + delta * 10; return next < 30 ? 0 : next > 300 ? 300 : static_cast<uint16_t>(next); }
bool manual_ready() { return manual_sync_at == 0 || millis() - manual_sync_at >= 30000U; }
void value(uint8_t i, char *out, size_t cap) { switch (i) { case 0: std::snprintf(out, cap, settings.poetry_interval_min ? "%u MIN" : "OFF", settings.poetry_interval_min); break; case 1: std::snprintf(out, cap, "%u SEC", settings.poetry_duration_s); break; case 2: std::snprintf(out, cap, settings.weather_interval_min ? "%u MIN" : "OFF", settings.weather_interval_min); break; case 3: std::snprintf(out, cap, manual_ready() ? "RUN" : "WAIT"); break; case 4: std::snprintf(out, cap, weather_sync_is_provisioning() ? "STOP" : "START"); break; case 5: std::snprintf(out, cap, file_manager_is_active() ? "STOP" : "START"); break; default: std::snprintf(out, cap, settings.screen_idle_min ? "%u MIN" : "OFF", settings.screen_idle_min); break; } }
void redraw() { lv_label_set_text(title, editing ? "SETTING EDIT" : "SETTING"); for (uint8_t i = 0; i < kItems; ++i) { char v[24] = {}; value(i, v, sizeof(v)); char row[72] = {}; std::snprintf(row, sizeof(row), "%c %-18s %s", navigation && i == selected ? '>' : ' ', labels[i], v); lv_label_set_text(rows[i], row); lv_obj_set_style_bg_color(rows[i], navigation && i == selected ? lv_color_black() : lv_color_white(), 0); lv_obj_set_style_text_color(rows[i], navigation && i == selected ? lv_color_white() : lv_color_black(), 0); } lv_label_set_text(hint, navigation ? (editing ? "LEFT / RIGHT ADJUST, MIDDLE SAVE" : "MIDDLE SELECT, RIGHT HOLD BACK") : "LEFT / RIGHT PAGE, MIDDLE ENTER"); }
void create_page() { root = lvgl_page_create(); lvgl_header(root, "SETTING"); title = lvgl_label(root, "SETTING", 8, 3, 200, &lv_font_montserrat_14); for (uint8_t i = 0; i < kItems; ++i) rows[i] = lvgl_label(root, "", 8, 27 + i * 17, 368, &lv_font_montserrat_12); hint = lvgl_label(root, "", 8, 149, 368, &lv_font_montserrat_12); page.view = root; }
void enter() { settings = settings_app_get(); selected = 0; editing = navigation = false; redraw(); }
void navigation_changed(bool active) { navigation = active; if (!active && editing) { settings = settings_app_get(); editing = false; } redraw(); }
void adjust(int delta) { if (selected == 0) settings.poetry_interval_min = interval(settings.poetry_interval_min, delta); else if (selected == 1) settings.poetry_duration_s = constrain(static_cast<int>(settings.poetry_duration_s) + delta * 5, 5, 300); else if (selected == 2) settings.weather_interval_min = interval(settings.weather_interval_min, delta); else if (selected == 6) settings.screen_idle_min = settings.screen_idle_min == 0 ? 1 : static_cast<uint16_t>(settings.screen_idle_min + delta); }
bool consume(const KeyEvent &event) { if (event.gesture != KeyGesture::Click) return false; if (editing) { if (event.id == KeyId::Left || event.id == KeyId::Right) adjust(event.id == KeyId::Left ? -1 : 1); else if (event.id == KeyId::Middle) { (void)settings_app_update(settings); (void)weather_sync_request(WEATHER_SYNC_SETTINGS_CHANGED); editing = false; } redraw(); return true; } if (event.id == KeyId::Left || event.id == KeyId::Right) { selected = static_cast<uint8_t>((selected + kItems + (event.id == KeyId::Left ? -1 : 1)) % kItems); redraw(); return true; } if (event.id != KeyId::Middle) return false; if (selected == 3) { if (manual_ready() && weather_sync_request(WEATHER_SYNC_MANUAL_NOW)) { manual_sync_at = millis(); (void)system_notify_post(SystemNotifyType::Info, "SYNC REQUESTED"); } } else if (selected == 4) { const bool stop = weather_sync_is_provisioning(); (void)weather_sync_request(stop ? WEATHER_SYNC_STOP_AP : WEATHER_SYNC_START_AP); } else if (selected == 5) { const bool stop = file_manager_is_active(); (void)file_manager_request(stop ? FILE_MANAGER_STOP_AP : FILE_MANAGER_START_AP); } else editing = true; redraw(); return true; }
bool service() { redraw(); return false; }
bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_setting_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Setting, create_page, enter, nullptr, consume, service, status_update, root, "setting", true, false, navigation_changed};
    return page;
}
