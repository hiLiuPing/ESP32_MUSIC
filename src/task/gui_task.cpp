#include <Arduino.h>

#include "gui/page_manager.h"
#include "task/task_system.h"

void gui_task(void *parameter) {
    (void)parameter;
    xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_DISPLAY_READY,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    gui_page_manager_init();

    for (;;) {
        UiInputEvent event = {};
        while (xQueueReceive(UiInputQueue, &event, 0) == pdTRUE) {
            gui_page_handle_input(event);
        }

        PlayerStatus status = {};
        if (xQueueReceive(PlayerStatusQueue, &status, 0) == pdTRUE) {
            gui_page_update_status(status);
        }

        const bool timed_refresh = gui_page_current() == UiPage::Boot;
        gui_page_render(timed_refresh);

        xSemaphoreTake(GuiWakeSemaphore, pdMS_TO_TICKS(100));
    }
}
