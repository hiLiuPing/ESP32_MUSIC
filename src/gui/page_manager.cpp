#include "gui/page_manager.h"

#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "MyIMG.h"
#include "app/music_library.h"
#include "app/player_app.h"
#include "bsp/bsp_display.h"
#include "task/task_system.h"

namespace {
std::atomic<UiPage> current_page{UiPage::Boot};
PlayerStatus player_status = {};
uint16_t selected_track = 0;
uint32_t boot_started_ms = 0;
uint32_t boot_frame = 0;
bool dirty = true;

constexpr UiPage pages[] = {UiPage::Home, UiPage::Music, UiPage::Read, UiPage::Setting};

void draw_text(int16_t x, int16_t y, const char *text, bool inverted = false) {
    U8G2_FOR_ST73XX &font = bsp_fonts();
    font.setForegroundColor(inverted ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);
    font.drawUTF8(x, y, text);
    font.setForegroundColor(ST7305_COLOR_BLACK);
}

void copy_utf8_fitted(const char *source, char *destination, size_t capacity, int16_t width) {
    if ((destination == nullptr) || (capacity == 0)) return;
    destination[0] = '\0';
    if (source == nullptr) return;

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
        for (size_t i = 1; i < glyph_bytes; ++i) {
            const unsigned char next = static_cast<unsigned char>(source[source_pos + i]);
            if ((next == 0) || ((next & 0xC0) != 0x80)) complete = false;
        }
        if (complete && (glyph_bytes > 1)) {
            if (glyph_bytes == 2) codepoint &= 0x1F;
            else if (glyph_bytes == 3) codepoint &= 0x0F;
            else codepoint &= 0x07;
            for (size_t i = 1; i < glyph_bytes; ++i) {
                codepoint = static_cast<uint16_t>((codepoint << 6) |
                    (static_cast<unsigned char>(source[source_pos + i]) & 0x3F));
            }
        }
        const bool supported = complete &&
            (glyph_bytes <= 3) && u8g2_IsGlyph(&bsp_fonts().u8g2, codepoint);
        if (!supported) {
            destination[output_pos++] = '?';
            source_pos += complete ? glyph_bytes : 1;
        } else {
            std::memcpy(destination + output_pos, source + source_pos, glyph_bytes);
            output_pos += glyph_bytes;
            source_pos += glyph_bytes;
        }
        destination[output_pos] = '\0';
        if (bsp_fonts().getUTF8Width(destination) > width) {
            output_pos -= supported ? glyph_bytes : 1;
            destination[output_pos] = '\0';
            break;
        }
    }
}

void draw_header(const char *title) {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    display.drawFastHLine(0, 21, 384, ST7305_COLOR_BLACK);
    draw_text(8, 16, title);
}

void render_boot() {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    char progress[48];
    const uint8_t completed = ((bits & HW_EVENT_DISPLAY_READY) ? 1 : 0) +
                              ((bits & HW_EVENT_SD_READY) ? 1 : 0) +
                              ((bits & HW_EVENT_CODEC_READY) ? 1 : 0);
    std::snprintf(progress, sizeof(progress), "Hardware %u/3", completed);

    draw_text(119, 58, "ESP32-S3 MUSIC");
    display.drawRectangle(91, 77, 292, 92, ST7305_COLOR_BLACK);
    const int fill_width = static_cast<int>((boot_frame % 17) * 12);
    if (fill_width > 0) display.drawFilledRectangle(94, 80, 94 + fill_width, 89, ST7305_COLOR_BLACK);
    draw_text(145, 118, progress);
    if ((bits & HW_EVENT_INIT_DONE) && !(bits & HW_EVENT_SD_READY)) {
        draw_text(132, 143, "SD unavailable");
    }
}

