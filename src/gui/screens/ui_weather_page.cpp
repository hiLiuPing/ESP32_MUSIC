#include "gui/screens/ui_weather_page.h"

#include <cstdio>
#include <cstring>

#include "app/app_data.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/resources/icons.h"

namespace {

constexpr int16_t SCREEN_W = EGUI_CONFIG_SCREEN_WIDTH;
constexpr uint8_t DAY_COUNT = APP_WEATHER_FORECAST_DAYS;

constexpr int16_t DATE_Y = 1;
constexpr int16_t WEEKDAY_Y = 18;
constexpr int16_t ICON_Y = 32;
constexpr int16_t ICON_SIZE = 27;
constexpr int16_t HIGH_TRACK_TOP = 74;
constexpr int16_t HIGH_TRACK_BOTTOM = 86;
constexpr int16_t LOW_TRACK_TOP = 106;
constexpr int16_t LOW_TRACK_BOTTOM = 120;

GuiEguiView view;
WeatherForecastDay days[DAY_COUNT] = {};
uint32_t forecast_version = 0U;
uint32_t logged_version = UINT32_MAX;
uint32_t rendered_version = UINT32_MAX;
uint8_t selected = 0U;
bool navigation_active = false;
bool complete_forecast = false;

static inline egui_color_t weather_color(uint32_t rgb) {
    return EGUI_COLOR_HEX(rgb);
}

static inline void draw_text(egui_canvas_t *canvas, const egui_font_t *font,
                             const char *text, int16_t x, int16_t y,
                             int16_t width, int16_t height, uint8_t align,
                             uint32_t color) {
    egui_region_t region = {{x, y}, {width, height}};
    egui_canvas_draw_text_in_rect(canvas, font, text == nullptr ? "" : text,
                                  &region, align, weather_color(color), EGUI_ALPHA_100);
}

static int16_t column_left(uint8_t index) {
    return static_cast<int16_t>((static_cast<uint32_t>(index) * SCREEN_W) / DAY_COUNT);
}

static int16_t column_right(uint8_t index) {
    return static_cast<int16_t>((static_cast<uint32_t>(index + 1U) * SCREEN_W) / DAY_COUNT);
}

static int16_t column_width(uint8_t index) {
    return static_cast<int16_t>(column_right(index) - column_left(index));
}

static int16_t column_center(uint8_t index) {
    return static_cast<int16_t>((static_cast<uint32_t>(index * 2U + 1U) * SCREEN_W) /
                                (DAY_COUNT * 2U));
}

static bool leap_year(uint16_t year) {
    return ((year % 4U) == 0U && (year % 100U) != 0U) || (year % 400U) == 0U;
}

static bool valid_date(const WeatherForecastDay &day) {
    if (!day.valid || day.year < 2000U || day.month < 1U || day.month > 12U ||
        day.day < 1U) return false;
    static constexpr uint8_t month_days[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                             31U, 31U, 30U, 31U, 30U, 31U};
    uint8_t max_day = month_days[day.month - 1U];
    if (day.month == 2U && leap_year(day.year)) ++max_day;
    return day.day <= max_day;
}

static const char *weekday_name(const WeatherForecastDay &day) {
    static constexpr const char *names[] = {"SUN", "MON", "TUE", "WED",
                                            "THU", "FRI", "SAT"};
    if (!valid_date(day)) return "---";
    int year = static_cast<int>(day.year);
    const int month = static_cast<int>(day.month);
    if (month < 3) --year;
    static constexpr uint8_t offset[] = {0U, 3U, 2U, 5U, 0U, 3U,
                                         5U, 1U, 4U, 6U, 2U, 4U};
    const int weekday = (year + year / 4 - year / 100 + year / 400 +
                         offset[month - 1] + static_cast<int>(day.day)) % 7;
    return names[weekday < 0 ? 0 : weekday];
}

static int16_t map_temperature(int value, int min_value, int max_value,
                               int16_t top, int16_t bottom) {
    int32_t result;
    if (max_value <= min_value) {
        result = top + (bottom - top) / 2;
    } else {
        result = top + (static_cast<int32_t>(max_value - value) * (bottom - top)) /
                         (max_value - min_value);
    }
    if (result < top) result = top;
    if (result > bottom) result = bottom;
    return static_cast<int16_t>(result);
}

static void copy_forecast() {
    WeatherForecastDay next[DAY_COUNT] = {};
    uint32_t version = 0U;
    if (!app_data_get_weather_forecast(next, DAY_COUNT, &version)) {
        complete_forecast = false;
        Serial.println("[WEATHER_UI] forecast snapshot unavailable");
        return;
    }
    std::memcpy(days, next, sizeof(days));
    forecast_version = version;
    complete_forecast = version != 0U;
    for (uint8_t i = 0U; i < DAY_COUNT; ++i) {
        if (!valid_date(days[i]) || days[i].icon_id == 0U) complete_forecast = false;
    }
    if (version != logged_version) {
        logged_version = version;
        Serial.printf("[WEATHER_UI] snapshot version=%lu complete=%u\n",
                      static_cast<unsigned long>(version), complete_forecast ? 1U : 0U);
        for (uint8_t i = 0U; i < DAY_COUNT; ++i) {
            Serial.printf("[WEATHER_UI] day=%u valid=%u date=%04u-%02u-%02u icon=%u high=%d low=%d\n",
                          static_cast<unsigned>(i), days[i].valid ? 1U : 0U,
                          static_cast<unsigned>(days[i].year), static_cast<unsigned>(days[i].month),
                          static_cast<unsigned>(days[i].day), static_cast<unsigned>(days[i].icon_id),
                          static_cast<int>(days[i].high_c), static_cast<int>(days[i].low_c));
        }
    }
}

static void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);

    int high_min = 0;
    int high_max = 0;
    int low_min = 0;
    int low_max = 0;
    bool have_temperature = false;
    if (complete_forecast) {
        high_min = high_max = days[0].high_c;
        low_min = low_max = days[0].low_c;
        have_temperature = true;
        for (uint8_t i = 1U; i < DAY_COUNT; ++i) {
            if (days[i].high_c < high_min) high_min = days[i].high_c;
            if (days[i].high_c > high_max) high_max = days[i].high_c;
            if (days[i].low_c < low_min) low_min = days[i].low_c;
            if (days[i].low_c > low_max) low_max = days[i].low_c;
        }
    }

    int16_t high_y[DAY_COUNT] = {};
    int16_t low_y[DAY_COUNT] = {};
    for (uint8_t i = 0U; i < DAY_COUNT; ++i) {
        const int16_t left = column_left(i);
        const int16_t width = column_width(i);
        const bool active = navigation_active && selected == i;
        const uint32_t primary = active ? 0xFFFFFFU : 0x111827U;
        if (active) egui_canvas_draw_rectangle_fill(canvas, left + 1, 0, width - 2, 160,
                                                    EGUI_COLOR_BLACK, EGUI_ALPHA_100);

        char date[12] = "--/--";
        if (valid_date(days[i])) {
            std::snprintf(date, sizeof(date), "%02u/%02u",
                          static_cast<unsigned>(days[i].month), static_cast<unsigned>(days[i].day));
        }
        draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), date,
                  left, DATE_Y, width, 13, EGUI_ALIGN_CENTER, primary);
        draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), weekday_name(days[i]),
                  left, WEEKDAY_Y, width, 13, EGUI_ALIGN_CENTER, active ? 0xFFFFFFU : 0x64748B);

        if (days[i].valid && days[i].icon_id != 0U) {
            const egui_image_std_t *icon = ui_weather_icon_get(days[i].icon_id);
            if (icon != nullptr) {
                egui_image_draw_image_resize(&icon->base, canvas,
                                             static_cast<int16_t>(column_center(i) - ICON_SIZE / 2),
                                             ICON_Y, ICON_SIZE, ICON_SIZE);
            }
        } else {
            draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), "--",
                      left, ICON_Y + 5, width, 18, EGUI_ALIGN_CENTER, 0x94A3B8);
        }

        if (have_temperature && days[i].valid) {
            high_y[i] = map_temperature(days[i].high_c, high_min, high_max,
                                         HIGH_TRACK_TOP, HIGH_TRACK_BOTTOM);
            low_y[i] = map_temperature(days[i].low_c, low_min, low_max,
                                        LOW_TRACK_TOP, LOW_TRACK_BOTTOM);
        } else {
            draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), "--/--C",
                      left, 143, width, 14, EGUI_ALIGN_CENTER, 0x94A3B8);
        }
        egui_canvas_draw_line(canvas, left + 3, 160, left + width - 4, 160, 1,
                              active ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }

    if (complete_forecast && have_temperature) {
        for (uint8_t i = 0U; i + 1U < DAY_COUNT; ++i) {
            egui_canvas_draw_line(canvas, column_center(i), high_y[i],
                                  column_center(i + 1U), high_y[i + 1U], 2,
                                  weather_color(0xF97316), EGUI_ALPHA_100);
            egui_canvas_draw_line(canvas, column_center(i), low_y[i],
                                  column_center(i + 1U), low_y[i + 1U], 2,
                                  weather_color(0x38BDF8), EGUI_ALPHA_100);
        }
        for (uint8_t i = 0U; i < DAY_COUNT; ++i) {
            egui_canvas_draw_circle_fill(canvas, column_center(i), high_y[i], 2,
                                         weather_color(0xF97316), EGUI_ALPHA_100);
            egui_canvas_draw_circle_fill(canvas, column_center(i), low_y[i], 2,
                                         weather_color(0x38BDF8), EGUI_ALPHA_100);
        }
        for (uint8_t i = 0U; i < DAY_COUNT; ++i) {
            const int16_t left = column_left(i);
            const int16_t width = column_width(i);
            char high_text[16] = {};
            char low_text[16] = {};
            std::snprintf(high_text, sizeof(high_text), "%s%dC",
                          days[i].stale ? "*" : "", static_cast<int>(days[i].high_c));
            std::snprintf(low_text, sizeof(low_text), "%s%dC",
                          days[i].stale ? "*" : "", static_cast<int>(days[i].low_c));
            draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), high_text,
                      left, high_y[i] - 14, width, 14, EGUI_ALIGN_CENTER,
                      navigation_active && selected == i ? 0xFFFFFF : 0xF97316);
            draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_10_4), low_text,
                      left, low_y[i] + 2, width, 14, EGUI_ALIGN_CENTER,
                      navigation_active && selected == i ? 0xFFFFFF : 0x38BDF8);
        }
        if (rendered_version != forecast_version) {
            rendered_version = forecast_version;
            Serial.printf("[WEATHER_UI] render version=%lu\n",
                          static_cast<unsigned long>(forecast_version));
        }
    }
}

