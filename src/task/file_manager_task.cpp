#include "task/file_manager_task.h"

#include "app/file_manager_network.h"
#include "app/system_notify.h"
#include "task/task_system.h"
#include "task/weather_sync_task.h"

TaskHandle_t FileManagerTaskHandle = nullptr;

namespace {
constexpr uint32_t PORTAL_STOP_TIMEOUT_MS = 3000U;

bool wait_weather_stopped(uint32_t timeout_ms) {
    const uint32_t start = millis();
    while (weather_sync_is_provisioning() &&
           static_cast<uint32_t>(millis() - start) < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
    return !weather_sync_is_provisioning();
}

void start_file_manager_ap() {
    if (weather_sync_is_provisioning()) {
        (void)weather_sync_request(WEATHER_SYNC_STOP_AP);
        if (!wait_weather_stopped(PORTAL_STOP_TIMEOUT_MS)) {
            system_notify_post(SystemNotifyType::Error, "WIFI AP BUSY");
            return;
        }
    }

    if (file_manager_network_start_ap()) {
        const String message = file_manager_network_ap_message();
        system_notify_post(SystemNotifyType::Info, message.c_str());
    } else {
        system_notify_post(SystemNotifyType::Error, "FILE AP START FAILED");
    }
}
}

void file_manager_task(void *parameter) {
    (void)parameter;
    (void)xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_INIT_DONE,
                              pdFALSE, pdTRUE, portMAX_DELAY);
    file_manager_network_init();

    for (;;) {
        uint32_t request = 0U;
        const TickType_t wait_ticks = file_manager_network_ap_active()
                                          ? pdMS_TO_TICKS(10U)
                                          : pdMS_TO_TICKS(100U);
        if (xTaskNotifyWait(0U, UINT32_MAX, &request, wait_ticks) == pdTRUE) {
            if ((request & FILE_MANAGER_STOP_AP) != 0U) {
                file_manager_network_stop_ap();
            }
            if ((request & FILE_MANAGER_START_AP) != 0U) {
                start_file_manager_ap();
            }
        }
        file_manager_network_process_ap();
    }
}

bool file_manager_request(uint32_t request) {
    return FileManagerTaskHandle != nullptr &&
           xTaskNotify(FileManagerTaskHandle, request, eSetBits) == pdPASS;
}

bool file_manager_is_active() {
    return file_manager_network_ap_active();
}

String file_manager_ap_message() {
    return file_manager_network_ap_message();
}
