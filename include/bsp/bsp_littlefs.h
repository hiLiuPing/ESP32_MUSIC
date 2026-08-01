#pragma once

#include <FS.h>
#include <freertos/FreeRTOS.h>

bool bsp_littlefs_init();
bool bsp_littlefs_available();
fs::FS &bsp_littlefs_fs();
size_t bsp_littlefs_total_bytes();
size_t bsp_littlefs_used_bytes();
bool bsp_littlefs_sync_from(fs::FS &source, const char *source_dir);
bool bsp_littlefs_lock(TickType_t timeout = portMAX_DELAY);
void bsp_littlefs_unlock();
