#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct AppDataSnapshot {
    uint32_t version;
    uint32_t uptime_ms;
    uint32_t hardware_bits;
};

void app_data_attach_mutex(SemaphoreHandle_t mutex);
void app_data_set_snapshot(const AppDataSnapshot &snapshot);
bool app_data_get_snapshot(AppDataSnapshot *snapshot);
