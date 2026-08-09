#include "gui/screens/ui_poetry_page.h"

#include <cstring>

#include "app/poetry_app.h"
#include "gui/ui_heiti_font.h"
#include "gui/ui_poetry_cache.h"
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
uint32_t displayed_hash = 0U;
uint32_t page_entered_ms = 0U;
uint32_t page_enter_reads = 0U;
bool pending_next = false;

void set_paragraph(const char *source) {
    size_t length = sizeof(kParagraphIndent) - 1U;
    std::memcpy(paragraph, kParagraphIndent, length);
    while (source != nullptr && *source != '\0' && length + 1U < sizeof(paragraph)) {
        const char ch = *source++;
        if (ch != '\r' && ch != '\n') paragraph[length++] = ch;
    }
    paragraph[length] = '\0';
    lv_label_set_text(body, paragraph);
}

void show_entry(const UiPoetryCacheEntry *entry) {
    if (entry == nullptr) return;
    lv_label_set_text(title, entry->title);
    set_paragraph(entry->body);
    displayed_hash = entry->content_hash;
    pending_next = false;
    Serial.printf("[POETRY_PAGE] show batch_index=%u elapsed=%lu reads_delta=%lu glyphs=%u\n",
                  static_cast<unsigned>(ui_poetry_cache_active_index()),
                  static_cast<unsigned long>(millis() - page_entered_ms),
                  static_cast<unsigned long>(ui_heiti_font_storage_read_count() - page_enter_reads),
                  static_cast<unsigned>(ui_heiti_font_poetry_cache_glyphs()));
}

void show_loading(const char *message = "POETRY CACHE LOADING") {
    displayed_hash = 0U;
    lv_label_set_text(title, "POETRY");
    lv_label_set_text(body, message);
}

void refresh_fallback() {
    PoetryEntry entry = {};
    if (!poetry_app_get_random(collection, &entry)) {
        show_loading("POETRY UNAVAILABLE");
        return;
    }
    lv_label_set_text(title, entry.title);
    set_paragraph(entry.body);
}

void refresh() {
    const UiPoetryCacheEntry *entry = ui_poetry_cache_current();
    if (entry != nullptr) show_entry(entry);
    else if (!ui_poetry_cache_init()) refresh_fallback();
    else show_loading();
}

void create_page() {
    root = lvgl_page_create();
    title = lvgl_label(root, "POETRY", 8, 3, 368, ui_heiti_font_get(18));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    body = lvgl_label(root, "", 8, 25, 368, ui_heiti_font_get(18));
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    page.view = root;
}

void enter() {
    page_entered_ms = millis();
    page_enter_reads = ui_heiti_font_storage_read_count();
    lv_obj_set_style_text_font(title, ui_heiti_font_get(18), 0);
    lv_obj_set_style_text_font(body, ui_heiti_font_get(18), 0);
    ui_poetry_cache_activate(collection);
    refresh();
}

bool consume(const KeyEvent &event) {
    if (event.gesture != KeyGesture::Click) return false;
    if (event.id == KeyId::Middle) {
        collection = collection == PoetryCollection::Song3000 ? PoetryCollection::Song300 : PoetryCollection::Song3000;
        pending_next = false;
        ui_poetry_cache_activate(collection);
        refresh();
        return true;
    }
    if (event.id != KeyId::Left && event.id != KeyId::Right) return false;
    if (!ui_poetry_cache_init()) {
        refresh_fallback();
        return true;
    }
    const UiPoetryCacheEntry *entry = ui_poetry_cache_move(event.id == KeyId::Left ? -1 : 1);
    if (entry != nullptr) show_entry(entry);
    else {
        pending_next = event.id == KeyId::Right;
        show_loading("NEXT BATCH LOADING");
    }
    return true;
}

bool service() {
    if (!ui_poetry_cache_is_ready()) return false;
    if (pending_next) {
        const UiPoetryCacheEntry *entry = ui_poetry_cache_move(1);
        if (entry != nullptr) show_entry(entry);
        return false;
    }
    const UiPoetryCacheEntry *entry = ui_poetry_cache_current();
    if (entry != nullptr && entry->content_hash != displayed_hash) show_entry(entry);
    return false;
}

bool status_update(const PlayerStatus &) { return false; }
}

GuiPageDescriptor &ui_poetry_page_descriptor() {
    if (page.init == nullptr) {
        page = GuiPageDescriptor{UiPage::Poetry, create_page, enter, nullptr, consume, service,
                                 status_update, root, "poetry", true, false, nullptr};
    }
    return page;
}
