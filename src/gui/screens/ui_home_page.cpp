#include "gui/screens/ui_home_page.h"

#include <stdio.h>
#include <string.h>
#include <cmath>

#include "core/egui_timer.h"
#include "app/app_data.h"
#include "bsp/bsp_display.h"
#include "gui/egui_port.h"
#include "gui/resources/home_camp_res.h"
#include "gui/resources/home_scene_res.h"
#include "gui/resources/home_sky_objects_res.h"
#include "gui/resources/icons.h"
#include "gui/page.h"
#include "gui/ui_heiti_font.h"
#include "task/task_system.h"

#define UI_SCREEN_W EGUI_CONFIG_SCREEN_WIDTH
#define UI_SCREEN_H EGUI_CONFIG_SCREEN_HEIGHT

static inline egui_color_t ui_color(uint32_t rgb) { return EGUI_COLOR_HEX(rgb); }

static inline void ui_draw_text(egui_canvas_t *canvas,
                                const egui_font_t *font,
                                const char *text,
                                egui_dim_t x,
                                egui_dim_t y,
                                egui_dim_t w,
                                egui_dim_t h,
                                uint8_t align,
                                uint32_t rgb) {
    egui_region_t rect = {{x, y}, {w, h}};
    egui_canvas_draw_text_in_rect(canvas, font, text, &rect, align,
                                  ui_color(rgb), EGUI_ALPHA_100);
}

static inline bool Time32_Reached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

static inline const egui_font_t *ui_heiti_font_get_16() {
    return ui_heiti_font_get(16U);
}

static inline const egui_font_t *ui_heiti_font_get_18() {
    return ui_heiti_font_get(18U);
}

typedef struct
{
    egui_view_t base;
    egui_timer_t timer;
} ui_home_page_t;

typedef struct
{
    int bike_x;
    uint8_t bike_index;
    uint8_t fire_index;
    uint8_t is_valid;
} ui_home_scene_state_t;

typedef struct
{
    int16_t x;
    int16_t y;
    uint8_t asset_index;
} ui_home_sky_entity_t;

#define HOME_SKY_CLOUD_COUNT 3U
#define HOME_SKY_BIRD_COUNT 5U

typedef struct
{
    ui_home_sky_entity_t clouds[HOME_SKY_CLOUD_COUNT];
    ui_home_sky_entity_t birds[HOME_SKY_BIRD_COUNT];
    uint32_t cloud_move_tick;
    uint32_t bird_move_tick;
    uint8_t is_valid;
} ui_home_sky_state_t;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t prev_x;
    int16_t prev_y;
    int8_t drift;
    uint8_t speed;
    uint8_t size;
    uint8_t alpha;
    uint16_t age_ms;
    uint16_t duration_ms;
} ui_home_particle_t;

typedef struct
{
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    uint8_t valid;
} ui_home_dirty_bounds_t;

typedef struct
{
    WeatherScene_t scene;
    uint8_t is_day;
    uint8_t particle_count;
    uint8_t is_valid;
    uint8_t lightning_active;
    uint8_t lightning_shape;
    int16_t lightning_x;
    uint32_t last_particle_tick;
    uint32_t lightning_start_tick;
    uint32_t next_lightning_tick;
} ui_home_weather_state_t;

typedef struct
{
    uint32_t background_rgb;
    uint32_t background_top_rgb;
    uint32_t background_bottom_rgb;
    uint32_t top_text_rgb;
    uint32_t ground_text_rgb;
    uint32_t tint_rgb;
    uint8_t tint_alpha;
} ui_home_style_t;

typedef enum
{
    HOME_BATTERY_STATIC = 0,
    HOME_BATTERY_CHARGING_FILL,
    HOME_BATTERY_CHARGING_HOLD,
    HOME_BATTERY_FULL_FILL,
    HOME_BATTERY_FULL_FLASH,
    HOME_BATTERY_FULL_STABLE
} ui_home_battery_phase_t;

typedef struct
{
    ui_home_battery_phase_t phase;
    uint32_t phase_tick;
    uint8_t display_percent;
    uint8_t full_start_percent;
    uint8_t visible;
    uint8_t full_animation_played;
    uint8_t prev_charging;
    uint8_t prev_full;
    uint8_t resume_pending;
} ui_home_battery_state_t;

static ui_home_page_t s_home_page;
static egui_view_api_t s_home_api;

egui_view_t *ui_HomePage = NULL;
static bool s_home_animation_enabled = true;
static ST7305QuantizeMode s_home_previous_quantize_mode = ST7305QuantizeMode::Bayer4x4;
static bool s_home_quantize_mode_saved = false;
static uint32_t s_home_scene_tick = 0U;
static uint32_t s_home_status_tick = 0U;
static uint32_t s_home_top_carousel_tick = 0U;
static uint32_t s_home_status_version = 0xFFFFFFFFU;
static uint8_t s_home_top_carousel_index = 0U;
static ui_home_scene_state_t s_home_scene_state;
static ui_home_sky_state_t s_home_sky_state;
static ui_home_weather_state_t s_home_weather_state;
static ui_home_battery_state_t s_home_battery_state;
static ui_home_particle_t s_home_particles[30];
static DataApp_HomeStatus_t s_home_render_status;
static ui_home_style_t s_home_render_style;
static WeatherScene_t s_home_render_scene = WEATHER_SCENE_UNKNOWN;
static uint32_t s_home_random_state = 0x6D2B79F5U;
static const egui_font_t *s_home_heiti_16 = NULL;
static uint8_t s_home_heiti_16_ready = 0U;

static void ui_HomePage_on_draw(egui_view_t *self);
static void ui_HomePage_draw_scene(egui_canvas_t *canvas);
static void ui_HomePage_draw_status(egui_canvas_t *canvas,
                                    const DataApp_HomeStatus_t *status,
                                    uint32_t top_text_rgb);
static void ui_HomePage_timer_cb(egui_timer_t *timer);
static void ui_HomePage_sky_reset(uint32_t now);
static void ui_HomePage_sky_update(egui_view_t *view, uint32_t now);
static void ui_HomePage_weather_reset(WeatherScene_t scene, uint8_t is_day, uint32_t now);
static ui_home_style_t ui_HomePage_get_style(WeatherScene_t scene, uint8_t is_day);
static uint8_t ui_HomePage_rgb_luma(uint32_t rgb);
static uint32_t ui_HomePage_mono_rgb(uint32_t rgb);
static uint32_t ui_HomePage_foreground_on_sky_rgb(void);
static uint32_t ui_HomePage_foreground_on_ground_rgb(void);
static void ui_HomePage_update_render_snapshot(const DataApp_HomeStatus_t *status);
static uint8_t ui_HomePage_battery_update(const DataApp_HomeStatus_t *status, uint32_t now, uint8_t restart);
static uint8_t ui_HomePage_warm_date_font(void);
static void ui_HomePage_draw_battery(egui_canvas_t *canvas,
                                     const DataApp_HomeStatus_t *status,
                                     uint32_t top_text_rgb);
static uint8_t ui_HomePage_canvas_intersects(egui_canvas_t *canvas,
                                              egui_dim_t x,
                                              egui_dim_t y,
                                              egui_dim_t w,
                                              egui_dim_t h);

/* Screen bounds for culling */
#define SCREEN_W (int)UI_SCREEN_W

#define HOME_STATUS_TOP_X 0
#define HOME_STATUS_TOP_Y 0
#define HOME_STATUS_TOP_W UI_SCREEN_W
#define HOME_STATUS_TOP_H WEATHER_ICON_SIZE
#define HOME_STATUS_WEATHER_ICON_X (UI_SCREEN_W - WEATHER_ICON_SIZE + 5)
#define HOME_STATUS_WEATHER_ICON_Y -10
#define HOME_STATUS_CAROUSEL_X 232
#define HOME_STATUS_CAROUSEL_Y 7
#define HOME_STATUS_CAROUSEL_W 90
#define HOME_STATUS_CAROUSEL_H 26
#define HOME_TOP_CAROUSEL_INTERVAL_MS 5000U
#define HOME_BOTTOM_STATUS_Y (HOME_GROUND_TILE_Y + 2)
#define HOME_BOTTOM_STATUS_H 24
#define HOME_BOTTOM_STATUS_CENTER_Y (HOME_BOTTOM_STATUS_Y + (HOME_BOTTOM_STATUS_H / 2))
#define HOME_STATUS_ENV_X 150
#define HOME_STATUS_ENV_Y 136
#define HOME_STATUS_ENV_W 148
#define HOME_STATUS_ENV_H 32
#define HOME_STATUS_ENV_TEXT_Y HOME_BOTTOM_STATUS_Y
#define HOME_STATUS_ENV_TEXT_H HOME_BOTTOM_STATUS_H
#define HOME_ENV_HUMIDITY_TEXT_X 170
#define HOME_ENV_HUMIDITY_TEXT_W 40
#define HOME_ENV_TEMPERATURE_TEXT_X 220
#define HOME_ENV_TEMPERATURE_TEXT_W 72
#define HOME_ENV_HUMIDITY_ICON_W 10
#define HOME_ENV_TEMPERATURE_ICON_W 10
#define HOME_ENV_ICON_TEXT_GAP 3
#define HOME_ENV_ICON_Y 141
#define HOME_STATUS_PM25_X 8
#define HOME_STATUS_PM25_Y HOME_BOTTOM_STATUS_Y
#define HOME_STATUS_PM25_W 132
#define HOME_STATUS_PM25_H HOME_BOTTOM_STATUS_H

#define HOME_BATTERY_REGION_X 168
#define HOME_BATTERY_REGION_Y 0
#define HOME_BATTERY_REGION_W 64
#define HOME_BATTERY_REGION_H 34
#define HOME_BATTERY_BODY_X 170
#define HOME_BATTERY_BODY_Y 10
#define HOME_BATTERY_BODY_W 20
#define HOME_BATTERY_BODY_H 12
#define HOME_BATTERY_TERMINAL_X 190
#define HOME_BATTERY_TERMINAL_Y 13
#define HOME_BATTERY_TERMINAL_W 3
#define HOME_BATTERY_TERMINAL_H 5
#define HOME_BATTERY_TEXT_X 197
#define HOME_BATTERY_TEXT_Y 7
#define HOME_BATTERY_TEXT_W 30
#define HOME_BATTERY_TEXT_H 24
#define HOME_BATTERY_CHARGE_FILL_MS 1200U
#define HOME_BATTERY_CHARGE_HOLD_MS 300U
#define HOME_BATTERY_FULL_FILL_MS 350U
#define HOME_BATTERY_FULL_FLASH_STEP_MS 150U
#define HOME_BATTERY_FULL_FLASH_STEPS 4U
#define HOME_TOP_TEXT_DARK_RGB     0x03131F
#define HOME_TOP_TEXT_LIGHT_RGB    0xF4FAFF
#define HOME_GROUND_TEXT_DAY_RGB   0xFFF4D6
#define HOME_GROUND_TEXT_NIGHT_RGB 0xEAF4FF
#define HOME_BATTERY_ACTIVE_RGB    0xFFD166
#define HOME_ENV_COLD_RGB          0x38BDF8
#define HOME_ENV_NORMAL_RGB        0x4ADE80
#define HOME_ENV_WARNING_RGB       0xFACC15
#define HOME_ENV_DANGER_RGB        0xEF4444
#define HOME_TEMP_COLD_END_X10     180
#define HOME_TEMP_NORMAL_START_X10 230
#define HOME_TEMP_NORMAL_END_X10   270
#define HOME_TEMP_WARNING_X10      310
#define HOME_TEMP_DANGER_X10       350
#define HOME_HUMIDITY_DRY_DANGER   20U
#define HOME_HUMIDITY_DRY_WARNING  30U
#define HOME_HUMIDITY_NORMAL_START 40U
#define HOME_HUMIDITY_NORMAL_END   65U
#define HOME_HUMIDITY_WET_WARNING  80U
#define HOME_HUMIDITY_WET_DANGER   90U

