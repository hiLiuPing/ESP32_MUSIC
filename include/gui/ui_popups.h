#pragma once

#include "app/system_notify.h"
#include "gui/page.h"

void ui_popups_init();
void ui_popups_service(UiPage current_page);
void ui_poetry_popup_dismiss();
bool ui_poetry_popup_is_visible();
void ui_system_popup_show(const SystemNotifyMessage &message);
void ui_system_popup_dismiss();
void ui_system_popup_dismiss_immediate();
bool ui_system_popup_is_visible();
