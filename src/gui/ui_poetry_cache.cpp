#include "gui/ui_poetry_cache.h"

#include <cstring>
#include <esp_heap_caps.h>

#include "app/player_app.h"
#include "gui/ui_heiti_font.h"

namespace {
constexpr size_t SHARED_GLYPH_POOL_BYTES = 512U * 1024U;
constexpr uint8_t PREPARE_RETRIES = 24U;
constexpr uint32_t SLOT_INTERVAL_MS = 250U;
constexpr uint32_t FAILURE_RETRY_MS = 5000U;

constexpr PoetryCollection SLOT_COLLECTION = PoetryCollection::Song3000;

UiPoetryCacheSlot *slots = nullptr;
uint8_t next_slot = 0U;
uint8_t selection_cursor[4] = {};
uint32_t next_attempt_ms = 0U;
bool initialized = false;

enum class PrepareFailure : uint8_t {
    None,
    Invalid,
    Duplicate,
    Layout,
    Glyph,
    Count,
};

void clear_slot(UiPoetryCacheSlot &slot) {
    slot.valid = false;
    slot.in_use = false;
    slot.content_hash = 0U;
    slot.line_count = 0U;
}

uint8_t utf8_bytes(const char *text) {
    if (text == nullptr || *text == '\0') return 0U;
    const uint8_t value = static_cast<uint8_t>(*text);
    uint8_t bytes = (value & 0x80U) == 0U ? 1U :
                    (value & 0xE0U) == 0xC0U ? 2U :
                    (value & 0xF0U) == 0xE0U ? 3U :
                    (value & 0xF8U) == 0xF0U ? 4U : 1U;
    for (uint8_t i = 1U; i < bytes; ++i) {
        if (text[i] == '\0') return i;
    }
    return bytes;
}

int16_t glyph_width(uint8_t bytes) {
    return bytes == 1U ? UI_POETRY_FONT_SIZE / 2 : UI_POETRY_FONT_SIZE;
}

int16_t text_width(const char *text) {
    int16_t width = 0;
    while (text != nullptr && *text != '\0') {
        const uint8_t bytes = utf8_bytes(text);
        if (bytes == 0U) break;
        width = static_cast<int16_t>(width + glyph_width(bytes));
        text += bytes;
    }
    return width;
}

void copy_utf8(char *destination, size_t capacity, const char *source) {
    if (destination == nullptr || capacity == 0U) return;
    size_t used = 0U;
    while (source != nullptr && *source != '\0') {
        const uint8_t bytes = utf8_bytes(source);
        if (bytes == 0U || used + bytes >= capacity) break;
        std::memcpy(destination + used, source, bytes);
        used += bytes;
        source += bytes;
    }
    destination[used] = '\0';
}

uint32_t content_hash(const PoetryEntry &entry) {
    uint32_t hash = 2166136261UL;
    const char *parts[2] = {entry.title, entry.body};
    for (const char *part : parts) {
        while (part != nullptr && *part != '\0') {
            hash ^= static_cast<uint8_t>(*part++);
            hash *= 16777619UL;
        }
        hash ^= 0xFFU;
        hash *= 16777619UL;
    }
    return hash;
}

bool duplicate_hash(uint32_t hash) {
    for (uint8_t i = 0U; i < UI_POETRY_CACHE_SLOT_COUNT; ++i) {
        if (slots[i].valid && slots[i].content_hash == hash) return true;
    }
    return false;
}

bool layout_body(UiPoetryCacheSlot &slot, const char *source) {
    size_t output = 0U;
    uint8_t line_index = 0U;
    int16_t line_width = 0;
    int16_t line_limit = UI_POETRY_DISPLAY_W - UI_POETRY_FIRST_LINE_INDENT;
    bool source_line_start = true;

    while (source != nullptr && *source != '\0' && output + 1U < sizeof(slot.body)) {
        if (*source == '\r' || *source == '\n') {
            ++source;
            source_line_start = true;
            continue;
        }
        if (source_line_start && (*source == ' ' || *source == '\t')) {
            ++source;
            continue;
        }
        source_line_start = false;
        const uint8_t bytes = utf8_bytes(source);
        if (bytes == 0U || output + bytes >= sizeof(slot.body)) return false;
        const int16_t width = glyph_width(bytes);
        if (line_width > 0 && line_width + width > line_limit) {
            if (line_index + 1U >= UI_POETRY_MAX_LINES ||
                output + 1U >= sizeof(slot.body)) return false;
            slot.body[output++] = '\n';
            ++line_index;
            line_width = 0;
            line_limit = UI_POETRY_DISPLAY_W;
        }
        std::memcpy(slot.body + output, source, bytes);
        output += bytes;
        source += bytes;
        line_width = static_cast<int16_t>(line_width + width);
    }
    slot.body[output] = '\0';
    if (slot.body[0] == '\0') return false;

    slot.line_count = 0U;
    char *line = slot.body;
    while (*line != '\0' && slot.line_count < UI_POETRY_MAX_LINES) {
        slot.lines[slot.line_count++] = line;
        char *end = std::strchr(line, '\n');
        if (end == nullptr) break;
        *end = '\0';
        line = end + 1;
    }
    slot.body_height = slot.line_count == 0U ? 0 :
        static_cast<int16_t>((slot.line_count - 1U) * UI_POETRY_LINE_STEP +
                             UI_POETRY_FONT_SIZE);
    if (slot.line_count == 0U || slot.body_height > UI_POETRY_BODY_H) return false;

    const int16_t body_y = UI_POETRY_TEXT_PAD_Y + UI_POETRY_TITLE_LINE_H +
                           UI_POETRY_TITLE_BODY_GAP;
    const int16_t start_y = static_cast<int16_t>(
        body_y + (UI_POETRY_BODY_H - slot.body_height) / 2);
    for (uint8_t i = 0U; i < slot.line_count; ++i) {
        slot.line_x[i] = static_cast<int16_t>(
            UI_POETRY_TEXT_PAD_X +
            (i == 0U ? UI_POETRY_FIRST_LINE_INDENT : 0));
        slot.line_y[i] = static_cast<int16_t>(
            start_y + i * UI_POETRY_LINE_STEP);
    }
    return true;
}

bool prepare_slot(UiPoetryCacheSlot &slot, const PoetryEntry &entry,
                  PrepareFailure *failure) {
    if (failure != nullptr) *failure = PrepareFailure::None;
    clear_slot(slot);
    if (!entry.valid || entry.title == nullptr || entry.body == nullptr) {
        if (failure != nullptr) *failure = PrepareFailure::Invalid;
        return false;
    }
    const uint32_t hash = content_hash(entry);
    if (duplicate_hash(hash)) {
        if (failure != nullptr) *failure = PrepareFailure::Duplicate;
        return false;
    }

    copy_utf8(slot.title, sizeof(slot.title), entry.title);
    slot.title_width = text_width(slot.title);
    if (slot.title[0] == '\0' || slot.title_width > UI_POETRY_DISPLAY_W ||
        !layout_body(slot, entry.body)) {
        if (failure != nullptr) *failure = PrepareFailure::Layout;
        return false;
    }

    if (!ui_heiti_font_cache_text(UI_POETRY_FONT_SIZE, slot.title)) {
        if (failure != nullptr) *failure = PrepareFailure::Glyph;
        return false;
    }
    for (uint8_t i = 0U; i < slot.line_count; ++i) {
        if (!ui_heiti_font_cache_text(UI_POETRY_FONT_SIZE, slot.lines[i])) {
            if (failure != nullptr) *failure = PrepareFailure::Glyph;
            return false;
        }
    }
    if (!ui_heiti_font_text_is_cached(UI_POETRY_FONT_SIZE, slot.title)) {
        if (failure != nullptr) *failure = PrepareFailure::Glyph;
        return false;
    }
    for (uint8_t i = 0U; i < slot.line_count; ++i) {
        if (!ui_heiti_font_text_is_cached(UI_POETRY_FONT_SIZE, slot.lines[i])) {
            if (failure != nullptr) *failure = PrepareFailure::Glyph;
            return false;
        }
    }

    slot.collection = entry.collection;
    slot.content_hash = hash;
    slot.valid = true;
    return true;
}
}