#define HOME_SKY_CLOUD_STEP_MS 200U
#define HOME_SKY_BIRD_STEP_MS 100U
#define HOME_SKY_RESPAWN_GAP_MIN 24U
#define HOME_SKY_RESPAWN_GAP_MAX 72U
#define HOME_SCENE_BIKE_STEP_MS 100U
#define HOME_SCENE_FIRE_STEP_MS 100U
#define HOME_BIKE_CYCLE_MS 3600000U
#define HOME_SCENE_RESUME_GAP_MS 250U
#define HOME_WEATHER_STEP_MS 100U
#define HOME_WEATHER_BOTTOM_Y 148
#define HOME_WEATHER_DIRTY_GROUPS 4U
#define HOME_RAIN_LIGHT_COUNT 8U
#define HOME_RAIN_MODERATE_COUNT 12U
#define HOME_RAIN_HEAVY_COUNT 16U
#define HOME_SNOW_COUNT 10U
#define HOME_STAR_CLEAR_COUNT 8U
#define HOME_STAR_CLOUDY_COUNT 4U

#define HOME_BIKE_START_X (int)((((uint32_t)UI_SCREEN_W * 20U) + 50U) / 100U)
#define HOME_BIKE_END_X (int)((((uint32_t)UI_SCREEN_W * 80U) + 50U) / 100U)
#define HOME_BIKE_Y 81
#define HOME_BIKE_W 62
#define HOME_BIKE_H 64
#define HOME_HOUSE_W 74
#define HOME_HOUSE_H 48
#define HOME_FIRE_W 22
#define HOME_FIRE_H 22
#define HOME_CAMP_GAP 4
#define HOME_CAMP_W (HOME_HOUSE_W + HOME_CAMP_GAP + HOME_FIRE_W)
#define HOME_CAMP_CENTER_X (((int)UI_SCREEN_W * 30) / 100)
#define HOME_HOUSE_X (HOME_CAMP_CENTER_X - (HOME_CAMP_W / 2))
#define HOME_HOUSE_Y 101
#define HOME_FIRE_X (HOME_HOUSE_X + HOME_HOUSE_W + HOME_CAMP_GAP)
#define HOME_FIRE_Y 126
#define HOME_GROUND_TILE_Y 136
#define HOME_GROUND_TILE_W 42
#define HOME_GROUND_TILE_H 24
#define HOME_GROUND_BASE_Y 143
#define HOME_GROUND_BASE_W 232
#define HOME_GROUND_BASE_H 25
#define HOME_SKY_BIRD_TOP_Y 32
#define HOME_SKY_BIRD_BOTTOM_Y 56
#define HOME_SKY_CLOUD_TOP_Y 64
#define HOME_SKY_CLOUD_BOTTOM_Y (HOME_GROUND_TILE_Y - 8)

static_assert((HOME_SKY_BIRD_BOTTOM_Y + 8) <= HOME_SKY_CLOUD_TOP_Y,
              "bird and cloud lanes must not overlap");

/* Skip image setup when the current PFB work region does not touch the image. */
static inline void draw_if_visible(const egui_image_std_t *img, egui_canvas_t *canvas,
                                   int x, int y, int w, int h)
{
    if ((img != NULL) &&
        ui_HomePage_canvas_intersects(canvas, (egui_dim_t)x, (egui_dim_t)y, (egui_dim_t)w, (egui_dim_t)h))
    {
        egui_image_draw_image(&img->base, canvas, x, y);
    }
}

static inline void draw_solid_if_visible(const egui_image_std_t *img, egui_canvas_t *canvas,
                                         int x, int y, int w, int h, uint32_t color_rgb)
{
    if ((img != NULL) &&
        ui_HomePage_canvas_intersects(canvas, (egui_dim_t)x, (egui_dim_t)y, (egui_dim_t)w, (egui_dim_t)h))
    {
        egui_image_draw_image_color(&img->base, canvas, x, y, ui_color(color_rgb), EGUI_ALPHA_100);
    }
}

static void ui_HomePage_invalidate_rect(egui_view_t *view,
                                        egui_dim_t x,
                                        egui_dim_t y,
                                        egui_dim_t w,
                                        egui_dim_t h)
{
    egui_region_t region;
    int x1 = x;
    int y1 = y;
    int x2 = x + w;
    int y2 = y + h;

    if ((view == NULL) || (w <= 0) || (h <= 0))
    {
        return;
    }

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)UI_SCREEN_W) x2 = (int)UI_SCREEN_W;
    if (y2 > (int)UI_SCREEN_H) y2 = (int)UI_SCREEN_H;
    if ((x2 <= x1) || (y2 <= y1))
    {
        return;
    }

    x1 &= ~1;
    y1 &= ~3;
    x2 = (x2 + 1) & ~1;
    y2 = (y2 + 3) & ~3;
    if (x2 > (int)UI_SCREEN_W) x2 = (int)UI_SCREEN_W;
    if (y2 > (int)UI_SCREEN_H) y2 = (int)UI_SCREEN_H;

    region.location.x = (egui_dim_t)x1;
    region.location.y = (egui_dim_t)y1;
    region.size.width = (egui_dim_t)(x2 - x1);
    region.size.height = (egui_dim_t)(y2 - y1);
    egui_view_invalidate_region(view, &region);
}

static void ui_HomePage_invalidate_clipped_rect(egui_view_t *view,
                                                int x,
                                                int y,
                                                int w,
                                                int h)
{
    int x2 = x + w;
    int y2 = y + h;

    if ((view == NULL) || (w <= 0) || (h <= 0) || (x2 <= 0) || (y2 <= 0) || (x >= (int)UI_SCREEN_W) || (y >= (int)UI_SCREEN_H))
    {
        return;
    }

    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }
    if (x2 > (int)UI_SCREEN_W)
    {
        x2 = (int)UI_SCREEN_W;
    }
    if (y2 > (int)UI_SCREEN_H)
    {
        y2 = (int)UI_SCREEN_H;
    }

    if ((x2 > x) && (y2 > y))
    {
        ui_HomePage_invalidate_rect(view, (egui_dim_t)x, (egui_dim_t)y, (egui_dim_t)(x2 - x), (egui_dim_t)(y2 - y));
    }
}

static void ui_HomePage_enter_mono_quantize_mode(void)
{
    if (!s_home_quantize_mode_saved)
    {
        s_home_previous_quantize_mode = bsp_display().getQuantizeMode();
        s_home_quantize_mode_saved = true;
    }
    /* Keep scene RGB565 assets as clean silhouettes instead of Bayer dots. */
    bsp_display().setQuantizeMode(ST7305QuantizeMode::MonoAsset);
}

static void ui_HomePage_restore_quantize_mode(void)
{
    if (s_home_quantize_mode_saved)
    {
        bsp_display().setQuantizeMode(s_home_previous_quantize_mode);
        s_home_quantize_mode_saved = false;
    }
}

static WeatherScene_t ui_HomePage_normalize_scene(uint8_t scene)
{
    if (scene <= (uint8_t)WEATHER_SCENE_SNOW)
    {
        return (WeatherScene_t)scene;
    }

    return WEATHER_SCENE_UNKNOWN;
}

static uint8_t ui_HomePage_is_rain_scene(WeatherScene_t scene)
{
    return (uint8_t)((scene == WEATHER_SCENE_LIGHT_RAIN) ||
                     (scene == WEATHER_SCENE_MODERATE_RAIN) ||
                     (scene == WEATHER_SCENE_HEAVY_RAIN));
}

static uint32_t ui_HomePage_random_next(void)
{
    uint32_t value = s_home_random_state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    if (value == 0U)
    {
        value = 0x6D2B79F5U;
    }
    s_home_random_state = value;
    return value;
}

static uint32_t ui_HomePage_random_range(uint32_t min_value, uint32_t max_value)
{
    if (max_value <= min_value)
    {
        return min_value;
    }

    return min_value + (ui_HomePage_random_next() % (max_value - min_value + 1U));
}

static int16_t ui_HomePage_sky_random_y(const home_sky_asset_t *asset,
                                        int top_y,
                                        int bottom_y)
{
    int max_y;

    if (asset == NULL)
    {
        return (int16_t)top_y;
    }
    max_y = bottom_y - (int)asset->height;
    if (max_y <= top_y)
    {
        return (int16_t)top_y;
    }
    return (int16_t)ui_HomePage_random_range((uint32_t)top_y, (uint32_t)max_y);
}

static uint8_t ui_HomePage_sky_next_asset(uint8_t count, uint8_t previous)
{
    uint8_t next;

    if (count <= 1U)
    {
        return 0U;
    }
    next = (uint8_t)ui_HomePage_random_range(0U, count - 2U);
    return (next >= previous) ? (uint8_t)(next + 1U) : next;
}

static void ui_HomePage_sky_reset_group(ui_home_sky_entity_t *entities,
                                        uint8_t entity_count,
                                        uint8_t asset_count,
                                        const home_sky_asset_t *(*get_asset)(uint8_t),
                                        int top_y,
                                        int bottom_y)
{
    int slot_width = (int)UI_SCREEN_W / entity_count;

    for (uint8_t i = 0U; i < entity_count; i++)
    {
        ui_home_sky_entity_t *entity = &entities[i];
        int slot_left = (int)i * slot_width;
        int slot_right = (i == (entity_count - 1U)) ? (int)UI_SCREEN_W : slot_left + slot_width;
        int max_x;
        const home_sky_asset_t *asset;

        entity->asset_index = (uint8_t)ui_HomePage_random_range(0U, asset_count - 1U);
        asset = get_asset(entity->asset_index);
        max_x = slot_right - (int)asset->width;
        entity->x = (int16_t)ui_HomePage_random_range((uint32_t)slot_left,
                                                     (uint32_t)((max_x > slot_left) ? max_x : slot_left));
        entity->y = ui_HomePage_sky_random_y(asset, top_y, bottom_y);
    }
}

static void ui_HomePage_sky_reset(uint32_t now)
{
    uint8_t cloud_asset_count = home_sky_cloud_count();
    uint8_t bird_asset_count = home_sky_bird_count();

    if ((cloud_asset_count == 0U) || (bird_asset_count == 0U))
    {
        memset(&s_home_sky_state, 0, sizeof(s_home_sky_state));
        return;
    }

    ui_HomePage_sky_reset_group(s_home_sky_state.clouds,
                                HOME_SKY_CLOUD_COUNT,
                                cloud_asset_count,
                                home_sky_cloud_get,
                                HOME_SKY_CLOUD_TOP_Y,
                                HOME_SKY_CLOUD_BOTTOM_Y);
    ui_HomePage_sky_reset_group(s_home_sky_state.birds,
                                HOME_SKY_BIRD_COUNT,
                                bird_asset_count,
                                home_sky_bird_get,
                                HOME_SKY_BIRD_TOP_Y,
                                HOME_SKY_BIRD_BOTTOM_Y);
    s_home_sky_state.cloud_move_tick = now;
    s_home_sky_state.bird_move_tick = now;
    s_home_sky_state.is_valid = 1U;
}

static void ui_HomePage_sky_invalidate_entity(egui_view_t *view,
                                               const ui_home_sky_entity_t *entity,
                                               const home_sky_asset_t *asset)
{
    if ((entity != NULL) && (asset != NULL))
    {
        ui_HomePage_invalidate_clipped_rect(view,
                                            entity->x,
                                            entity->y,
                                            (int)asset->width,
                                            (int)asset->height);
    }
}

