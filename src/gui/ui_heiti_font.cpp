#include "gui/ui_heiti_font.h"

#include <LittleFS.h>
#include <Arduino.h>
#include <cstdint>
#include <cstring>
#include <esp_heap_caps.h>

#include "bsp/bsp_littlefs.h"
#include "resource/egui_resource.h"

namespace {
constexpr uint16_t MAX_CMAP = 128U;
constexpr uint32_t MAX_GLYPH_RAW = 512U;
constexpr uint8_t FMT0_FULL = 0U;
constexpr uint8_t SPARSE_FULL = 1U;
constexpr uint8_t FMT0_TINY = 2U;
constexpr uint8_t SPARSE_TINY = 3U;
constexpr uint32_t FONT_OPEN_RETRY_MS = 200U;
constexpr uint8_t GLYPH_CACHE_SLOTS = 32U;
constexpr uint16_t GLYPH_CACHE_BITMAP_SIZE = 256U;
constexpr uint16_t POETRY_HASH_EMPTY = 0U;

struct CmapEntry {
    uint32_t data_offset;
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    uint16_t data_entries_count;
    uint8_t format;
};

struct GlyphDsc {
    uint16_t advance;
    uint16_t box_w;
    uint16_t box_h;
    int16_t ofs_x;
    int16_t ofs_y;
};

struct FontContext {
    egui_font_t base;
    const char *path;
    uint8_t requested_size;
    bool ready;
    bool attempted;
    uint32_t next_retry_ms;
    File file;
    uint8_t bpp;
    uint16_t line_height;
    int16_t baseline;
    uint16_t default_advance;
    uint8_t aw_bits;
    uint8_t xy_bits;
    uint8_t wh_bits;
    uint8_t loca_format;
    uint8_t aw_format;
    uint8_t compression;
    uint32_t cmap_start;
    uint32_t loca_data_start;
    uint32_t loca_count;
    uint32_t glyf_start;
    uint32_t glyf_length;
    uint16_t cmap_count;
    CmapEntry cmap[MAX_CMAP];
    uint8_t glyph_raw[MAX_GLYPH_RAW];
    egui_font_t cached_base;
};

struct GlyphCacheEntry {
    FontContext *font;
    uint32_t cp;
    GlyphDsc dsc;
    uint32_t age;
    bool valid;
    bool missing;
    uint8_t bitmap[GLYPH_CACHE_BITMAP_SIZE];
};

FontContext contexts[3] = {
    {{nullptr, nullptr}, "/heiti_4_16.bin", 16U},
    {{nullptr, nullptr}, "/heiti_4_18.bin", 18U},
    {{nullptr, nullptr}, "/heiti_4_20.bin", 20U},
};
GlyphCacheEntry glyph_cache[GLYPH_CACHE_SLOTS] = {};
uint32_t glyph_cache_age = 0U;
GlyphCacheEntry *poetry_glyph_cache = nullptr;
size_t poetry_glyph_capacity = 0U;
size_t poetry_glyph_count = 0U;
size_t poetry_glyph_bytes = 0U;
uint16_t *poetry_glyph_index = nullptr;
size_t poetry_glyph_index_capacity = 0U;
uint32_t poetry_glyph_hash_probes = 0U;
uint32_t storage_read_count = 0U;

int font_draw(const egui_font_t *, egui_canvas_t *, const void *, egui_dim_t, egui_dim_t, egui_color_t, egui_alpha_t);
int font_size(const egui_font_t *, const void *, uint8_t, egui_dim_t, egui_dim_t *, egui_dim_t *);
int cached_font_draw(const egui_font_t *, egui_canvas_t *, const void *, egui_dim_t, egui_dim_t, egui_color_t, egui_alpha_t);
int cached_font_size(const egui_font_t *, const void *, uint8_t, egui_dim_t, egui_dim_t *, egui_dim_t *);
const egui_font_api_t api = {font_draw, font_size};
const egui_font_api_t cached_api = {cached_font_draw, cached_font_size};

FontContext *context_for(const egui_font_t *font) {
    return font == nullptr ? nullptr : static_cast<FontContext *>(const_cast<void *>(font->res));
}

uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8)); }
int16_t rd16s(const uint8_t *p) { return static_cast<int16_t>(rd16(p)); }
uint32_t rd32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool read_at(FontContext &ctx, uint32_t offset, void *buffer, size_t length) {
    if (!ctx.file || buffer == nullptr || length == 0U || !bsp_littlefs_lock(pdMS_TO_TICKS(100U))) return false;
    ++storage_read_count;
    const bool ok = ctx.file.seek(offset) && ctx.file.read(static_cast<uint8_t *>(buffer), length) == length;
    bsp_littlefs_unlock();
    return ok;
}

bool read_u16(FontContext &ctx, uint32_t offset, uint16_t *value) {
    uint8_t raw[2] = {};
    if (value == nullptr || !read_at(ctx, offset, raw, sizeof(raw))) return false;
    *value = rd16(raw);
    return true;
}

bool read_u32(FontContext &ctx, uint32_t offset, uint32_t *value) {
    uint8_t raw[4] = {};
    if (value == nullptr || !read_at(ctx, offset, raw, sizeof(raw))) return false;
    *value = rd32(raw);
    return true;
}

bool section_header(FontContext &ctx, uint32_t offset, const char label[4], uint32_t *length) {
    uint8_t raw[8] = {};
    return length != nullptr && read_at(ctx, offset, raw, sizeof(raw)) &&
           std::memcmp(raw + 4, label, 4U) == 0 && (*length = rd32(raw), *length >= 8U);
}

