#include "app/music_library.h"

#include <Arduino.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <esp_heap_caps.h>

#include "app/system_notify.h"

namespace {
constexpr uint8_t MAX_SCAN_DEPTH = 16U;
constexpr size_t INTERNAL_FALLBACK_TRACKS = 128U;
constexpr size_t INITIAL_PATH_CAPACITY = 4096U;
constexpr const char *MUSIC_ROOT = "/music";

struct TrackCache {
    char *paths = nullptr;
    uint32_t *offsets = nullptr;
    size_t count = 0U;
    size_t path_bytes = 0U;
    size_t path_capacity = 0U;
    size_t track_limit = 0U;
    uint32_t capabilities = 0U;
    bool psram = false;
    bool failed = false;
};

TrackCache active_cache;
SemaphoreHandle_t track_mutex = nullptr;
bool fallback_notified = false;

class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = mutex_ != nullptr &&
                  xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
    }
    ~LockGuard() {
        if (locked_) xSemaphoreGive(mutex_);
    }
    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_ = false;
};

void release_cache(TrackCache &cache) {
    if (cache.paths != nullptr) heap_caps_free(cache.paths);
    if (cache.offsets != nullptr) heap_caps_free(cache.offsets);
    cache = {};
}

bool allocate_cache(TrackCache &cache, bool use_psram) {
    release_cache(cache);
    cache.psram = use_psram;
    cache.track_limit = use_psram ? PLAYER_MAX_TRACKS : INTERNAL_FALLBACK_TRACKS;
    cache.capabilities = (use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL) |
                         MALLOC_CAP_8BIT;
    cache.offsets = static_cast<uint32_t *>(
        heap_caps_malloc(cache.track_limit * sizeof(uint32_t),
                         cache.capabilities));
    cache.paths = static_cast<char *>(
        heap_caps_malloc(INITIAL_PATH_CAPACITY, cache.capabilities));
    if (cache.offsets == nullptr || cache.paths == nullptr) {
        release_cache(cache);
        return false;
    }
    cache.path_capacity = INITIAL_PATH_CAPACITY;
    return true;
}

bool initialize_staging_cache(TrackCache &cache) {
    if (psramFound() && allocate_cache(cache, true)) return true;
    if (!allocate_cache(cache, false)) return false;
    if (!fallback_notified) {
        fallback_notified = true;
        Serial.println("[MUSIC_LIBRARY] PSRAM unavailable; limiting library to 128 tracks");
        system_notify_post(SystemNotifyType::Warning,
                           "PLAYLIST LIMITED TO 128 TRACKS");
    }
    return true;
}

bool grow_paths(TrackCache &cache, size_t required) {
    if (required <= cache.path_capacity) return true;
    const size_t maximum = cache.track_limit * PLAYER_PATH_LENGTH;
    if (required > maximum) return false;
    size_t next = cache.path_capacity;
    while (next < required && next < maximum) {
        next = std::min(maximum, next * 2U);
    }
    void *resized = heap_caps_realloc(cache.paths, next, cache.capabilities);
    if (resized == nullptr) return false;
    cache.paths = static_cast<char *>(resized);
    cache.path_capacity = next;
    return true;
}

bool append_path(TrackCache &cache, const char *path) {
    if (path == nullptr || cache.count >= cache.track_limit) return false;
    const size_t length = std::strlen(path);
    if (length == 0U || length >= PLAYER_PATH_LENGTH) return false;
    const size_t required = cache.path_bytes + length + 1U;
    if (!grow_paths(cache, required)) {
        cache.failed = true;
        return false;
    }
    cache.offsets[cache.count++] = static_cast<uint32_t>(cache.path_bytes);
    std::memcpy(cache.paths + cache.path_bytes, path, length + 1U);
    cache.path_bytes = required;
    return true;
}

bool ends_with_ignore_case(const char *path, const char *extension) {
    if (path == nullptr || extension == nullptr) return false;
    const size_t path_length = std::strlen(path);
    const size_t extension_length = std::strlen(extension);
    if (path_length < extension_length) return false;
    const char *suffix = path + path_length - extension_length;
    for (size_t index = 0U; index < extension_length; ++index) {
        if (std::tolower(static_cast<unsigned char>(suffix[index])) !=
            std::tolower(static_cast<unsigned char>(extension[index]))) {
            return false;
        }
    }
    return true;
}

bool is_supported_audio_path(const char *path) {
    constexpr const char *SUPPORTED_EXTENSIONS[] = {
        ".mp3", ".aac", ".m4a", ".wav", ".flac",
    };
    for (const char *extension : SUPPORTED_EXTENSIONS) {
        if (ends_with_ignore_case(path, extension)) return true;
    }
    return false;
}

