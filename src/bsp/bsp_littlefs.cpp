#include "bsp/bsp_littlefs.h"

#include <LittleFS.h>
#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>
#include <freertos/semphr.h>

#ifndef BSP_LITTLEFS_DEBUG_MODE
#define BSP_LITTLEFS_DEBUG_MODE 0
#endif

namespace {
bool mounted = false;
SemaphoreHandle_t fs_mutex = nullptr;

constexpr size_t kCopyBufferSize = 4096U;
constexpr size_t kYieldIntervalBytes = 64U * 1024U;

bool init_littlefs() {
    if (LittleFS.begin(false)) {
        Serial.println("[LittleFS] initial mount successful");
        return true;
    }

    Serial.println("[LittleFS] initial mount failed");
#if BSP_LITTLEFS_DEBUG_MODE
    Serial.println("[LittleFS] debug recovery: formatting filesystem");
    if (!LittleFS.format()) {
        Serial.println("[LittleFS] debug recovery: format failed");
        return false;
    }

    Serial.println("[LittleFS] debug recovery: format successful, remounting");
    if (!LittleFS.begin(false)) {
        Serial.println("[LittleFS] debug recovery: remount failed");
        return false;
    }

    Serial.println("[LittleFS] debug recovery: remount successful");
    return true;
#else
    Serial.println("[LittleFS] debug recovery disabled");
    return false;
#endif
}

struct SyncStats {
    uint32_t copied = 0U;
    uint32_t skipped = 0U;
    uint32_t deleted = 0U;
    uint32_t failed = 0U;
    uint64_t copied_bytes = 0U;
    uint64_t deleted_bytes = 0U;
};

String join_path(const String &directory, const char *name) {
    if (directory == "/") return directory + name;
    return directory + "/" + name;
}

bool target_is_directory(const String &path) {
    File target = LittleFS.open(path, FILE_READ);
    if (!target) return false;
    const bool is_directory = target.isDirectory();
    target.close();
    return is_directory;
}

bool ensure_target_directory(const String &path, SyncStats &stats) {
    if (LittleFS.exists(path)) {
        if (target_is_directory(path)) return true;
        Serial.printf("[LittleFS] SYNC FAIL target is not directory: %s\n", path.c_str());
        ++stats.failed;
        return false;
    }
    if (LittleFS.mkdir(path)) return true;
    Serial.printf("[LittleFS] SYNC FAIL mkdir: %s\n", path.c_str());
    ++stats.failed;
    return false;
}

bool remove_target_tree(const String &path, SyncStats &stats) {
    File target = LittleFS.open(path, FILE_READ);
    if (!target) {
        Serial.printf("[LittleFS] SYNC FAIL open stale target: %s\n", path.c_str());
        ++stats.failed;
        return false;
    }

    if (!target.isDirectory()) {
        const size_t file_size = target.size();
        target.close();
        if (!LittleFS.remove(path)) {
            Serial.printf("[LittleFS] SYNC FAIL delete: %s\n", path.c_str());
            ++stats.failed;
            return false;
        }
        Serial.printf("[LittleFS] SYNC DELETE %s (%lu bytes)\n", path.c_str(),
                      static_cast<unsigned long>(file_size));
        ++stats.deleted;
        stats.deleted_bytes += file_size;
        return true;
    }

    bool remove_ok = true;
    while (true) {
        File child = target.openNextFile(FILE_READ);
        if (!child) break;
        const String child_path = join_path(path, child.name());
        child.close();
        if (!remove_target_tree(child_path, stats)) remove_ok = false;
    }
    target.close();
    if (!LittleFS.rmdir(path)) {
        Serial.printf("[LittleFS] SYNC FAIL rmdir: %s\n", path.c_str());
        ++stats.failed;
        return false;
    }
    Serial.printf("[LittleFS] SYNC RMDIR %s\n", path.c_str());
    return remove_ok;
}

bool remove_stale_targets(fs::FS &source, const String &source_directory,
                          File &target_directory, const String &target_directory_path,
                          SyncStats &stats) {
    bool directory_ok = true;
    while (true) {
        File target_entry = target_directory.openNextFile(FILE_READ);
        if (!target_entry) break;

        const String target_path = join_path(target_directory_path, target_entry.name());
        const String source_path = join_path(source_directory, target_entry.name());
        if (!source.exists(source_path)) {
            target_entry.close();
            if (!remove_target_tree(target_path, stats)) directory_ok = false;
            continue;
        }

        File source_entry = source.open(source_path, FILE_READ);
        if (!source_entry) {
            Serial.printf("[LittleFS] SYNC FAIL open source counterpart: %s\n",
                          source_path.c_str());
            ++stats.failed;
            directory_ok = false;
        } else if (source_entry.isDirectory() && target_entry.isDirectory()) {
            if (!remove_stale_targets(source, source_path, target_entry, target_path, stats)) {
                directory_ok = false;
            }
        }
        source_entry.close();
        target_entry.close();
    }
    return directory_ok;
}

bool copy_missing_file(File &source_file, const String &source_path,
                       const String &target_path, uint8_t *buffer,
                       SyncStats &stats) {
    if (LittleFS.exists(target_path)) {
        File target = LittleFS.open(target_path, FILE_READ);
        const bool is_regular_file = target && !target.isDirectory();
        target.close();
        if (is_regular_file) {
            ++stats.skipped;
            return true;
        }
        Serial.printf("[LittleFS] SYNC FAIL target is not file: %s\n", target_path.c_str());
        ++stats.failed;
        return false;
    }

    const String temporary_path = target_path + ".sd-sync.tmp";
    if (LittleFS.exists(temporary_path)) {
        File temporary = LittleFS.open(temporary_path, FILE_READ);
        const bool temporary_is_directory = temporary && temporary.isDirectory();
        temporary.close();
        if (temporary_is_directory || !LittleFS.remove(temporary_path)) {
            Serial.printf("[LittleFS] SYNC FAIL stale temporary: %s\n",
                          temporary_path.c_str());
            ++stats.failed;
            return false;
        }
    }

    const size_t source_size = source_file.size();
    const size_t total_bytes = LittleFS.totalBytes();
    const size_t used_bytes = LittleFS.usedBytes();
    const size_t free_bytes = total_bytes >= used_bytes ? total_bytes - used_bytes : 0U;
    if (source_size > free_bytes) {
        Serial.printf("[LittleFS] SYNC FAIL no space: %s need=%lu free=%lu\n",
                      target_path.c_str(), static_cast<unsigned long>(source_size),
                      static_cast<unsigned long>(free_bytes));
        ++stats.failed;
        return false;
    }

    File target_file = LittleFS.open(temporary_path, FILE_WRITE);
    if (!target_file) {
        Serial.printf("[LittleFS] SYNC FAIL open target: %s\n", temporary_path.c_str());
        ++stats.failed;
        return false;
    }

    size_t copied_bytes = 0U;
    size_t bytes_since_yield = 0U;
    bool copy_ok = true;
    while (copied_bytes < source_size) {
        const size_t requested = std::min(kCopyBufferSize, source_size - copied_bytes);
        const size_t read_bytes = source_file.read(buffer, requested);
        if (read_bytes == 0U || target_file.write(buffer, read_bytes) != read_bytes) {
            copy_ok = false;
            break;
        }
        copied_bytes += read_bytes;
        bytes_since_yield += read_bytes;
        if (bytes_since_yield >= kYieldIntervalBytes) {
            bytes_since_yield = 0U;
            vTaskDelay(1);
        }
    }
    target_file.flush();
    target_file.close();

    if (copy_ok && copied_bytes == source_size) {
        File verification = LittleFS.open(temporary_path, FILE_READ);
        copy_ok = verification && !verification.isDirectory() &&
                  verification.size() == source_size;
        verification.close();
    }
    if (copy_ok) copy_ok = LittleFS.rename(temporary_path, target_path);

    if (!copy_ok) {
        LittleFS.remove(temporary_path);
        Serial.printf("[LittleFS] SYNC FAIL copy: %s -> %s (%lu/%lu bytes)\n",
                      source_path.c_str(), target_path.c_str(),
                      static_cast<unsigned long>(copied_bytes),
                      static_cast<unsigned long>(source_size));
        ++stats.failed;
        return false;
    }

    Serial.printf("[LittleFS] SYNC COPY %s -> %s (%lu bytes)\n",
                  source_path.c_str(), target_path.c_str(),
                  static_cast<unsigned long>(source_size));
    ++stats.copied;
    stats.copied_bytes += source_size;
    return true;
}

bool sync_directory(File &source_directory, const String &target_directory,
                    uint8_t *buffer, SyncStats &stats) {
    bool directory_ok = true;
    while (true) {
        File entry = source_directory.openNextFile(FILE_READ);
        if (!entry) break;

        const String source_path(entry.path());
        const String target_path = join_path(target_directory, entry.name());
        if (entry.isDirectory()) {
            if (ensure_target_directory(target_path, stats)) {
                if (!sync_directory(entry, target_path, buffer, stats)) directory_ok = false;
            } else {
                directory_ok = false;
            }
        } else if (!copy_missing_file(entry, source_path, target_path, buffer, stats)) {
            directory_ok = false;
        }
        entry.close();
    }
    return directory_ok;
}

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
    const bool mount_ok = init_littlefs();
    if (mount_ok) {
        const double size_mb = static_cast<double>(LittleFS.totalBytes()) / (1024.0 * 1024.0);
        Serial.printf("[LittleFS] size=%.2f MB\n", size_mb);
        Serial.println("[LittleFS] mounted, file list:");
        File root = LittleFS.open("/");
        if (root) {
            print_tree(root, 0U);
            root.close();
        }
        constexpr uint8_t sizes[] = {16U, 18U, 20U};
        for (uint8_t i = 0U; i < sizeof(sizes); ++i) {
            char path[32] = {};
            std::snprintf(path, sizeof(path), "/heiti_4_%u.bin", sizes[i]);
            print_resource_check(path);
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

bool bsp_littlefs_sync_from(fs::FS &source, const char *source_dir) {
    if (!mounted) {
        Serial.println("[LittleFS] SYNC FAIL filesystem unavailable");
        return false;
    }
    if (source_dir == nullptr || source_dir[0] == '\0') {
        Serial.println("[LittleFS] SYNC FAIL invalid source directory");
        return false;
    }
    if (!bsp_littlefs_lock(portMAX_DELAY)) {
        Serial.println("[LittleFS] SYNC FAIL lock");
        return false;
    }

    SyncStats stats;
    bool sync_ok = false;
    String normalized_source(source_dir);
    if (!normalized_source.startsWith("/")) normalized_source = "/" + normalized_source;
    while (normalized_source.length() > 1U && normalized_source.endsWith("/")) {
        normalized_source.remove(normalized_source.length() - 1U);
    }

    File source_root = source.open(normalized_source, FILE_READ);
    if (!source_root || !source_root.isDirectory()) {
        Serial.printf("[LittleFS] SYNC FAIL source directory: %s\n",
                      normalized_source.c_str());
        ++stats.failed;
    } else {
        std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kCopyBufferSize]);
        if (!buffer) {
            Serial.println("[LittleFS] SYNC FAIL copy buffer allocation");
            ++stats.failed;
        } else {
            File target_root = LittleFS.open("/", FILE_READ);
            if (!target_root || !target_root.isDirectory()) {
                Serial.println("[LittleFS] SYNC FAIL target root directory");
                ++stats.failed;
            } else {
                sync_ok = remove_stale_targets(source, normalized_source, target_root,
                                               "/", stats);
                target_root.close();
                if (!sync_directory(source_root, "/", buffer.get(), stats)) sync_ok = false;
            }
        }
    }
    source_root.close();

    Serial.printf("[LittleFS] SYNC done copied=%lu skipped=%lu deleted=%lu failed=%lu "
                  "copied_bytes=%llu deleted_bytes=%llu\n",
                  static_cast<unsigned long>(stats.copied),
                  static_cast<unsigned long>(stats.skipped),
                  static_cast<unsigned long>(stats.deleted),
                  static_cast<unsigned long>(stats.failed),
                  static_cast<unsigned long long>(stats.copied_bytes),
                  static_cast<unsigned long long>(stats.deleted_bytes));
    bsp_littlefs_unlock();
    return sync_ok && stats.failed == 0U;
}

bool bsp_littlefs_lock(TickType_t timeout) {
    return fs_mutex != nullptr && xSemaphoreTake(fs_mutex, timeout) == pdTRUE;
}

void bsp_littlefs_unlock() {
    if (fs_mutex != nullptr) xSemaphoreGive(fs_mutex);
}
