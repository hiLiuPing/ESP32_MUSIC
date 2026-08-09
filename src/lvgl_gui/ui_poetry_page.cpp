#include "gui/screens/ui_poetry_page.h"

#include <cstring>

#include "app/poetry_app.h"
#include "gui/ui_heiti_font.h"
#include "lvgl_ui_common.h"

namespace {
constexpr char kParagraphIndent[] = "\xE3\x80\x80\xE3\x80\x80";
constexpr size_t kParagraphCapacity = 3080U;

lv_obj_t *root = nullptr;
lv_obj_t *title = nullptr;
lv_obj_t *body = nullptr;
GuiPageDescriptor page = {};
PoetryCollection collection = PoetryCollection::Song3000;
char paragraph[kParagraphCapacity] = {};

void set_paragraph(const char *source) {
    size_t length = 0U;
    std::memcpy(paragraph, kParagraphIndent, sizeof(kParagraphIndent) - 1U);
    length = sizeof(kParagraphIndent) - 1U;
    if (source != nullptr) {
        while (*source != '\0' && length + 1U < sizeof(paragraph)) {
            const char ch = *source++;
            if (ch != '\r' && ch != '\n') paragraph[length++] = ch;
        }
    }
    paragraph[length] = '\0';
    lv_label_set_text(body, paragraph);
}

void refresh() {
    PoetryEntry entry = {};
    if (!poetry_app_get_random(collection, &entry)) { lv_label_set_text(title, "POETRY"); lv_label_set_text(body, "POETRY CACHE LOADING"); return; }
    lv_label_set_text(title, entry.title);
    set_paragraph(entry.body);
}
void create_page() { root = lvgl_page_create(); title = lvgl_label(root, "POETRY", 8, 3, 368, ui_heiti_font_get(18)); lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0); body = lvgl_label(root, "", 8, 25, 368, ui_heiti_font_get(18)); lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP); page.view = root; }
void enter() { lv_obj_set_style_text_font(title, ui_heiti_font_get(18), 0); lv_obj_set_style_text_font(body, ui_heiti_font_get(18), 0); refresh(); }
bool consume(const KeyEvent &event) { if (event.id == KeyId::Middle && event.gesture == KeyGesture::Click) { collection = collection == PoetryCollection::Song3000 ? PoetryCollection::Song300 : PoetryCollection::Song3000; refresh(); return true; } return false; }
bool service() { return false; }
bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_poetry_page_descriptor() {
    if (page.init == nullptr) page = GuiPageDescriptor{UiPage::Poetry, create_page, enter, nullptr, consume, service, status_update, root, "poetry", true, false, nullptr};
    return page;
}
