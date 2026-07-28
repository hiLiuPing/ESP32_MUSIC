#include "gui/screens/ui_music_page.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/music_library.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_heiti_font.h"
#include "task/task_system.h"

namespace {
constexpr uint8_t CONTROL_COUNT = 6;
constexpr uint8_t CONTROL_VOLUME = 0;
constexpr uint8_t CONTROL_MODE = 1;
constexpr uint8_t CONTROL_PREVIOUS = 2;
constexpr uint8_t CONTROL_PLAY = 3;
constexpr uint8_t CONTROL_NEXT = 4;
constexpr uint8_t CONTROL_PLAYLIST = 5;
constexpr uint8_t PLAYLIST_ROWS = 7;
constexpr uint32_t SPECTRUM_FRAME_MS = 80;

enum class MusicSubview : uint8_t {
    Main,
    Volume,
    Playlist,
};

GuiEguiView view;
PlayerStatus player_status = {};
MusicSubview subview = MusicSubview::Main;
bool navigation_active = false;
uint8_t selected_control = CONTROL_PLAY;
uint16_t selected_track = 0;
uint16_t visible_track_start = 0;
uint8_t displayed_spectrum[PLAYER_SPECTRUM_BANDS] = {};
uint32_t last_spectrum_frame_ms = 0;

const egui_font_t *music_font() {
    const egui_font_t *font = ui_heiti_font_get(16U);
    return font != nullptr ? font
                           : reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT);
}

void draw_centered_text(egui_canvas_t *canvas, const egui_font_t *font,
                        const char *text, int16_t x, int16_t y,
                        int16_t width, int16_t height,
                        egui_color_t color = EGUI_COLOR_BLACK) {
    egui_region_t region = {{x, y}, {width, height}};
    egui_canvas_draw_text_in_rect(canvas, font, text, &region,
                                  EGUI_ALIGN_CENTER | EGUI_ALIGN_VCENTER,
                                  color, EGUI_ALPHA_100);
}

void draw_right_text(egui_canvas_t *canvas, const egui_font_t *font,
                     const char *text, int16_t x, int16_t y,
                     int16_t width, int16_t height,
                     egui_color_t color = EGUI_COLOR_BLACK) {
    egui_region_t region = {{x, y}, {width, height}};
    egui_canvas_draw_text_in_rect(canvas, font, text, &region,
                                  EGUI_ALIGN_RIGHT | EGUI_ALIGN_VCENTER,
                                  color, EGUI_ALPHA_100);
}

void draw_speaker_icon(egui_canvas_t *canvas, int16_t cx, int16_t cy,
                       egui_color_t color) {
    egui_canvas_draw_rectangle_fill(canvas, cx - 9, cy - 4, 5, 8,
                                    color, EGUI_ALPHA_100);
    egui_canvas_draw_triangle_fill(canvas, cx - 4, cy - 5, cx + 2, cy - 10,
                                   cx + 2, cy + 10, color, EGUI_ALPHA_100);
    if (player_status.volume == 0) {
        egui_canvas_draw_line(canvas, cx + 5, cy - 5, cx + 12, cy + 5, 2,
                              color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, cx + 12, cy - 5, cx + 5, cy + 5, 2,
                              color, EGUI_ALPHA_100);
    } else {
        egui_canvas_draw_line(canvas, cx + 6, cy - 6, cx + 10, cy - 2, 2,
                              color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, cx + 10, cy - 2, cx + 10, cy + 2, 2,
                              color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, cx + 10, cy + 2, cx + 6, cy + 6, 2,
                              color, EGUI_ALPHA_100);
    }
}

