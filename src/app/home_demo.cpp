#include "app/home_demo.h"

#include <Arduino.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/app_data.h"

#if HOME_DEMO_ENABLE
namespace {
constexpr uint32_t DEMO_INTERVAL_MS = 2000U;
constexpr size_t SERIAL_LINE_SIZE = 64U;

struct HomeDemoData {
    const char *name;
    const char *text;
    int16_t temperature_c;
    int16_t high_c;
    int16_t low_c;
    int16_t pm25;
    uint16_t icon_id;
    WeatherScene_t scene;
    uint8_t hour;
    uint8_t minute;
};

constexpr HomeDemoData DEMO_DATA[] = {
    {"night",             "Clear",     24, 29, 21, 10, 150, WEATHER_SCENE_CLEAR,         0,  0},
    {"dawn-start",        "Clear",     24, 29, 21, 10, 150, WEATHER_SCENE_CLEAR,         6,  0},
    {"dawn-blend",        "Clear",     24, 29, 21, 10, 150, WEATHER_SCENE_CLEAR,         6, 37},
    {"day-edge-before",   "Clear",     24, 29, 21, 10, 150, WEATHER_SCENE_CLEAR,         6, 59},
    {"day-edge",          "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,         7,  0},
    {"dawn-peak",         "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,         7, 15},
    {"dawn-fade",         "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,         7, 52},
    {"dynamic-day",       "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,         8, 30},
    {"white-clouds",      "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,         9,  0},
    {"sunset-start",      "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,        17,  0},
    {"sunset-blend",      "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,        17, 30},
    {"sunset-peak",       "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,        18,  0},
    {"night-edge-before", "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,        18, 59},
    {"night-edge",        "Clear",     24, 29, 21, 10, 150, WEATHER_SCENE_CLEAR,        19,  0},
    {"night-stable",      "Clear",     24, 29, 21, 10, 150, WEATHER_SCENE_CLEAR,        19, 30},
    {"clear-day",         "Clear",     30, 34, 25, 12, 100, WEATHER_SCENE_CLEAR,        10,  0},
    {"clear-night",       "Clear",     24, 29, 21, 10, 150, WEATHER_SCENE_CLEAR,        22,  0},
    {"cloudy-day",        "Cloudy",    27, 31, 23, 18, 101, WEATHER_SCENE_CLOUDY,       10,  0},
    {"cloudy-night",      "Cloudy",    22, 27, 19, 20, 151, WEATHER_SCENE_CLOUDY,       22,  0},
    {"light-rain-day",    "LightRain", 25, 28, 22, 24, 305, WEATHER_SCENE_LIGHT_RAIN,    10,  0},
    {"light-rain-night",  "LightRain", 21, 25, 18, 26, 309, WEATHER_SCENE_LIGHT_RAIN,    22,  0},
    {"mid-rain-day",      "MidRain",   24, 27, 20, 30, 306, WEATHER_SCENE_MODERATE_RAIN, 10,  0},
    {"mid-rain-night",    "MidRain",   20, 24, 17, 32, 314, WEATHER_SCENE_MODERATE_RAIN, 22,  0},
    {"heavy-rain-day",    "HeavyRain", 22, 25, 19, 38, 307, WEATHER_SCENE_HEAVY_RAIN,    10,  0},
    {"heavy-rain-night",  "HeavyRain", 19, 23, 16, 42, 310, WEATHER_SCENE_HEAVY_RAIN,    22,  0},
    {"snow-day",          "Snow",      -2,  1, -6, 15, 400, WEATHER_SCENE_SNOW,          10,  0},
    {"snow-night",        "Snow",      -5, -1, -9, 16, 401, WEATHER_SCENE_SNOW,          22,  0},
};

constexpr size_t DEMO_COUNT = sizeof(DEMO_DATA) / sizeof(DEMO_DATA[0]);
static_assert(DEMO_COUNT == 27U, "Home demo scene count changed");

size_t demo_index = 0U;
bool automatic = true;
uint32_t next_change_ms = 0U;
char serial_line[SERIAL_LINE_SIZE] = {};
size_t serial_length = 0U;

const char *scene_name(WeatherScene_t scene) {
    switch (scene) {
        case WEATHER_SCENE_CLEAR: return "clear";
        case WEATHER_SCENE_CLOUDY: return "cloudy";
        case WEATHER_SCENE_LIGHT_RAIN: return "light-rain";
        case WEATHER_SCENE_MODERATE_RAIN: return "moderate-rain";
        case WEATHER_SCENE_HEAVY_RAIN: return "heavy-rain";
        case WEATHER_SCENE_SNOW: return "snow";
        case WEATHER_SCENE_UNKNOWN:
        default: return "unknown";
    }
}

WeatherScene_t scene_for_icon(uint16_t icon_id) {
    if (icon_id == 100U || icon_id == 150U) return WEATHER_SCENE_CLEAR;
    if ((icon_id >= 101U && icon_id <= 104U) ||
        (icon_id >= 151U && icon_id <= 154U)) return WEATHER_SCENE_CLOUDY;
    if (icon_id == 305U || icon_id == 309U) return WEATHER_SCENE_LIGHT_RAIN;
    if (icon_id == 306U || icon_id == 314U) return WEATHER_SCENE_MODERATE_RAIN;
    if (icon_id == 307U || icon_id == 310U) return WEATHER_SCENE_HEAVY_RAIN;
    if (icon_id >= 400U && icon_id <= 499U) return WEATHER_SCENE_SNOW;
    return WEATHER_SCENE_CLEAR;
}

void set_weather_strings(HomeWeatherData *weather, const HomeDemoData &demo) {
    if (weather == nullptr) return;
    std::snprintf(weather->text, sizeof(weather->text), "%s", demo.text);
    std::snprintf(weather->feels_like, sizeof(weather->feels_like), "%dC",
                  static_cast<int>(demo.temperature_c));
    std::snprintf(weather->wind, sizeof(weather->wind), "East 2");
    std::snprintf(weather->visibility, sizeof(weather->visibility), "10 km");
    std::snprintf(weather->pm10, sizeof(weather->pm10), "%d",
                  static_cast<int>(demo.pm25 + 8));
    std::snprintf(weather->pm2p5, sizeof(weather->pm2p5), "%d",
                  static_cast<int>(demo.pm25));
    std::snprintf(weather->no2, sizeof(weather->no2), "10");
    std::snprintf(weather->so2, sizeof(weather->so2), "6");
    std::snprintf(weather->co, sizeof(weather->co), "0.5");
    std::snprintf(weather->o3, sizeof(weather->o3), "70");
}

void fill_forecast(WeatherForecastDay *forecast, const HomeDemoData &demo) {
    static constexpr uint8_t days[APP_WEATHER_FORECAST_DAYS] = {15, 16, 17, 18, 19, 20, 21};
    static constexpr int16_t highs[APP_WEATHER_FORECAST_DAYS] = {32, 32, 33, 34, 34, 30, 1};
    static constexpr int16_t lows[APP_WEATHER_FORECAST_DAYS] = {21, 21, 23, 24, 25, 25, -6};
    static constexpr uint16_t icons[APP_WEATHER_FORECAST_DAYS] = {100, 101, 101, 305, 305, 306, 400};
    static constexpr const char *texts[APP_WEATHER_FORECAST_DAYS] = {
        "Clear", "Cloudy", "Cloudy", "Rain", "Rain", "MidRain", "Snow",
    };

    for (uint8_t i = 0U; i < APP_WEATHER_FORECAST_DAYS; ++i) {
        WeatherForecastDay &day = forecast[i];
        day.year = 2026U;
        day.month = 7U;
        day.day = days[i];
        day.high_c = highs[i];
        day.low_c = lows[i];
        day.icon_id = icons[i];
        day.scene = scene_for_icon(icons[i]);
        day.valid = true;
        day.stale = false;
        std::snprintf(day.text_day, sizeof(day.text_day), "%s", texts[i]);
        std::snprintf(day.date_text, sizeof(day.date_text), "2026-07-%02u",
                      static_cast<unsigned>(days[i]));
    }

    forecast[0].high_c = demo.high_c;
    forecast[0].low_c = demo.low_c;
    forecast[0].icon_id = demo.icon_id;
    forecast[0].scene = demo.scene;
    std::snprintf(forecast[0].text_day, sizeof(forecast[0].text_day), "%s", demo.text);
}

void print_status() {
    const HomeDemoData &demo = DEMO_DATA[demo_index];
    Serial.printf("[HomeDemo] %u/%u %s scene=%s icon=%u time=%02u:%02u mode=%s\n",
                  static_cast<unsigned>(demo_index + 1U),
                  static_cast<unsigned>(DEMO_COUNT), demo.name,
                  scene_name(demo.scene), static_cast<unsigned>(demo.icon_id),
                  static_cast<unsigned>(demo.hour), static_cast<unsigned>(demo.minute),
                  automatic ? "auto" : "paused");
}

void apply_scene() {
    const HomeDemoData &demo = DEMO_DATA[demo_index];
    AppTime time = {};
    HomeWeatherData weather = {};
    WeatherForecastDay forecast[APP_WEATHER_FORECAST_DAYS] = {};

    time.year = 2026U;
    time.month = 7U;
    time.day = 15U;
    time.hour = demo.hour;
    time.minute = demo.minute;
    time.second = 0U;
    time.valid = true;
    time.stale = false;

    weather.high_c = demo.high_c;
    weather.low_c = demo.low_c;
    weather.pm25 = demo.pm25;
    weather.temperature_c = demo.temperature_c;
    weather.humidity = 65U;
    weather.aqi = static_cast<int16_t>(demo.pm25 + 20);
    weather.icon_id = demo.icon_id;
    weather.scene = demo.scene;
    weather.valid = true;
    weather.stale = false;
    set_weather_strings(&weather, demo);
    fill_forecast(forecast, demo);

    app_data_set_home_demo(time, weather, forecast, APP_WEATHER_FORECAST_DAYS);
    print_status();
}

char *trim(char *text) {
    while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) ++text;
    char *end = text + std::strlen(text);
    while (end > text && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
    *end = '\0';
    return text;
}

void handle_command(char *line, uint32_t now_ms) {
    char *command = trim(line);
    constexpr char PREFIX[] = "home-demo";
    if (std::strncmp(command, PREFIX, sizeof(PREFIX) - 1U) != 0) return;

    command = trim(command + sizeof(PREFIX) - 1U);
    if (std::strcmp(command, "auto") == 0) {
        automatic = true;
        next_change_ms = now_ms + DEMO_INTERVAL_MS;
        print_status();
    } else if (std::strcmp(command, "pause") == 0) {
        automatic = false;
        print_status();
    } else if (std::strcmp(command, "next") == 0) {
        automatic = false;
        demo_index = (demo_index + 1U) % DEMO_COUNT;
        apply_scene();
    } else if (std::strcmp(command, "prev") == 0) {
        automatic = false;
        demo_index = (demo_index + DEMO_COUNT - 1U) % DEMO_COUNT;
        apply_scene();
    } else if (std::strcmp(command, "status") == 0) {
        print_status();
    } else {
        char *end = nullptr;
        const long selected = std::strtol(command, &end, 10);
        if (command[0] != '\0' && end != command && *trim(end) == '\0' &&
            selected >= 1L && selected <= static_cast<long>(DEMO_COUNT)) {
            automatic = false;
            demo_index = static_cast<size_t>(selected - 1L);
            apply_scene();
        } else {
            Serial.printf("[HomeDemo] usage: home-demo auto|pause|next|prev|status|1..%u\n",
                          static_cast<unsigned>(DEMO_COUNT));
        }
    }
}

void service_serial(uint32_t now_ms) {
    while (Serial.available() > 0) {
        const int value = Serial.read();
        if (value < 0) break;
        const char ch = static_cast<char>(value);
        if (ch == '\r' || ch == '\n') {
            if (serial_length != 0U) {
                serial_line[serial_length] = '\0';
                handle_command(serial_line, now_ms);
                serial_length = 0U;
            }
        } else if (serial_length + 1U < SERIAL_LINE_SIZE) {
            serial_line[serial_length++] = ch;
        } else {
            serial_length = 0U;
            Serial.println("[HomeDemo] serial command too long");
        }
    }
}
}
#endif

void home_demo_init(uint32_t now_ms) {
#if HOME_DEMO_ENABLE
    demo_index = 0U;
    automatic = true;
    next_change_ms = now_ms + DEMO_INTERVAL_MS;
    serial_length = 0U;
    apply_scene();
#else
    (void)now_ms;
#endif
}
void home_demo_service(uint32_t now_ms) {
#if HOME_DEMO_ENABLE
    service_serial(now_ms);
    if (automatic && static_cast<int32_t>(now_ms - next_change_ms) >= 0) {
        demo_index = (demo_index + 1U) % DEMO_COUNT;
        next_change_ms = now_ms + DEMO_INTERVAL_MS;
        apply_scene();
    }
#else
    (void)now_ms;
#endif
}
