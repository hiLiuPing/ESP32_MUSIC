#include "app/player_app.h"

#include <cstring>

namespace {
PlayerStatus shared_status = {};
SemaphoreHandle_t status_mutex = nullptr;
}

void player_app_attach_mutex(SemaphoreHandle_t mutex) {
    status_mutex = mutex;
}

void player_app_set_status(const PlayerStatus &status) {
    if ((status_mutex != nullptr) && (xSemaphoreTake(status_mutex, portMAX_DELAY) == pdTRUE)) {
        shared_status = status;
        xSemaphoreGive(status_mutex);
    }
}

bool player_app_get_status(PlayerStatus *status) {
    if ((status == nullptr) || (status_mutex == nullptr) ||
        (xSemaphoreTake(status_mutex, pdMS_TO_TICKS(50)) != pdTRUE)) {
        return false;
    }
    *status = shared_status;
    xSemaphoreGive(status_mutex);
    return true;
}

const char *player_state_name(PlayerState state) {
    switch (state) {
        case PlayerState::Initializing: return "initializing";
        case PlayerState::Ready: return "ready";
        case PlayerState::Playing: return "playing";
        case PlayerState::Paused: return "paused";
        case PlayerState::Stopped: return "stopped";
        case PlayerState::NoSd: return "no-sd";
        case PlayerState::Empty: return "empty";
        case PlayerState::Error: return "error";
    }
    return "unknown";
}

const char *player_error_name(PlayerError error) {
    switch (error) {
        case PlayerError::None: return "none";
        case PlayerError::SdUnavailable: return "sd-unavailable";
        case PlayerError::CodecUnavailable: return "codec-unavailable";
        case PlayerError::EmptyLibrary: return "empty-library";
        case PlayerError::TrackOutOfRange: return "track-out-of-range";
        case PlayerError::OpenFailed: return "open-failed";
        case PlayerError::DecodeStopped: return "decode-stopped";
        case PlayerError::QueueFull: return "queue-full";
    }
    return "unknown";
}
