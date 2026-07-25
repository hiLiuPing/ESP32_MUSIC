#pragma once

#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "app/player_types.h"

void music_library_attach_mutex(SemaphoreHandle_t mutex);
bool music_library_scan(fs::FS &filesystem);
size_t music_library_count();
bool music_library_get(size_t index, char *path, size_t path_capacity,
                       char *name, size_t name_capacity);
