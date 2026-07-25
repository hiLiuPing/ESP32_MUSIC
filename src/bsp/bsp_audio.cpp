#include "bsp/bsp_audio.h"

#include <WM8978.h>

#include "bsp/board_config.h"

namespace {
WM8978 codec;
}

bool bsp_audio_codec_init() {
    pinMode(BoardConfig::AudioPower, OUTPUT);
    pinMode(BoardConfig::AudioEnable, OUTPUT);
    pinMode(BoardConfig::HeadphoneDetect, INPUT);
    digitalWrite(BoardConfig::AudioPower, HIGH);
    digitalWrite(BoardConfig::AudioEnable, HIGH);

    if (!codec.begin(BoardConfig::I2cSda, BoardConfig::I2cScl)) {
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
