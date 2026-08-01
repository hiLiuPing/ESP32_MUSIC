#pragma once

#include <Arduino.h>

#include "app/sensor_service.h"

enum class MotionGesture : uint8_t {
    None,
    LeftDown,
    RightDown,
    FrontDown,
    BackDown,
};

void motion_gesture_reset();
bool motion_gesture_arm(const SensorSnapshot &snapshot);
MotionGesture motion_gesture_update(const SensorSnapshot &snapshot);
