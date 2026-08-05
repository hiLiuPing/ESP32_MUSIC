#pragma once

#include "gui/page.h"

/* The active screen is used by the LVGL popup layer. */
lv_obj_t *gui_page_active_root();

void gui_page_manager_init();
bool gui_page_manager_register(GuiPageDescriptor *page);
bool gui_page_manager_load(UiPage page);
UiPage gui_page_current();
void gui_page_goto(UiPage page);
void gui_page_previous();
void gui_page_next();
void gui_page_back();
void gui_page_handle_key(const KeyEvent &event);
void gui_page_update_status(const PlayerStatus &status);
void gui_page_service();
void gui_page_render(bool force = false);
const char *gui_page_name(UiPage page);
