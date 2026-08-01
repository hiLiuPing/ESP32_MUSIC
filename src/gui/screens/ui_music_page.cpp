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
constexpr uint8_t CONTROL_COUNT = 7;
constexpr uint8_t CONTROL_VOLUME = 0;
constexpr uint8_t CONTROL_MODE = 1;
constexpr uint8_t CONTROL_PREVIOUS = 2;
constexpr uint8_t CONTROL_PLAY = 3;
constexpr uint8_t CONTROL_NEXT = 4;
constexpr uint8_t CONTROL_PLAYLIST = 5;
constexpr uint8_t CONTROL_SETTINGS = 6;
constexpr uint8_t AUDIO_ITEM_COUNT = 6;
constexpr uint8_t AUDIO_ITEM_SLEEP_TIMER = 5;
constexpr uint8_t PLAYLIST_ROWS = 7;
constexpr uint32_t SPECTRUM_FRAME_MS = 80;
constexpr uint8_t SPECTRUM_PEAK_HOLD_FRAMES = 2;
constexpr int16_t SPECTRUM_BAR_WIDTH = 10;
constexpr int16_t SPECTRUM_BAR_GAP = 5;
constexpr int16_t SPECTRUM_BOTTOM = 86;
constexpr int16_t SPECTRUM_HEIGHT = 61;
constexpr int16_t SPECTRUM_PEAK_HEIGHT = 2;
constexpr int16_t PROGRESS_Y = 95;
constexpr int16_t TIME_Y = 105;
constexpr egui_region_t TITLE_REGION = {{12, 1}, {360, 20}};
constexpr egui_region_t SPECTRUM_REGION = {{12, 23}, {360, 65}};
constexpr egui_region_t PROGRESS_REGION = {{12, 91}, {360, 34}};
constexpr egui_region_t CONTROLS_REGION = {{0, 128}, {384, 40}};
constexpr egui_region_t PLAYLIST_HEADER_REGION = {{0, 0}, {384, 22}};
constexpr egui_region_t PLAYLIST_BODY_REGION = {{0, 22}, {384, 146}};
constexpr egui_region_t PLAYLIST_EMPTY_REGION = {{12, 62}, {360, 28}};
constexpr uint8_t PLAYLIST_GLYPH_PREFETCH_BATCH = 4U;

enum class MusicSubview : uint8_t {
    Main,
    Volume,
    Playlist,
    AudioSettingsPage,
};

GuiEguiView view;
PlayerStatus player_status = {};
MusicSubview subview = MusicSubview::Main;
bool navigation_active = false;
uint8_t selected_control = CONTROL_PLAY;
uint8_t audio_selected = 0U;
bool audio_editing = false;
AudioSettings audio_draft = {};
AudioSettings audio_saved = {};
uint16_t selected_track = 0;
uint16_t visible_track_start = 0;
uint8_t displayed_spectrum[PLAYER_SPECTRUM_BANDS] = {};
uint8_t spectrum_peaks[PLAYER_SPECTRUM_BANDS] = {};
uint8_t spectrum_peak_ticks[PLAYER_SPECTRUM_BANDS] = {};
uint32_t last_spectrum_frame_ms = 0;
uint16_t playlist_glyph_cursor = 0U;
uint16_t playlist_glyph_track_count = 0U;
bool playlist_glyph_cache_full = false;

uint8_t spectrum_pixel_height(uint8_t level) {
    if (level == 0) return 0;
    return static_cast<uint8_t>(std::max<int16_t>(
        2, static_cast<int16_t>((level * SPECTRUM_HEIGHT) / 255)));
}

bool work_intersects(egui_canvas_t *canvas, const egui_region_t &region) {
    const egui_region_t *work = egui_canvas_get_base_view_work_region(canvas);
    return work != nullptr &&
           work->location.x < region.location.x + region.size.width &&
           work->location.x + work->size.width > region.location.x &&
           work->location.y < region.location.y + region.size.height &&
           work->location.y + work->size.height > region.location.y;
}

