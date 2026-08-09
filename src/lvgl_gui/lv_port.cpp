#include "gui/lv_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "bsp/bsp_display.h"

namespace {
constexpr uint16_t kDisplayWidth = 384;
constexpr uint16_t kDisplayHeight = 168;
constexpr uint16_t kBufferRows = 8;
constexpr size_t kBufferBytes = kDisplayWidth * kBufferRows * sizeof(uint16_t);

lv_display_t *s_display = nullptr;
void *s_draw_buffer = nullptr;
uint32_t s_flush_count = 0;
uint32_t s_display_count = 0;

uint32_t tick_millis() {
    return static_cast<uint32_t>(millis());
}

void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    const int16_t width = static_cast<int16_t>(area->x2 - area->x1 + 1);
    const int16_t height = static_cast<int16_t>(area->y2 - area->y1 + 1);
    if (width > 0 && height > 0) {
        bsp_display().blitRgb565(area->x1, area->y1, width, height,
                                 reinterpret_cast<const uint16_t *>(pixels));
    }
    ++s_flush_count;
    const bool is_last = lv_display_flush_is_last(display);
    if (is_last) {
        bsp_display().display();
        ++s_display_count;
    }
    if (s_flush_count == 1U || s_display_count == 1U ||
        (s_flush_count % 100U) == 0U) {
        Serial.printf("[LVGL] flush=%lu display=%lu area=(%d,%d)-(%d,%d)\n",
                      static_cast<unsigned long>(s_flush_count),
                      static_cast<unsigned long>(s_display_count), area->x1,
                      area->y1, area->x2, area->y2);
    }
    lv_display_flush_ready(display);
}
}

bool lv_port_start() {
    if (s_display != nullptr) return true;

    lv_init();
    lv_tick_set_cb(tick_millis);
    s_draw_buffer = heap_caps_aligned_alloc(4, kBufferBytes,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_draw_buffer == nullptr) {
        s_draw_buffer = heap_caps_aligned_alloc(4, kBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_draw_buffer == nullptr) {
        Serial.println("[LVGL] draw buffer allocation failed");
        return false;
    }

    s_display = lv_display_create(kDisplayWidth, kDisplayHeight);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, s_draw_buffer, nullptr, kBufferBytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, flush);
    bsp_display().clearDisplay();
    return true;
}

void lv_port_poll() {
    if (s_display != nullptr) (void)lv_timer_handler();
}

void lv_port_force_refresh() {
    if (s_display != nullptr) lv_obj_invalidate(lv_screen_active());
}

lv_display_t *lv_port_display() { return s_display; }
