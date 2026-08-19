#pragma once

#include <Audio.h>

enum class BspAudioPlaybackRoute : uint8_t {
    Off,
    Speaker,
    Headphones,
};

void bsp_audio_power_on_early();
bool bsp_audio_codec_init();
void bsp_audio_apply_codec_settings(int8_t bass_db, int8_t treble_db,
                                    uint8_t surround_depth);
bool bsp_audio_set_output_muted(bool muted);
bool bsp_audio_set_playback_route(BspAudioPlaybackRoute route);
bool bsp_audio_configure_i2s(Audio &audio);
