#include "app/poetry_app.h"

#include <cstring>

#include "bsp/bsp_littlefs.h"

namespace {
constexpr size_t TEXT_CAP = 3072U;
constexpr uint32_t HEADER_SIZE = 32U;
constexpr uint32_t ENTRY_SIZE = 8U;
constexpr uint32_t QIDX_HEADER_SIZE = 16U;
constexpr uint32_t QIDX_MAX_COUNT = 100000U;
char text_buf[TEXT_CAP] = {};
PoetryEntry cached = {};
uint32_t last_index[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
bool initialized = false;

struct LittleFsGuard {
    bool locked;
    explicit LittleFsGuard(TickType_t timeout) : locked(bsp_littlefs_lock(timeout)) {}
    ~LittleFsGuard() { if (locked) bsp_littlefs_unlock(); }
};

const char *path_for(PoetryCollection c) {
    switch (c) {
        case PoetryCollection::Song300: return "/song_300.idx";
        case PoetryCollection::Tang300: return "/tang_300.idx";
        case PoetryCollection::Song3000: return "/song_3000.idx";
        case PoetryCollection::ChinaQuotes: return "/quotes_china.idx";
    }
    return nullptr;
}

bool read_u32(File &file, uint32_t offset, uint32_t *out) {
    uint8_t raw[4] = {};
    if (!file.seek(offset) || file.read(raw, sizeof(raw)) != sizeof(raw)) return false;
    *out = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
          (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
    return true;
}

bool read_text(File &file, uint32_t offset, uint32_t length) {
    const uint32_t size = static_cast<uint32_t>(file.size());
    if (length == 0U || length >= TEXT_CAP || offset > size || length > size - offset ||
        !file.seek(offset) || file.read(reinterpret_cast<uint8_t *>(text_buf), length) != length) {
        return false;
    }
    text_buf[length] = '\0';
    while (length > 0U && (text_buf[length - 1U] == '\0' || text_buf[length - 1U] == '\r' ||
                           text_buf[length - 1U] == '\n')) {
        text_buf[--length] = '\0';
    }
    return length != 0U;
}

bool choose_entry(File &file, uint32_t count, PoetryCollection collection, PoetryEntry *out) {
    if (count == 0U) return false;
    uint32_t index = esp_random() % count;
    const uint8_t ci = static_cast<uint8_t>(collection);
    if (count > 1U && last_index[ci] < count && index == last_index[ci]) index = (index + 1U) % count;
    last_index[ci] = index;
    uint32_t offset = 0U;
    uint32_t length = 0U;
    const uint32_t size = static_cast<uint32_t>(file.size());
    if (index > (size - HEADER_SIZE) / ENTRY_SIZE ||
        !read_u32(file, HEADER_SIZE + index * ENTRY_SIZE, &offset) ||
        !read_u32(file, HEADER_SIZE + index * ENTRY_SIZE + 4U, &length) ||
        length == 0U || length >= TEXT_CAP) return false;
    uint32_t data_offset = 0U;
    if (!read_u32(file, 16U, &data_offset) || data_offset > size ||
        offset > size - data_offset || !read_text(file, data_offset + offset, length)) return false;
    char *title_end = std::strchr(text_buf, '\n');
    if (title_end != nullptr) {
        *title_end = '\0';
        out->body = title_end + 1U;
    } else {
        out->body = text_buf;
    }
    out->title = text_buf;
    out->author = "";
    if (out->title[0] == '\0' || out->body[0] == '\0') return false;
    out->collection = collection;
    out->serial++;
    out->valid = true;
    return true;
}

bool choose_qidx_entry(File &file, uint32_t count, PoetryEntry *out) {
    if (count == 0U || count > QIDX_MAX_COUNT || out == nullptr) return false;
    uint32_t index = esp_random() % count;
    if (count > 1U && last_index[static_cast<uint8_t>(PoetryCollection::ChinaQuotes)] < count &&
        index == last_index[static_cast<uint8_t>(PoetryCollection::ChinaQuotes)]) {
        index = (index + 1U) % count;
    }
    last_index[static_cast<uint8_t>(PoetryCollection::ChinaQuotes)] = index;

    uint32_t offset = 0U;
    uint32_t length = 0U;
    if (!read_u32(file, QIDX_HEADER_SIZE + index * ENTRY_SIZE, &offset) ||
        !read_u32(file, QIDX_HEADER_SIZE + index * ENTRY_SIZE + 4U, &length) ||
        !read_text(file, offset, length)) {
        return false;
    }
    out->title = "QUOTE";
    out->author = "";
    out->body = text_buf;
    out->collection = PoetryCollection::ChinaQuotes;
    out->serial++;
    out->valid = true;
    return true;
}
}

void poetry_app_init() {
    if (initialized) return;
    initialized = true;
    cached = {};
}

bool poetry_app_get_random(PoetryCollection collection, PoetryEntry *out) {
    poetry_app_init();
    const char *path = path_for(collection);
    if (out == nullptr || path == nullptr || !bsp_littlefs_available()) return false;
    LittleFsGuard guard(pdMS_TO_TICKS(100U));
    if (!guard.locked) return false;
    File file = bsp_littlefs_fs().open(path, "r");
    if (!file) return false;
    uint8_t magic[4] = {};
    uint32_t count = 0U;
    bool ok = file.read(magic, sizeof(magic)) == sizeof(magic);
    if (ok && std::memcmp(magic, "QIDX", 4U) == 0 && collection == PoetryCollection::ChinaQuotes) {
        uint32_t data_offset = 0U;
        const uint32_t file_size = static_cast<uint32_t>(file.size());
        ok = read_u32(file, 4U, &count) && read_u32(file, 8U, &data_offset) &&
             count <= QIDX_MAX_COUNT && data_offset >= QIDX_HEADER_SIZE && data_offset <= file_size;
        if (ok) {
            // Some shipped QIDX files include one extra count for the data-start
            // sentinel. Limit random access to the complete item table.
            const uint32_t table_count = (data_offset - QIDX_HEADER_SIZE) / ENTRY_SIZE;
            if (count > table_count) count = table_count;
            ok = count != 0U && choose_qidx_entry(file, count, out);
        }
    } else if (ok && std::memcmp(magic, "SNGI", 4U) == 0 && collection != PoetryCollection::ChinaQuotes) {
        ok = read_u32(file, 8U, &count) && count > 0U &&
             HEADER_SIZE + count * ENTRY_SIZE <= file.size() && choose_entry(file, count, collection, out);
    } else {
        ok = false;
    }
    file.close();
    if (ok) cached = *out;
    return ok;
}

bool poetry_app_get_cached(PoetryEntry *out) {
    if (out == nullptr || !cached.valid) return false;
    *out = cached;
    return true;
}

const char *poetry_app_collection_name(PoetryCollection collection) {
    switch (collection) {
        case PoetryCollection::Song300: return "SONG 300";
        case PoetryCollection::Tang300: return "TANG 300";
        case PoetryCollection::Song3000: return "SONG 3000";
        case PoetryCollection::ChinaQuotes: return "CHINA QUOTES";
    }
    return "POETRY";
}