uint32_t bit_read(const uint8_t *data, uint32_t *position, uint8_t count) {
    uint32_t value = 0U;
    while (count-- != 0U) {
        const uint32_t index = *position >> 3;
        value = (value << 1) | ((data[index] >> (7U - (*position & 7U))) & 1U);
        ++*position;
    }
    return value;
}

int16_t bit_read_signed(const uint8_t *data, uint32_t *position, uint8_t count) {
    const uint16_t value = static_cast<uint16_t>(bit_read(data, position, count));
    if (count == 0U || (value & (1U << (count - 1U))) == 0U) return static_cast<int16_t>(value);
    return static_cast<int16_t>(value | static_cast<uint16_t>(0xFFFFU << count));
}

void bit_write(uint8_t *data, uint32_t *position, uint8_t value, uint8_t count) {
    while (count-- != 0U) {
        const uint32_t index = *position >> 3;
        const uint8_t shift = static_cast<uint8_t>(7U - (*position & 7U));
        data[index] = static_cast<uint8_t>((data[index] & ~(1U << shift)) | (((value >> count) & 1U) << shift));
        ++*position;
    }
}

uint16_t advance_from_raw(uint16_t value) { return static_cast<uint16_t>((value + 8U) >> 4); }

bool cmap_contains(const CmapEntry &entry, uint32_t cp) {
    return cp >= entry.range_start && cp < entry.range_start + entry.range_length;
}

bool lookup_entry(FontContext &ctx, const CmapEntry &entry, uint32_t cp, uint32_t *glyph) {
    if (glyph == nullptr || !cmap_contains(entry, cp)) return false;
    const uint32_t relative = cp - entry.range_start;
    const uint32_t data_start = ctx.cmap_start + entry.data_offset;
    if (entry.format == FMT0_TINY) {
        *glyph = static_cast<uint32_t>(entry.glyph_id_start) + relative;
        return true;
    }
    if (entry.format == FMT0_FULL) {
        uint8_t offset = 0U;
        if (relative >= entry.data_entries_count || !read_at(ctx, data_start + relative, &offset, 1U)) return false;
        *glyph = static_cast<uint32_t>(entry.glyph_id_start) + offset;
        return true;
    }
    if (entry.format == SPARSE_TINY || entry.format == SPARSE_FULL) {
        uint32_t left = 0U;
        uint32_t right = entry.data_entries_count;
        while (left < right) {
            const uint32_t middle = left + ((right - left) >> 1);
            uint16_t listed = 0U;
            if (!read_u16(ctx, data_start + middle * 2U, &listed)) return false;
            if (listed == relative) {
                if (entry.format == SPARSE_TINY) {
                    *glyph = static_cast<uint32_t>(entry.glyph_id_start) + middle;
                    return true;
                }
                uint16_t offset = 0U;
                const uint32_t offset_start = data_start + static_cast<uint32_t>(entry.data_entries_count) * 2U;
                if (!read_u16(ctx, offset_start + middle * 2U, &offset)) return false;
                *glyph = static_cast<uint32_t>(entry.glyph_id_start) + offset;
                return true;
            }
            if (listed < relative) left = middle + 1U;
            else right = middle;
        }
    }
    return false;
}

bool lookup(FontContext &ctx, uint32_t cp, uint32_t *glyph) {
    if (glyph == nullptr || cp == 0U) return false;
    for (uint16_t i = 0U; i < ctx.cmap_count; ++i) {
        if (cmap_contains(ctx.cmap[i], cp) && lookup_entry(ctx, ctx.cmap[i], cp, glyph)) return true;
    }
    return false;
}

uint32_t header_bits(const FontContext &ctx) {
    return static_cast<uint32_t>(ctx.aw_bits) + 2U * ctx.xy_bits + 2U * ctx.wh_bits;
}

bool glyph_span(FontContext &ctx, uint32_t glyph, uint32_t *offset, uint32_t *size) {
    if (offset == nullptr || size == nullptr || glyph >= ctx.loca_count) return false;
    const uint32_t entry_size = ctx.loca_format == 1U ? 4U : 2U;
    uint32_t current = 0U;
    if (entry_size == 4U) {
        if (!read_u32(ctx, ctx.loca_data_start + glyph * entry_size, &current)) return false;
    } else {
        uint16_t value = 0U;
        if (!read_u16(ctx, ctx.loca_data_start + glyph * entry_size, &value)) return false;
        current = value;
    }
    uint32_t next = ctx.glyf_length;
    if (glyph + 1U < ctx.loca_count) {
        if (entry_size == 4U) {
            if (!read_u32(ctx, ctx.loca_data_start + (glyph + 1U) * entry_size, &next)) return false;
        } else {
            uint16_t value = 0U;
            if (!read_u16(ctx, ctx.loca_data_start + (glyph + 1U) * entry_size, &value)) return false;
            next = value;
        }
    }
    if (next < current || next > ctx.glyf_length) return false;
    *offset = current;
    *size = next - current;
    return true;
}

GlyphDsc parse_glyph_dsc(const FontContext &ctx, const uint8_t *raw) {
    GlyphDsc dsc = {};
    uint32_t position = 0U;
    dsc.advance = ctx.aw_bits == 0U ? advance_from_raw(ctx.default_advance) :
                  (ctx.aw_format == 0U ? static_cast<uint16_t>(bit_read(raw, &position, ctx.aw_bits)) :
                                         advance_from_raw(static_cast<uint16_t>(bit_read(raw, &position, ctx.aw_bits))));
    dsc.ofs_x = bit_read_signed(raw, &position, ctx.xy_bits);
    dsc.ofs_y = bit_read_signed(raw, &position, ctx.xy_bits);
    dsc.box_w = static_cast<uint16_t>(bit_read(raw, &position, ctx.wh_bits));
    dsc.box_h = static_cast<uint16_t>(bit_read(raw, &position, ctx.wh_bits));
    return dsc;
}

