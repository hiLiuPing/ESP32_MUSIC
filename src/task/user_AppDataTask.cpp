#include "task/user_AppDataTask.h"

#include <Arduino.h>

#include "app/app_data.h"
#include "task/task_system.h"

void user_AppDataTask(void *parameter) {
    (void)parameter;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        DataApp_HomeStatus_Update();
        app_data_update_runtime(
            millis(), static_cast<uint32_t>(xEventGroupGetBits(HardwareEventGroup)));
        if (GuiWakeSemaphore != nullptr) xSemaphoreGive(GuiWakeSemaphore);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