void draw_repeat_icon(egui_canvas_t *canvas, int16_t cx, int16_t cy,
                      egui_color_t color) {
    if (player_status.playback_mode == PlaybackMode::Shuffle) {
        egui_canvas_draw_line(canvas, cx - 10, cy - 7, cx + 10, cy + 7, 2,
                              color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, cx - 10, cy + 7, cx - 3, cy + 2, 2,
                              color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, cx - 3, cy + 2, cx + 10, cy - 7, 2,
                              color, EGUI_ALPHA_100);
        egui_canvas_draw_triangle_fill(canvas, cx + 10, cy - 7, cx + 5, cy - 8,
                                       cx + 9, cy - 2, color, EGUI_ALPHA_100);
        egui_canvas_draw_triangle_fill(canvas, cx + 10, cy + 7, cx + 5, cy + 8,
                                       cx + 9, cy + 2, color, EGUI_ALPHA_100);
        return;
    }

    egui_canvas_draw_line(canvas, cx - 9, cy - 6, cx + 8, cy - 6, 2,
                          color, EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, cx + 9, cy - 5, cx + 9, cy + 2, 2,
                          color, EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, cx + 8, cy + 6, cx - 9, cy + 6, 2,
                          color, EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, cx - 10, cy + 5, cx - 10, cy - 2, 2,
                          color, EGUI_ALPHA_100);
    egui_canvas_draw_triangle_fill(canvas, cx + 9, cy + 6, cx + 4, cy + 2,
                                   cx + 10, cy + 1, color, EGUI_ALPHA_100);
    egui_canvas_draw_triangle_fill(canvas, cx - 10, cy - 6, cx - 5, cy - 2,
                                   cx - 11, cy - 1, color, EGUI_ALPHA_100);
    if (player_status.playback_mode == PlaybackMode::RepeatOne) {
        egui_canvas_draw_line(canvas, cx, cy - 3, cx, cy + 3, 2,
                              color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, cx - 2, cy - 2, cx, cy - 4, 1,
                              color, EGUI_ALPHA_100);
    }
}

