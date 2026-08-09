#include "gui/screens/ui_music_page.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

#include "app/lyrics_service.h"
#include "app/music_library.h"
#include "gui/page_manager.h"
#include "gui/ui_heiti_font.h"
#include "lvgl_ui_common.h"
#include "task/task_system.h"

namespace {
enum class View : uint8_t { Main, Volume, Playlists, Tracks, Audio };
constexpr uint8_t kControlCount = 7;
constexpr uint8_t kAudioItemCount = 6;
lv_obj_t *root = nullptr;
lv_obj_t *title = nullptr;
lv_obj_t *track = nullptr;
lv_obj_t *lyric = nullptr;
lv_obj_t *elapsed_label = nullptr;
lv_obj_t *duration_label = nullptr;
lv_obj_t *body = nullptr;
GuiPageDescriptor page = {};
PlayerStatus state = {};
View view = View::Main;
uint8_t selected = 3;
uint16_t playlist = 0;
uint16_t track_index = 0;
bool navigation = false;
bool editing = false;
AudioSettings audio = {};
AudioSettings saved_audio = {};
uint8_t audio_selected = 0;
uint8_t bars[PLAYER_SPECTRUM_BANDS] = {};
uint8_t peaks[PLAYER_SPECTRUM_BANDS] = {};
uint32_t last_frame = 0;
uint32_t displayed_lyric_second = UINT32_MAX;

void rect(lv_layer_t *layer, int x, int y, int w, int h, lv_color_t c, lv_opa_t opa = LV_OPA_COVER, int radius = 0) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_color = c; d.bg_opa = opa; d.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1}; lv_draw_rect(layer, &d, &a);
}
void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t c, int width = 1) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.p1 = {x1, y1}; d.p2 = {x2, y2}; d.color = c; d.width = width; d.opa = LV_OPA_COVER; d.round_start = d.round_end = 1; lv_draw_line(layer, &d);
}
void circle(lv_layer_t *layer, int x, int y, int r, lv_color_t c, bool fill) {
    if (fill) rect(layer, x - r, y - r, r * 2 + 1, r * 2 + 1, c, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    else { lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d); d.center = {x, y}; d.radius = r; d.width = 2; d.color = c; d.start_angle = 0; d.end_angle = 360; d.opa = LV_OPA_COVER; lv_draw_arc(layer, &d); }
}

void draw_controls(lv_layer_t *layer, int ox, int oy) {
    const lv_color_t fg = lv_color_black();
    for (uint8_t i = 0; i < kControlCount; ++i) {
        const int x = ox + 27 + i * 55;
        const int y = oy + 145;
        const bool focus = navigation && selected == i;
        circle(layer, x, y, i == 3 ? 19 : 16, fg, focus);
        const lv_color_t ic = focus ? lv_color_white() : fg;
        if (i == 0) { line(layer, x - 9, y, x + 9, y, ic, 2); line(layer, x - 9, y, x - 3, y - 6, ic, 2); line(layer, x - 9, y, x - 3, y + 6, ic, 2); }
        else if (i == 1) { line(layer, x - 9, y - 6, x + 9, y + 6, ic, 2); line(layer, x - 9, y + 6, x + 9, y - 6, ic, 2); }
        else if (i == 2) { line(layer, x + 7, y - 9, x - 5, y, ic, 2); line(layer, x - 5, y, x + 7, y + 9, ic, 2); line(layer, x + 9, y - 10, x + 9, y + 10, ic, 2); }
        else if (i == 3) { if (state.state == PlayerState::Playing) { rect(layer, x - 7, y - 9, 5, 18, ic, LV_OPA_COVER, 1); rect(layer, x + 3, y - 9, 5, 18, ic, LV_OPA_COVER, 1); } else { lv_point_precise_t p[3] = {{x - 7, y - 10}, {x + 10, y}, {x - 7, y + 10}}; lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.points = p; d.point_cnt = 3; d.color = ic; d.width = 3; d.opa = LV_OPA_COVER; lv_draw_line(layer, &d); } }
        else if (i == 4) { line(layer, x - 7, y - 9, x + 5, y, ic, 2); line(layer, x + 5, y, x - 7, y + 9, ic, 2); line(layer, x - 9, y - 10, x - 9, y + 10, ic, 2); }
        else if (i == 5) { for (int row = -6; row <= 6; row += 6) { circle(layer, x - 8, y + row, 1, ic, true); line(layer, x - 3, y + row, x + 9, y + row, ic, 2); } }
        else { circle(layer, x, y, 7, ic, false); circle(layer, x, y, 2, ic, true); }
    }
}

