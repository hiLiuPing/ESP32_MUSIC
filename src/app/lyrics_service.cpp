#include "app/lyrics_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

#include "app/player_types.h"

namespace {
constexpr size_t LYRICS_MAX_ENTRIES = 512U;
constexpr size_t LYRICS_TEXT_POOL_SIZE = 16U * 1024U;
constexpr size_t LYRICS_MAX_TIMESTAMPS_PER_LINE = 16U;

struct LyricEntry {
    uint32_t timestamp_ms;
    uint16_t text_offset;
    uint16_t order;
};

LyricEntry entries[LYRICS_MAX_ENTRIES] = {};
char *text_pool = nullptr;
size_t entry_count = 0U;
size_t text_used = 0U;
uint16_t next_order = 0U;
SemaphoreHandle_t lyrics_mutex = nullptr;

class LockGuard {
public:
    explicit LockGuard(TickType_t timeout)
        : locked_(lyrics_mutex != nullptr &&
                  xSemaphoreTake(lyrics_mutex, timeout) == pdTRUE) {}
    ~LockGuard() {
        if (locked_) xSemaphoreGive(lyrics_mutex);
    }
    bool locked() const { return locked_; }

private:
    bool locked_ = false;
};

void clear_unlocked() {
    entry_count = 0U;
    text_used = 0U;
    next_order = 0U;
    if (text_pool != nullptr) text_pool[0] = '\0';
}

bool ensure_text_pool() {
    if (text_pool != nullptr) return true;
    text_pool = static_cast<char *>(
        heap_caps_calloc(LYRICS_TEXT_POOL_SIZE, sizeof(char),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (text_pool == nullptr) {
        Serial.println("[LYRICS] PSRAM text pool allocation failed");
        return false;
    }
    return true;
}

bool build_lrc_path(const char *audio_path, char *out, size_t out_capacity) {
    if (audio_path == nullptr || out == nullptr || out_capacity == 0U) return false;
    const size_t length = std::strlen(audio_path);
    if (length == 0U || length >= out_capacity) return false;

    const char *extension = std::strrchr(audio_path, '.');
    const char *slash = std::strrchr(audio_path, '/');
    if (extension == nullptr || (slash != nullptr && extension < slash)) return false;
    const size_t stem_length = static_cast<size_t>(extension - audio_path);
    if (stem_length + sizeof(".lrc") > out_capacity) return false;
    std::memcpy(out, audio_path, stem_length);
    std::memcpy(out + stem_length, ".lrc", sizeof(".lrc"));
    return true;
}

bool parse_timestamp(const char *text, size_t length, uint32_t *timestamp_ms) {
    if (text == nullptr || timestamp_ms == nullptr || length < 4U) return false;
    size_t index = 0U;
    uint32_t minutes = 0U;
    uint32_t seconds = 0U;
    uint32_t fraction = 0U;
    uint8_t fraction_digits = 0U;

    while (index < length && std::isdigit(static_cast<unsigned char>(text[index]))) {
        minutes = minutes * 10U + static_cast<uint32_t>(text[index] - '0');
        if (minutes > 71582U) return false;
        ++index;
    }
    if (index == 0U || index >= length || text[index++] != ':') return false;

    const size_t seconds_start = index;
    while (index < length && std::isdigit(static_cast<unsigned char>(text[index]))) {
        seconds = seconds * 10U + static_cast<uint32_t>(text[index] - '0');
        if (seconds > 99U) return false;
        ++index;
    }
    if (index == seconds_start || seconds >= 60U) return false;

    if (index < length && text[index] == '.') {
        ++index;
        while (index < length && std::isdigit(static_cast<unsigned char>(text[index]))) {
            if (fraction_digits >= 3U) return false;
            fraction = fraction * 10U + static_cast<uint32_t>(text[index] - '0');
            ++fraction_digits;
            ++index;
        }
        if (fraction_digits == 0U) return false;
    }
    if (index != length) return false;

    if (fraction_digits == 1U) fraction *= 100U;
    else if (fraction_digits == 2U) fraction *= 10U;
    const uint64_t total =
        (static_cast<uint64_t>(minutes) * 60ULL + seconds) * 1000ULL + fraction;
    if (total > UINT32_MAX) return false;
    *timestamp_ms = static_cast<uint32_t>(total);
    return true;
}

bool valid_utf8_sequence(const char *text, size_t remaining, size_t *bytes) {
    if (text == nullptr || bytes == nullptr || remaining == 0U) return false;
    const uint8_t lead = static_cast<uint8_t>(text[0]);
    if (lead < 0x80U) {
        *bytes = 1U;
        return true;
    }
    const size_t count = (lead & 0xE0U) == 0xC0U ? 2U :
                         (lead & 0xF0U) == 0xE0U ? 3U :
                         (lead & 0xF8U) == 0xF0U ? 4U : 0U;
    if (count == 0U || count > remaining) return false;
    for (size_t index = 1U; index < count; ++index) {
        if ((static_cast<uint8_t>(text[index]) & 0xC0U) != 0x80U) return false;
    }
    *bytes = count;
    return true;
}

size_t copy_utf8_truncated(char *out, size_t out_capacity, const char *text) {
    if (out == nullptr || out_capacity == 0U || text == nullptr) return 0U;
    size_t written = 0U;
    for (size_t index = 0U; text[index] != '\0';) {
        size_t bytes = 0U;
        if (!valid_utf8_sequence(text + index, std::strlen(text + index), &bytes)) {
            ++index;
            continue;
        }
        if (written + bytes >= out_capacity) break;
        std::memcpy(out + written, text + index, bytes);
        written += bytes;
        index += bytes;
    }
    out[written] = '\0';
    return written;
}

char *trim_text(char *text) {
    if (text == nullptr) return text;
    while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text))) ++text;
    size_t length = std::strlen(text);
    while (length > 0U && std::isspace(static_cast<unsigned char>(text[length - 1U]))) {
        text[--length] = '\0';
    }
    return text;
}