static void ui_HomePage_sky_move_group(egui_view_t *view,
                                        ui_home_sky_entity_t *entities,
                                        uint8_t entity_count,
                                        uint32_t steps,
                                        uint8_t asset_count,
                                        const home_sky_asset_t *(*get_asset)(uint8_t),
                                        int top_y,
                                        int bottom_y)
{
    for (uint8_t i = 0U; i < entity_count; i++)
    {
        ui_home_sky_entity_t previous = entities[i];
        ui_home_sky_entity_t *entity = &entities[i];
        const home_sky_asset_t *old_asset = get_asset(previous.asset_index);

        entity->x = (int16_t)(entity->x - (int32_t)steps);
        if ((old_asset != NULL) && ((int)entity->x + (int)old_asset->width < 0))
        {
            int rightmost = (int)UI_SCREEN_W;
            const home_sky_asset_t *new_asset;

            entity->asset_index = ui_HomePage_sky_next_asset(asset_count, previous.asset_index);
            new_asset = get_asset(entity->asset_index);
            for (uint8_t peer_index = 0U; peer_index < entity_count; peer_index++)
            {
                const ui_home_sky_entity_t *peer;
                const home_sky_asset_t *peer_asset;
                int peer_right;

                if (peer_index == i)
                {
                    continue;
                }
                peer = &entities[peer_index];
                peer_asset = get_asset(peer->asset_index);
                if (peer_asset == NULL)
                {
                    continue;
                }
                peer_right = (int)peer->x + (int)peer_asset->width;
                if (peer_right > rightmost)
                {
                    rightmost = peer_right;
                }
            }
            entity->x = (int16_t)(rightmost +
                         (int)ui_HomePage_random_range(HOME_SKY_RESPAWN_GAP_MIN,
                                                       HOME_SKY_RESPAWN_GAP_MAX));
            entity->y = ui_HomePage_sky_random_y(new_asset, top_y, bottom_y);
        }

        ui_HomePage_sky_invalidate_entity(view, &previous, old_asset);
        ui_HomePage_sky_invalidate_entity(view, entity, get_asset(entity->asset_index));
    }
}

static void ui_HomePage_sky_update(egui_view_t *view, uint32_t now)
{
    uint32_t cloud_steps;
    uint32_t bird_steps;

    if ((view == NULL) || (s_home_sky_state.is_valid == 0U))
    {
        return;
    }

    cloud_steps = (now - s_home_sky_state.cloud_move_tick) / HOME_SKY_CLOUD_STEP_MS;
    if (cloud_steps != 0U)
    {
        s_home_sky_state.cloud_move_tick += cloud_steps * HOME_SKY_CLOUD_STEP_MS;
        ui_HomePage_sky_move_group(view,
                                   s_home_sky_state.clouds,
                                   HOME_SKY_CLOUD_COUNT,
                                   cloud_steps,
                                   home_sky_cloud_count(),
                                   home_sky_cloud_get,
                                   HOME_SKY_CLOUD_TOP_Y,
                                   HOME_SKY_CLOUD_BOTTOM_Y);
    }

    if (s_home_render_status.is_day == 0U)
    {
        s_home_sky_state.bird_move_tick = now;
        return;
    }

    bird_steps = (now - s_home_sky_state.bird_move_tick) / HOME_SKY_BIRD_STEP_MS;
    if (bird_steps != 0U)
    {
        s_home_sky_state.bird_move_tick += bird_steps * HOME_SKY_BIRD_STEP_MS;
        ui_HomePage_sky_move_group(view,
                                   s_home_sky_state.birds,
                                   HOME_SKY_BIRD_COUNT,
                                   bird_steps,
                                   home_sky_bird_count(),
                                   home_sky_bird_get,
                                   HOME_SKY_BIRD_TOP_Y,
                                   HOME_SKY_BIRD_BOTTOM_Y);
    }
}

static uint8_t ui_HomePage_weather_particle_count(WeatherScene_t scene, uint8_t is_day)
{
    switch (scene)
    {
    case WEATHER_SCENE_LIGHT_RAIN:
        return HOME_RAIN_LIGHT_COUNT;
    case WEATHER_SCENE_MODERATE_RAIN:
        return HOME_RAIN_MODERATE_COUNT;
    case WEATHER_SCENE_HEAVY_RAIN:
        return HOME_RAIN_HEAVY_COUNT;
    case WEATHER_SCENE_SNOW:
        return HOME_SNOW_COUNT;
    case WEATHER_SCENE_CLOUDY:
        return (is_day == 0U) ? HOME_STAR_CLOUDY_COUNT : 0U;
    case WEATHER_SCENE_CLEAR:
        return (is_day == 0U) ? HOME_STAR_CLEAR_COUNT : 0U;
    case WEATHER_SCENE_UNKNOWN:
    default:
        return 0U;
    }
}

static uint8_t ui_HomePage_is_star_scene(WeatherScene_t scene, uint8_t is_day)
{
    return (uint8_t)((is_day == 0U) &&
                     ((scene == WEATHER_SCENE_CLEAR) ||
                      (scene == WEATHER_SCENE_CLOUDY)));
}

static uint8_t ui_HomePage_rain_length(WeatherScene_t scene)
{
    switch (scene)
    {
    case WEATHER_SCENE_HEAVY_RAIN:
        return 18U;
    case WEATHER_SCENE_MODERATE_RAIN:
        return 13U;
    case WEATHER_SCENE_LIGHT_RAIN:
    default:
        return 8U;
    }
}

static void ui_HomePage_particle_init(ui_home_particle_t *particle,
                                      WeatherScene_t scene,
                                      uint8_t is_day,
                                      uint8_t random_y)
{
    if (particle == NULL)
    {
        return;
    }

    particle->x = (int16_t)ui_HomePage_random_range(2U, UI_SCREEN_W - 3U);
    particle->y = (int16_t)((random_y != 0U) ?
                                ui_HomePage_random_range(0U, HOME_WEATHER_BOTTOM_Y) :
                                -(int16_t)ui_HomePage_random_range(2U, 24U));
    particle->drift = 0;
    particle->speed = 0U;
    particle->size = 1U;
    particle->alpha = 255U;
    particle->age_ms = 0U;
    particle->duration_ms = 0U;

    if (scene == WEATHER_SCENE_LIGHT_RAIN)
    {
        particle->drift = -1;
        particle->speed = (uint8_t)ui_HomePage_random_range(4U, 6U);
    }
    else if (scene == WEATHER_SCENE_MODERATE_RAIN)
    {
        particle->drift = -1;
        particle->speed = (uint8_t)ui_HomePage_random_range(7U, 9U);
    }
    else if (scene == WEATHER_SCENE_HEAVY_RAIN)
    {
        particle->drift = (int8_t)(-(int8_t)ui_HomePage_random_range(1U, 2U));
        particle->speed = (uint8_t)ui_HomePage_random_range(10U, 14U);
    }
    else if (scene == WEATHER_SCENE_SNOW)
    {
        particle->drift = (int8_t)((int32_t)ui_HomePage_random_range(0U, 2U) - 1);
        particle->speed = (uint8_t)ui_HomePage_random_range(1U, 3U);
        particle->size = (uint8_t)ui_HomePage_random_range(1U, 2U);
    }
    else if (ui_HomePage_is_star_scene(scene, is_day) != 0U)
    {
        particle->size = (uint8_t)ui_HomePage_random_range(7U, 9U);
        particle->x = (int16_t)ui_HomePage_random_range(particle->size,
                                                       UI_SCREEN_W - particle->size - 1U);
        particle->y = (int16_t)ui_HomePage_random_range(particle->size, 106U);
        particle->duration_ms = (uint16_t)ui_HomePage_random_range(1600U, 3200U);
    }

    particle->prev_x = particle->x;
    particle->prev_y = particle->y;
}

static void ui_HomePage_schedule_lightning(WeatherScene_t scene, uint32_t now)
{
    uint32_t min_ms;
    uint32_t max_ms;

    switch (scene)
    {
    case WEATHER_SCENE_HEAVY_RAIN:
        min_ms = 4000U;
        max_ms = 12000U;
        break;
    case WEATHER_SCENE_MODERATE_RAIN:
        min_ms = 8000U;
        max_ms = 18000U;
        break;
    case WEATHER_SCENE_LIGHT_RAIN:
    default:
        min_ms = 12000U;
        max_ms = 24000U;
        break;
    }

    s_home_weather_state.next_lightning_tick = now + ui_HomePage_random_range(min_ms, max_ms);
}

static void ui_HomePage_weather_reset(WeatherScene_t scene, uint8_t is_day, uint32_t now)
{
    uint8_t count = ui_HomePage_weather_particle_count(scene, is_day);

    s_home_random_state ^= now ^ ((uint32_t)scene << 24) ^ ((uint32_t)is_day << 16);
    if (s_home_random_state == 0U)
    {
        s_home_random_state = 0x6D2B79F5U;
    }

    s_home_weather_state.scene = scene;
    s_home_weather_state.is_day = (is_day != 0U) ? 1U : 0U;
    s_home_weather_state.particle_count = count;
    s_home_weather_state.is_valid = 1U;
    s_home_weather_state.lightning_active = 0U;
    s_home_weather_state.last_particle_tick = now;
    s_home_weather_state.lightning_start_tick = 0U;
    s_home_weather_state.next_lightning_tick = 0U;

    for (uint8_t i = 0U; i < count; i++)
    {
        ui_HomePage_particle_init(&s_home_particles[i], scene, is_day, 1U);
        if (ui_HomePage_is_star_scene(scene, is_day) != 0U)
        {
            s_home_particles[i].age_ms = (uint16_t)ui_HomePage_random_range(
                0U, s_home_particles[i].duration_ms - 1U);
        }
    }

    if (ui_HomePage_is_rain_scene(scene) != 0U)
    {
        ui_HomePage_schedule_lightning(scene, now);
    }
}

static void ui_HomePage_dirty_add(ui_home_dirty_bounds_t *groups,
                                  int x,
                                  int y,
                                  int w,
                                  int h)
{
    int center_x;
    uint8_t group_index;
    ui_home_dirty_bounds_t *bounds;

    if ((groups == NULL) || (w <= 0) || (h <= 0))
    {
        return;
    }

    center_x = x + (w / 2);
    if (center_x < 0)
    {
        center_x = 0;
    }
    else if (center_x >= (int)UI_SCREEN_W)
    {
        center_x = (int)UI_SCREEN_W - 1;
    }
    group_index = (uint8_t)(((uint32_t)center_x * HOME_WEATHER_DIRTY_GROUPS) / UI_SCREEN_W);
    if (group_index >= HOME_WEATHER_DIRTY_GROUPS)
    {
        group_index = HOME_WEATHER_DIRTY_GROUPS - 1U;
    }

    bounds = &groups[group_index];
    if (bounds->valid == 0U)
    {
        bounds->min_x = x;
        bounds->min_y = y;
        bounds->max_x = x + w;
        bounds->max_y = y + h;
        bounds->valid = 1U;
        return;
    }

    if (x < bounds->min_x) bounds->min_x = x;
    if (y < bounds->min_y) bounds->min_y = y;
    if ((x + w) > bounds->max_x) bounds->max_x = x + w;
    if ((y + h) > bounds->max_y) bounds->max_y = y + h;
}

static void ui_HomePage_particle_dirty_add(ui_home_dirty_bounds_t *groups,
                                           const ui_home_particle_t *particle,
                                           WeatherScene_t scene)
{
    int radius;
    int length;

    if ((groups == NULL) || (particle == NULL))
    {
        return;
    }

    if (ui_HomePage_is_rain_scene(scene) != 0U)
    {
        length = ui_HomePage_rain_length(scene);
        ui_HomePage_dirty_add(groups, particle->prev_x - 4, particle->prev_y - 2, 9, length + 5);
        ui_HomePage_dirty_add(groups, particle->x - 4, particle->y - 2, 9, length + 5);
    }
    else
    {
        radius = (int)particle->size + 2;
        ui_HomePage_dirty_add(groups, particle->prev_x - radius, particle->prev_y - radius,
                              (radius * 2) + 1, (radius * 2) + 1);
        ui_HomePage_dirty_add(groups, particle->x - radius, particle->y - radius,
                              (radius * 2) + 1, (radius * 2) + 1);
    }
}

