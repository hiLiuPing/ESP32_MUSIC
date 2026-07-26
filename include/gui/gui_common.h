#pragma once

#include <Arduino.h>

#include "gui/egui_port.h"

struct GuiEguiView {
    egui_view_t base;
    void (*draw)(egui_canvas_t *canvas);
};

void gui_egui_view_init(GuiEguiView *view, egui_core_t *core,
                        void (*draw)(egui_canvas_t *canvas));
void gui_draw_text(egui_canvas_t *canvas, int16_t x, int16_t y,
                   const char *text, bool inverted = false);
void gui_draw_header(egui_canvas_t *canvas, const char *title);
void gui_draw_page_background(egui_canvas_t *canvas);
void gui_copy_utf8_fitted(const char *source, char *destination,
                          size_t capacity, int16_t width);
void gui_format_time(uint32_t seconds, char *buffer, size_t capacity);