void scan_directory(fs::FS &filesystem, const char *directory_path,
                    uint8_t depth, TrackCache &cache) {
    if (cache.failed || cache.count >= cache.track_limit ||
        depth > MAX_SCAN_DEPTH) return;
    File directory = filesystem.open(directory_path);
    if (!directory || !directory.isDirectory()) return;

    for (File entry = directory.openNextFile(); entry && !cache.failed &&
         cache.count < cache.track_limit; entry = directory.openNextFile()) {
        const char *entry_path = entry.path();
        if (entry.isDirectory()) {
            if (entry_path != nullptr &&
                std::strlen(entry_path) < PLAYER_PATH_LENGTH) {
                scan_directory(filesystem, entry_path,
                               static_cast<uint8_t>(depth + 1U), cache);
            }
        } else if (is_supported_audio_path(entry_path)) {
            (void)append_path(cache, entry_path);
        }
        entry.close();
    }
    directory.close();
}

const char *cache_path(const TrackCache &cache, size_t index) {
    if (cache.paths == nullptr || cache.offsets == nullptr ||
        index >= cache.count) return nullptr;
    return cache.paths + cache.offsets[index];
}

const char *base_name(const char *path) {
    const char *slash = path == nullptr ? nullptr : std::strrchr(path, '/');
    return slash == nullptr ? path : slash + 1;
}
}

void music_library_attach_mutex(SemaphoreHandle_t mutex) {
    track_mutex = mutex;
}

bool music_library_scan(fs::FS &filesystem) {
    TrackCache staging;
    if (!initialize_staging_cache(staging)) {
        Serial.println("[MUSIC_LIBRARY] failed to allocate playlist cache");
        system_notify_post(SystemNotifyType::Storage,
                           "PLAYLIST CACHE ALLOCATION FAILED");
        return false;
    }

    scan_directory(filesystem, MUSIC_ROOT, 0U, staging);
    if (staging.failed) {
        Serial.println("[MUSIC_LIBRARY] path pool exhausted; keeping previous library");
        release_cache(staging);
        system_notify_post(SystemNotifyType::Storage,
                           "PLAYLIST CACHE EXHAUSTED");
        return false;
    }

    std::sort(staging.offsets, staging.offsets + staging.count,
              [&staging](uint32_t left, uint32_t right) {
                  return strcasecmp(staging.paths + left,
                                    staging.paths + right) < 0;
              });

    TrackCache previous;
    size_t cached_count = 0U;
    size_t cached_path_bytes = 0U;
    size_t cached_path_capacity = 0U;
    size_t cached_track_limit = 0U;
    bool cached_in_psram = false;
    {
        LockGuard lock(track_mutex);
        if (!lock.locked()) {
            release_cache(staging);
            return false;
        }
        previous = active_cache;
        active_cache = staging;
        staging = {};
        cached_count = active_cache.count;
        cached_path_bytes = active_cache.path_bytes;
        cached_path_capacity = active_cache.path_capacity;
        cached_track_limit = active_cache.track_limit;
        cached_in_psram = active_cache.psram;
    }
    release_cache(previous);

    Serial.printf("[MUSIC_LIBRARY] cached=%u path_bytes=%u capacity=%u memory=%s free_psram=%u\n",
                  static_cast<unsigned>(cached_count),
                  static_cast<unsigned>(cached_path_bytes),
                  static_cast<unsigned>(cached_path_capacity),
                  cached_in_psram ? "PSRAM" : "internal",
                  static_cast<unsigned>(ESP.getFreePsram()));
    if (cached_count == cached_track_limit) {
        Serial.printf("[MUSIC_LIBRARY] scan capped at %u tracks\n",
                      static_cast<unsigned>(cached_track_limit));
    }
    return true;
}
size_t music_library_count() {
    LockGuard lock(track_mutex);
    return lock.locked() ? active_cache.count : 0U;
}

bool music_library_get(size_t index, char *path, size_t path_capacity,
                       char *name, size_t name_capacity) {
    LockGuard lock(track_mutex);
    if (!lock.locked()) return false;
    const char *stored_path = cache_path(active_cache, index);
    if (stored_path == nullptr) return false;
    if (path != nullptr && path_capacity > 0U) {
        std::strncpy(path, stored_path, path_capacity - 1U);
        path[path_capacity - 1U] = '\0';
    }
    if (name != nullptr && name_capacity > 0U) {
        const char *stored_name = base_name(stored_path);
        std::strncpy(name, stored_name == nullptr ? "" : stored_name,
                     name_capacity - 1U);
        name[name_capacity - 1U] = '\0';
    }
    return true;
}
