#include "gui/screens/ui_poetry_page.h"

#include "app/poetry_app.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_heiti_font.h"
#include "gui/ui_popups.h"

namespace {
GuiEguiView view;
PoetryEntry entry = {};
PoetryCollection collection = PoetryCollection::Song3000;
bool content_ready = false;

void refresh_content() {
    content_ready = false;
    for (uint8_t retry = 0U; retry < 24U && !content_ready; ++retry) {
        content_ready = poetry_app_get_random(collection, &entry) &&
                        ui_poetry_popup_prepare_cached(&entry);
    }
}

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    if (content_ready && ui_poetry_popup_draw_cached(canvas)) {
        return;
    }
    egui_region_t message = {{54, 58}, {320, 28}};
    egui_canvas_draw_text_in_rect(canvas, ui_heiti_font_get(16U), "诗词资源加载失败", &message,
                                  EGUI_ALIGN_CENTER, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
}
void init() { gui_egui_view_init(&view, egui_port_core(), draw); poetry_app_init(); }
void enter() { refresh_content(); }
void exit() {}
void navigation_changed(bool) {}
bool key_consume(const KeyEvent &event) {
    if (event.id != KeyId::Middle || event.gesture != KeyGesture::Click) return false;
    collection = static_cast<PoetryCollection>((static_cast<uint8_t>(collection) + 1U) % 4U);
    refresh_content();
    return true;
}
bool service() { return false; }
bool update_status(const PlayerStatus &) { return false; }
GuiPageDescriptor descriptor = {UiPage::Poetry, init, enter, exit, key_consume, service, update_status, EGUI_VIEW_OF(&view), "poetry", true, false, navigation_changed};
}
GuiPageDescriptor &ui_poetry_page_descriptor() { return descriptor; }