static void ui_HomePage_weather_update_particles(egui_view_t *view, uint32_t now)
{
    ui_home_dirty_bounds_t dirty[HOME_WEATHER_DIRTY_GROUPS] = {0};
    WeatherScene_t scene = s_home_weather_state.scene;
    uint8_t is_star_scene = ui_HomePage_is_star_scene(scene, s_home_weather_state.is_day);

    if ((uint32_t)(now - s_home_weather_state.last_particle_tick) < HOME_WEATHER_STEP_MS)
    {
        return;
    }
    s_home_weather_state.last_particle_tick = now;

    for (uint8_t i = 0U; i < s_home_weather_state.particle_count; i++)
    {
        ui_home_particle_t *particle = &s_home_particles[i];

        particle->prev_x = particle->x;
        particle->prev_y = particle->y;

        if (ui_HomePage_is_rain_scene(scene) != 0U)
        {
            particle->x = (int16_t)(particle->x + particle->drift);
            particle->y = (int16_t)(particle->y + particle->speed);
            if ((particle->y > HOME_WEATHER_BOTTOM_Y) || (particle->x < -4))
            {
                int16_t old_x = particle->prev_x;
                int16_t old_y = particle->prev_y;

                ui_HomePage_particle_init(particle, scene, s_home_weather_state.is_day, 0U);
                particle->prev_x = old_x;
                particle->prev_y = old_y;
            }
        }
        else if (scene == WEATHER_SCENE_SNOW)
        {
            particle->x = (int16_t)(particle->x + particle->drift);
            particle->y = (int16_t)(particle->y + particle->speed);
            if ((particle->y > HOME_WEATHER_BOTTOM_Y) || (particle->x < -3) || (particle->x > ((int)UI_SCREEN_W + 3)))
            {
                int16_t old_x = particle->prev_x;
                int16_t old_y = particle->prev_y;

                ui_HomePage_particle_init(particle, scene, s_home_weather_state.is_day, 0U);
                particle->prev_x = old_x;
                particle->prev_y = old_y;
            }
        }
        else if (is_star_scene != 0U)
        {
            particle->age_ms = (uint16_t)(particle->age_ms + HOME_WEATHER_STEP_MS);
            if (particle->age_ms >= particle->duration_ms)
            {
                int16_t old_x = particle->prev_x;
                int16_t old_y = particle->prev_y;

                ui_HomePage_particle_init(particle, scene, s_home_weather_state.is_day, 1U);
                particle->prev_x = old_x;
                particle->prev_y = old_y;
            }
        }

        ui_HomePage_particle_dirty_add(dirty, particle, scene);
    }

    for (uint8_t i = 0U; i < HOME_WEATHER_DIRTY_GROUPS; i++)
    {
        if (dirty[i].valid != 0U)
        {
            ui_HomePage_invalidate_clipped_rect(view,
                                                dirty[i].min_x,
                                                dirty[i].min_y,
                                                dirty[i].max_x - dirty[i].min_x,
                                                dirty[i].max_y - dirty[i].min_y);
        }
    }
}

static void ui_HomePage_weather_update_lightning(egui_view_t *view, uint32_t now)
{
    if (ui_HomePage_is_rain_scene(s_home_weather_state.scene) == 0U)
    {
        s_home_weather_state.lightning_active = 0U;
        return;
    }

    if (s_home_weather_state.lightning_active != 0U)
    {
        if ((uint32_t)(now - s_home_weather_state.lightning_start_tick) >= 260U)
        {
            s_home_weather_state.lightning_active = 0U;
            ui_HomePage_schedule_lightning(s_home_weather_state.scene, now);
        }
        egui_view_invalidate_full(view);
        return;
    }

    if ((int32_t)(now - s_home_weather_state.next_lightning_tick) >= 0)
    {
        s_home_weather_state.lightning_active = 1U;
        s_home_weather_state.lightning_start_tick = now;
        s_home_weather_state.lightning_x = (int16_t)ui_HomePage_random_range(70U, UI_SCREEN_W - 70U);
        s_home_weather_state.lightning_shape = (uint8_t)ui_HomePage_random_range(0U, 3U);
        egui_view_invalidate_full(view);
    }
}

static uint8_t ui_HomePage_scene_bike_index(uint32_t tick)
{
    return (uint8_t)((tick / HOME_SCENE_BIKE_STEP_MS) % 4U);
}

static int ui_HomePage_scene_bike_x(uint32_t tick)
{
    uint32_t cycle_tick = tick % HOME_BIKE_CYCLE_MS;
    int range = HOME_BIKE_END_X - HOME_BIKE_START_X;

    if (range <= 0)
    {
        return HOME_BIKE_START_X;
    }

    return HOME_BIKE_START_X + (int)(((cycle_tick * (uint32_t)range) + (HOME_BIKE_CYCLE_MS / 2U)) / HOME_BIKE_CYCLE_MS);
}

static uint8_t ui_HomePage_scene_fire_index(uint32_t tick)
{
    return (uint8_t)((tick / HOME_SCENE_FIRE_STEP_MS) % 4U);
}

static void ui_HomePage_get_scene_state(uint32_t tick, ui_home_scene_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->bike_x = ui_HomePage_scene_bike_x(tick);
    state->bike_index = ui_HomePage_scene_bike_index(tick);
    state->fire_index = ui_HomePage_scene_fire_index(tick);
    state->is_valid = 1U;
}

static void ui_HomePage_union_clipped_bounds(int x,
                                             int y,
                                             int w,
                                             int h,
                                             int *min_x,
                                             int *min_y,
                                             int *max_x,
                                             int *max_y)
{
    int x2 = x + w;
    int y2 = y + h;

    if ((w <= 0) || (h <= 0) || (x2 <= 0) || (y2 <= 0) || (x >= (int)UI_SCREEN_W) || (y >= (int)UI_SCREEN_H))
    {
        return;
    }

    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }
    if (x2 > (int)UI_SCREEN_W)
    {
        x2 = (int)UI_SCREEN_W;
    }
    if (y2 > (int)UI_SCREEN_H)
    {
        y2 = (int)UI_SCREEN_H;
    }

    if ((x2 <= x) || (y2 <= y))
    {
        return;
    }

    if (x < *min_x)
    {
        *min_x = x;
    }
    if (y < *min_y)
    {
        *min_y = y;
    }
    if (x2 > *max_x)
    {
        *max_x = x2;
    }
    if (y2 > *max_y)
    {
        *max_y = y2;
    }
}

static void ui_HomePage_invalidate_precise_scene(egui_view_t *view, const ui_home_scene_state_t *prev, const ui_home_scene_state_t *next)
{
    if ((view == NULL) || (prev == NULL) || (next == NULL))
    {
        return;
    }

    if (s_home_render_status.is_day != 0U)
    {
        if ((prev->bike_index != next->bike_index) || (prev->bike_x != next->bike_x))
        {
            ui_HomePage_invalidate_clipped_rect(view, prev->bike_x, HOME_BIKE_Y, HOME_BIKE_W, HOME_BIKE_H);
            ui_HomePage_invalidate_clipped_rect(view, next->bike_x, HOME_BIKE_Y, HOME_BIKE_W, HOME_BIKE_H);
        }
    }
    else if ((prev->fire_index != next->fire_index) &&
             (ui_HomePage_is_rain_scene(s_home_render_scene) == 0U))
    {
        ui_HomePage_invalidate_clipped_rect(view, HOME_FIRE_X, HOME_FIRE_Y, HOME_FIRE_W, HOME_FIRE_H);
    }
}

static void ui_HomePage_invalidate_status_regions(egui_view_t *view)
{
    ui_HomePage_invalidate_rect(view, HOME_STATUS_TOP_X, HOME_STATUS_TOP_Y, HOME_STATUS_TOP_W, HOME_STATUS_TOP_H);
}

static void ui_HomePage_invalidate_battery(egui_view_t *view)
{
    ui_HomePage_invalidate_rect(view,
                                HOME_BATTERY_REGION_X,
                                HOME_BATTERY_REGION_Y,
                                HOME_BATTERY_REGION_W,
                                HOME_BATTERY_REGION_H);
}

static uint8_t ui_HomePage_battery_update(const DataApp_HomeStatus_t *status,
                                          uint32_t now,
                                          uint8_t restart)
{
    ui_home_battery_state_t *state = &s_home_battery_state;
    uint8_t old_percent;
    uint8_t old_visible;
    uint8_t old_active;
    uint8_t active;
    uint32_t elapsed;

    if (status == NULL)
    {
        return 0U;
    }

    old_percent = state->display_percent;
    old_visible = state->visible;
    old_active = (uint8_t)(state->prev_charging || state->prev_full);
    active = (uint8_t)(status->charging || status->charge_full);

    if (status->charge_full != 0U)
    {
        if ((state->prev_full == 0U) && (state->full_animation_played == 0U))
        {
            state->phase = HOME_BATTERY_FULL_FILL;
            state->phase_tick = now;
            state->full_start_percent = state->display_percent;
            state->visible = 1U;
        }

        if (state->phase == HOME_BATTERY_FULL_FILL)
        {
            elapsed = now - state->phase_tick;
            if (elapsed >= HOME_BATTERY_FULL_FILL_MS)
            {
                state->display_percent = 100U;
                state->phase = HOME_BATTERY_FULL_FLASH;
                state->phase_tick = now;
            }
            else
            {
                state->display_percent = (uint8_t)(state->full_start_percent +
                    (((uint32_t)(100U - state->full_start_percent) * elapsed) / HOME_BATTERY_FULL_FILL_MS));
            }
        }
        else if (state->phase == HOME_BATTERY_FULL_FLASH)
        {
            uint32_t flash_step = (now - state->phase_tick) / HOME_BATTERY_FULL_FLASH_STEP_MS;

            if (flash_step >= HOME_BATTERY_FULL_FLASH_STEPS)
            {
                state->phase = HOME_BATTERY_FULL_STABLE;
                state->display_percent = 100U;
                state->visible = 1U;
                state->full_animation_played = 1U;
            }
            else
            {
                state->visible = (uint8_t)((flash_step & 1U) != 0U);
            }
        }
        else if (state->phase == HOME_BATTERY_FULL_STABLE)
        {
            state->display_percent = 100U;
            state->visible = 1U;
        }
    }
    else if (status->charging != 0U)
    {
        if ((state->prev_charging == 0U) || (restart != 0U) ||
            ((state->phase != HOME_BATTERY_CHARGING_FILL) &&
             (state->phase != HOME_BATTERY_CHARGING_HOLD)))
        {
            state->phase = HOME_BATTERY_CHARGING_FILL;
            state->phase_tick = now;
            state->display_percent = 0U;
            state->visible = 1U;
            state->full_animation_played = 0U;
        }

        if (state->phase == HOME_BATTERY_CHARGING_FILL)
        {
            elapsed = now - state->phase_tick;
            if (elapsed >= HOME_BATTERY_CHARGE_FILL_MS)
            {
                state->display_percent = status->battery_percent;
                state->phase = HOME_BATTERY_CHARGING_HOLD;
                state->phase_tick = now;
            }
            else
            {
                state->display_percent = (uint8_t)(((uint32_t)status->battery_percent * elapsed) /
                                                   HOME_BATTERY_CHARGE_FILL_MS);
            }
        }
        else if ((now - state->phase_tick) >= HOME_BATTERY_CHARGE_HOLD_MS)
        {
            state->phase = HOME_BATTERY_CHARGING_FILL;
            state->phase_tick = now;
            state->display_percent = 0U;
        }
    }
    else
    {
        state->phase = HOME_BATTERY_STATIC;
        state->phase_tick = now;
        state->display_percent = status->battery_percent;
        state->visible = 1U;
        state->full_animation_played = 0U;
    }

    state->prev_charging = status->charging;
    state->prev_full = status->charge_full;
    state->resume_pending = 0U;

    return (uint8_t)((old_percent != state->display_percent) ||
                     (old_visible != state->visible) ||
                     (old_active != active));
}

