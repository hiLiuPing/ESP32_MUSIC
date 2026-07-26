#pragma once

#include <Arduino.h>

namespace BoardConfig {
constexpr uint8_t DisplayDc = 5;
constexpr uint8_t DisplayReset = 4;
constexpr uint8_t DisplayCs = 3;
constexpr uint8_t DisplayClock = 6;
constexpr uint8_t DisplayData = 1;

constexpr uint8_t SdMiso = 41;
constexpr uint8_t SdMosi = 39;
constexpr uint8_t SdClock = 40;
constexpr uint8_t SdCs = 38;
constexpr uint32_t SdFrequency = 40000000;

constexpr uint8_t I2sDataOut = 12;
constexpr uint8_t I2sBitClock = 14;
constexpr uint8_t I2sWordSelect = 15;
constexpr uint8_t I2sMasterClock = 11;
constexpr uint8_t I2sDataIn = 13;

constexpr uint8_t I2cSda = 17;
constexpr uint8_t I2cScl = 16;
constexpr uint8_t AudioPower = 18;
constexpr uint8_t AudioEnable = 21;
constexpr uint8_t HeadphoneDetect = 47;
constexpr uint8_t StatusLed = 48;

constexpr uint8_t KeyLeft = 8;
constexpr uint8_t Keyright = 2;
constexpr uint8_t KeyMidle = 7;
}
