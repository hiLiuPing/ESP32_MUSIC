#include "gui/screens/ui_home_page.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

#include "app/app_data.h"
#include "app/player_types.h"
#include "bsp/bsp_display.h"
#include "gui/ui_heiti_font.h"
#include "lvgl_ui_common.h"
#include "task/task_system.h"

namespace {
lv_obj_t *root = nullptr;
lv_obj_t *clock_label = nullptr;
lv_obj_t *date_label = nullptr;
lv_obj_t *weather_label = nullptr;
lv_obj_t *environment_label = nullptr;
lv_obj_t *battery_label = nullptr;
GuiPageDescriptor page = {};

uint32_t frame_tick = 0;
uint32_t random_state = 0x6D2B79F5U;
uint8_t cloud_phase = 0;
uint8_t bird_phase = 0;
uint8_t bike_frame = 0;
uint8_t fire_frame = 0;
uint8_t carousel = 0;
ST7305QuantizeMode previous_quantize = ST7305QuantizeMode::Bayer4x4;
bool quantize_saved = false;
AppDataSnapshot snapshot = {};

uint32_t rnd() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state ? random_state : 0x6D2B79F5U;
}

lv_color_t sky_color(bool day, WeatherScene_t scene) {
    if (!day) return lv_color_hex(scene == WEATHER_SCENE_HEAVY_RAIN ? 0x202633 : 0x111827);
    switch (scene) {
        case WEATHER_SCENE_HEAVY_RAIN: return lv_color_hex(0x66717D);
        case WEATHER_SCENE_MODERATE_RAIN: return lv_color_hex(0x8D9BA4);
        case WEATHER_SCENE_LIGHT_RAIN: return lv_color_hex(0xAEBCC0);
        case WEATHER_SCENE_CLOUDY: return lv_color_hex(0xB9C8CC);
        case WEATHER_SCENE_SNOW: return lv_color_hex(0xCBD9DE);
        default: return lv_color_hex(0xBFE5F4);
    }
}

void rect(lv_layer_t *layer, int x, int y, int w, int h, lv_color_t color,
          lv_opa_t opa = LV_OPA_COVER, int radius = 0) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = radius;
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &dsc, &a);
}

void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t color,
          int width = 1, lv_opa_t opa = LV_OPA_COVER) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = {x1, y1}; dsc.p2 = {x2, y2}; dsc.color = color;
    dsc.width = width; dsc.opa = opa; dsc.round_start = 1; dsc.round_end = 1;
    lv_draw_line(layer, &dsc);
}

void draw_cloud(lv_layer_t *layer, int x, int y, bool dark) {
    lv_color_t c = dark ? lv_color_hex(0x647080) : lv_color_hex(0xF5FAFC);
    rect(layer, x, y + 6, 52, 14, c, LV_OPA_90, 7);
    rect(layer, x + 9, y, 18, 16, c, LV_OPA_90, 9);
    rect(layer, x + 27, y + 3, 24, 17, c, LV_OPA_90, 9);
}

void draw_bird(lv_layer_t *layer, int x, int y, uint8_t phase, lv_color_t c) {
    int lift = phase ? 2 : -1;
    line(layer, x, y, x + 5, y + lift, c, 2);
    line(layer, x + 5, y + lift, x + 10, y, c, 2);
}

void draw_bike(lv_layer_t *layer, int x, int y, uint8_t frame, lv_color_t c) {
    const int bob = frame == 1 || frame == 2 ? 1 : 0;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc); arc.color = c; arc.width = 2; arc.radius = 11; arc.opa = LV_OPA_COVER;
    arc.center = {x + 11, y + bob}; arc.start_angle = 0; arc.end_angle = 360; lv_draw_arc(layer, &arc);
    arc.center = {x + 40, y + bob}; lv_draw_arc(layer, &arc);
    line(layer, x + 11, y + bob, x + 22, y - 13 + bob, c, 2);
    line(layer, x + 22, y - 13 + bob, x + 40, y + bob, c, 2);
    line(layer, x + 22, y - 13 + bob, x + 30, y + 4 + bob, c, 2);
    line(layer, x + 22, y - 13 + bob, x + 17, y - 16 + bob, c, 2);
    line(layer, x + 17, y - 16 + bob, x + 25, y - 16 + bob, c, 2);
    line(layer, x + 30, y + 4 + bob, x + 34, y - 4 + bob, c, 2);
}

