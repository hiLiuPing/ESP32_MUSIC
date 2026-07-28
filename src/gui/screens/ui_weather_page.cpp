#include "gui/screens/ui_weather_page.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "app/app_data.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/resources/icons.h"

namespace {
GuiEguiView view;
WeatherForecastDay days[APP_WEATHER_FORECAST_DAYS] = {};
uint32_t forecast_version = 0U;
uint8_t selected = 0U;
bool navigation_active = false;

const char *weekday_name(const WeatherForecastDay &day) {
    static constexpr const char *names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (!day.valid || day.year < 2020U) return "---";
    struct tm value = {};
    value.tm_year = static_cast<int>(day.year) - 1900;
    value.tm_mon = static_cast<int>(day.month) - 1;
    value.tm_mday = day.day;
    value.tm_isdst = -1;
    const time_t result = std::mktime(&value);
    if (result == static_cast<time_t>(-1)) return "---";
    return names[value.tm_wday % 7];
}

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "WEATHER 7D");
    egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), "7 DAY FORECAST", 12, 27,
                          EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    for (uint8_t i = 0U; i < APP_WEATHER_FORECAST_DAYS; ++i) {
        const int16_t x = static_cast<int16_t>(4 + i * 54);
        const bool active = navigation_active && i == selected;
        if (active) egui_canvas_draw_rectangle_fill(canvas, x, 46, 50, 108, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        const egui_color_t color = active ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
        char date[16] = {};
        if (days[i].valid) std::snprintf(date, sizeof(date), "%02u/%02u", days[i].month, days[i].day);
        else std::snprintf(date, sizeof(date), "--/--");
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), date, x + 3, 51, color, EGUI_ALPHA_100);
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), weekday_name(days[i]), x + 5, 62, color, EGUI_ALPHA_100);
        if (days[i].valid) {
            const egui_image_std_t *icon = ui_weather_icon_get_mask(days[i].icon_id);
            if (icon != nullptr) egui_image_draw_image_resize(&icon->base, canvas, x + 9, 73, 32, 28);
        }
        char temp[24] = {};
        std::snprintf(temp, sizeof(temp), "%d/%dC", days[i].high_c, days[i].low_c);
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), temp, x + 1, 107, color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, x + 5, 129, x + 45, 129, 1, active ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        if (days[i].valid) {
            const int16_t marker = static_cast<int16_t>(133 - (days[i].high_c - 20) * 2);
            egui_canvas_draw_circle_fill_basic(canvas, x + 25, marker, 2, color, EGUI_ALPHA_100);
        }
    }
    if (forecast_version == 0U) gui_draw_text(canvas, 144, 84, "NO WEATHER DATA");
}

void init() {
    gui_egui_view_init(&view, egui_port_core(), draw);
    app_data_get_weather_forecast(days, APP_WEATHER_FORECAST_DAYS, &forecast_version);
}
void enter() { navigation_active = false; app_data_get_weather_forecast(days, APP_WEATHER_FORECAST_DAYS, &forecast_version); }
void exit() {}
void navigation_changed(bool active) { navigation_active = active; }
bool key_consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::Click) {
        selected = static_cast<uint8_t>((selected + 1U) % APP_WEATHER_FORECAST_DAYS);
        return true;
    }
    return false;
}
bool service() {
    WeatherForecastDay next[APP_WEATHER_FORECAST_DAYS] = {};
    uint32_t version = 0U;
    if (!app_data_get_weather_forecast(next, APP_WEATHER_FORECAST_DAYS, &version) || version == forecast_version) return false;
    std::memcpy(days, next, sizeof(days));
    forecast_version = version;
    return true;
}
bool update_status(const PlayerStatus &) { return false; }
GuiPageDescriptor descriptor = {UiPage::Weather, init, enter, exit, key_consume, service, update_status, EGUI_VIEW_OF(&view), "weather", true, false, navigation_changed};
}

GuiPageDescriptor &ui_weather_page_descriptor() { return descriptor; }
