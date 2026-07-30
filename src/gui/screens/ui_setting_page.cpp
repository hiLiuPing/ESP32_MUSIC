#include "gui/screens/ui_setting_page.h"

#include <cstdio>

#include "app/settings_app.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "task/weather_sync_task.h"

namespace {
GuiEguiView view;
AppSettings settings = {};
uint8_t selected = 0U;
bool editing = false;
bool navigation_active = false;
bool last_provisioning = false;
constexpr uint8_t ITEM_COUNT = 5U;

const char *labels[ITEM_COUNT] = {"POETRY POPUP", "POETRY DURATION", "WEATHER SYNC", "WIFI CONFIG", "SCREEN SLEEP"};

uint16_t adjust_interval(uint16_t value, int delta) {
    if (value == 0U) return delta > 0 ? 30U : 0U;
    if (value == 30U && delta < 0) return 0U;

    const int next = static_cast<int>(value) + delta * 10;
    if (next < 30) return 30U;
    if (next > 300) return 300U;
    return static_cast<uint16_t>(next);
}

void format_value(uint8_t i, char *out, size_t size) {
    switch (i) {
        case 0:
            std::snprintf(out, size,
                          settings.poetry_interval_min ? "%u MIN" : "OFF",
                          settings.poetry_interval_min);
            break;
        case 1: std::snprintf(out, size, "%u SEC", settings.poetry_duration_s); break;
        case 2:
            std::snprintf(out, size,
                          settings.weather_interval_min ? "%u MIN" : "OFF",
                          settings.weather_interval_min);
            break;
        case 3: std::snprintf(out, size, "%s", weather_sync_is_provisioning() ? "STOP" : "START"); break;
        case 4: std::snprintf(out, size, settings.screen_idle_min ? "%u MIN" : "OFF", settings.screen_idle_min); break;
        default: out[0] = '\0'; break;
    }
}

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, editing ? "SETTING EDIT" : "SETTING");
    const uint8_t first = selected > 5U ? selected - 5U : 0U;
    for (uint8_t row = 0U; row < 6U && first + row < ITEM_COUNT; ++row) {
        const uint8_t i = first + row;
        const int16_t y = static_cast<int16_t>(27 + row * 22);
        const bool focused = navigation_active && i == selected;
        if (focused) egui_canvas_draw_rectangle_fill(canvas, 4, y - 2, 376, 20, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        const egui_color_t color = focused ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), labels[i], 12, y, color, EGUI_ALPHA_100);
        char value[20] = {};
        format_value(i, value, sizeof(value));
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), value, 270, y, color, EGUI_ALPHA_100);
    }
    if (navigation_active) {
        gui_draw_text(canvas, 12, 153, editing ? "MIDDLE SAVE" : "MIDDLE EDIT");
    }
}

void adjust(int delta) {
    switch (selected) {
        case 0:
            settings.poetry_interval_min =
                adjust_interval(settings.poetry_interval_min, delta);
            break;
        case 1: settings.poetry_duration_s = static_cast<uint16_t>(constrain(static_cast<int>(settings.poetry_duration_s) + delta * 5, 5, 300)); break;
        case 2:
            settings.weather_interval_min =
                adjust_interval(settings.weather_interval_min, delta);
            break;
        case 3: break;
        case 4: settings.screen_idle_min = settings.screen_idle_min == 0U ? 1U : static_cast<uint16_t>(settings.screen_idle_min + delta); if (settings.screen_idle_min > 360U) settings.screen_idle_min = 0U; break;
    }
}

void init() { gui_egui_view_init(&view, egui_port_core(), draw); settings = settings_app_get(); last_provisioning = weather_sync_is_provisioning(); }
void enter() { settings = settings_app_get(); selected = 0U; editing = false; navigation_active = false; last_provisioning = weather_sync_is_provisioning(); }
void exit() {}
void navigation_changed(bool active) {
    navigation_active = active;
    if (!active && editing) {
        settings = settings_app_get();
        editing = false;
    }
}
bool key_consume(const KeyEvent &event) {
    if (editing) {
        if (event.id == KeyId::Left && event.gesture == KeyGesture::Click) { adjust(-1); return true; }
        if (event.id == KeyId::Right && event.gesture == KeyGesture::Click) { adjust(1); return true; }
        if (event.id == KeyId::Middle && event.gesture == KeyGesture::Click) {
            const bool changed = settings_app_update(settings);
            if (changed) (void)weather_sync_request(WEATHER_SYNC_SETTINGS_CHANGED);
            if (selected == 3U) {
                (void)weather_sync_request(weather_sync_is_provisioning() ? WEATHER_SYNC_STOP_AP : WEATHER_SYNC_START_AP);
            }
            editing = false;
            Serial.printf("[SETTING] save item=%u\n", selected);
            return true;
        }
        return true;
    }
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::Click) {
        editing = true;
        Serial.printf("[SETTING] enter item=%u\n", selected);
        return true;
    }
    if (event.id == KeyId::Left && event.gesture == KeyGesture::Click) { selected = static_cast<uint8_t>((selected + ITEM_COUNT - 1U) % ITEM_COUNT); return true; }
    if (event.id == KeyId::Right && event.gesture == KeyGesture::Click) { selected = static_cast<uint8_t>((selected + 1U) % ITEM_COUNT); return true; }
    return false;
}
bool service() {
    const bool provisioning = weather_sync_is_provisioning();
    if (provisioning == last_provisioning) return false;
    last_provisioning = provisioning;
    return true;
}
bool update_status(const PlayerStatus &) { return false; }
GuiPageDescriptor descriptor = {UiPage::Setting, init, enter, exit, key_consume, service, update_status, EGUI_VIEW_OF(&view), "setting", true, false, navigation_changed};
}

GuiPageDescriptor &ui_setting_page_descriptor() { return descriptor; }
