#pragma once

#include <cstdint>

constexpr uint8_t PCF8563_I2C_ADDRESS = 0x51U;

struct Pcf8563DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

bool pcf8563_datetime_valid(const Pcf8563DateTime &value);
bool pcf8563_init();
bool pcf8563_read(Pcf8563DateTime *value);
bool pcf8563_write(const Pcf8563DateTime &value);
