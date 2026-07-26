#include "task/user_KeyManllegeTask.h"

#include <Arduino.h>

#include "app/key_types.h"
#include "task/task_system.h"

void user_KeyManllegeTask(void *parameter) {
    (void)parameter;
    uint32_t dropped_events = 0;

    for (;;) {
        KeyEvent event = {};
        if (xQueueReceive(KeyEventQueue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (xQueueSend(GuiKeyQueue, &event, 0) != pdTRUE) {
            ++dropped_events;
            if ((dropped_events & 0x07U) == 0U) {
                Serial.printf("[KEY] GUI queue dropped=%lu\n",
                              static_cast<unsigned long>(dropped_events));
            }
            continue;
        }
        if (GuiWakeSemaphore != nullptr) {
            xSemaphoreGive(GuiWakeSemaphore);
        }
    }
}
