#pragma once

#include <FS.h>
#include <freertos/FreeRTOS.h>

bool bsp_littlefs_init();
bool bsp_littlefs_available();
fs::FS &bsp_littlefs_fs();
bool bsp_littlefs_lock(TickType_t timeout = portMAX_DELAY);
void bsp_littlefs_unlock();
