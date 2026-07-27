#pragma once

#include "app/poetry_app.h"
#include "app/system_notify.h"
#include "gui/egui_port.h"

void ui_popups_init();
void ui_popups_service(bool home_active);
void ui_poetry_popup_show(const PoetryEntry *entry);
void ui_poetry_popup_dismiss();
bool ui_poetry_popup_is_visible();
bool ui_poetry_popup_prepare_cached(const PoetryEntry *entry);
bool ui_poetry_popup_draw_cached(egui_canvas_t *canvas);
void ui_system_popup_show(const SystemNotifyMessage &message);
void ui_system_popup_dismiss();
void ui_system_popup_dismiss_immediate();
bool ui_system_popup_is_visible();
