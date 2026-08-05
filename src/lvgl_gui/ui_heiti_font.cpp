#include "gui/ui_heiti_font.h"

#include <cstring>

#include "bsp/bsp_littlefs.h"
#include "font/binfont_loader/lv_binfont_loader.h"

#ifndef LVGL_HEITI_BINFONT_ENABLE
#define LVGL_HEITI_BINFONT_ENABLE 0
#endif

namespace {
struct FontSlot { uint8_t size; const char *path; lv_font_t *font; };
FontSlot slots[] = {{16, "F:/heiti_4_16.bin", nullptr}, {18, "F:/heiti_4_18.bin", nullptr}, {20, "F:/heiti_4_20.bin", nullptr}};
FontSlot &slot(uint8_t size) { return size <= 16 ? slots[0] : size <= 18 ? slots[1] : slots[2]; }
const lv_font_t *fallback_font() { return &lv_font_montserrat_16; }
}

const lv_font_t *ui_heiti_font_get(uint8_t size) {
    FontSlot &item = slot(size);
#if LVGL_HEITI_BINFONT_ENABLE
    if (item.font == nullptr && bsp_littlefs_available()) {
        item.font = lv_binfont_create(item.path);
    }
#else
    (void)item;
#endif
    return item.font != nullptr ? item.font : fallback_font();
}
const lv_font_t *ui_heiti_font_get_cached(uint8_t size) { return ui_heiti_font_get(size); }
bool ui_heiti_font_is_ready(uint8_t size) { return slot(size).font != nullptr; }
bool ui_heiti_font_warm_text(uint8_t, const char *) { return true; }
bool ui_heiti_font_poetry_cache_init(size_t) { return true; }
bool ui_heiti_font_cache_text(uint8_t, const char *) { return true; }
bool ui_heiti_font_text_is_cached(uint8_t, const char *) { return true; }
size_t ui_heiti_font_poetry_cache_bytes() { return 0; }
size_t ui_heiti_font_poetry_cache_glyphs() { return 0; }
uint32_t ui_heiti_font_storage_read_count() { return 0; }
