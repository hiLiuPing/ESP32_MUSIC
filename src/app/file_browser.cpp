#include "app/file_browser.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "bsp/bsp_littlefs.h"
#include "bsp/bsp_storage.h"

namespace {
bool ends_with_ignore_case(const char *path, const char *extension) {
    if (path == nullptr || extension == nullptr) return false;
    const size_t path_length = std::strlen(path);
    const size_t extension_length = std::strlen(extension);
    if (path_length < extension_length) return false;
    for (size_t i = 0U; i < extension_length; ++i) {
        if (std::tolower(static_cast<unsigned char>(path[path_length - extension_length + i])) !=
            std::tolower(static_cast<unsigned char>(extension[i]))) return false;
    }
    return true;
}

fs::FS *filesystem_for(FileBrowserStorage storage) {
    return storage == FileBrowserStorage::SdCard ? &bsp_storage_fs() : &bsp_littlefs_fs();
}

bool lock_flash(FileBrowserStorage storage) {
    return storage != FileBrowserStorage::InternalFlash ||
           bsp_littlefs_lock(pdMS_TO_TICKS(200U));
}

void unlock_flash(FileBrowserStorage storage) {
    if (storage == FileBrowserStorage::InternalFlash) bsp_littlefs_unlock();
}

const char *name_from_path(const char *path) {
    const char *slash = path == nullptr ? nullptr : std::strrchr(path, '/');
    return slash == nullptr ? (path == nullptr ? "" : path) : slash + 1;
}
}

bool file_browser_storage_available(FileBrowserStorage storage) {
    return storage == FileBrowserStorage::SdCard
               ? (bsp_storage_available() || bsp_storage_init())
               : bsp_littlefs_available();
}

size_t file_browser_list(FileBrowserStorage storage, const char *directory,
                         FileBrowserEntry *entries, size_t capacity) {
    if (entries == nullptr || capacity == 0U || directory == nullptr ||
        !file_browser_storage_available(storage) || !lock_flash(storage)) return 0U;
    fs::FS *filesystem = filesystem_for(storage);
    File root = filesystem->open(directory, FILE_READ);
    if (!root || !root.isDirectory()) {
        root.close();
        unlock_flash(storage);
        return 0U;
    }
    size_t count = 0U;
    for (File file = root.openNextFile(FILE_READ); file && count < capacity;
         file = root.openNextFile(FILE_READ)) {
        FileBrowserEntry &entry = entries[count];
        std::memset(&entry, 0, sizeof(entry));
        std::snprintf(entry.path, sizeof(entry.path), "%s", file.path());
        std::snprintf(entry.name, sizeof(entry.name), "%s", name_from_path(file.path()));
        entry.directory = file.isDirectory();
        entry.size_bytes = entry.directory ? 0U : static_cast<uint32_t>(file.size());
        entry.modified = file.getLastWrite();
        file.close();
        ++count;
    }
    root.close();
    unlock_flash(storage);
    std::sort(entries, entries + count, [](const FileBrowserEntry &left,
                                            const FileBrowserEntry &right) {
        if (left.directory != right.directory) return left.directory;
        return strcasecmp(left.name, right.name) < 0;
    });
    return count;
}

bool file_browser_delete(FileBrowserStorage storage, const char *path) {
    if (path == nullptr || path[0] != '/' || !file_browser_storage_available(storage) ||
        !lock_flash(storage)) return false;
    fs::FS *filesystem = filesystem_for(storage);
    File file = filesystem->open(path, FILE_READ);
    const bool removable = file && !file.isDirectory();
    file.close();
    const bool removed = removable && filesystem->remove(path);
    unlock_flash(storage);
    return removed;
}

bool file_browser_is_audio(const char *path) {
    constexpr const char *extensions[] = {".mp3", ".aac", ".m4a", ".wav", ".flac"};
    for (const char *extension : extensions) {
        if (ends_with_ignore_case(path, extension)) return true;
    }
    return false;
}

bool file_browser_is_text(const char *path) { return ends_with_ignore_case(path, ".txt"); }
