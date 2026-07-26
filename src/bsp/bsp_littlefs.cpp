#include "bsp/bsp_littlefs.h"

#include <LittleFS.h>
#include <Arduino.h>
#include <cstdio>
#include <freertos/semphr.h>

namespace {
bool mounted = false;
SemaphoreHandle_t fs_mutex = nullptr;

void print_tree(File dir, uint8_t depth) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        for (uint8_t i = 0U; i < depth; ++i) Serial.print("  ");
        Serial.printf("[LittleFS] %s %s %lu bytes\n", entry.isDirectory() ? "DIR " : "FILE",
                      entry.name(), static_cast<unsigned long>(entry.size()));
        if (entry.isDirectory() && depth < 4U) print_tree(entry, depth + 1U);
        entry.close();
    }
}

void print_resource_check(const char *path, uint32_t expected_size = 0U) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        Serial.printf("[LittleFS] CHECK %s OPEN_FAIL\n", path);
        return;
    }
    uint8_t magic[4] = {};
    const size_t read_size = file.read(magic, sizeof(magic));
    Serial.printf("[LittleFS] CHECK %s size=%lu magic=%02X%02X%02X%02X",
                  path, static_cast<unsigned long>(file.size()), magic[0], magic[1], magic[2], magic[3]);
    if (expected_size != 0U) {
        Serial.printf(" expected=%lu %s", static_cast<unsigned long>(expected_size),
                      file.size() == expected_size ? "OK" : "BAD_SIZE");
    }
    Serial.printf(" read=%u\n", static_cast<unsigned>(read_size));
    file.close();
}
}

bool bsp_littlefs_init() {
    if (mounted) {
        return true;
    }
    if (fs_mutex == nullptr) {
        fs_mutex = xSemaphoreCreateMutex();
        Serial.printf("[LittleFS] mutex %s\n", fs_mutex != nullptr ? "ready" : "CREATE_FAILED");
    }
    if (fs_mutex == nullptr || xSemaphoreTake(fs_mutex, portMAX_DELAY) != pdTRUE) {
        Serial.println("[LittleFS] lock failed");
        return false;
    }
    const bool mount_ok = LittleFS.begin(false);
    if (mount_ok) {
        Serial.println("[LittleFS] mounted, file list:");
        File root = LittleFS.open("/");
        if (root) {
            print_tree(root, 0U);
            root.close();
        }
        constexpr uint32_t glyphs = 21173U;
        constexpr uint8_t sizes[] = {10U, 12U, 14U, 16U, 18U, 20U};
        constexpr uint8_t bytes[] = {13U, 18U, 25U, 32U, 41U, 50U};
        for (uint8_t i = 0U; i < sizeof(sizes); ++i) {
            char path[32] = {};
            std::snprintf(path, sizeof(path), "/heiti_1_%u.bin", sizes[i]);
            print_resource_check(path, glyphs * bytes[i]);
        }
        print_resource_check("/song_3000.idx");
        print_resource_check("/song_300.idx");
        print_resource_check("/tang_300.idx");
        print_resource_check("/quotes_china.idx");
    }
    xSemaphoreGive(fs_mutex);
    mounted = mount_ok;
    return mounted;
}

bool bsp_littlefs_available() {
    return mounted;
}

fs::FS &bsp_littlefs_fs() {
    return LittleFS;
}

bool bsp_littlefs_lock(TickType_t timeout) {
    return fs_mutex != nullptr && xSemaphoreTake(fs_mutex, timeout) == pdTRUE;
}

void bsp_littlefs_unlock() {
    if (fs_mutex != nullptr) xSemaphoreGive(fs_mutex);
}
