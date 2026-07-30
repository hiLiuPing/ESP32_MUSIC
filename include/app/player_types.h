#pragma once

#include <Arduino.h>

constexpr size_t PLAYER_MAX_TRACKS = 1000;
constexpr size_t PLAYER_PATH_LENGTH = 256;
constexpr size_t PLAYER_NAME_LENGTH = 128;
constexpr uint8_t PLAYER_VOLUME_MIN = 0;
constexpr uint8_t PLAYER_VOLUME_MAX = 21;
constexpr uint8_t PLAYER_DEFAULT_VOLUME = 12;
constexpr size_t PLAYER_SPECTRUM_BANDS = 24;
constexpr uint16_t AUDIO_SLEEP_TIMER_DEFAULT_MIN = 60U;
constexpr uint16_t AUDIO_SLEEP_TIMER_MIN = 30U;
constexpr uint16_t AUDIO_SLEEP_TIMER_MAX = 300U;

enum class PlaybackMode : uint8_t {
    RepeatAll,
    RepeatOne,
    Shuffle,
};

struct AudioSettings {
    uint8_t volume;
    PlaybackMode playback_mode;
    bool amplifier_enabled;
    int8_t bass_db;
    int8_t treble_db;
    uint8_t surround_depth;
    uint16_t sleep_timer_min;
};

enum class PlayerState : uint8_t {
    Initializing,
    Ready,
    Playing,
    Paused,
    Stopped,
    NoSd,
    Empty,
    Error,
};

enum class PlayerError : uint8_t {
    None,
    SdUnavailable,
    CodecUnavailable,
    EmptyLibrary,
    TrackOutOfRange,
    OpenFailed,
    DecodeStopped,
    QueueFull,
};

enum class PlayerCommandType : uint8_t {
    PlaySelected,
    Toggle,
    Play,
    Pause,
    Previous,
    Next,
    SetVolume,
    ChangeVolume,
    CyclePlaybackMode,
    ApplyAudioSettings,
    Rescan,
};

struct PlayerCommand {
    PlayerCommandType type;
    int16_t value;
    AudioSettings audio_settings;
    bool persist_audio_settings;
    bool restart_sleep_timer;
};

struct PlayerStatus {
    uint32_t version;
    PlayerState state;
    PlayerError error;
    uint16_t track_index;
    uint16_t track_count;
    uint32_t elapsed_seconds;
    uint32_t duration_seconds;
    uint8_t volume;
    PlaybackMode playback_mode;
    bool amplifier_enabled;
    int8_t bass_db;
    int8_t treble_db;
    uint8_t surround_depth;
    uint16_t sleep_timer_min;
    uint32_t sleep_timer_remaining_seconds;
    uint8_t spectrum[PLAYER_SPECTRUM_BANDS];
    char file_name[PLAYER_NAME_LENGTH];
};
