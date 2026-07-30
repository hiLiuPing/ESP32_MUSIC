#include "app/audio_settings.h"

#include <Preferences.h>

namespace {
Preferences prefs;
AudioSettings current = {};
bool initialized = false;

AudioSettings defaults() {
    return AudioSettings{PLAYER_DEFAULT_VOLUME, PlaybackMode::Shuffle, false,
                         0, 0, 0, AUDIO_SLEEP_TIMER_DEFAULT_MIN};
}

void normalize(AudioSettings &settings) {
    settings.volume = static_cast<uint8_t>(
        constrain(static_cast<int>(settings.volume), PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX));
    const uint8_t mode = static_cast<uint8_t>(settings.playback_mode);
    if (mode > static_cast<uint8_t>(PlaybackMode::Shuffle)) {
        settings.playback_mode = PlaybackMode::Shuffle;
    }
    settings.bass_db = static_cast<int8_t>(
        constrain(static_cast<int>(settings.bass_db), -12, 12));
    settings.treble_db = static_cast<int8_t>(
        constrain(static_cast<int>(settings.treble_db), -12, 12));
    settings.surround_depth = static_cast<uint8_t>(
        constrain(static_cast<int>(settings.surround_depth), 0, 15));
    if (settings.sleep_timer_min != 0U &&
        (settings.sleep_timer_min < AUDIO_SLEEP_TIMER_MIN ||
         settings.sleep_timer_min > AUDIO_SLEEP_TIMER_MAX)) {
        settings.sleep_timer_min = AUDIO_SLEEP_TIMER_DEFAULT_MIN;
    }
    settings.amplifier_enabled = settings.amplifier_enabled ? true : false;
}

void persist() {
    prefs.putUChar("volume", current.volume);
    prefs.putUChar("mode", static_cast<uint8_t>(current.playback_mode));
    prefs.putBool("amp", current.amplifier_enabled);
    prefs.putChar("bass", current.bass_db);
    prefs.putChar("treble", current.treble_db);
    prefs.putUChar("surround", current.surround_depth);
    prefs.putUShort("sleep", current.sleep_timer_min);
}

bool equal(const AudioSettings &left, const AudioSettings &right) {
    return left.volume == right.volume &&
           left.playback_mode == right.playback_mode &&
           left.amplifier_enabled == right.amplifier_enabled &&
           left.bass_db == right.bass_db &&
           left.treble_db == right.treble_db &&
           left.surround_depth == right.surround_depth &&
           left.sleep_timer_min == right.sleep_timer_min;
}
}

void audio_settings_init() {
    if (initialized) return;

    prefs.begin("s3audio", false);
    current = defaults();
    current.volume = prefs.getUChar("volume", current.volume);
    current.playback_mode = static_cast<PlaybackMode>(
        prefs.getUChar("mode", static_cast<uint8_t>(current.playback_mode)));
    current.amplifier_enabled = prefs.getBool("amp", current.amplifier_enabled);
    current.bass_db = prefs.getChar("bass", current.bass_db);
    current.treble_db = prefs.getChar("treble", current.treble_db);
    current.surround_depth = prefs.getUChar("surround", current.surround_depth);
    current.sleep_timer_min = prefs.getUShort("sleep", current.sleep_timer_min);
    normalize(current);
    persist();
    initialized = true;
}

AudioSettings audio_settings_get() {
    audio_settings_init();
    return current;
}

bool audio_settings_update(const AudioSettings &settings) {
    audio_settings_init();
    AudioSettings next = settings;
    normalize(next);
    if (equal(next, current)) return false;
    current = next;
    persist();
    return true;
}
