#include "bsp/mpu6050.h"

#include <Arduino.h>

#include "bsp/bsp_i2c.h"

namespace {
constexpr uint8_t REGISTER_SMPLRT_DIV = 0x19U;
constexpr uint8_t REGISTER_CONFIG = 0x1AU;
constexpr uint8_t REGISTER_GYRO_CONFIG = 0x1BU;
constexpr uint8_t REGISTER_ACCEL_CONFIG = 0x1CU;
constexpr uint8_t REGISTER_ACCEL_XOUT_H = 0x3BU;
constexpr uint8_t REGISTER_PWR_MGMT_1 = 0x6BU;
constexpr uint8_t REGISTER_WHO_AM_I = 0x75U;

int16_t decode_i16(const uint8_t *data) {
    return static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8U) |
                                data[1]);
}

bool write_register(uint8_t address, uint8_t reg, uint8_t value) {
    return bsp_i2c_write_register(address, reg, &value, 1U);
}

int16_t acceleration_to_mg(int16_t raw) {
    const int32_t scaled = static_cast<int32_t>(raw) * 1000;
    return static_cast<int16_t>((scaled >= 0 ? scaled + 8192 : scaled - 8192) /
                                16384);
}

int32_t gyro_to_mdps(int16_t raw) {
    const int32_t scaled = static_cast<int32_t>(raw) * 1000;
    return (scaled >= 0 ? scaled + 65 : scaled - 65) / 131;
}
}

bool mpu6050_init(uint8_t *active_address) {
    if (active_address == nullptr) return false;
    const uint8_t addresses[] = {MPU6050_I2C_ADDRESS_LOW,
                                 MPU6050_I2C_ADDRESS_HIGH};
    for (uint8_t address : addresses) {
        uint8_t identity = 0U;
        if (bsp_i2c_probe(address) != 0U ||
            !bsp_i2c_read_register(address, REGISTER_WHO_AM_I, &identity, 1U) ||
            (identity & 0x7EU) != 0x68U) {
            continue;
        }
        if (!write_register(address, REGISTER_PWR_MGMT_1, 0x01U)) continue;
        vTaskDelay(pdMS_TO_TICKS(10U));
        if (!write_register(address, REGISTER_SMPLRT_DIV, 0x07U) ||
            !write_register(address, REGISTER_CONFIG, 0x03U) ||
            !write_register(address, REGISTER_GYRO_CONFIG, 0x00U) ||
            !write_register(address, REGISTER_ACCEL_CONFIG, 0x00U)) {
            continue;
        }
        *active_address = address;
        return true;
    }
    return false;
}

bool mpu6050_read(uint8_t active_address, Mpu6050Sample *sample) {
    uint8_t data[14] = {};
    if (sample == nullptr ||
        (active_address != MPU6050_I2C_ADDRESS_LOW &&
         active_address != MPU6050_I2C_ADDRESS_HIGH) ||
        !bsp_i2c_read_register(active_address, REGISTER_ACCEL_XOUT_H,
                               data, sizeof(data))) {
        return false;
    }
    const int16_t raw_temperature = decode_i16(data + 6);
    for (uint8_t axis = 0U; axis < 3U; ++axis) {
        sample->acceleration_mg[axis] = acceleration_to_mg(
            decode_i16(data + static_cast<size_t>(axis) * 2U));
        sample->angular_velocity_mdps[axis] = gyro_to_mdps(
            decode_i16(data + 8U + static_cast<size_t>(axis) * 2U));
    }
    sample->temperature_x100 = static_cast<int16_t>(
        3653 + ((static_cast<int32_t>(raw_temperature) * 100) / 340));
    return true;
}