const egui_font_t *music_font() {
    const egui_font_t *font = ui_heiti_font_get_cached(16U);
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

void draw_settings_icon(egui_canvas_t *canvas, int16_t cx, int16_t cy,
                        egui_color_t color) {
    egui_canvas_draw_circle_basic(canvas, cx, cy, 7, 3, color, EGUI_ALPHA_100);
    egui_canvas_draw_circle_fill_basic(canvas, cx, cy, 2,
                                       color, EGUI_ALPHA_100);
    constexpr int8_t offsets[8][2] = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    };
    for (const auto &offset : offsets) {
        egui_canvas_draw_line(canvas,
                              cx + offset[0] * 6, cy + offset[1] * 6,
                              cx + offset[0] * 11, cy + offset[1] * 11,
                              3, color, EGUI_ALPHA_100);
    }
}

void draw_control(egui_canvas_t *canvas, uint8_t index) {
    const int16_t cx = static_cast<int16_t>(27 + index * 55);
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
        case CONTROL_SETTINGS: draw_settings_icon(canvas, cx, cy, color); break;
        default: break;
    }
}

const char *empty_state_message() {
    if (player_status.state == PlayerState::NoSd) return "SD CARD NOT FOUND";
    if (player_status.state == PlayerState::Empty) return "NO AUDIO IN /music";
    if (player_status.error != PlayerError::None) return "PLAYER ERROR";
    return nullptr;
}