void init() {
    Serial.println("[WEATHER_UI] init");
    gui_egui_view_init(&view, egui_port_core(), draw);
    copy_forecast();
}

void enter() {
    navigation_active = false;
    copy_forecast();
    Serial.printf("[WEATHER_UI] enter version=%lu complete=%u\n",
                  static_cast<unsigned long>(forecast_version), complete_forecast ? 1U : 0U);
}

void exit() {}

void navigation_changed(bool active) { navigation_active = active; }

bool key_consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::Click) {
        selected = static_cast<uint8_t>((selected + 1U) % DAY_COUNT);
        return true;
    }
    return false;
}

bool service() {
    WeatherForecastDay next[DAY_COUNT] = {};
    uint32_t version = 0U;
    if (!app_data_get_weather_forecast(next, DAY_COUNT, &version) || version == forecast_version) return false;
    Serial.printf("[WEATHER_UI] service detected forecast version=%lu\n",
                  static_cast<unsigned long>(version));
    copy_forecast();
    return true;
}

bool update_status(const PlayerStatus &) { return false; }

GuiPageDescriptor descriptor = {UiPage::Weather, init, enter, exit, key_consume, service,
                               update_status, EGUI_VIEW_OF(&view), "weather", true, false,
                               navigation_changed};

}  // namespace

GuiPageDescriptor &ui_weather_page_descriptor() { return descriptor; }
