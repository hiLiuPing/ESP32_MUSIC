#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>

enum WeatherScene_t : uint8_t {
    WEATHER_SCENE_UNKNOWN = 0,
    WEATHER_SCENE_CLEAR,
    WEATHER_SCENE_CLOUDY,
    WEATHER_SCENE_LIGHT_RAIN,
    WEATHER_SCENE_MODERATE_RAIN,
    WEATHER_SCENE_HEAVY_RAIN,
    WEATHER_SCENE_SNOW,
};

using HomeWeatherScene = WeatherScene_t;

struct AppTime {
    uint32_t version;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    union {
        uint8_t minute;
        uint8_t min;
    };
    uint8_t second;
    bool valid;
    bool stale;
};

struct HomeWeatherData {
    uint32_t version;
    int16_t high_c;
    int16_t low_c;
    int16_t pm25;
    uint16_t icon_id;
    WeatherScene_t scene;
    bool valid;
    bool stale;
};

struct WeatherForecastDay {
    uint32_t version;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    int16_t high_c;
    int16_t low_c;
    uint16_t icon_id;
    WeatherScene_t scene;
    bool valid;
    bool stale;
};

constexpr uint8_t APP_WEATHER_FORECAST_DAYS = 7U;

struct HomeEnvironmentData {
    uint32_t version;
    int16_t temperature_x10;
    uint8_t humidity;
    bool valid;
    bool stale;
};

struct HomeBatteryData {
    uint32_t version;
    uint8_t percent;
    bool charging;
    bool full;
    bool valid;
    bool stale;
};

struct AppDataSnapshot {
    uint32_t version;
    uint32_t uptime_ms;
    uint32_t hardware_bits;

    AppTime time;
    HomeWeatherData weather;
    WeatherForecastDay forecast[APP_WEATHER_FORECAST_DAYS];
    uint32_t forecast_version;
    HomeEnvironmentData environment;
    HomeBatteryData battery;

    char time_text[8];
    char date_text[24];
    char week_text[24];
    char temp_range_text[24];
    char pm25_text[24];
    char env_text[32];
    uint8_t is_day;

    // Flat view retained for the migrated renderer.
    int16_t environment_temp_x10;
    uint16_t weather_icon_id;
    uint8_t environment_humidity;
    uint8_t weather_scene;
    uint8_t battery_percent;
    uint8_t charging;
    uint8_t charge_full;
    uint8_t environment_valid;
    uint8_t environment_stale;
    uint8_t battery_valid;
    uint8_t battery_stale;
    uint8_t weather_valid;
    uint8_t weather_stale;
};

using DataApp_HomeStatus_t = AppDataSnapshot;
using app_time_t = AppTime;

void app_data_attach_mutex(SemaphoreHandle_t mutex);
void app_data_set_snapshot(const AppDataSnapshot &snapshot);
bool app_data_get_snapshot(AppDataSnapshot *snapshot);

void app_data_set_time(const AppTime &time);
void app_data_mark_time_fresh();
void app_data_mark_time_stale();
void app_data_set_weather(const HomeWeatherData &weather);
void app_data_mark_weather_stale();
void app_data_set_weather_forecast(const WeatherForecastDay *days, uint8_t count,
                                   uint32_t version = 0U, bool stale = false);
bool app_data_get_weather_forecast(WeatherForecastDay *days, uint8_t count,
                                   uint32_t *version = nullptr);
void app_data_set_environment(const HomeEnvironmentData &environment);
void app_data_set_battery(const HomeBatteryData &battery);
void app_data_update_runtime(uint32_t uptime_ms, uint32_t hardware_bits);
void app_data_update_temporary_home_data(uint32_t uptime_ms,
                                         uint32_t hardware_bits);

void DataApp_HomeStatus_Update();
void DataApp_HomeStatus_Get(DataApp_HomeStatus_t *out);
void Time_Get(app_time_t *out);
uint8_t Time_GetColon();
uint8_t Time_IsDaytime();