void draw_fire(lv_layer_t *layer, int x, int y, uint8_t frame, lv_color_t c) {
    const int spread = frame & 1U;
    rect(layer, x, y + 14, 30, 4, c, LV_OPA_COVER, 2);
    line(layer, x + 6, y + 15, x + 14, y + 4 - spread, c, 3);
    line(layer, x + 24, y + 15, x + 15, y + 3 + spread, c, 3);
    rect(layer, x + 11, y + 1 + spread, 8, 14, c, LV_OPA_COVER, 4);
}

void draw_home(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t coords; lv_obj_get_coords(root, &coords);
    const int ox = coords.x1; const int oy = coords.y1;
    const bool day = snapshot.is_day != 0;
    const WeatherScene_t scene = static_cast<WeatherScene_t>(snapshot.weather_scene);
    const lv_color_t bg = sky_color(day, scene);
    rect(layer, ox, oy, 384, 168, bg);
    const lv_color_t fg = day ? lv_color_black() : lv_color_white();
    const lv_color_t ground = day ? lv_color_hex(0x6C8B55) : lv_color_hex(0x2E3B34);
    rect(layer, ox, oy + 122, 384, 46, ground);

    draw_cloud(layer, ox + 32 - (cloud_phase / 2), oy + 30, !day);
    draw_cloud(layer, ox + 186 - (cloud_phase / 3), oy + 18, !day);
    draw_cloud(layer, ox + 295 - (cloud_phase / 4), oy + 43, !day);
    if (day) for (uint8_t i = 0; i < 5; ++i) draw_bird(layer, ox + 42 + i * 64 - (bird_phase / 3), oy + 55 + (i % 2) * 10, bird_phase & 1U, fg);

    if (!day && (scene == WEATHER_SCENE_CLEAR || scene == WEATHER_SCENE_CLOUDY)) {
        for (uint8_t i = 0; i < 14; ++i) {
            const int sx = ox + 12 + static_cast<int>((i * 71U + 17U) % 355U);
            const int sy = oy + 26 + static_cast<int>((i * 37U + 9U) % 82U);
            rect(layer, sx, sy, 2 + ((i + fire_frame) & 1U), 2 + ((i + fire_frame) & 1U), lv_color_white(), LV_OPA_70, LV_RADIUS_CIRCLE);
        }
    }
    const bool rain = scene == WEATHER_SCENE_LIGHT_RAIN || scene == WEATHER_SCENE_MODERATE_RAIN || scene == WEATHER_SCENE_HEAVY_RAIN;
    for (uint8_t i = 0; i < 22; ++i) {
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
    if (day) draw_bike(layer, ox + 92 + static_cast<int>((frame_tick / 50U) % 180U), oy + 132, bike_frame, fg);
    else if (!rain) draw_fire(layer, ox + 178, oy + 132, fire_frame, fg);

    rect(layer, ox + 8, oy + 7, 80, 22, bg, LV_OPA_80, 3);
    rect(layer, ox + 286, oy + 7, 90, 22, bg, LV_OPA_80, 3);
}

void refresh_labels() {
    if (!app_data_get_snapshot(&snapshot)) return;
    const lv_color_t text_color = snapshot.is_day ? lv_color_black() : lv_color_white();
    lv_obj_set_style_text_color(clock_label, text_color, 0);
    lv_obj_set_style_text_color(date_label, text_color, 0);
    lv_obj_set_style_text_color(weather_label, text_color, 0);
    lv_obj_set_style_text_color(environment_label, text_color, 0);
    lv_obj_set_style_text_color(battery_label, text_color, 0);
    lv_label_set_text(clock_label, snapshot.time_text[0] ? snapshot.time_text : "--:--");
    lv_label_set_text(date_label, snapshot.date_text);
    const char *scene_names[] = {"天气", "晴天", "多云", "小雨", "中雨", "暴雨", "下雪"};
    const uint8_t scene = snapshot.weather_scene <= WEATHER_SCENE_SNOW ? snapshot.weather_scene : 0;
    char weather[96] = {};
    std::snprintf(weather, sizeof(weather), "%s  %s", snapshot.weather.text, snapshot.temp_range_text);
    lv_label_set_text(weather_label, weather);
    char env[64] = {};
    std::snprintf(env, sizeof(env), "%s  %s", scene_names[scene], snapshot.env_text);
    lv_label_set_text(environment_label, env);
    char bat[24] = {};
    std::snprintf(bat, sizeof(bat), "BAT %u%%%s", snapshot.battery_percent, snapshot.charging ? " +" : "");
    lv_label_set_text(battery_label, bat);
}

void create_page() {
    root = lvgl_page_create();
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(root, draw_home, LV_EVENT_DRAW_MAIN, nullptr);
    clock_label = lvgl_label(root, "--:--", 12, 8, 90, ui_heiti_font_get(20));
    date_label = lvgl_label(root, "", 105, 10, 150, ui_heiti_font_get(16));
    battery_label = lvgl_label(root, "", 286, 10, 88, ui_heiti_font_get(16));
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    weather_label = lvgl_label(root, "", 12, 95, 360, ui_heiti_font_get(16));
    lv_obj_set_style_text_align(weather_label, LV_TEXT_ALIGN_CENTER, 0);
    environment_label = lvgl_label(root, "", 12, 110, 360, ui_heiti_font_get(16));
    lv_obj_set_style_text_align(environment_label, LV_TEXT_ALIGN_CENTER, 0);
    page.view = root;
}

void enter() {
    if (!quantize_saved) { previous_quantize = bsp_display().getQuantizeMode(); quantize_saved = true; }
    bsp_display().setQuantizeMode(ST7305QuantizeMode::MonoAsset);
    frame_tick = millis(); cloud_phase = bird_phase = bike_frame = fire_frame = carousel = 0;
    refresh_labels(); lv_obj_invalidate(root);
}

void exit() {
    if (quantize_saved) { bsp_display().setQuantizeMode(previous_quantize); quantize_saved = false; }
}

bool service() {
    const uint32_t now = millis();
    if (now - frame_tick >= 50U) {
        const uint32_t steps = (now - frame_tick) / 50U;
        frame_tick += steps * 50U;
        cloud_phase = static_cast<uint8_t>(cloud_phase + steps * 2U);
        bird_phase = static_cast<uint8_t>(bird_phase + steps);
        bike_frame = static_cast<uint8_t>((now / 100U) & 3U);
        fire_frame = static_cast<uint8_t>((now / 100U) & 3U);
        if ((now / 1000U) % 5U == 0U) carousel = static_cast<uint8_t>((now / 1000U) % 3U);
        refresh_labels(); lv_obj_invalidate(root);
    }
    return false;
}

bool consume(const KeyEvent &event) {
    if (event.gesture != KeyGesture::Click) return false;
    if (event.id == KeyId::Left) return task_post_player_command(PlayerCommandType::Previous, 0, true);
    if (event.id == KeyId::Right) return task_post_player_command(PlayerCommandType::Next, 0, true);
    if (event.id == KeyId::Middle) return task_post_player_command(PlayerCommandType::Toggle, 0, true);
    return false;
}
bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_home_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Home, create_page, enter, exit, consume, service, status_update, root, "home", true, false, nullptr};
    return page;
}
