#include "gui/ui_heiti_font.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>

#include "bsp/bsp_littlefs.h"

namespace {
constexpr uint16_t kMaxCmap = 128U;
constexpr uint32_t kMaxGlyphRaw = 512U;
constexpr uint8_t kFmt0Full = 0U;
constexpr uint8_t kSparseFull = 1U;
constexpr uint8_t kFmt0Tiny = 2U;
constexpr uint8_t kSparseTiny = 3U;
constexpr uint32_t kRetryMs = 200U;
constexpr uint8_t kCacheSlots = 48U;
constexpr uint16_t kCacheBitmapBytes = 256U;

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
    FontContext(const char *font_path, uint8_t size)
        : path(font_path), requested_size(size) {}

    lv_font_t font = {};
    const char *path = nullptr;
    uint8_t requested_size = 16U;
    bool ready = false;
    bool attempted = false;
    uint32_t next_retry_ms = 0U;
    File file;
    uint8_t bpp = 4U;
    uint16_t line_height = 16U;
    int16_t baseline = 16;
    uint16_t default_advance = 16U;
    uint8_t aw_bits = 0U;
    uint8_t xy_bits = 0U;
    uint8_t wh_bits = 0U;
    uint8_t loca_format = 0U;
    uint8_t aw_format = 0U;
    uint8_t compression = 0U;
    uint32_t cmap_start = 0U;
    uint32_t loca_data_start = 0U;
    uint32_t loca_count = 0U;
    uint32_t glyf_start = 0U;
    uint32_t glyf_length = 0U;
    uint16_t cmap_count = 0U;
    CmapEntry cmap[kMaxCmap] = {};
    uint8_t glyph_raw[kMaxGlyphRaw] = {};
};

struct GlyphCacheEntry {
    FontContext *font = nullptr;
    uint32_t cp = 0U;
    GlyphDsc dsc = {};
    uint32_t age = 0U;
    bool valid = false;
    bool placeholder = false;
    uint8_t bitmap[kCacheBitmapBytes] = {};
};

FontContext contexts[] = {
    FontContext("/heiti_4_16.bin", 16U),
    FontContext("/heiti_4_18.bin", 18U),
    FontContext("/heiti_4_20.bin", 20U),
};
GlyphCacheEntry glyph_cache[kCacheSlots] = {};
uint32_t cache_age = 0U;
uint32_t storage_reads = 0U;

uint16_t rd16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

int16_t rd16s(const uint8_t *p) { return static_cast<int16_t>(rd16(p)); }

