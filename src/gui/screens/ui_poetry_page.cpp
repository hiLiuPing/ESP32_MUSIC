#include "gui/screens/ui_poetry_page.h"

#include "app/poetry_app.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_heiti_font.h"

namespace {
GuiEguiView view;
PoetryEntry entry = {};
PoetryCollection collection = PoetryCollection::Song3000;

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    gui_draw_header(canvas, "POETRY");
    gui_draw_text(canvas, 12, 30, poetry_app_collection_name(collection));
    if (!entry.valid) {
        gui_draw_text(canvas, 12, 65, "NO POETRY RESOURCE");
        return;
    }
    const egui_font_t *font = ui_heiti_font_get(16U);
    egui_canvas_draw_text(canvas, font, entry.title, 18, 53, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), entry.author, 18, 76, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    egui_region_t body = {{18, 96}, {348, 52}};
    egui_canvas_draw_text_in_rect(canvas, font, entry.body, &body, EGUI_ALIGN_LEFT, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
}
void init() { gui_egui_view_init(&view, egui_port_core(), draw); poetry_app_init(); }
void enter() { (void)poetry_app_get_random(collection, &entry); }
void exit() {}
bool key_consume(const KeyEvent &event) {
    if (event.id != KeyId::Middle || event.gesture != KeyGesture::Click) return false;
    collection = static_cast<PoetryCollection>((static_cast<uint8_t>(collection) + 1U) % 4U);
    (void)poetry_app_get_random(collection, &entry);
    return true;
}
bool service() { return false; }
bool update_status(const PlayerStatus &) { return false; }
GuiPageDescriptor descriptor = {UiPage::Poetry, init, enter, exit, key_consume, service, update_status, EGUI_VIEW_OF(&view), "poetry", true, false};
}
GuiPageDescriptor &ui_poetry_page_descriptor() { return descriptor; }
