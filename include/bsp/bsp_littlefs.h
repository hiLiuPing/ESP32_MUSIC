#pragma once

#include <FS.h>

bool bsp_littlefs_init();
bool bsp_littlefs_available();
fs::FS &bsp_littlefs_fs();
