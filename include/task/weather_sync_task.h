#pragma once

#include <Arduino.h>

#include <cstdint>

enum WeatherSyncRequest : uint32_t {
    WEATHER_SYNC_NOW = BIT0,
    WEATHER_SYNC_SETTINGS_CHANGED = BIT1,
    WEATHER_SYNC_START_AP = BIT2,
    WEATHER_SYNC_STOP_AP = BIT3,
};

extern TaskHandle_t WeatherSyncTaskHandle;

void weather_sync_task(void *parameter);
bool weather_sync_request(uint32_t request);
bool weather_sync_is_provisioning();