bool ui_poetry_cache_init() {
    if (initialized) return slots != nullptr;
    initialized = true;
    poetry_app_init();
    if (!psramFound() ||
        !ui_heiti_font_poetry_cache_init(SHARED_GLYPH_POOL_BYTES)) {
        Serial.println("[POETRY_CACHE] PSRAM glyph cache unavailable");
        return false;
    }
    slots = static_cast<UiPoetryCacheSlot *>(
        heap_caps_calloc(UI_POETRY_CACHE_SLOT_COUNT, sizeof(UiPoetryCacheSlot),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (slots == nullptr) {
        Serial.println("[POETRY_CACHE] PSRAM slot allocation failed");
        return false;
    }
    Serial.printf("[POETRY_CACHE] slots=%u bytes, free_psram=%u\n",
                  static_cast<unsigned>(sizeof(UiPoetryCacheSlot) *
                                        UI_POETRY_CACHE_SLOT_COUNT),
                  static_cast<unsigned>(ESP.getFreePsram()));
    return true;
}

bool ui_poetry_cache_service() {
    if (!ui_poetry_cache_init()) return false;
    PlayerStatus status = {};
    if (player_app_get_status(&status) && status.state == PlayerState::Playing) return false;
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - next_attempt_ms) < 0) return false;

    uint8_t slot_index = next_slot;
    bool found_slot = false;
    for (uint8_t offset = 0U; offset < UI_POETRY_CACHE_SLOT_COUNT; ++offset) {
        const uint8_t index = static_cast<uint8_t>(
            (next_slot + offset) % UI_POETRY_CACHE_SLOT_COUNT);
        if (!slots[index].valid && !slots[index].in_use) {
            slot_index = index;
            found_slot = true;
            break;
        }
    }
    if (!found_slot) return false;

    UiPoetryCacheSlot &slot = slots[slot_index];
    const PoetryCollection collection = SLOT_COLLECTION;
    uint8_t read_failures = 0U;
    uint8_t failures[static_cast<uint8_t>(PrepareFailure::Count)] = {};
    for (uint8_t retry = 0U; retry < PREPARE_RETRIES; ++retry) {
        PoetryEntry entry = {};
        if (!poetry_app_get_random(collection, &entry)) {
            ++read_failures;
            continue;
        }
        PrepareFailure failure = PrepareFailure::None;
        if (prepare_slot(slot, entry, &failure)) {
            next_slot = static_cast<uint8_t>(
                (slot_index + 1U) % UI_POETRY_CACHE_SLOT_COUNT);
            next_attempt_ms = now + SLOT_INTERVAL_MS;
            Serial.printf("[POETRY_CACHE] ready=%u/%u collection=%u glyphs=%u reads=%lu free_psram=%u\n",
                          static_cast<unsigned>(ui_poetry_cache_ready_count(
                              PoetryCollection::Song3000)),
                          UI_POETRY_CACHE_SLOT_COUNT,
                          static_cast<unsigned>(collection),
                          static_cast<unsigned>(ui_heiti_font_poetry_cache_glyphs()),
                          static_cast<unsigned long>(ui_heiti_font_storage_read_count()),
                          static_cast<unsigned>(ESP.getFreePsram()));
            return true;
        }
        ++failures[static_cast<uint8_t>(failure)];
    }
    next_slot = slot_index;
    next_attempt_ms = now + FAILURE_RETRY_MS;
    Serial.printf("[POETRY_CACHE] prepare failed collection=%u read=%u invalid=%u duplicate=%u layout=%u glyph=%u retry_ms=%lu\n",
                  static_cast<unsigned>(collection), read_failures,
                  failures[static_cast<uint8_t>(PrepareFailure::Invalid)],
                  failures[static_cast<uint8_t>(PrepareFailure::Duplicate)],
                  failures[static_cast<uint8_t>(PrepareFailure::Layout)],
                  failures[static_cast<uint8_t>(PrepareFailure::Glyph)],
                  static_cast<unsigned long>(FAILURE_RETRY_MS));
    return false;
}

