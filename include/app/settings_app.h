#pragma once

#include <Arduino.h>

struct AppSettings {
    uint16_t poetry_interval_min;
    uint16_t poetry_duration_s;
    uint16_t weather_interval_min;
    uint16_t screen_idle_min;
};

void settings_app_init();
AppSettings settings_app_get();
bool settings_app_update(const AppSettings &settings);
void settings_app_reset_defaults();
