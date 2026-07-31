#include "app/sensor_service.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <freertos/semphr.h>

#include "bsp/max17048.h"
#include "bsp/mpu6050.h"
#include "bsp/pcf8563.h"
#include "bsp/sht40.h"

namespace {
constexpr uint32_t REINIT_INTERVAL_MS = 5000U;
constexpr uint32_t SLOW_STALE_MS = 5000U;
constexpr uint32_t MOTION_STALE_MS = 300U;

struct DeviceState {
    bool initialized;
    bool attempted;
    uint32_t last_attempt_ms;
};

portMUX_TYPE snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t service_mutex = nullptr;
SensorSnapshot current_snapshot = {};
DeviceState environment_device = {};
DeviceState battery_device = {};
DeviceState rtc_device = {};
DeviceState motion_device = {};
uint8_t motion_address = 0U;

class ServiceGuard {
  public:
    ServiceGuard()
        : locked_(service_mutex != nullptr &&
                  xSemaphoreTakeRecursive(service_mutex,
                                          pdMS_TO_TICKS(250U)) == pdTRUE) {}
    ~ServiceGuard() {
        if (locked_) xSemaphoreGiveRecursive(service_mutex);
    }
    explicit operator bool() const { return locked_; }

  private:
    bool locked_;
};

bool retry_due(const DeviceState &device, uint32_t now) {
    return !device.attempted ||
           static_cast<uint32_t>(now - device.last_attempt_ms) >= REINIT_INTERVAL_MS;
}

template <typename Initializer>
bool ensure_device(DeviceState *device, uint32_t now, Initializer initializer) {
    if (device->initialized) return true;
    if (!retry_due(*device, now)) return false;
    device->attempted = true;
    device->last_attempt_ms = now;
    device->initialized = initializer();
    return device->initialized;
}

void record_result(SensorHealth *health, bool success, uint32_t now) {
    if (success) {
        health->valid = true;
        health->stale = false;
        health->consecutive_failures = 0U;
        health->last_success_ms = now;
    } else if (health->consecutive_failures < UINT16_MAX) {
        ++health->consecutive_failures;
    }
}

void update_stale(SensorHealth *health, uint32_t now, uint32_t threshold_ms) {
    if (health->valid &&
        static_cast<uint32_t>(now - health->last_success_ms) >= threshold_ms) {
        health->stale = true;
    }
}

AppTime app_time_from_pcf(const Pcf8563DateTime &value, uint32_t version) {
    AppTime result = {};
    result.version = version;
    result.year = value.year;
    result.month = value.month;
    result.day = value.day;
    result.hour = value.hour;
    result.minute = value.minute;
    result.second = value.second;
    result.valid = true;
    result.stale = false;
    return result;
}

Pcf8563DateTime pcf_time_from_app(const AppTime &value) {
    return {value.year, value.month, value.day, value.hour,
            value.minute, value.second};
}

bool init_motion() {
    return mpu6050_init(&motion_address);
}
}

uint32_t sensor_service_init() {
    const uint32_t now = millis();
    uint32_t mask = 0U;
    if (service_mutex == nullptr) {
        service_mutex = xSemaphoreCreateRecursiveMutex();
    }
    if (service_mutex == nullptr) {
        Serial.println("[SENSOR] failed to create service mutex");
        return 0U;
    }
    std::memset(&current_snapshot, 0, sizeof(current_snapshot));

    if (ensure_device(&environment_device, now, sht40_init)) {
        mask |= SENSOR_INIT_ENVIRONMENT;
        Serial.println("[SENSOR] SHT40 ready address=0x44");
    } else {
        Serial.println("[SENSOR] SHT40 unavailable address=0x44");
    }
    if (ensure_device(&battery_device, now, max17048_init)) {
        mask |= SENSOR_INIT_BATTERY;
        Serial.println("[SENSOR] MAX17048 ready address=0x36");
    } else {
        Serial.println("[SENSOR] MAX17048 unavailable address=0x36");
    }
    if (ensure_device(&rtc_device, now, pcf8563_init)) {
        mask |= SENSOR_INIT_RTC;
        Serial.println("[SENSOR] PCF8563 ready address=0x51");
    } else {
        Serial.println("[SENSOR] PCF8563 unavailable address=0x51");
    }
    if (ensure_device(&motion_device, now, init_motion)) {
        mask |= SENSOR_INIT_MOTION;
        Serial.printf("[SENSOR] MPU6050 ready address=0x%02X\n", motion_address);
    } else {
        Serial.println("[SENSOR] MPU6050 unavailable address=0x68/0x69");
    }
    return mask;
}