bool get_glyph_dsc(FontContext &ctx, uint32_t glyph, GlyphDsc *dsc) {
    if (dsc == nullptr) return false;
    uint32_t offset = 0U;
    uint32_t size = 0U;
    if (glyph == 0U) { *dsc = {}; return true; }
    if (!glyph_span(ctx, glyph, &offset, &size)) return false;
    const uint32_t bytes = (header_bits(ctx) + 7U) >> 3;
    if (bytes == 0U || bytes > size || bytes > sizeof(ctx.glyph_raw) ||
        !read_at(ctx, ctx.glyf_start + offset, ctx.glyph_raw, bytes)) return false;
    *dsc = parse_glyph_dsc(ctx, ctx.glyph_raw);
    return true;
}

struct Rle {
    const uint8_t *data;
    uint32_t start;
    uint32_t position;
    uint8_t bpp;
    uint8_t previous;
    uint8_t count;
    uint8_t state;
};

uint8_t rle_next(Rle &rle) {
    uint8_t value = 0U;
    if (rle.state == 0U) {
        value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp));
        if (rle.position != rle.start + rle.bpp && rle.previous == value) { rle.count = 0U; rle.state = 1U; }
        rle.previous = value;
    } else if (rle.state == 1U) {
        const uint8_t marker = static_cast<uint8_t>(bit_read(rle.data, &rle.position, 1U));
        ++rle.count;
        if (marker != 0U) {
            value = rle.previous;
            if (rle.count == 11U) {
                rle.count = static_cast<uint8_t>(bit_read(rle.data, &rle.position, 6U));
                rle.state = rle.count == 0U ? 0U : 2U;
                if (rle.count == 0U) { value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp)); rle.previous = value; }
            }
        } else { value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp)); rle.previous = value; rle.state = 0U; }
    } else {
        value = rle.previous;
        if (--rle.count == 0U) { value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp)); rle.previous = value; rle.state = 0U; }
    }
    return value;
}

bool get_glyph(FontContext &ctx, uint32_t glyph, GlyphDsc *dsc, uint8_t *bitmap, uint32_t bitmap_size) {
    if (dsc == nullptr || bitmap == nullptr || glyph == 0U) { if (dsc != nullptr) *dsc = {}; return glyph == 0U; }
    uint32_t offset = 0U;
    uint32_t total = 0U;
    if (!glyph_span(ctx, glyph, &offset, &total) || total == 0U || total > sizeof(ctx.glyph_raw) ||
        !read_at(ctx, ctx.glyf_start + offset, ctx.glyph_raw, total)) return false;
    *dsc = parse_glyph_dsc(ctx, ctx.glyph_raw);
    if (dsc->box_w == 0U || dsc->box_h == 0U) return true;
    const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
    const uint32_t bits = static_cast<uint32_t>(dsc->box_w) * dsc->box_h * pixel_bpp;
    const uint32_t bytes = (bits + 7U) >> 3;
    if (bytes > bitmap_size || total * 8U < header_bits(ctx)) return false;
    if (ctx.compression == 0U) {
        if (bits > total * 8U - header_bits(ctx)) return false;
        std::memset(bitmap, 0, bytes);
        uint32_t source = header_bits(ctx);
        uint32_t target = 0U;
        uint32_t remaining = bits;
        while (remaining != 0U) { const uint8_t count = static_cast<uint8_t>(remaining > 8U ? 8U : remaining); bit_write(bitmap, &target, static_cast<uint8_t>(bit_read(ctx.glyph_raw, &source, count)), count); remaining -= count; }
        return true;
    }
    if (dsc->box_w > 32U) return false;
    std::memset(bitmap, 0, bytes);
    uint8_t previous_line[32] = {};
    uint8_t current_line[32] = {};
    Rle rle = {ctx.glyph_raw, header_bits(ctx), header_bits(ctx), ctx.bpp, 0U, 0U, 0U};
    uint32_t target = 0U;
    for (uint16_t y = 0U; y < dsc->box_h; ++y) {
        for (uint16_t x = 0U; x < dsc->box_w; ++x) {
            current_line[x] = rle_next(rle);
            if (ctx.compression == 1U && y != 0U) current_line[x] ^= previous_line[x];
            uint8_t value = current_line[x];
            if (ctx.bpp == 3U) value = static_cast<uint8_t>((value * 15U) / 7U);
            bit_write(bitmap, &target, value, pixel_bpp);
        }
        std::memcpy(previous_line, current_line, dsc->box_w);
    }
    return true;
}