const UiPoetryCacheSlot *ui_poetry_cache_select(PoetryCollection collection) {
    // A prepared entry is safe to display while the rest of the pool warms up.
    if (slots == nullptr) return nullptr;
    const uint8_t ci = static_cast<uint8_t>(collection);
    if (ci >= sizeof(selection_cursor)) return nullptr;
    for (uint8_t offset = 0U; offset < UI_POETRY_CACHE_SLOT_COUNT; ++offset) {
        const uint8_t index = static_cast<uint8_t>(
            (selection_cursor[ci] + offset) % UI_POETRY_CACHE_SLOT_COUNT);
        if (slots[index].valid && !slots[index].in_use &&
            slots[index].collection == collection) {
            slots[index].in_use = true;
            selection_cursor[ci] = static_cast<uint8_t>(
                (index + 1U) % UI_POETRY_CACHE_SLOT_COUNT);
            return &slots[index];
        }
    }
    return nullptr;
}

void ui_poetry_cache_release(const UiPoetryCacheSlot *slot) {
    if (slots == nullptr || slot == nullptr) return;
    for (uint8_t index = 0U; index < UI_POETRY_CACHE_SLOT_COUNT; ++index) {
        if (slot != &slots[index]) continue;
        if (!slots[index].valid || !slots[index].in_use) return;
        clear_slot(slots[index]);
        return;
    }
}