void draw_main(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    if (work_intersects(canvas, TITLE_REGION)) {
        const char *title = player_status.file_name[0] == '\0'
                                ? "MUSIC"
                                : player_status.file_name;
        draw_centered_text(canvas, music_font(), title, 12, 1, 360, 20);
    }

    if (work_intersects(canvas, SPECTRUM_REGION)) {
        const char *message = empty_state_message();
        if (message != nullptr) {
            draw_centered_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                               message, 12, 49, 360, 22);
        } else {
            for (size_t index = 0; index < PLAYER_SPECTRUM_BANDS; ++index) {
                const int16_t height = spectrum_pixel_height(displayed_spectrum[index]);
                const int16_t x = static_cast<int16_t>(
                    14 + index * (SPECTRUM_BAR_WIDTH + SPECTRUM_BAR_GAP));
                if (height > 0) {
                    egui_canvas_draw_rectangle_fill(canvas, x, SPECTRUM_BOTTOM - height,
                                                    SPECTRUM_BAR_WIDTH, height,
                                                    EGUI_COLOR_BLACK, EGUI_ALPHA_100);
                }
                if (spectrum_peaks[index] > 0) {
                    egui_canvas_draw_rectangle_fill(
                        canvas, x, SPECTRUM_BOTTOM - spectrum_peaks[index],
                        SPECTRUM_BAR_WIDTH, SPECTRUM_PEAK_HEIGHT,
                        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
                }
            }
        }
    }

    if (work_intersects(canvas, PROGRESS_REGION)) {
        constexpr int16_t progress_x = 16;
        constexpr int16_t progress_width = 352;
        egui_canvas_draw_round_rectangle(canvas, progress_x, PROGRESS_Y,
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
            egui_canvas_draw_round_rectangle_fill(canvas, progress_x + 1, PROGRESS_Y + 1,
                                                  static_cast<int16_t>(progress), 4, 2,
                                                  EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
        const int16_t thumb_x = static_cast<int16_t>(progress_x + 1 + progress);
        egui_canvas_draw_circle_fill_basic(canvas, thumb_x, PROGRESS_Y + 3, 4,
                                           EGUI_COLOR_BLACK, EGUI_ALPHA_100);

        char elapsed[12] = {};
        char duration[12] = {};
        gui_format_time(player_status.elapsed_seconds, elapsed, sizeof(elapsed));
        gui_format_time(player_status.duration_seconds, duration, sizeof(duration));
        egui_region_t elapsed_region = {{16, TIME_Y}, {100, 18}};
        egui_canvas_draw_text_in_rect(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                                      elapsed, &elapsed_region,
                                      EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER,
                                      EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        draw_right_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                        duration, 268, TIME_Y, 100, 18);
        if (player_status.state == PlayerState::Playing &&
            player_status.sleep_timer_remaining_seconds > 0U) {
            char sleep_time[12] = {};
            char sleep_label[24] = {};
            gui_format_time(player_status.sleep_timer_remaining_seconds,
                            sleep_time, sizeof(sleep_time));
            std::snprintf(sleep_label, sizeof(sleep_label), "SLEEP %s", sleep_time);
            draw_centered_text(canvas,
                               EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                               sleep_label, 128, TIME_Y, 128, 18);
        }
    }

    if (work_intersects(canvas, CONTROLS_REGION)) {
        for (uint8_t index = 0; index < CONTROL_COUNT; ++index) {
            draw_control(canvas, index);
        }
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
    if (work_intersects(canvas, PLAYLIST_HEADER_REGION)) {
        gui_draw_header(canvas, "PLAYLIST");
        char count[24] = {};
        std::snprintf(count, sizeof(count), "%u/%u",
                      player_status.track_count == 0 ? 0 : selected_track + 1,
                      player_status.track_count);
        draw_right_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                        count, 280, 1, 94, 20);
    }

    if (player_status.track_count == 0) {
        if (work_intersects(canvas, PLAYLIST_EMPTY_REGION)) {
            draw_centered_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                               empty_state_message() == nullptr
                                   ? "NO TRACKS"
                                   : empty_state_message(),
                               12, 62, 360, 28);
        }
        return;
    }

    for (uint8_t row = 0; row < PLAYLIST_ROWS; ++row) {
        const uint16_t index = visible_track_start + row;
        if (index >= player_status.track_count) break;
        const int16_t y = static_cast<int16_t>(24 + row * 20);
        const egui_region_t row_region = {{4, y}, {376, 19}};
        if (!work_intersects(canvas, row_region)) continue;
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

const char *audio_setting_label(uint8_t index) {
    switch (index) {
        case 0: return "VOLUME";
        case 1: return "SPEAKER";
        case 2: return "BASS";
        case 3: return "TREBLE";
        case 4: return "3D SURROUND";
        case 5: return "SLEEP TIMER";
        default: return "";
    }
}

void format_audio_setting(uint8_t index, char *out, size_t size) {
    switch (index) {
        case 0: std::snprintf(out, size, "%u / %u", audio_draft.volume,
                              PLAYER_VOLUME_MAX); break;
        case 1: std::snprintf(out, size, "%s",
                              audio_draft.amplifier_enabled ? "ON" : "OFF"); break;
        case 2: std::snprintf(out, size, "%+d DB", audio_draft.bass_db); break;
        case 3: std::snprintf(out, size, "%+d DB", audio_draft.treble_db); break;
        case 4: std::snprintf(out, size, "%u / 15", audio_draft.surround_depth); break;
        case 5:
            if (audio_draft.sleep_timer_min == 0U) {
                std::snprintf(out, size, "OFF");
            } else {
                std::snprintf(out, size, "%u MIN", audio_draft.sleep_timer_min);
            }
            break;
        default: out[0] = '\0'; break;
    }
}

void draw_audio_settings(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, audio_editing ? "AUDIO EDIT" : "AUDIO SETTING");
    for (uint8_t index = 0U; index < AUDIO_ITEM_COUNT; ++index) {
        const int16_t y = static_cast<int16_t>(27 + index * 22);
        const bool focused = navigation_active && index == audio_selected;
        if (focused) {
            egui_canvas_draw_rectangle_fill(canvas, 4, y - 2, 376, 20,
                                            EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
        const egui_color_t color = focused ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                              audio_setting_label(index), 12, y, color, EGUI_ALPHA_100);
        char value[20] = {};
        format_audio_setting(index, value, sizeof(value));
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4),
                              value, 270, y, color, EGUI_ALPHA_100);
    }
    if (navigation_active) {
        gui_draw_text(canvas, 12, 153, audio_editing ? "MIDDLE SAVE" : "MIDDLE EDIT");
    }
}

void draw(egui_canvas_t *canvas) {
    switch (subview) {
        case MusicSubview::Main: draw_main(canvas); break;
        case MusicSubview::Volume: draw_volume(canvas); break;
        case MusicSubview::Playlist: draw_playlist(canvas); break;
        case MusicSubview::AudioSettingsPage: draw_audio_settings(canvas); break;
    }
}

void init() {
    if (player_status.version == 0) {
        std::memset(&player_status, 0, sizeof(player_status));
        player_status.state = PlayerState::Initializing;
        player_status.volume = PLAYER_DEFAULT_VOLUME;
        player_status.playback_mode = PlaybackMode::Shuffle;
        player_status.sleep_timer_min = AUDIO_SLEEP_TIMER_DEFAULT_MIN;
    }
    gui_egui_view_init(&view, egui_port_core(), draw);
}

void enter() {
    navigation_active = false;
    subview = MusicSubview::Main;
    selected_control = CONTROL_PLAY;
    audio_selected = 0U;
    audio_editing = false;
    selected_track = player_status.track_count == 0 ? 0 : player_status.track_index;
    visible_track_start = 0;
    clamp_playlist_window();
    if (player_status.state == PlayerState::Playing) {
        std::memcpy(displayed_spectrum, player_status.spectrum,
                    sizeof(displayed_spectrum));
    } else {
        std::memset(displayed_spectrum, 0, sizeof(displayed_spectrum));
    }
    for (size_t index = 0; index < PLAYER_SPECTRUM_BANDS; ++index) {
        spectrum_peaks[index] = spectrum_pixel_height(displayed_spectrum[index]);
        spectrum_peak_ticks[index] = 0;
    }
    last_spectrum_frame_ms = millis();
}

void exit() {
    navigation_active = false;
    subview = MusicSubview::Main;
    std::memset(spectrum_peaks, 0, sizeof(spectrum_peaks));
    std::memset(spectrum_peak_ticks, 0, sizeof(spectrum_peak_ticks));
}

void navigation_changed(bool active) {
    navigation_active = active;
    if (!active) {
        if (subview == MusicSubview::AudioSettingsPage && audio_editing) {
            (void)task_post_player_audio_settings(audio_saved, false);
        }
        subview = MusicSubview::Main;
        selected_control = CONTROL_PLAY;
        audio_editing = false;
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

egui_region_t playlist_row_region(uint16_t index, uint16_t window_start) {
    const int16_t row = static_cast<int16_t>(index - window_start);
    return {{4, static_cast<int16_t>(24 + row * 20)}, {376, 19}};
}

void invalidate_playlist_selection(uint16_t old_selected,
                                   uint16_t old_window_start) {
    egui_view_invalidate_region(EGUI_VIEW_OF(&view), &PLAYLIST_HEADER_REGION);
    if (old_window_start != visible_track_start) {
        egui_view_invalidate_region(EGUI_VIEW_OF(&view), &PLAYLIST_BODY_REGION);
        return;
    }
    const egui_region_t old_row =
        playlist_row_region(old_selected, old_window_start);
    const egui_region_t new_row =
        playlist_row_region(selected_track, visible_track_start);
    egui_view_invalidate_region(EGUI_VIEW_OF(&view), &old_row);
    egui_view_invalidate_region(EGUI_VIEW_OF(&view), &new_row);
}

void reset_playlist_glyph_prefetch() {
    playlist_glyph_cursor = 0U;
    playlist_glyph_track_count = player_status.track_count;
    playlist_glyph_cache_full = false;
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
        case CONTROL_SETTINGS:
            audio_saved = AudioSettings{player_status.volume, player_status.playback_mode,
                                        player_status.amplifier_enabled, player_status.bass_db,
                                        player_status.treble_db, player_status.surround_depth,
                                        player_status.sleep_timer_min};
            audio_draft = audio_saved;
            audio_selected = 0U;
            audio_editing = false;
            subview = MusicSubview::AudioSettingsPage;
            break;
        default:
            break;
    }
}

void adjust_audio_setting(int direction) {
    switch (audio_selected) {
        case 0:
            audio_draft.volume = static_cast<uint8_t>(constrain(
                static_cast<int>(audio_draft.volume) + direction,
                PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX));
            break;
        case 1:
            audio_draft.amplifier_enabled = !audio_draft.amplifier_enabled;
            break;
        case 2:
            audio_draft.bass_db = static_cast<int8_t>(constrain(
                static_cast<int>(audio_draft.bass_db) + direction, -12, 12));
            break;
        case 3:
            audio_draft.treble_db = static_cast<int8_t>(constrain(
                static_cast<int>(audio_draft.treble_db) + direction, -12, 12));
            break;
        case 4:
            audio_draft.surround_depth = static_cast<uint8_t>(constrain(
                static_cast<int>(audio_draft.surround_depth) + direction, 0, 15));
            break;
        case AUDIO_ITEM_SLEEP_TIMER:
            if (direction > 0) {
                audio_draft.sleep_timer_min =
                    audio_draft.sleep_timer_min == 0U
                        ? AUDIO_SLEEP_TIMER_MIN
                        : (audio_draft.sleep_timer_min >= AUDIO_SLEEP_TIMER_MAX
                               ? 0U
                               : static_cast<uint16_t>(audio_draft.sleep_timer_min + 30U));
            } else {
                audio_draft.sleep_timer_min =
                    audio_draft.sleep_timer_min == 0U
                        ? AUDIO_SLEEP_TIMER_MAX
                        : (audio_draft.sleep_timer_min <= AUDIO_SLEEP_TIMER_MIN
                               ? 0U
                               : static_cast<uint16_t>(audio_draft.sleep_timer_min - 30U));
            }
            break;
        default: break;
    }
    (void)task_post_player_audio_settings(audio_draft, false);
}

bool key_consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::LongPress &&
        subview != MusicSubview::Main) {
        if (subview == MusicSubview::AudioSettingsPage && audio_editing) {
            (void)task_post_player_audio_settings(audio_saved, false);
            audio_draft = audio_saved;
            audio_editing = false;
        }
        selected_control = subview == MusicSubview::Volume
                               ? CONTROL_VOLUME
                               : (subview == MusicSubview::Playlist
                                      ? CONTROL_PLAYLIST
                                      : CONTROL_SETTINGS);
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
        } else if (subview == MusicSubview::AudioSettingsPage) {
            if (audio_editing) {
                adjust_audio_setting(direction);
            } else {
                audio_selected = static_cast<uint8_t>(
                    (audio_selected + AUDIO_ITEM_COUNT + direction) % AUDIO_ITEM_COUNT);
            }
        } else {
            const uint16_t old_selected = selected_track;
            const uint16_t old_window_start = visible_track_start;
            move_playlist(direction);
            invalidate_playlist_selection(old_selected, old_window_start);
            return false;
        }
        return true;
    }

    if (event.id != KeyId::Middle) {
        return false;
    }
    if (subview == MusicSubview::Main) {
        execute_control();
    } else if (subview == MusicSubview::AudioSettingsPage) {
        if (audio_editing) {
            (void)task_post_player_audio_settings(
                audio_draft, true, audio_selected == AUDIO_ITEM_SLEEP_TIMER);
            audio_saved = audio_draft;
            audio_editing = false;
        } else if (audio_selected == 1U) {
            audio_draft.amplifier_enabled = !audio_draft.amplifier_enabled;
            (void)task_post_player_audio_settings(audio_draft, true, false);
            audio_saved = audio_draft;
            audio_editing = false;
            Serial.printf("[MUSIC] speaker=%s\n",
                          audio_draft.amplifier_enabled ? "ON" : "OFF");
        } else {
            audio_editing = true;
        }
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
        const uint8_t old_peak = spectrum_peaks[index];
        const uint8_t height = spectrum_pixel_height(displayed_spectrum[index]);
        if (height >= spectrum_peaks[index]) {
            spectrum_peaks[index] = height;
            spectrum_peak_ticks[index] = 0;
        } else if (spectrum_peaks[index] > 0) {
            ++spectrum_peak_ticks[index];
            if (spectrum_peak_ticks[index] >= SPECTRUM_PEAK_HOLD_FRAMES) {
                spectrum_peak_ticks[index] = 0;
                --spectrum_peaks[index];
            }
        }
        changed = changed || displayed_spectrum[index] != old_value ||
                  spectrum_peaks[index] != old_peak;
    }
    if (changed && subview == MusicSubview::Main) {
        egui_view_invalidate_region(EGUI_VIEW_OF(&view), &SPECTRUM_REGION);
    }
    return false;
}