void append_line(const uint32_t *timestamps, size_t timestamp_count, char *text,
                 bool *entry_limit_reported, bool *text_limit_reported) {
    if (timestamps == nullptr || timestamp_count == 0U || text == nullptr) return;
    const char *trimmed = trim_text(text);
    const size_t remaining = LYRICS_TEXT_POOL_SIZE - text_used;
    if (remaining == 0U) {
        if (text_limit_reported != nullptr && !*text_limit_reported) {
            *text_limit_reported = true;
            Serial.println("[LYRICS] text pool full; remaining lyrics skipped");
        }
        return;
    }

    const uint16_t text_offset = static_cast<uint16_t>(text_used);
    const size_t copied = copy_utf8_truncated(text_pool + text_used, remaining, trimmed);
    text_used += copied + 1U;

    for (size_t index = 0U; index < timestamp_count; ++index) {
        if (entry_count >= LYRICS_MAX_ENTRIES) {
            if (entry_limit_reported != nullptr && !*entry_limit_reported) {
                *entry_limit_reported = true;
                Serial.printf("[LYRICS] entry limit reached: %u\n",
                              static_cast<unsigned>(LYRICS_MAX_ENTRIES));
            }
            return;
        }
        entries[entry_count++] = {timestamps[index], text_offset, next_order++};
    }
}

void parse_line(char *line, bool first_line, bool *entry_limit_reported,
                bool *text_limit_reported) {
    if (line == nullptr) return;
    if (first_line && static_cast<uint8_t>(line[0]) == 0xEFU &&
        static_cast<uint8_t>(line[1]) == 0xBBU &&
        static_cast<uint8_t>(line[2]) == 0xBFU) {
        std::memmove(line, line + 3, std::strlen(line + 3) + 1U);
    }

    uint32_t timestamps[LYRICS_MAX_TIMESTAMPS_PER_LINE] = {};
    size_t timestamp_count = 0U;
    size_t index = 0U;
    while (line[index] == '[') {
        char *closing = std::strchr(line + index + 1U, ']');
        if (closing == nullptr) break;
        uint32_t timestamp = 0U;
        const size_t token_length = static_cast<size_t>(closing - (line + index + 1U));
        if (!parse_timestamp(line + index + 1U, token_length, &timestamp)) break;
        if (timestamp_count < LYRICS_MAX_TIMESTAMPS_PER_LINE) {
            timestamps[timestamp_count++] = timestamp;
        }
        index = static_cast<size_t>(closing - line) + 1U;
    }
    if (timestamp_count != 0U) {
        append_line(timestamps, timestamp_count, line + index,
                    entry_limit_reported, text_limit_reported);
    }
}
}

