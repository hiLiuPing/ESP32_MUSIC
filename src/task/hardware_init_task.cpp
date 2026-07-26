#include <Arduino.h>

#include "bsp/bsp_audio.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_storage.h"
#include "bsp/bsp_littlefs.h"
#include "task/task_system.h"
#include "app/system_notify.h"

void hardware_init_task(void *parameter) {
    (void)parameter;

    // Mount LittleFS before publishing display readiness so GUI resources are
    // available before the GUI task can start rendering pages.
    Serial.println("[HW] LittleFS init");
    if (bsp_littlefs_init()) {
        Serial.println("[HW] LittleFS ready");
    } else {
        Serial.println("[HW] LittleFS unavailable");
    }

    Serial.println("[HW] display init");
    if (bsp_display_init()) {
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_DISPLAY_READY);
    }

    Serial.println("[HW] SD init");
    if (bsp_storage_init()) {
        Serial.println("[HW] SD ready");
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_SD_READY);
    } else {
        Serial.println("[HW] SD unavailable");
        system_notify_post(SystemNotifyType::Storage, "SD CARD UNAVAILABLE");
    }

    Serial.println("[HW] WM8978 init");
    if (bsp_audio_codec_init()) {
        Serial.println("[HW] WM8978 ready");
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_CODEC_READY);
    } else {
        Serial.println("[HW] WM8978 unavailable");
        system_notify_post(SystemNotifyType::Audio, "AUDIO CODEC UNAVAILABLE");
    }

    xEventGroupSetBits(HardwareEventGroup, HW_EVENT_INIT_DONE);
    if (GuiWakeSemaphore != nullptr) {
        xSemaphoreGive(GuiWakeSemaphore);
    }
    vTaskDelete(nullptr);
}
