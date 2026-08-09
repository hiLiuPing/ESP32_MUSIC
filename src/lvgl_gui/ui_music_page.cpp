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
lv_obj_t *root = nullptr;
lv_obj_t *title = nullptr;
lv_obj_t *track = nullptr;
lv_obj_t *lyric = nullptr;
lv_obj_t *time_label = nullptr;
lv_obj_t *body = nullptr;
lv_obj_t *hint = nullptr;
GuiPageDescriptor page = {};
PlayerStatus state = {};
View view = View::Main;
uint8_t selected = 3;
uint16_t playlist = 0;
uint16_t track_index = 0;
bool navigation = false;
bool editing = false;
AudioSettings audio = {};
uint8_t bars[PLAYER_SPECTRUM_BANDS] = {};
uint8_t peaks[PLAYER_SPECTRUM_BANDS] = {};
uint32_t last_frame = 0;

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

void redraw() {
    const char *heading = view == View::Main ? "音乐" : view == View::Volume ? "音量" : view == View::Playlists ? "播放列表" : view == View::Tracks ? "曲目" : (editing ? "音频设置*" : "音频设置");
    lv_label_set_text(title, heading);
    if (view == View::Main) {
        lv_label_set_text(track, state.file_name[0] ? state.file_name : "暂无曲目");
        char lyric_text[LYRICS_LINE_BUFFER_SIZE] = {}; (void)lyrics_service_get_current_line(state.elapsed_seconds, lyric_text, sizeof(lyric_text)); lv_label_set_text(lyric, lyric_text);
        char tm[32] = {}; std::snprintf(tm, sizeof(tm), "%02lu:%02lu / %02lu:%02lu   音量 %u", static_cast<unsigned long>(state.elapsed_seconds / 60), static_cast<unsigned long>(state.elapsed_seconds % 60), static_cast<unsigned long>(state.duration_seconds / 60), static_cast<unsigned long>(state.duration_seconds % 60), state.volume); lv_label_set_text(time_label, tm); lv_label_set_text(body, "");
    } else if (view == View::Volume) { char t[48] = {}; std::snprintf(t, sizeof(t), "音量  %u\n左右调节，中键返回", state.volume); lv_label_set_text(body, t); }
    else if (view == View::Playlists) { char t[768] = {}; size_t n = music_library_playlist_count(); for (size_t i = 0; i < n && i < 7; ++i) { char name[96] = {}; size_t tracks = 0; (void)music_library_playlist_get(i, name, sizeof(name), &tracks); std::snprintf(t + std::strlen(t), sizeof(t) - std::strlen(t), "%c %s (%u)\n", i == playlist ? '>' : ' ', name, static_cast<unsigned>(tracks)); } lv_label_set_text(body, t); }
    else if (view == View::Tracks) { char t[768] = {}; size_t n = 0; (void)music_library_playlist_get(playlist, nullptr, 0, &n); for (size_t i = 0; i < n && i < 7; ++i) { char name[PLAYER_NAME_LENGTH] = {}; (void)music_library_playlist_track_get(playlist, i, nullptr, nullptr, 0, name, sizeof(name)); std::snprintf(t + std::strlen(t), sizeof(t) - std::strlen(t), "%c %s\n", i == track_index ? '>' : ' ', name); } lv_label_set_text(body, t); }
    else { char t[256] = {}; std::snprintf(t, sizeof(t), "音量 %u\n功放 %s\n低音 %d\n高音 %d\n环绕 %u\n睡眠 %u 分钟", audio.volume, audio.amplifier_enabled ? "开" : "关", audio.bass_db, audio.treble_db, audio.surround_depth, audio.sleep_timer_min); lv_label_set_text(body, t); }
    lv_label_set_text(hint, navigation ? "左右选择，中键确认，右键长按返回" : "左右切页，中键进入"); lv_obj_invalidate(root);
}

