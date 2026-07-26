#pragma once

#include <Arduino.h>

enum class PoetryCollection : uint8_t { Song300, Tang300, Song3000, ChinaQuotes };

struct PoetryEntry {
    const char *title;
    const char *author;
    const char *body;
    PoetryCollection collection;
    uint32_t serial;
    bool valid;
};

void poetry_app_init();
bool poetry_app_get_random(PoetryCollection collection, PoetryEntry *out);
bool poetry_app_get_cached(PoetryEntry *out);
const char *poetry_app_collection_name(PoetryCollection collection);
