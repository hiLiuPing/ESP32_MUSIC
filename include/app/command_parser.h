#pragma once

#include "app/player_types.h"

bool command_parser_parse(const char *line, UiInputEvent *event,
                          char *error, size_t error_capacity);
