#pragma once

#include <Arduino.h>

constexpr size_t PLAYER_MAX_TRACKS = 128;
constexpr size_t PLAYER_PATH_LENGTH = 256;
constexpr size_t PLAYER_NAME_LENGTH = 128;
constexpr uint8_t PLAYER_VOLUME_MIN = 0;
constexpr uint8_t PLAYER_VOLUME_MAX = 21;
constexpr uint8_t PLAYER_DEFAULT_VOLUME = 12;

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
    Rescan,
};

struct PlayerCommand {
    PlayerCommandType type;
    int16_t value;
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
    char file_name[PLAYER_NAME_LENGTH];
};
