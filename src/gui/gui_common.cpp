#include "gui/gui_common.h"

#include <cstdio>
#include <cstring>

#include "bsp/bsp_display.h"

void gui_draw_text(int16_t x, int16_t y, const char *text, bool inverted) {
    U8G2_FOR_ST73XX &font = bsp_fonts();
    font.setForegroundColor(inverted ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);
    font.drawUTF8(x, y, text == nullptr ? "" : text);
    font.setForegroundColor(ST7305_COLOR_BLACK);
}

void gui_draw_header(const char *title) {
    bsp_display().drawFastHLine(0, 21, 384, ST7305_COLOR_BLACK);
    gui_draw_text(8, 16, title);
}

void gui_copy_utf8_fitted(const char *source, char *destination,
                          size_t capacity, int16_t width) {
    if ((destination == nullptr) || (capacity == 0)) {
        return;
    }
    destination[0] = '\0';
    if (source == nullptr) {
        return;
    }

    size_t source_pos = 0;
    size_t output_pos = 0;
    while ((source[source_pos] != '\0') && (output_pos + 4 < capacity)) {
        const unsigned char first = static_cast<unsigned char>(source[source_pos]);
        size_t glyph_bytes = 1;
        if ((first & 0xE0) == 0xC0) glyph_bytes = 2;
        else if ((first & 0xF0) == 0xE0) glyph_bytes = 3;
        else if ((first & 0xF8) == 0xF0) glyph_bytes = 4;

        bool complete = true;
        uint16_t codepoint = first;
        for (size_t index = 1; index < glyph_bytes; ++index) {
            const unsigned char next = static_cast<unsigned char>(source[source_pos + index]);
            if ((next == 0) || ((next & 0xC0) != 0x80)) {
                complete = false;
            }
        }
        if (complete && (glyph_bytes > 1)) {
            if (glyph_bytes == 2) codepoint &= 0x1F;
            else if (glyph_bytes == 3) codepoint &= 0x0F;
            else codepoint &= 0x07;
            for (size_t index = 1; index < glyph_bytes; ++index) {
                codepoint = static_cast<uint16_t>((codepoint << 6) |
                    (static_cast<unsigned char>(source[source_pos + index]) & 0x3F));
            }
        }

        const bool supported = complete && (glyph_bytes <= 3) &&
                               u8g2_IsGlyph(&bsp_fonts().u8g2, codepoint);
        if (supported) {
            std::memcpy(destination + output_pos, source + source_pos, glyph_bytes);
            output_pos += glyph_bytes;
            source_pos += glyph_bytes;
        } else {
            destination[output_pos++] = '?';
            source_pos += complete ? glyph_bytes : 1;
        }
        destination[output_pos] = '\0';
        if (bsp_fonts().getUTF8Width(destination) > width) {
            output_pos -= supported ? glyph_bytes : 1;
            destination[output_pos] = '\0';
            break;
        }
    }
}

void gui_format_time(uint32_t seconds, char *buffer, size_t capacity) {
    if ((buffer == nullptr) || (capacity == 0)) {
        return;
    }
    std::snprintf(buffer, capacity, "%02lu:%02lu",
                  static_cast<unsigned long>(seconds / 60),
                  static_cast<unsigned long>(seconds % 60));
}
