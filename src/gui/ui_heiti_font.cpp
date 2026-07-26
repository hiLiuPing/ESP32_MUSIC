#include "gui/ui_heiti_font.h"

#include <LittleFS.h>
#include <cstdio>
#include <cstring>

#include "bsp/bsp_littlefs.h"
#include "resource/egui_resource.h"

namespace {
constexpr uint32_t ASCII_START = 0x20U;
constexpr uint32_t ASCII_END = 0x7FU;
constexpr uint32_t CJK_START = 0x4E00U;
constexpr uint32_t CJK_END = 0x9FA5U;
constexpr uint32_t FULL_START = 0xFF01U;
constexpr uint32_t FULL_END = 0xFF60U;
constexpr uint32_t LATIN1_START = 0x00B0U;
constexpr uint32_t LATIN1_END = 0x00BEU;
constexpr uint32_t CJK_PUNC_START = 0x3000U;
constexpr uint32_t CJK_PUNC_END = 0x303FU;
constexpr uint32_t ASCII_GLYPHS = ASCII_END - ASCII_START + 1U;
constexpr uint32_t CJK_GLYPHS = CJK_END - CJK_START + 1U;
constexpr uint32_t FULL_GLYPHS = FULL_END - FULL_START + 1U;
constexpr uint32_t LATIN1_GLYPHS = LATIN1_END - LATIN1_START + 1U;

struct FontContext {
    egui_font_t base;
    uint8_t size;
    bool ready;
    File file;
    const egui_font_t *fallback;
};

FontContext contexts[6] = {};
bool initialized = false;

const egui_font_t *fallback_for(uint8_t size) {
    if (size <= 12U) return EGUI_FONT_OF(&egui_res_font_montserrat_12_4);
    if (size <= 16U) return EGUI_FONT_OF(&egui_res_font_montserrat_16_4);
    if (size <= 18U) return EGUI_FONT_OF(&egui_res_font_montserrat_18_4);
    return EGUI_FONT_OF(&egui_res_font_montserrat_20_4);
}

FontContext *context_for(const egui_font_t *font) {
    return font == nullptr ? nullptr : static_cast<FontContext *>(const_cast<void *>(font->res));
}

int font_draw(const egui_font_t *font, egui_canvas_t *canvas, const void *string,
              egui_dim_t x, egui_dim_t y, egui_color_t color, egui_alpha_t alpha);
int font_size(const egui_font_t *font, const void *string, uint8_t multi,
              egui_dim_t line_space, egui_dim_t *width, egui_dim_t *height);
const egui_font_api_t api = {font_draw, font_size};

int utf8_advance(const char *text, uint32_t *cp) {
    return egui_font_get_utf8_code_fast(text, cp);
}

uint32_t normalize_codepoint(uint32_t cp) {
    switch (cp) {
        case 0x0009U:
        case 0x000AU:
        case 0x000DU: return 0x20U;
        case 0x2018U:
        case 0x2019U: return 0x27U;
        case 0x201CU:
        case 0x201DU: return 0x22U;
        case 0x300AU: return 0xFF1CU;
        case 0x300BU: return 0xFF1EU;
        default: return cp;
    }
}

int32_t glyph_index(uint32_t cp) {
    cp = normalize_codepoint(cp);
    if (cp >= ASCII_START && cp <= ASCII_END) return static_cast<int32_t>(cp - ASCII_START);
    if (cp >= CJK_START && cp <= CJK_END) {
        return static_cast<int32_t>(ASCII_GLYPHS + cp - CJK_START);
    }
    if (cp >= FULL_START && cp <= FULL_END) {
        return static_cast<int32_t>(ASCII_GLYPHS + CJK_GLYPHS + cp - FULL_START);
    }
    if (cp >= LATIN1_START && cp <= LATIN1_END) {
        return static_cast<int32_t>(ASCII_GLYPHS + CJK_GLYPHS + FULL_GLYPHS + cp - LATIN1_START);
    }
    if (cp >= CJK_PUNC_START && cp <= CJK_PUNC_END) {
        return static_cast<int32_t>(ASCII_GLYPHS + CJK_GLYPHS + FULL_GLYPHS + LATIN1_GLYPHS + cp - CJK_PUNC_START);
    }
    return -1;
}

uint32_t glyph_bytes(uint8_t size) {
    return (static_cast<uint32_t>(size) * size + 7U) / 8U;
}

uint32_t glyph_count() {
    return ASCII_GLYPHS + CJK_GLYPHS + FULL_GLYPHS + LATIN1_GLYPHS +
           (CJK_PUNC_END - CJK_PUNC_START + 1U);
}

int font_draw(const egui_font_t *font, egui_canvas_t *canvas, const void *string,
              egui_dim_t x, egui_dim_t y, egui_color_t color, egui_alpha_t alpha) {
    FontContext *ctx = context_for(font);
    const char *text = static_cast<const char *>(string);
    if (ctx == nullptr || canvas == nullptr || text == nullptr) return 0;
    egui_dim_t pen_x = x;
    int consumed = 0;
    while (*text != '\0') {
        if (*text == '\r') {
            ++consumed;
            ++text;
            continue;
        }
        if (*text == '\n') {
            ++consumed;
            break;
        }
        uint32_t cp = 0U;
        const int bytes = utf8_advance(text, &cp);
        if (bytes <= 0) break;
        const int32_t index = glyph_index(cp);
        if (ctx->ready && index >= 0) {
            const uint32_t glyph_size = glyph_bytes(ctx->size);
            uint8_t bitmap[64] = {};
            bool glyph_loaded = false;
            if (glyph_size <= sizeof(bitmap) && bsp_littlefs_lock(pdMS_TO_TICKS(20U))) {
                glyph_loaded = ctx->file.seek(static_cast<uint32_t>(index) * glyph_size) &&
                               ctx->file.read(bitmap, glyph_size) == glyph_size;
                bsp_littlefs_unlock();
            }
            if (glyph_loaded) {
                for (uint8_t gy = 0U; gy < ctx->size; ++gy) {
                    for (uint8_t gx = 0U; gx < ctx->size; ++gx) {
                        const uint32_t bit = static_cast<uint32_t>(gy) * ctx->size + gx;
                        if ((bitmap[bit >> 3] & (0x80U >> (bit & 7U))) != 0U) {
                            egui_canvas_draw_point(canvas, pen_x + gx, y + gy, color, alpha);
                        }
                    }
                }
            }
            pen_x += ctx->size;
        } else {
            char one[2] = {'?', '\0'};
            if (cp >= 0x20U && cp <= 0x7FU && bytes == 1) one[0] = text[0];
            egui_canvas_draw_text(canvas, ctx->fallback, one, pen_x, y, color, alpha);
            pen_x += static_cast<egui_dim_t>(ctx->size / 2U);
        }
        text += bytes;
        consumed += bytes;
    }
    return consumed;
}

int font_size(const egui_font_t *font, const void *string, uint8_t multi,
              egui_dim_t line_space, egui_dim_t *width, egui_dim_t *height) {
    FontContext *ctx = context_for(font);
    const char *text = static_cast<const char *>(string);
    if (ctx == nullptr || text == nullptr || width == nullptr || height == nullptr) return 0;
    egui_dim_t line_width = 0;
    egui_dim_t max_width = 0;
    uint8_t lines = 1U;
    while (*text != '\0') {
        if (*text == '\n' && multi) { if (line_width > max_width) max_width = line_width; line_width = 0; ++lines; ++text; continue; }
        uint32_t cp = 0U; const int bytes = utf8_advance(text, &cp); if (bytes <= 0) break;
        line_width += glyph_index(cp) >= 0 ? ctx->size : static_cast<egui_dim_t>(ctx->size / 2U);
        text += bytes;
    }
    if (line_width > max_width) max_width = line_width;
    *width = max_width; *height = static_cast<egui_dim_t>(lines * ctx->size + (lines > 1U ? (lines - 1U) * line_space : 0U));
    return 0;
}

void init_contexts() {
    if (!initialized) {
        const uint8_t sizes[6] = {10U, 12U, 14U, 16U, 18U, 20U};
        for (uint8_t i = 0U; i < 6U; ++i) {
            FontContext &ctx = contexts[i];
            ctx.base.res = &ctx;
            ctx.base.api = &api;
            ctx.size = sizes[i];
            ctx.fallback = fallback_for(ctx.size);
        }
        initialized = true;
    }
    if (!bsp_littlefs_available()) return;
    for (uint8_t i = 0U; i < 6U; ++i) {
        FontContext &ctx = contexts[i];
        if (ctx.ready) continue;
        char path[32] = {};
        std::snprintf(path, sizeof(path), "/heiti_1_%u.bin", static_cast<unsigned>(ctx.size));
        if (!bsp_littlefs_lock(pdMS_TO_TICKS(100U))) continue;
        if (ctx.file) ctx.file.close();
        ctx.file = bsp_littlefs_fs().open(path, "r");
        const uint64_t expected = static_cast<uint64_t>(glyph_count()) * glyph_bytes(ctx.size);
        ctx.ready = ctx.file && static_cast<uint64_t>(ctx.file.size()) == expected;
        bsp_littlefs_unlock();
    }
}
}

const egui_font_t *ui_heiti_font_get(uint8_t size) {
    init_contexts();
    uint8_t index = size <= 10U ? 0U : size <= 12U ? 1U : size <= 14U ? 2U : size <= 16U ? 3U : size <= 18U ? 4U : 5U;
    return &contexts[index].base;
}

bool ui_heiti_font_is_ready(uint8_t size) {
    init_contexts();
    return context_for(ui_heiti_font_get(size))->ready;
}