size_t ui_poetry_cache_ready_count(PoetryCollection collection) {
    size_t count = 0U;
    if (slots == nullptr) return count;
    for (uint8_t i = 0U; i < UI_POETRY_CACHE_SLOT_COUNT; ++i) {
        if (slots[i].valid && !slots[i].in_use &&
            slots[i].collection == collection) ++count;
    }
    return count;
}

void ui_poetry_cache_draw(egui_canvas_t *canvas, const UiPoetryCacheSlot *slot,
                          int16_t panel_x, int16_t panel_y) {
    if (canvas == nullptr || slot == nullptr || !slot->valid) return;
    const egui_font_t *font = ui_heiti_font_get_cached(UI_POETRY_FONT_SIZE);
    const egui_region_t *work = egui_canvas_get_base_view_work_region(canvas);
    const int16_t display_x = static_cast<int16_t>(panel_x + UI_POETRY_TEXT_PAD_X);
    const int16_t title_y = static_cast<int16_t>(panel_y + UI_POETRY_TEXT_PAD_Y);
    const int16_t title_x = static_cast<int16_t>(
        display_x + (UI_POETRY_DISPLAY_W - slot->title_width) / 2);
    if (title_y + UI_POETRY_FONT_SIZE > work->location.y &&
        title_y < work->location.y + work->size.height) {
        egui_canvas_draw_text(canvas, font, slot->title, title_x, title_y,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }

    for (uint8_t i = 0U; i < slot->line_count; ++i) {
        const int16_t line_y = static_cast<int16_t>(panel_y + slot->line_y[i]);
        if (line_y + UI_POETRY_FONT_SIZE <= work->location.y ||
            line_y >= work->location.y + work->size.height) continue;
        const int16_t line_x = static_cast<int16_t>(panel_x + slot->line_x[i]);
        egui_canvas_draw_text(canvas, font, slot->lines[i], line_x, line_y,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
}