uint32_t rd32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool read_at(FontContext &ctx, uint32_t offset, void *buffer, size_t length) {
    if (!ctx.file || buffer == nullptr || length == 0U ||
        !bsp_littlefs_lock(pdMS_TO_TICKS(100U))) return false;
    ++storage_reads;
    const bool ok = ctx.file.seek(offset) &&
                    ctx.file.read(static_cast<uint8_t *>(buffer), length) == length;
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

bool section_header(FontContext &ctx, uint32_t offset, const char label[4],
                    uint32_t *length) {
    uint8_t raw[8] = {};
    return length != nullptr && read_at(ctx, offset, raw, sizeof(raw)) &&
           std::memcmp(raw + 4, label, 4U) == 0 &&
           (*length = rd32(raw), *length >= 8U);
}

uint32_t bit_read(const uint8_t *data, uint32_t *position, uint8_t count) {
    uint32_t value = 0U;
    while (count-- != 0U) {
        const uint32_t index = *position >> 3U;
        value = (value << 1U) |
                ((data[index] >> (7U - (*position & 7U))) & 1U);
        ++*position;
    }
    return value;
}

void bit_write(uint8_t *data, uint32_t *position, uint8_t value, uint8_t count) {
    while (count-- != 0U) {
        const uint32_t index = *position >> 3U;
        const uint8_t shift = static_cast<uint8_t>(7U - (*position & 7U));
        data[index] = static_cast<uint8_t>(
            (data[index] & ~(1U << shift)) | (((value >> count) & 1U) << shift));
        ++*position;
    }
}

int16_t bit_read_signed(const uint8_t *data, uint32_t *position, uint8_t count) {
    const uint16_t value = static_cast<uint16_t>(bit_read(data, position, count));
    if (count == 0U || (value & (1U << (count - 1U))) == 0U) {
        return static_cast<int16_t>(value);
    }
    return static_cast<int16_t>(value | static_cast<uint16_t>(0xFFFFU << count));
}

uint16_t advance_from_raw(uint16_t value) {
    return static_cast<uint16_t>((value + 8U) >> 4U);
}

bool cmap_contains(const CmapEntry &entry, uint32_t cp) {
    return cp >= entry.range_start &&
           cp < entry.range_start + entry.range_length;
}

bool lookup_entry(FontContext &ctx, const CmapEntry &entry, uint32_t cp,
                  uint32_t *glyph) {
    if (glyph == nullptr || !cmap_contains(entry, cp)) return false;
    const uint32_t relative = cp - entry.range_start;
    const uint32_t data_start = ctx.cmap_start + entry.data_offset;
    if (entry.format == kFmt0Tiny) {
        *glyph = static_cast<uint32_t>(entry.glyph_id_start) + relative;
        return true;
    }
    if (entry.format == kFmt0Full) {
        uint8_t offset = 0U;
        if (relative >= entry.data_entries_count ||
            !read_at(ctx, data_start + relative, &offset, 1U)) return false;
        *glyph = static_cast<uint32_t>(entry.glyph_id_start) + offset;
        return true;
    }
    if (entry.format == kSparseTiny || entry.format == kSparseFull) {
        uint32_t left = 0U;
        uint32_t right = entry.data_entries_count;
        while (left < right) {
            const uint32_t middle = left + ((right - left) >> 1U);
            uint16_t listed = 0U;
            if (!read_u16(ctx, data_start + middle * 2U, &listed)) return false;
            if (listed == relative) {
                if (entry.format == kSparseTiny) {
                    *glyph = static_cast<uint32_t>(entry.glyph_id_start) + middle;
                    return true;
                }
                uint16_t offset = 0U;
                const uint32_t offset_start =
                    data_start + static_cast<uint32_t>(entry.data_entries_count) * 2U;
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
        if (cmap_contains(ctx.cmap[i], cp) &&
            lookup_entry(ctx, ctx.cmap[i], cp, glyph)) return true;
    }
    return false;
}

uint32_t header_bits(const FontContext &ctx) {
    return static_cast<uint32_t>(ctx.aw_bits) + 2U * ctx.xy_bits +
           2U * ctx.wh_bits;
}

bool glyph_span(FontContext &ctx, uint32_t glyph, uint32_t *offset,
                uint32_t *size) {
    if (offset == nullptr || size == nullptr || glyph >= ctx.loca_count) return false;
    const uint32_t entry_size = ctx.loca_format == 1U ? 4U : 2U;
    auto read_offset = [&](uint32_t index, uint32_t *value) {
        if (entry_size == 4U) return read_u32(ctx, ctx.loca_data_start + index * entry_size, value);
        uint16_t short_value = 0U;
        if (!read_u16(ctx, ctx.loca_data_start + index * entry_size, &short_value)) return false;
        *value = short_value;
        return true;
    };
    uint32_t current = 0U;
    uint32_t next = ctx.glyf_length;
    if (!read_offset(glyph, &current) ||
        (glyph + 1U < ctx.loca_count && !read_offset(glyph + 1U, &next)) ||
        next < current || next > ctx.glyf_length) return false;
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
    uint32_t offset = 0U;
    uint32_t size = 0U;
    if (dsc == nullptr || glyph == 0U || !glyph_span(ctx, glyph, &offset, &size)) return false;
    const uint32_t bytes = (header_bits(ctx) + 7U) >> 3U;
    if (bytes == 0U || bytes > size || bytes > sizeof(ctx.glyph_raw) ||
        !read_at(ctx, ctx.glyf_start + offset, ctx.glyph_raw, bytes)) return false;
    *dsc = parse_glyph_dsc(ctx, ctx.glyph_raw);
    return true;
}

struct RleState {
    const uint8_t *data;
    uint32_t start;
    uint32_t position;
    uint8_t bpp;
    uint8_t previous;
    uint8_t count;
    uint8_t state;
};

uint8_t rle_next(RleState &rle) {
    uint8_t value = 0U;
    if (rle.state == 0U) {
        value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp));
        if (rle.position != rle.start + rle.bpp && rle.previous == value) {
            rle.count = 0U;
            rle.state = 1U;
        }
        rle.previous = value;
    } else if (rle.state == 1U) {
        const uint8_t marker = static_cast<uint8_t>(bit_read(rle.data, &rle.position, 1U));
        ++rle.count;
        if (marker != 0U) {
            value = rle.previous;
            if (rle.count == 11U) {
                rle.count = static_cast<uint8_t>(bit_read(rle.data, &rle.position, 6U));
                rle.state = rle.count == 0U ? 0U : 2U;
                if (rle.count == 0U) {
                    value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp));
                    rle.previous = value;
                }
            }
        } else {
            value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp));
            rle.previous = value;
            rle.state = 0U;
        }
    } else {
        value = rle.previous;
        if (--rle.count == 0U) {
            value = static_cast<uint8_t>(bit_read(rle.data, &rle.position, rle.bpp));
            rle.previous = value;
            rle.state = 0U;
        }
    }
    return value;
}

