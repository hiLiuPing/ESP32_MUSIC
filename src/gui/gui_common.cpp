#include "gui/gui_common.h"

#include <cstdio>
#include <cstring>

namespace {
void gui_view_on_draw(egui_view_t *self) {
    GuiEguiView *view = reinterpret_cast<GuiEguiView *>(self);
    egui_canvas_t *canvas = egui_view_get_canvas(self);
    if ((canvas != nullptr) && (view->draw != nullptr)) {
        view->draw(canvas);
    }
}

const egui_view_api_t gui_view_api = {
    .dispatch_touch_event = egui_view_dispatch_touch_event,
    .on_touch_event = egui_view_on_touch_event,
    .on_intercept_touch_event = egui_view_on_intercept_touch_event,
    .compute_scroll = egui_view_compute_scroll,
    .calculate_layout = egui_view_calculate_layout,
    .request_layout = egui_view_request_layout,
    .draw = egui_view_draw,
    .on_attach_to_window = egui_view_on_attach_to_window,
    .on_draw = gui_view_on_draw,
    .on_detach_from_window = egui_view_on_detach_from_window,
};
}

void gui_egui_view_init(GuiEguiView *view, egui_core_t *core,
                        void (*draw)(egui_canvas_t *canvas)) {
    egui_view_init(EGUI_VIEW_OF(view), core);
    view->base.api = &gui_view_api;
    view->draw = draw;
    egui_view_set_size(EGUI_VIEW_OF(view), EGUI_CONFIG_SCREEN_WIDTH,
                       EGUI_CONFIG_SCREEN_HEIGHT);
}

void gui_draw_text(egui_canvas_t *canvas, int16_t x, int16_t y,
                   const char *text, bool inverted) {
    egui_canvas_draw_text(canvas, (const egui_font_t *)EGUI_CONFIG_FONT_DEFAULT,
                          text == nullptr ? "" : text, x, y,
                          inverted ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK,
                          EGUI_ALPHA_100);
}

void gui_draw_page_background(egui_canvas_t *canvas) {
    egui_canvas_draw_rectangle_fill(canvas, 0, 0, EGUI_CONFIG_SCREEN_WIDTH,
                                    EGUI_CONFIG_SCREEN_HEIGHT,
                                    EGUI_COLOR_WHITE, EGUI_ALPHA_100);
}

void gui_draw_header(egui_canvas_t *canvas, const char *title) {
    gui_draw_text(canvas, 8, 4, title);
    egui_canvas_draw_line(canvas, 0, 21, EGUI_CONFIG_SCREEN_WIDTH - 1, 21, 1,
                          EGUI_COLOR_BLACK, EGUI_ALPHA_100);
}

void gui_copy_utf8_fitted(const char *source, char *destination,
                          size_t capacity, int16_t width) {
    if ((destination == nullptr) || (capacity == 0)) {
        return;
    }
    destination[0] = '\0';
    if (source == nullptr) {
        return;
    }

    const size_t max_chars = static_cast<size_t>(width > 0 ? width / 8 : 0);
    size_t output_pos = 0;
    for (size_t source_pos = 0;
         (source[source_pos] != '\0') && (output_pos + 1 < capacity) &&
         (output_pos < max_chars);
         ++source_pos) {
        const unsigned char value = static_cast<unsigned char>(source[source_pos]);
        destination[output_pos++] = (value >= 0x20U && value <= 0x7EU)
                                        ? static_cast<char>(value)
                                        : '?';
    }
    destination[output_pos] = '\0';
}

void gui_format_time(uint32_t seconds, char *buffer, size_t capacity) {
    if ((buffer == nullptr) || (capacity == 0)) {
        return;
    }
    std::snprintf(buffer, capacity, "%02lu:%02lu",
                  static_cast<unsigned long>(seconds / 60),
                  static_cast<unsigned long>(seconds % 60));
}
