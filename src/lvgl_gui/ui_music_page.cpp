#include "gui/screens/ui_music_page.h"

#include <Arduino.h>
#include <cstring>

#include "app/lyrics_service.h"
#include "app/music_library.h"
#include "gui/page_manager.h"
#include "gui/ui_heiti_font.h"
#include "lvgl_ui_common.h"
#include "task/task_system.h"

namespace {
enum class View : uint8_t { Main, Volume, Playlists, Tracks, Audio };
lv_obj_t *root = nullptr;
lv_obj_t *title = nullptr;
lv_obj_t *body = nullptr;
lv_obj_t *hint = nullptr;
GuiPageDescriptor page = {};
PlayerStatus state = {};
View view = View::Main;
uint8_t selected = 2;
uint16_t playlist = 0;
uint16_t track = 0;
bool navigation = false;
bool editing = false;
AudioSettings audio = {};

void redraw() {
    char text[1024] = {};
    const char *heading = "MUSIC";
    if (view == View::Main) {
        char lyric[LYRICS_LINE_BUFFER_SIZE] = {};
        (void)lyrics_service_get_current_line(state.elapsed_seconds, lyric, sizeof(lyric));
        const char *mode = state.playback_mode == PlaybackMode::Shuffle ? "SHUFFLE" :
                           state.playback_mode == PlaybackMode::RepeatOne ? "REPEAT ONE" : "REPEAT ALL";
        std::snprintf(text, sizeof(text), "%s\n\n%s\n\n%02lu:%02lu / %02lu:%02lu\nVOL %u  %s\n\n%s\n\n[ VOL  MODE  PREV  PLAY  NEXT  LIST  AUDIO ]",
                      state.file_name[0] ? state.file_name : "NO TRACK", lyric,
                      static_cast<unsigned long>(state.elapsed_seconds / 60), static_cast<unsigned long>(state.elapsed_seconds % 60),
                      static_cast<unsigned long>(state.duration_seconds / 60), static_cast<unsigned long>(state.duration_seconds % 60),
                      state.volume, mode, state.state == PlayerState::Playing ? "PLAYING" : "PAUSED");
    } else if (view == View::Volume) {
        heading = "VOLUME"; std::snprintf(text, sizeof(text), "\n\n\n\n\nVOLUME: %u\n\nLEFT / RIGHT TO ADJUST\nMIDDLE BACK", state.volume);
    } else if (view == View::Playlists) {
        heading = "PLAYLISTS"; size_t count = music_library_playlist_count();
        for (size_t i = 0; i < count && i < 6; ++i) { char name[96] = {}; size_t tracks = 0; (void)music_library_playlist_get(i, name, sizeof(name), &tracks); std::snprintf(text + std::strlen(text), sizeof(text) - std::strlen(text), "%c %s (%u)\n", i == playlist ? '>' : ' ', name, static_cast<unsigned>(tracks)); }
    } else if (view == View::Tracks) {
        heading = "TRACKS"; size_t count = 0; (void)music_library_playlist_get(playlist, nullptr, 0, &count);
        for (size_t i = 0; i < count && i < 6; ++i) { char name[PLAYER_NAME_LENGTH] = {}; (void)music_library_playlist_track_get(playlist, i, nullptr, nullptr, 0, name, sizeof(name)); std::snprintf(text + std::strlen(text), sizeof(text) - std::strlen(text), "%c %s\n", i == track ? '>' : ' ', name); }
    } else {
        heading = editing ? "AUDIO EDIT" : "AUDIO SETTINGS";
        std::snprintf(text, sizeof(text), "VOL %u\nAMP %s\nBASS %d\nTREBLE %d\nSURROUND %u\nSLEEP %u MIN\n\n%s", audio.volume, audio.amplifier_enabled ? "ON" : "OFF", audio.bass_db, audio.treble_db, audio.surround_depth, audio.sleep_timer_min, editing ? "LEFT / RIGHT ADJUST, MIDDLE SAVE" : "MIDDLE EDIT");
    }
    lv_label_set_text(title, heading); lv_label_set_text(body, text);
    lv_label_set_text(hint, navigation ? "MIDDLE: SELECT   RIGHT HOLD: BACK" : "LEFT / RIGHT: PAGE   MIDDLE: ENTER");
}

void create_page() { root = lvgl_page_create(); title = lvgl_label(root, "MUSIC", 8, 3, 360, &lv_font_montserrat_14); lvgl_header(root, "MUSIC"); body = lvgl_label(root, "", 10, 28, 364, &lv_font_montserrat_14); lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP); hint = lvgl_label(root, "", 8, 150, 368, &lv_font_montserrat_12); page.view = root; }
void enter() { view = View::Main; selected = 2; navigation = editing = false; lv_obj_set_style_text_font(body, ui_heiti_font_get(16), 0); redraw(); }
void exit() { editing = false; }
void navigation_changed(bool active) { navigation = active; if (!active) { view = View::Main; editing = false; } redraw(); }
void adjust_audio(int delta) { audio.volume = static_cast<uint8_t>(constrain(static_cast<int>(audio.volume) + delta, PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX)); (void)task_post_player_audio_settings(audio, false); }
bool consume(const KeyEvent &event) {
    if (event.id == KeyId::Middle && event.gesture == KeyGesture::LongPress && view != View::Main) { view = View::Main; editing = false; redraw(); return true; }
    if (event.gesture != KeyGesture::Click) return false;
    const int direction = event.id == KeyId::Left ? -1 : 1;
    if (event.id == KeyId::Left || event.id == KeyId::Right) {
        if (view == View::Main) selected = static_cast<uint8_t>((selected + 7 + direction) % 7);
        else if (view == View::Volume) (void)task_post_player_command(PlayerCommandType::ChangeVolume, direction, true);
        else if (view == View::Playlists) { const size_t c = music_library_playlist_count(); if (c) playlist = static_cast<uint16_t>((playlist + c + direction) % c); }
        else if (view == View::Tracks) { size_t c = 0; (void)music_library_playlist_get(playlist, nullptr, 0, &c); if (c) track = static_cast<uint16_t>((track + c + direction) % c); }
        else if (editing) adjust_audio(direction);
        redraw(); return true;
    }
    if (event.id != KeyId::Middle) return false;
    if (view == View::Main) { if (selected == 0) view = View::Volume; else if (selected == 1) (void)task_post_player_command(PlayerCommandType::CyclePlaybackMode, 0, true); else if (selected == 2) (void)task_post_player_command(PlayerCommandType::Previous, 0, true); else if (selected == 3) (void)task_post_player_command(PlayerCommandType::Toggle, 0, true); else if (selected == 4) (void)task_post_player_command(PlayerCommandType::Next, 0, true); else if (selected == 5) view = View::Playlists; else { view = View::Audio; audio = {state.volume, state.playback_mode, state.amplifier_enabled, state.bass_db, state.treble_db, state.surround_depth, state.sleep_timer_min}; } }
    else if (view == View::Playlists) { view = View::Tracks; track = 0; }
    else if (view == View::Tracks) (void)task_post_player_selection(playlist, track);
    else if (view == View::Audio) { if (editing) { (void)task_post_player_audio_settings(audio, true); editing = false; } else editing = true; }
    redraw(); return true;
}
bool service() { if (view == View::Main) redraw(); return false; }
bool status_update(const PlayerStatus &incoming) { state = incoming; if (root != nullptr && view == View::Main) redraw(); return true; }
}

GuiPageDescriptor &ui_music_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Music, create_page, enter, exit, consume, service, status_update, root, "music", true, false, navigation_changed};
    return page;
}
void ui_music_page_cache_service() {}
