#pragma once

#include <lvgl.h>

inline lv_obj_t *lvgl_page_create() {
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(page, lv_color_white(), 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    return page;
}

inline lv_obj_t *lvgl_label(lv_obj_t *parent, const char *text, int16_t x, int16_t y,
                             int16_t width, const lv_font_t *font = &lv_font_montserrat_14) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text == nullptr ? "" : text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    return label;
}

inline void lvgl_header(lv_obj_t *page, const char *title) {
    lvgl_label(page, title, 8, 3, 300, &lv_font_montserrat_14);
    lv_obj_t *line = lv_obj_create(page);
    lv_obj_set_pos(line, 0, 21);
    lv_obj_set_size(line, 384, 1);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_border_width(line, 0, 0);
}