void render_home() {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    draw_header("HOME");
    const int16_t xs[] = {20, 114, 208, 302};
    const unsigned char *icons[] = {tianqi, yinyue, yuedu, shezhi};
    const char *labels[] = {"HOME", "MUSIC", "READ", "SETTING"};
    for (size_t index = 0; index < 4; ++index) {
        display.drawBitmap(xs[index], 35, icons[index], 64, 64, 1);
        const int16_t text_width = bsp_fonts().getUTF8Width(labels[index]);
        draw_text(xs[index] + (64 - text_width) / 2, 125, labels[index]);
    }
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    if (!(bits & HW_EVENT_SD_READY)) draw_text(8, 158, "SD ERROR");
    else if (!(bits & HW_EVENT_CODEC_READY)) draw_text(8, 158, "CODEC ERROR");
    else draw_text(8, 158, "page music | page read | page setting");
}

void format_time(uint32_t seconds, char *buffer, size_t capacity) {
    std::snprintf(buffer, capacity, "%02lu:%02lu",
                  static_cast<unsigned long>(seconds / 60),
                  static_cast<unsigned long>(seconds % 60));
}

void render_music() {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    draw_header("MUSIC");
    display.drawFastVLine(239, 22, 146, ST7305_COLOR_BLACK);

    const uint16_t count = player_status.track_count;
    if (count == 0) {
        draw_text(12, 58, player_status.state == PlayerState::NoSd ? "SD CARD NOT FOUND" : "NO MP3 FILES");
    } else {
        if (selected_track >= count) selected_track = count - 1;
        constexpr uint16_t rows = 7;
        uint16_t first = (selected_track >= rows) ? selected_track - rows + 1 : 0;
        for (uint16_t row = 0; row < rows; ++row) {
            const uint16_t index = first + row;
            if (index >= count) break;
            char name[PLAYER_NAME_LENGTH] = {};
            char shown[PLAYER_NAME_LENGTH] = {};
            music_library_get(index, nullptr, 0, name, sizeof(name));
            copy_utf8_fitted(name, shown, sizeof(shown), 205);
            const int16_t y_top = 26 + row * 19;
            if (index == selected_track) {
                display.drawFilledRectangle(3, y_top, 235, y_top + 17, ST7305_COLOR_BLACK);
                draw_text(7, y_top + 14, shown, true);
            } else {
                draw_text(7, y_top + 14, shown);
            }
        }
    }

    char buffer[64];
    draw_text(250, 40, player_state_name(player_status.state));
    char active_name[PLAYER_NAME_LENGTH] = {};
    copy_utf8_fitted(player_status.file_name, active_name, sizeof(active_name), 125);
    draw_text(250, 59, active_name[0] == '\0' ? "--" : active_name);
    std::snprintf(buffer, sizeof(buffer), "%u/%u",
                  count == 0 ? 0 : player_status.track_index + 1, count);
    draw_text(250, 78, buffer);

    char elapsed[12];
    char duration[12];
    format_time(player_status.elapsed_seconds, elapsed, sizeof(elapsed));
    format_time(player_status.duration_seconds, duration, sizeof(duration));
    std::snprintf(buffer, sizeof(buffer), "%s / %s", elapsed, duration);
    draw_text(250, 96, buffer);

    display.drawRectangle(250, 105, 374, 116, ST7305_COLOR_BLACK);
    uint32_t progress = 0;
    if (player_status.duration_seconds > 0) {
        progress = std::min<uint32_t>(120,
                    (player_status.elapsed_seconds * 120U) / player_status.duration_seconds);
    }
    if (progress > 0) display.drawFilledRectangle(252, 107, 252 + progress, 114, ST7305_COLOR_BLACK);
    std::snprintf(buffer, sizeof(buffer), "VOL %u/%u", player_status.volume, PLAYER_VOLUME_MAX);
    draw_text(250, 136, buffer);
    std::snprintf(buffer, sizeof(buffer), "ERR %s", player_error_name(player_status.error));
    draw_text(250, 154, buffer);
    draw_text(250, 167, "up down ok");
}

void render_placeholder(const char *title, const char *message) {
    draw_header(title);
    draw_text(118, 78, message);
    draw_text(114, 104, "page home to return");
}

int page_index(UiPage page) {
    for (int index = 0; index < 4; ++index) {
        if (pages[index] == page) return index;
    }
    return 0;
}
}

