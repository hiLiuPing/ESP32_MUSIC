#include "bsp/sht40.h"

#include <Arduino.h>

#include "bsp/bsp_i2c.h"

namespace {
constexpr uint8_t COMMAND_HIGH_PRECISION = 0xFDU;
constexpr uint8_t COMMAND_SOFT_RESET = 0x94U;
}

uint8_t sht40_crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0xFFU;
    if (data == nullptr) return crc;
    for (uint8_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x80U) != 0U
                      ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                      : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

bool sht40_init() {
    if (bsp_i2c_probe(SHT40_I2C_ADDRESS) != 0U ||
        !bsp_i2c_write(SHT40_I2C_ADDRESS, &COMMAND_SOFT_RESET, 1U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2U));
    return true;
}

bool sht40_read(float *temperature_c, float *humidity_percent) {
    if (temperature_c == nullptr || humidity_percent == nullptr ||
        !bsp_i2c_write(SHT40_I2C_ADDRESS, &COMMAND_HIGH_PRECISION, 1U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10U));
    uint8_t data[6] = {};
    if (!bsp_i2c_read(SHT40_I2C_ADDRESS, data, sizeof(data)) ||
        sht40_crc8(data, 2U) != data[2] ||
        sht40_crc8(data + 3, 2U) != data[5]) {
        return false;
    }
    const uint16_t raw_temperature =
        static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
    const uint16_t raw_humidity =
        static_cast<uint16_t>((static_cast<uint16_t>(data[3]) << 8U) | data[4]);
    *temperature_c = -45.0f + (175.0f * raw_temperature / 65535.0f);
    *humidity_percent = constrain(-6.0f + (125.0f * raw_humidity / 65535.0f),
                                  0.0f, 100.0f);
    return true;
}
