#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "app/player_types.h"

void player_app_attach_mutex(SemaphoreHandle_t mutex);
void player_app_set_status(const PlayerStatus &status);
bool player_app_get_status(PlayerStatus *status);
const char *player_state_name(PlayerState state);
const char *player_error_name(PlayerError error);
