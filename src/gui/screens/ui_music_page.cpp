#include "gui/screens/ui_music_page.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/music_library.h"
#include "app/player_app.h"
#include "bsp/bsp_display.h"
#include "gui/gui_common.h"
#include "task/task_system.h"

namespace {
PlayerStatus player_status = {};
uint16_t selected_track = 0;

void init() {
    std::memset(&player_status, 0, sizeof(player_status));
    player_status.state = PlayerState::Initializing;
    selected_track = 0;
}

void enter() {}
void exit() {}

bool key_consume(const KeyEvent &event) {
    if ((event.id != KeyId::Middle) || (event.gesture != KeyGesture::Click) ||
        (player_status.track_count == 0)) {
        return false;
    }

    if ((selected_track == player_status.track_index) &&
        ((player_status.state == PlayerState::Playing) ||
         (player_status.state == PlayerState::Paused))) {
        (void)task_post_player_command(PlayerCommandType::Toggle);
    } else {
        (void)task_post_player_command(PlayerCommandType::PlaySelected,
                                       static_cast<int16_t>(selected_track));
    }
    return true;
}

bool service() {
    return false;
}

bool update_status(const PlayerStatus &status) {
    if (status.version == player_status.version) {
        return false;
    }
    player_status = status;
    if ((player_status.track_count > 0) && (selected_track >= player_status.track_count)) {
        selected_track = player_status.track_count - 1;
    }
    return true;
}

void render_track_list() {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    const uint16_t count = player_status.track_count;
    if (count == 0) {
        gui_draw_text(12, 58, player_status.state == PlayerState::NoSd
                                  ? "SD CARD NOT FOUND" : "NO MP3 FILES");
        return;
    }

    constexpr uint16_t rows = 7;
    const uint16_t first = (selected_track >= rows) ? selected_track - rows + 1 : 0;
    for (uint16_t row = 0; row < rows; ++row) {
        const uint16_t index = first + row;
        if (index >= count) {
            break;
        }
        char name[PLAYER_NAME_LENGTH] = {};
        char shown[PLAYER_NAME_LENGTH] = {};
        (void)music_library_get(index, nullptr, 0, name, sizeof(name));
        gui_copy_utf8_fitted(name, shown, sizeof(shown), 205);
        const int16_t y_top = 26 + row * 19;
        if (index == selected_track) {
            display.drawFilledRectangle(3, y_top, 235, y_top + 17, ST7305_COLOR_BLACK);
            gui_draw_text(7, y_top + 14, shown, true);
        } else {
            gui_draw_text(7, y_top + 14, shown);
        }
    }
}

void render_status() {
    ST7305_2p9_BW_DisplayDriver &display = bsp_display();
    char buffer[64];
    gui_draw_text(250, 40, player_state_name(player_status.state));

    char active_name[PLAYER_NAME_LENGTH] = {};
    gui_copy_utf8_fitted(player_status.file_name, active_name, sizeof(active_name), 125);
    gui_draw_text(250, 59, active_name[0] == '\0' ? "--" : active_name);
    std::snprintf(buffer, sizeof(buffer), "%u/%u",
                  player_status.track_count == 0 ? 0 : player_status.track_index + 1,
                  player_status.track_count);
    gui_draw_text(250, 78, buffer);

    char elapsed[12];
    char duration[12];
    gui_format_time(player_status.elapsed_seconds, elapsed, sizeof(elapsed));
    gui_format_time(player_status.duration_seconds, duration, sizeof(duration));
    std::snprintf(buffer, sizeof(buffer), "%s / %s", elapsed, duration);
    gui_draw_text(250, 96, buffer);

    display.drawRectangle(250, 105, 374, 116, ST7305_COLOR_BLACK);
    uint32_t progress = 0;
    if (player_status.duration_seconds > 0) {
        progress = std::min<uint32_t>(120,
            (player_status.elapsed_seconds * 120U) / player_status.duration_seconds);
    }
    if (progress > 0) {
        display.drawFilledRectangle(252, 107, 252 + progress, 114, ST7305_COLOR_BLACK);
    }
    std::snprintf(buffer, sizeof(buffer), "VOL %u/%u",
                  player_status.volume, PLAYER_VOLUME_MAX);
    gui_draw_text(250, 136, buffer);
    std::snprintf(buffer, sizeof(buffer), "ERR %s", player_error_name(player_status.error));
    gui_draw_text(250, 154, buffer);
}

void render() {
    gui_draw_header("MUSIC");
    bsp_display().drawFastVLine(239, 22, 146, ST7305_COLOR_BLACK);
    render_track_list();
    render_status();
}

GuiPageDescriptor descriptor = {
    UiPage::Music, init, enter, exit, key_consume, service, update_status, render,
    "music", true, false,
};
}

GuiPageDescriptor &ui_music_page_descriptor() {
    return descriptor;
}
