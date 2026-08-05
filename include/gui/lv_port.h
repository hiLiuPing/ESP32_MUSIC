#pragma once

#include <lvgl.h>

bool lv_port_start();
void lv_port_poll();
void lv_port_force_refresh();
lv_display_t *lv_port_display();

