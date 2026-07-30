#pragma once

#include <Audio.h>

void bsp_audio_power_on_early();
bool bsp_audio_codec_init();
void bsp_audio_apply_codec_settings(int8_t bass_db, int8_t treble_db,
                                    uint8_t surround_depth);
void bsp_audio_apply_output_route(bool amplifier_enabled);
void bsp_audio_set_amplifier_enabled(bool enabled);
bool bsp_audio_configure_i2s(Audio &audio, bool amplifier_enabled);
