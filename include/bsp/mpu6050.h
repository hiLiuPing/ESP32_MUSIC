#pragma once

#include <cstdint>

constexpr uint8_t MPU6050_I2C_ADDRESS_LOW = 0x68U;
constexpr uint8_t MPU6050_I2C_ADDRESS_HIGH = 0x69U;

struct Mpu6050Sample {
    int16_t acceleration_mg[3];
    int32_t angular_velocity_mdps[3];
    int16_t temperature_x100;
};

bool mpu6050_init(uint8_t *active_address);
bool mpu6050_read(uint8_t active_address, Mpu6050Sample *sample);