void create_page() {
    root = lvgl_page_create(); lv_obj_add_event_cb(root, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    title = lvgl_label(root, "音乐", 8, 4, 200, ui_heiti_font_get(18));
    track = lvgl_label(root, "", 12, 24, 360, ui_heiti_font_get(16)); lv_obj_set_style_text_align(track, LV_TEXT_ALIGN_CENTER, 0);
    lyric = lvgl_label(root, "", 12, 108, 360, ui_heiti_font_get(16)); lv_obj_set_style_text_align(lyric, LV_TEXT_ALIGN_CENTER, 0);
    time_label = lvgl_label(root, "", 12, 120, 360, &lv_font_montserrat_12); lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    body = lvgl_label(root, "", 12, 30, 360, ui_heiti_font_get(16)); lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    hint = lvgl_label(root, "", 8, 158, 368, ui_heiti_font_get(12)); page.view = root;
}

void enter() { view = View::Main; selected = 3; navigation = editing = false; last_frame = millis(); std::memset(bars, 0, sizeof(bars)); std::memset(peaks, 0, sizeof(peaks)); lv_obj_set_style_text_font(body, ui_heiti_font_get(16), 0); redraw(); }
void exit() { editing = false; }
void navigation_changed(bool active) { navigation = active; if (!active) { view = View::Main; editing = false; } redraw(); }
void adjust_audio(int delta) { audio.volume = static_cast<uint8_t>(constrain(static_cast<int>(audio.volume) + delta, PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX)); (void)task_post_player_audio_settings(audio, false); }
bool consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::LongPress && view != View::Main) { view = View::Main; editing = false; redraw(); return true; }
    if (event.gesture != KeyGesture::Click) return false;
    const int direction = event.id == KeyId::Left ? -1 : 1;
    if (event.id == KeyId::Left || event.id == KeyId::Right) {
        if (view == View::Main) selected = static_cast<uint8_t>((selected + kControlCount + direction) % kControlCount);
        else if (view == View::Volume) (void)task_post_player_command(PlayerCommandType::ChangeVolume, direction, true);
        else if (view == View::Playlists) { const size_t c = music_library_playlist_count(); if (c) playlist = static_cast<uint16_t>((playlist + c + direction) % c); }
        else if (view == View::Tracks) { size_t c = 0; (void)music_library_playlist_get(playlist, nullptr, 0, &c); if (c) track_index = static_cast<uint16_t>((track_index + c + direction) % c); }
        else if (editing) adjust_audio(direction); redraw(); return true;
    }
    if (event.id != KeyId::Middle) return false;
    if (view == View::Main) { if (selected == 0) view = View::Volume; else if (selected == 1) (void)task_post_player_command(PlayerCommandType::CyclePlaybackMode, 0, true); else if (selected == 2) (void)task_post_player_command(PlayerCommandType::Previous, 0, true); else if (selected == 3) (void)task_post_player_command(PlayerCommandType::Toggle, 0, true); else if (selected == 4) (void)task_post_player_command(PlayerCommandType::Next, 0, true); else if (selected == 5) view = View::Playlists; else { view = View::Audio; audio = {state.volume, state.playback_mode, state.amplifier_enabled, state.bass_db, state.treble_db, state.surround_depth, state.sleep_timer_min}; } }
    else if (view == View::Playlists) { view = View::Tracks; track_index = 0; }
    else if (view == View::Tracks) (void)task_post_player_selection(playlist, track_index);
    else if (view == View::Audio) { if (editing) { (void)task_post_player_audio_settings(audio, true); editing = false; } else editing = true; }
    redraw(); return true;
}
bool service() { const uint32_t now = millis(); if (now - last_frame >= 80U) { last_frame = now; for (uint8_t i = 0; i < PLAYER_SPECTRUM_BANDS; ++i) { const uint8_t target = state.state == PlayerState::Playing ? state.spectrum[i] : 0; bars[i] = bars[i] < target ? target : bars[i] > 20 ? bars[i] - 20 : 0; peaks[i] = bars[i] > peaks[i] ? bars[i] : peaks[i] > 3 ? peaks[i] - 3 : 0; } lv_obj_invalidate(root); } if (view == View::Main) redraw(); return false; }
bool status_update(const PlayerStatus &incoming) { if (incoming.version == state.version) return false; state = incoming; if (root != nullptr) redraw(); return true; }
}

GuiPageDescriptor &ui_music_page_descriptor() { if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Music, create_page, enter, exit, consume, service, status_update, root, "music", true, false, navigation_changed}; return page; }
void ui_music_page_cache_service() { }
