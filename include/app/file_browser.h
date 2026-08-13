#pragma once

#include <Arduino.h>
#include <ctime>

enum class FileBrowserStorage : uint8_t { SdCard, InternalFlash };

constexpr size_t FILE_BROWSER_PATH_LENGTH = 256U;
constexpr size_t FILE_BROWSER_NAME_LENGTH = 96U;
constexpr size_t FILE_BROWSER_LIST_LIMIT = 48U;

struct FileBrowserEntry {
    char path[FILE_BROWSER_PATH_LENGTH];
    char name[FILE_BROWSER_NAME_LENGTH];
    uint32_t size_bytes;
    time_t modified;
    bool directory;
};

bool file_browser_storage_available(FileBrowserStorage storage);
size_t file_browser_list(FileBrowserStorage storage, const char *directory,
                         FileBrowserEntry *entries, size_t capacity);
bool file_browser_delete(FileBrowserStorage storage, const char *path);
bool file_browser_is_audio(const char *path);
bool file_browser_is_text(const char *path);
