#pragma once

#include <cstdint>

#include "app/app_data.h"

struct SensorHealth {
    bool valid;
    bool stale;
    uint16_t consecutive_failures;
    uint32_t last_success_ms;
};

struct SensorEnvironmentValue {
    int16_t temperature_x10;
    uint8_t humidity;
};

struct SensorBatteryValue {
    uint8_t percent;
    uint16_t voltage_mv;
};

struct SensorMotionValue {
    int16_t acceleration_mg[3];
    int32_t angular_velocity_mdps[3];
    int16_t temperature_x100;
};

template <typename Value>
struct SensorValueSnapshot {
    Value value;
    SensorHealth health;
};

struct SensorSnapshot {
    uint32_t version;
    SensorValueSnapshot<SensorEnvironmentValue> environment;
    SensorValueSnapshot<SensorBatteryValue> battery;
    SensorValueSnapshot<SensorMotionValue> motion;
    SensorValueSnapshot<AppTime> rtc;
};

constexpr uint32_t SENSOR_INIT_ENVIRONMENT = BIT0;
constexpr uint32_t SENSOR_INIT_BATTERY = BIT1;
constexpr uint32_t SENSOR_INIT_RTC = BIT2;
constexpr uint32_t SENSOR_INIT_MOTION = BIT3;
constexpr uint32_t SENSOR_INIT_ALL = SENSOR_INIT_ENVIRONMENT |
                                     SENSOR_INIT_BATTERY |
                                     SENSOR_INIT_RTC |
                                     SENSOR_INIT_MOTION;

uint32_t sensor_service_init();
bool sensor_service_update_environment();
bool sensor_service_update_battery();
bool sensor_service_update_motion();
bool sensor_service_read_rtc(AppTime *time);
bool sensor_service_write_rtc(const AppTime &time, AppTime *verified_time = nullptr);
void sensor_service_get_snapshot(SensorSnapshot *snapshot);
