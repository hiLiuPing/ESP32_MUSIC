#include "gui/screens/ui_setting_page.h"

#include <cstdio>

#include "app/settings_app.h"
#include "app/system_notify.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_heiti_font.h"
#include "task/file_manager_task.h"
#include "task/weather_sync_task.h"

namespace {
GuiEguiView view;
AppSettings settings = {};
uint8_t selected = 0U;
bool editing = false;
bool navigation_active = false;
bool last_provisioning = false;
bool last_file_manager_active = false;
bool manual_sync_requested = false;
bool last_manual_sync_ready = true;
uint32_t last_manual_sync_ms = 0U;
int16_t selection_box_y = 25;
constexpr uint8_t ITEM_COUNT = 7U;
constexpr uint8_t VISIBLE_ITEMS = 6U;
constexpr int16_t ROW_HEIGHT = 22;
constexpr uint32_t MANUAL_SYNC_COOLDOWN_MS = 30000U;

const char *labels[ITEM_COUNT] = {"诗词弹窗", "展示时长", "天气同步", "立即同步",
                                  "无线网络", "文件管理", "屏幕休眠"};

const egui_font_t *setting_font() {
    const egui_font_t *font = ui_heiti_font_get_cached(16U);
    return font != nullptr ? font
                           : reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT);
}

void draw_setting_header(egui_canvas_t *canvas, const char *title) {
    egui_canvas_draw_text(canvas, setting_font(), title, 8, 4,
                          EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, 0, 21, EGUI_CONFIG_SCREEN_WIDTH - 1, 21, 1,
                          EGUI_COLOR_BLACK, EGUI_ALPHA_100);
}

uint16_t adjust_interval(uint16_t value, int delta) {
    if (value == 0U) return delta > 0 ? 30U : 0U;
    if (value == 30U && delta < 0) return 0U;

    const int next = static_cast<int>(value) + delta * 10;
    if (next < 30) return 30U;
    if (next > 300) return 300U;
    return static_cast<uint16_t>(next);
}

uint8_t visible_first() {
    return selected >= VISIBLE_ITEMS ? static_cast<uint8_t>(selected - VISIBLE_ITEMS + 1U) : 0U;
}

int16_t selection_y_for(uint8_t item) {
    const uint8_t first = visible_first();
    const uint8_t row = item >= first ? static_cast<uint8_t>(item - first) : 0U;
    return static_cast<int16_t>(25 + row * ROW_HEIGHT);
}

egui_region_t setting_row_region(uint8_t item, uint8_t first) {
    const uint8_t row = item >= first ? static_cast<uint8_t>(item - first) : 0U;
    return {{0, static_cast<int16_t>(25 + row * ROW_HEIGHT)},
            {EGUI_CONFIG_SCREEN_WIDTH, ROW_HEIGHT}};
}

