#include "gui/screens/ui_setting_page.h"

#include <cstdio>

#include "anim/egui_animation_value.h"
#include "anim/egui_interpolator_anticipate_overshoot.h"
#include "app/settings_app.h"
#include "app/system_notify.h"
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
bool manual_sync_requested = false;
bool last_manual_sync_ready = true;
uint32_t last_manual_sync_ms = 0U;
int16_t selection_box_y = 25;
egui_animation_value_t selection_animation = {};
egui_interpolator_anticipate_overshoot_t selection_interpolator = {};
constexpr uint8_t ITEM_COUNT = 6U;
constexpr uint16_t SELECTION_ANIMATION_MS = 240U;
constexpr uint32_t MANUAL_SYNC_COOLDOWN_MS = 30000U;

const char *labels[ITEM_COUNT] = {"POETRY POPUP", "POETRY DURATION", "WEATHER SYNC",
                                  "SYNC NOW", "WIFI CONFIG", "SCREEN SLEEP"};

uint16_t adjust_interval(uint16_t value, int delta) {
    if (value == 0U) return delta > 0 ? 30U : 0U;
    if (value == 30U && delta < 0) return 0U;

    const int next = static_cast<int>(value) + delta * 10;
    if (next < 30) return 30U;
    if (next > 300) return 300U;
    return static_cast<uint16_t>(next);
}

void selection_animation_on_value(egui_animation_t *, int32_t value) {
    selection_box_y = static_cast<int16_t>(value);
    egui_view_invalidate_full(EGUI_VIEW_OF(&view));
}

void animate_selection_to(uint8_t item) {
    const int16_t target_y = static_cast<int16_t>(25 + item * 22);
    if (selection_box_y == target_y) return;

    egui_animation_stop(EGUI_ANIM_OF(&selection_animation));
    egui_animation_value_init(EGUI_ANIM_OF(&selection_animation));
    egui_animation_value_set_range(&selection_animation, selection_box_y, target_y);
    egui_animation_value_set_on_value(&selection_animation, selection_animation_on_value);
    egui_animation_target_view_set(EGUI_ANIM_OF(&selection_animation), EGUI_VIEW_OF(&view));
    egui_animation_duration_set(EGUI_ANIM_OF(&selection_animation), SELECTION_ANIMATION_MS);
    egui_interpolator_anticipate_overshoot_init(EGUI_INTERP_OF(&selection_interpolator));
    egui_interpolator_anticipate_overshoot_tension_set(
        EGUI_INTERP_OF(&selection_interpolator), EGUI_FLOAT_VALUE(0.7f));
    egui_animation_interpolator_set(EGUI_ANIM_OF(&selection_animation),
                                    EGUI_INTERP_OF(&selection_interpolator));
    egui_animation_start(EGUI_ANIM_OF(&selection_animation));
}

bool manual_sync_ready(uint32_t now) {
    return !manual_sync_requested ||
           static_cast<uint32_t>(now - last_manual_sync_ms) >=
               MANUAL_SYNC_COOLDOWN_MS;
}

bool request_manual_sync() {
    const uint32_t now = millis();
    if (!manual_sync_ready(now)) {
        Serial.println("[SETTING] manual sync ignored: cooldown");
        (void)system_notify_post(SystemNotifyType::Info, "SYNC WAIT");
        return true;
    }
    if (weather_sync_request(WEATHER_SYNC_MANUAL_NOW)) {
        manual_sync_requested = true;
        last_manual_sync_ms = now;
        Serial.println("[SETTING] manual sync requested");
        (void)system_notify_post(SystemNotifyType::Info, "SYNC REQUESTED");
    } else {
        Serial.println("[SETTING] manual sync request failed");
        (void)system_notify_post(SystemNotifyType::Warning, "SYNC REQUEST FAILED");
    }
    return true;
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
        case 3:
            std::snprintf(out, size, "%s", manual_sync_ready(millis()) ? "RUN" : "WAIT");
            break;
        case 4: std::snprintf(out, size, "%s", weather_sync_is_provisioning() ? "STOP" : "START"); break;
        case 5: std::snprintf(out, size, settings.screen_idle_min ? "%u MIN" : "OFF", settings.screen_idle_min); break;
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
        const egui_color_t color = EGUI_COLOR_BLACK;
        if (focused) {
            egui_canvas_draw_round_rectangle(canvas, 4, selection_box_y, 376, 20, 5, 1,
                                             EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), labels[i], 12, y, color, EGUI_ALPHA_100);
        char value[20] = {};
        format_value(i, value, sizeof(value));
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), value, 270, y, color, EGUI_ALPHA_100);
    }
    if (navigation_active) {
        const char *hint = editing ? "MIDDLE SAVE" :
                           selected == 3U ? "MIDDLE SYNC" :
                           selected == 4U ? "MIDDLE WIFI" : "MIDDLE EDIT";
        gui_draw_text(canvas, 12, 153, hint);
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
        case 4: break;
        case 5: settings.screen_idle_min = settings.screen_idle_min == 0U ? 1U : static_cast<uint16_t>(settings.screen_idle_min + delta); if (settings.screen_idle_min > 360U) settings.screen_idle_min = 0U; break;
    }
}

void init() {
    gui_egui_view_init(&view, egui_port_core(), draw);
    settings = settings_app_get();
    selection_box_y = 25;
    last_provisioning = weather_sync_is_provisioning();
    last_manual_sync_ready = manual_sync_ready(millis());
}
void enter() {
    egui_animation_stop(EGUI_ANIM_OF(&selection_animation));
    settings = settings_app_get();
    selected = 0U;
    selection_box_y = 25;
    editing = false;
    navigation_active = false;
    last_provisioning = weather_sync_is_provisioning();
    last_manual_sync_ready = manual_sync_ready(millis());
}
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
            editing = false;
            Serial.printf("[SETTING] save item=%u\n", selected);
            return true;
        }
        return true;
    }
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::Click) {
        if (selected == 3U) {
            return request_manual_sync();
        }
        if (selected == 4U) {
            const bool request_stop = weather_sync_is_provisioning();
            (void)weather_sync_request(request_stop ? WEATHER_SYNC_STOP_AP
                                                     : WEATHER_SYNC_START_AP);
            Serial.printf("[SETTING] wifi config %s\n",
                          request_stop ? "stop" : "start");
            return true;
        }
        editing = true;
        Serial.printf("[SETTING] enter item=%u\n", selected);
        return true;
    }
    if (event.id == KeyId::Left && event.gesture == KeyGesture::Click) {
        selected = static_cast<uint8_t>((selected + ITEM_COUNT - 1U) % ITEM_COUNT);
        animate_selection_to(selected);
        return true;
    }
    if (event.id == KeyId::Right && event.gesture == KeyGesture::Click) {
        selected = static_cast<uint8_t>((selected + 1U) % ITEM_COUNT);
        animate_selection_to(selected);
        return true;
    }
    return false;
}
bool service() {
    const bool provisioning = weather_sync_is_provisioning();
    const bool sync_ready = manual_sync_ready(millis());
    const bool changed = provisioning != last_provisioning ||
                         sync_ready != last_manual_sync_ready;
    last_provisioning = provisioning;
    last_manual_sync_ready = sync_ready;
    return changed;
}
bool update_status(const PlayerStatus &) { return false; }
GuiPageDescriptor descriptor = {UiPage::Setting, init, enter, exit, key_consume, service, update_status, EGUI_VIEW_OF(&view), "setting", true, false, navigation_changed};
}

GuiPageDescriptor &ui_setting_page_descriptor() { return descriptor; }
