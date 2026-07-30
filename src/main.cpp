#include <Arduino.h>

#include "app/audio_settings.h"
#include "bsp/bsp_audio.h"
#include "task/task_system.h"

void setup() {
    bsp_audio_power_on_early();
    Serial.begin(115200);
    delay(50);
    Serial.println("[BOOT] ESP32-S3 music player");
    audio_settings_init();
    task_system_init();
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
