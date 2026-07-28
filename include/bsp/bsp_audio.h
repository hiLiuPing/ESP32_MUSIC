#pragma once

#include <Audio.h>

void bsp_audio_power_on_early();
bool bsp_audio_codec_init();
bool bsp_audio_configure_i2s(Audio &audio);
