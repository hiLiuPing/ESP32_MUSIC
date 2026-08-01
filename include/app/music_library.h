#pragma once

#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "app/player_types.h"

void music_library_attach_mutex(SemaphoreHandle_t mutex);
bool music_library_scan(fs::FS &filesystem);
size_t music_library_count();
uint32_t music_library_version();
bool music_library_get(size_t index, char *path, size_t path_capacity,
                       char *name, size_t name_capacity);
size_t music_library_playlist_count();
bool music_library_playlist_get(size_t playlist_index,
                                char *name, size_t name_capacity,
                                size_t *track_count);
bool music_library_playlist_track_get(size_t playlist_index,
                                      size_t playlist_track_index,
                                      uint16_t *global_track_index,
                                      char *path, size_t path_capacity,
                                      char *name, size_t name_capacity);
bool music_library_playlist_find_track(size_t playlist_index,
                                       size_t global_track_index,
                                       uint16_t *playlist_track_index);
