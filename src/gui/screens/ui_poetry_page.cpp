#include "gui/screens/ui_poetry_page.h"

#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_poetry_cache.h"

namespace {
GuiEguiView view;
PoetryCollection collection = PoetryCollection::Song3000;
const UiPoetryCacheSlot *content = nullptr;

void refresh_content() {
    content = ui_poetry_cache_select(collection);
}

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    if (content != nullptr) {
        ui_poetry_cache_draw(canvas, content);
        return;
    }
    egui_region_t message = {{54, 58}, {320, 28}};
    egui_canvas_draw_text_in_rect(canvas,
                                  EGUI_FONT_OF(&egui_res_font_montserrat_16_4),
                                  "POETRY CACHE LOADING", &message,
                                  EGUI_ALIGN_CENTER, EGUI_COLOR_BLACK,
                                  EGUI_ALPHA_100);
}

void init() { gui_egui_view_init(&view, egui_port_core(), draw); }
void enter() { refresh_content(); }
void exit() {}
void navigation_changed(bool) {}

bool key_consume(const KeyEvent &event) {
    if (event.id != KeyId::Middle || event.gesture != KeyGesture::Click) return false;
    collection = collection == PoetryCollection::Song3000
                     ? PoetryCollection::Song300
                     : PoetryCollection::Song3000;
    refresh_content();
    return true;
}

bool service() {
    if (content != nullptr) return false;
    refresh_content();
    return content != nullptr;
}

bool update_status(const PlayerStatus &) { return false; }

GuiPageDescriptor descriptor = {
    UiPage::Poetry, init, enter, exit, key_consume, service, update_status,
    EGUI_VIEW_OF(&view), "poetry", true, false, navigation_changed,
};
}

GuiPageDescriptor &ui_poetry_page_descriptor() { return descriptor; }
