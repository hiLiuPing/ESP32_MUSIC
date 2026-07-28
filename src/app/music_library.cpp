#include "app/music_library.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
struct TrackEntry {
    char path[PLAYER_PATH_LENGTH];
};

TrackEntry tracks[PLAYER_MAX_TRACKS];
size_t track_count = 0;
SemaphoreHandle_t track_mutex = nullptr;

class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        locked_ = (mutex_ != nullptr) && (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
    }
    ~LockGuard() {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }
    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_ = false;
};

bool is_mp3_path(const char *path) {
    if (path == nullptr) {
        return false;
    }
    const size_t length = std::strlen(path);
    if (length < 4) {
        return false;
    }
    const char *extension = path + length - 4;
    return (extension[0] == '.') &&
           (std::tolower(static_cast<unsigned char>(extension[1])) == 'm') &&
           (std::tolower(static_cast<unsigned char>(extension[2])) == 'p') &&
           (extension[3] == '3');
}

constexpr uint8_t MAX_SCAN_DEPTH = 16;
constexpr const char *MUSIC_ROOT = "/music";

void scan_directory(fs::FS &filesystem, const char *directory_path, uint8_t depth) {
    if ((track_count >= PLAYER_MAX_TRACKS) || (depth > MAX_SCAN_DEPTH)) {
        return;
    }

    File directory = filesystem.open(directory_path);
    if (!directory || !directory.isDirectory()) {
        return;
    }

    for (File entry = directory.openNextFile(); entry && (track_count < PLAYER_MAX_TRACKS);
         entry = directory.openNextFile()) {
        const char *entry_path = entry.path();
        if (entry.isDirectory()) {
            if ((entry_path != nullptr) && (std::strlen(entry_path) < PLAYER_PATH_LENGTH)) {
                scan_directory(filesystem, entry_path, depth + 1);
            }
        } else if (is_mp3_path(entry_path) &&
                   (std::strlen(entry_path) < PLAYER_PATH_LENGTH)) {
            std::strncpy(tracks[track_count].path, entry_path, PLAYER_PATH_LENGTH - 1);
            tracks[track_count].path[PLAYER_PATH_LENGTH - 1] = '\0';
            ++track_count;
        }
        entry.close();
    }
    directory.close();
}

const char *base_name(const char *path) {
    const char *slash = std::strrchr(path, '/');
    return (slash == nullptr) ? path : slash + 1;
}
}

void music_library_attach_mutex(SemaphoreHandle_t mutex) {
    track_mutex = mutex;
}

bool music_library_scan(fs::FS &filesystem) {
    LockGuard lock(track_mutex);
    if (!lock.locked()) {
        return false;
    }

    track_count = 0;
    std::memset(tracks, 0, sizeof(tracks));
    scan_directory(filesystem, MUSIC_ROOT, 0);
    std::sort(tracks, tracks + track_count, [](const TrackEntry &left, const TrackEntry &right) {
        return strcasecmp(left.path, right.path) < 0;
    });
    return true;
}

size_t music_library_count() {
    LockGuard lock(track_mutex);
    return lock.locked() ? track_count : 0;
}

bool music_library_get(size_t index, char *path, size_t path_capacity,
                       char *name, size_t name_capacity) {
    LockGuard lock(track_mutex);
    if (!lock.locked() || (index >= track_count)) {
        return false;
    }
    if ((path != nullptr) && (path_capacity > 0)) {
        std::strncpy(path, tracks[index].path, path_capacity - 1);
        path[path_capacity - 1] = '\0';
    }
    if ((name != nullptr) && (name_capacity > 0)) {
        std::strncpy(name, base_name(tracks[index].path), name_capacity - 1);
        name[name_capacity - 1] = '\0';
    }
    return true;
}
