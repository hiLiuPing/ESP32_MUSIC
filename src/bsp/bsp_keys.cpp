#include "bsp/bsp_keys.h"

#include <Arduino.h>

#include "bsp/board_config.h"

namespace {
uint8_t key_pin(KeyId id) {
    switch (id) {
        case KeyId::Left: return BoardConfig::KeyLeft;
        case KeyId::Right: return BoardConfig::Keyright;
        case KeyId::Middle: return BoardConfig::KeyMidle;
    }
    return BoardConfig::KeyMidle;
}
}

void bsp_keys_init() {
    pinMode(BoardConfig::KeyLeft, INPUT_PULLUP);
    pinMode(BoardConfig::Keyright, INPUT_PULLUP);
    pinMode(BoardConfig::KeyMidle, INPUT_PULLUP);
}

bool bsp_key_is_pressed(KeyId id) {
    return digitalRead(key_pin(id)) == LOW;
}
