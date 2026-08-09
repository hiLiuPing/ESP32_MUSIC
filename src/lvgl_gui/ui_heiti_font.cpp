#include "gui/ui_heiti_font.h"

#include <Arduino.h>
#include <cstring>

#include "bsp/bsp_littlefs.h"

namespace {
struct FontSlot { uint8_t size; const char *path; const lv_font_t *font; };
FontSlot slots[] = {
    {16, "F:/heiti_4_16.bin", nullptr},
    {18, "F:/heiti_4_18.bin", nullptr},
    {20, "F:/heiti_4_20.bin", nullptr},
};
FontSlot &slot(uint8_t size) { return size <= 16 ? slots[0] : size <= 18 ? slots[1] : slots[2]; }
const lv_font_t *fallback_font() {
#if LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    return &lv_font_source_han_sans_sc_16_cjk;
#else
    return &lv_font_montserrat_16;
#endif
}
}

const lv_font_t *ui_heiti_font_get(uint8_t size) {
    FontSlot &item = slot(size);
    // The on-demand LVGL CJK font is linked in flash and does not load the
    // multi-megabyte LittleFS binfont into the LVGL heap. Keep the binfont
    // paths in the slot table so the application API remains unchanged.
    (void)item;
    static bool reported = false;
    if (!reported) {
        reported = true;
        Serial.printf("[LVGL_FONT] using flash CJK fallback; LittleFS binfonts remain on-demand (%s)\n",
                      bsp_littlefs_available() ? "available" : "unavailable");
    }
    if (item.font == nullptr) item.font = fallback_font();
    return item.font != nullptr ? item.font : fallback_font();
}
const lv_font_t *ui_heiti_font_get_cached(uint8_t size) { return ui_heiti_font_get(size); }
bool ui_heiti_font_is_ready(uint8_t size) { return slot(size).font != nullptr || fallback_font() != nullptr; }
bool ui_heiti_font_warm_text(uint8_t, const char *) { return true; }
bool ui_heiti_font_poetry_cache_init(size_t) { return true; }
bool ui_heiti_font_cache_text(uint8_t, const char *) { return true; }
bool ui_heiti_font_text_is_cached(uint8_t, const char *) { return true; }
size_t ui_heiti_font_poetry_cache_bytes() { return 0; }
size_t ui_heiti_font_poetry_cache_glyphs() { return 0; }
uint32_t ui_heiti_font_storage_read_count() { return 0; }
