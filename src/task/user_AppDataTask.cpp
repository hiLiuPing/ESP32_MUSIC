#include "task/user_AppDataTask.h"

#include <Arduino.h>

#include "app/app_data.h"
#include "task/task_system.h"

void user_AppDataTask(void *parameter) {
    (void)parameter;
    TickType_t last_wake = xTaskGetTickCount();
    AppDataSnapshot snapshot = {};

    for (;;) {
        ++snapshot.version;
        snapshot.uptime_ms = millis();
        snapshot.hardware_bits = static_cast<uint32_t>(
            xEventGroupGetBits(HardwareEventGroup));
        app_data_set_snapshot(snapshot);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}