bool get_glyph(FontContext &ctx, uint32_t glyph, GlyphDsc *dsc,
               uint8_t *bitmap, uint32_t bitmap_size) {
    uint32_t offset = 0U;
    uint32_t total = 0U;
    if (dsc == nullptr || bitmap == nullptr || glyph == 0U ||
        !glyph_span(ctx, glyph, &offset, &total) || total == 0U ||
        total > sizeof(ctx.glyph_raw) ||
        !read_at(ctx, ctx.glyf_start + offset, ctx.glyph_raw, total)) return false;
    *dsc = parse_glyph_dsc(ctx, ctx.glyph_raw);
    if (dsc->box_w == 0U || dsc->box_h == 0U) return true;
    const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
    const uint32_t bits = static_cast<uint32_t>(dsc->box_w) * dsc->box_h * pixel_bpp;
    const uint32_t bytes = (bits + 7U) >> 3U;
    if (bytes > bitmap_size || total * 8U < header_bits(ctx)) return false;
    std::memset(bitmap, 0, bytes);
    if (ctx.compression == 0U) {
        if (bits > total * 8U - header_bits(ctx)) return false;
        uint32_t source = header_bits(ctx);
        uint32_t target = 0U;
        uint32_t remaining = bits;
        while (remaining != 0U) {
            const uint8_t count = static_cast<uint8_t>(remaining > 8U ? 8U : remaining);
            bit_write(bitmap, &target, static_cast<uint8_t>(bit_read(ctx.glyph_raw, &source, count)), count);
            remaining -= count;
        }
        return true;
    }
    if (dsc->box_w > 32U) return false;
    uint8_t previous_line[32] = {};
    uint8_t current_line[32] = {};
    RleState rle = {ctx.glyph_raw, header_bits(ctx), header_bits(ctx), ctx.bpp, 0U, 0U, 0U};
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
    ctx.attempted = true;
    ctx.next_retry_ms = now + kRetryMs;
    if (ctx.file) ctx.file.close();
    if (!bsp_littlefs_lock(pdMS_TO_TICKS(100U))) return false;
    ctx.file = bsp_littlefs_fs().open(ctx.path, "r");
    bsp_littlefs_unlock();
    if (!ctx.file) return false;
    auto fail = [&ctx]() {
        if (ctx.file) ctx.file.close();
        ctx.ready = false;
        return false;
    };
    uint8_t head[48] = {};
    uint32_t head_len = 0U;
    if (!read_at(ctx, 0U, head, sizeof(head)) || std::memcmp(head + 4, "head", 4U) != 0 ||
        (head_len = rd32(head), head_len < sizeof(head))) return fail();
    const int16_t ascent = rd16s(head + 16);
    const int16_t descent = rd16s(head + 18);
    const int16_t typo_ascent = rd16s(head + 20);
    const int16_t typo_descent = rd16s(head + 22);
    const int16_t typo_line_gap = rd16s(head + 24);
    const int32_t typo_height = static_cast<int32_t>(typo_ascent) - typo_descent + typo_line_gap;
    const int32_t fallback_height = static_cast<int32_t>(ascent) - descent;
    if (typo_ascent > 0 && typo_descent <= 0 && typo_line_gap >= 0 && typo_height > 0 && typo_height <= 0xFFFFL) {
        ctx.line_height = static_cast<uint16_t>(typo_height);
        ctx.baseline = typo_ascent;
    } else if (ascent > 0 && descent <= 0 && fallback_height > 0 && fallback_height <= 0xFFFFL) {
        ctx.line_height = static_cast<uint16_t>(fallback_height);
        ctx.baseline = ascent;
    } else return fail();
    ctx.default_advance = rd16(head + 30);
    ctx.loca_format = head[34]; ctx.aw_format = head[36]; ctx.bpp = head[37];
    ctx.xy_bits = head[38]; ctx.wh_bits = head[39]; ctx.aw_bits = head[40]; ctx.compression = head[41];
    if (ctx.bpp == 0U || ctx.bpp > 8U || ctx.loca_format > 1U) return fail();
    uint32_t cmap_len = 0U, loca_len = 0U, glyf_len = 0U;
    const uint32_t cmap_start = head_len;
    const uint32_t loca_start = cmap_start + (section_header(ctx, cmap_start, "cmap", &cmap_len) ? cmap_len : 0U);
    const uint32_t glyf_start = loca_start + (section_header(ctx, loca_start, "loca", &loca_len) ? loca_len : 0U);
    if (!section_header(ctx, cmap_start, "cmap", &cmap_len) || !section_header(ctx, loca_start, "loca", &loca_len) ||
        !section_header(ctx, glyf_start, "glyf", &glyf_len)) return fail();
    uint32_t cmap_count = 0U, loca_count = 0U;
    if (!read_u32(ctx, cmap_start + 8U, &cmap_count) || cmap_count == 0U || cmap_count > kMaxCmap ||
        !read_u32(ctx, loca_start + 8U, &loca_count) || loca_count == 0U) return fail();
    ctx.cmap_start = cmap_start; ctx.cmap_count = static_cast<uint16_t>(cmap_count);
    ctx.loca_data_start = loca_start + 12U; ctx.loca_count = loca_count;
    ctx.glyf_start = glyf_start; ctx.glyf_length = glyf_len;
    for (uint16_t i = 0U; i < ctx.cmap_count; ++i) {
        uint8_t raw[16] = {};
        if (!read_at(ctx, cmap_start + 12U + static_cast<uint32_t>(i) * 16U, raw, sizeof(raw))) return fail();
        CmapEntry &entry = ctx.cmap[i];
        entry.data_offset = rd32(raw); entry.range_start = rd32(raw + 4U);
        entry.range_length = rd16(raw + 8U); entry.glyph_id_start = rd16(raw + 10U);
        entry.data_entries_count = rd16(raw + 12U); entry.format = raw[14];
        if (entry.format > kSparseTiny) return fail();
    }
    ctx.ready = true;
    ctx.font.line_height = ctx.line_height;
    ctx.font.base_line = static_cast<int32_t>(ctx.line_height) - ctx.baseline;
    return true;
}

