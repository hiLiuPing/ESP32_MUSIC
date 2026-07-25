#include "bsp/bsp_input.h"

#include <cstring>

namespace {
char serial_line[96];
size_t serial_length = 0;
}

void bsp_input_init() {
    serial_length = 0;
}

bool bsp_input_read_line(char *buffer, size_t capacity) {
    if ((buffer == nullptr) || (capacity == 0)) {
        return false;
    }

    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if ((ch == '\r') || (ch == '\n')) {
            if (serial_length == 0) {
                continue;
            }
            serial_line[serial_length] = '\0';
            std::strncpy(buffer, serial_line, capacity - 1);
            buffer[capacity - 1] = '\0';
            serial_length = 0;
            return true;
        }
        if ((ch >= 32) && (ch <= 126) && (serial_length < sizeof(serial_line) - 1)) {
            serial_line[serial_length++] = ch;
        }
    }
    return false;
}

bool bsp_input_poll_key(UiInputEvent *event) {
    (void)event;
    return false;
}
