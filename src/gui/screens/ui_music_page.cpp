#include "gui/screens/ui_music_page.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/music_library.h"
#include "app/player_app.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "task/task_system.h"

namespace {
GuiEguiView page_view;
PlayerStatus player_status = {};
uint16_t selected_track = 0;

void render_track_list(egui_canvas_t *canvas) {
    const uint16_t count = player_status.track_count;
    if (count == 0) {
        gui_draw_text(canvas, 12, 53, player_status.state == PlayerState::NoSd
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
        const int16_t y_top = static_cast<int16_t>(26 + row * 19);
        if (index == selected_track) {
            egui_canvas_draw_rectangle_fill(canvas, 3, y_top, 232, 17,
                                            EGUI_COLOR_BLACK, EGUI_ALPHA_100);
            gui_draw_text(canvas, 7, y_top + 2, shown, true);
        } else {
            gui_draw_text(canvas, 7, y_top + 2, shown);
        }
    }
}

void render_status(egui_canvas_t *canvas) {
    char buffer[64];
    gui_draw_text(canvas, 250, 30, player_state_name(player_status.state));

    char active_name[PLAYER_NAME_LENGTH] = {};
    gui_copy_utf8_fitted(player_status.file_name, active_name,
                         sizeof(active_name), 125);
    gui_draw_text(canvas, 250, 50, active_name[0] == '\0' ? "--" : active_name);
    std::snprintf(buffer, sizeof(buffer), "%u/%u",
                  player_status.track_count == 0 ? 0 : player_status.track_index + 1,
                  player_status.track_count);
    gui_draw_text(canvas, 250, 70, buffer);

    char elapsed[12];
    char duration[12];
    gui_format_time(player_status.elapsed_seconds, elapsed, sizeof(elapsed));
    gui_format_time(player_status.duration_seconds, duration, sizeof(duration));
    std::snprintf(buffer, sizeof(buffer), "%s / %s", elapsed, duration);
    gui_draw_text(canvas, 250, 90, buffer);

    egui_canvas_draw_rectangle(canvas, 250, 108, 124, 12, 1,
                               EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    uint32_t progress = 0;
    if (player_status.duration_seconds > 0) {
        progress = std::min<uint32_t>(120,
            (player_status.elapsed_seconds * 120U) / player_status.duration_seconds);
    }
    if (progress > 0) {
        egui_canvas_draw_rectangle_fill(canvas, 252, 110,
                                        static_cast<int16_t>(progress), 8,
                                        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
    std::snprintf(buffer, sizeof(buffer), "VOL %u/%u",
                  player_status.volume, PLAYER_VOLUME_MAX);
    gui_draw_text(canvas, 250, 130, buffer);
    std::snprintf(buffer, sizeof(buffer), "ERR %s",
                  player_error_name(player_status.error));
    gui_draw_text(canvas, 250, 149, buffer);
}

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "MUSIC");
    egui_canvas_draw_line(canvas, 239, 22, 239, 167, 1,
                          EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    render_track_list(canvas);
    render_status(canvas);
}

void init() {
    std::memset(&player_status, 0, sizeof(player_status));
    player_status.state = PlayerState::Initializing;
    selected_track = 0;
    gui_egui_view_init(&page_view, egui_port_core(), draw);
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

bool service() { return false; }

bool update_status(const PlayerStatus &status) {
    if (status.version == player_status.version) {
        return false;
    }
    player_status = status;
    if ((player_status.track_count > 0) &&
        (selected_track >= player_status.track_count)) {
        selected_track = player_status.track_count - 1;
    }
    return true;
}

GuiPageDescriptor descriptor = {
    UiPage::Music, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&page_view), "music", true, false,
};
}

GuiPageDescriptor &ui_music_page_descriptor() {
    return descriptor;
}