void draw_main(lv_layer_t *layer, int ox, int oy) {
    for (uint8_t i = 0; i < PLAYER_SPECTRUM_BANDS; ++i) {
        const int x = ox + 8 + i * 15;
        const int h = bars[i] > 0 ? 8 + bars[i] * 55 / 255 : 2;
        rect(layer, x, oy + 94 - h, 9, h, lv_color_black(), LV_OPA_COVER, 2);
        if (peaks[i] > 0) rect(layer, x, oy + 92 - peaks[i] * 55 / 255, 9, 2, lv_color_black());
    }
    rect(layer, ox + 10, oy + 101, 364, 4, lv_color_hex(0xA0A0A0), LV_OPA_COVER, 2);
    const int progress = state.duration_seconds ? static_cast<int>((state.elapsed_seconds * 360U) / state.duration_seconds) : 0;
    rect(layer, ox + 10, oy + 101, progress > 360 ? 360 : progress, 4, lv_color_black(), LV_OPA_COVER, 2);
    draw_controls(layer, ox, oy);
}

void draw_cb(lv_event_t *e) {
    if (view != View::Main) return;
    lv_layer_t *layer = lv_event_get_layer(e); lv_area_t c; lv_obj_get_coords(root, &c); draw_main(layer, c.x1, c.y1);
}

bool set_text_if_changed(lv_obj_t *label, const char *text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
        return true;
    }
    return false;
}

const char *audio_label(uint8_t item) {
    switch (item) {
        case 0: return "音量";
        case 1: return "功放";
        case 2: return "低音";
        case 3: return "高音";
        case 4: return "环绕";
        case 5: return "定时关闭";
        default: return "";
    }
}

void format_audio_value(uint8_t item, char *out, size_t size) {
    switch (item) {
        case 0: std::snprintf(out, size, "%u", audio.volume); break;
        case 1: std::snprintf(out, size, "%s", audio.amplifier_enabled ? "开" : "关"); break;
        case 2: std::snprintf(out, size, "%+d dB", audio.bass_db); break;
        case 3: std::snprintf(out, size, "%+d dB", audio.treble_db); break;
        case 4: std::snprintf(out, size, "%u / 15", audio.surround_depth); break;
        case 5: std::snprintf(out, size, audio.sleep_timer_min ? "%u 分钟" : "关闭", audio.sleep_timer_min); break;
        default: out[0] = '\0'; break;
    }
}

void format_audio_settings(char *out, size_t size) {
    if (size == 0U) return;
    out[0] = '\0';
    for (uint8_t item = 0; item < kAudioItemCount; ++item) {
        char value[24] = {};
        format_audio_value(item, value, sizeof(value));
        const size_t used = std::strlen(out);
        if (used >= size) break;
        std::snprintf(out + used, size - used, "%c %s: %s%s",
                      item == audio_selected ? '>' : ' ', audio_label(item), value,
                      item + 1U == kAudioItemCount ? "" : "\n");
    }
}

void adjust_audio(int delta) {
    switch (audio_selected) {
        case 0:
            audio.volume = static_cast<uint8_t>(constrain(
                static_cast<int>(audio.volume) + delta, PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX));
            break;
        case 1: audio.amplifier_enabled = !audio.amplifier_enabled; break;
        case 2:
            audio.bass_db = static_cast<int8_t>(constrain(
                static_cast<int>(audio.bass_db) + delta, -12, 12));
            break;
        case 3:
            audio.treble_db = static_cast<int8_t>(constrain(
                static_cast<int>(audio.treble_db) + delta, -12, 12));
            break;
        case 4:
            audio.surround_depth = static_cast<uint8_t>(constrain(
                static_cast<int>(audio.surround_depth) + delta, 0, 15));
            break;
        case 5:
            if (delta > 0) {
                audio.sleep_timer_min = audio.sleep_timer_min == 0U
                    ? AUDIO_SLEEP_TIMER_MIN
                    : (audio.sleep_timer_min >= AUDIO_SLEEP_TIMER_MAX
                           ? 0U
                           : static_cast<uint16_t>(audio.sleep_timer_min + 30U));
            } else {
                audio.sleep_timer_min = audio.sleep_timer_min == 0U
                    ? AUDIO_SLEEP_TIMER_MAX
                    : (audio.sleep_timer_min <= AUDIO_SLEEP_TIMER_MIN
                           ? 0U
                           : static_cast<uint16_t>(audio.sleep_timer_min - 30U));
            }
            break;
        default: return;
    }
    (void)task_post_player_audio_settings(audio, false);
}

