#include <Arduino.h>

#include <Audio.h>
#include <cstring>

#include "app/music_library.h"
#include "bsp/bsp_audio.h"
#include "bsp/bsp_storage.h"
#include "task/task_system.h"

namespace {
Audio audio;
volatile bool eof_received = false;
PlayerStatus status = {};
bool sd_ready = false;
bool codec_ready = false;
bool track_started = false;
TickType_t last_status_tick = 0;

void publish(bool force = false) {
    const TickType_t now = xTaskGetTickCount();
    if (!force && ((now - last_status_tick) < pdMS_TO_TICKS(500))) {
        return;
    }
    last_status_tick = now;
    if (track_started) {
        status.elapsed_seconds = audio.getAudioCurrentTime();
        status.duration_seconds = audio.getAudioFileDuration();
    }
    ++status.version;
    task_publish_player_status(status);
}

void select_track(uint16_t index) {
    status.track_index = index;
    char path[PLAYER_PATH_LENGTH] = {};
    music_library_get(index, path, sizeof(path), status.file_name, sizeof(status.file_name));
    status.elapsed_seconds = 0;
    status.duration_seconds = 0;
}

bool start_track(uint16_t index) {
    char path[PLAYER_PATH_LENGTH] = {};
    char name[PLAYER_NAME_LENGTH] = {};
    if (!sd_ready) {
        status.state = PlayerState::NoSd;
        status.error = PlayerError::SdUnavailable;
        publish(true);
        return false;
    }
    if (!codec_ready) {
        status.state = PlayerState::Error;
        status.error = PlayerError::CodecUnavailable;
        publish(true);
        return false;
    }
    if (!music_library_get(index, path, sizeof(path), name, sizeof(name))) {
        status.state = PlayerState::Error;
        status.error = PlayerError::TrackOutOfRange;
        publish(true);
        return false;
    }

    audio.stopSong();
    eof_received = false;
    if (!audio.connecttoFS(bsp_storage_fs(), path)) {
        track_started = false;
        status.state = PlayerState::Error;
        status.error = PlayerError::OpenFailed;
        std::snprintf(status.file_name, sizeof(status.file_name), "%s", name);
        Serial.printf("[PLAYER] failed to open %s\n", path);
        publish(true);
        return false;
    }

    track_started = true;
    status.track_index = index;
    status.elapsed_seconds = 0;
    status.duration_seconds = 0;
    status.state = PlayerState::Playing;
    status.error = PlayerError::None;
    std::snprintf(status.file_name, sizeof(status.file_name), "%s", name);
    Serial.printf("[PLAYER] playing %s\n", path);
    publish(true);
    return true;
}

void rescan_library() {
    audio.stopSong();
    track_started = false;
    eof_received = false;
    status.elapsed_seconds = 0;
    status.duration_seconds = 0;
    status.error = PlayerError::None;

    xEventGroupClearBits(HardwareEventGroup, HW_EVENT_LIBRARY_READY);
    sd_ready = bsp_storage_available() || bsp_storage_init();
    if (sd_ready) xEventGroupSetBits(HardwareEventGroup, HW_EVENT_SD_READY);
    else xEventGroupClearBits(HardwareEventGroup, HW_EVENT_SD_READY);
    if (!sd_ready) {
        status.track_count = 0;
        status.file_name[0] = '\0';
        status.state = PlayerState::NoSd;
        status.error = PlayerError::SdUnavailable;
        publish(true);
        return;
    }

    music_library_scan(bsp_storage_fs());
    status.track_count = static_cast<uint16_t>(music_library_count());
    if (status.track_count == 0) {
        status.track_index = 0;
        status.file_name[0] = '\0';
        status.state = PlayerState::Empty;
        status.error = PlayerError::EmptyLibrary;
    } else {
        select_track(0);
        status.state = codec_ready ? PlayerState::Ready : PlayerState::Error;
        status.error = codec_ready ? PlayerError::None : PlayerError::CodecUnavailable;
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_LIBRARY_READY);
    }
    Serial.printf("[PLAYER] scan complete, %u track(s)\n", status.track_count);
    publish(true);
}

void move_track(int direction, bool play) {
    if (status.track_count == 0) return;
    const int count = status.track_count;
    const uint16_t index = static_cast<uint16_t>((status.track_index + count + direction) % count);
    if (play || track_started) start_track(index);
    else { select_track(index); publish(true); }
}

void handle_command(const PlayerCommand &command) {
    switch (command.type) {
        case PlayerCommandType::PlaySelected:
            if ((command.value >= 0) && (command.value < status.track_count)) {
                start_track(static_cast<uint16_t>(command.value));
            } else {
                status.state = PlayerState::Error;
                status.error = PlayerError::TrackOutOfRange;
                publish(true);
            }
            break;
        case PlayerCommandType::Toggle:
            if (status.state == PlayerState::Playing || status.state == PlayerState::Paused) {
                if (audio.pauseResume()) {
                    status.state = (status.state == PlayerState::Playing)
                                       ? PlayerState::Paused : PlayerState::Playing;
                    publish(true);
                }
            } else if (status.track_count > 0) {
                start_track(status.track_index);
            }
            break;
        case PlayerCommandType::Play:
            if (status.state == PlayerState::Paused) {
                if (audio.pauseResume()) { status.state = PlayerState::Playing; publish(true); }
            } else if (status.track_count > 0) {
                start_track(status.track_index);
            }
            break;
        case PlayerCommandType::Pause:
            if ((status.state == PlayerState::Playing) && audio.pauseResume()) {
                status.state = PlayerState::Paused;
                publish(true);
            }
            break;
        case PlayerCommandType::Previous: move_track(-1, true); break;
        case PlayerCommandType::Next: move_track(1, true); break;
        case PlayerCommandType::SetVolume:
            status.volume = constrain(command.value, PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX);
            audio.setVolume(status.volume);
            publish(true);
            break;
        case PlayerCommandType::ChangeVolume:
            status.volume = constrain(static_cast<int>(status.volume) + command.value,
                                      PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX);
            audio.setVolume(status.volume);
            publish(true);
            break;
        case PlayerCommandType::Rescan: rescan_library(); break;
    }
}
}

void audio_eof_mp3(const char *info) {
    (void)info;
    eof_received = true;
}

void audio_info(const char *info) {
    Serial.printf("[AUDIO] %s\n", info);
}

void player_task(void *parameter) {
    (void)parameter;
    std::memset(&status, 0, sizeof(status));
    status.state = PlayerState::Initializing;
    status.volume = PLAYER_DEFAULT_VOLUME;
    publish(true);

    xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    sd_ready = (bits & HW_EVENT_SD_READY) != 0;
    codec_ready = (bits & HW_EVENT_CODEC_READY) != 0;
    if (codec_ready && !bsp_audio_configure_i2s(audio)) {
        codec_ready = false;
    }
    audio.setVolume(status.volume);
    rescan_library();

    for (;;) {
        PlayerCommand command = {};
        while (xQueueReceive(PlayerCommandQueue, &command, 0) == pdTRUE) {
            handle_command(command);
        }

        if (status.state == PlayerState::Playing) {
            audio.loop();
            if (eof_received) {
                eof_received = false;
                track_started = false;
                move_track(1, true);
            } else if (track_started && !audio.isRunning()) {
                track_started = false;
                status.state = PlayerState::Error;
                status.error = PlayerError::DecodeStopped;
                publish(true);
            }
        }
        if (status.state == PlayerState::Playing) {
            publish(false);
        }
        vTaskDelay(status.state == PlayerState::Playing ? 1 : pdMS_TO_TICKS(10));
    }
}
