#pragma once

#include <Arduino.h>

#include "app/poetry_app.h"
#include "gui/egui_port.h"

constexpr uint8_t UI_POETRY_CACHE_SLOT_COUNT = 10U;
constexpr uint8_t UI_POETRY_FONT_SIZE = 18U;
constexpr uint8_t UI_POETRY_MAX_LINES = 6U;
constexpr int16_t UI_POETRY_PANEL_X = 32;
constexpr int16_t UI_POETRY_PANEL_Y = 4;
constexpr int16_t UI_POETRY_PANEL_W = 320;
constexpr int16_t UI_POETRY_PANEL_H = 160;
constexpr int16_t UI_POETRY_PANEL_RADIUS = 10;
constexpr int16_t UI_POETRY_TEXT_PAD_X = 12;
constexpr int16_t UI_POETRY_TEXT_PAD_Y = 6;
constexpr int16_t UI_POETRY_DISPLAY_W = UI_POETRY_PANEL_W - 2 * UI_POETRY_TEXT_PAD_X;
constexpr int16_t UI_POETRY_TITLE_LINE_H = 22;
constexpr int16_t UI_POETRY_TITLE_BODY_GAP = 2;
constexpr int16_t UI_POETRY_LINE_STEP = 22;
constexpr int16_t UI_POETRY_BODY_H =
    UI_POETRY_PANEL_H - 2 * UI_POETRY_TEXT_PAD_Y -
    UI_POETRY_TITLE_LINE_H - UI_POETRY_TITLE_BODY_GAP;
constexpr int16_t UI_POETRY_FIRST_LINE_INDENT = UI_POETRY_FONT_SIZE;

struct UiPoetryCacheSlot {
    PoetryCollection collection;
    uint32_t content_hash;
    bool valid;
    bool in_use;
    bool consumed;
    char title[128];
    char body[3072];
    const char *lines[UI_POETRY_MAX_LINES];
    uint8_t line_count;
    int16_t title_width;
    int16_t body_height;
    int16_t line_x[UI_POETRY_MAX_LINES];
    int16_t line_y[UI_POETRY_MAX_LINES];
};

bool ui_poetry_cache_init();
bool ui_poetry_cache_service();
const UiPoetryCacheSlot *ui_poetry_cache_select(PoetryCollection collection);
void ui_poetry_cache_release(const UiPoetryCacheSlot *slot);
size_t ui_poetry_cache_ready_count(PoetryCollection collection);
void ui_poetry_cache_draw(egui_canvas_t *canvas, const UiPoetryCacheSlot *slot,
                          int16_t panel_x = UI_POETRY_PANEL_X,
                          int16_t panel_y = UI_POETRY_PANEL_Y);
