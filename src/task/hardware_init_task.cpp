#include <Arduino.h>

#include "app/audio_settings.h"
#include "app/sensor_service.h"
#include "bsp/bsp_audio.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_storage.h"
#include "bsp/bsp_littlefs.h"
#include "task/task_system.h"
#include "app/system_notify.h"

namespace {
constexpr uint32_t EXPECTED_PSRAM_BYTES = 2U * 1024U * 1024U;

void log_psram_status() {
    const bool found = psramFound();
    const uint32_t total_bytes = ESP.getPsramSize();
    const uint32_t free_bytes = ESP.getFreePsram();
    Serial.printf("[HW] PSRAM %s, total=%lu bytes, free=%lu bytes\n",
                  found ? "ready" : "unavailable",
                  static_cast<unsigned long>(total_bytes),
                  static_cast<unsigned long>(free_bytes));

    if (!found) {
        Serial.println("[HW] WARNING: PSRAM unavailable; audio will use the internal RAM buffer");
    } else if (total_bytes != EXPECTED_PSRAM_BYTES) {
        Serial.printf("[HW] WARNING: expected %lu bytes of PSRAM, detected %lu bytes\n",
                      static_cast<unsigned long>(EXPECTED_PSRAM_BYTES),
                      static_cast<unsigned long>(total_bytes));
    }
}
}

void hardware_init_task(void *parameter) {
    (void)parameter;

    log_psram_status();

    // Bring the display up first so the GUI can present Boot while the
    // storage and audio devices continue initializing in this task.
    Serial.println("[HW] display init");
    if (bsp_display_init()) {
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_DISPLAY_READY);
        if (GuiWakeSemaphore != nullptr) {
            xSemaphoreGive(GuiWakeSemaphore);
        }
    }

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

    bool littlefs_resources_ready = littlefs_ready;
    if (littlefs_ready && sd_ready) {
        Serial.println("[HW] LittleFS resource sync");
        if (bsp_littlefs_sync_from(bsp_storage_fs(), "/data")) {
            Serial.println("[HW] LittleFS resource sync complete");
        } else {
            Serial.println("[HW] LittleFS resource sync failed");
            littlefs_resources_ready = false;
            system_notify_post(SystemNotifyType::Storage, "RESOURCE SYNC FAILED");
        }
    } else {
        Serial.println("[HW] LittleFS resource sync skipped");
    }
    if (littlefs_resources_ready) {
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_LITTLEFS_READY);
        if (GuiWakeSemaphore != nullptr) {
            xSemaphoreGive(GuiWakeSemaphore);
        }
    }

    Serial.println("[HW] WM8978 init");
    if (bsp_audio_codec_init()) {
        const AudioSettings audio_settings = audio_settings_get();
        bsp_audio_apply_codec_settings(audio_settings.bass_db,
                                       audio_settings.treble_db,
                                       audio_settings.surround_depth);
        Serial.println("[HW] WM8978 ready");
        xEventGroupSetBits(HardwareEventGroup, HW_EVENT_CODEC_READY);
    } else {
        Serial.println("[HW] WM8978 unavailable");
        system_notify_post(SystemNotifyType::Audio, "AUDIO CODEC UNAVAILABLE");
    }

    Serial.println("[HW] I2C sensors init");
    const uint32_t sensor_mask = sensor_service_init();
    Serial.printf("[HW] sensor init mask=0x%02lX expected=0x%02lX\n",
                  static_cast<unsigned long>(sensor_mask),
                  static_cast<unsigned long>(SENSOR_INIT_ALL));

    xEventGroupSetBits(HardwareEventGroup, HW_EVENT_INIT_DONE);
    if (GuiWakeSemaphore != nullptr) {
        xSemaphoreGive(GuiWakeSemaphore);
    }
    vTaskDelete(nullptr);
}
