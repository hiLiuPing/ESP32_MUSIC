#include <Arduino.h>

#include "app/command_parser.h"
#include "app/player_app.h"
#include "bsp/bsp_input.h"
#include "gui/page_manager.h"
#include "task/task_system.h"

namespace {
void print_status() {
    PlayerStatus status = {};
    if (!player_app_get_status(&status)) {
        Serial.println("ERR status unavailable");
        return;
    }
    Serial.printf("STATUS page=%s state=%s track=%u/%u time=%lu/%lu vol=%u error=%s file=\"%s\"\n",
                  gui_page_name(gui_page_current()), player_state_name(status.state),
                  status.track_count == 0 ? 0 : status.track_index + 1, status.track_count,
                  static_cast<unsigned long>(status.elapsed_seconds),
                  static_cast<unsigned long>(status.duration_seconds), status.volume,
                  player_error_name(status.error), status.file_name);
}

void submit_event(const UiInputEvent &event) {
    if (event.type == UiInputType::Status) {
        print_status();
        return;
    }
    if (xQueueSend(UiInputQueue, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
        xSemaphoreGive(GuiWakeSemaphore);
        Serial.println("OK");
    } else {
        Serial.println("ERR input queue full");
    }
}
}

void input_task(void *parameter) {
    (void)parameter;
    bsp_input_init();
    char line[96];
    char error[64];

    for (;;) {
        if (bsp_input_read_line(line, sizeof(line))) {
            UiInputEvent event = {};
            if (command_parser_parse(line, &event, error, sizeof(error))) {
                submit_event(event);
            } else {
                Serial.printf("ERR %s\n", error);
            }
        }

        UiInputEvent key_event = {};
        if (bsp_input_poll_key(&key_event)) {
            submit_event(key_event);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
