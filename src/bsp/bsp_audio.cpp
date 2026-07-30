#include "bsp/bsp_audio.h"

#include <Wire.h>
#include <WM8978.h>

#include "bsp/board_config.h"

namespace {
WM8978 codec;
constexpr uint32_t I2C_FREQUENCY = 100000U;
constexpr uint32_t AUDIO_POWER_OFF_MS = 100U;
constexpr uint32_t AUDIO_POWER_SETTLE_MS = 100U;
constexpr uint32_t AMPLIFIER_SETTLE_MS = 5U;
constexpr uint8_t CODEC_INIT_ATTEMPTS = 3U;
constexpr uint8_t SPEAKER_VOLUME = 55U;
constexpr uint8_t HEADPHONE_ACTIVE_VOLUME = 63U;
constexpr uint8_t HEADPHONE_MUTED_VOLUME = 0U;

uint8_t eq_gain_from_db(int8_t db) {
    return static_cast<uint8_t>(constrain(static_cast<int>(db) + 12, 0, 24));
}

bool start_i2c_bus() {
    const bool ready = Wire.begin(BoardConfig::I2cSda, BoardConfig::I2cScl, I2C_FREQUENCY);
    Serial.printf("[I2C] begin SDA=%u SCL=%u frequency=%lu result=%s\n",
                  BoardConfig::I2cSda, BoardConfig::I2cScl,
                  static_cast<unsigned long>(I2C_FREQUENCY),
                  ready ? "ready" : "failed");
    return ready;
}

uint8_t probe_i2c_address(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission();
}

void scan_i2c_bus() {
    uint8_t devices = 0U;
    Serial.println("[I2C] scan start range=0x08-0x77");
    for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
        const uint8_t result = probe_i2c_address(address);
        if (result == 0U) {
            ++devices;
            Serial.printf("[I2C] found address=0x%02X\n", address);
        } else if ((result != 2U) && (result != 3U)) {
            Serial.printf("[I2C] scan error address=0x%02X result=%u\n", address, result);
        }
    }
    if (devices == 0U) {
        Serial.println("[I2C] no devices found");
    }
    Serial.printf("[I2C] scan complete devices=%u\n", devices);
}

void power_cycle_codec() {
    digitalWrite(BoardConfig::AmplifierEnable, LOW);
    digitalWrite(BoardConfig::AudioPower, LOW);
    delay(AUDIO_POWER_OFF_MS);
    digitalWrite(BoardConfig::AudioPower, HIGH);
    Serial.printf("[AUDIO] power GPIO%u=HIGH amplifier GPIO%u=LOW, waiting %lu ms\n",
                  BoardConfig::AudioPower, BoardConfig::AmplifierEnable,
                  static_cast<unsigned long>(AUDIO_POWER_SETTLE_MS));
    delay(AUDIO_POWER_SETTLE_MS);
}
}

void bsp_audio_apply_codec_settings(int8_t bass_db, int8_t treble_db,
                                    uint8_t surround_depth) {
    codec.set3Ddir(1U);  // Apply playback effects to the DAC path.
    codec.setEQ1(0U, eq_gain_from_db(bass_db));
    codec.setEQ2(0U, 12U);
    codec.setEQ3(0U, 12U);
    codec.setEQ4(0U, 12U);
    codec.setEQ5(3U, eq_gain_from_db(treble_db));
    codec.set3D(static_cast<uint8_t>(constrain(static_cast<int>(surround_depth), 0, 15)));
}

void bsp_audio_set_amplifier_enabled(bool enabled) {
    pinMode(BoardConfig::AmplifierEnable, OUTPUT);
    digitalWrite(BoardConfig::AmplifierEnable, enabled ? HIGH : LOW);
}

void bsp_audio_apply_output_route(bool amplifier_enabled) {
    const uint8_t headphone_volume = amplifier_enabled
                                         ? HEADPHONE_MUTED_VOLUME
                                         : HEADPHONE_ACTIVE_VOLUME;
    codec.setHPvol(headphone_volume, headphone_volume);
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
            Wire.end();
            power_cycle_codec();
        }
        wire_ready = start_i2c_bus();
        last_probe_result = 0xFFU;
        if ((attempt == 1U) && wire_ready) {
            scan_i2c_bus();
        }

        Serial.printf("[AUDIO] WM8978 init attempt=%u/%u\n", attempt, CODEC_INIT_ATTEMPTS);
        if (wire_ready) {
            last_probe_result = probe_i2c_address(WM8978_ADDR);
            if ((last_probe_result == 0U) && codec.begin()) {
                codec.setSPKvol(SPEAKER_VOLUME);
                bsp_audio_apply_output_route(true);
                bsp_audio_apply_codec_settings(0, 0, 0);
                return true;
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

bool bsp_audio_configure_i2s(Audio &audio, bool amplifier_enabled) {
    bsp_audio_set_amplifier_enabled(false);
    const bool configured = audio.setPinout(BoardConfig::I2sBitClock,
                                            BoardConfig::I2sWordSelect,
                                            BoardConfig::I2sDataOut,
                                            BoardConfig::I2sDataIn,
                                            BoardConfig::I2sMasterClock);
    if (!configured) {
        Serial.println("[AUDIO] I2S configuration failed, amplifier remains disabled");
        return false;
    }

    delay(AMPLIFIER_SETTLE_MS);
    bsp_audio_set_amplifier_enabled(amplifier_enabled);
    Serial.printf("[AUDIO] I2S ready, amplifier GPIO%u=%s\n",
                  BoardConfig::AmplifierEnable,
                  amplifier_enabled ? "HIGH" : "LOW");
    return true;
}