void invalidate_selection_change(uint8_t old_selected, uint8_t old_first) {
    const uint8_t new_first = visible_first();
    if (old_first != new_first) {
        // The visible rows shift when crossing the six-item window boundary.
        const egui_region_t content_region = {
            {0, 21}, {EGUI_CONFIG_SCREEN_WIDTH, EGUI_CONFIG_SCREEN_HEIGHT - 21}};
        egui_view_invalidate_region(EGUI_VIEW_OF(&view), &content_region);
        return;
    }

    const egui_region_t old_region = setting_row_region(old_selected, old_first);
    const egui_region_t new_region = setting_row_region(selected, new_first);
    egui_view_invalidate_region(EGUI_VIEW_OF(&view), &old_region);
    egui_view_invalidate_region(EGUI_VIEW_OF(&view), &new_region);

    // The bottom hint changes for the action rows.
    if ((old_selected >= 3U && old_selected <= 5U) ||
        (selected >= 3U && selected <= 5U)) {
        const egui_region_t hint_region = {
            {0, 145}, {EGUI_CONFIG_SCREEN_WIDTH, EGUI_CONFIG_SCREEN_HEIGHT - 145}};
        egui_view_invalidate_region(EGUI_VIEW_OF(&view), &hint_region);
    }
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
                          settings.poetry_interval_min ? "%u 分钟" : "关闭",
                          settings.poetry_interval_min);
            break;
        case 1: std::snprintf(out, size, "%u 秒", settings.poetry_duration_s); break;
        case 2:
            std::snprintf(out, size,
                          settings.weather_interval_min ? "%u 分钟" : "关闭",
                          settings.weather_interval_min);
            break;
        case 3:
            std::snprintf(out, size, "%s", manual_sync_ready(millis()) ? "执行" : "等待");
            break;
        case 4: std::snprintf(out, size, "%s", weather_sync_is_provisioning() ? "停止" : "启动"); break;
        case 5: std::snprintf(out, size, "%s", file_manager_is_active() ? "停止" : "启动"); break;
        case 6: std::snprintf(out, size, settings.screen_idle_min ? "%u 分钟" : "关闭", settings.screen_idle_min); break;
        default: out[0] = '\0'; break;
    }
}

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    draw_setting_header(canvas, editing ? "编辑设置" : "设置");
    const uint8_t first = visible_first();
    for (uint8_t row = 0U; row < VISIBLE_ITEMS && first + row < ITEM_COUNT; ++row) {
        const uint8_t i = first + row;
        const int16_t y = static_cast<int16_t>(27 + row * ROW_HEIGHT);
        const bool focused = navigation_active && i == selected;
        const egui_color_t color = focused ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
        if (focused) {
            egui_canvas_draw_round_rectangle_fill(canvas, 4, selection_box_y, 376, 20, 5,
                                                  EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
        egui_canvas_draw_text(canvas, setting_font(), labels[i], 12, y, color, EGUI_ALPHA_100);
        char value[20] = {};
        format_value(i, value, sizeof(value));
        egui_canvas_draw_text(canvas, setting_font(), value, 270, y, color, EGUI_ALPHA_100);
    }
    if (navigation_active) {
        const char *hint = editing ? "中键保存" :
                           selected == 3U ? "中键同步" :
                           selected == 4U ? "中键网络" :
                           selected == 5U ? "中键文件" : nullptr;
        if (hint != nullptr) {
            egui_canvas_draw_text(canvas, setting_font(), hint, 12, 150,
                                  EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
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
        case 5: break;
        case 6: settings.screen_idle_min = settings.screen_idle_min == 0U ? 1U : static_cast<uint16_t>(settings.screen_idle_min + delta); if (settings.screen_idle_min > 360U) settings.screen_idle_min = 0U; break;
    }
}

void init() {
    gui_egui_view_init(&view, egui_port_core(), draw);
    settings = settings_app_get();
    selection_box_y = 25;
    last_provisioning = weather_sync_is_provisioning();
    last_file_manager_active = file_manager_is_active();
    last_manual_sync_ready = manual_sync_ready(millis());
}
void enter() {
    settings = settings_app_get();
    selected = 0U;
    selection_box_y = 25;
    editing = false;
    navigation_active = false;
    last_provisioning = weather_sync_is_provisioning();
    last_file_manager_active = file_manager_is_active();
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
            if (!request_stop) (void)file_manager_request(FILE_MANAGER_STOP_AP);
            (void)weather_sync_request(request_stop ? WEATHER_SYNC_STOP_AP
                                                     : WEATHER_SYNC_START_AP);
            Serial.printf("[SETTING] wifi config %s\n",
                          request_stop ? "stop" : "start");
            return true;
        }
        if (selected == 5U) {
            const bool request_stop = file_manager_is_active();
            (void)file_manager_request(request_stop ? FILE_MANAGER_STOP_AP
                                                    : FILE_MANAGER_START_AP);
            Serial.printf("[SETTING] file manager %s\n",
                          request_stop ? "stop" : "start");
            return true;
        }
        editing = true;
        Serial.printf("[SETTING] enter item=%u\n", selected);
        return true;
    }
    if (event.id == KeyId::Left && event.gesture == KeyGesture::Click) {
        const uint8_t old_selected = selected;
        const uint8_t old_first = visible_first();
        selected = static_cast<uint8_t>((selected + ITEM_COUNT - 1U) % ITEM_COUNT);
        selection_box_y = selection_y_for(selected);
        invalidate_selection_change(old_selected, old_first);
        return true;
    }
    if (event.id == KeyId::Right && event.gesture == KeyGesture::Click) {
        const uint8_t old_selected = selected;
        const uint8_t old_first = visible_first();
        selected = static_cast<uint8_t>((selected + 1U) % ITEM_COUNT);
        selection_box_y = selection_y_for(selected);
        invalidate_selection_change(old_selected, old_first);
        return true;
    }
    return false;
}
bool service() {
    const bool provisioning = weather_sync_is_provisioning();
    const bool file_manager_active = file_manager_is_active();
    const bool sync_ready = manual_sync_ready(millis());
    const bool changed = provisioning != last_provisioning ||
                         file_manager_active != last_file_manager_active ||
                         sync_ready != last_manual_sync_ready;
    last_provisioning = provisioning;
    last_file_manager_active = file_manager_active;
    last_manual_sync_ready = sync_ready;
    return changed;
}
bool update_status(const PlayerStatus &) { return false; }
GuiPageDescriptor descriptor = {UiPage::Setting, init, enter, exit, key_consume, service, update_status, EGUI_VIEW_OF(&view), "setting", true, false, navigation_changed};
}

GuiPageDescriptor &ui_setting_page_descriptor() { return descriptor; }