bool open_context(FontContext &ctx) {
    if (ctx.ready) return true;
    const uint32_t now = millis();
    if (ctx.attempted && static_cast<int32_t>(now - ctx.next_retry_ms) < 0) return false;
    if (!bsp_littlefs_available()) return false;
    auto fail = [&ctx]() {
        if (ctx.file) ctx.file.close();
        ctx.ready = false;
        return false;
    };
    ctx.attempted = true;
    ctx.next_retry_ms = now + FONT_OPEN_RETRY_MS;
    if (ctx.file) ctx.file.close();
    if (!bsp_littlefs_lock(pdMS_TO_TICKS(100U))) return false;
    ctx.file = bsp_littlefs_fs().open(ctx.path, "r");
    bsp_littlefs_unlock();
    if (!ctx.file) return false;

    uint8_t head[48] = {};
    uint32_t head_len = 0U;
    if (!read_at(ctx, 0U, head, sizeof(head)) || std::memcmp(head + 4, "head", 4U) != 0 ||
        (head_len = rd32(head), head_len < sizeof(head))) return fail();
    const int16_t ascent = rd16s(head + 16);
    const int16_t descent = rd16s(head + 18);
    const int16_t typo_ascent = rd16s(head + 20);
    const int16_t typo_descent = rd16s(head + 22);
    const int16_t typo_line_gap = rd16s(head + 24);
    const int32_t typo_line_height = static_cast<int32_t>(typo_ascent) -
                                     typo_descent + typo_line_gap;
    if (typo_ascent > 0 && typo_descent <= 0 && typo_line_gap >= 0 &&
        typo_line_height > 0 && typo_line_height <= 0xFFFFL) {
        ctx.line_height = static_cast<uint16_t>(typo_line_height);
        ctx.baseline = typo_ascent;
    } else {
        const int32_t fallback_line_height = static_cast<int32_t>(ascent) - descent;
        if (ascent <= 0 || descent > 0 || fallback_line_height <= 0 ||
            fallback_line_height > 0xFFFFL) return fail();
        ctx.line_height = static_cast<uint16_t>(fallback_line_height);
        ctx.baseline = ascent;
    }
    ctx.default_advance = rd16(head + 30);
    ctx.loca_format = head[34];
    ctx.aw_format = head[36];
    ctx.bpp = head[37];
    ctx.xy_bits = head[38];
    ctx.wh_bits = head[39];
    ctx.aw_bits = head[40];
    ctx.compression = head[41];
    if (ctx.bpp == 0U || ctx.bpp > 8U || ctx.loca_format > 1U) return fail();

    uint32_t cmap_len = 0U;
    uint32_t loca_len = 0U;
    uint32_t glyf_len = 0U;
    const uint32_t cmap_start = head_len;
    const uint32_t loca_start = cmap_start + (section_header(ctx, cmap_start, "cmap", &cmap_len) ? cmap_len : 0U);
    const uint32_t glyf_start = loca_start + (section_header(ctx, loca_start, "loca", &loca_len) ? loca_len : 0U);
    if (!section_header(ctx, cmap_start, "cmap", &cmap_len) || !section_header(ctx, loca_start, "loca", &loca_len) ||
        !section_header(ctx, glyf_start, "glyf", &glyf_len)) return fail();
    uint32_t cmap_count = 0U;
    uint32_t loca_count = 0U;
    if (!read_u32(ctx, cmap_start + 8U, &cmap_count) || cmap_count == 0U || cmap_count > MAX_CMAP ||
        !read_u32(ctx, loca_start + 8U, &loca_count) || loca_count == 0U) return fail();
    ctx.cmap_start = cmap_start;
    ctx.cmap_count = static_cast<uint16_t>(cmap_count);
    ctx.loca_data_start = loca_start + 12U;
    ctx.loca_count = loca_count;
    // loca offsets are relative to the glyf section, including its 8-byte header.
    ctx.glyf_start = glyf_start;
    ctx.glyf_length = glyf_len;
    for (uint16_t i = 0U; i < ctx.cmap_count; ++i) {
        uint8_t raw[16] = {};
        if (!read_at(ctx, cmap_start + 12U + static_cast<uint32_t>(i) * 16U, raw, sizeof(raw))) return fail();
        CmapEntry &entry = ctx.cmap[i];
        entry.data_offset = rd32(raw);
        entry.range_start = rd32(raw + 4);
        entry.range_length = rd16(raw + 8);
        entry.glyph_id_start = rd16(raw + 10);
        entry.data_entries_count = rd16(raw + 12);
        entry.format = raw[14];
        if (entry.format > SPARSE_TINY) return fail();
    }
    ctx.ready = true;
    return true;
}

FontContext *select_context(uint8_t size) {
    return size <= 16U ? &contexts[0] : size <= 18U ? &contexts[1] : &contexts[2];
}

const egui_font_t *fallback_for(const FontContext &ctx) {
    return ctx.requested_size >= 20U ? EGUI_FONT_OF(&egui_res_font_montserrat_20_4) :
           ctx.requested_size >= 18U ? EGUI_FONT_OF(&egui_res_font_montserrat_18_4) :
                                       EGUI_FONT_OF(&egui_res_font_montserrat_16_4);
}

int utf8_decode(const char *text, uint32_t *cp) {
    if (text == nullptr || cp == nullptr || *text == '\0') return 0;
    const uint8_t c = static_cast<uint8_t>(text[0]);
    if ((c & 0x80U) == 0U) { *cp = c; return 1; }
    if ((c & 0xE0U) == 0xC0U) { *cp = ((c & 0x1FU) << 6) | (static_cast<uint8_t>(text[1]) & 0x3FU); return 2; }
    if ((c & 0xF0U) == 0xE0U) { *cp = ((c & 0x0FU) << 12) | ((static_cast<uint8_t>(text[1]) & 0x3FU) << 6) | (static_cast<uint8_t>(text[2]) & 0x3FU); return 3; }
    if ((c & 0xF8U) == 0xF0U) { *cp = ((c & 7U) << 18) | ((static_cast<uint8_t>(text[1]) & 0x3FU) << 12) | ((static_cast<uint8_t>(text[2]) & 0x3FU) << 6) | (static_cast<uint8_t>(text[3]) & 0x3FU); return 4; }
    *cp = c;
    return 1;
}

int draw_fallback(const FontContext &ctx, egui_canvas_t *canvas, const char *text, int bytes, egui_dim_t x, egui_dim_t y, egui_color_t color, egui_alpha_t alpha, egui_dim_t *advance) {
    const egui_font_t *font = fallback_for(ctx);
    char one[5] = {};
    if (bytes > 4) bytes = 4;
    std::memcpy(one, text, static_cast<size_t>(bytes));
    if (canvas != nullptr) egui_canvas_draw_text(canvas, font, one, x, y, color, alpha);
    egui_dim_t width = 0;
    egui_dim_t height = 0;
    (void)egui_font_get_str_size_with_canvas(font, canvas, one, 0U, 0, &width, &height);
    if (advance != nullptr) *advance = width > 0 ? width : static_cast<egui_dim_t>(ctx.requested_size / 2U);
    return bytes;
}

