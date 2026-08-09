#pragma once

#include <Arduino.h>

#include <lvgl.h>

const lv_font_t *ui_heiti_font_get(uint8_t size);
const lv_font_t *ui_heiti_font_get_cached(uint8_t size);
bool ui_heiti_font_is_ready(uint8_t size);
bool ui_heiti_font_warm_text(uint8_t size, const char *text);
bool ui_heiti_font_poetry_cache_init(size_t max_bytes);
void ui_heiti_font_poetry_cache_reset();
bool ui_heiti_font_cache_codepoint(uint8_t size, uint32_t codepoint);
bool ui_heiti_font_cache_text(uint8_t size, const char *text);
bool ui_heiti_font_text_is_cached(uint8_t size, const char *text);
size_t ui_heiti_font_poetry_cache_bytes();
size_t ui_heiti_font_poetry_cache_glyphs();
uint32_t ui_heiti_font_storage_read_count();
