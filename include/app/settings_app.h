#pragma once

#include <Arduino.h>

struct AppSettings {
    uint16_t poetry_interval_min;
    uint16_t poetry_duration_s;
    uint16_t weather_interval_min;
    uint16_t screen_idle_min;
    uint16_t auto_off_min;
    uint8_t poetry_enabled;
    uint8_t home_theme;
    uint8_t weather_sync_enabled;
};

void settings_app_init();
AppSettings settings_app_get();
bool settings_app_update(const AppSettings &settings);
void settings_app_reset_defaults();
