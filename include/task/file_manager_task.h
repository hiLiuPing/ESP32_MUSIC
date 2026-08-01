#pragma once

#include <Arduino.h>

#include <cstdint>

enum FileManagerRequest : uint32_t {
    FILE_MANAGER_START_AP = BIT0,
    FILE_MANAGER_STOP_AP = BIT1,
};

extern TaskHandle_t FileManagerTaskHandle;

void file_manager_task(void *parameter);
bool file_manager_request(uint32_t request);
bool file_manager_is_active();
String file_manager_ap_message();
