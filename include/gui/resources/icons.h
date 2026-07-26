#ifndef __WEATHER_ICONS_H__
#define __WEATHER_ICONS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "image/egui_image_std.h"

#define WEATHER_ICON_SIZE 64U
#define WEATHER_ICON_PIXELS (WEATHER_ICON_SIZE * WEATHER_ICON_SIZE)
#define WEATHER_ICON_RGB565_BYTES (WEATHER_ICON_PIXELS * 2U)
#define WEATHER_ICON_ALPHA8_BYTES WEATHER_ICON_PIXELS

#if EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_RGB565
const egui_image_std_t *ui_weather_icon_get(uint16_t icon_id);
const egui_image_std_t *ui_weather_icon_get_mask(uint16_t icon_id);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __WEATHER_ICONS_H__ */
