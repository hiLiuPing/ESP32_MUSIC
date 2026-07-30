#pragma once

#include <Arduino.h>

#include "app/player_types.h"

void audio_settings_init();
AudioSettings audio_settings_get();
bool audio_settings_update(const AudioSettings &settings);
