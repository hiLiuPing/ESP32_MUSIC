#pragma once

#include <Arduino.h>

#include "app/poetry_app.h"

constexpr uint8_t UI_POETRY_BATCH_SIZE = 10U;

struct UiPoetryCacheEntry {
    const char *title;
    const char *body;
    PoetryCollection collection;
    uint32_t content_hash;
};

bool ui_poetry_cache_init();
void ui_poetry_cache_activate(PoetryCollection collection);
bool ui_poetry_cache_service();
bool ui_poetry_cache_is_ready();
const UiPoetryCacheEntry *ui_poetry_cache_current();
const UiPoetryCacheEntry *ui_poetry_cache_move(int8_t direction);
const UiPoetryCacheEntry *ui_poetry_cache_take_for_popup(PoetryCollection collection);
uint8_t ui_poetry_cache_active_index();