egui_alpha_t alpha_from_value(uint8_t value, uint8_t bpp) {
    if (value == 0U) return EGUI_ALPHA_0;
    if (bpp == 3U) bpp = 4U;
    const uint16_t max_value = static_cast<uint16_t>((1U << bpp) - 1U);
    if (value >= max_value) return EGUI_ALPHA_100;
    return static_cast<egui_alpha_t>((static_cast<uint16_t>(value) * EGUI_ALPHA_100 + max_value / 2U) / max_value);
}

size_t poetry_hash_slot(const FontContext &ctx, uint32_t cp) {
    uintptr_t key = reinterpret_cast<uintptr_t>(&ctx);
    key ^= static_cast<uintptr_t>(cp) + static_cast<uintptr_t>(0x9E3779B9U) +
           (key << 6U) + (key >> 2U);
    key ^= key >> 16U;
    return static_cast<size_t>(key) & (poetry_glyph_index_capacity - 1U);
}

GlyphCacheEntry *find_poetry_cached_glyph(FontContext &ctx, uint32_t cp) {
    if (poetry_glyph_cache == nullptr || poetry_glyph_index == nullptr ||
        poetry_glyph_index_capacity == 0U) {
        return nullptr;
    }

    size_t slot = poetry_hash_slot(ctx, cp);
    for (size_t probe = 0U; probe < poetry_glyph_index_capacity; ++probe) {
        ++poetry_glyph_hash_probes;
        const uint16_t stored = poetry_glyph_index[slot];
        if (stored == POETRY_HASH_EMPTY) return nullptr;
        GlyphCacheEntry &entry = poetry_glyph_cache[static_cast<size_t>(stored - 1U)];
        if (entry.valid && entry.font == &ctx && entry.cp == cp) return &entry;
        slot = (slot + 1U) & (poetry_glyph_index_capacity - 1U);
    }
    return nullptr;
}

bool insert_poetry_cached_glyph(size_t entry_index) {
    if (poetry_glyph_cache == nullptr || poetry_glyph_index == nullptr ||
        entry_index >= poetry_glyph_count + 1U || entry_index >= 0xFFFFU) {
        return false;
    }

    GlyphCacheEntry &entry = poetry_glyph_cache[entry_index];
    size_t slot = poetry_hash_slot(*entry.font, entry.cp);
    for (size_t probe = 0U; probe < poetry_glyph_index_capacity; ++probe) {
        const uint16_t stored = poetry_glyph_index[slot];
        if (stored == POETRY_HASH_EMPTY) {
            poetry_glyph_index[slot] = static_cast<uint16_t>(entry_index + 1U);
            return true;
        }
        GlyphCacheEntry &existing = poetry_glyph_cache[static_cast<size_t>(stored - 1U)];
        if (existing.valid && existing.font == entry.font && existing.cp == entry.cp) {
            return true;
        }
        slot = (slot + 1U) & (poetry_glyph_index_capacity - 1U);
    }
    return false;
}

bool commit_poetry_cached_glyph() {
    const size_t entry_index = poetry_glyph_count;
    if (!insert_poetry_cached_glyph(entry_index)) {
        poetry_glyph_cache[entry_index] = {};
        return false;
    }
    ++poetry_glyph_count;
    return true;
}

GlyphCacheEntry *find_regular_cached_glyph(FontContext &ctx, uint32_t cp) {
    for (GlyphCacheEntry &entry : glyph_cache) {
        if (entry.valid && entry.font == &ctx && entry.cp == cp) {
            entry.age = ++glyph_cache_age;
            return &entry;
        }
    }
    return nullptr;
}

GlyphCacheEntry *find_cached_glyph(FontContext &ctx, uint32_t cp) {
    GlyphCacheEntry *entry = find_poetry_cached_glyph(ctx, cp);
    return entry != nullptr ? entry : find_regular_cached_glyph(ctx, cp);
}

bool cache_poetry_glyph(FontContext &ctx, uint32_t cp) {
    if (find_poetry_cached_glyph(ctx, cp) != nullptr) return true;
    if (poetry_glyph_cache == nullptr || poetry_glyph_count >= poetry_glyph_capacity) return false;

    GlyphCacheEntry *destination = &poetry_glyph_cache[poetry_glyph_count];
    GlyphCacheEntry *regular = find_regular_cached_glyph(ctx, cp);
    if (regular != nullptr) {
        *destination = *regular;
        destination->age = 0U;
        return commit_poetry_cached_glyph();
    }

    uint32_t glyph = 0U;
    if (!lookup(ctx, cp, &glyph)) {
        destination->font = &ctx;
        destination->cp = cp;
        destination->dsc.advance = static_cast<uint16_t>(ctx.requested_size / 2U);
        destination->age = 0U;
        destination->valid = true;
        destination->missing = true;
        Serial.printf("[FONT_CACHE] missing U+%04lX, using '-'\n",
                      static_cast<unsigned long>(cp));
        return commit_poetry_cached_glyph();
    }
    GlyphDsc dsc = {};
    uint8_t bitmap[MAX_GLYPH_RAW] = {};
    if (!get_glyph(ctx, glyph, &dsc, bitmap, sizeof(bitmap))) return false;
    const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
    const uint32_t bitmap_bytes =
        (static_cast<uint32_t>(dsc.box_w) * dsc.box_h * pixel_bpp + 7U) >> 3;
    if (bitmap_bytes > sizeof(destination->bitmap)) return false;

    destination->font = &ctx;
    destination->cp = cp;
    destination->dsc = dsc;
    destination->age = 0U;
    destination->valid = true;
    destination->missing = false;
    if (bitmap_bytes != 0U) std::memcpy(destination->bitmap, bitmap, bitmap_bytes);
    return commit_poetry_cached_glyph();
}

