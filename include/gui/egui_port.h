#pragma once

extern "C" {
#include "egui.h"
}

bool egui_port_start();
void egui_port_poll();
egui_core_t *egui_port_core();