FontContext *context_for(const lv_font_t *font) {
    return font == nullptr ? nullptr : static_cast<FontContext *>(font->user_data);
}

FontContext *select_context(uint8_t size) {
    return size <= 16U ? &contexts[0] : size <= 18U ? &contexts[1] : &contexts[2];
}

const lv_font_t *fallback_font(uint8_t size) {
#if LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    (void)size;
    return &lv_font_source_han_sans_sc_16_cjk;
#else
    return size >= 20U ? &lv_font_montserrat_20 : size >= 18U ? &lv_font_montserrat_18 : &lv_font_montserrat_16;
#endif
}

GlyphCacheEntry *find_cache(FontContext &ctx, uint32_t cp) {
    for (GlyphCacheEntry &entry : glyph_cache) {
        if (entry.valid && entry.font == &ctx && entry.cp == cp) {
            entry.age = ++cache_age;
            return &entry;
        }
    }
    return nullptr;
}

GlyphCacheEntry *allocate_cache() {
    GlyphCacheEntry *oldest = &glyph_cache[0];
    for (GlyphCacheEntry &entry : glyph_cache) {
        if (!entry.valid) return &entry;
        if (entry.age < oldest->age) oldest = &entry;
    }
    return oldest;
}

uint16_t default_advance(const FontContext &ctx) {
    const uint16_t advance = advance_from_raw(ctx.default_advance);
    return advance != 0U ? advance : ctx.requested_size;
}

