#include "gui/screens/ui_poetry_page.h"

#include "app/poetry_app.h"
#include "gui/ui_heiti_font.h"
#include "lvgl_ui_common.h"

namespace {
lv_obj_t *root = nullptr;
lv_obj_t *title = nullptr;
lv_obj_t *body = nullptr;
GuiPageDescriptor page = {};
PoetryCollection collection = PoetryCollection::Song3000;

void refresh() {
    PoetryEntry entry = {};
    if (!poetry_app_get_random(collection, &entry)) { lv_label_set_text(title, "POETRY"); lv_label_set_text(body, "POETRY CACHE LOADING"); return; }
    lv_label_set_text(title, entry.title);
    lv_label_set_text(body, entry.body);
}
void create_page() { root = lvgl_page_create(); title = lvgl_label(root, "POETRY", 16, 8, 352, ui_heiti_font_get(18)); lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0); body = lvgl_label(root, "", 22, 38, 340, ui_heiti_font_get(18)); lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP); page.view = root; }
void enter() { lv_obj_set_style_text_font(title, ui_heiti_font_get(18), 0); lv_obj_set_style_text_font(body, ui_heiti_font_get(18), 0); refresh(); }
bool consume(const KeyEvent &event) { if (event.id == KeyId::Middle && event.gesture == KeyGesture::Click) { collection = collection == PoetryCollection::Song3000 ? PoetryCollection::Song300 : PoetryCollection::Song3000; refresh(); return true; } return false; }
bool service() { return false; }
bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_poetry_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Poetry, create_page, enter, nullptr, consume, service, status_update, root, "poetry", true, false, nullptr};
    return page;
}
