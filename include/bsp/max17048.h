#pragma once

#include <cstdint>

constexpr uint8_t MAX17048_I2C_ADDRESS = 0x36U;

bool max17048_init();
bool max17048_read(float *soc_percent, uint16_t *voltage_mv);
