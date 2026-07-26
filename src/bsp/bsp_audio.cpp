#include "bsp/bsp_audio.h"

#include <Wire.h>
#include <WM8978.h>

#include "bsp/board_config.h"

namespace {
WM8978 codec;
constexpr uint32_t AUDIO_POWER_SETTLE_MS = 20U;

void print_codec_probe() {
    const bool wire_ready = Wire.begin(BoardConfig::I2cSda, BoardConfig::I2cScl, 100000U);
    uint8_t result = 0xFFU;
    if (wire_ready) {
        Wire.beginTransmission(WM8978_ADDR);
        result = Wire.endTransmission();
    }
    Serial.printf("[AUDIO] I2C probe SDA=%u SCL=%u addr=0x%02X wire=%s result=%u\n",
                  BoardConfig::I2cSda, BoardConfig::I2cScl, WM8978_ADDR,
                  wire_ready ? "ready" : "failed", result);
}
}

bool bsp_audio_codec_init() {
    pinMode(BoardConfig::AudioPower, OUTPUT);
    pinMode(BoardConfig::AudioEnable, OUTPUT);
    pinMode(BoardConfig::HeadphoneDetect, INPUT);
    digitalWrite(BoardConfig::AudioPower, HIGH);
    digitalWrite(BoardConfig::AudioEnable, HIGH);
    Serial.printf("[AUDIO] power GPIO%u=HIGH enable GPIO%u=HIGH, waiting %lu ms\n",
                  BoardConfig::AudioPower, BoardConfig::AudioEnable,
                  static_cast<unsigned long>(AUDIO_POWER_SETTLE_MS));
    delay(AUDIO_POWER_SETTLE_MS);

    if (!codec.begin(BoardConfig::I2cSda, BoardConfig::I2cScl)) {
        print_codec_probe();
        return false;
    }
    codec.setSPKvol(0);
    codec.setHPvol(20, 20);
    return true;
}

bool bsp_audio_configure_i2s(Audio &audio) {
    return audio.setPinout(BoardConfig::I2sBitClock,
                           BoardConfig::I2sWordSelect,
                           BoardConfig::I2sDataOut,
                           BoardConfig::I2sDataIn,
                           BoardConfig::I2sMasterClock);
}
