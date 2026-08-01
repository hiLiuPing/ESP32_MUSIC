#pragma once

#include <Arduino.h>

#include "font/egui_font.h"

const egui_font_t *ui_heiti_font_get(uint8_t size);
const egui_font_t *ui_heiti_font_get_cached(uint8_t size);
bool ui_heiti_font_is_ready(uint8_t size);
bool ui_heiti_font_warm_text(uint8_t size, const char *text);
bool ui_heiti_font_poetry_cache_init(size_t max_bytes);
bool ui_heiti_font_cache_text(uint8_t size, const char *text);
bool ui_heiti_font_text_is_cached(uint8_t size, const char *text);
size_t ui_heiti_font_poetry_cache_bytes();
size_t ui_heiti_font_poetry_cache_glyphs();
uint32_t ui_heiti_font_storage_read_count();
