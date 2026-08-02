#include "task/user_KeyManllegeTask.h"

#include <Arduino.h>

#include "app/key_types.h"
#include "app/motion_gesture.h"
#include "app/sensor_service.h"
#include "gui/page_manager.h"
#include "task/task_system.h"

namespace {
constexpr uint32_t GESTURE_ACTIVE_MS = 3000U;
constexpr TickType_t KEY_MANAGER_WAIT_TICKS = pdMS_TO_TICKS(10U);

bool gesture_window_active(uint32_t now, uint32_t active_until_ms) {
    return active_until_ms != 0U &&
           static_cast<int32_t>(now - active_until_ms) < 0;
}

void refresh_gesture_window(uint32_t *active_until_ms) {
    if (active_until_ms == nullptr) return;
    *active_until_ms = millis() + GESTURE_ACTIVE_MS;
}

bool post_gesture_command(MotionGesture gesture) {
    switch (gesture) {
        case MotionGesture::LeftDown:
            return task_post_player_command(PlayerCommandType::Previous, 0, true);
        case MotionGesture::RightDown:
            return task_post_player_command(PlayerCommandType::Next, 0, true);
        case MotionGesture::FrontDown:
            return task_post_player_command(PlayerCommandType::ChangeVolume, -1, true);
        case MotionGesture::BackDown:
            return task_post_player_command(PlayerCommandType::ChangeVolume, 1, true);
        case MotionGesture::None:
        default:
            return false;
    }
}

const char *gesture_name(MotionGesture gesture) {
    switch (gesture) {
        case MotionGesture::LeftDown: return "LEFT_DOWN";
        case MotionGesture::RightDown: return "RIGHT_DOWN";
        case MotionGesture::FrontDown: return "FRONT_DOWN";
        case MotionGesture::BackDown: return "BACK_DOWN";
        case MotionGesture::None:
        default: return "NONE";
    }
}
}

void user_KeyManllegeTask(void *parameter) {
    (void)parameter;
    (void)xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_INIT_DONE,
                              pdFALSE, pdTRUE, portMAX_DELAY);
    uint32_t dropped_events = 0;
    uint32_t gesture_active_until_ms = 0U;
    bool gesture_was_active = false;

    for (;;) {
        KeyEvent event = {};
        if (xQueueReceive(KeyEventQueue, &event, KEY_MANAGER_WAIT_TICKS) == pdTRUE) {
            if (gui_page_current() == UiPage::Home) {
                SensorSnapshot snapshot = {};
                sensor_service_get_snapshot(&snapshot);
                const bool motion_ready = motion_gesture_arm(snapshot);
                refresh_gesture_window(&gesture_active_until_ms);
                gesture_was_active = true;
                Serial.printf(
                    "[KEY] motion window ready=%u valid=%u stale=%u acc=%d,%d,%d\n",
                    motion_ready ? 1U : 0U,
                    snapshot.motion.health.valid ? 1U : 0U,
                    snapshot.motion.health.stale ? 1U : 0U,
                    snapshot.motion.value.acceleration_mg[0],
                    snapshot.motion.value.acceleration_mg[1],
                    snapshot.motion.value.acceleration_mg[2]);
            }

            if (xQueueSend(GuiKeyQueue, &event, 0) != pdTRUE) {
                ++dropped_events;
                if ((dropped_events & 0x07U) == 0U) {
                    Serial.printf("[KEY] GUI queue dropped=%lu\n",
                                  static_cast<unsigned long>(dropped_events));
                }
            } else if (GuiWakeSemaphore != nullptr) {
                xSemaphoreGive(GuiWakeSemaphore);
            }
        }

        const uint32_t now = millis();
        if (gui_page_current() == UiPage::Home &&
            gesture_window_active(now, gesture_active_until_ms)) {
            SensorSnapshot snapshot = {};
            sensor_service_get_snapshot(&snapshot);
            const MotionGesture gesture = motion_gesture_update(snapshot);
            if (gesture != MotionGesture::None) {
                if (post_gesture_command(gesture)) {
                    refresh_gesture_window(&gesture_active_until_ms);
                    gesture_was_active = true;
                    Serial.printf(
                        "[KEY] motion gesture=%s window refreshed=%lums\n",
                        gesture_name(gesture),
                        static_cast<unsigned long>(GESTURE_ACTIVE_MS));
                }
            }
        } else if (gesture_was_active) {
            gesture_was_active = false;
            motion_gesture_reset();
        }
    }
}
