#include "gui/ui_popups.h"

#include "app/poetry_app.h"
#include "app/settings_app.h"
#include "gui/page_manager.h"
#include "gui/ui_heiti_font.h"
#include "gui/ui_poetry_cache.h"
#include "lvgl_ui_common.h"

namespace {
lv_obj_t *poetry = nullptr; lv_obj_t *poetry_title = nullptr; lv_obj_t *poetry_body = nullptr;
lv_obj_t *notice = nullptr; lv_obj_t *notice_title = nullptr; lv_obj_t *notice_body = nullptr;
bool poetry_visible = false; SystemNotifyMessage current = {}; uint32_t poetry_started = 0; uint32_t notice_started = 0; uint32_t next_poetry = 0;
void panel_style(lv_obj_t *panel) { lv_obj_set_style_bg_color(panel, lv_color_white(), 0); lv_obj_set_style_border_color(panel, lv_color_black(), 0); lv_obj_set_style_border_width(panel, 2, 0); lv_obj_set_style_radius(panel, 8, 0); lv_obj_set_style_pad_all(panel, 8, 0); }
bool show_poetry() { const UiPoetryCacheEntry *entry = ui_poetry_cache_take_for_popup(PoetryCollection::Song3000); if (entry == nullptr) return false; lv_label_set_text(poetry_title, entry->title); lv_label_set_text(poetry_body, entry->body); lv_obj_clear_flag(poetry, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(poetry); poetry_visible = true; poetry_started = millis(); Serial.printf("[POETRY_POPUP] show reads=%lu glyphs=%u\n", static_cast<unsigned long>(ui_heiti_font_storage_read_count()), static_cast<unsigned>(ui_heiti_font_poetry_cache_glyphs())); return true; }
}
void ui_popups_init() { if (poetry != nullptr) return; lv_obj_t *layer = lv_layer_top(); poetry = lv_obj_create(layer); lv_obj_set_pos(poetry, 32, 4); lv_obj_set_size(poetry, 320, 160); panel_style(poetry); poetry_title = lvgl_label(poetry, "", 8, 4, 288, ui_heiti_font_get(18)); lv_obj_set_style_text_align(poetry_title, LV_TEXT_ALIGN_CENTER, 0); poetry_body = lvgl_label(poetry, "", 8, 32, 288, ui_heiti_font_get(18)); lv_label_set_long_mode(poetry_body, LV_LABEL_LONG_MODE_WRAP); lv_obj_add_flag(poetry, LV_OBJ_FLAG_HIDDEN); notice = lv_obj_create(layer); lv_obj_set_pos(notice, 42, 42); lv_obj_set_size(notice, 300, 72); panel_style(notice); notice_title = lvgl_label(notice, "", 10, 5, 270, ui_heiti_font_get(16)); notice_body = lvgl_label(notice, "", 10, 32, 270, ui_heiti_font_get(16)); lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN); next_poetry = millis() + 1200000UL; }
void ui_popups_service(UiPage page) { ui_popups_init(); (void)ui_poetry_cache_service(); lv_obj_set_style_text_font(poetry_title, ui_heiti_font_get(18), 0); lv_obj_set_style_text_font(poetry_body, ui_heiti_font_get(18), 0); lv_obj_set_style_text_font(notice_title, ui_heiti_font_get(16), 0); lv_obj_set_style_text_font(notice_body, ui_heiti_font_get(16), 0); const uint32_t now = millis(); const AppSettings settings = settings_app_get(); if (page == UiPage::Boot) { ui_poetry_popup_dismiss(); ui_system_popup_dismiss_immediate(); return; } if (notice_started != 0 && now - notice_started > 4500U) ui_system_popup_dismiss_immediate(); if (poetry_visible && now - poetry_started > static_cast<uint32_t>(settings.poetry_duration_s) * 1000UL) ui_poetry_popup_dismiss(); if (page != UiPage::Home) ui_poetry_popup_dismiss(); if (page == UiPage::Home && settings.poetry_interval_min != 0 && !poetry_visible && notice_started == 0 && static_cast<int32_t>(now - next_poetry) >= 0) { next_poetry = now + (show_poetry() ? static_cast<uint32_t>(settings.poetry_interval_min) * 60000UL : 1000UL); } SystemNotifyMessage message = {}; if (notice_started == 0 && system_notify_try_receive(&message)) ui_system_popup_show(message); }
void ui_poetry_popup_dismiss() { if (poetry != nullptr) lv_obj_add_flag(poetry, LV_OBJ_FLAG_HIDDEN); poetry_visible = false; }
bool ui_poetry_popup_is_visible() { return poetry_visible; }
void ui_system_popup_show(const SystemNotifyMessage &message) { ui_popups_init(); ui_poetry_popup_dismiss(); current = message; const char *kind = message.type == SystemNotifyType::Error || message.type == SystemNotifyType::Player ? "ERROR" : message.type == SystemNotifyType::Warning ? "WARNING" : message.type == SystemNotifyType::Music ? "MUSIC" : "NOTICE"; lv_label_set_text(notice_title, kind); lv_label_set_text(notice_body, message.text); lv_obj_clear_flag(notice, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(notice); notice_started = millis(); }
void ui_system_popup_dismiss() { ui_system_popup_dismiss_immediate(); }
void ui_system_popup_dismiss_immediate() { if (notice != nullptr) lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN); notice_started = 0; }
bool ui_system_popup_is_visible() { return notice_started != 0; }
bool ui_system_popup_is_blocking() { return notice_started != 0 && current.type != SystemNotifyType::Music; }
