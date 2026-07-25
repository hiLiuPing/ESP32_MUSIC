#pragma once

#include <Arduino.h>

#include "app/player_types.h"

void bsp_input_init();
bool bsp_input_read_line(char *buffer, size_t capacity);
bool bsp_input_poll_key(UiInputEvent *event);