void cache_playlist_glyphs() {
    if (playlist_glyph_track_count != player_status.track_count) {
        reset_playlist_glyph_prefetch();
    }
    if (!playlist_glyph_cache_full &&
        player_status.state != PlayerState::Playing &&
        playlist_glyph_cursor < playlist_glyph_track_count) {
        for (uint8_t batch = 0U;
             batch < PLAYLIST_GLYPH_PREFETCH_BATCH &&
             playlist_glyph_cursor < playlist_glyph_track_count;
             ++batch) {
            char name[PLAYER_NAME_LENGTH] = {};
            if (music_library_get(playlist_glyph_cursor, nullptr, 0U, name,
                                  sizeof(name)) &&
                ui_heiti_font_cache_text(16U, name)) {
                ++playlist_glyph_cursor;
                continue;
            }
            playlist_glyph_cache_full = true;
            Serial.printf("[MUSIC_UI] glyph prefetch stopped at %u/%u glyphs=%u\n",
                          playlist_glyph_cursor, playlist_glyph_track_count,
                          static_cast<unsigned>(ui_heiti_font_poetry_cache_glyphs()));
            break;
        }
        if (!playlist_glyph_cache_full &&
            playlist_glyph_cursor == playlist_glyph_track_count) {
            Serial.printf("[MUSIC_UI] glyph prefetch complete tracks=%u glyphs=%u reads=%lu\n",
                          playlist_glyph_track_count,
                          static_cast<unsigned>(ui_heiti_font_poetry_cache_glyphs()),
                          static_cast<unsigned long>(ui_heiti_font_storage_read_count()));
        }
    }
}

