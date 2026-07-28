#include "gui/screens/ui_music_page.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/music_library.h"
#include "app/player_app.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_heiti_font.h"
#include "task/task_system.h"

namespace {
constexpr int16_t PLAYER_PANEL_WIDTH = 150;
constexpr int16_t TRACK_LIST_X = PLAYER_PANEL_WIDTH + 4;
constexpr int16_t TRACK_LIST_Y = 24;
constexpr int16_t TRACK_LIST_WIDTH = EGUI_CONFIG_SCREEN_WIDTH - TRACK_LIST_X;
constexpr int16_t TRACK_LIST_HEIGHT = EGUI_CONFIG_SCREEN_HEIGHT - TRACK_LIST_Y;
constexpr int16_t TRACK_ROW_HEIGHT = 17;
constexpr uint16_t VISIBLE_TRACK_ROWS = 6;

constexpr int16_t CONTROL_X = 8;
constexpr int16_t CONTROL_Y = 138;
constexpr int16_t CONTROL_WIDTH = 40;
constexpr int16_t CONTROL_HEIGHT = 26;
constexpr int16_t CONTROL_GAP = 7;
constexpr uint8_t CONTROL_COUNT = 3;
constexpr uint8_t PLAY_PAUSE_CONTROL = 1;

enum class FocusZone : uint8_t {
    Player,
    TrackList,
};

egui_view_group_t page_root;
GuiEguiView background_view;
GuiEguiView focus_overlay_view;
egui_view_button_t control_buttons[CONTROL_COUNT];
egui_view_list_t track_list;

PlayerStatus player_status = {};
FocusZone focus_zone = FocusZone::TrackList;
uint8_t selected_control = PLAY_PAUSE_CONTROL;
uint16_t selected_track = 0;
uint16_t visible_track_start = 0;
uint16_t rendered_track_count = UINT16_MAX;
bool view_initialized = false;
bool focus_pending = false;
char visible_track_names[VISIBLE_TRACK_ROWS][PLAYER_NAME_LENGTH] = {};

egui_background_color_param_t row_background_normal = {};
egui_background_params_t row_background_params = {};
egui_background_color_t row_background = {};

const egui_font_t *music_text_font() {
    const egui_font_t *font = ui_heiti_font_get(16U);
    return font != nullptr ? font
                           : reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT);
}

bool is_active_playback_state(PlayerState state) {
    return state == PlayerState::Playing || state == PlayerState::Paused;
}

