#include <Arduino.h>
#include <esp_system.h>

#include "app/audio_settings.h"
#include "bsp/bsp_audio.h"
#include "task/task_system.h"

namespace {
const char *reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt-wdt";
        case ESP_RST_TASK_WDT: return "task-wdt";
        case ESP_RST_WDT: return "other-wdt";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}
}

void setup() {
    bsp_audio_power_on_early();
    Serial.begin(115200);
    delay(50);
    Serial.println("[BOOT] ESP32-S3 music player");
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.printf("[BOOT] reset reason=%s (%d)\n", reset_reason_name(reset_reason),
                  static_cast<int>(reset_reason));
    audio_settings_init();
    task_system_init();
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
