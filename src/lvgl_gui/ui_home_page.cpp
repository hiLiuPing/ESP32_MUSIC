#include "gui/screens/ui_home_page.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>

#include "app/app_data.h"
#include "app/player_types.h"
#include "bsp/bsp_display.h"
#include "gui/lvgl_resources.h"
#include "gui/resources/lvgl_home_resources.h"
#include "gui/ui_heiti_font.h"
#include "lvgl_ui_common.h"
#include "task/task_system.h"

namespace {
lv_obj_t *root = nullptr;
lv_obj_t *clock_label = nullptr;
lv_obj_t *date_label = nullptr;
lv_obj_t *carousel_label = nullptr;
lv_obj_t *battery_label = nullptr;
GuiPageDescriptor page = {};

uint32_t frame_tick = 0;
uint32_t carousel_tick = 0;
uint32_t bike_cycle_tick = 0;
uint32_t bird_phase = 0;
uint8_t bike_frame = 0;
uint8_t fire_frame = 0;
uint8_t carousel = 0;
ST7305QuantizeMode previous_quantize = ST7305QuantizeMode::Bayer4x4;
bool quantize_saved = false;
AppDataSnapshot snapshot = {};

struct CloudState {
    const lv_image_dsc_t *asset;
    int32_t x_fp;
    int16_t y;
    uint32_t speed_fp;
};

constexpr int32_t CLOUD_FP_ONE = 65536;
constexpr int32_t CLOUD_MIN_GAP = 12 * CLOUD_FP_ONE;
CloudState clouds[3] = {};

lv_color_t sky_color(bool day, WeatherScene_t scene) {
    (void)day;
    switch (scene) {
        case WEATHER_SCENE_HEAVY_RAIN: return lv_color_hex(0x66717D);
        case WEATHER_SCENE_MODERATE_RAIN: return lv_color_hex(0x8D9BA4);
        case WEATHER_SCENE_LIGHT_RAIN: return lv_color_hex(0xAEBCC0);
        case WEATHER_SCENE_CLOUDY: return lv_color_hex(0xB9C8CC);
        case WEATHER_SCENE_SNOW: return lv_color_hex(0xCBD9DE);
        default: return lv_color_hex(0xBFE5F4);
    }
}

void randomize_cloud(uint8_t index, bool initial) {
    if (index >= 3U || lvgl_home_cloud_count == 0U) return;
    CloudState &cloud = clouds[index];
    cloud.asset = lvgl_home_clouds[static_cast<uint32_t>(random(0, lvgl_home_cloud_count))];
    cloud.y = static_cast<int16_t>(28 + random(0, 62));
    cloud.speed_fp = static_cast<uint32_t>(18 + random(0, 17)) * 65536U;
    if (initial) cloud.x_fp = 0;
}

int32_t cloud_width_fp(const CloudState &cloud) {
    return cloud.asset == nullptr ? 0 : static_cast<int32_t>(cloud.asset->header.w) * CLOUD_FP_ONE;
}

void sort_clouds() {
    if (clouds[0].x_fp > clouds[1].x_fp) std::swap(clouds[0], clouds[1]);
    if (clouds[1].x_fp > clouds[2].x_fp) std::swap(clouds[1], clouds[2]);
    if (clouds[0].x_fp > clouds[1].x_fp) std::swap(clouds[0], clouds[1]);
}

void initialize_clouds() {
    int32_t x = static_cast<int32_t>(-80 + random(0, 240)) * CLOUD_FP_ONE;
    for (uint8_t i = 0U; i < 3U; ++i) {
        randomize_cloud(i, true);
        clouds[i].x_fp = x;
        x += cloud_width_fp(clouds[i]) + CLOUD_MIN_GAP +
             static_cast<int32_t>(random(30, 90)) * CLOUD_FP_ONE;
    }
}

void respawn_cloud(uint8_t index) {
    randomize_cloud(index, false);
    int32_t right_edge = 384 * CLOUD_FP_ONE;
    for (uint8_t i = 0U; i < 3U; ++i) {
        if (i == index) continue;
        right_edge = std::max(right_edge, clouds[i].x_fp + cloud_width_fp(clouds[i]) + CLOUD_MIN_GAP);
    }
    clouds[index].x_fp = right_edge + static_cast<int32_t>(random(0, 90)) * CLOUD_FP_ONE;
}

void update_clouds(uint32_t elapsed_ms) {
    for (uint8_t i = 0U; i < 3U; ++i) {
        CloudState &cloud = clouds[i];
        if (cloud.asset == nullptr) randomize_cloud(i, false);
        cloud.x_fp -= static_cast<int32_t>((cloud.speed_fp * elapsed_ms) / 1000U);
        if (cloud.asset != nullptr && cloud.x_fp + cloud_width_fp(cloud) < 0) {
            respawn_cloud(i);
        }
    }
    sort_clouds();
    for (uint8_t i = 1U; i < 3U; ++i) {
        const int32_t minimum_x = clouds[i - 1U].x_fp + cloud_width_fp(clouds[i - 1U]) + CLOUD_MIN_GAP;
        if (clouds[i].x_fp < minimum_x) clouds[i].x_fp = minimum_x;
    }
}

void rect(lv_layer_t *layer, int x, int y, int w, int h, lv_color_t color,
          lv_opa_t opa = LV_OPA_COVER, int radius = 0) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = radius;
    lv_area_t area = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &dsc, &area);
}