bool sensor_service_update_environment() {
    ServiceGuard guard;
    if (!guard) return false;
    const uint32_t now = millis();
    float temperature = 0.0f;
    float humidity = 0.0f;
    bool success = ensure_device(&environment_device, now, sht40_init) &&
                   sht40_read(&temperature, &humidity);
    if (!success && environment_device.initialized) {
        environment_device.initialized = false;
        environment_device.last_attempt_ms = now;
    }
    taskENTER_CRITICAL(&snapshot_mux);
    if (success) {
        current_snapshot.environment.value.temperature_x10 =
            static_cast<int16_t>(std::lround(temperature * 10.0f));
        const float clamped_humidity = std::min(std::max(humidity, 0.0f), 100.0f);
        current_snapshot.environment.value.humidity = static_cast<uint8_t>(
            std::lround(clamped_humidity));
        ++current_snapshot.version;
    }
    record_result(&current_snapshot.environment.health, success, now);
    taskEXIT_CRITICAL(&snapshot_mux);
    return success;
}

bool sensor_service_update_battery() {
    ServiceGuard guard;
    if (!guard) return false;
    const uint32_t now = millis();
    float soc = 0.0f;
    uint16_t voltage_mv = 0U;
    bool success = ensure_device(&battery_device, now, max17048_init) &&
                   max17048_read(&soc, &voltage_mv);
    if (!success && battery_device.initialized) {
        battery_device.initialized = false;
        battery_device.last_attempt_ms = now;
    }
    taskENTER_CRITICAL(&snapshot_mux);
    if (success) {
        const float clamped_soc = std::min(std::max(soc, 0.0f), 100.0f);
        current_snapshot.battery.value.percent = static_cast<uint8_t>(
            std::lround(clamped_soc));
        current_snapshot.battery.value.voltage_mv = voltage_mv;
        ++current_snapshot.version;
    }
    record_result(&current_snapshot.battery.health, success, now);
    taskEXIT_CRITICAL(&snapshot_mux);
    return success;
}

bool sensor_service_update_motion() {
    ServiceGuard guard;
    if (!guard) return false;
    const uint32_t now = millis();
    Mpu6050Sample sample = {};
    bool success = ensure_device(&motion_device, now, init_motion) &&
                   mpu6050_read(motion_address, &sample);
    if (!success && motion_device.initialized) {
        motion_device.initialized = false;
        motion_device.last_attempt_ms = now;
    }
    taskENTER_CRITICAL(&snapshot_mux);
    if (success) {
        std::memcpy(current_snapshot.motion.value.acceleration_mg,
                    sample.acceleration_mg, sizeof(sample.acceleration_mg));
        std::memcpy(current_snapshot.motion.value.angular_velocity_mdps,
                    sample.angular_velocity_mdps,
                    sizeof(sample.angular_velocity_mdps));
        current_snapshot.motion.value.temperature_x100 = sample.temperature_x100;
        ++current_snapshot.version;
    }
    record_result(&current_snapshot.motion.health, success, now);
    taskEXIT_CRITICAL(&snapshot_mux);
    return success;
}

bool sensor_service_read_rtc(AppTime *time) {
    ServiceGuard guard;
    if (!guard) return false;
    const uint32_t now = millis();
    Pcf8563DateTime value = {};
    bool success = ensure_device(&rtc_device, now, pcf8563_init) &&
                   pcf8563_read(&value);
    if (!success && rtc_device.initialized) {
        rtc_device.initialized = false;
        rtc_device.last_attempt_ms = now;
    }
    taskENTER_CRITICAL(&snapshot_mux);
    if (success) {
        current_snapshot.rtc.value = app_time_from_pcf(
            value, current_snapshot.rtc.value.version + 1U);
        ++current_snapshot.version;
    }
    record_result(&current_snapshot.rtc.health, success, now);
    if (success && time != nullptr) *time = current_snapshot.rtc.value;
    taskEXIT_CRITICAL(&snapshot_mux);
    return success;
}

bool sensor_service_write_rtc(const AppTime &time, AppTime *verified_time) {
    ServiceGuard guard;
    if (!guard) return false;
    if (!time.valid) return false;
    const uint32_t now = millis();
    rtc_device.attempted = false;
    if (!ensure_device(&rtc_device, now, pcf8563_init) ||
        !pcf8563_write(pcf_time_from_app(time))) {
        rtc_device.initialized = false;
        taskENTER_CRITICAL(&snapshot_mux);
        record_result(&current_snapshot.rtc.health, false, now);
        taskEXIT_CRITICAL(&snapshot_mux);
        return false;
    }
    AppTime verified = {};
    if (!sensor_service_read_rtc(&verified)) return false;
    if (verified_time != nullptr) *verified_time = verified;
    return true;
}

void sensor_service_get_snapshot(SensorSnapshot *snapshot) {
    if (snapshot == nullptr) return;
    const uint32_t now = millis();
    taskENTER_CRITICAL(&snapshot_mux);
    *snapshot = current_snapshot;
    taskEXIT_CRITICAL(&snapshot_mux);
    update_stale(&snapshot->environment.health, now, SLOW_STALE_MS);
    update_stale(&snapshot->battery.health, now, SLOW_STALE_MS);
    update_stale(&snapshot->rtc.health, now, SLOW_STALE_MS);
    update_stale(&snapshot->motion.health, now, MOTION_STALE_MS);
}
