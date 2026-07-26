#ifndef __HOME_SKY_OBJECTS_RES_H__
#define __HOME_SKY_OBJECTS_RES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "image/egui_image_std.h"

typedef struct
{
    const egui_image_std_t *image;
    uint16_t width;
    uint16_t height;
} home_sky_asset_t;

#if EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_ALPHA_8
uint8_t home_sky_cloud_count(void);
const home_sky_asset_t *home_sky_cloud_get(uint8_t index);
uint8_t home_sky_bird_count(void);
const home_sky_asset_t *home_sky_bird_get(uint8_t index);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __HOME_SKY_OBJECTS_RES_H__ */