void draw_skip_icon(egui_canvas_t *canvas, int16_t cx, int16_t cy,
                    bool next, egui_color_t color) {
    if (next) {
        egui_canvas_draw_triangle_fill(canvas, cx - 8, cy - 9, cx + 6, cy,
                                       cx - 8, cy + 9, color, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle_fill(canvas, cx + 7, cy - 10, 3, 20,
                                        color, EGUI_ALPHA_100);
    } else {
        egui_canvas_draw_triangle_fill(canvas, cx + 8, cy - 9, cx - 6, cy,
                                       cx + 8, cy + 9, color, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle_fill(canvas, cx - 10, cy - 10, 3, 20,
                                        color, EGUI_ALPHA_100);
    }
}

void draw_play_icon(egui_canvas_t *canvas, int16_t cx, int16_t cy,
                    egui_color_t color) {
    if (player_status.state == PlayerState::Playing) {
        egui_canvas_draw_rectangle_fill(canvas, cx - 7, cy - 10, 5, 20,
                                        color, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle_fill(canvas, cx + 3, cy - 10, 5, 20,
                                        color, EGUI_ALPHA_100);
    } else {
        egui_canvas_draw_triangle_fill(canvas, cx - 7, cy - 11, cx + 10, cy,
                                       cx - 7, cy + 11, color, EGUI_ALPHA_100);
    }
}

void draw_playlist_icon(egui_canvas_t *canvas, int16_t cx, int16_t cy,
                        egui_color_t color) {
    for (int16_t row = -7; row <= 7; row += 7) {
        egui_canvas_draw_circle_fill_basic(canvas, cx - 9, cy + row, 2,
                                           color, EGUI_ALPHA_100);
        egui_canvas_draw_line(canvas, cx - 4, cy + row, cx + 10, cy + row, 2,
                              color, EGUI_ALPHA_100);
    }
}

void draw_control(egui_canvas_t *canvas, uint8_t index) {
    const int16_t cx = static_cast<int16_t>(32 + index * 64);
    constexpr int16_t cy = 148;
    const bool focused = navigation_active && selected_control == index;
    const int16_t radius = index == CONTROL_PLAY ? 19 : 16;
    if (focused) {
        egui_canvas_draw_circle_fill_basic(canvas, cx, cy, radius,
                                           EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    } else {
        egui_canvas_draw_circle_basic(canvas, cx, cy, radius, 2,
                                      EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
    const egui_color_t color = focused ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
    switch (index) {
        case CONTROL_VOLUME: draw_speaker_icon(canvas, cx, cy, color); break;
        case CONTROL_MODE: draw_repeat_icon(canvas, cx, cy, color); break;
        case CONTROL_PREVIOUS: draw_skip_icon(canvas, cx, cy, false, color); break;
        case CONTROL_PLAY: draw_play_icon(canvas, cx, cy, color); break;
        case CONTROL_NEXT: draw_skip_icon(canvas, cx, cy, true, color); break;
        case CONTROL_PLAYLIST: draw_playlist_icon(canvas, cx, cy, color); break;
        default: break;
    }
}

const char *empty_state_message() {
    if (player_status.state == PlayerState::NoSd) return "SD CARD NOT FOUND";
    if (player_status.state == PlayerState::Empty) return "NO MP3 IN /music";
    if (player_status.error != PlayerError::None) return "PLAYER ERROR";
    return nullptr;
}

void draw_main(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    const char *title = player_status.file_name[0] == '\0' ? "MUSIC" : player_status.file_name;
    draw_centered_text(canvas, music_font(), title, 12, 1, 360, 20);

    const char *message = empty_state_message();
    if (message != nullptr) {
        draw_centered_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                           message, 12, 42, 360, 22);
    } else {
        constexpr int16_t bar_width = 10;
        constexpr int16_t bar_gap = 5;
        constexpr int16_t spectrum_bottom = 79;
        constexpr int16_t spectrum_height = 53;
        for (size_t index = 0; index < PLAYER_SPECTRUM_BANDS; ++index) {
            const int16_t height = std::max<int16_t>(2,
                static_cast<int16_t>((displayed_spectrum[index] * spectrum_height) / 255));
            const int16_t x = static_cast<int16_t>(14 + index * (bar_width + bar_gap));
            egui_canvas_draw_rectangle_fill(canvas, x, spectrum_bottom - height,
                                            bar_width, height,
                                            EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
    }

    constexpr int16_t progress_x = 16;
    constexpr int16_t progress_y = 88;
    constexpr int16_t progress_width = 352;
    egui_canvas_draw_round_rectangle(canvas, progress_x, progress_y,
                                     progress_width, 6, 3, 1,
                                     EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    uint32_t progress = 0;
    if (player_status.duration_seconds > 0) {
        progress = static_cast<uint32_t>(std::min<uint64_t>(
            progress_width - 2,
            (static_cast<uint64_t>(player_status.elapsed_seconds) *
             (progress_width - 2)) / player_status.duration_seconds));
    }
    if (progress > 0) {
        egui_canvas_draw_round_rectangle_fill(canvas, progress_x + 1, progress_y + 1,
                                              static_cast<int16_t>(progress), 4, 2,
                                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
    const int16_t thumb_x = static_cast<int16_t>(progress_x + 1 + progress);
    egui_canvas_draw_circle_fill_basic(canvas, thumb_x, progress_y + 3, 4,
                                       EGUI_COLOR_BLACK, EGUI_ALPHA_100);

    char elapsed[12] = {};
    char duration[12] = {};
    gui_format_time(player_status.elapsed_seconds, elapsed, sizeof(elapsed));
    gui_format_time(player_status.duration_seconds, duration, sizeof(duration));
    egui_region_t elapsed_region = {{16, 98}, {100, 18}};
    egui_canvas_draw_text_in_rect(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                                  elapsed, &elapsed_region,
                                  EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER,
                                  EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    draw_right_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                    duration, 268, 98, 100, 18);

    for (uint8_t index = 0; index < CONTROL_COUNT; ++index) {
        draw_control(canvas, index);
    }
}

void draw_volume(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "VOLUME");
    draw_speaker_icon(canvas, 64, 68, EGUI_COLOR_BLACK);

    char value[24] = {};
    std::snprintf(value, sizeof(value), "%u / %u",
                  player_status.volume, PLAYER_VOLUME_MAX);
    draw_centered_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_30_4),
                       value, 100, 42, 230, 50);

    constexpr int16_t bar_x = 48;
    constexpr int16_t bar_y = 111;
    constexpr int16_t bar_width = 288;
    egui_canvas_draw_round_rectangle(canvas, bar_x, bar_y, bar_width, 14, 4, 2,
                                     EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    const int16_t fill_width = static_cast<int16_t>(
        (static_cast<uint32_t>(player_status.volume) * (bar_width - 4)) /
        PLAYER_VOLUME_MAX);
    if (fill_width > 0) {
        egui_canvas_draw_round_rectangle_fill(canvas, bar_x + 2, bar_y + 2,
                                              fill_width, 10, 3,
                                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
    gui_draw_text(canvas, bar_x, 137, "MIN");
    draw_right_text(canvas, reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT),
                    "MAX", bar_x, 134, bar_width, 20);
}

void clamp_playlist_window() {
    if (player_status.track_count == 0) {
        selected_track = 0;
        visible_track_start = 0;
        return;
    }
    selected_track = std::min<uint16_t>(selected_track, player_status.track_count - 1);
    if (selected_track < visible_track_start) {
        visible_track_start = selected_track;
    } else if (selected_track >= visible_track_start + PLAYLIST_ROWS) {
        visible_track_start = selected_track - PLAYLIST_ROWS + 1;
    }
    const uint16_t max_start = player_status.track_count > PLAYLIST_ROWS
                                   ? player_status.track_count - PLAYLIST_ROWS
                                   : 0;
    visible_track_start = std::min<uint16_t>(visible_track_start, max_start);
}

void draw_playlist(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "PLAYLIST");
    char count[24] = {};
    std::snprintf(count, sizeof(count), "%u/%u",
                  player_status.track_count == 0 ? 0 : selected_track + 1,
                  player_status.track_count);
    draw_right_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                    count, 280, 1, 94, 20);

    if (player_status.track_count == 0) {
        draw_centered_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                           empty_state_message() == nullptr ? "NO TRACKS" : empty_state_message(),
                           12, 62, 360, 28);
        return;
    }

    for (uint8_t row = 0; row < PLAYLIST_ROWS; ++row) {
        const uint16_t index = visible_track_start + row;
        if (index >= player_status.track_count) break;
        const int16_t y = static_cast<int16_t>(24 + row * 20);
        const bool focused = index == selected_track;
        if (focused) {
            egui_canvas_draw_rectangle_fill(canvas, 4, y, 376, 19,
                                            EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
        const egui_color_t color = focused ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
        if (index == player_status.track_index &&
            (player_status.state == PlayerState::Playing ||
             player_status.state == PlayerState::Paused)) {
            egui_canvas_draw_triangle_fill(canvas, 10, y + 4, 18, y + 9,
                                           10, y + 14, color, EGUI_ALPHA_100);
        }
        char name[PLAYER_NAME_LENGTH] = {};
        (void)music_library_get(index, nullptr, 0, name, sizeof(name));
        egui_region_t name_region = {{24, y}, {350, 19}};
        egui_canvas_draw_text_in_rect(canvas, music_font(), name, &name_region,
                                      EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER,
                                      color, EGUI_ALPHA_100);
    }
}

void draw(egui_canvas_t *canvas) {
    switch (subview) {
        case MusicSubview::Main: draw_main(canvas); break;
        case MusicSubview::Volume: draw_volume(canvas); break;
        case MusicSubview::Playlist: draw_playlist(canvas); break;
    }
}

void init() {
    if (player_status.version == 0) {
        std::memset(&player_status, 0, sizeof(player_status));
        player_status.state = PlayerState::Initializing;
        player_status.volume = PLAYER_DEFAULT_VOLUME;
        player_status.playback_mode = PlaybackMode::RepeatAll;
    }
    gui_egui_view_init(&view, egui_port_core(), draw);
}

void enter() {
    navigation_active = false;
    subview = MusicSubview::Main;
    selected_control = CONTROL_PLAY;
    selected_track = player_status.track_count == 0 ? 0 : player_status.track_index;
    visible_track_start = 0;
    clamp_playlist_window();
    std::memcpy(displayed_spectrum, player_status.spectrum,
                sizeof(displayed_spectrum));
    last_spectrum_frame_ms = millis();
}

void exit() {
    navigation_active = false;
    subview = MusicSubview::Main;
}

void navigation_changed(bool active) {
    navigation_active = active;
    if (!active) {
        subview = MusicSubview::Main;
        selected_control = CONTROL_PLAY;
    }
}

void move_playlist(int direction) {
    if (player_status.track_count == 0) return;
    if (direction < 0) {
        selected_track = selected_track == 0
                             ? player_status.track_count - 1
                             : selected_track - 1;
    } else {
        selected_track = selected_track + 1 >= player_status.track_count
                             ? 0
                             : selected_track + 1;
    }
    clamp_playlist_window();
}

void execute_control() {
    switch (selected_control) {
        case CONTROL_VOLUME:
            subview = MusicSubview::Volume;
            break;
        case CONTROL_MODE:
            (void)task_post_player_command(PlayerCommandType::CyclePlaybackMode);
            break;
        case CONTROL_PREVIOUS:
            if (player_status.track_count > 0) {
                (void)task_post_player_command(PlayerCommandType::Previous);
            }
            break;
        case CONTROL_PLAY:
            if (player_status.track_count > 0) {
                (void)task_post_player_command(PlayerCommandType::Toggle);
            }
            break;
        case CONTROL_NEXT:
            if (player_status.track_count > 0) {
                (void)task_post_player_command(PlayerCommandType::Next);
            }
            break;
        case CONTROL_PLAYLIST:
            selected_track = player_status.track_count == 0
                                 ? 0
                                 : player_status.track_index;
            visible_track_start = 0;
            clamp_playlist_window();
            subview = MusicSubview::Playlist;
            break;
        default:
            break;
    }
}

bool key_consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::LongPress &&
        subview != MusicSubview::Main) {
        selected_control = subview == MusicSubview::Volume
                               ? CONTROL_VOLUME
                               : CONTROL_PLAYLIST;
        subview = MusicSubview::Main;
        return true;
    }
    if (event.gesture != KeyGesture::Click) {
        return false;
    }

    if (event.id == KeyId::Left || event.id == KeyId::Right) {
        const int direction = event.id == KeyId::Left ? -1 : 1;
        if (subview == MusicSubview::Main) {
            selected_control = static_cast<uint8_t>(
                (selected_control + CONTROL_COUNT + direction) % CONTROL_COUNT);
        } else if (subview == MusicSubview::Volume) {
            (void)task_post_player_command(PlayerCommandType::ChangeVolume,
                                           static_cast<int16_t>(direction));
        } else {
            move_playlist(direction);
        }
        return true;
    }

    if (event.id != KeyId::Middle) {
        return false;
    }
    if (subview == MusicSubview::Main) {
        execute_control();
    } else if (subview == MusicSubview::Playlist && player_status.track_count > 0) {
        (void)task_post_player_command(PlayerCommandType::PlaySelected,
                                       static_cast<int16_t>(selected_track));
    }
    return true;
}

bool service() {
    const uint32_t now = millis();
    if (now - last_spectrum_frame_ms < SPECTRUM_FRAME_MS) {
        return false;
    }
    last_spectrum_frame_ms = now;
    bool changed = false;
    for (size_t index = 0; index < PLAYER_SPECTRUM_BANDS; ++index) {
        const uint8_t target = player_status.state == PlayerState::Playing
                                   ? player_status.spectrum[index]
                                   : 0;
        const uint8_t old_value = displayed_spectrum[index];
        if (old_value < target) {
            displayed_spectrum[index] = target;
        } else if (old_value > target) {
            displayed_spectrum[index] = old_value > 24
                                            ? old_value - 24
                                            : 0;
        }
        changed = changed || displayed_spectrum[index] != old_value;
    }
    return changed && subview == MusicSubview::Main;
}

bool update_status(const PlayerStatus &status) {
    if (status.version == player_status.version) {
        return false;
    }
    player_status = status;
    clamp_playlist_window();
    return true;
}

GuiPageDescriptor descriptor = {
    UiPage::Music, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&view), "music", true, false, navigation_changed,
};
}

GuiPageDescriptor &ui_music_page_descriptor() {
    return descriptor;
}