void lyrics_service_attach_mutex(SemaphoreHandle_t mutex) {
    lyrics_mutex = mutex;
    (void)ensure_text_pool();
}

void lyrics_service_clear() {
    LockGuard lock(portMAX_DELAY);
    if (lock.locked()) clear_unlocked();
}

bool lyrics_service_load(fs::FS &filesystem, const char *audio_path) {
    if (!ensure_text_pool()) return false;
    char lrc_path[PLAYER_PATH_LENGTH] = {};
    if (!build_lrc_path(audio_path, lrc_path, sizeof(lrc_path))) {
        lyrics_service_clear();
        return false;
    }

    File file = filesystem.open(lrc_path, FILE_READ);
    if (!file) {
        lyrics_service_clear();
        Serial.printf("[LYRICS] no LRC: %s\n", lrc_path);
        return false;
    }

    LockGuard lock(portMAX_DELAY);
    if (!lock.locked()) {
        file.close();
        return false;
    }
    clear_unlocked();

    char line[LYRICS_LINE_BUFFER_SIZE] = {};
    size_t line_length = 0U;
    bool first_line = true;
    bool line_overflow = false;
    bool overflow_reported = false;
    bool entry_limit_reported = false;
    bool text_limit_reported = false;
    while (file.available()) {
        const int value = file.read();
        if (value < 0) break;
        if (value == '\n') {
            line[line_length] = '\0';
            parse_line(line, first_line, &entry_limit_reported, &text_limit_reported);
            if (line_overflow && !overflow_reported) {
                overflow_reported = true;
                Serial.printf("[LYRICS] lines longer than %u bytes are truncated\n",
                              static_cast<unsigned>(LYRICS_LINE_BUFFER_SIZE - 1U));
            }
            first_line = false;
            line_length = 0U;
            line_overflow = false;
            continue;
        }
        if (line_length + 1U < sizeof(line)) {
            line[line_length++] = static_cast<char>(value);
        } else {
            line_overflow = true;
        }
    }
    if (line_length > 0U || first_line) {
        line[line_length] = '\0';
        parse_line(line, first_line, &entry_limit_reported, &text_limit_reported);
    }
    file.close();

    std::sort(entries, entries + entry_count,
              [](const LyricEntry &left, const LyricEntry &right) {
                  return left.timestamp_ms != right.timestamp_ms
                             ? left.timestamp_ms < right.timestamp_ms
                             : left.order < right.order;
              });
    Serial.printf("[LYRICS] loaded=%u path=%s\n", static_cast<unsigned>(entry_count),
                  lrc_path);
    return entry_count > 0U;
}

bool lyrics_service_get_current_line(uint32_t elapsed_seconds,
                                     char *out, size_t out_capacity) {
    if (out == nullptr || out_capacity == 0U) return false;
    out[0] = '\0';
    if (text_pool == nullptr) return false;
    LockGuard lock(pdMS_TO_TICKS(5U));
    if (!lock.locked() || entry_count == 0U) return false;

    const uint64_t elapsed_ms = static_cast<uint64_t>(elapsed_seconds) * 1000ULL;
    if (elapsed_ms < entries[0].timestamp_ms) return false;
    size_t left = 0U;
    size_t right = entry_count;
    while (left + 1U < right) {
        const size_t middle = left + (right - left) / 2U;
        if (entries[middle].timestamp_ms <= elapsed_ms) left = middle;
        else right = middle;
    }
    std::strncpy(out, text_pool + entries[left].text_offset, out_capacity - 1U);
    out[out_capacity - 1U] = '\0';
    return true;
}
