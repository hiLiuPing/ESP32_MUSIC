#include <Arduino.h>

#include <Audio.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dsps_fft2r.h>
#include <esp_system.h>

#include "app/music_library.h"
#include "bsp/bsp_audio.h"
#include "bsp/bsp_storage.h"
#include "task/task_system.h"

namespace {
constexpr size_t FFT_SIZE = 512;
constexpr uint32_t SPECTRUM_INTERVAL_MS = 80;
constexpr uint32_t STATUS_INTERVAL_MS = 100;
constexpr float SPECTRUM_MIN_HZ = 80.0F;
constexpr float SPECTRUM_MAX_HZ = 16000.0F;
constexpr float SPECTRUM_MIN_DB = -72.0F;
constexpr float SPECTRUM_MAX_DB = -12.0F;

Audio audio;
volatile bool eof_received = false;
PlayerStatus status = {};
bool sd_ready = false;
bool codec_ready = false;
bool track_started = false;
TickType_t last_status_tick = 0;
TickType_t last_spectrum_tick = 0;

alignas(16) float fft_data[FFT_SIZE * 2] = {};
alignas(16) float fft_table[FFT_SIZE] = {};
float fft_window[FFT_SIZE] = {};
uint16_t band_start[PLAYER_SPECTRUM_BANDS] = {};
uint16_t band_end[PLAYER_SPECTRUM_BANDS] = {};
uint32_t mapped_sample_rate = 0;
bool spectrum_ready = false;

uint16_t shuffle_order[PLAYER_MAX_TRACKS] = {};
uint16_t shuffle_count = 0;
uint16_t shuffle_cursor = 0;

void clear_spectrum() {
    std::memset(status.spectrum, 0, sizeof(status.spectrum));
}

void publish(bool force = false) {
    const TickType_t now = xTaskGetTickCount();
    if (!force && ((now - last_status_tick) < pdMS_TO_TICKS(STATUS_INTERVAL_MS))) {
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

void init_spectrum() {
    for (size_t index = 0; index < FFT_SIZE; ++index) {
        fft_window[index] = 0.5F -
                            0.5F * std::cos(2.0F * PI * index /
                                            static_cast<float>(FFT_SIZE - 1));
    }
    const esp_err_t result = dsps_fft2r_init_fc32(fft_table, FFT_SIZE);
    spectrum_ready = result == ESP_OK;
    if (!spectrum_ready) {
        Serial.printf("[SPECTRUM] ESP-DSP init failed: %d\n", result);
    }
}

void rebuild_band_map(uint32_t sample_rate) {
    if (sample_rate == 0 || sample_rate == mapped_sample_rate) {
        return;
    }
    mapped_sample_rate = sample_rate;
    const float nyquist = sample_rate * 0.5F;
    const float max_frequency = std::min(SPECTRUM_MAX_HZ, nyquist);
    const float ratio = std::pow(max_frequency / SPECTRUM_MIN_HZ,
                                 1.0F / PLAYER_SPECTRUM_BANDS);
    const float bin_width = static_cast<float>(sample_rate) / FFT_SIZE;

    for (size_t band = 0; band < PLAYER_SPECTRUM_BANDS; ++band) {
        const float low = SPECTRUM_MIN_HZ * std::pow(ratio, band);
        const float high = SPECTRUM_MIN_HZ * std::pow(ratio, band + 1);
        uint16_t start = static_cast<uint16_t>(std::ceil(low / bin_width));
        uint16_t end = static_cast<uint16_t>(std::ceil(high / bin_width));
        start = std::max<uint16_t>(1, std::min<uint16_t>(start, FFT_SIZE / 2 - 1));
        end = std::max<uint16_t>(static_cast<uint16_t>(start + 1), end);
        end = std::min<uint16_t>(end, FFT_SIZE / 2);
        band_start[band] = start;
        band_end[band] = end;
    }
}

void analyze_pcm(int16_t *buffer, uint16_t frame_count) {
    if (!spectrum_ready || buffer == nullptr || frame_count < FFT_SIZE ||
        status.state != PlayerState::Playing) {
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    if ((now - last_spectrum_tick) < pdMS_TO_TICKS(SPECTRUM_INTERVAL_MS)) {
        return;
    }
    last_spectrum_tick = now;

    const uint8_t channels = std::max<uint8_t>(1, audio.getChannels());
    const uint32_t sample_rate = audio.getSampleRate();
    rebuild_band_map(sample_rate);
    if (mapped_sample_rate == 0) {
        return;
    }

    for (size_t index = 0; index < FFT_SIZE; ++index) {
        int32_t mono = buffer[index * channels];
        if (channels > 1) {
            mono = (mono + buffer[index * channels + 1]) / 2;
        }
        fft_data[index * 2] = (static_cast<float>(mono) / 32768.0F) *
                              fft_window[index];
        fft_data[index * 2 + 1] = 0.0F;
    }

    if (dsps_fft2r_fc32(fft_data, FFT_SIZE) != ESP_OK ||
        dsps_bit_rev_fc32(fft_data, FFT_SIZE) != ESP_OK) {
        return;
    }

    constexpr float fft_scale = FFT_SIZE * 0.25F;
    constexpr float db_span = SPECTRUM_MAX_DB - SPECTRUM_MIN_DB;
    for (size_t band = 0; band < PLAYER_SPECTRUM_BANDS; ++band) {
        float peak = 0.0F;
        for (uint16_t bin = band_start[band]; bin < band_end[band]; ++bin) {
            const float real = fft_data[bin * 2];
            const float imaginary = fft_data[bin * 2 + 1];
            peak = std::max(peak, std::sqrt(real * real + imaginary * imaginary));
        }
        const float db = 20.0F * std::log10(peak / fft_scale + 1.0e-9F);
        const float normalized = (db - SPECTRUM_MIN_DB) / db_span;
        status.spectrum[band] = static_cast<uint8_t>(
            constrain(static_cast<int>(normalized * 255.0F), 0, 255));
    }
}

void build_shuffle(uint16_t current_index) {
    shuffle_count = status.track_count;
    shuffle_cursor = 0;
    if (shuffle_count == 0) {
        return;
    }

    shuffle_order[0] = std::min<uint16_t>(current_index, shuffle_count - 1);
    uint16_t write_index = 1;
    for (uint16_t index = 0; index < shuffle_count; ++index) {
        if (index != shuffle_order[0]) {
            shuffle_order[write_index++] = index;
        }
    }
    for (uint16_t index = shuffle_count; index > 2; --index) {
        const uint16_t selected = static_cast<uint16_t>(1 + esp_random() % (index - 1));
        std::swap(shuffle_order[index - 1], shuffle_order[selected]);
    }
}

void select_track(uint16_t index) {
    status.track_index = index;
    char path[PLAYER_PATH_LENGTH] = {};
    music_library_get(index, path, sizeof(path), status.file_name,
                      sizeof(status.file_name));
    status.elapsed_seconds = 0;
    status.duration_seconds = 0;
}

bool start_track(uint16_t index) {
    char path[PLAYER_PATH_LENGTH] = {};
    char name[PLAYER_NAME_LENGTH] = {};
    if (!sd_ready) {
        status.state = PlayerState::NoSd;
        status.error = PlayerError::SdUnavailable;
        clear_spectrum();
        publish(true);
        return false;
    }
    if (!codec_ready) {
        status.state = PlayerState::Error;
        status.error = PlayerError::CodecUnavailable;
        clear_spectrum();
        publish(true);
        return false;
    }
    if (!music_library_get(index, path, sizeof(path), name, sizeof(name))) {
        status.state = PlayerState::Error;
        status.error = PlayerError::TrackOutOfRange;
        clear_spectrum();
        publish(true);
        return false;
    }

    audio.stopSong();
    eof_received = false;
    clear_spectrum();
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
    clear_spectrum();

    xEventGroupClearBits(HardwareEventGroup, HW_EVENT_LIBRARY_READY);
    sd_ready = bsp_storage_available() || bsp_storage_init();
    if (sd_ready) xEventGroupSetBits(HardwareEventGroup, HW_EVENT_SD_READY);
    else xEventGroupClearBits(HardwareEventGroup, HW_EVENT_SD_READY);
    if (!sd_ready) {
        status.track_count = 0;
        status.file_name[0] = '\0';
        status.state = PlayerState::NoSd;
        status.error = PlayerError::SdUnavailable;
        build_shuffle(0);
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
    build_shuffle(status.track_index);
    Serial.printf("[PLAYER] scan complete, %u track(s)\n", status.track_count);
    publish(true);
}

bool move_shuffle(int direction, uint16_t *index) {
    if (index == nullptr || status.track_count == 0) {
        return false;
    }
    if (shuffle_count != status.track_count ||
        shuffle_order[shuffle_cursor] != status.track_index) {
        build_shuffle(status.track_index);
    }
    if (status.track_count == 1) {
        *index = status.track_index;
        return true;
    }
    if (direction < 0) {
        if (shuffle_cursor == 0) {
            return false;
        }
        --shuffle_cursor;
    } else if (shuffle_cursor + 1 < shuffle_count) {
        ++shuffle_cursor;
    } else {
        build_shuffle(status.track_index);
        shuffle_cursor = 1;
    }
    *index = shuffle_order[shuffle_cursor];
    return true;
}

void move_track(int direction, bool play, bool automatic = false) {
    if (status.track_count == 0) return;

    uint16_t index = status.track_index;
    if (automatic && status.playback_mode == PlaybackMode::RepeatOne) {
        index = status.track_index;
    } else if (status.playback_mode == PlaybackMode::Shuffle) {
        if (!move_shuffle(direction, &index)) {
            return;
        }
    } else {
        const int count = status.track_count;
        index = static_cast<uint16_t>((status.track_index + count + direction) % count);
    }

    if (play || track_started) start_track(index);
    else { select_track(index); publish(true); }
}

void handle_command(const PlayerCommand &command) {
    switch (command.type) {
        case PlayerCommandType::PlaySelected:
            if ((command.value >= 0) && (command.value < status.track_count)) {
                const uint16_t index = static_cast<uint16_t>(command.value);
                if (start_track(index) && status.playback_mode == PlaybackMode::Shuffle) {
                    build_shuffle(index);
                }
            } else {
                status.state = PlayerState::Error;
                status.error = PlayerError::TrackOutOfRange;
                clear_spectrum();
                publish(true);
            }
            break;
        case PlayerCommandType::Toggle:
            if (status.state == PlayerState::Playing || status.state == PlayerState::Paused) {
                if (audio.pauseResume()) {
                    status.state = (status.state == PlayerState::Playing)
                                       ? PlayerState::Paused : PlayerState::Playing;
                    if (status.state == PlayerState::Paused) clear_spectrum();
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
                clear_spectrum();
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
        case PlayerCommandType::CyclePlaybackMode:
            status.playback_mode = static_cast<PlaybackMode>(
                (static_cast<uint8_t>(status.playback_mode) + 1U) % 3U);
            if (status.playback_mode == PlaybackMode::Shuffle) {
                build_shuffle(status.track_index);
            }
            publish(true);
            break;
        case PlayerCommandType::Rescan: rescan_library(); break;
    }
}
}

void audio_process_extern(int16_t *buffer, uint16_t length, bool *continue_i2s) {
    if (continue_i2s != nullptr) {
        *continue_i2s = true;
    }
    analyze_pcm(buffer, length);
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
    status.playback_mode = PlaybackMode::RepeatAll;
    init_spectrum();
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
                move_track(1, true, true);
            } else if (track_started && !audio.isRunning()) {
                track_started = false;
                status.state = PlayerState::Error;
                status.error = PlayerError::DecodeStopped;
                clear_spectrum();
                publish(true);
            }
        }
        if (status.state == PlayerState::Playing) {
            publish(false);
        }
        vTaskDelay(status.state == PlayerState::Playing ? 1 : pdMS_TO_TICKS(10));
    }
}
