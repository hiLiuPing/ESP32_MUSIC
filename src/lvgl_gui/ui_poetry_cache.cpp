#include "gui/ui_poetry_cache.h"

#include <cstring>
#include <esp_heap_caps.h>

#include "gui/ui_heiti_font.h"

namespace {
constexpr size_t kGlyphPoolBytes = 384U * 1024U;
constexpr size_t kTitleBytes = 128U;
constexpr size_t kBodyBytes = 3072U;
constexpr uint8_t kGlyphsPerService = 2U;
constexpr uint8_t kCollectRetries = 12U;

enum class BatchState : uint8_t { Empty, Collecting, Warming, Ready };

struct StoredEntry {
    char title[kTitleBytes] = {};
    char body[kBodyBytes] = {};
    uint32_t content_hash = 0U;
};

struct PoetryBatch {
    PoetryCollection collection = PoetryCollection::Song3000;
    BatchState state = BatchState::Empty;
    uint8_t count = 0U;
    uint8_t warm_entry = 0U;
    bool warm_title = true;
    const char *warm_cursor = nullptr;
    StoredEntry entries[UI_POETRY_BATCH_SIZE] = {};
};

PoetryBatch *batches = nullptr;
bool initialized = false;
bool available = false;
uint8_t active_batch = 0U;
uint8_t prefetch_batch = 1U;
uint8_t active_index = 0U;
uint8_t popup_index = 0U;
UiPoetryCacheEntry selected = {};

uint8_t utf8_bytes(const char *text) {
    if (text == nullptr || *text == '\0') return 0U;
    const uint8_t first = static_cast<uint8_t>(*text);
    const uint8_t bytes = (first & 0x80U) == 0U ? 1U :
                          (first & 0xE0U) == 0xC0U ? 2U :
                          (first & 0xF0U) == 0xE0U ? 3U :
                          (first & 0xF8U) == 0xF0U ? 4U : 1U;
    for (uint8_t i = 1U; i < bytes; ++i) if (text[i] == '\0') return 1U;
    return bytes;
}

uint32_t decode_utf8(const char *text, uint8_t bytes) {
    const uint8_t first = static_cast<uint8_t>(text[0]);
    if (bytes == 1U) return first;
    if (bytes == 2U) return ((first & 0x1FU) << 6U) | (static_cast<uint8_t>(text[1]) & 0x3FU);
    if (bytes == 3U) return ((first & 0x0FU) << 12U) | ((static_cast<uint8_t>(text[1]) & 0x3FU) << 6U) |
                             (static_cast<uint8_t>(text[2]) & 0x3FU);
    return ((first & 7U) << 18U) | ((static_cast<uint8_t>(text[1]) & 0x3FU) << 12U) |
           ((static_cast<uint8_t>(text[2]) & 0x3FU) << 6U) | (static_cast<uint8_t>(text[3]) & 0x3FU);
}

void copy_text(char *destination, size_t capacity, const char *source) {
    if (destination == nullptr || capacity == 0U) return;
    size_t used = 0U;
    while (source != nullptr && *source != '\0') {
        const uint8_t bytes = utf8_bytes(source);
        if (used + bytes >= capacity) break;
        std::memcpy(destination + used, source, bytes);
        used += bytes;
        source += bytes;
    }
    destination[used] = '\0';
}

uint32_t content_hash(const char *title, const char *body) {
    uint32_t hash = 2166136261UL;
    for (const char *part : {title, body}) {
        while (part != nullptr && *part != '\0') {
            hash ^= static_cast<uint8_t>(*part++);
            hash *= 16777619UL;
        }
        hash ^= 0xFFU;
        hash *= 16777619UL;
    }
    return hash;
}

bool is_duplicate(uint32_t hash) {
    if (batches == nullptr) return false;
    for (uint8_t batch_index = 0U; batch_index < 2U; ++batch_index) {
        const PoetryBatch &batch = batches[batch_index];
        for (uint8_t i = 0U; i < batch.count; ++i) {
            if (batch.entries[i].content_hash == hash) return true;
        }
    }
    return false;
}

void start_batch(PoetryBatch &batch, PoetryCollection collection) {
    std::memset(&batch, 0, sizeof(batch));
    batch.collection = collection;
    batch.state = BatchState::Collecting;
}

const UiPoetryCacheEntry *entry_at(const PoetryBatch &batch, uint8_t index) {
    if (batch.state != BatchState::Ready || index >= batch.count) return nullptr;
    const StoredEntry &stored = batch.entries[index];
    selected = {stored.title, stored.body, batch.collection, stored.content_hash};
    return &selected;
}

void log_batch(const char *phase, const PoetryBatch &batch) {
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    Serial.printf("[POETRY_CACHE] %s collection=%u count=%u glyphs=%u bytes=%u reads=%lu free=%u largest=%u\n",
                  phase, static_cast<unsigned>(batch.collection), static_cast<unsigned>(batch.count),
                  static_cast<unsigned>(ui_heiti_font_poetry_cache_glyphs()),
                  static_cast<unsigned>(ui_heiti_font_poetry_cache_bytes()),
                  static_cast<unsigned long>(ui_heiti_font_storage_read_count()),
                  static_cast<unsigned>(heap_caps_get_free_size(caps)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
}

bool collect_step(PoetryBatch &batch) {
    for (uint8_t retry = 0U; retry < kCollectRetries; ++retry) {
        PoetryEntry entry = {};
        if (!poetry_app_get_random(batch.collection, &entry) || !entry.valid ||
            entry.title == nullptr || entry.body == nullptr) continue;
        const uint32_t hash = content_hash(entry.title, entry.body);
        if (is_duplicate(hash)) continue;
        StoredEntry &stored = batch.entries[batch.count];
        copy_text(stored.title, sizeof(stored.title), entry.title);
        copy_text(stored.body, sizeof(stored.body), entry.body);
        if (stored.title[0] == '\0' || stored.body[0] == '\0') continue;
        stored.content_hash = hash;
        ++batch.count;
        if (batch.count == UI_POETRY_BATCH_SIZE) {
            batch.state = BatchState::Warming;
            batch.warm_entry = 0U;
            batch.warm_title = true;
            batch.warm_cursor = batch.entries[0].title;
            Serial.printf("[POETRY_CACHE] collected collection=%u count=%u\n",
                          static_cast<unsigned>(batch.collection), static_cast<unsigned>(batch.count));
        }
        return true;
    }
    return false;
}

bool warm_step(PoetryBatch &batch) {
    uint8_t warmed = 0U;
    while (warmed < kGlyphsPerService && batch.state == BatchState::Warming) {
        if (batch.warm_cursor == nullptr || *batch.warm_cursor == '\0') {
            if (batch.warm_title) {
                batch.warm_title = false;
                batch.warm_cursor = batch.entries[batch.warm_entry].body;
                continue;
            }
            ++batch.warm_entry;
            if (batch.warm_entry >= batch.count) {
                batch.state = BatchState::Ready;
                log_batch("ready", batch);
                return true;
            }
            batch.warm_title = true;
            batch.warm_cursor = batch.entries[batch.warm_entry].title;
            continue;
        }
        const uint8_t bytes = utf8_bytes(batch.warm_cursor);
        const uint32_t codepoint = decode_utf8(batch.warm_cursor, bytes);
        batch.warm_cursor += bytes;
        if (codepoint < 0x80U || codepoint == 0x0AU || codepoint == 0x0DU) continue;
        if (!ui_heiti_font_cache_codepoint(18U, codepoint)) return false;
        ++warmed;
    }
    return warmed != 0U;
}

void promote_prefetch() {
    const uint8_t old_active = active_batch;
    active_batch = prefetch_batch;
    prefetch_batch = old_active;
    active_index = 0U;
    popup_index = 0U;
    start_batch(batches[prefetch_batch], batches[active_batch].collection);
    log_batch("promoted", batches[active_batch]);
}
}

bool ui_poetry_cache_init() {
    if (initialized) return available;
    initialized = true;
    poetry_app_init();
    if (!ui_heiti_font_poetry_cache_init(kGlyphPoolBytes)) {
        Serial.println("[POETRY_CACHE] PSRAM glyph pool unavailable; using direct LittleFS fallback");
        return false;
    }
    batches = static_cast<PoetryBatch *>(heap_caps_calloc(2U, sizeof(PoetryBatch),
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (batches == nullptr) {
        Serial.println("[POETRY_CACHE] PSRAM text batches unavailable; using direct LittleFS fallback");
        return false;
    }
    available = true;
    start_batch(batches[active_batch], PoetryCollection::Song3000);
    Serial.println("[POETRY_CACHE] activate collection=2");
    return true;
}

void ui_poetry_cache_activate(PoetryCollection collection) {
    if (!ui_poetry_cache_init() || batches == nullptr) return;
    if (batches[active_batch].collection == collection &&
        batches[active_batch].state != BatchState::Empty) return;
    ui_heiti_font_poetry_cache_reset();
    active_batch = 0U;
    prefetch_batch = 1U;
    active_index = 0U;
    popup_index = 0U;
    start_batch(batches[active_batch], collection);
    std::memset(&batches[prefetch_batch], 0, sizeof(PoetryBatch));
    Serial.printf("[POETRY_CACHE] activate collection=%u\n", static_cast<unsigned>(collection));
}

bool ui_poetry_cache_service() {
    if (!ui_poetry_cache_init() || batches == nullptr) return false;
    PoetryBatch &active = batches[active_batch];
    if (active.state == BatchState::Collecting) return collect_step(active);
    if (active.state == BatchState::Warming) return warm_step(active);
    PoetryBatch &prefetch = batches[prefetch_batch];
    if (prefetch.state == BatchState::Empty) {
        start_batch(prefetch, active.collection);
        Serial.printf("[POETRY_CACHE] prefetch collection=%u\n", static_cast<unsigned>(active.collection));
        return true;
    }
    if (prefetch.state == BatchState::Collecting) return collect_step(prefetch);
    if (prefetch.state == BatchState::Warming) return warm_step(prefetch);
    return false;
}

bool ui_poetry_cache_is_ready() {
    return available && batches != nullptr && batches[active_batch].state == BatchState::Ready;
}

const UiPoetryCacheEntry *ui_poetry_cache_current() {
    return ui_poetry_cache_is_ready() ? entry_at(batches[active_batch], active_index) : nullptr;
}

const UiPoetryCacheEntry *ui_poetry_cache_move(int8_t direction) {
    if (!ui_poetry_cache_is_ready() || direction == 0) return ui_poetry_cache_current();
    if (direction < 0) {
        active_index = active_index == 0U ? UI_POETRY_BATCH_SIZE - 1U : active_index - 1U;
        return entry_at(batches[active_batch], active_index);
    }
    if (active_index + 1U < UI_POETRY_BATCH_SIZE) {
        ++active_index;
        return entry_at(batches[active_batch], active_index);
    }
    if (batches[prefetch_batch].state != BatchState::Ready) return nullptr;
    promote_prefetch();
    return entry_at(batches[active_batch], active_index);
}

const UiPoetryCacheEntry *ui_poetry_cache_take_for_popup(PoetryCollection collection) {
    ui_poetry_cache_activate(collection);
    if (!ui_poetry_cache_is_ready()) return nullptr;
    if (popup_index >= UI_POETRY_BATCH_SIZE) {
        if (batches[prefetch_batch].state != BatchState::Ready) return nullptr;
        promote_prefetch();
    }
    return entry_at(batches[active_batch], popup_index++);
}

uint8_t ui_poetry_cache_active_index() { return active_index; }