void ui_HomePage_screen_init(void)
{
    egui_view_t *view = EGUI_VIEW_OF(&s_home_page.base);
    DataApp_HomeStatus_t status;

    ui_HomePage = view;
    ui_HomePage_enter_mono_quantize_mode();
    egui_view_init(view, egui_port_core());
    egui_view_copy_api(view, &s_home_api);
    s_home_api.on_draw = ui_HomePage_on_draw;
    view->api = &s_home_api;
    egui_view_set_position(view, 0, 0);
    egui_view_set_size(view, UI_SCREEN_W, UI_SCREEN_H);
    egui_view_set_visible(view, 1);
    s_home_animation_enabled = true;
    s_home_scene_tick = egui_timer_get_current_time();
    s_home_status_tick = s_home_scene_tick;
    s_home_top_carousel_tick = s_home_scene_tick;
    s_home_top_carousel_index = 0U;
    s_home_status_version = 0xFFFFFFFFU;
    s_home_scene_state.is_valid = 0U;
    s_home_weather_state.is_valid = 0U;
    memset(&s_home_battery_state, 0, sizeof(s_home_battery_state));
    s_home_battery_state.visible = 1U;
    s_home_heiti_16 = NULL;
    s_home_heiti_16_ready = ui_HomePage_warm_date_font();
    DataApp_HomeStatus_Get(&status);
    ui_HomePage_update_render_snapshot(&status);
    (void)ui_HomePage_battery_update(&status, s_home_scene_tick, 1U);
    ui_HomePage_weather_reset(s_home_render_scene, status.is_day, s_home_scene_tick);
    ui_HomePage_sky_reset(s_home_scene_tick);
    egui_view_start_periodic(view, &s_home_page.timer, view, ui_HomePage_timer_cb, 50U);
}

void ui_HomePage_screen_enter(void)
{
    DataApp_HomeStatus_t status;

    s_home_animation_enabled = true;
    ui_HomePage_enter_mono_quantize_mode();
    s_home_scene_tick = egui_timer_get_current_time();
    s_home_status_tick = s_home_scene_tick;
    s_home_top_carousel_tick = s_home_scene_tick;
    s_home_top_carousel_index = 0U;
    s_home_scene_state.is_valid = 0U;
    s_home_weather_state.is_valid = 0U;
    s_home_heiti_16_ready = ui_HomePage_warm_date_font();
    DataApp_HomeStatus_Get(&status);
    ui_HomePage_update_render_snapshot(&status);
    (void)ui_HomePage_battery_update(&status, s_home_scene_tick, 1U);
    ui_HomePage_weather_reset(s_home_render_scene, status.is_day, s_home_scene_tick);
    ui_HomePage_sky_reset(s_home_scene_tick);

}

void ui_HomePage_screen_destroy(void)
{
    s_home_animation_enabled = false;
    ui_HomePage_restore_quantize_mode();
    if (s_home_battery_state.prev_charging != 0U)
    {
        s_home_battery_state.resume_pending = 1U;
    }
}

static bool ui_HomePage_key_consume(const KeyEvent &event)
{
    if (event.gesture == KeyGesture::DoubleClick) {
        if (event.id == KeyId::Middle)
        {
            return task_post_player_command(PlayerCommandType::Toggle, 0, true);
        }
        if (event.id == KeyId::Left)
        {
            return task_post_player_command(PlayerCommandType::Previous, 0, true);
        }
        if (event.id == KeyId::Right)
        {
            return task_post_player_command(PlayerCommandType::Next, 0, true);
        }
    }

    if (event.gesture == KeyGesture::LongPress) {
        if (event.id == KeyId::Left)
        {
            return task_post_player_command(PlayerCommandType::ChangeVolume, -1, true);
        }
        if (event.id == KeyId::Right)
        {
            return task_post_player_command(PlayerCommandType::ChangeVolume, 1, true);
        }
    }

    return false;
}

void ui_HomePage_set_animation_enabled(bool enable)
{
    bool next = enable ? true : false;

    if (s_home_animation_enabled == next)
    {
        return;
    }

    s_home_animation_enabled = next;
    if (!s_home_animation_enabled)
    {
        if (s_home_battery_state.prev_charging != 0U)
        {
            s_home_battery_state.resume_pending = 1U;
        }
        return;
    }

    if (s_home_animation_enabled)
    {
        DataApp_HomeStatus_t status;

        s_home_scene_tick = egui_timer_get_current_time();
        s_home_scene_state.is_valid = 0U;
        DataApp_HomeStatus_Get(&status);
        ui_HomePage_update_render_snapshot(&status);
        (void)ui_HomePage_battery_update(&status, s_home_scene_tick, 1U);
        ui_HomePage_weather_reset(s_home_render_scene, status.is_day, s_home_scene_tick);
        ui_HomePage_sky_reset(s_home_scene_tick);
        if ((ui_HomePage != NULL) && egui_view_get_visible(ui_HomePage))
        {
            egui_view_invalidate_full(ui_HomePage);
        }
    }
}

bool ui_HomePage_get_animation_enabled(void)
{
    return s_home_animation_enabled;
}

static void ui_HomePage_timer_cb(egui_timer_t *timer)
{
    egui_view_t *view = (egui_view_t *)timer->user_data;
    DataApp_HomeStatus_t status;
    uint32_t now;
    uint8_t refresh_status = 0U;
    uint8_t refresh_full = 0U;
    WeatherScene_t scene;

    if ((view == NULL) || !egui_view_get_visible(view))
    {
        now = egui_timer_get_current_time();
        s_home_scene_state.is_valid = 0U;
        s_home_weather_state.is_valid = 0U;
        if ((s_home_battery_state.phase == HOME_BATTERY_CHARGING_FILL) ||
            (s_home_battery_state.phase == HOME_BATTERY_CHARGING_HOLD) ||
            (s_home_battery_state.phase == HOME_BATTERY_FULL_FILL) ||
            (s_home_battery_state.phase == HOME_BATTERY_FULL_FLASH))
        {
            s_home_battery_state.phase_tick = now;
        }
        if (s_home_battery_state.prev_charging != 0U)
        {
            s_home_battery_state.resume_pending = 1U;
        }
        return;
    }

    now = egui_timer_get_current_time();
    DataApp_HomeStatus_Get(&status);
    ui_HomePage_update_render_snapshot(&status);
    {
        const uint8_t heiti_ready = ui_HomePage_warm_date_font();
        if (heiti_ready != s_home_heiti_16_ready)
        {
            s_home_heiti_16_ready = heiti_ready;
            refresh_status = 1U;
        }
    }
    if (s_home_animation_enabled || (!status.charging && !status.charge_full))
    {
        if (ui_HomePage_battery_update(&status, now, s_home_battery_state.resume_pending) != 0U)
        {
            ui_HomePage_invalidate_battery(view);
        }
    }
    else
    {
        s_home_battery_state.phase_tick = now;
    }
    scene = s_home_render_scene;
    if ((s_home_weather_state.is_valid == 0U) ||
        (s_home_weather_state.scene != scene) ||
        (s_home_weather_state.is_day != status.is_day))
    {
        ui_HomePage_weather_reset(scene, status.is_day, now);
        refresh_full = 1U;
    }
    if (status.version != s_home_status_version)
    {
        s_home_status_version = status.version;
        refresh_status = 1U;
    }
    if ((now - s_home_top_carousel_tick) >= HOME_TOP_CAROUSEL_INTERVAL_MS)
    {
        s_home_top_carousel_tick = now;
        s_home_top_carousel_index = (uint8_t)((s_home_top_carousel_index + 1U) % 3U);
        refresh_status = 1U;
    }
    if ((now - s_home_status_tick) >= 1000U)
    {
        s_home_status_tick = now;
        refresh_status = 1U;
    }

    if (s_home_animation_enabled)
    {
        ui_home_scene_state_t next_scene_state;

        ui_HomePage_get_scene_state(now, &next_scene_state);
        if ((s_home_scene_state.is_valid != 0U) && ((now - s_home_scene_tick) > HOME_SCENE_RESUME_GAP_MS))
        {
            s_home_scene_state.is_valid = 0U;
            ui_HomePage_sky_reset(now);
        }
        if (s_home_scene_state.is_valid == 0U)
        {
            if (s_home_sky_state.is_valid == 0U)
            {
                ui_HomePage_sky_reset(now);
            }
            egui_view_invalidate_full(view);
        }
        else
        {
            ui_HomePage_sky_update(view, now);
            ui_HomePage_invalidate_precise_scene(view, &s_home_scene_state, &next_scene_state);
        }
        s_home_scene_state = next_scene_state;
        s_home_scene_tick = now;

        ui_HomePage_weather_update_particles(view, now);
        ui_HomePage_weather_update_lightning(view, now);

        if (refresh_full != 0U)
        {
            egui_view_invalidate_full(view);
        }
        else if (refresh_status != 0U)
        {
            ui_HomePage_invalidate_status_regions(view);
        }
    }
    else if (refresh_full != 0U)
    {
        s_home_scene_tick = now;
        egui_view_invalidate_full(view);
    }
    else if (refresh_status != 0U)
    {
        ui_HomePage_invalidate_status_regions(view);
    }
}

static void ui_HomePage_draw_sky_group(egui_canvas_t *canvas,
                                       const ui_home_sky_entity_t *entities,
                                       uint8_t entity_count,
                                       const home_sky_asset_t *(*get_asset)(uint8_t))
{
    if ((canvas == NULL) || (entities == NULL) || (get_asset == NULL))
    {
        return;
    }

    for (uint8_t i = 0U; i < entity_count; i++)
    {
        const home_sky_asset_t *asset = get_asset(entities[i].asset_index);

        if (asset != NULL)
        {
            draw_solid_if_visible(asset->image,
                                  canvas,
                                  entities[i].x,
                                  entities[i].y,
                                  asset->width,
                                  asset->height,
                                  0x000000);
        }
    }
}

static void ui_HomePage_draw_scene(egui_canvas_t *canvas)
{
#if EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_RGB565

    static const egui_image_std_t *const bikes[] =
    {
        &qoi_scene_bike1,
        &qoi_scene_bike2,
        &qoi_scene_bike3,
        &qoi_scene_bike4,
    };
    static const egui_image_std_t *const fires[] =
    {
        &home_camp_fire1,
        &home_camp_fire2,
        &home_camp_fire3,
        &home_camp_fire4,
    };

    uint32_t tick = s_home_scene_tick;
    uint8_t bike_index = ui_HomePage_scene_bike_index(tick);
    uint8_t fire_index = ui_HomePage_scene_fire_index(tick);
    int bike_x = ui_HomePage_scene_bike_x(tick);

    /* 云：慢速滚动，裁掉屏幕外的副本 */
    if (s_home_sky_state.is_valid != 0U)
    {
        ui_HomePage_draw_sky_group(canvas,
                                   s_home_sky_state.clouds,
                                   HOME_SKY_CLOUD_COUNT,
                                   home_sky_cloud_get);
        if (s_home_render_status.is_day != 0U)
        {
            ui_HomePage_draw_sky_group(canvas,
                                       s_home_sky_state.birds,
                                       HOME_SKY_BIRD_COUNT,
                                       home_sky_bird_get);
        }
    }

    /* 地面：只画屏幕内可见的瓦片 */
    for (int x = 0; x < SCREEN_W; x += 40)
    {
        draw_if_visible(&qoi_scene_grass, canvas, x, HOME_GROUND_TILE_Y,
                        HOME_GROUND_TILE_W, HOME_GROUND_TILE_H);
    }

    draw_if_visible(&qoi_scene_grass0, canvas, 0, HOME_GROUND_BASE_Y,
                    HOME_GROUND_BASE_W, HOME_GROUND_BASE_H);
    draw_if_visible(&qoi_scene_grass0, canvas, 195, HOME_GROUND_BASE_Y,
                    HOME_GROUND_BASE_W, HOME_GROUND_BASE_H);

    if (s_home_render_status.is_day != 0U)
    {
        draw_if_visible(bikes[bike_index], canvas, bike_x, HOME_BIKE_Y, HOME_BIKE_W, HOME_BIKE_H);
    }
    else
    {
        draw_if_visible(&home_camp_house, canvas, HOME_HOUSE_X, HOME_HOUSE_Y, HOME_HOUSE_W, HOME_HOUSE_H);
        if (ui_HomePage_is_rain_scene(s_home_render_scene) == 0U)
        {
            draw_if_visible(fires[fire_index], canvas, HOME_FIRE_X, HOME_FIRE_Y, HOME_FIRE_W, HOME_FIRE_H);
        }
    }

#else
    EGUI_UNUSED(canvas);
#endif
}