void discard_audio_changes() {
    if (view == View::Audio && editing) {
        (void)task_post_player_audio_settings(saved_audio, false);
        audio = saved_audio;
    }
    editing = false;
}

void redraw() {
    const char *heading = view == View::Main ? "音乐" : view == View::Volume ? "音量" : view == View::Playlists ? "播放列表" : view == View::Tracks ? "曲目" : (editing ? "音频设置*" : "音频设置");
    if (view == View::Main) {
        lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lyric, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(elapsed_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(duration_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
        const bool track_changed = set_text_if_changed(track, state.file_name[0] ? state.file_name : "暂无曲目");
        if (track_changed || displayed_lyric_second != state.elapsed_seconds) {
            char lyric_text[LYRICS_LINE_BUFFER_SIZE] = {};
            (void)lyrics_service_get_current_line(state.elapsed_seconds, lyric_text, sizeof(lyric_text));
            set_text_if_changed(lyric, lyric_text);
            displayed_lyric_second = state.elapsed_seconds;
        }
        char elapsed[12] = {}; char duration[12] = {};
        std::snprintf(elapsed, sizeof(elapsed), "%02lu:%02lu", static_cast<unsigned long>(state.elapsed_seconds / 60), static_cast<unsigned long>(state.elapsed_seconds % 60));
        std::snprintf(duration, sizeof(duration), "%02lu:%02lu", static_cast<unsigned long>(state.duration_seconds / 60), static_cast<unsigned long>(state.duration_seconds % 60));
        set_text_if_changed(elapsed_label, elapsed); set_text_if_changed(duration_label, duration);
    } else {
        lv_obj_clear_flag(title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lyric, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(elapsed_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(duration_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
        set_text_if_changed(title, heading);
        if (view == View::Volume) { char t[32] = {}; std::snprintf(t, sizeof(t), "音量 %u", state.volume); set_text_if_changed(body, t); }
    else if (view == View::Playlists) { char t[768] = {}; size_t n = music_library_playlist_count(); for (size_t i = 0; i < n && i < 7; ++i) { char name[96] = {}; size_t tracks = 0; (void)music_library_playlist_get(i, name, sizeof(name), &tracks); std::snprintf(t + std::strlen(t), sizeof(t) - std::strlen(t), "%c %s (%u)\n", i == playlist ? '>' : ' ', name, static_cast<unsigned>(tracks)); } set_text_if_changed(body, t); }
    else if (view == View::Tracks) { char t[768] = {}; size_t n = 0; (void)music_library_playlist_get(playlist, nullptr, 0, &n); for (size_t i = 0; i < n && i < 7; ++i) { char name[PLAYER_NAME_LENGTH] = {}; (void)music_library_playlist_track_get(playlist, i, nullptr, nullptr, 0, name, sizeof(name)); std::snprintf(t + std::strlen(t), sizeof(t) - std::strlen(t), "%c %s\n", i == track_index ? '>' : ' ', name); } set_text_if_changed(body, t); }
        else if (view == View::Audio) { char t[256] = {}; format_audio_settings(t, sizeof(t)); set_text_if_changed(body, t); }
    }
}

void create_page() {
    root = lvgl_page_create(); lv_obj_add_event_cb(root, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    title = lvgl_label(root, "音乐", 8, 4, 200, ui_heiti_font_get(18));
    track = lvgl_label(root, "", 12, 0, 360, ui_heiti_font_get(16)); lv_obj_set_style_text_align(track, LV_TEXT_ALIGN_CENTER, 0);
    lyric = lvgl_label(root, "", 76, 106, 232, ui_heiti_font_get(16)); lv_obj_set_style_text_align(lyric, LV_TEXT_ALIGN_CENTER, 0);
    elapsed_label = lvgl_label(root, "", 12, 108, 62, &lv_font_montserrat_12); lv_obj_set_style_text_align(elapsed_label, LV_TEXT_ALIGN_LEFT, 0);
    duration_label = lvgl_label(root, "", 310, 108, 62, &lv_font_montserrat_12); lv_obj_set_style_text_align(duration_label, LV_TEXT_ALIGN_RIGHT, 0);
    body = lvgl_label(root, "", 12, 30, 360, ui_heiti_font_get(16)); lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    page.view = root;
}

void enter() { view = View::Main; selected = 3; audio_selected = 0; navigation = editing = false; last_frame = millis(); displayed_lyric_second = UINT32_MAX; std::memset(bars, 0, sizeof(bars)); std::memset(peaks, 0, sizeof(peaks)); lv_obj_set_style_text_font(body, ui_heiti_font_get(16), 0); redraw(); lv_obj_invalidate(root); }
void exit() { discard_audio_changes(); }
void navigation_changed(bool active) { navigation = active; if (!active) { discard_audio_changes(); view = View::Main; } redraw(); lv_obj_invalidate(root); }
bool consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::LongPress && view != View::Main) { discard_audio_changes(); view = View::Main; redraw(); lv_obj_invalidate(root); return true; }
    if (event.gesture != KeyGesture::Click) return false;
    const int direction = event.id == KeyId::Left ? -1 : 1;
    if (event.id == KeyId::Left || event.id == KeyId::Right) {
        if (view == View::Main) selected = static_cast<uint8_t>((selected + kControlCount + direction) % kControlCount);
        else if (view == View::Volume) (void)task_post_player_command(PlayerCommandType::ChangeVolume, direction, true);
        else if (view == View::Playlists) { const size_t c = music_library_playlist_count(); if (c) playlist = static_cast<uint16_t>((playlist + c + direction) % c); }
        else if (view == View::Tracks) { size_t c = 0; (void)music_library_playlist_get(playlist, nullptr, 0, &c); if (c) track_index = static_cast<uint16_t>((track_index + c + direction) % c); }
        else if (view == View::Audio && editing) adjust_audio(direction);
        else if (view == View::Audio) audio_selected = static_cast<uint8_t>((audio_selected + kAudioItemCount + direction) % kAudioItemCount);
        redraw(); lv_obj_invalidate(root); return true;
    }
    if (event.id != KeyId::Middle) return false;
    if (view == View::Main) { if (selected == 0) view = View::Volume; else if (selected == 1) (void)task_post_player_command(PlayerCommandType::CyclePlaybackMode, 0, true); else if (selected == 2) (void)task_post_player_command(PlayerCommandType::Previous, 0, true); else if (selected == 3) (void)task_post_player_command(PlayerCommandType::Toggle, 0, true); else if (selected == 4) (void)task_post_player_command(PlayerCommandType::Next, 0, true); else if (selected == 5) view = View::Playlists; else { view = View::Audio; audio = {state.volume, state.playback_mode, state.amplifier_enabled, state.bass_db, state.treble_db, state.surround_depth, state.sleep_timer_min}; saved_audio = audio; audio_selected = 0; editing = false; } }
    else if (view == View::Playlists) { view = View::Tracks; track_index = 0; }
    else if (view == View::Tracks) (void)task_post_player_selection(playlist, track_index);
    else if (view == View::Audio) { if (editing) { (void)task_post_player_audio_settings(audio, true); saved_audio = audio; editing = false; } else editing = true; }
    redraw(); lv_obj_invalidate(root); return true;
}
bool service() { const uint32_t now = millis(); if (now - last_frame >= 80U) { last_frame = now; for (uint8_t i = 0; i < PLAYER_SPECTRUM_BANDS; ++i) { const uint8_t target = state.state == PlayerState::Playing ? state.spectrum[i] : 0; bars[i] = bars[i] < target ? target : bars[i] > 20 ? bars[i] - 20 : 0; peaks[i] = bars[i] > peaks[i] ? bars[i] : peaks[i] > 3 ? peaks[i] - 3 : 0; } if (view == View::Main) lv_obj_invalidate(root); } return false; }
bool status_update(const PlayerStatus &incoming) { if (incoming.version == state.version) return false; const bool main_visual_changed = incoming.state != state.state || incoming.elapsed_seconds != state.elapsed_seconds || incoming.duration_seconds != state.duration_seconds; state = incoming; if (root != nullptr) { redraw(); if (view == View::Main && main_visual_changed) lv_obj_invalidate(root); } return true; }
}

GuiPageDescriptor &ui_music_page_descriptor() { if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Music, create_page, enter, exit, consume, service, status_update, root, "music", true, false, navigation_changed}; return page; }
void ui_music_page_cache_service() { }
