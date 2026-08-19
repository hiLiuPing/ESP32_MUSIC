#include <Arduino.h>
#include <freertos/task.h>

#include "app/settings_app.h"
#include "bsp/bsp_display.h"
#include "gui/page_manager.h"
#include "gui/egui_port.h"
#include "gui/screens/ui_music_page.h"
#include "gui/ui_heiti_font.h"
#include "gui/ui.h"
#include "task/task_system.h"

namespace {
constexpr uint32_t GUI_POLL_WARN_MS = 250U;
constexpr uint32_t GUI_POLL_LOG_INTERVAL_MS = 1000U;

void update_player_visualization(bool display_sleeping) {
    const bool active = !display_sleeping &&
                        gui_page_current() == UiPage::Music &&
                        ui_music_page_visualization_visible();
    task_set_player_visualization_active(active);
}
}

void gui_task(void *parameter) {
    (void)parameter;
    xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_DISPLAY_READY,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    if (!egui_port_start()) {
        Serial.println("[GUI] failed to start EGUI");
        vTaskDelete(nullptr);
    }

    uint32_t last_input_ms = millis();
    uint32_t last_slow_poll_log_ms = 0U;
#if PROJECT_TASK_STACK_DEBUG
    uint32_t last_stack_log_ms = 0U;
#endif
    bool display_sleeping = false;
    update_player_visualization(false);
    for (;;) {
        KeyEvent event = {};
        while (xQueueReceive(GuiKeyQueue, &event, 0) == pdTRUE) {
            last_input_ms = millis();
            if (display_sleeping) {
                bsp_display_set_sleeping(false);
                display_sleeping = false;
                egui_core_force_refresh(egui_port_core());
            }
            gui_page_handle_key(event);
        }

        PlayerStatus status = {};
        if (xQueueReceive(PlayerStatusQueue, &status, 0) == pdTRUE) {
            gui_page_update_status(status);
        }

        uint32_t poll_elapsed_ms = 0U;
        uint32_t poll_finished_ms = millis();
        if (!display_sleeping) {
            ui_music_page_cache_service();
            gui_page_service();
            gui_page_render();
            const uint32_t poll_started_ms = millis();
            egui_port_poll();
            poll_elapsed_ms = millis() - poll_started_ms;
            poll_finished_ms = millis();
        } else {
            // Consume notifications promptly without rendering to a sleeping panel.
            gui_page_service();
        }
#if PROJECT_TASK_STACK_DEBUG
        if (poll_finished_ms - last_stack_log_ms >= 5000U) {
            last_stack_log_ms = poll_finished_ms;
            Serial.printf("[STACK] GUI free=%u bytes\n",
                          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        }
#endif
        if (poll_elapsed_ms >= GUI_POLL_WARN_MS &&
            (last_slow_poll_log_ms == 0U ||
             poll_finished_ms - last_slow_poll_log_ms >= GUI_POLL_LOG_INTERVAL_MS)) {
            last_slow_poll_log_ms = poll_finished_ms;
            ui_heiti_font_log_cache_stats(poll_elapsed_ms);
        }

        const AppSettings settings = settings_app_get();
        if (!display_sleeping && settings.screen_idle_min != 0U &&
            millis() - last_input_ms >= static_cast<uint32_t>(settings.screen_idle_min) * 60000UL) {
            bsp_display_set_sleeping(true);
            display_sleeping = true;
        }
        update_player_visualization(display_sleeping);
        xSemaphoreTake(GuiWakeSemaphore,
                       display_sleeping ? portMAX_DELAY : pdMS_TO_TICKS(30));
    }
}
