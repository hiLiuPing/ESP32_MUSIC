#include "bsp/bsp_i2c.h"

#include <Wire.h>
#include <freertos/semphr.h>

#include "bsp/board_config.h"

namespace {
constexpr uint32_t I2C_FREQUENCY = 100000U;
SemaphoreHandle_t i2c_mutex = nullptr;

bool ensure_mutex() {
    if (i2c_mutex == nullptr) {
        i2c_mutex = xSemaphoreCreateMutex();
    }
    return i2c_mutex != nullptr;
}

bool read_locked(uint8_t address, uint8_t *data, size_t length) {
    if (data == nullptr || length == 0U || length > 255U) return false;
    const size_t received = Wire.requestFrom(address, static_cast<uint8_t>(length));
    if (received != length) {
        while (Wire.available() > 0) (void)Wire.read();
        return false;
    }
    for (size_t i = 0U; i < length; ++i) {
        if (Wire.available() <= 0) return false;
        data[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}
}

bool bsp_i2c_lock(TickType_t timeout) {
    return ensure_mutex() && xSemaphoreTake(i2c_mutex, timeout) == pdTRUE;
}

void bsp_i2c_unlock() {
    if (i2c_mutex != nullptr) xSemaphoreGive(i2c_mutex);
}

bool bsp_i2c_begin() {
    if (!bsp_i2c_lock()) return false;
    const bool ready = Wire.begin(BoardConfig::I2cSda, BoardConfig::I2cScl,
                                  I2C_FREQUENCY);
    bsp_i2c_unlock();
    Serial.printf("[I2C] begin SDA=%u SCL=%u frequency=%lu result=%s\n",
                  BoardConfig::I2cSda, BoardConfig::I2cScl,
                  static_cast<unsigned long>(I2C_FREQUENCY),
                  ready ? "ready" : "failed");
    return ready;
}

void bsp_i2c_end() {
    if (!bsp_i2c_lock()) return;
    Wire.end();
    bsp_i2c_unlock();
}

uint8_t bsp_i2c_probe(uint8_t address) {
    if (!bsp_i2c_lock(pdMS_TO_TICKS(100U))) return 4U;
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    bsp_i2c_unlock();
    return result;
}

void bsp_i2c_scan() {
    uint8_t devices = 0U;
    Serial.println("[I2C] scan start range=0x08-0x77");
    for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
        const uint8_t result = bsp_i2c_probe(address);
        if (result == 0U) {
            ++devices;
            Serial.printf("[I2C] found address=0x%02X\n", address);
        } else if (result != 2U && result != 3U) {
            Serial.printf("[I2C] scan error address=0x%02X result=%u\n",
                          address, result);
        }
    }
    if (devices == 0U) Serial.println("[I2C] no devices found");
    Serial.printf("[I2C] scan complete devices=%u\n", devices);
}

bool bsp_i2c_write(uint8_t address, const uint8_t *data, size_t length) {
    if (data == nullptr || length == 0U || !bsp_i2c_lock(pdMS_TO_TICKS(100U))) {
        return false;
    }
    Wire.beginTransmission(address);
    const size_t written = Wire.write(data, length);
    const uint8_t result = Wire.endTransmission();
    bsp_i2c_unlock();
    return written == length && result == 0U;
}

bool bsp_i2c_read(uint8_t address, uint8_t *data, size_t length) {
    if (!bsp_i2c_lock(pdMS_TO_TICKS(100U))) return false;
    const bool result = read_locked(address, data, length);
    bsp_i2c_unlock();
    return result;
}

bool bsp_i2c_write_register(uint8_t address, uint8_t reg,
                            const uint8_t *data, size_t length) {
    if ((length != 0U && data == nullptr) ||
        !bsp_i2c_lock(pdMS_TO_TICKS(100U))) {
        return false;
    }
    Wire.beginTransmission(address);
    const size_t reg_written = Wire.write(reg);
    const size_t data_written = length == 0U ? 0U : Wire.write(data, length);
    const uint8_t result = Wire.endTransmission();
    bsp_i2c_unlock();
    return reg_written == 1U && data_written == length && result == 0U;
}

bool bsp_i2c_read_register(uint8_t address, uint8_t reg,
                           uint8_t *data, size_t length) {
    if (data == nullptr || length == 0U ||
        !bsp_i2c_lock(pdMS_TO_TICKS(100U))) {
        return false;
    }
    Wire.beginTransmission(address);
    const size_t written = Wire.write(reg);
    const uint8_t result = Wire.endTransmission(false);
    const bool success = written == 1U && result == 0U &&
                         read_locked(address, data, length);
    bsp_i2c_unlock();
    return success;
}