GlyphCacheEntry *allocate_glyph_cache() {
    GlyphCacheEntry *oldest = &glyph_cache[0];
    for (GlyphCacheEntry &entry : glyph_cache) {
        if (!entry.valid) return &entry;
        if (entry.age < oldest->age) oldest = &entry;
    }
    return oldest;
}

int render_cached_glyph(FontContext &ctx, const GlyphCacheEntry &cached,
                        egui_canvas_t *canvas, egui_dim_t x, egui_dim_t y,
                        egui_color_t color, egui_alpha_t alpha,
                        egui_dim_t *advance) {
    if (cached.missing) {
        return draw_fallback(ctx, canvas, "-", 1, x, y, color, alpha, advance);
    }
    const GlyphDsc &dsc = cached.dsc;
    if (advance != nullptr) *advance = dsc.advance;
    if (canvas == nullptr || dsc.box_w == 0U || dsc.box_h == 0U) return 1;
    const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
    for (uint16_t gy = 0U; gy < dsc.box_h; ++gy) {
        for (uint16_t gx = 0U; gx < dsc.box_w; ++gx) {
            uint32_t bit =
                (static_cast<uint32_t>(gy) * dsc.box_w + gx) * pixel_bpp;
            const uint8_t value = static_cast<uint8_t>(
                bit_read(cached.bitmap, &bit, pixel_bpp));
            const egui_alpha_t glyph_alpha = alpha_from_value(value, ctx.bpp);
            if (glyph_alpha != EGUI_ALPHA_0) {
                const egui_dim_t baseline = static_cast<egui_dim_t>(y + ctx.baseline);
                egui_canvas_draw_point(
                    canvas, static_cast<egui_dim_t>(x + dsc.ofs_x + gx),
                    static_cast<egui_dim_t>(baseline - dsc.ofs_y - dsc.box_h + gy),
                    color, egui_color_alpha_mix(alpha, glyph_alpha));
            }
        }
    }
    return 1;
}

int draw_glyph(FontContext &ctx, egui_canvas_t *canvas, uint32_t cp, egui_dim_t x, egui_dim_t y, egui_color_t color, egui_alpha_t alpha, egui_dim_t *advance) {
    GlyphDsc dsc = {};
    uint8_t bitmap[MAX_GLYPH_RAW] = {};
    const uint8_t *pixels = bitmap;
    GlyphCacheEntry *cached = find_cached_glyph(ctx, cp);
    if (cached != nullptr) {
        return render_cached_glyph(ctx, *cached, canvas, x, y, color, alpha,
                                   advance);
    } else {
        uint32_t glyph = 0U;
        if (!lookup(ctx, cp, &glyph) || !get_glyph(ctx, glyph, &dsc, bitmap, sizeof(bitmap))) return 0;
        const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
        const uint32_t bitmap_bytes = (static_cast<uint32_t>(dsc.box_w) * dsc.box_h * pixel_bpp + 7U) >> 3;
        if (bitmap_bytes <= GLYPH_CACHE_BITMAP_SIZE) {
            cached = allocate_glyph_cache();
            cached->font = &ctx;
            cached->cp = cp;
            cached->dsc = dsc;
            cached->age = ++glyph_cache_age;
            cached->valid = true;
            cached->missing = false;
            std::memcpy(cached->bitmap, bitmap, bitmap_bytes);
            pixels = cached->bitmap;
        }
    }
    if (advance != nullptr) *advance = dsc.advance;
    if (canvas == nullptr || dsc.box_w == 0U || dsc.box_h == 0U) return 1;
    const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
    for (uint16_t gy = 0U; gy < dsc.box_h; ++gy) {
        for (uint16_t gx = 0U; gx < dsc.box_w; ++gx) {
            uint32_t bit = (static_cast<uint32_t>(gy) * dsc.box_w + gx) * pixel_bpp;
            const uint8_t value = static_cast<uint8_t>(bit_read(pixels, &bit, pixel_bpp));
            const egui_alpha_t glyph_alpha = alpha_from_value(value, ctx.bpp);
            if (glyph_alpha != EGUI_ALPHA_0) {
                const egui_dim_t baseline = static_cast<egui_dim_t>(y + ctx.baseline);
                egui_canvas_draw_point(canvas, static_cast<egui_dim_t>(x + dsc.ofs_x + gx), static_cast<egui_dim_t>(baseline - dsc.ofs_y - dsc.box_h + gy), color, egui_color_alpha_mix(alpha, glyph_alpha));
            }
        }
    }
    return 1;
}

int font_draw(const egui_font_t *font, egui_canvas_t *canvas, const void *string, egui_dim_t x, egui_dim_t y, egui_color_t color, egui_alpha_t alpha) {
    FontContext *ctx = context_for(font);
    const char *text = static_cast<const char *>(string);
    if (ctx == nullptr || text == nullptr) return 0;
    (void)open_context(*ctx);
    int consumed = 0;
    egui_dim_t pen_x = x;
    while (*text != '\0') {
        if (*text == '\r') { ++text; ++consumed; continue; }
        if (*text == '\n') { ++consumed; break; }
        uint32_t cp = 0U;
        const int bytes = utf8_decode(text, &cp);
        if (bytes <= 0) break;
        egui_dim_t advance = 0;
        if (cp < 0x80U) draw_fallback(*ctx, canvas, text, bytes, pen_x, y, color, alpha, &advance);
        else if (!ctx->ready || !draw_glyph(*ctx, canvas, cp, pen_x, y, color, alpha, &advance)) draw_fallback(*ctx, canvas, "?", 1, pen_x, y, color, alpha, &advance);
        pen_x = static_cast<egui_dim_t>(pen_x + advance);
        text += bytes;
        consumed += bytes;
    }
    return consumed;
}

