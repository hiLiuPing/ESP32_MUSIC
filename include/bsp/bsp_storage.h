#pragma once

#include <FS.h>

bool bsp_storage_init();
bool bsp_storage_available();
fs::FS &bsp_storage_fs();
size_t bsp_storage_total_bytes();
size_t bsp_storage_used_bytes();
