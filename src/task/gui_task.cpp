#include <Arduino.h>

#include "gui/page_manager.h"
#include "gui/egui_port.h"
#include "gui/ui.h"
#include "task/task_system.h"

void gui_task(void *parameter) {
    (void)parameter;
    xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_DISPLAY_READY,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    if (!egui_port_start()) {
        Serial.println("[GUI] failed to start EGUI");
        vTaskDelete(nullptr);
    }

    for (;;) {
        KeyEvent event = {};
        while (xQueueReceive(GuiKeyQueue, &event, 0) == pdTRUE) {
            gui_page_handle_key(event);
        }

        PlayerStatus status = {};
        if (xQueueReceive(PlayerStatusQueue, &status, 0) == pdTRUE) {
            gui_page_update_status(status);
        }

        gui_page_service();
        gui_page_render();
        egui_port_poll();

        xSemaphoreTake(GuiWakeSemaphore, pdMS_TO_TICKS(50));
    }
}