static ui_home_style_t ui_HomePage_get_style(WeatherScene_t scene, uint8_t is_day)
{
    ui_home_style_t style;

    if (is_day != 0U)
    {
        style.background_rgb = 0x87CEEB;
        style.background_top_rgb = 0x67BCE4;
        style.background_bottom_rgb = 0xA8DEF1;
        style.top_text_rgb = HOME_TOP_TEXT_DARK_RGB;
        style.ground_text_rgb = HOME_GROUND_TEXT_DAY_RGB;
        style.tint_rgb = 0x000000;
        style.tint_alpha = 0U;

    }
    else
    {
        style.background_rgb = 0xFFFFFF;
        style.background_top_rgb = 0xFFFFFF;
        style.background_bottom_rgb = 0xFFFFFF;
        style.top_text_rgb = HOME_TOP_TEXT_DARK_RGB;
        style.ground_text_rgb = HOME_TOP_TEXT_DARK_RGB;
        style.tint_rgb = 0xFFFFFF;
        style.tint_alpha = 0U;
    }

    // Weather changes icons and particles, never the monochrome background.
    EGUI_UNUSED(scene);

    return style;
}

static uint32_t ui_HomePage_mix_rgb(uint32_t from_rgb, uint32_t to_rgb, uint8_t amount)
{
    uint32_t inverse = 255U - amount;
    uint32_t red = ((((from_rgb >> 16) & 0xFFU) * inverse) +
                    (((to_rgb >> 16) & 0xFFU) * amount) + 127U) / 255U;
    uint32_t green = ((((from_rgb >> 8) & 0xFFU) * inverse) +
                      (((to_rgb >> 8) & 0xFFU) * amount) + 127U) / 255U;
    uint32_t blue = (((from_rgb & 0xFFU) * inverse) +
                     ((to_rgb & 0xFFU) * amount) + 127U) / 255U;

    return (red << 16) | (green << 8) | blue;
}

static uint8_t ui_HomePage_rgb_luma(uint32_t rgb)
{
    uint32_t red = (rgb >> 16) & 0xFFU;
    uint32_t green = (rgb >> 8) & 0xFFU;
    uint32_t blue = rgb & 0xFFU;

    return (uint8_t)(((red * 77U) + (green * 150U) + (blue * 29U) + 128U) >> 8);
}

static void ui_HomePage_update_render_snapshot(const DataApp_HomeStatus_t *status)
{
    if (status == NULL)
    {
        return;
    }

    s_home_render_status = *status;
    s_home_render_scene = ui_HomePage_normalize_scene(status->weather_scene);
    s_home_render_style = ui_HomePage_get_style(s_home_render_scene, status->is_day);
}

static uint16_t ui_HomePage_star_progress(const ui_home_particle_t *particle)
{
    uint16_t half_duration;

    if ((particle == NULL) || (particle->duration_ms < 2U))
    {
        return 0U;
    }

    half_duration = (uint16_t)(particle->duration_ms / 2U);
    if (particle->age_ms <= half_duration)
    {
        return (uint16_t)(((uint32_t)particle->age_ms * 1000U) / half_duration);
    }

    return (uint16_t)(((uint32_t)(particle->duration_ms - particle->age_ms) * 1000U) /
                      (particle->duration_ms - half_duration));
}

static uint8_t ui_HomePage_star_radius(const ui_home_particle_t *particle)
{
    uint8_t minimum_radius;
    uint16_t progress;

    if (particle == NULL)
    {
        return 1U;
    }

    minimum_radius = (uint8_t)((particle->size + 2U) / 3U);
    progress = ui_HomePage_star_progress(particle);
    return (uint8_t)(minimum_radius +
                     (((uint32_t)(particle->size - minimum_radius) * progress + 500U) / 1000U));
}

static egui_alpha_t ui_HomePage_star_alpha(const ui_home_particle_t *particle)
{
    const uint32_t minimum_alpha = ((uint32_t)EGUI_ALPHA_100 * 25U) / 100U;
    const uint32_t progress = ui_HomePage_star_progress(particle);
    return (egui_alpha_t)(minimum_alpha +
                          ((((uint32_t)EGUI_ALPHA_100 - minimum_alpha) * progress + 500U) /
                           1000U));
}

static void ui_HomePage_draw_star(egui_canvas_t *canvas,
                                  const ui_home_particle_t *particle)
{
    const int radius = (int)ui_HomePage_star_radius(particle);
    const int waist = (radius > 3) ? ((radius + 2) / 3) : 1;
    const egui_dim_t points[] = {
        particle->x,         (egui_dim_t)(particle->y - radius),
        (egui_dim_t)(particle->x + waist), (egui_dim_t)(particle->y - waist),
        (egui_dim_t)(particle->x + radius), particle->y,
        (egui_dim_t)(particle->x + waist), (egui_dim_t)(particle->y + waist),
        particle->x,         (egui_dim_t)(particle->y + radius),
        (egui_dim_t)(particle->x - waist), (egui_dim_t)(particle->y + waist),
        (egui_dim_t)(particle->x - radius), particle->y,
        (egui_dim_t)(particle->x - waist), (egui_dim_t)(particle->y - waist),
    };

    egui_canvas_draw_polygon_fill(canvas, points, 8U,
                                  ui_color(0xFFF4C2),
                                  ui_HomePage_star_alpha(particle));
}

static void ui_HomePage_draw_weather_particles(egui_canvas_t *canvas)
{
    WeatherScene_t scene = s_home_weather_state.scene;
    uint8_t is_star_scene = ui_HomePage_is_star_scene(scene, s_home_weather_state.is_day);

    if ((canvas == NULL) || (s_home_weather_state.is_valid == 0U))
    {
        return;
    }

    for (uint8_t i = 0U; i < s_home_weather_state.particle_count; i++)
    {
        const ui_home_particle_t *particle = &s_home_particles[i];

        if (ui_HomePage_is_rain_scene(scene) != 0U)
        {
            uint8_t length = ui_HomePage_rain_length(scene);
            uint32_t color = ui_HomePage_foreground_on_sky_rgb();

            if (!ui_HomePage_canvas_intersects(canvas, particle->x - 4, particle->y - 2, 9, length + 5))
            {
                continue;
            }
            egui_canvas_draw_line(canvas,
                                  particle->x,
                                  particle->y,
                                  (egui_dim_t)(particle->x - 3),
                                  (egui_dim_t)(particle->y + length),
                                  1,
                                  ui_color(color),
                                  EGUI_ALPHA_100);
        }
        else if (scene == WEATHER_SCENE_SNOW)
        {
            int radius = (int)particle->size + 2;

            if (!ui_HomePage_canvas_intersects(canvas,
                                               particle->x - radius,
                                               particle->y - radius,
                                               (radius * 2) + 1,
                                               (radius * 2) + 1))
            {
                continue;
            }
            egui_canvas_draw_circle_fill_basic(canvas,
                                               particle->x,
                                               particle->y,
                                               particle->size,
                                               ui_color(ui_HomePage_foreground_on_sky_rgb()),
                                               EGUI_ALPHA_100);
            if (particle->size > 1U)
            {
                egui_canvas_draw_hline(canvas, (egui_dim_t)(particle->x - 2), particle->y, 5, ui_color(ui_HomePage_foreground_on_sky_rgb()), EGUI_ALPHA_100);
                egui_canvas_draw_vline(canvas, particle->x, (egui_dim_t)(particle->y - 2), 5, ui_color(ui_HomePage_foreground_on_sky_rgb()), EGUI_ALPHA_100);
            }
        }
        else if (is_star_scene != 0U)
        {
            int radius = (int)particle->size + 1;

            if (!ui_HomePage_canvas_intersects(canvas,
                                               particle->x - radius,
                                               particle->y - radius,
                                               (radius * 2) + 1,
                                               (radius * 2) + 1))
            {
                continue;
            }
            ui_HomePage_draw_star(canvas, particle);
        }
    }
}

static void ui_HomePage_draw_lightning(egui_canvas_t *canvas, uint32_t now)
{
    uint32_t elapsed;
    egui_alpha_t flash_alpha;
    int direction;
    int x0;
    int x1;
    int x2;
    int x3;

    if ((canvas == NULL) || (s_home_weather_state.lightning_active == 0U))
    {
        return;
    }

    elapsed = now - s_home_weather_state.lightning_start_tick;
    if (elapsed < 70U)
    {
        flash_alpha = EGUI_ALPHA_100;
    }
    else
    {
        flash_alpha = EGUI_ALPHA_0;
    }

    if (flash_alpha == EGUI_ALPHA_0)
    {
        return;
    }

    egui_canvas_draw_rectangle_fill(canvas, 0, 0, UI_SCREEN_W, UI_SCREEN_H,
                                    ui_color(0xFFFFFF), flash_alpha);

    direction = ((s_home_weather_state.lightning_shape & 1U) != 0U) ? 1 : -1;
    x0 = s_home_weather_state.lightning_x;
    x1 = x0 + direction * (9 + (s_home_weather_state.lightning_shape & 3U));
    x2 = x1 - direction * 15;
    x3 = x2 + direction * 8;
    egui_canvas_draw_line(canvas, x0, 3, x1, 33, 2, ui_color(0x000000), EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, x1, 33, x2, 62, 2, ui_color(0x000000), EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, x2, 62, x3, 94, 2, ui_color(0x000000), EGUI_ALPHA_100);
}

static void ui_HomePage_draw_raw_text(egui_canvas_t *canvas,
                                      const egui_font_t *font,
                                      const char *text,
                                      egui_dim_t x,
                                      egui_dim_t y,
                                      uint32_t rgb)
{
    if ((font != NULL) && (text != NULL))
    {
        egui_canvas_draw_text(canvas, font, text, x, y, ui_color(rgb), EGUI_ALPHA_100);
    }
}

static egui_dim_t ui_HomePage_draw_text_advance(egui_canvas_t *canvas,
                                                const egui_font_t *font,
                                                const char *text,
                                                egui_dim_t x,
                                                egui_dim_t y,
                                                uint32_t rgb)
{
    egui_dim_t width = 0;
    egui_dim_t height = 0;

    ui_HomePage_draw_raw_text(canvas, font, text, x, y, rgb);
    (void)egui_font_get_str_size_with_canvas(font, canvas, text, 0U, 0, &width, &height);
    return width;
}

static uint8_t ui_HomePage_warm_date_font(void)
{
    static const char date_markers[] = "\346\234\210\346\227\245";

    if (s_home_heiti_16 == NULL)
    {
        s_home_heiti_16 = ui_heiti_font_get_16();
    }
    return ui_heiti_font_warm_text(16U, date_markers) ? 1U : 0U;
}

