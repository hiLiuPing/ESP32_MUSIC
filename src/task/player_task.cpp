#include <Arduino.h>

#include <Audio.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dsps_fft2r.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include "app/music_library.h"
#include "app/lyrics_service.h"
#include "app/audio_settings.h"
#include "bsp/bsp_audio.h"
#include "bsp/bsp_storage.h"
#include "task/task_system.h"

namespace {
constexpr size_t FFT_SIZE = 512;
constexpr int AUDIO_PSRAM_BUFFER_BYTES = 512 * 1024;
constexpr uint32_t SPECTRUM_INTERVAL_MS = 80;
constexpr uint32_t STATUS_INTERVAL_MS = 100;
constexpr uint32_t WEATHER_RECOVERY_GRACE_MS = 15000;
constexpr float SPECTRUM_MIN_HZ = 80.0F;
constexpr float SPECTRUM_MAX_HZ = 16000.0F;
constexpr float SPECTRUM_MIN_DB = -72.0F;
constexpr float SPECTRUM_MAX_DB = -12.0F;

Audio audio;
volatile bool eof_received = false;
PlayerStatus status = {};
bool sd_ready = false;
bool codec_ready = false;
bool i2s_ready = false;
bool track_started = false;
bool audio_input_uses_psram = false;
bool last_start_skipped = false;
bool skip_feedback_posted_in_pass = false;
bool weather_recovery_pending = false;
uint32_t weather_recovery_started_ms = 0U;
uint32_t weather_recovery_file_pos = 0U;
uint32_t last_sleep_timer_ms = 0U;
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
uint8_t attempted_tracks[(PLAYER_MAX_TRACKS + 7U) / 8U] = {};
char current_path[PLAYER_PATH_LENGTH] = {};

void publish(bool force = false);
void clear_spectrum();

void clear_weather_recovery() {
    weather_recovery_pending = false;
    weather_recovery_started_ms = 0U;
    weather_recovery_file_pos = 0U;
}

void log_psram_usage(const char *phase) {
    constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    Serial.printf("[PSRAM] %s free=%u largest=%u\n",
                  phase == nullptr ? "unknown" : phase,
                  static_cast<unsigned>(heap_caps_get_free_size(caps)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
}

bool ends_with_ignore_case(const char *path, const char *extension) {
    if (path == nullptr || extension == nullptr) return false;
    const size_t path_length = std::strlen(path);
    const size_t extension_length = std::strlen(extension);
    if (path_length < extension_length) return false;
    const char *suffix = path + path_length - extension_length;
    for (size_t index = 0U; index < extension_length; ++index) {
        if (std::tolower(static_cast<unsigned char>(suffix[index])) !=
            std::tolower(static_cast<unsigned char>(extension[index]))) {
            return false;
        }
    }
    return true;
}

void reset_track_attempts() {
    std::memset(attempted_tracks, 0, sizeof(attempted_tracks));
    skip_feedback_posted_in_pass = false;
}

bool track_was_attempted(uint16_t index) {
    return (attempted_tracks[index / 8U] & (1U << (index % 8U))) != 0U;
}

void mark_track_attempted(uint16_t index) {
    attempted_tracks[index / 8U] |= static_cast<uint8_t>(1U << (index % 8U));
}

void post_music_feedback(bool enabled, const char *text) {
    if (enabled && text != nullptr) {
        (void)system_notify_post(SystemNotifyType::Music, text);
    }
}

size_t utf8_sequence_length(char lead) {
    const uint8_t byte = static_cast<uint8_t>(lead);
    if ((byte & 0x80U) == 0U) return 1U;
    if ((byte & 0xE0U) == 0xC0U) return 2U;
    if ((byte & 0xF0U) == 0xE0U) return 3U;
    if ((byte & 0xF8U) == 0xF0U) return 4U;
    return 0U;
}

bool utf8_continuation(char byte) {
    return (static_cast<uint8_t>(byte) & 0xC0U) == 0x80U;
}

void append_utf8_truncated(char *destination, size_t destination_size,
                           const char *source) {
    if (destination == nullptr || destination_size == 0U || source == nullptr) {
        return;
    }
    size_t used = std::strlen(destination);
    if (used >= destination_size - 1U) return;

    while (*source != '\0') {
        const size_t bytes = utf8_sequence_length(*source);
        if (bytes == 0U || used + bytes >= destination_size) break;
        bool complete = true;
        for (size_t index = 1U; index < bytes; ++index) {
            if (source[index] == '\0' || !utf8_continuation(source[index])) {
                complete = false;
                break;
            }
        }
        if (!complete) break;
        std::memcpy(destination + used, source, bytes);
        used += bytes;
        destination[used] = '\0';
        source += bytes;
    }
}

void post_track_feedback(bool enabled, const char *action) {
    if (!enabled || action == nullptr) return;
    char text[sizeof(SystemNotifyMessage::text)] = {};
    std::snprintf(text, sizeof(text), "%s", action);
    if (status.file_name[0] != '\0') {
        append_utf8_truncated(text, sizeof(text), " ");
        append_utf8_truncated(text, sizeof(text), status.file_name);
    }
    post_music_feedback(true, text);
}

void post_volume_feedback(bool enabled, int16_t delta) {
    if (!enabled) return;
    char text[48] = {};
    const char *action = delta > 0 ? "音量加" : delta < 0 ? "音量减" : "音量";
    std::snprintf(text, sizeof(text), "%s %u/%u", action,
                  static_cast<unsigned>(status.volume),
                  static_cast<unsigned>(PLAYER_VOLUME_MAX));
    post_music_feedback(true, text);
}

void normalize_audio_settings(AudioSettings &settings) {
    settings.volume = static_cast<uint8_t>(
        constrain(static_cast<int>(settings.volume), PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX));
    if (static_cast<uint8_t>(settings.playback_mode) >
        static_cast<uint8_t>(PlaybackMode::Shuffle)) {
        settings.playback_mode = PlaybackMode::Shuffle;
    }
    settings.bass_db = static_cast<int8_t>(
        constrain(static_cast<int>(settings.bass_db), -12, 12));
    settings.treble_db = static_cast<int8_t>(
        constrain(static_cast<int>(settings.treble_db), -12, 12));
    settings.surround_depth = static_cast<uint8_t>(
        constrain(static_cast<int>(settings.surround_depth), 0, 15));
    settings.amplifier_enabled = settings.amplifier_enabled ? true : false;
}

AudioSettings current_audio_settings() {
    return AudioSettings{status.volume, status.playback_mode,
                         status.amplifier_enabled, status.bass_db,
                         status.treble_db, status.surround_depth,
                         status.sleep_timer_min};
}

void persist_runtime_audio_settings() {
    (void)audio_settings_update(current_audio_settings());
}

void arm_sleep_timer(uint16_t minutes) {
    status.sleep_timer_remaining_seconds = static_cast<uint32_t>(minutes) * 60UL;
    last_sleep_timer_ms = millis();
}

void service_sleep_timer() {
    const uint32_t now = millis();
    if (last_sleep_timer_ms == 0U) {
        last_sleep_timer_ms = now;
    }
    if (status.state != PlayerState::Playing ||
        status.sleep_timer_remaining_seconds == 0U) {
        last_sleep_timer_ms = now;
        return;
    }

    const uint32_t elapsed_seconds = (now - last_sleep_timer_ms) / 1000UL;
    if (elapsed_seconds == 0U) return;
    last_sleep_timer_ms += elapsed_seconds * 1000UL;
    if (elapsed_seconds < status.sleep_timer_remaining_seconds) {
        status.sleep_timer_remaining_seconds -= elapsed_seconds;
        return;
    }

    status.sleep_timer_remaining_seconds = 0U;
    audio.stopSong();
    eof_received = false;
    track_started = false;
    lyrics_service_clear();
    status.elapsed_seconds = 0U;
    status.duration_seconds = 0U;
    status.state = PlayerState::Stopped;
    status.error = PlayerError::None;
    clear_spectrum();
    Serial.println("[PLAYER] sleep timer expired, playback stopped");
    publish(true);
}

void apply_audio_settings(const AudioSettings &requested, bool persist,
                          bool restart_sleep_timer) {
    AudioSettings settings = requested;
    normalize_audio_settings(settings);
    status.volume = settings.volume;
    status.playback_mode = settings.playback_mode;
    status.amplifier_enabled = settings.amplifier_enabled;
    status.bass_db = settings.bass_db;
    status.treble_db = settings.treble_db;
    status.surround_depth = settings.surround_depth;
    status.sleep_timer_min = settings.sleep_timer_min;
    audio.setVolume(status.volume);
    if (codec_ready) {
        bsp_audio_apply_codec_settings(status.bass_db, status.treble_db,
                                       status.surround_depth);
        bsp_audio_apply_output_route(status.amplifier_enabled);
    }
    bsp_audio_set_amplifier_enabled(codec_ready && i2s_ready &&
                                    status.amplifier_enabled);
    if (persist) {
        (void)audio_settings_update(settings);
        if (restart_sleep_timer) {
            arm_sleep_timer(settings.sleep_timer_min);
        }
    }
    publish(true);
}

void clear_spectrum() {
    std::memset(status.spectrum, 0, sizeof(status.spectrum));
}

void publish(bool force) {
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
    constexpr float fft_scale_squared = fft_scale * fft_scale;
    constexpr float db_span = SPECTRUM_MAX_DB - SPECTRUM_MIN_DB;
    for (size_t band = 0; band < PLAYER_SPECTRUM_BANDS; ++band) {
        float peak_power = 0.0F;
        for (uint16_t bin = band_start[band]; bin < band_end[band]; ++bin) {
            const float real = fft_data[bin * 2];
            const float imaginary = fft_data[bin * 2 + 1];
            peak_power = std::max(peak_power,
                                  real * real + imaginary * imaginary);
        }
        const float db = 10.0F *
                         std::log10(peak_power / fft_scale_squared + 1.0e-18F);
        const float normalized = (db - SPECTRUM_MIN_DB) / db_span;
        status.spectrum[band] = static_cast<uint8_t>(
            constrain(static_cast<int>(normalized * 255.0F), 0, 255));
    }
}

void build_shuffle(uint16_t current_index) {
    shuffle_count = status.playlist_track_count;
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
    uint16_t global_index = 0U;
    char path[PLAYER_PATH_LENGTH] = {};
    if (!music_library_playlist_track_get(status.playlist_index, index,
                                          &global_index, path, sizeof(path),
                                          status.file_name,
                                          sizeof(status.file_name))) {
        return;
    }
    status.track_index = global_index;
    status.playlist_track_index = index;
    status.elapsed_seconds = 0;
    status.duration_seconds = 0;
}

bool start_track_once(uint16_t index) {
    char path[PLAYER_PATH_LENGTH] = {};
    char name[PLAYER_NAME_LENGTH] = {};
    uint16_t global_index = 0U;
    lyrics_service_clear();
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
    if (!music_library_playlist_track_get(status.playlist_index, index,
                                          &global_index, path, sizeof(path),
                                          name, sizeof(name))) {
        status.state = PlayerState::Error;
        status.error = PlayerError::TrackOutOfRange;
        clear_spectrum();
        publish(true);
        return false;
    }

    audio.stopSong();
    eof_received = false;
    clear_weather_recovery();
    clear_spectrum();
    status.track_index = global_index;
    status.playlist_track_index = index;
    std::snprintf(status.file_name, sizeof(status.file_name), "%s", name);
    std::snprintf(current_path, sizeof(current_path), "%s", path);
    if (ends_with_ignore_case(path, ".flac") && !audio_input_uses_psram) {
        track_started = false;
        status.state = PlayerState::Error;
        status.error = PlayerError::OpenFailed;
        Serial.printf("[PLAYER] skipped FLAC without PSRAM input buffer: %s\n", path);
        return false;
    }

    (void)lyrics_service_load(bsp_storage_fs(), path);
    if (!audio.connecttoFS(bsp_storage_fs(), path)) {
        track_started = false;
        status.state = PlayerState::Error;
        status.error = PlayerError::OpenFailed;
        Serial.printf("[PLAYER] failed to open %s\n", path);
        return false;
    }

    track_started = true;
    status.elapsed_seconds = 0;
    status.duration_seconds = 0;
    status.state = PlayerState::Playing;
    status.error = PlayerError::None;
    Serial.printf("[PLAYER] playing %s\n", path);
    publish(true);
    return true;
}

bool restart_current_track(uint32_t resume_file_pos) {
    if (!sd_ready || !codec_ready || current_path[0] == '\0') return false;
    audio.stopSong();
    eof_received = false;
    clear_spectrum();
    if (!audio.connecttoFS(bsp_storage_fs(), current_path, resume_file_pos)) {
        track_started = false;
        status.state = PlayerState::Error;
        status.error = PlayerError::OpenFailed;
        Serial.printf("[PLAYER] weather recovery reopen failed pos=%lu path=%s\n",
                      static_cast<unsigned long>(resume_file_pos), current_path);
        publish(true);
        return false;
    }
    track_started = true;
    status.state = PlayerState::Playing;
    status.error = PlayerError::None;
    Serial.printf("[PLAYER] weather recovery resumed pos=%lu path=%s\n",
                  static_cast<unsigned long>(resume_file_pos), current_path);
    publish(true);
    return true;
}

void set_no_playable_tracks() {
    audio.stopSong();
    eof_received = false;
    track_started = false;
    clear_weather_recovery();
    lyrics_service_clear();
    status.elapsed_seconds = 0U;
    status.duration_seconds = 0U;
    status.state = PlayerState::Error;
    status.error = PlayerError::NoPlayableTracks;
    clear_spectrum();
    Serial.println("[PLAYER] no playable tracks after one library pass");
    post_music_feedback(true, "没有可播放歌曲");
    publish(true);
}

bool start_track_with_fallback(uint16_t index, int direction = 1,
                               bool reset_attempts = true) {
    last_start_skipped = false;
    if (status.playlist_track_count == 0U) return false;
    if (reset_attempts) reset_track_attempts();

    const int step = direction < 0 ? -1 : 1;
    uint16_t candidate = index;
    bool skipped = !reset_attempts;
    for (uint16_t checked = 0U; checked < status.playlist_track_count; ++checked) {
        if (track_was_attempted(candidate)) {
            const int count = status.playlist_track_count;
            candidate = static_cast<uint16_t>((candidate + count + step) % count);
            continue;
        }
        mark_track_attempted(candidate);
        if (start_track_once(candidate)) {
            if (skipped) {
                last_start_skipped = true;
                if (!skip_feedback_posted_in_pass) {
                    skip_feedback_posted_in_pass = true;
                    post_music_feedback(true, "已跳过无法播放的文件");
                }
            }
            return true;
        }
        if (status.error != PlayerError::OpenFailed) return false;
        skipped = true;
        const int count = status.playlist_track_count;
        candidate = static_cast<uint16_t>((candidate + count + step) % count);
    }

    set_no_playable_tracks();
    return false;
}

void rescan_library(bool keep_stopped = false) {
    audio.stopSong();
    track_started = false;
    eof_received = false;
    clear_weather_recovery();
    lyrics_service_clear();
    current_path[0] = '\0';
    status.elapsed_seconds = 0;
    status.duration_seconds = 0;
    status.sleep_timer_remaining_seconds = 0U;
    status.error = PlayerError::None;
    reset_track_attempts();
    clear_spectrum();

    xEventGroupClearBits(HardwareEventGroup, HW_EVENT_LIBRARY_READY);
    sd_ready = bsp_storage_available() || bsp_storage_init();
    if (sd_ready) xEventGroupSetBits(HardwareEventGroup, HW_EVENT_SD_READY);
    else xEventGroupClearBits(HardwareEventGroup, HW_EVENT_SD_READY);
    if (!sd_ready) {
        status.track_count = 0;
        status.library_version = 0U;
        status.playlist_index = 0U;
        status.playlist_track_index = 0U;
        status.playlist_track_count = 0U;
        status.file_name[0] = '\0';
        status.state = PlayerState::NoSd;
        status.error = PlayerError::SdUnavailable;
        build_shuffle(0);
        publish(true);
        return;
    }

    music_library_scan(bsp_storage_fs());
    log_psram_usage("after library scan");
    status.library_version = music_library_version();
    status.track_count = static_cast<uint16_t>(music_library_count());
    status.playlist_index = 0U;
    status.playlist_track_index = 0U;
    status.playlist_track_count = status.track_count;
    if (status.track_count == 0) {
        status.track_index = 0;
        status.file_name[0] = '\0';
        status.state = PlayerState::Empty;
        status.error = PlayerError::EmptyLibrary;
    } else {
        select_track(0);
        status.state = keep_stopped ? PlayerState::Stopped
                                    : (codec_ready ? PlayerState::Ready : PlayerState::Error);
        status.error = codec_ready ? PlayerError::None : PlayerError::CodecUnavailable;
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_LIBRARY_READY);
    }
    build_shuffle(status.playlist_track_index);
    Serial.printf("[PLAYER] scan complete, %u track(s), %u playlist(s)\n",
                  status.track_count,
                  static_cast<unsigned>(music_library_playlist_count()));
    publish(true);
}

void stop_for_storage() {
    audio.stopSong();
    eof_received = false;
    track_started = false;
    clear_weather_recovery();
    lyrics_service_clear();
    current_path[0] = '\0';
    status.elapsed_seconds = 0U;
    status.duration_seconds = 0U;
    status.sleep_timer_remaining_seconds = 0U;
    status.state = status.track_count == 0U ? PlayerState::Empty : PlayerState::Stopped;
    status.error = status.track_count == 0U ? PlayerError::EmptyLibrary : PlayerError::None;
    clear_spectrum();
    Serial.println("[PLAYER] stopped for storage access");
    publish(true);
}

bool move_shuffle(int direction, uint16_t *index) {
    if (index == nullptr || status.playlist_track_count == 0) {
        return false;
    }
    if (shuffle_count != status.playlist_track_count ||
        shuffle_order[shuffle_cursor] != status.playlist_track_index) {
        build_shuffle(status.playlist_track_index);
    }
    if (status.playlist_track_count == 1) {
        *index = status.playlist_track_index;
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
        build_shuffle(status.playlist_track_index);
        shuffle_cursor = 1;
    }
    *index = shuffle_order[shuffle_cursor];
    return true;
}

bool move_track(int direction, bool play, bool automatic = false,
                bool current_failed = false) {
    if (status.playlist_track_count == 0) return false;

    uint16_t index = status.playlist_track_index;
    if (current_failed) {
        const int count = status.playlist_track_count;
        index = static_cast<uint16_t>((status.playlist_track_index + count + direction) % count);
    } else if (automatic && status.playback_mode == PlaybackMode::RepeatOne) {
        index = status.playlist_track_index;
    } else if (status.playback_mode == PlaybackMode::Shuffle) {
        if (!move_shuffle(direction, &index)) {
            return false;
        }
    } else {
        const int count = status.playlist_track_count;
        index = static_cast<uint16_t>((status.playlist_track_index + count + direction) % count);
    }

    if (play || track_started) {
        return start_track_with_fallback(index, direction, !current_failed);
    }
    select_track(index);
    publish(true);
    return true;
}

void handle_command(const PlayerCommand &command) {
    last_start_skipped = false;
    switch (command.type) {
        case PlayerCommandType::PlaySelected:
        {
            size_t playlist_track_count = 0U;
            const bool playlist_valid = music_library_playlist_get(
                command.playlist_index, nullptr, 0U, &playlist_track_count);
            if (playlist_valid &&
                command.playlist_track_index < playlist_track_count) {
                status.playlist_index = command.playlist_index;
                status.playlist_track_index = command.playlist_track_index;
                status.playlist_track_count =
                    static_cast<uint16_t>(playlist_track_count);
                const uint16_t index = command.playlist_track_index;
                if (start_track_with_fallback(index) &&
                    status.playback_mode == PlaybackMode::Shuffle) {
                    build_shuffle(status.playlist_track_index);
                }
            } else {
                status.state = PlayerState::Error;
                status.error = PlayerError::TrackOutOfRange;
                clear_spectrum();
                publish(true);
            }
            break;
        }
        case PlayerCommandType::Toggle:
        {
            bool succeeded = false;
            if (status.state == PlayerState::Playing || status.state == PlayerState::Paused) {
                if (audio.pauseResume()) {
                    status.state = (status.state == PlayerState::Playing)
                                       ? PlayerState::Paused : PlayerState::Playing;
                    if (status.state == PlayerState::Paused) clear_spectrum();
                    publish(true);
                    succeeded = true;
                }
            } else if (status.track_count > 0) {
                succeeded = start_track_with_fallback(status.playlist_track_index);
            }
            if (command.show_feedback) {
                if (succeeded && !last_start_skipped) {
                    post_music_feedback(true, status.state == PlayerState::Paused
                                                  ? "暂停" : "播放");
                } else if (status.track_count == 0) {
                    post_music_feedback(true, "没有可播放歌曲");
                } else if (status.error == PlayerError::None) {
                    post_music_feedback(true, status.state == PlayerState::Playing
                                                  ? "暂停失败" : "播放失败");
                }
            }
            break;
        }
        case PlayerCommandType::Play:
        {
            bool succeeded = false;
            if (status.state == PlayerState::Paused) {
                if (audio.pauseResume()) {
                    status.state = PlayerState::Playing;
                    publish(true);
                    succeeded = true;
                }
            } else if (status.track_count > 0) {
                succeeded = start_track_with_fallback(status.playlist_track_index);
            }
            if (command.show_feedback) {
                if (succeeded && !last_start_skipped) post_music_feedback(true, "播放");
                else if (status.track_count == 0) post_music_feedback(true, "没有可播放歌曲");
                else if (status.error == PlayerError::None) post_music_feedback(true, "播放失败");
            }
            break;
        }
        case PlayerCommandType::Pause:
        {
            bool succeeded = false;
            if ((status.state == PlayerState::Playing) && audio.pauseResume()) {
                status.state = PlayerState::Paused;
                clear_spectrum();
                publish(true);
                succeeded = true;
            }
            if (command.show_feedback) {
                post_music_feedback(true, succeeded ? "暂停" : "暂停失败");
            }
            break;
        }
        case PlayerCommandType::Previous:
        {
            const bool succeeded = move_track(-1, true);
            if (command.show_feedback) {
                if (succeeded && !last_start_skipped) post_track_feedback(true, "上一曲");
                else if (status.track_count == 0) post_music_feedback(true, "没有可播放歌曲");
                else if (status.error == PlayerError::None) post_music_feedback(true, "上一曲失败");
            }
            break;
        }
        case PlayerCommandType::Next:
        {
            const bool succeeded = move_track(1, true);
            if (command.show_feedback) {
                if (succeeded && !last_start_skipped) post_track_feedback(true, "下一曲");
                else if (status.track_count == 0) post_music_feedback(true, "没有可播放歌曲");
                else if (status.error == PlayerError::None) post_music_feedback(true, "下一曲失败");
            }
            break;
        }
        case PlayerCommandType::SetVolume:
            status.volume = constrain(command.value, PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX);
            audio.setVolume(status.volume);
            persist_runtime_audio_settings();
            publish(true);
            post_volume_feedback(command.show_feedback, 0);
            break;
        case PlayerCommandType::ChangeVolume:
            status.volume = constrain(static_cast<int>(status.volume) + command.value,
                                      PLAYER_VOLUME_MIN, PLAYER_VOLUME_MAX);
            audio.setVolume(status.volume);
            persist_runtime_audio_settings();
            publish(true);
            post_volume_feedback(command.show_feedback, command.value);
            break;
        case PlayerCommandType::CyclePlaybackMode:
            status.playback_mode = static_cast<PlaybackMode>(
                (static_cast<uint8_t>(status.playback_mode) + 1U) % 3U);
            if (status.playback_mode == PlaybackMode::Shuffle) {
                build_shuffle(status.playlist_track_index);
            }
            persist_runtime_audio_settings();
            publish(true);
            break;
        case PlayerCommandType::ApplyAudioSettings:
            apply_audio_settings(command.audio_settings,
                                 command.persist_audio_settings,
                                 command.restart_sleep_timer);
            break;
        case PlayerCommandType::StopForStorage: stop_for_storage(); break;
        case PlayerCommandType::RefreshLibraryStopped: rescan_library(true); break;
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
    audio.setBufsize(-1, AUDIO_PSRAM_BUFFER_BYTES);
    log_psram_usage("before audio input buffer");
    const bool audio_input_ready = audio.initInputBuffer();
    audio_input_uses_psram = audio.inputBufferUsesPSRAM();
    Serial.printf("[PLAYER] audio input buffer request=%d bytes ready=%s memory=%s\n",
                  AUDIO_PSRAM_BUFFER_BYTES,
                  audio_input_ready ? "yes" : "no",
                  audio_input_uses_psram ? "PSRAM" : "internal");
    log_psram_usage("after audio input buffer");

    audio_settings_init();
    const AudioSettings saved_audio_settings = audio_settings_get();
    std::memset(&status, 0, sizeof(status));
    status.state = PlayerState::Initializing;
    status.volume = saved_audio_settings.volume;
    status.playback_mode = saved_audio_settings.playback_mode;
    status.amplifier_enabled = saved_audio_settings.amplifier_enabled;
    status.bass_db = saved_audio_settings.bass_db;
    status.treble_db = saved_audio_settings.treble_db;
    status.surround_depth = saved_audio_settings.surround_depth;
    status.sleep_timer_min = saved_audio_settings.sleep_timer_min;
    status.sleep_timer_remaining_seconds = 0U;
    last_sleep_timer_ms = millis();
    init_spectrum();
    publish(true);

    xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);
    const EventBits_t bits = xEventGroupGetBits(HardwareEventGroup);
    sd_ready = (bits & HW_EVENT_SD_READY) != 0;
    codec_ready = (bits & HW_EVENT_CODEC_READY) != 0;
    if (codec_ready) {
        i2s_ready = bsp_audio_configure_i2s(audio,
                                            saved_audio_settings.amplifier_enabled);
        if (!i2s_ready) codec_ready = false;
        if (i2s_ready) {
            bsp_audio_apply_output_route(saved_audio_settings.amplifier_enabled);
        }
    }
    audio.setVolume(status.volume);
    rescan_library();

    for (;;) {
        PlayerCommand command = {};
        while (xQueueReceive(PlayerCommandQueue, &command, 0) == pdTRUE) {
            handle_command(command);
        }

        service_sleep_timer();

        if (status.state == PlayerState::Playing) {
            audio.loop();
            if (eof_received) {
                eof_received = false;
                track_started = false;
                clear_weather_recovery();
                move_track(1, true, true);
            } else if (track_started && !audio.isRunning()) {
                const bool weather_busy = weather_sync_is_busy();
                if (weather_busy || weather_recovery_pending) {
                    const uint32_t now = millis();
                    if (!weather_recovery_pending) {
                        weather_recovery_pending = true;
                        weather_recovery_started_ms = now;
                        weather_recovery_file_pos = audio.getFilePos();
                        clear_spectrum();
                        Serial.printf("[PLAYER] decode paused during weather sync pos=%lu\n",
                                      static_cast<unsigned long>(weather_recovery_file_pos));
                        publish(true);
                    }
                    if (weather_busy &&
                        static_cast<uint32_t>(now - weather_recovery_started_ms) <
                        WEATHER_RECOVERY_GRACE_MS) {
                        vTaskDelay(pdMS_TO_TICKS(20));
                        continue;
                    }
                    const uint32_t resume_pos = weather_recovery_file_pos;
                    clear_weather_recovery();
                    if (resume_pos != 0U && restart_current_track(resume_pos)) {
                        continue;
                    }
                }
                track_started = false;
                status.error = PlayerError::DecodeStopped;
                clear_spectrum();
                Serial.printf("[PLAYER] decode stopped for track %u, trying next\n",
                              static_cast<unsigned>(status.track_index));
                move_track(1, true, true, true);
            }
            if (audio.isRunning()) clear_weather_recovery();
        }
        if (status.state == PlayerState::Playing) {
            publish(false);
        }
        vTaskDelay(status.state == PlayerState::Playing ? 1 : pdMS_TO_TICKS(10));
    }
}