bool update_status(const PlayerStatus &status) {
    if (status.version == player_status.version) {
        return false;
    }

    const PlayerStatus previous = player_status;
    player_status = status;
    if (previous.track_count != status.track_count) {
        reset_playlist_glyph_prefetch();
    }
    clamp_playlist_window();

    const bool playback_context_changed =
        previous.state != status.state ||
        previous.error != status.error;

    switch (subview) {
        case MusicSubview::Main:
            if (playback_context_changed ||
                previous.track_index != status.track_index ||
                previous.track_count != status.track_count ||
                previous.volume != status.volume ||
                previous.playback_mode != status.playback_mode ||
                std::strcmp(previous.file_name, status.file_name) != 0) {
                return true;
            }
            if (previous.elapsed_seconds != status.elapsed_seconds ||
                previous.duration_seconds != status.duration_seconds ||
                previous.sleep_timer_remaining_seconds !=
                    status.sleep_timer_remaining_seconds) {
                egui_view_invalidate_region(EGUI_VIEW_OF(&view), &PROGRESS_REGION);
            }
            return false;
        case MusicSubview::Volume:
            return playback_context_changed || previous.volume != status.volume;
        case MusicSubview::Playlist:
            return playback_context_changed ||
                   previous.track_index != status.track_index ||
                   previous.track_count != status.track_count;
        case MusicSubview::AudioSettingsPage:
            return playback_context_changed || previous.volume != status.volume ||
                   previous.amplifier_enabled != status.amplifier_enabled ||
                   previous.bass_db != status.bass_db ||
                   previous.treble_db != status.treble_db ||
                   previous.surround_depth != status.surround_depth ||
                   previous.sleep_timer_min != status.sleep_timer_min;
    }
    return false;
}

GuiPageDescriptor descriptor = {
    UiPage::Music, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&view), "music", true, false, navigation_changed,
};
}

GuiPageDescriptor &ui_music_page_descriptor() {
    return descriptor;
}

void ui_music_page_cache_service() {
    cache_playlist_glyphs();
}