static void ui_HomePage_draw_date_text(egui_canvas_t *canvas,
                                       const egui_font_t *number_font,
                                       const egui_font_t *heiti_font,
                                       const char *text,
                                       egui_dim_t x,
                                       egui_dim_t y,
                                       uint32_t rgb)
{
    static const char month_marker[] = "\346\234\210";
    static const char day_marker[] = "\346\227\245";
    const char *month_pos;
    const char *day_pos;
    size_t month_len;
    size_t day_len;
    char month_text[3];
    char day_text[3];
    egui_dim_t pen_x = x;

    if ((number_font == NULL) || (heiti_font == NULL) || (text == NULL))
    {
        ui_HomePage_draw_raw_text(canvas, heiti_font, text, x, y, rgb);
        return;
    }

    month_pos = strstr(text, month_marker);
    day_pos = (month_pos != NULL) ? strstr(month_pos + sizeof(month_marker) - 1U, day_marker) : NULL;
    month_len = (month_pos != NULL) ? (size_t)(month_pos - text) : 0U;
    day_len = ((month_pos != NULL) && (day_pos != NULL)) ?
                  (size_t)(day_pos - (month_pos + sizeof(month_marker) - 1U)) :
                  0U;

    if ((month_len == 0U) || (month_len >= sizeof(month_text)) ||
        (day_len == 0U) || (day_len >= sizeof(day_text)) ||
        (day_pos == NULL) || (day_pos[sizeof(day_marker) - 1U] != '\0'))
    {
        ui_HomePage_draw_raw_text(canvas, heiti_font, text, x, y, rgb);
        return;
    }

    for (size_t i = 0U; i < month_len; i++)
    {
        if ((text[i] < '0') || (text[i] > '9'))
        {
            ui_HomePage_draw_raw_text(canvas, heiti_font, text, x, y, rgb);
            return;
        }
    }
    for (size_t i = 0U; i < day_len; i++)
    {
        const char ch = month_pos[sizeof(month_marker) - 1U + i];

        if ((ch < '0') || (ch > '9'))
        {
            ui_HomePage_draw_raw_text(canvas, heiti_font, text, x, y, rgb);
            return;
        }
    }

    memcpy(month_text, text, month_len);
    month_text[month_len] = '\0';
    memcpy(day_text, month_pos + sizeof(month_marker) - 1U, day_len);
    day_text[day_len] = '\0';

    if (s_home_heiti_16_ready == 0U)
    {
        char ascii_date[6];

        (void)snprintf(ascii_date, sizeof(ascii_date), "%s/%s",
                       month_text, day_text);
        ui_HomePage_draw_raw_text(canvas, number_font, ascii_date, x, y, rgb);
        return;
    }

    pen_x += ui_HomePage_draw_text_advance(canvas, number_font, month_text, pen_x, y, rgb);
    pen_x += ui_HomePage_draw_text_advance(canvas, heiti_font, month_marker, pen_x, y, rgb);
    pen_x += 2;
    pen_x += ui_HomePage_draw_text_advance(canvas, number_font, day_text, pen_x, y, rgb);
    (void)ui_HomePage_draw_text_advance(canvas, heiti_font, day_marker, pen_x, y, rgb);
}

static uint8_t ui_HomePage_canvas_intersects(egui_canvas_t *canvas,
                                             egui_dim_t x,
                                             egui_dim_t y,
                                             egui_dim_t w,
                                             egui_dim_t h)
{
    egui_region_t target;
    egui_region_t clipped;
    egui_region_t *work_region;

    if (canvas == NULL)
    {
        return 0U;
    }

    target.location.x = x;
    target.location.y = y;
    target.size.width = w;
    target.size.height = h;
    work_region = egui_canvas_get_base_view_work_region(canvas);
    egui_region_intersect(work_region, &target, &clipped);

    return (uint8_t)((clipped.size.width > 0) && (clipped.size.height > 0));
}

static void ui_HomePage_draw_battery(egui_canvas_t *canvas,
                                     const DataApp_HomeStatus_t *status,
                                     uint32_t top_text_rgb)
{
    const egui_font_t *font = EGUI_FONT_OF(&egui_res_font_montserrat_12_4);
    uint32_t color_rgb;
    uint8_t display_percent;
    egui_dim_t inner_width;
    egui_dim_t fill_width;
    char percent_text[6];

    if ((status == NULL) ||
        !ui_HomePage_canvas_intersects(canvas,
                                       HOME_BATTERY_REGION_X,
                                       HOME_BATTERY_REGION_Y,
                                       HOME_BATTERY_REGION_W,
                                       HOME_BATTERY_REGION_H))
    {
        return;
    }

    if (s_home_battery_state.visible == 0U)
    {
        return;
    }

    color_rgb = (status->charging || status->charge_full) ?
                    HOME_BATTERY_ACTIVE_RGB :
                    top_text_rgb;
    display_percent = s_home_battery_state.display_percent;
    if (display_percent > 100U)
    {
        display_percent = 100U;
    }

    egui_canvas_draw_round_rectangle(canvas,
                                     HOME_BATTERY_BODY_X,
                                     HOME_BATTERY_BODY_Y,
                                     HOME_BATTERY_BODY_W,
                                     HOME_BATTERY_BODY_H,
                                     2,
                                     1,
                                     ui_color(color_rgb),
                                     EGUI_ALPHA_100);
    egui_canvas_draw_round_rectangle_fill(canvas,
                                          HOME_BATTERY_TERMINAL_X,
                                          HOME_BATTERY_TERMINAL_Y,
                                          HOME_BATTERY_TERMINAL_W,
                                          HOME_BATTERY_TERMINAL_H,
                                          1,
                                          ui_color(color_rgb),
                                          EGUI_ALPHA_100);

    inner_width = HOME_BATTERY_BODY_W - 4;
    fill_width = (status->battery_valid != 0U) ?
                     (egui_dim_t)(((uint32_t)inner_width * display_percent) / 100U) : 0;
    if ((status->battery_valid != 0U) && (display_percent != 0U) && (fill_width == 0))
    {
        fill_width = 1;
    }
    if (fill_width > 0)
    {
        egui_canvas_draw_round_rectangle_fill(canvas,
                                              HOME_BATTERY_BODY_X + 2,
                                              HOME_BATTERY_BODY_Y + 2,
                                              fill_width,
                                              HOME_BATTERY_BODY_H - 4,
                                              1,
                                              ui_color(color_rgb),
                                              EGUI_ALPHA_100);
    }

    if (status->battery_valid == 0U)
    {
        (void)snprintf(percent_text, sizeof(percent_text), "--");
    }
    else
    {
        (void)snprintf(percent_text, sizeof(percent_text), "%s%u%%",
                       (status->battery_stale != 0U) ? "*" : "",
                       (unsigned int)status->battery_percent);
    }
    ui_draw_text(canvas,
                 font,
                 percent_text,
                 HOME_BATTERY_TEXT_X,
                 HOME_BATTERY_TEXT_Y,
                 HOME_BATTERY_TEXT_W,
                 HOME_BATTERY_TEXT_H,
                 EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER,
                 color_rgb);
}

static void ui_HomePage_draw_top_status(egui_canvas_t *canvas,
                                        const DataApp_HomeStatus_t *status,
                                        uint32_t top_text_rgb)
{
    const egui_font_t *small_font = EGUI_FONT_OF(&egui_res_font_montserrat_16_4);
    const egui_font_t *carousel_font = EGUI_FONT_OF(&egui_res_font_montserrat_12_4);
    const egui_font_t *heiti_font_16;
#if EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_RGB565
    const egui_image_std_t *weather_icon;
#endif
    const char *carousel_text;

    if ((status == NULL) ||
        !ui_HomePage_canvas_intersects(canvas, HOME_STATUS_TOP_X, HOME_STATUS_TOP_Y, HOME_STATUS_TOP_W, HOME_STATUS_TOP_H))
    {
        return;
    }

    if (s_home_heiti_16 == NULL)
    {
        s_home_heiti_16 = ui_heiti_font_get_16();
    }
    heiti_font_16 = (s_home_heiti_16 != NULL) ? s_home_heiti_16 : small_font;

    ui_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_30_4), status->time_text, 8, 1, 88, 32, EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER, top_text_rgb);
    ui_HomePage_draw_date_text(canvas, small_font, heiti_font_16, status->date_text, 104, 7, top_text_rgb);

    switch (s_home_top_carousel_index)
    {
    case 1U:
        carousel_text = status->env_text;
        break;
    case 2U:
        carousel_text = status->temp_range_text;
        break;
    case 0U:
    default:
        carousel_text = status->pm25_text;
        break;
    }
    ui_draw_text(canvas,
                 carousel_font,
                 carousel_text,
                 HOME_STATUS_CAROUSEL_X,
                 HOME_STATUS_CAROUSEL_Y,
                 HOME_STATUS_CAROUSEL_W,
                 HOME_STATUS_CAROUSEL_H,
                 EGUI_ALIGN_CENTER | EGUI_ALIGN_VCENTER,
                 top_text_rgb);

#if EGUI_CONFIG_FUNCTION_IMAGE_FORMAT_RGB565
    weather_icon = ui_weather_icon_get_mask(status->weather_icon_id);
    if (weather_icon != NULL)
    {
        /* Weather symbols intentionally use one solid foreground color. */
        egui_image_draw_image_color(&weather_icon->base,
                                    canvas,
                                    HOME_STATUS_WEATHER_ICON_X,
                                    HOME_STATUS_WEATHER_ICON_Y,
                                    ui_color(top_text_rgb),
                                    EGUI_ALPHA_100);
    }
#endif
}

static void ui_HomePage_draw_pm25_status(egui_canvas_t *canvas,
                                         const DataApp_HomeStatus_t *status,
                                         uint32_t ground_text_rgb)
{
    const egui_font_t *small_font = EGUI_FONT_OF(&egui_res_font_montserrat_16_4);
    const egui_font_t *heiti_font;

    if ((status == NULL) ||
        !ui_HomePage_canvas_intersects(canvas, HOME_STATUS_PM25_X, HOME_STATUS_PM25_Y, HOME_STATUS_PM25_W, HOME_STATUS_PM25_H))
    {
        return;
    }

    if (s_home_heiti_16 == NULL)
    {
        s_home_heiti_16 = ui_heiti_font_get_16();
    }
    heiti_font = (s_home_heiti_16 != NULL) ? s_home_heiti_16 : small_font;

    ui_draw_text(canvas,
                 heiti_font,
                 status->pm25_text,
                 HOME_STATUS_PM25_X,
                 HOME_STATUS_PM25_Y,
                 HOME_STATUS_PM25_W,
                 HOME_STATUS_PM25_H,
                 EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER,
                 ground_text_rgb);
}

static uint8_t ui_HomePage_value_mix_amount(int32_t value,
                                             int32_t from_value,
                                             int32_t to_value)
{
    if (value <= from_value)
    {
        return 0U;
    }
    if (value >= to_value)
    {
        return 255U;
    }

    return (uint8_t)(((uint32_t)(value - from_value) * 255U) /
                     (uint32_t)(to_value - from_value));
}

static uint32_t ui_HomePage_temperature_color(int16_t temperature_x10)
{
    if (temperature_x10 <= HOME_TEMP_COLD_END_X10)
    {
        return HOME_ENV_COLD_RGB;
    }
    if (temperature_x10 < HOME_TEMP_NORMAL_START_X10)
    {
        return ui_HomePage_mix_rgb(HOME_ENV_COLD_RGB,
                                   HOME_ENV_NORMAL_RGB,
                                   ui_HomePage_value_mix_amount(temperature_x10,
                                                                HOME_TEMP_COLD_END_X10,
                                                                HOME_TEMP_NORMAL_START_X10));
    }
    if (temperature_x10 <= HOME_TEMP_NORMAL_END_X10)
    {
        return HOME_ENV_NORMAL_RGB;
    }
    if (temperature_x10 < HOME_TEMP_WARNING_X10)
    {
        return ui_HomePage_mix_rgb(HOME_ENV_NORMAL_RGB,
                                   HOME_ENV_WARNING_RGB,
                                   ui_HomePage_value_mix_amount(temperature_x10,
                                                                HOME_TEMP_NORMAL_END_X10,
                                                                HOME_TEMP_WARNING_X10));
    }
    if (temperature_x10 < HOME_TEMP_DANGER_X10)
    {
        return ui_HomePage_mix_rgb(HOME_ENV_WARNING_RGB,
                                   HOME_ENV_DANGER_RGB,
                                   ui_HomePage_value_mix_amount(temperature_x10,
                                                                HOME_TEMP_WARNING_X10,
                                                                HOME_TEMP_DANGER_X10));
    }

    return HOME_ENV_DANGER_RGB;
}

