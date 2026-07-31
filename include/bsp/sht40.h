#pragma once

#include <cstdint>

constexpr uint8_t SHT40_I2C_ADDRESS = 0x44U;

uint8_t sht40_crc8(const uint8_t *data, uint8_t length);
bool sht40_init();
bool sht40_read(float *temperature_c, float *humidity_percent);
