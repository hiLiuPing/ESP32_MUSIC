#include <Arduino.h>

#include "app/settings_app.h"
#include "bsp/bsp_display.h"
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

    uint32_t last_input_ms = millis();
    bool display_sleeping = false;
    bool display_poweroff = false;
    for (;;) {
        KeyEvent event = {};
        while (xQueueReceive(GuiKeyQueue, &event, 0) == pdTRUE) {
            last_input_ms = millis();
            if (display_sleeping) {
                bsp_display().display_on(true);
                display_sleeping = false;
                display_poweroff = false;
                egui_core_force_refresh(egui_port_core());
            }
            gui_page_handle_key(event);
        }

        PlayerStatus status = {};
        if (xQueueReceive(PlayerStatusQueue, &status, 0) == pdTRUE) {
            gui_page_update_status(status);
        }

        gui_page_service();
        gui_page_render();
        egui_port_poll();

        const AppSettings settings = settings_app_get();
        if (!display_sleeping && settings.screen_idle_min != 0U &&
            millis() - last_input_ms >= static_cast<uint32_t>(settings.screen_idle_min) * 60000UL) {
            bsp_display().display_on(false);
            display_sleeping = true;
        }
        if (!display_poweroff && settings.auto_off_min != 0U &&
            millis() - last_input_ms >= static_cast<uint32_t>(settings.auto_off_min) * 60000UL) {
            bsp_display().display_on(false);
            display_sleeping = true;
            display_poweroff = true;
        }

        xSemaphoreTake(GuiWakeSemaphore, pdMS_TO_TICKS(50));
    }
}
