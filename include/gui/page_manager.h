#pragma once

#include "app/player_types.h"

void gui_page_manager_init();
UiPage gui_page_current();
void gui_page_goto(UiPage page);
void gui_page_previous();
void gui_page_next();
void gui_page_handle_input(const UiInputEvent &event);
void gui_page_update_status(const PlayerStatus &status);
void gui_page_render(bool force = false);
const char *gui_page_name(UiPage page);
