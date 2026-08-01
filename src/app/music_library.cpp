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
constexpr size_t INITIAL_PLAYLIST_NAME_CAPACITY = 512U;
constexpr const char *MUSIC_ROOT = "/music";

struct PlaylistDescriptor {
    uint32_t name_offset = 0U;
    uint16_t first_track = 0U;
    uint16_t track_count = 0U;
};

struct TrackCache {
    char *paths = nullptr;
    uint32_t *offsets = nullptr;
    char *playlist_names = nullptr;
    PlaylistDescriptor *playlists = nullptr;
    uint16_t *playlist_tracks = nullptr;
    size_t count = 0U;
    size_t path_bytes = 0U;
    size_t path_capacity = 0U;
    size_t playlist_count = 0U;
    size_t playlist_track_count = 0U;
    size_t playlist_name_bytes = 0U;
    size_t playlist_name_capacity = 0U;
    size_t track_limit = 0U;
    uint32_t capabilities = 0U;
    uint32_t version = 0U;
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
    if (cache.playlist_names != nullptr) heap_caps_free(cache.playlist_names);
    if (cache.playlists != nullptr) heap_caps_free(cache.playlists);
    if (cache.playlist_tracks != nullptr) heap_caps_free(cache.playlist_tracks);
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
    cache.playlist_names = static_cast<char *>(
        heap_caps_malloc(INITIAL_PLAYLIST_NAME_CAPACITY, cache.capabilities));
    cache.playlists = static_cast<PlaylistDescriptor *>(
        heap_caps_calloc(cache.track_limit + 1U, sizeof(PlaylistDescriptor),
                         cache.capabilities));
    cache.playlist_tracks = static_cast<uint16_t *>(
        heap_caps_malloc(cache.track_limit * sizeof(uint16_t),
                         cache.capabilities));
    if (cache.offsets == nullptr || cache.paths == nullptr ||
        cache.playlist_names == nullptr || cache.playlists == nullptr ||
        cache.playlist_tracks == nullptr) {
        release_cache(cache);
        return false;
    }
    cache.path_capacity = INITIAL_PATH_CAPACITY;
    cache.playlist_name_capacity = INITIAL_PLAYLIST_NAME_CAPACITY;
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

bool grow_playlist_names(TrackCache &cache, size_t required) {
    if (required <= cache.playlist_name_capacity) return true;
    const size_t maximum = cache.track_limit * PLAYER_PATH_LENGTH;
    if (required > maximum) return false;
    size_t next = cache.playlist_name_capacity;
    while (next < required && next < maximum) {
        next = std::min(maximum, next * 2U);
    }
    void *resized = heap_caps_realloc(cache.playlist_names, next,
                                      cache.capabilities);
    if (resized == nullptr) return false;
    cache.playlist_names = static_cast<char *>(resized);
    cache.playlist_name_capacity = next;
    return true;
}

bool append_playlist_name(TrackCache &cache, const char *name, size_t length,
                          uint32_t *offset) {
    if (name == nullptr || offset == nullptr || length == 0U) return false;
    const size_t required = cache.playlist_name_bytes + length + 1U;
    if (!grow_playlist_names(cache, required)) return false;
    *offset = static_cast<uint32_t>(cache.playlist_name_bytes);
    std::memcpy(cache.playlist_names + cache.playlist_name_bytes, name, length);
    cache.playlist_names[cache.playlist_name_bytes + length] = '\0';
    cache.playlist_name_bytes = required;
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

const char *playlist_name(const TrackCache &cache, size_t index) {
    if (cache.playlist_names == nullptr || cache.playlists == nullptr ||
        index >= cache.playlist_count) return nullptr;
    return cache.playlist_names + cache.playlists[index].name_offset;
}

bool top_level_folder(const char *path, const char **name, size_t *length) {
    if (path == nullptr || name == nullptr || length == nullptr) return false;
    constexpr size_t ROOT_LENGTH = 6U;
    if (strncasecmp(path, MUSIC_ROOT, ROOT_LENGTH) != 0 ||
        path[ROOT_LENGTH] != '/') return false;
    const char *component = path + ROOT_LENGTH + 1U;
    const char *slash = std::strchr(component, '/');
    if (slash == nullptr || slash == component) return false;
    *name = component;
    *length = static_cast<size_t>(slash - component);
    return true;
}

bool same_playlist_name(const TrackCache &cache, size_t playlist_index,
                        const char *name, size_t length) {
    const char *stored = playlist_name(cache, playlist_index);
    return stored != nullptr && std::strlen(stored) == length &&
           strncasecmp(stored, name, length) == 0;
}

bool build_playlists(TrackCache &cache) {
    constexpr char ALL_MUSIC[] = "ALL MUSIC";
    uint32_t all_name_offset = 0U;
    if (!append_playlist_name(cache, ALL_MUSIC, sizeof(ALL_MUSIC) - 1U,
                              &all_name_offset)) {
        return false;
    }
    cache.playlist_count = 1U;
    cache.playlists[0].name_offset = all_name_offset;
    cache.playlists[0].track_count = static_cast<uint16_t>(cache.count);

    for (size_t global_index = 0U; global_index < cache.count; ++global_index) {
        const char *folder = nullptr;
        size_t folder_length = 0U;
        if (!top_level_folder(cache_path(cache, global_index),
                              &folder, &folder_length)) {
            continue;
        }

        size_t playlist_index = cache.playlist_count - 1U;
        if (playlist_index == 0U ||
            !same_playlist_name(cache, playlist_index, folder, folder_length)) {
            if (cache.playlist_count >= cache.track_limit + 1U) return false;
            uint32_t name_offset = 0U;
            if (!append_playlist_name(cache, folder, folder_length,
                                      &name_offset)) {
                return false;
            }
            playlist_index = cache.playlist_count++;
            cache.playlists[playlist_index].name_offset = name_offset;
            cache.playlists[playlist_index].first_track =
                static_cast<uint16_t>(cache.playlist_track_count);
        }

        if (cache.playlist_track_count >= cache.track_limit) return false;
        cache.playlist_tracks[cache.playlist_track_count++] =
            static_cast<uint16_t>(global_index);
        ++cache.playlists[playlist_index].track_count;
    }
    return true;
}

void copy_text(char *destination, size_t capacity, const char *source) {
    if (destination == nullptr || capacity == 0U) return;
    std::strncpy(destination, source == nullptr ? "" : source, capacity - 1U);
    destination[capacity - 1U] = '\0';
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
    if (!build_playlists(staging)) {
        Serial.println("[MUSIC_LIBRARY] playlist metadata exhausted; keeping previous library");
        release_cache(staging);
        system_notify_post(SystemNotifyType::Storage,
                           "PLAYLIST METADATA EXHAUSTED");
        return false;
    }

    TrackCache previous;
    size_t cached_count = 0U;
    size_t cached_path_bytes = 0U;
    size_t cached_path_capacity = 0U;
    size_t cached_track_limit = 0U;
    size_t cached_playlist_count = 0U;
    bool cached_in_psram = false;
    {
        LockGuard lock(track_mutex);
        if (!lock.locked()) {
            release_cache(staging);
            return false;
        }
        previous = active_cache;
        staging.version = active_cache.version + 1U;
        if (staging.version == 0U) staging.version = 1U;
        active_cache = staging;
        staging = {};
        cached_count = active_cache.count;
        cached_path_bytes = active_cache.path_bytes;
        cached_path_capacity = active_cache.path_capacity;
        cached_track_limit = active_cache.track_limit;
        cached_playlist_count = active_cache.playlist_count;
        cached_in_psram = active_cache.psram;
    }
    release_cache(previous);

    Serial.printf("[MUSIC_LIBRARY] cached=%u playlists=%u path_bytes=%u capacity=%u memory=%s free_psram=%u\n",
                  static_cast<unsigned>(cached_count),
                  static_cast<unsigned>(cached_playlist_count),
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

uint32_t music_library_version() {
    LockGuard lock(track_mutex);
    return lock.locked() ? active_cache.version : 0U;
}

bool music_library_get(size_t index, char *path, size_t path_capacity,
                       char *name, size_t name_capacity) {
    LockGuard lock(track_mutex);
    if (!lock.locked()) return false;
    const char *stored_path = cache_path(active_cache, index);
    if (stored_path == nullptr) return false;
    if (path != nullptr && path_capacity > 0U) {
        copy_text(path, path_capacity, stored_path);
    }
    if (name != nullptr && name_capacity > 0U) {
        const char *stored_name = base_name(stored_path);
        copy_text(name, name_capacity, stored_name);
    }
    return true;
}


size_t music_library_playlist_count() {
    LockGuard lock(track_mutex);
    return lock.locked() ? active_cache.playlist_count : 0U;
}

bool music_library_playlist_get(size_t playlist_index,
                                char *name, size_t name_capacity,
                                size_t *track_count) {
    LockGuard lock(track_mutex);
    if (!lock.locked() || playlist_index >= active_cache.playlist_count) {
        return false;
    }
    copy_text(name, name_capacity, playlist_name(active_cache, playlist_index));
    if (track_count != nullptr) {
        *track_count = active_cache.playlists[playlist_index].track_count;
    }
    return true;
}

bool music_library_playlist_track_get(size_t playlist_index,
                                      size_t playlist_track_index,
                                      uint16_t *global_track_index,
                                      char *path, size_t path_capacity,
                                      char *name, size_t name_capacity) {
    LockGuard lock(track_mutex);
    if (!lock.locked() || playlist_index >= active_cache.playlist_count) {
        return false;
    }
    const PlaylistDescriptor &playlist = active_cache.playlists[playlist_index];
    if (playlist_track_index >= playlist.track_count) return false;
    const size_t global_index = playlist_index == 0U
                                    ? playlist_track_index
                                    : active_cache.playlist_tracks[
                                          playlist.first_track + playlist_track_index];
    const char *stored_path = cache_path(active_cache, global_index);
    if (stored_path == nullptr) return false;
    if (global_track_index != nullptr) {
        *global_track_index = static_cast<uint16_t>(global_index);
    }
    copy_text(path, path_capacity, stored_path);
    copy_text(name, name_capacity, base_name(stored_path));
    return true;
}

bool music_library_playlist_find_track(size_t playlist_index,
                                       size_t global_track_index,
                                       uint16_t *playlist_track_index) {
    LockGuard lock(track_mutex);
    if (!lock.locked() || playlist_track_index == nullptr ||
        playlist_index >= active_cache.playlist_count ||
        global_track_index >= active_cache.count) {
        return false;
    }
    const PlaylistDescriptor &playlist = active_cache.playlists[playlist_index];
    if (playlist_index == 0U) {
        *playlist_track_index = static_cast<uint16_t>(global_track_index);
        return true;
    }
    for (uint16_t index = 0U; index < playlist.track_count; ++index) {
        if (active_cache.playlist_tracks[playlist.first_track + index] ==
            global_track_index) {
            *playlist_track_index = index;
            return true;
        }
    }
    return false;
}
