#include "bsp/bsp_audio.h"

#include <Wire.h>
#include <WM8978.h>

#include "bsp/board_config.h"
#include "bsp/bsp_i2c.h"

#ifndef PROJECT_DEBUG_MODE
#define PROJECT_DEBUG_MODE 0
#endif

namespace {
WM8978 codec;
constexpr uint32_t AUDIO_POWER_OFF_MS = 100U;
constexpr uint32_t AUDIO_POWER_SETTLE_MS = 100U;
constexpr uint32_t AMPLIFIER_SETTLE_MS = 5U;
constexpr uint8_t CODEC_INIT_ATTEMPTS = 3U;
constexpr uint8_t SPEAKER_VOLUME = 55U;
constexpr uint8_t HEADPHONE_ACTIVE_VOLUME = 63U;
constexpr uint8_t HEADPHONE_MUTED_VOLUME = 0U;
bool output_muted = false;
bool output_mute_initialized = false;

uint8_t eq_gain_from_db(int8_t db) {
    return static_cast<uint8_t>(constrain(static_cast<int>(db) + 12, 0, 24));
}

void power_cycle_codec() {
    digitalWrite(BoardConfig::AmplifierEnable, LOW);
    output_mute_initialized = false;
    digitalWrite(BoardConfig::AudioPower, LOW);
    delay(AUDIO_POWER_OFF_MS);
    digitalWrite(BoardConfig::AudioPower, HIGH);
    Serial.printf("[AUDIO] power GPIO%u=HIGH amplifier GPIO%u=LOW, waiting %lu ms\n",
                  BoardConfig::AudioPower, BoardConfig::AmplifierEnable,
                  static_cast<unsigned long>(AUDIO_POWER_SETTLE_MS));
    delay(AUDIO_POWER_SETTLE_MS);
}

void apply_codec_settings_unlocked(int8_t bass_db, int8_t treble_db,
                                   uint8_t surround_depth) {
    codec.set3Ddir(1U);
    codec.setEQ1(0U, eq_gain_from_db(bass_db));
    codec.setEQ2(0U, 12U);
    codec.setEQ3(0U, 12U);
    codec.setEQ4(0U, 12U);
    codec.setEQ5(3U, eq_gain_from_db(treble_db));
    codec.set3D(static_cast<uint8_t>(constrain(static_cast<int>(surround_depth), 0, 15)));
}

void apply_output_route_unlocked(BspAudioPlaybackRoute route) {
    const bool speaker_enabled = route == BspAudioPlaybackRoute::Speaker;
    const bool headphones_enabled = route == BspAudioPlaybackRoute::Headphones;
    codec.setSPKvol(speaker_enabled ? SPEAKER_VOLUME : 0U);
    const uint8_t headphone_volume = headphones_enabled
                                         ? HEADPHONE_ACTIVE_VOLUME
                                         : HEADPHONE_MUTED_VOLUME;
    codec.setHPvol(headphone_volume, headphone_volume);
    codec.setPlaybackPower(route != BspAudioPlaybackRoute::Off,
                           speaker_enabled);
}
}

void bsp_audio_apply_codec_settings(int8_t bass_db, int8_t treble_db,
                                    uint8_t surround_depth) {
    if (!bsp_i2c_lock(pdMS_TO_TICKS(100U))) return;
    apply_codec_settings_unlocked(bass_db, treble_db, surround_depth);
    bsp_i2c_unlock();
}

bool bsp_audio_set_output_muted(bool muted) {
    if (output_mute_initialized && output_muted == muted) return true;
    if (!bsp_i2c_lock(pdMS_TO_TICKS(100U))) return false;
    codec.setDACMute(muted);
    output_muted = muted;
    output_mute_initialized = true;
    bsp_i2c_unlock();
    return true;
}