static uint32_t ui_HomePage_humidity_color(uint8_t humidity)
{
    if (humidity <= HOME_HUMIDITY_DRY_DANGER)
    {
        return HOME_ENV_DANGER_RGB;
    }
    if (humidity < HOME_HUMIDITY_DRY_WARNING)
    {
        return ui_HomePage_mix_rgb(HOME_ENV_DANGER_RGB,
                                   HOME_ENV_WARNING_RGB,
                                   ui_HomePage_value_mix_amount(humidity,
                                                                HOME_HUMIDITY_DRY_DANGER,
                                                                HOME_HUMIDITY_DRY_WARNING));
    }
    if (humidity < HOME_HUMIDITY_NORMAL_START)
    {
        return ui_HomePage_mix_rgb(HOME_ENV_WARNING_RGB,
                                   HOME_ENV_NORMAL_RGB,
                                   ui_HomePage_value_mix_amount(humidity,
                                                                HOME_HUMIDITY_DRY_WARNING,
                                                                HOME_HUMIDITY_NORMAL_START));
    }
    if (humidity <= HOME_HUMIDITY_NORMAL_END)
    {
        return HOME_ENV_NORMAL_RGB;
    }
    if (humidity < HOME_HUMIDITY_WET_WARNING)
    {
        return ui_HomePage_mix_rgb(HOME_ENV_NORMAL_RGB,
                                   HOME_ENV_WARNING_RGB,
                                   ui_HomePage_value_mix_amount(humidity,
                                                                HOME_HUMIDITY_NORMAL_END,
                                                                HOME_HUMIDITY_WET_WARNING));
    }
    if (humidity < HOME_HUMIDITY_WET_DANGER)
    {
        return ui_HomePage_mix_rgb(HOME_ENV_WARNING_RGB,
                                   HOME_ENV_DANGER_RGB,
                                   ui_HomePage_value_mix_amount(humidity,
                                                                HOME_HUMIDITY_WET_WARNING,
                                                                HOME_HUMIDITY_WET_DANGER));
    }

    return HOME_ENV_DANGER_RGB;
}

static void ui_HomePage_draw_humidity_icon(egui_canvas_t *canvas,
                                            egui_dim_t x,
                                            egui_dim_t y,
                                            uint32_t rgb,
                                            egui_alpha_t alpha)
{
    egui_canvas_draw_line(canvas, x + 5, y, x + 1, y + 7, 3,
                          ui_color(rgb), alpha);
    egui_canvas_draw_line(canvas, x + 5, y, x + 9, y + 7, 3,
                          ui_color(rgb), alpha);
    egui_canvas_draw_circle_fill_basic(canvas, x + 5, y + 9, 4,
                                       ui_color(rgb), alpha);
}

static void ui_HomePage_draw_temperature_icon(egui_canvas_t *canvas,
                                               egui_dim_t x,
                                               egui_dim_t y,
                                               uint32_t rgb,
                                               egui_alpha_t alpha)
{
    egui_canvas_draw_round_rectangle(canvas, x + 3, y, 5, 13, 2, 1,
                                     ui_color(rgb), alpha);
    egui_canvas_draw_line(canvas, x + 5, y + 3, x + 5, y + 12, 2,
                          ui_color(rgb), alpha);
    egui_canvas_draw_circle_fill_basic(canvas, x + 5, y + 13, 4,
                                       ui_color(rgb), alpha);
}

static void ui_HomePage_draw_env_status(egui_canvas_t *canvas,
                                        const DataApp_HomeStatus_t *status,
                                        uint32_t ground_text_rgb)
{
    const egui_font_t *small_font = EGUI_FONT_OF(&egui_res_font_montserrat_16_4);
    const egui_font_t *heiti_font;
    uint32_t humidity_rgb = ground_text_rgb;
    uint32_t temperature_rgb = ground_text_rgb;
    egui_alpha_t icon_alpha = EGUI_ALPHA_100;
    egui_dim_t humidity_text_width = 0;
    egui_dim_t humidity_text_height = 0;
    egui_dim_t humidity_icon_x;
    egui_dim_t temperature_text_width = 0;
    egui_dim_t temperature_text_height = 0;
    egui_dim_t temperature_icon_x;
    int32_t temperature_abs;
    char humidity_text[6];
    char temperature_text[12];

    if ((status == NULL) ||
        !ui_HomePage_canvas_intersects(canvas, HOME_STATUS_ENV_X, HOME_STATUS_ENV_Y, HOME_STATUS_ENV_W, HOME_STATUS_ENV_H))
    {
        return;
    }

    if (s_home_heiti_16 == NULL)
    {
        s_home_heiti_16 = ui_heiti_font_get_16();
    }
    heiti_font = (s_home_heiti_16 != NULL) ? s_home_heiti_16 : small_font;

    if (status->environment_valid != 0U)
    {
        humidity_rgb = ui_HomePage_humidity_color(status->environment_humidity);
        temperature_rgb = ui_HomePage_temperature_color(status->environment_temp_x10);
        icon_alpha = EGUI_ALPHA_100;
        temperature_abs = (status->environment_temp_x10 < 0) ?
                              -(int32_t)status->environment_temp_x10 :
                              (int32_t)status->environment_temp_x10;
        (void)snprintf(humidity_text, sizeof(humidity_text), "%u%%",
                       (unsigned int)status->environment_humidity);
        (void)snprintf(temperature_text,
                       sizeof(temperature_text),
                       "%s%s%ld.%ldC",
                       (status->environment_stale != 0U) ? "*" : "",
                       (status->environment_temp_x10 < 0) ? "-" : "",
                       (long)(temperature_abs / 10),
                       (long)(temperature_abs % 10));
    }
    else
    {
        (void)snprintf(humidity_text, sizeof(humidity_text), "--%%");
        (void)snprintf(temperature_text, sizeof(temperature_text), "--C");
    }

    (void)egui_font_get_str_size_with_canvas(heiti_font,
                                             canvas,
                                             humidity_text,
                                             0U,
                                             0,
                                             &humidity_text_width,
                                             &humidity_text_height);
    humidity_icon_x = (egui_dim_t)(HOME_ENV_HUMIDITY_TEXT_X +
                                   HOME_ENV_HUMIDITY_TEXT_W -
                                   humidity_text_width -
                                   HOME_ENV_ICON_TEXT_GAP -
                                   HOME_ENV_HUMIDITY_ICON_W);
    (void)egui_font_get_str_size_with_canvas(heiti_font,
                                             canvas,
                                             temperature_text,
                                             0U,
                                             0,
                                             &temperature_text_width,
                                             &temperature_text_height);
    temperature_icon_x = (egui_dim_t)(HOME_ENV_TEMPERATURE_TEXT_X +
                                      HOME_ENV_TEMPERATURE_TEXT_W -
                                      temperature_text_width -
                                      HOME_ENV_ICON_TEXT_GAP -
                                      HOME_ENV_TEMPERATURE_ICON_W);

    ui_HomePage_draw_humidity_icon(canvas,
                                   humidity_icon_x,
                                   HOME_ENV_ICON_Y,
                                   humidity_rgb,
                                   icon_alpha);
    ui_draw_text(canvas,
                 heiti_font,
                 humidity_text,
                 HOME_ENV_HUMIDITY_TEXT_X,
                 HOME_STATUS_ENV_TEXT_Y,
                 HOME_ENV_HUMIDITY_TEXT_W,
                 HOME_STATUS_ENV_TEXT_H,
                 EGUI_ALIGN_RIGHT | EGUI_ALIGN_VCENTER,
                 ground_text_rgb);
    ui_HomePage_draw_temperature_icon(canvas,
                                      temperature_icon_x,
                                      HOME_ENV_ICON_Y,
                                      temperature_rgb,
                                      icon_alpha);
    ui_draw_text(canvas,
                 heiti_font,
                 temperature_text,
                 HOME_ENV_TEMPERATURE_TEXT_X,
                 HOME_STATUS_ENV_TEXT_Y,
                 HOME_ENV_TEMPERATURE_TEXT_W,
                 HOME_STATUS_ENV_TEXT_H,
                 EGUI_ALIGN_RIGHT | EGUI_ALIGN_VCENTER,
                 ground_text_rgb);
}

static void ui_HomePage_draw_status(egui_canvas_t *canvas,
                                    const DataApp_HomeStatus_t *status,
                                    uint32_t top_text_rgb)
{
    if (status == NULL)
    {
        return;
    }

    ui_HomePage_draw_top_status(canvas, status, top_text_rgb);
    ui_HomePage_draw_battery(canvas, status, top_text_rgb);
}

static uint32_t ui_HomePage_mono_rgb(uint32_t rgb)
{
    return (ui_HomePage_rgb_luma(rgb) >= 128U) ? 0xFFFFFF : 0x000000;
}

static uint32_t ui_HomePage_foreground_on_sky_rgb(void)
{
    uint32_t background_rgb = s_home_render_style.background_top_rgb;

    return (ui_HomePage_mono_rgb(background_rgb) == 0x000000) ? 0xFFFFFF : 0x000000;
}

static uint32_t ui_HomePage_foreground_on_ground_rgb(void)
{
    return ui_HomePage_mono_rgb(s_home_render_style.background_bottom_rgb) == 0x000000
               ? 0xFFFFFF
               : 0x000000;
}

static void ui_HomePage_draw_sky_gradient(egui_canvas_t *canvas,
                                          uint32_t top_rgb,
                                          uint32_t bottom_rgb)
{
    uint32_t top_mono_rgb = ui_HomePage_mono_rgb(top_rgb);
    uint32_t bottom_mono_rgb = ui_HomePage_mono_rgb(bottom_rgb);

    if (top_mono_rgb == bottom_mono_rgb)
    {
        egui_canvas_draw_rectangle_fill(canvas, 0, 0, UI_SCREEN_W, UI_SCREEN_H,
                                        ui_color(top_mono_rgb), EGUI_ALPHA_100);
        return;
    }

    egui_canvas_draw_rectangle_fill(canvas, 0, 0, UI_SCREEN_W, HOME_GROUND_TILE_Y,
                                    ui_color(top_mono_rgb), EGUI_ALPHA_100);
    egui_canvas_draw_rectangle_fill(canvas, 0, HOME_GROUND_TILE_Y,
                                    UI_SCREEN_W, UI_SCREEN_H - HOME_GROUND_TILE_Y,
                                    ui_color(bottom_mono_rgb), EGUI_ALPHA_100);
}

static void ui_HomePage_on_draw(egui_view_t *self)
{
    egui_canvas_t *canvas = egui_view_get_canvas(self);
    uint32_t top_text_rgb;

    ui_HomePage_draw_sky_gradient(canvas,
                                  s_home_render_style.background_top_rgb,
                                  s_home_render_style.background_bottom_rgb);
    top_text_rgb = ui_HomePage_foreground_on_sky_rgb();
    ui_HomePage_draw_scene(canvas);
    ui_HomePage_draw_weather_particles(canvas);
    ui_HomePage_draw_lightning(canvas, s_home_scene_tick);
    ui_HomePage_draw_status(canvas,
                            &s_home_render_status,
                            top_text_rgb);
}

namespace {
GuiPageDescriptor descriptor = {
    UiPage::Home, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, "home", true, false,
};

void page_init() {
    ui_HomePage_screen_init();
    descriptor.view = ui_HomePage;
}

void page_enter() { ui_HomePage_screen_enter(); }
void page_exit() { ui_HomePage_screen_destroy(); }
bool page_key_consume(const KeyEvent &event) { return ui_HomePage_key_consume(event); }
bool page_service() { return false; }
bool page_update_status(const PlayerStatus &) { return false; }
void page_navigation_changed(bool) {}

struct HomePageDescriptorInitializer {
    HomePageDescriptorInitializer() {
        descriptor.init = page_init;
        descriptor.enter = page_enter;
        descriptor.exit = page_exit;
        descriptor.key_consume = page_key_consume;
        descriptor.service = page_service;
        descriptor.update_status = page_update_status;
        descriptor.navigation_changed = page_navigation_changed;
    }
} descriptor_initializer;
}

GuiPageDescriptor &ui_home_page_descriptor() {
    return descriptor;
}