void draw_player_status(egui_canvas_t *canvas) {
    char buffer[64] = {};
    const egui_font_t *font = music_text_font();

    gui_draw_text(canvas, 8, 27, player_state_name(player_status.state));

    egui_region_t name_region = {{8, 45}, {PLAYER_PANEL_WIDTH - 16, 18}};
    egui_canvas_draw_text_in_rect(canvas, font,
                                  player_status.file_name[0] == '\0'
                                      ? "--"
                                      : player_status.file_name,
                                  &name_region, EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER,
                                  EGUI_COLOR_BLACK, EGUI_ALPHA_100);

    std::snprintf(buffer, sizeof(buffer), "%u/%u",
                  player_status.track_count == 0 ? 0 : player_status.track_index + 1,
                  player_status.track_count);
    gui_draw_text(canvas, 8, 68, buffer);

    char elapsed[12] = {};
    char duration[12] = {};
    gui_format_time(player_status.elapsed_seconds, elapsed, sizeof(elapsed));
    gui_format_time(player_status.duration_seconds, duration, sizeof(duration));
    std::snprintf(buffer, sizeof(buffer), "%s / %s", elapsed, duration);
    gui_draw_text(canvas, 8, 86, buffer);

    constexpr uint32_t progress_width = PLAYER_PANEL_WIDTH - 20;
    egui_canvas_draw_rectangle(canvas, 8, 105, progress_width + 4, 9, 1,
                               EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    uint32_t progress = 0;
    if (player_status.duration_seconds > 0) {
        progress = static_cast<uint32_t>(std::min<uint64_t>(
            progress_width,
            (static_cast<uint64_t>(player_status.elapsed_seconds) * progress_width) /
                player_status.duration_seconds));
    }
    if (progress > 0) {
        egui_canvas_draw_rectangle_fill(canvas, 10, 107,
                                        static_cast<int16_t>(progress), 5,
                                        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }

    if (player_status.error == PlayerError::None) {
        std::snprintf(buffer, sizeof(buffer), "VOL %u/%u",
                      player_status.volume, PLAYER_VOLUME_MAX);
    } else {
        std::snprintf(buffer, sizeof(buffer), "ERR %s",
                      player_error_name(player_status.error));
    }
    egui_region_t status_region = {{8, 117}, {PLAYER_PANEL_WIDTH - 16, 18}};
    egui_canvas_draw_text_in_rect(
        canvas, reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT),
        buffer, &status_region, EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER,
        EGUI_COLOR_BLACK, EGUI_ALPHA_100);
}

void draw_background(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "MUSIC");

    char count_text[24] = {};
    std::snprintf(count_text, sizeof(count_text), "TRACKS %u",
                  player_status.track_count);
    gui_draw_text(canvas, 286, 4, count_text);

    egui_canvas_draw_line(canvas, PLAYER_PANEL_WIDTH, 22, PLAYER_PANEL_WIDTH,
                          EGUI_CONFIG_SCREEN_HEIGHT - 1, 1,
                          EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    draw_player_status(canvas);

    if (player_status.track_count == 0) {
        const char *message = player_status.state == PlayerState::NoSd
                                  ? "SD CARD NOT FOUND"
                                  : "NO MP3 IN /music";
        gui_draw_text(canvas, TRACK_LIST_X + 10, 70, message);
    }
}

void draw_focus_overlay(egui_canvas_t *canvas) {
    egui_view_t *focused = nullptr;
    if (focus_zone == FocusZone::Player) {
        focused = EGUI_VIEW_OF(&control_buttons[selected_control]);
    } else if (player_status.track_count > 0 &&
               selected_track >= visible_track_start) {
        const uint16_t row = selected_track - visible_track_start;
        if (row < egui_view_list_get_item_count(EGUI_VIEW_OF(&track_list))) {
            focused = EGUI_VIEW_OF(&track_list.items[row]);
        }
    }

    if (focused == nullptr || focused->region_screen.size.width <= 0 ||
        focused->region_screen.size.height <= 0) {
        return;
    }

    const egui_region_t &region = focused->region_screen;
    const int16_t inset = focus_zone == FocusZone::Player ? 2 : 1;
    egui_canvas_draw_rectangle(canvas,
                               region.location.x - inset,
                               region.location.y - inset,
                               region.size.width + inset * 2,
                               region.size.height + inset * 2,
                               2, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
}

void update_play_pause_icon() {
    if (!view_initialized) {
        return;
    }
    egui_view_button_set_icon(
        EGUI_VIEW_OF(&control_buttons[PLAY_PAUSE_CONTROL]),
        player_status.state == PlayerState::Playing
            ? EGUI_ICON_MS_PAUSE
            : EGUI_ICON_MS_PLAY_ARROW);
}

void clamp_track_window() {
    const uint16_t count = player_status.track_count;
    if (count == 0) {
        selected_track = 0;
        visible_track_start = 0;
        return;
    }

    if (selected_track >= count) {
        selected_track = count - 1;
    }
    if (selected_track < visible_track_start) {
        visible_track_start = selected_track;
    } else if (selected_track >= visible_track_start + VISIBLE_TRACK_ROWS) {
        visible_track_start = selected_track - VISIBLE_TRACK_ROWS + 1;
    }

    const uint16_t max_start = count > VISIBLE_TRACK_ROWS
                                   ? count - VISIBLE_TRACK_ROWS
                                   : 0;
    visible_track_start = std::min(visible_track_start, max_start);
}

void rebuild_track_window() {
    if (!view_initialized) {
        return;
    }

    clamp_track_window();
    egui_view_list_clear(EGUI_VIEW_OF(&track_list));
    std::memset(visible_track_names, 0, sizeof(visible_track_names));

    const egui_font_t *font = music_text_font();
    const bool show_active_track = is_active_playback_state(player_status.state);
    for (uint16_t row = 0; row < VISIBLE_TRACK_ROWS; ++row) {
        const uint16_t track_index = visible_track_start + row;
        if (track_index >= player_status.track_count) {
            break;
        }

        (void)music_library_get(track_index, nullptr, 0,
                                visible_track_names[row],
                                sizeof(visible_track_names[row]));
        const char *icon = show_active_track &&
                                   track_index == player_status.track_index
                               ? EGUI_ICON_MS_MUSIC_NOTE
                               : nullptr;
        const int8_t added = egui_view_list_add_item_with_icon(
            EGUI_VIEW_OF(&track_list), icon, visible_track_names[row]);
        if (added < 0) {
            break;
        }

        egui_view_t *item = EGUI_VIEW_OF(&track_list.items[added]);
        egui_view_set_background(item, EGUI_BG_OF(&row_background));
        egui_view_label_set_font(item, font);
        egui_view_label_set_font_color(item, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }

    if (player_status.track_count > 0) {
        egui_view_list_set_selected_index(
            EGUI_VIEW_OF(&track_list),
            static_cast<uint8_t>(selected_track - visible_track_start));
    }
    rendered_track_count = player_status.track_count;
    egui_view_invalidate(EGUI_VIEW_OF(&focus_overlay_view));
}

void sync_track_selection() {
    const uint16_t old_start = visible_track_start;
    clamp_track_window();
    if (old_start != visible_track_start ||
        rendered_track_count != player_status.track_count) {
        rebuild_track_window();
        return;
    }
    if (player_status.track_count > 0) {
        egui_view_list_set_selected_index(
            EGUI_VIEW_OF(&track_list),
            static_cast<uint8_t>(selected_track - visible_track_start));
    }
    egui_view_invalidate(EGUI_VIEW_OF(&focus_overlay_view));
}

void request_zone_focus() {
    if (!view_initialized) {
        return;
    }
    if (focus_zone == FocusZone::Player) {
        egui_view_request_focus(EGUI_VIEW_OF(&control_buttons[selected_control]));
    } else {
        egui_view_request_focus(EGUI_VIEW_OF(&track_list));
    }
    egui_view_invalidate(EGUI_VIEW_OF(&focus_overlay_view));
}

void move_player_control(int direction) {
    const int next = (static_cast<int>(selected_control) + CONTROL_COUNT + direction) %
                     CONTROL_COUNT;
    selected_control = static_cast<uint8_t>(next);
    request_zone_focus();
}

void move_track_selection(int direction) {
    const uint16_t count = player_status.track_count;
    if (count == 0) {
        return;
    }
    if (direction < 0) {
        selected_track = selected_track == 0 ? count - 1 : selected_track - 1;
    } else {
        selected_track = selected_track + 1 >= count ? 0 : selected_track + 1;
    }
    sync_track_selection();
}

void execute_player_control() {
    if (player_status.track_count == 0) {
        return;
    }
    switch (selected_control) {
        case 0:
            (void)task_post_player_command(PlayerCommandType::Previous);
            break;
        case PLAY_PAUSE_CONTROL:
            (void)task_post_player_command(PlayerCommandType::Toggle);
            break;
        case 2:
            (void)task_post_player_command(PlayerCommandType::Next);
            break;
        default:
            break;
    }
}

void init_control_button(uint8_t index, const char *icon) {
    egui_view_t *button = EGUI_VIEW_OF(&control_buttons[index]);
    egui_view_button_init(button, egui_port_core());
    egui_view_set_position(button,
                           CONTROL_X + index * (CONTROL_WIDTH + CONTROL_GAP),
                           CONTROL_Y);
    egui_view_set_size(button, CONTROL_WIDTH, CONTROL_HEIGHT);
    egui_view_label_set_text(button, nullptr);
    egui_view_label_set_font_color(button, EGUI_COLOR_WHITE, EGUI_ALPHA_100);
    egui_view_button_set_icon(button, icon);
    egui_view_button_set_icon_font(button, EGUI_FONT_ICON_MS_20);
    egui_view_set_focus_frame_style(button, 2, 1,
                                    EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_view_group_add_child(EGUI_VIEW_OF(&page_root), button);
}

void init() {
    if (player_status.version == 0) {
        std::memset(&player_status, 0, sizeof(player_status));
        player_status.state = PlayerState::Initializing;
        player_status.volume = PLAYER_DEFAULT_VOLUME;
    }

    egui_view_group_init(EGUI_VIEW_OF(&page_root), egui_port_core());
    egui_view_set_size(EGUI_VIEW_OF(&page_root),
                       EGUI_CONFIG_SCREEN_WIDTH, EGUI_CONFIG_SCREEN_HEIGHT);

    row_background_normal.type = EGUI_BACKGROUND_COLOR_TYPE_SOLID;
    row_background_normal.alpha = EGUI_ALPHA_100;
    row_background_normal.color = EGUI_COLOR_WHITE;
    row_background_params.normal_param = &row_background_normal;
    row_background_params.pressed_param = &row_background_normal;
    row_background_params.disabled_param = &row_background_normal;
    row_background_params.focused_param = &row_background_normal;
    egui_background_color_init(EGUI_BG_OF(&row_background));
    egui_background_set_params(EGUI_BG_OF(&row_background),
                               &row_background_params);

    gui_egui_view_init(&background_view, egui_port_core(), draw_background);
    egui_view_group_add_child(EGUI_VIEW_OF(&page_root),
                              EGUI_VIEW_OF(&background_view));

    EGUI_VIEW_LIST_PARAMS_INIT(list_params, TRACK_LIST_X, TRACK_LIST_Y,
                               TRACK_LIST_WIDTH, TRACK_LIST_HEIGHT,
                               TRACK_ROW_HEIGHT);
    egui_view_list_init_with_params(EGUI_VIEW_OF(&track_list),
                                    egui_port_core(), &list_params);
    egui_view_list_set_icon_font(EGUI_VIEW_OF(&track_list), EGUI_FONT_ICON_MS_16);
    egui_view_list_set_icon_color(EGUI_VIEW_OF(&track_list), EGUI_COLOR_BLACK);
    egui_view_set_focus_frame_style(EGUI_VIEW_OF(&track_list), 0, 1,
                                    EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_view_group_add_child(EGUI_VIEW_OF(&page_root), EGUI_VIEW_OF(&track_list));

    init_control_button(0, EGUI_ICON_MS_ARROW_BACK);
    init_control_button(PLAY_PAUSE_CONTROL, EGUI_ICON_MS_PLAY_ARROW);
    init_control_button(2, EGUI_ICON_MS_ARROW_FORWARD);

    gui_egui_view_init(&focus_overlay_view, egui_port_core(), draw_focus_overlay);
    egui_view_group_add_child(EGUI_VIEW_OF(&page_root),
                              EGUI_VIEW_OF(&focus_overlay_view));

    view_initialized = true;
    clamp_track_window();
    rebuild_track_window();
    update_play_pause_icon();
}

void enter() {
    focus_zone = FocusZone::TrackList;
    focus_pending = true;
}

void exit() {
    focus_pending = false;
    egui_view_clear_focus(EGUI_VIEW_OF(&page_root));
}

bool key_consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::LongPress) {
        focus_zone = focus_zone == FocusZone::TrackList
                         ? FocusZone::Player
                         : FocusZone::TrackList;
        focus_pending = true;
        return true;
    }

    if (event.gesture != KeyGesture::Click) {
        return false;
    }

    if (event.id == KeyId::Left || event.id == KeyId::Right) {
        const int direction = event.id == KeyId::Left ? -1 : 1;
        if (focus_zone == FocusZone::Player) {
            move_player_control(direction);
        } else {
            move_track_selection(direction);
        }
        return true;
    }

    if (event.id != KeyId::Middle) {
        return false;
    }
    if (focus_zone == FocusZone::Player) {
        execute_player_control();
    } else if (player_status.track_count > 0) {
        (void)task_post_player_command(PlayerCommandType::PlaySelected,
                                       static_cast<int16_t>(selected_track));
    }
    return true;
}

bool service() {
    if (!focus_pending) {
        return false;
    }
    focus_pending = false;
    request_zone_focus();
    return true;
}

bool update_status(const PlayerStatus &status) {
    if (status.version == player_status.version) {
        return false;
    }

    const uint16_t old_track_count = player_status.track_count;
    const uint16_t old_active_track = is_active_playback_state(player_status.state)
                                          ? player_status.track_index
                                          : UINT16_MAX;
    const PlayerState old_state = player_status.state;
    player_status = status;
    clamp_track_window();

    if (view_initialized) {
        const uint16_t new_active_track = is_active_playback_state(player_status.state)
                                              ? player_status.track_index
                                              : UINT16_MAX;
        if (old_track_count != player_status.track_count ||
            old_active_track != new_active_track) {
            rebuild_track_window();
        }
        if (old_state != player_status.state) {
            update_play_pause_icon();
        }
    }
    return true;
}

GuiPageDescriptor descriptor = {
    UiPage::Music, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&page_root), "music", true, false,
};
}

GuiPageDescriptor &ui_music_page_descriptor() {
    return descriptor;
}