void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t color,
          int width = 1, lv_opa_t opa = LV_OPA_COVER, bool rounded = true) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = {x1, y1};
    dsc.p2 = {x2, y2};
    dsc.color = color;
    dsc.width = width;
    dsc.opa = opa;
    dsc.round_start = rounded;
    dsc.round_end = rounded;
    lv_draw_line(layer, &dsc);
}

void diamond(lv_layer_t *layer, int x, int y, int radius, lv_color_t color, lv_opa_t opa) {
    line(layer, x, y - radius, x + radius, y, color, 1, opa, false);
    line(layer, x + radius, y, x, y + radius, color, 1, opa, false);
    line(layer, x, y + radius, x - radius, y, color, 1, opa, false);
    line(layer, x - radius, y, x, y - radius, color, 1, opa, false);
}

void image(lv_layer_t *layer, const lv_image_dsc_t *source, int x, int y,
           lv_color_t recolor = lv_color_white(), bool tint = false,
           lv_opa_t opa = LV_OPA_COVER) {
    if (layer == nullptr || source == nullptr) return;
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = source;
    dsc.opa = opa;
    dsc.recolor = recolor;
    dsc.recolor_opa = (tint || source->header.cf == LV_COLOR_FORMAT_A8) ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_area_t area = {x, y, x + static_cast<int>(source->header.w) - 1,
                      y + static_cast<int>(source->header.h) - 1};
    lv_draw_image(layer, &dsc, &area);
}

void draw_battery(lv_layer_t *layer, int x, int y, uint8_t percent, lv_color_t color, lv_color_t background) {
    rect(layer, x, y, 20, 12, color, LV_OPA_COVER, 2);
    rect(layer, x + 2, y + 2, 16, 8, background, LV_OPA_COVER, 1);
    const int fill = (static_cast<int>(percent) * 16 + 50) / 100;
    if (fill > 0) rect(layer, x + 2, y + 2, fill, 8, color, LV_OPA_COVER, 1);
    rect(layer, x + 20, y + 3, 3, 6, color, LV_OPA_COVER, 1);
}