bool bsp_audio_set_playback_route(BspAudioPlaybackRoute route) {
    pinMode(BoardConfig::AmplifierEnable, OUTPUT);
    digitalWrite(BoardConfig::AmplifierEnable, LOW);

    if (!bsp_i2c_lock(pdMS_TO_TICKS(100U))) return false;
    codec.setDACMute(true);
    output_muted = true;
    output_mute_initialized = true;
    apply_output_route_unlocked(route);
    bsp_i2c_unlock();

    if (route == BspAudioPlaybackRoute::Off) return true;
    delay(AMPLIFIER_SETTLE_MS);
    if (!bsp_audio_set_output_muted(false)) return false;
    if (route == BspAudioPlaybackRoute::Speaker) {
        digitalWrite(BoardConfig::AmplifierEnable, HIGH);
    }
    return true;
}

void bsp_audio_power_on_early() {
    pinMode(BoardConfig::AmplifierEnable, OUTPUT);
    digitalWrite(BoardConfig::AmplifierEnable, LOW);
    pinMode(BoardConfig::AudioPower, OUTPUT);
    digitalWrite(BoardConfig::AudioPower, HIGH);
}

bool bsp_audio_codec_init() {
    pinMode(BoardConfig::AmplifierEnable, OUTPUT);
    digitalWrite(BoardConfig::AmplifierEnable, LOW);
    pinMode(BoardConfig::AudioPower, OUTPUT);
    digitalWrite(BoardConfig::AudioPower, HIGH);
    pinMode(BoardConfig::HeadphoneDetect, INPUT);
    Serial.printf("[AUDIO] power GPIO%u=HIGH since early boot, amplifier GPIO%u=LOW\n",
                  BoardConfig::AudioPower, BoardConfig::AmplifierEnable);

    bool wire_ready = false;
    uint8_t last_probe_result = 0xFFU;
    for (uint8_t attempt = 1U; attempt <= CODEC_INIT_ATTEMPTS; ++attempt) {
        if (attempt > 1U) {
            bsp_i2c_end();
            power_cycle_codec();
        }
        wire_ready = bsp_i2c_begin();
        last_probe_result = 0xFFU;
#if PROJECT_DEBUG_MODE
        if ((attempt == 1U) && wire_ready) {
            bsp_i2c_scan();
        }
#endif

        Serial.printf("[AUDIO] WM8978 init attempt=%u/%u\n", attempt, CODEC_INIT_ATTEMPTS);
        if (wire_ready) {
            last_probe_result = bsp_i2c_probe(WM8978_ADDR);
            if (last_probe_result == 0U && bsp_i2c_lock(pdMS_TO_TICKS(500U))) {
                const bool initialized = codec.begin();
                if (initialized) {
                    codec.setDACMute(true);
                    apply_output_route_unlocked(BspAudioPlaybackRoute::Off);
                    apply_codec_settings_unlocked(0, 0, 0);
                    output_muted = true;
                    output_mute_initialized = true;
                }
                bsp_i2c_unlock();
                if (initialized) return true;
            }
        }
        Serial.printf("[AUDIO] WM8978 attempt failed addr=0x%02X result=%u\n",
                      WM8978_ADDR, last_probe_result);
    }

    digitalWrite(BoardConfig::AmplifierEnable, LOW);
    Serial.printf("[AUDIO] WM8978 init failed attempts=%u addr=0x%02X result=%u "
                  "SDA=%d SCL=%d\n",
                  CODEC_INIT_ATTEMPTS, WM8978_ADDR, last_probe_result,
                  digitalRead(BoardConfig::I2cSda), digitalRead(BoardConfig::I2cScl));
    return false;
}

bool bsp_audio_configure_i2s(Audio &audio) {
    pinMode(BoardConfig::AmplifierEnable, OUTPUT);
    digitalWrite(BoardConfig::AmplifierEnable, LOW);
    const bool configured = audio.setPinout(BoardConfig::I2sBitClock,
                                            BoardConfig::I2sWordSelect,
                                            BoardConfig::I2sDataOut,
                                            BoardConfig::I2sDataIn,
                                            BoardConfig::I2sMasterClock);
    if (!configured) {
        Serial.println("[AUDIO] I2S configuration failed, amplifier remains disabled");
        return false;
    }

    Serial.printf("[AUDIO] I2S ready, amplifier GPIO%u=LOW\n",
                  BoardConfig::AmplifierEnable);
    return true;
}