void gui_page_manager_init() {
    current_page.store(UiPage::Boot);
    boot_started_ms = millis();
    boot_frame = 0;
    selected_track = 0;
    dirty = true;
}

UiPage gui_page_current() {
    return current_page.load();
}

void gui_page_goto(UiPage page) {
    if (page == UiPage::Boot) return;
    current_page.store(page);
    dirty = true;
}

void gui_page_previous() {
    const int index = page_index(current_page.load());
    gui_page_goto(pages[(index + 3) % 4]);
}

void gui_page_next() {
    const int index = page_index(current_page.load());
    gui_page_goto(pages[(index + 1) % 4]);
}

void gui_page_handle_input(const UiInputEvent &event) {
    if (current_page.load() == UiPage::Boot) {
        return;
    }
    switch (event.type) {
        case UiInputType::PageGoto: gui_page_goto(event.page); return;
        case UiInputType::PagePrevious: gui_page_previous(); return;
        case UiInputType::PageNext: gui_page_next(); return;
        case UiInputType::Up:
            if ((current_page.load() == UiPage::Music) && (selected_track > 0)) { --selected_track; dirty = true; }
            return;
        case UiInputType::Down:
            if ((current_page.load() == UiPage::Music) && (selected_track + 1 < player_status.track_count)) {
                ++selected_track; dirty = true;
            }
            return;
        case UiInputType::Ok:
            if (current_page.load() == UiPage::Music) {
                if ((selected_track == player_status.track_index) &&
                    ((player_status.state == PlayerState::Playing) ||
                     (player_status.state == PlayerState::Paused))) {
                    task_post_player_command(PlayerCommandType::Toggle);
                } else {
                    task_post_player_command(PlayerCommandType::PlaySelected, selected_track);
                }
            }
            return;
        case UiInputType::Play: task_post_player_command(PlayerCommandType::Play); return;
        case UiInputType::Pause: task_post_player_command(PlayerCommandType::Pause); return;
        case UiInputType::Toggle: task_post_player_command(PlayerCommandType::Toggle); return;
        case UiInputType::TrackPrevious: task_post_player_command(PlayerCommandType::Previous); return;
        case UiInputType::TrackNext: task_post_player_command(PlayerCommandType::Next); return;
        case UiInputType::VolumeSet: task_post_player_command(PlayerCommandType::SetVolume, event.value); return;
        case UiInputType::VolumeChange: task_post_player_command(PlayerCommandType::ChangeVolume, event.value); return;
        case UiInputType::Rescan: task_post_player_command(PlayerCommandType::Rescan); return;
        case UiInputType::Status: return;
    }
}

void gui_page_update_status(const PlayerStatus &status) {
    player_status = status;
    if ((player_status.track_count > 0) && (selected_track >= player_status.track_count)) {
        selected_track = player_status.track_count - 1;
    }
    const UiPage page = current_page.load();
    if (page == UiPage::Music || page == UiPage::Boot) dirty = true;
}

void gui_page_render(bool force) {
    const bool boot_active = current_page.load() == UiPage::Boot;
    if (boot_active) {
        const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
        if (((millis() - boot_started_ms) >= 1200U) && (bits & HW_EVENT_INIT_DONE)) {
            current_page.store(UiPage::Home);
            dirty = true;
        } else {
            ++boot_frame;
            dirty = true;
        }
    }
    if (!dirty && !force) return;

    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    display.clearDisplay();
    switch (current_page.load()) {
        case UiPage::Boot: render_boot(); break;
        case UiPage::Home: render_home(); break;
        case UiPage::Music: render_music(); break;
        case UiPage::Read: render_placeholder("READ", "Reader is reserved"); break;
        case UiPage::Setting: render_placeholder("SETTING", "Settings are reserved"); break;
    }
    display.display();
    dirty = false;
}

const char *gui_page_name(UiPage page) {
    switch (page) {
        case UiPage::Boot: return "boot";
        case UiPage::Home: return "home";
        case UiPage::Music: return "music";
        case UiPage::Read: return "read";
        case UiPage::Setting: return "setting";
    }
    return "unknown";
}