void draw_home(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(root, &coords);
    const int ox = coords.x1;
    const int oy = coords.y1;
    const bool day = snapshot.is_day != 0;
    const WeatherScene_t scene = static_cast<WeatherScene_t>(snapshot.weather_scene);
    const lv_color_t bg = sky_color(day, scene);
    const lv_color_t fg = lv_color_black();
    rect(layer, ox, oy, 384, 168, bg);

    for (int x = 0; x < 384; x += 42) image(layer, lvgl_home_grass_tiles[0], ox + x, oy + 136);
    image(layer, lvgl_home_grass_bases[0], ox, oy + 143);
    image(layer, lvgl_home_grass_bases[0], ox + 195, oy + 143);

    // MonoAsset renders the cloud masks as a solid foreground color.
    const lv_color_t cloud_color = lv_color_black();
    for (uint8_t i = 0; i < 3U; ++i) {
        const CloudState &cloud = clouds[i];
        image(layer, cloud.asset, ox + (cloud.x_fp >> 16), oy + cloud.y, cloud_color, true);
    }
    if (day) {
        for (uint8_t i = 0; i < 5U; ++i) {
            const lv_image_dsc_t *asset = lvgl_home_birds[(i * 3U) % lvgl_home_bird_count];
            const int span = 384 + static_cast<int>(asset->header.w);
            const int base = 42 + static_cast<int>(i) * 64;
            const int x = ox + ((base - bird_phase / 10U) % span + span) % span - static_cast<int>(asset->header.w);
            image(layer, asset, x, oy + 32 + (i % 2U) * 10, fg, true);
        }
    }

    if (!day && (scene == WEATHER_SCENE_CLEAR || scene == WEATHER_SCENE_CLOUDY)) {
        for (uint8_t i = 0; i < 14U; ++i) {
            const int sx = ox + 12 + static_cast<int>((i * 71U + 17U) % 355U);
            const int sy = oy + 26 + static_cast<int>((i * 37U + 9U) % 82U);
            const uint8_t phase = static_cast<uint8_t>((frame_tick / 120U + i * 3U) % 4U);
            const int radius = 3 + (phase <= 2U ? phase : 4U - phase);
            const lv_opa_t opacity = phase == 2U ? LV_OPA_COVER : phase == 1U ? LV_OPA_80 : LV_OPA_60;
            diamond(layer, sx, sy, radius, fg, opacity);
        }
    }
    const bool rain = scene == WEATHER_SCENE_LIGHT_RAIN || scene == WEATHER_SCENE_MODERATE_RAIN || scene == WEATHER_SCENE_HEAVY_RAIN;
    for (uint8_t i = 0; i < 22U; ++i) {
        const int px = ox + static_cast<int>((i * 47U + frame_tick / (rain ? 3U : 7U)) % 384U);
        const int py = oy + 24 + static_cast<int>((i * 31U + frame_tick / (rain ? 2U : 5U)) % 102U);
        if (rain) line(layer, px, py, px - 2, py + (scene == WEATHER_SCENE_HEAVY_RAIN ? 12 : 7), fg, 1, LV_OPA_70);
        else if (scene == WEATHER_SCENE_SNOW) rect(layer, px, py, 3, 3, fg, LV_OPA_80, LV_RADIUS_CIRCLE);
    }
    if (rain && (frame_tick % 11000U) < 260U) {
        const int lx = ox + 80 + static_cast<int>((frame_tick / 1000U) % 200U);
        line(layer, lx, oy + 38, lx - 8, oy + 62, fg, 2);
        line(layer, lx - 8, oy + 62, lx + 1, oy + 58, fg, 2);
        line(layer, lx + 1, oy + 58, lx - 5, oy + 86, fg, 2);
    }
    if (day) {
        constexpr uint32_t kBikeCrossingMs = 3600000U;
        constexpr int kBikeStartX = -62;
        constexpr int kBikeEndX = 384;
        const uint32_t elapsed = frame_tick - bike_cycle_tick;
        const int bike_x = kBikeStartX + static_cast<int>(
            ((elapsed % kBikeCrossingMs) * static_cast<uint32_t>(kBikeEndX - kBikeStartX)) /
            kBikeCrossingMs);
        image(layer, lvgl_home_bikes[bike_frame % lvgl_home_bike_count], ox + bike_x, oy + 81);
    } else {
        image(layer, lvgl_home_house[0], ox + 72, oy + 101);
        if (!rain) image(layer, lvgl_home_fires[fire_frame % lvgl_home_fire_count], ox + 150, oy + 126);
    }

    const lv_color_t top_fg = lv_color_black();
    draw_battery(layer, ox + 169, oy + 10, snapshot.battery_percent, top_fg, bg);
    const lv_image_dsc_t *weather_icon = lvgl_weather_icon_get(snapshot.weather_icon_id);
    if (weather_icon != nullptr) image(layer, weather_icon, ox + 325, oy - 10, top_fg, true);
}

