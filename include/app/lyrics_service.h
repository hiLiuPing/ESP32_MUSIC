#pragma once

#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

constexpr size_t LYRICS_LINE_BUFFER_SIZE = 256U;

void lyrics_service_attach_mutex(SemaphoreHandle_t mutex);
bool lyrics_service_load(fs::FS &filesystem, const char *audio_path);
void lyrics_service_clear();
bool lyrics_service_get_current_line(uint32_t elapsed_seconds,
                                     char *out, size_t out_capacity);