int font_size(const egui_font_t *font, const void *string, uint8_t multi, egui_dim_t line_space, egui_dim_t *width, egui_dim_t *height) {
    FontContext *ctx = context_for(font);
    const char *text = static_cast<const char *>(string);
    if (ctx == nullptr || text == nullptr || width == nullptr || height == nullptr) return 0;
    (void)open_context(*ctx);
    egui_dim_t line_width = 0;
    egui_dim_t max_width = 0;
    egui_dim_t line_height = ctx->line_height != 0U ? ctx->line_height : ctx->requested_size;
    egui_dim_t total_height = line_height;
    while (*text != '\0') {
        if (*text == '\r') { ++text; continue; }
        if (*text == '\n') {
            if (line_width > max_width) max_width = line_width;
            if (multi == 0U) break;
            line_width = 0;
            total_height = static_cast<egui_dim_t>(total_height + line_height + line_space);
            ++text;
            continue;
        }
        uint32_t cp = 0U;
        const int bytes = utf8_decode(text, &cp);
        if (bytes <= 0) break;
        egui_dim_t advance = 0;
        if (cp < 0x80U) {
            const egui_font_t *fallback = fallback_for(*ctx);
            egui_dim_t fw = 0;
            egui_dim_t fh = 0;
            char one[5] = {};
            std::memcpy(one, text, static_cast<size_t>(bytes));
            (void)egui_font_get_str_size_with_canvas(fallback, nullptr, one, 0U, 0, &fw, &fh);
            advance = fw > 0 ? fw : static_cast<egui_dim_t>(ctx->requested_size / 2U);
        } else {
            GlyphCacheEntry *cached = find_cached_glyph(*ctx, cp);
            if (cached != nullptr) {
                advance = cached->dsc.advance;
            } else {
                uint32_t glyph = 0U;
                GlyphDsc dsc = {};
                advance = ctx->ready && lookup(*ctx, cp, &glyph) &&
                                  get_glyph_dsc(*ctx, glyph, &dsc)
                              ? dsc.advance
                              : ctx->requested_size;
            }
        }
        line_width = static_cast<egui_dim_t>(line_width + advance);
        text += bytes;
    }
    if (line_width > max_width) max_width = line_width;
    *width = max_width;
    *height = total_height;
    return 0;
}

int cached_font_draw(const egui_font_t *font, egui_canvas_t *canvas,
                     const void *string, egui_dim_t x, egui_dim_t y,
                     egui_color_t color, egui_alpha_t alpha) {
    FontContext *ctx = context_for(font);
    const char *text = static_cast<const char *>(string);
    if (ctx == nullptr || text == nullptr) return 0;
    int consumed = 0;
    egui_dim_t pen_x = x;
    while (*text != '\0') {
        if (*text == '\r') {
            ++text;
            ++consumed;
            continue;
        }
        if (*text == '\n') {
            ++consumed;
            break;
        }
        uint32_t cp = 0U;
        const int bytes = utf8_decode(text, &cp);
        if (bytes <= 0) break;
        egui_dim_t advance = 0;
        if (cp < 0x80U) {
            draw_fallback(*ctx, canvas, text, bytes, pen_x, y, color, alpha,
                          &advance);
        } else {
            GlyphCacheEntry *cached = find_cached_glyph(*ctx, cp);
            if (cached != nullptr) {
                render_cached_glyph(*ctx, *cached, canvas, pen_x, y, color,
                                    alpha, &advance);
            } else {
                draw_fallback(*ctx, canvas, "-", 1, pen_x, y, color, alpha,
                              &advance);
            }
        }
        pen_x = static_cast<egui_dim_t>(pen_x + advance);
        text += bytes;
        consumed += bytes;
    }
    return consumed;
}

int cached_font_size(const egui_font_t *font, const void *string, uint8_t multi,
                     egui_dim_t line_space, egui_dim_t *width,
                     egui_dim_t *height) {
    FontContext *ctx = context_for(font);
    const char *text = static_cast<const char *>(string);
    if (ctx == nullptr || text == nullptr || width == nullptr || height == nullptr) {
        return 0;
    }
    egui_dim_t line_width = 0;
    egui_dim_t max_width = 0;
    const egui_dim_t line_height =
        ctx->line_height != 0U ? ctx->line_height : ctx->requested_size;
    egui_dim_t total_height = line_height;
    while (*text != '\0') {
        if (*text == '\r') {
            ++text;
            continue;
        }
        if (*text == '\n') {
            if (line_width > max_width) max_width = line_width;
            if (multi == 0U) break;
            line_width = 0;
            total_height = static_cast<egui_dim_t>(
                total_height + line_height + line_space);
            ++text;
            continue;
        }
        uint32_t cp = 0U;
        const int bytes = utf8_decode(text, &cp);
        if (bytes <= 0) break;
        egui_dim_t advance = 0;
        if (cp < 0x80U) {
            const egui_font_t *fallback = fallback_for(*ctx);
            egui_dim_t fw = 0;
            egui_dim_t fh = 0;
            char one[5] = {};
            std::memcpy(one, text, static_cast<size_t>(bytes));
            (void)egui_font_get_str_size_with_canvas(
                fallback, nullptr, one, 0U, 0, &fw, &fh);
            advance = fw > 0 ? fw : static_cast<egui_dim_t>(ctx->requested_size / 2U);
        } else {
            GlyphCacheEntry *cached = find_cached_glyph(*ctx, cp);
            advance = cached != nullptr ? cached->dsc.advance
                                        : static_cast<egui_dim_t>(ctx->requested_size / 2U);
        }
        line_width = static_cast<egui_dim_t>(line_width + advance);
        text += bytes;
    }
    if (line_width > max_width) max_width = line_width;
    *width = max_width;
    *height = total_height;
    return 0;
}
}

