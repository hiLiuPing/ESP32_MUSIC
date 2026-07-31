#include "bsp/pcf8563.h"

#include "bsp/bsp_i2c.h"

namespace {
constexpr uint8_t REGISTER_CONTROL_STATUS_1 = 0x00U;
constexpr uint8_t REGISTER_SECONDS = 0x02U;
constexpr uint8_t CONTROL_STOP = 0x20U;
constexpr uint8_t SECONDS_VL = 0x80U;

uint8_t bcd_encode(uint8_t value) {
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

uint8_t bcd_decode(uint8_t value) {
    return static_cast<uint8_t>(((value >> 4U) * 10U) + (value & 0x0FU));
}

bool leap_year(uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

uint8_t days_in_month(uint16_t year, uint8_t month) {
    static constexpr uint8_t days[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                       31U, 31U, 30U, 31U, 30U, 31U};
    if (month < 1U || month > 12U) return 0U;
    if (month == 2U && leap_year(year)) return 29U;
    return days[month - 1U];
}

uint8_t weekday(uint16_t year, uint8_t month, uint8_t day) {
    static constexpr uint8_t offsets[] = {0U, 3U, 2U, 5U, 0U, 3U,
                                           5U, 1U, 4U, 6U, 2U, 4U};
    uint16_t adjusted_year = year;
    if (month < 3U) --adjusted_year;
    return static_cast<uint8_t>((adjusted_year + adjusted_year / 4U -
                                 adjusted_year / 100U + adjusted_year / 400U +
                                 offsets[month - 1U] + day) % 7U);
}
}

bool pcf8563_datetime_valid(const Pcf8563DateTime &value) {
    return value.year >= 2000U && value.year <= 2099U &&
           value.month >= 1U && value.month <= 12U &&
           value.day >= 1U && value.day <= days_in_month(value.year, value.month) &&
           value.hour < 24U && value.minute < 60U && value.second < 60U;
}

bool pcf8563_init() {
    uint8_t control = 0U;
    if (bsp_i2c_probe(PCF8563_I2C_ADDRESS) != 0U ||
        !bsp_i2c_read_register(PCF8563_I2C_ADDRESS,
                               REGISTER_CONTROL_STATUS_1, &control, 1U)) {
        return false;
    }
    control = static_cast<uint8_t>(control & ~CONTROL_STOP);
    return bsp_i2c_write_register(PCF8563_I2C_ADDRESS,
                                  REGISTER_CONTROL_STATUS_1, &control, 1U);
}

bool pcf8563_read(Pcf8563DateTime *value) {
    uint8_t data[7] = {};
    if (value == nullptr ||
        !bsp_i2c_read_register(PCF8563_I2C_ADDRESS, REGISTER_SECONDS,
                               data, sizeof(data)) ||
        (data[0] & SECONDS_VL) != 0U) {
        return false;
    }
    Pcf8563DateTime decoded = {};
    decoded.second = bcd_decode(static_cast<uint8_t>(data[0] & 0x7FU));
    decoded.minute = bcd_decode(static_cast<uint8_t>(data[1] & 0x7FU));
    decoded.hour = bcd_decode(static_cast<uint8_t>(data[2] & 0x3FU));
    decoded.day = bcd_decode(static_cast<uint8_t>(data[3] & 0x3FU));
    decoded.month = bcd_decode(static_cast<uint8_t>(data[5] & 0x1FU));
    decoded.year = static_cast<uint16_t>(2000U + bcd_decode(data[6]));
    if (!pcf8563_datetime_valid(decoded)) return false;
    *value = decoded;
    return true;
}

bool pcf8563_write(const Pcf8563DateTime &value) {
    if (!pcf8563_datetime_valid(value)) return false;
    const uint8_t data[7] = {
        bcd_encode(value.second),
        bcd_encode(value.minute),
        bcd_encode(value.hour),
        bcd_encode(value.day),
        weekday(value.year, value.month, value.day),
        bcd_encode(value.month),
        bcd_encode(static_cast<uint8_t>(value.year - 2000U)),
    };
    return bsp_i2c_write_register(PCF8563_I2C_ADDRESS, REGISTER_SECONDS,
                                  data, sizeof(data));
}
