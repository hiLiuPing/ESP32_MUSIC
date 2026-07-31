#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>

bool bsp_i2c_begin();
void bsp_i2c_end();
bool bsp_i2c_lock(TickType_t timeout = portMAX_DELAY);
void bsp_i2c_unlock();

uint8_t bsp_i2c_probe(uint8_t address);
void bsp_i2c_scan();

bool bsp_i2c_write(uint8_t address, const uint8_t *data, size_t length);
bool bsp_i2c_read(uint8_t address, uint8_t *data, size_t length);
bool bsp_i2c_write_register(uint8_t address, uint8_t reg,
                            const uint8_t *data, size_t length);
bool bsp_i2c_read_register(uint8_t address, uint8_t reg,
                           uint8_t *data, size_t length);
