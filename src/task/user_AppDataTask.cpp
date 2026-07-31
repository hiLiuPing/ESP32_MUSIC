#include "task/user_AppDataTask.h"

#include <Arduino.h>

#include <ctime>

#include "app/app_data.h"
#include "app/home_demo.h"
#include "app/sensor_service.h"
#include "task/task_system.h"

namespace {
constexpr uint32_t APP_DATA_LOOP_MS = 30U;
constexpr uint32_t APP_DATA_SLOW_PERIOD_MS = 1000U;

#if !HOME_DEMO_ENABLE
bool read_system_time(AppTime *time) {
    if (time == nullptr) return false;
    const time_t now = std::time(nullptr);
    struct tm local = {};
    if (now < 1577836800 || localtime_r(&now, &local) == nullptr ||
        local.tm_year + 1900 < 2020) {
        return false;
    }
    static uint32_t version = 0U;
    time->version = ++version;
    time->year = static_cast<uint16_t>(local.tm_year + 1900);
    time->month = static_cast<uint8_t>(local.tm_mon + 1);
    time->day = static_cast<uint8_t>(local.tm_mday);
    time->hour = static_cast<uint8_t>(local.tm_hour);
    time->minute = static_cast<uint8_t>(local.tm_min);
    time->second = static_cast<uint8_t>(local.tm_sec);
    time->valid = true;
    time->stale = true;
    return true;
}
#endif

void update_slow_data() {
    (void)sensor_service_update_environment();
    (void)sensor_service_update_battery();

#if !HOME_DEMO_ENABLE
    AppTime time = {};
    if (sensor_service_read_rtc(&time)) {
        app_data_set_time(time);
    } else if (read_system_time(&time)) {
        app_data_set_time(time);
    } else {
        app_data_mark_time_stale();
    }
#endif

    SensorSnapshot sensors = {};
    sensor_service_get_snapshot(&sensors);
    static uint32_t environment_version = 0U;
    static uint32_t battery_version = 0U;
    HomeEnvironmentData environment = {};
    environment.version = ++environment_version;
    environment.temperature_x10 = sensors.environment.value.temperature_x10;
    environment.humidity = sensors.environment.value.humidity;
    environment.valid = sensors.environment.health.valid;
    environment.stale = sensors.environment.health.stale;
    app_data_set_environment(environment);

    HomeBatteryData battery = {};
    battery.version = ++battery_version;
    battery.percent = sensors.battery.value.percent;
    battery.voltage_mv = sensors.battery.value.voltage_mv;
    battery.charging = false;
    battery.full = false;
    battery.valid = sensors.battery.health.valid;
    battery.stale = sensors.battery.health.stale;
    app_data_set_battery(battery);

#if !HOME_DEMO_ENABLE
    DataApp_HomeStatus_Update();
#endif
    app_data_update_runtime(
        millis(), static_cast<uint32_t>(xEventGroupGetBits(HardwareEventGroup)));
    if (GuiWakeSemaphore != nullptr) xSemaphoreGive(GuiWakeSemaphore);
}
}

void user_AppDataTask(void *parameter) {
    (void)parameter;
    (void)xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_INIT_DONE,
                              pdFALSE, pdTRUE, portMAX_DELAY);
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_slow = last_wake - pdMS_TO_TICKS(APP_DATA_SLOW_PERIOD_MS);
    home_demo_init(millis());

    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        home_demo_service(millis());
        (void)sensor_service_update_motion();
        if (static_cast<TickType_t>(now - last_slow) >=
            pdMS_TO_TICKS(APP_DATA_SLOW_PERIOD_MS)) {
            last_slow += pdMS_TO_TICKS(APP_DATA_SLOW_PERIOD_MS);
            update_slow_data();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_DATA_LOOP_MS));
    }
}