const egui_font_t *ui_heiti_font_get(uint8_t size) {
    FontContext *ctx = select_context(size);
    if (ctx->base.api == nullptr) {
        for (FontContext &item : contexts) { item.base.res = &item; item.base.api = &api; }
    }
    (void)open_context(*ctx);
    return &ctx->base;
}

const egui_font_t *ui_heiti_font_get_cached(uint8_t size) {
    FontContext *ctx = select_context(size);
    if (ctx->cached_base.api == nullptr) {
        ctx->cached_base.res = ctx;
        ctx->cached_base.api = &cached_api;
    }
    (void)open_context(*ctx);
    return &ctx->cached_base;
}

bool ui_heiti_font_is_ready(uint8_t size) {
    FontContext *ctx = select_context(size);
    (void)ui_heiti_font_get(size);
    return ctx->ready;
}

bool ui_heiti_font_warm_text(uint8_t size, const char *text) {
    FontContext *ctx = select_context(size);
    if (text == nullptr || !ui_heiti_font_is_ready(size)) return false;
    bool ready = true;
    while (*text != '\0') {
        if (*text == '\r' || *text == '\n') {
            ++text;
            continue;
        }
        uint32_t cp = 0U;
        const int bytes = utf8_decode(text, &cp);
        if (bytes <= 0) return false;
        if (cp >= 0x80U) {
            egui_dim_t advance = 0;
            if (!draw_glyph(*ctx, nullptr, cp, 0, 0, EGUI_COLOR_BLACK,
                            EGUI_ALPHA_100, &advance)) {
                ready = false;
            }
        }
        text += bytes;
    }
    return ready;
}

bool ui_heiti_font_poetry_cache_init(size_t max_bytes) {
    if (poetry_glyph_cache != nullptr) return poetry_glyph_index != nullptr;
    const size_t capacity = max_bytes / sizeof(GlyphCacheEntry);
    if (capacity == 0U || capacity >= 0xFFFFU || !psramFound()) return false;
    size_t index_capacity = 1U;
    while (index_capacity < capacity * 2U) index_capacity <<= 1U;
    const size_t bytes = capacity * sizeof(GlyphCacheEntry);
    GlyphCacheEntry *cache = static_cast<GlyphCacheEntry *>(
        heap_caps_calloc(capacity, sizeof(GlyphCacheEntry),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (cache == nullptr) return false;
    uint16_t *index = static_cast<uint16_t *>(
        heap_caps_calloc(index_capacity, sizeof(uint16_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (index == nullptr) {
        heap_caps_free(cache);
        return false;
    }
    poetry_glyph_cache = cache;
    poetry_glyph_index = index;
    poetry_glyph_capacity = capacity;
    poetry_glyph_bytes = bytes;
    poetry_glyph_index_capacity = index_capacity;
    poetry_glyph_hash_probes = 0U;
    Serial.printf("[POETRY_CACHE] glyph pool=%u bytes, capacity=%u index=%u bytes\n",
                  static_cast<unsigned>(bytes), static_cast<unsigned>(capacity),
                  static_cast<unsigned>(index_capacity * sizeof(uint16_t)));
    for (FontContext &ctx : contexts) {
        if (ctx.requested_size <= 18U) (void)open_context(ctx);
    }
    return true;
}

bool ui_heiti_font_cache_text(uint8_t size, const char *text) {
    FontContext *ctx = select_context(size);
    if (text == nullptr || poetry_glyph_cache == nullptr ||
        !ui_heiti_font_is_ready(size)) return false;
    while (*text != '\0') {
        if (*text == '\r' || *text == '\n') {
            ++text;
            continue;
        }
        uint32_t cp = 0U;
        const int bytes = utf8_decode(text, &cp);
        if (bytes <= 0) return false;
        if (cp >= 0x80U && !cache_poetry_glyph(*ctx, cp)) return false;
        text += bytes;
    }
    return true;
}

bool ui_heiti_font_text_is_cached(uint8_t size, const char *text) {
    FontContext *ctx = select_context(size);
    if (text == nullptr || poetry_glyph_cache == nullptr) return false;
    while (*text != '\0') {
        if (*text == '\r' || *text == '\n') {
            ++text;
            continue;
        }
        uint32_t cp = 0U;
        const int bytes = utf8_decode(text, &cp);
        if (bytes <= 0) return false;
        if (cp >= 0x80U && find_poetry_cached_glyph(*ctx, cp) == nullptr) return false;
        text += bytes;
    }
    return true;
}

size_t ui_heiti_font_poetry_cache_bytes() { return poetry_glyph_bytes; }
size_t ui_heiti_font_poetry_cache_glyphs() { return poetry_glyph_count; }
uint32_t ui_heiti_font_storage_read_count() { return storage_read_count; }
void ui_heiti_font_log_cache_stats(uint32_t poll_ms) {
    Serial.printf("[GUI] poll=%lums poetry_glyphs=%u hash_probes=%lu\n",
                  static_cast<unsigned long>(poll_ms),
                  static_cast<unsigned>(poetry_glyph_count),
                  static_cast<unsigned long>(poetry_glyph_hash_probes));
    poetry_glyph_hash_probes = 0U;
}
