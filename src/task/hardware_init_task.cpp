#include <Arduino.h>

#include "bsp/bsp_audio.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_storage.h"
#include "bsp/bsp_littlefs.h"
#include "task/task_system.h"
#include "app/system_notify.h"

void hardware_init_task(void *parameter) {
    (void)parameter;

    Serial.println("[HW] LittleFS init");
    const bool littlefs_ready = bsp_littlefs_init();
    if (littlefs_ready) {
        Serial.println("[HW] LittleFS ready");
    } else {
        Serial.println("[HW] LittleFS unavailable");
        system_notify_post(SystemNotifyType::Storage, "LITTLEFS UNAVAILABLE");
    }

    Serial.println("[HW] SD init");
    const bool sd_ready = bsp_storage_init();
    if (sd_ready) {
        Serial.println("[HW] SD ready");
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_SD_READY);
    } else {
        Serial.println("[HW] SD unavailable");
        system_notify_post(SystemNotifyType::Storage, "SD CARD UNAVAILABLE");
    }

    if (littlefs_ready && sd_ready) {
        Serial.println("[HW] LittleFS resource sync");
        if (bsp_littlefs_sync_from(bsp_storage_fs(), "/data")) {
            Serial.println("[HW] LittleFS resource sync complete");
        } else {
            Serial.println("[HW] LittleFS resource sync failed");
            system_notify_post(SystemNotifyType::Storage, "RESOURCE SYNC FAILED");
        }
    } else {
        Serial.println("[HW] LittleFS resource sync skipped");
    }

    // Publish display readiness only after LittleFS resources are stable.
    Serial.println("[HW] display init");
    if (bsp_display_init()) {
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_DISPLAY_READY);
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
