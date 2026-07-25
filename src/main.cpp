#include <Arduino.h>

#include "task/task_system.h"

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("[BOOT] ESP32-S3 music player");
    task_system_init();
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