void make_full_width_space(FontContext &ctx, GlyphCacheEntry *cached) {
    cached->dsc.advance = default_advance(ctx);
}

void make_missing_placeholder(FontContext &ctx, GlyphCacheEntry *cached) {
    const uint16_t dot_size = ctx.line_height >= 20U ? 4U : 3U;
    const uint16_t advance = default_advance(ctx);
    const int16_t dot_top = static_cast<int16_t>((ctx.line_height - dot_size) / 2U);
    const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
    const uint8_t max_value = static_cast<uint8_t>((1U << pixel_bpp) - 1U);

    cached->placeholder = true;
    cached->dsc.advance = advance;
    cached->dsc.box_w = dot_size;
    cached->dsc.box_h = dot_size;
    cached->dsc.ofs_x = static_cast<int16_t>((advance - dot_size) / 2U);
    cached->dsc.ofs_y = static_cast<int16_t>(
        (ctx.line_height - ctx.font.base_line) - dot_size - dot_top);
    uint32_t bit = 0U;
    for (uint16_t y = 0U; y < dot_size; ++y) {
        for (uint16_t x = 0U; x < dot_size; ++x) {
            bit_write(cached->bitmap, &bit, max_value, pixel_bpp);
        }
    }
}

GlyphCacheEntry *load_cache(FontContext &ctx, uint32_t cp) {
    GlyphCacheEntry *cached = find_cache(ctx, cp);
    if (cached != nullptr) return cached;
    cached = allocate_cache();
    *cached = {};
    cached->font = &ctx; cached->cp = cp; cached->valid = true; cached->age = ++cache_age;
    if (cp == 0x3000U) {
        make_full_width_space(ctx, cached);
        return cached;
    }
    uint32_t glyph = 0U;
    GlyphDsc dsc = {};
    if (!ctx.ready || !lookup(ctx, cp, &glyph) || !get_glyph(ctx, glyph, &dsc, cached->bitmap, sizeof(cached->bitmap))) {
        make_missing_placeholder(ctx, cached);
        return cached;
    }
    const uint8_t pixel_bpp = ctx.bpp == 3U ? 4U : ctx.bpp;
    const uint32_t bitmap_bytes = (static_cast<uint32_t>(dsc.box_w) * dsc.box_h * pixel_bpp + 7U) >> 3U;
    if (bitmap_bytes > sizeof(cached->bitmap)) {
        make_missing_placeholder(ctx, cached);
        return cached;
    }
    cached->dsc = dsc;
    return cached;
}

bool font_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                        uint32_t letter, uint32_t) {
    FontContext *ctx = context_for(font);
    if (ctx == nullptr || letter < 0x80U || !open_context(*ctx)) return false;
    GlyphCacheEntry *cached = load_cache(*ctx, letter);
    if (cached == nullptr) return false;
    out->resolved_font = font;
    out->adv_w = cached->dsc.advance;
    out->box_w = cached->dsc.box_w;
    out->box_h = cached->dsc.box_h;
    out->ofs_x = cached->dsc.ofs_x;
    out->ofs_y = cached->dsc.ofs_y;
    out->stride = 0U;
    out->format = LV_FONT_GLYPH_FORMAT_A8;
    out->is_placeholder = cached->placeholder;
    out->req_raw_bitmap = 0U;
    out->gid.index = letter;
    return true;
}

