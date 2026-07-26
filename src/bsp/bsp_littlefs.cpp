#include "bsp/bsp_littlefs.h"

#include <LittleFS.h>

namespace {
bool mounted = false;
}

bool bsp_littlefs_init() {
    if (mounted) {
        return true;
    }
    mounted = LittleFS.begin(false);
    return mounted;
}

bool bsp_littlefs_available() {
    return mounted;
}

fs::FS &bsp_littlefs_fs() {
    return LittleFS;
}
