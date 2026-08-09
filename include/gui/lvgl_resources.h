#pragma once

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t * const lvgl_weather_icons[];
extern const unsigned lvgl_weather_icon_count;
const lv_image_dsc_t *lvgl_weather_icon_get(uint16_t icon_id);

#ifdef __cplusplus
}
#endif
