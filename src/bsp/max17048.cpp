#include "bsp/max17048.h"

#include <Arduino.h>

#include "bsp/bsp_i2c.h"

namespace {
constexpr uint8_t REGISTER_VCELL = 0x02U;
constexpr uint8_t REGISTER_SOC = 0x04U;
constexpr uint8_t REGISTER_COMMAND = 0xFEU;

bool read_u16(uint8_t reg, uint16_t *value) {
    uint8_t data[2] = {};
    if (value == nullptr ||
        !bsp_i2c_read_register(MAX17048_I2C_ADDRESS, reg, data, sizeof(data))) {
        return false;
    }
    *value = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) |
                                   data[1]);
    return true;
}
}

bool max17048_init() {
    const uint8_t reset[2] = {0x00U, 0x54U};
    if (bsp_i2c_probe(MAX17048_I2C_ADDRESS) != 0U ||
        !bsp_i2c_write_register(MAX17048_I2C_ADDRESS, REGISTER_COMMAND,
                                reset, sizeof(reset))) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5U));
    return true;
}

bool max17048_read(float *soc_percent, uint16_t *voltage_mv) {
    uint16_t raw_soc = 0U;
    uint16_t raw_vcell = 0U;
    if (soc_percent == nullptr || voltage_mv == nullptr ||
        !read_u16(REGISTER_SOC, &raw_soc) ||
        !read_u16(REGISTER_VCELL, &raw_vcell)) {
        return false;
    }
    *soc_percent = constrain(static_cast<float>(raw_soc) / 256.0f, 0.0f, 100.0f);
    const float millivolts = static_cast<float>(raw_vcell >> 4U) * 1.25f;
    *voltage_mv = static_cast<uint16_t>(millivolts + 0.5f);
    return true;
}