const void *font_get_glyph_bitmap(lv_font_glyph_dsc_t *glyph,
                                  lv_draw_buf_t *draw_buf) {
    if (glyph == nullptr || draw_buf == nullptr || glyph->resolved_font == nullptr) return nullptr;
    FontContext *ctx = context_for(glyph->resolved_font);
    if (ctx == nullptr) return nullptr;
    GlyphCacheEntry *cached = load_cache(*ctx, glyph->gid.index);
    if (cached == nullptr) return nullptr;
    const uint8_t pixel_bpp = ctx->bpp == 3U ? 4U : ctx->bpp;
    const uint16_t max_value = static_cast<uint16_t>((1U << pixel_bpp) - 1U);
    const uint32_t stride = draw_buf->header.stride;
    std::memset(draw_buf->data, 0, draw_buf->data_size);
    for (uint16_t y = 0U; y < cached->dsc.box_h; ++y) {
        for (uint16_t x = 0U; x < cached->dsc.box_w; ++x) {
            uint32_t bit = (static_cast<uint32_t>(y) * cached->dsc.box_w + x) * pixel_bpp;
            const uint8_t value = static_cast<uint8_t>(bit_read(cached->bitmap, &bit, pixel_bpp));
            draw_buf->data[static_cast<uint32_t>(y) * stride + x] =
                static_cast<uint8_t>((static_cast<uint16_t>(value) * 255U + max_value / 2U) / max_value);
        }
    }
    return draw_buf;
}

const lv_font_t *ui_font(uint8_t size) {
    FontContext *ctx = select_context(size);
    if (ctx->font.get_glyph_dsc == nullptr) {
        ctx->font.get_glyph_dsc = font_get_glyph_dsc;
        ctx->font.get_glyph_bitmap = font_get_glyph_bitmap;
        ctx->font.release_glyph = nullptr;
        ctx->font.kerning = LV_FONT_KERNING_NONE;
        ctx->font.static_bitmap = 0U;
        ctx->font.user_data = ctx;
        ctx->font.fallback = fallback_font(size);
    }
    if (!open_context(*ctx)) return fallback_font(size);
    return &ctx->font;
}
}

const lv_font_t *ui_heiti_font_get(uint8_t size) { return ui_font(size); }
const lv_font_t *ui_heiti_font_get_cached(uint8_t size) { return ui_font(size); }

bool ui_heiti_font_is_ready(uint8_t size) {
    FontContext *ctx = select_context(size);
    return open_context(*ctx);
}

bool ui_heiti_font_warm_text(uint8_t size, const char *text) {
    if (text == nullptr || !ui_heiti_font_is_ready(size)) return false;
    FontContext *ctx = select_context(size);
    while (*text != '\0') {
        const uint8_t first = static_cast<uint8_t>(*text);
        uint32_t cp = first;
        size_t bytes = 1U;
        if ((first & 0xE0U) == 0xC0U) { cp = ((first & 0x1FU) << 6U) | (static_cast<uint8_t>(text[1]) & 0x3FU); bytes = 2U; }
        else if ((first & 0xF0U) == 0xE0U) { cp = ((first & 0x0FU) << 12U) | ((static_cast<uint8_t>(text[1]) & 0x3FU) << 6U) | (static_cast<uint8_t>(text[2]) & 0x3FU); bytes = 3U; }
        else if ((first & 0xF8U) == 0xF0U) { cp = ((first & 7U) << 18U) | ((static_cast<uint8_t>(text[1]) & 0x3FU) << 12U) | ((static_cast<uint8_t>(text[2]) & 0x3FU) << 6U) | (static_cast<uint8_t>(text[3]) & 0x3FU); bytes = 4U; }
        if (cp >= 0x80U && load_cache(*ctx, cp) == nullptr) return false;
        text += bytes;
    }
    return true;
}

bool ui_heiti_font_poetry_cache_init(size_t) { return true; }
bool ui_heiti_font_cache_text(uint8_t size, const char *text) { return ui_heiti_font_warm_text(size, text); }
bool ui_heiti_font_text_is_cached(uint8_t size, const char *text) { return ui_heiti_font_warm_text(size, text); }
size_t ui_heiti_font_poetry_cache_bytes() { return 0U; }
size_t ui_heiti_font_poetry_cache_glyphs() { return 0U; }
uint32_t ui_heiti_font_storage_read_count() { return storage_reads; }
