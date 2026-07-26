#pragma once

#include <Arduino.h>

enum class KeyId : uint8_t {
    Left,
    Right,
    Middle,
};

enum class KeyGesture : uint8_t {
    Click,
    DoubleClick,
    LongPress,
};

struct KeyEvent {
    KeyId id;
    KeyGesture gesture;
};
