#include "task/user_KeyTask.h"

#include <Arduino.h>
#include <OneButton.h>

#include "app/key_types.h"
#include "bsp/bsp_keys.h"
#include "task/task_system.h"

namespace {
constexpr uint32_t SCAN_PERIOD_MS = 10;
constexpr int DEBOUNCE_MS = 30;
constexpr uint32_t CLICK_MS = 150;
constexpr uint32_t PRESS_MS = 500;

struct ButtonBinding {
    OneButton button;
    KeyId id;
};

ButtonBinding left_button = {};
ButtonBinding right_button = {};
ButtonBinding middle_button = {};
uint32_t dropped_events = 0;

void post_event(ButtonBinding *binding, KeyGesture gesture) {
    if ((binding == nullptr) || (KeyEventQueue == nullptr)) {
        return;
    }
    const KeyEvent event{binding->id, gesture};
    if (xQueueSend(KeyEventQueue, &event, 0) != pdTRUE) {
        ++dropped_events;
        if ((dropped_events & 0x07U) == 0U) {
            Serial.printf("[KEY] input queue dropped=%lu\n",
                          static_cast<unsigned long>(dropped_events));
        }
    }
}

void on_click(void *context) {
    post_event(static_cast<ButtonBinding *>(context), KeyGesture::Click);
}

void on_double_click(void *context) {
    post_event(static_cast<ButtonBinding *>(context), KeyGesture::DoubleClick);
}

void on_long_press(void *context) {
    post_event(static_cast<ButtonBinding *>(context), KeyGesture::LongPress);
}

void configure_button(ButtonBinding &binding, KeyId id) {
    binding.id = id;
    binding.button.setDebounceMs(DEBOUNCE_MS);
    binding.button.setClickMs(CLICK_MS);
    binding.button.setPressMs(PRESS_MS);
    binding.button.attachClick(on_click, &binding);
    binding.button.attachDoubleClick(on_double_click, &binding);
    binding.button.attachLongPressStart(on_long_press, &binding);
}
}

void user_KeyTask(void *parameter) {
    (void)parameter;
    bsp_keys_init();
    configure_button(left_button, KeyId::Left);
    configure_button(right_button, KeyId::Right);
    configure_button(middle_button, KeyId::Middle);

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        left_button.button.tick(bsp_key_is_pressed(KeyId::Left));
        right_button.button.tick(bsp_key_is_pressed(KeyId::Right));
        middle_button.button.tick(bsp_key_is_pressed(KeyId::Middle));
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}
