#include "gui/egui_port.h"

#include <Arduino.h>
#include <cstdlib>

#include "bsp/bsp_display.h"
#include "gui/ui.h"

namespace {
egui_core_t s_egui_core;
EGUI_CONFIG_PFB_BUFFER_DECLARE(egui_pfb);
bool started = false;
portMUX_TYPE egui_lock = portMUX_INITIALIZER_UNLOCKED;

void assert_handler(const char *file, int line) {
    Serial.printf("[EGUI] assert %s:%d\n", file, line);
    abort();
}

void delay_ms(uint32_t ms) {
    delay(ms);
}

uint32_t get_tick_ms() {
    return millis();
}

egui_base_t interrupt_disable() {
    portENTER_CRITICAL(&egui_lock);
    return 0;
}

void interrupt_enable(egui_base_t) {
    portEXIT_CRITICAL(&egui_lock);
}

const egui_platform_ops_t platform_ops = {
    .assert_handler = assert_handler,
    .delay = delay_ms,
    .get_tick_ms = get_tick_ms,
    .interrupt_disable = interrupt_disable,
    .interrupt_enable = interrupt_enable,
    .load_external_resource = nullptr,
    .timer_start = nullptr,
    .timer_stop = nullptr,
};

egui_platform_t platform = {
    .ops = &platform_ops,
};

void display_init(egui_core_t *) {
    bsp_display().clearDisplay();
}

void display_draw_area(egui_core_t *, int16_t x, int16_t y, int16_t width,
                       int16_t height, const egui_color_int_t *data) {
    bsp_display().blitRgb565(x, y, width, height,
                            reinterpret_cast<const uint16_t *>(data));
}

void display_flush(egui_core_t *) {
    bsp_display().display();
}

void display_set_power(egui_core_t *, uint8_t on) {
    bsp_display().display_on(on != 0);
}

const egui_display_driver_ops_t display_ops = {
    .init = display_init,
    .draw_area = display_draw_area,
    .wait_draw_complete = nullptr,
    .flush = display_flush,
    .set_brightness = nullptr,
    .set_power = display_set_power,
    .set_rotation = nullptr,
    .fill_rect = nullptr,
    .blit = nullptr,
    .blend = nullptr,
    .wait_vsync = nullptr,
};

egui_display_driver_t display_driver = {
    .ops = &display_ops,
    .physical_width = EGUI_CONFIG_SCREEN_WIDTH,
    .physical_height = EGUI_CONFIG_SCREEN_HEIGHT,
    .rotation = EGUI_DISPLAY_ROTATION_0,
    .brightness = 255,
    .power_on = 1,
    .frame_sync_enabled = 0,
    .frame_sync_ready = 1,
    .user_data = nullptr,
};

void ui_bootstrap(egui_core_t *) {
    gui_init();
}
}

bool egui_port_start() {
    if (started) {
        return true;
    }

    egui_platform_register(&platform);
    egui_color_int_t *buffers[EGUI_CONFIG_PFB_BUFFER_COUNT];
    for (int index = 0; index < EGUI_CONFIG_PFB_BUFFER_COUNT; ++index) {
        buffers[index] = egui_pfb[index];
    }

    const egui_display_setup_t setup = {
        .screen_width = EGUI_CONFIG_SCREEN_WIDTH,
        .screen_height = EGUI_CONFIG_SCREEN_HEIGHT,
        .pfb_width = EGUI_CONFIG_PFB_WIDTH,
        .pfb_height = EGUI_CONFIG_PFB_HEIGHT,
        .pfb_buffers = buffers,
        .pfb_buffer_count = EGUI_CONFIG_PFB_BUFFER_COUNT,
        .display_driver = &display_driver,
        .render_config = nullptr,
        .touch_register = nullptr,
        .uicode_init = ui_bootstrap,
        .display_id = 0,
    };
    egui_setup_display(&s_egui_core, &setup);
    started = true;
    return true;
}

void egui_port_poll() {
    if (started) {
        egui_polling_work(&s_egui_core);
    }
}

egui_core_t *egui_port_core() {
    return &s_egui_core;
}