void refresh_labels() {
    if (!app_data_get_snapshot(&snapshot)) return;
    const lv_color_t text_color = lv_color_black();
    lv_obj_set_style_text_color(clock_label, text_color, 0);
    lv_obj_set_style_text_color(date_label, text_color, 0);
    lv_obj_set_style_text_color(carousel_label, text_color, 0);
    lv_obj_set_style_text_color(battery_label, text_color, 0);
    lv_label_set_text(clock_label, snapshot.time_text[0] ? snapshot.time_text : "--:--");
    lv_label_set_text(date_label, snapshot.date_text);
    const char *carousel_text = snapshot.pm25_text;
    if (carousel == 1U) carousel_text = snapshot.env_text;
    else if (carousel == 2U) carousel_text = snapshot.temp_range_text;
    lv_label_set_text(carousel_label, carousel_text);
    char bat[8] = {};
    std::snprintf(bat, sizeof(bat), "%u%%", snapshot.battery_percent);
    lv_label_set_text(battery_label, bat);
}

void create_page() {
    root = lvgl_page_create();
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(root, draw_home, LV_EVENT_DRAW_MAIN, nullptr);
    clock_label = lvgl_label(root, "--:--", 8, 1, 90, &lv_font_montserrat_30);
    date_label = lvgl_label(root, "", 104, 7, 60, ui_heiti_font_get(16));
    battery_label = lvgl_label(root, "", 194, 5, 38, ui_heiti_font_get(16));
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_CENTER, 0);
    carousel_label = lvgl_label(root, "", 232, 7, 90, ui_heiti_font_get(16));
    lv_obj_set_style_text_align(carousel_label, LV_TEXT_ALIGN_CENTER, 0);
    page.view = root;
}

void enter() {
    if (!quantize_saved) { previous_quantize = bsp_display().getQuantizeMode(); quantize_saved = true; }
    bsp_display().setQuantizeMode(ST7305QuantizeMode::MonoAsset);
    frame_tick = millis();
    carousel_tick = frame_tick;
    bike_cycle_tick = frame_tick;
    bird_phase = bike_frame = fire_frame = carousel = 0;
    initialize_clouds();
    refresh_labels();
    lv_obj_invalidate(root);
}

void exit() {
    if (quantize_saved) { bsp_display().setQuantizeMode(previous_quantize); quantize_saved = false; }
}

bool service() {
    const uint32_t now = millis();
    if (now - frame_tick >= 30U) {
        const uint32_t elapsed = now - frame_tick;
        frame_tick = now;
        update_clouds(elapsed);
        bird_phase += elapsed;
        bike_frame = static_cast<uint8_t>((now / 100U) & 3U);
        fire_frame = static_cast<uint8_t>((now / 100U) & 3U);
        if (now - carousel_tick >= 5000U) {
            carousel_tick = now;
            carousel = static_cast<uint8_t>((carousel + 1U) % 3U);
        }
        refresh_labels();
        lv_obj_invalidate(root);
    }
    return false;
}

bool consume(const KeyEvent &event) {
    if (event.gesture == KeyGesture::DoubleClick) {
        if (event.id == KeyId::Middle) return task_post_player_command(PlayerCommandType::Toggle, 0, true);
        if (event.id == KeyId::Left) return task_post_player_command(PlayerCommandType::Previous, 0, true);
        if (event.id == KeyId::Right) return task_post_player_command(PlayerCommandType::Next, 0, true);
    }
    if (event.gesture == KeyGesture::LongPress) {
        if (event.id == KeyId::Left) return task_post_player_command(PlayerCommandType::ChangeVolume, -1, true);
        if (event.id == KeyId::Right) return task_post_player_command(PlayerCommandType::ChangeVolume, 1, true);
    }
    return false;
}
bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_home_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Home, create_page, enter, exit, consume, service, status_update, root, "home", true, false, nullptr};
    return page;
}
