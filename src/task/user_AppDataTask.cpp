#include "task/user_AppDataTask.h"

#include <Arduino.h>

#include "app/app_data.h"
#include "task/task_system.h"

void user_AppDataTask(void *parameter) {
    (void)parameter;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        app_data_update_temporary_home_data(
            millis(), static_cast<uint32_t>(xEventGroupGetBits(HardwareEventGroup)));
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
