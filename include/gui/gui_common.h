#pragma once

#include <Arduino.h>

void gui_draw_text(int16_t x, int16_t y, const char *text, bool inverted = false);
void gui_draw_header(const char *title);
void gui_copy_utf8_fitted(const char *source, char *destination,
                          size_t capacity, int16_t width);
void gui_format_time(uint32_t seconds, char *buffer, size_t capacity);
