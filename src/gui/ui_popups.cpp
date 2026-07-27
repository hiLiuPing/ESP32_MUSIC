#include "gui/ui_popups.h"

#include <cstdio>
#include <cstring>

#include "app/settings_app.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_heiti_font.h"

namespace {
constexpr int16_t POETRY_SCREEN_W = EGUI_CONFIG_SCREEN_WIDTH;
constexpr int16_t POETRY_SCREEN_H = EGUI_CONFIG_SCREEN_HEIGHT;
constexpr int16_t POETRY_PAD_X = 12;
constexpr int16_t POETRY_PAD_Y = 6;
constexpr uint8_t POETRY_FONT_SIZE = 18U;
constexpr int16_t POETRY_LINE_STEP = 22;
constexpr int16_t POETRY_TITLE_BODY_GAP = 8;
constexpr int16_t POETRY_FONT_TOP_GUARD = 4;
constexpr int16_t POETRY_DISPLAY_W = POETRY_SCREEN_W - 2 * POETRY_PAD_X;
constexpr int16_t POETRY_CONTENT_H = POETRY_SCREEN_H - 2 * POETRY_PAD_Y;
constexpr uint8_t POETRY_MAX_LINES = 12U;
constexpr uint8_t POETRY_PREPARE_RETRIES = 24U;

GuiEguiView view;
bool initialized = false;
bool poetry_visible = false;
bool poetry_ready = false;
bool system_visible = false;
enum class SystemPopupPhase : uint8_t { Idle, Entering, Holding, Exiting };
SystemPopupPhase system_phase = SystemPopupPhase::Idle;
SystemNotifyMessage system_message = {};
char poetry_title[128] = {};
char poetry_body[3072] = {};
char *poetry_lines[POETRY_MAX_LINES] = {};
uint8_t poetry_line_count = 0U;
int16_t poetry_title_width = 0;
int16_t poetry_body_height = 0;
uint32_t poetry_started = 0U;
uint32_t next_poetry = 0U;
uint32_t system_started = 0U;
int16_t system_panel_y = -72;
uint16_t last_poetry_interval = 20U;

uint8_t utf8_bytes(const char *text) {
    if (text == nullptr || *text == '\0') return 0U;
    const uint8_t value = static_cast<uint8_t>(*text);
    uint8_t bytes = (value & 0x80U) == 0U ? 1U : (value & 0xE0U) == 0xC0U ? 2U :
                    (value & 0xF0U) == 0xE0U ? 3U : (value & 0xF8U) == 0xF0U ? 4U : 1U;
    for (uint8_t i = 1U; i < bytes; ++i) if (text[i] == '\0') return i;
    return bytes;
}

int16_t estimated_glyph_width(uint8_t bytes) {
    return bytes == 1U ? POETRY_FONT_SIZE / 2 : POETRY_FONT_SIZE;
}

int16_t estimated_text_width(const char *text) {
    int16_t width = 0;
    while (text != nullptr && *text != '\0') {
        const uint8_t bytes = utf8_bytes(text);
        if (bytes == 0U) break;
        width = static_cast<int16_t>(width + estimated_glyph_width(bytes));
        text += bytes;
    }
    return width;
}

void copy_utf8(char *destination, size_t capacity, const char *source) {
    if (destination == nullptr || capacity == 0U) return;
    size_t used = 0U;
    while (source != nullptr && *source != '\0') {
        const uint8_t bytes = utf8_bytes(source);
        if (bytes == 0U || used + bytes >= capacity) break;
        std::memcpy(destination + used, source, bytes);
        used += bytes;
        source += bytes;
    }
    destination[used] = '\0';
}

bool prepare_poetry(const PoetryEntry &entry) {
    poetry_ready = false;
    if (!entry.valid || entry.title == nullptr || entry.body == nullptr ||
        !ui_heiti_font_is_ready(POETRY_FONT_SIZE)) return false;
    copy_utf8(poetry_title, sizeof(poetry_title), entry.title);
    poetry_title_width = estimated_text_width(poetry_title);
    if (poetry_title[0] == '\0' || poetry_title_width > POETRY_DISPLAY_W) return false;

    size_t output = 0U;
    uint8_t line_index = 0U;
    int16_t line_width = 0;
    int16_t line_limit = POETRY_DISPLAY_W;
    const char *source = entry.body;
    while (*source != '\0' && output + 1U < sizeof(poetry_body)) {
        if (*source == '\r') { ++source; continue; }
        if (*source == '\n') {
            ++source;
            if (line_width == 0) continue;
            if (line_index + 1U >= POETRY_MAX_LINES || output + 1U >= sizeof(poetry_body)) return false;
            poetry_body[output++] = '\n';
            ++line_index;
            line_width = 0;
            continue;
        }
        if (line_width == 0 && (*source == ' ' || *source == '\t')) { ++source; continue; }
        const uint8_t bytes = utf8_bytes(source);
        if (bytes == 0U || output + bytes >= sizeof(poetry_body)) break;
        const int16_t glyph_width = estimated_glyph_width(bytes);
        if (line_width > 0 && line_width + glyph_width > line_limit) {
            if (line_index + 1U >= POETRY_MAX_LINES || output + 1U >= sizeof(poetry_body)) return false;
            poetry_body[output++] = '\n';
            ++line_index;
            line_width = 0;
        }
        std::memcpy(poetry_body + output, source, bytes);
        output += bytes;
        source += bytes;
        line_width = static_cast<int16_t>(line_width + glyph_width);
    }
    poetry_body[output] = '\0';
    if (poetry_body[0] == '\0') return false;

    poetry_line_count = 0U;
    char *line = poetry_body;
    while (*line != '\0' && poetry_line_count < POETRY_MAX_LINES) {
        poetry_lines[poetry_line_count++] = line;
        char *end = std::strchr(line, '\n');
        if (end == nullptr) break;
        *end = '\0';
        line = end + 1;
    }
    poetry_body_height = poetry_line_count == 0U ? 0 :
                          static_cast<int16_t>((poetry_line_count - 1U) * POETRY_LINE_STEP + POETRY_FONT_SIZE);
    const int16_t total_height = static_cast<int16_t>(POETRY_FONT_SIZE +
                                                      POETRY_TITLE_BODY_GAP +
                                                      poetry_body_height);
    poetry_ready = poetry_line_count != 0U && total_height <= POETRY_CONTENT_H;
    return poetry_ready;
}

void draw_poetry(egui_canvas_t *canvas) {
    egui_canvas_draw_rectangle_fill(canvas, 0, 0, POETRY_SCREEN_W, POETRY_SCREEN_H,
                                    EGUI_COLOR_WHITE, EGUI_ALPHA_100);
    const egui_font_t *font = ui_heiti_font_get(POETRY_FONT_SIZE);
    const egui_region_t *work = egui_canvas_get_base_view_work_region(canvas);
    const int16_t total_height = static_cast<int16_t>(POETRY_FONT_SIZE +
                                                      POETRY_TITLE_BODY_GAP +
                                                      poetry_body_height);
    const int16_t title_x = static_cast<int16_t>((POETRY_SCREEN_W - poetry_title_width) / 2);
    const int16_t title_y = static_cast<int16_t>((POETRY_SCREEN_H - total_height) / 2);
    if (title_y + POETRY_FONT_SIZE > work->location.y &&
        title_y - POETRY_FONT_TOP_GUARD < work->location.y + work->size.height) {
        egui_canvas_draw_text(canvas, font, poetry_title, title_x, title_y,
                              EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
    const int16_t start_y = static_cast<int16_t>(title_y + POETRY_FONT_SIZE +
                                                 POETRY_TITLE_BODY_GAP);
    for (uint8_t i = 0U; i < poetry_line_count; ++i) {
        const int16_t line_width = estimated_text_width(poetry_lines[i]);
        const int16_t line_x = static_cast<int16_t>((POETRY_SCREEN_W - line_width) / 2);
        const int16_t line_y = static_cast<int16_t>(start_y + i * POETRY_LINE_STEP);
        if (line_y + POETRY_FONT_SIZE > work->location.y &&
            line_y - POETRY_FONT_TOP_GUARD < work->location.y + work->size.height) {
            egui_canvas_draw_text(canvas, font, poetry_lines[i], line_x, line_y,
                                  EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
    }
}

void draw(egui_canvas_t *canvas) {
    if (poetry_visible) {
        draw_poetry(canvas);
        return;
    }
    if (system_phase != SystemPopupPhase::Idle) {
        egui_canvas_draw_rectangle_fill(canvas, 42, system_panel_y, 300, 72, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle(canvas, 42, system_panel_y, 300, 72, 2, EGUI_COLOR_WHITE, EGUI_ALPHA_100);
        const bool error_kind = system_message.type == SystemNotifyType::Error ||
                                system_message.type == SystemNotifyType::Storage ||
                                system_message.type == SystemNotifyType::Audio ||
                                system_message.type == SystemNotifyType::Player;
        const char *kind = error_kind ? "ERROR" :
                           system_message.type == SystemNotifyType::Warning ? "WARNING" : "NOTICE";
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_16_4), kind, 58, system_panel_y + 9, EGUI_COLOR_WHITE, EGUI_ALPHA_100);
        egui_canvas_draw_text(canvas, EGUI_FONT_OF(&egui_res_font_montserrat_12_4), system_message.text, 58, system_panel_y + 37, EGUI_COLOR_WHITE, EGUI_ALPHA_100);
    }
}

void invalidate() { if (initialized) egui_view_invalidate_full(EGUI_VIEW_OF(&view)); }
}

void ui_popups_init() {
    if (initialized) return;
    gui_egui_view_init(&view, egui_port_core(), draw);
    egui_core_add_user_root_view(EGUI_VIEW_OF(&view));
    egui_view_set_visible(EGUI_VIEW_OF(&view), 1);
    initialized = true;
    next_poetry = millis() + 1200000UL;
}

void ui_popups_service(bool home_active) {
    ui_popups_init();
    const uint32_t now = millis();
    SystemNotifyMessage message = {};
    if (system_notify_try_receive(&message)) ui_system_popup_show(message);
    if (system_phase == SystemPopupPhase::Entering) {
        const uint32_t elapsed = now - system_started;
        if (elapsed >= 250U) {
            system_panel_y = 42;
            system_phase = SystemPopupPhase::Holding;
            system_started = now;
        } else {
            system_panel_y = static_cast<int16_t>(-72 + (114L * elapsed) / 250U);
        }
        invalidate();
    } else if (system_phase == SystemPopupPhase::Holding) {
        if (now - system_started >= 4500U) ui_system_popup_dismiss();
    } else if (system_phase == SystemPopupPhase::Exiting) {
        const uint32_t elapsed = now - system_started;
        if (elapsed >= 250U) {
            system_phase = SystemPopupPhase::Idle;
            system_visible = false;
            system_panel_y = -72;
        } else {
            system_panel_y = static_cast<int16_t>(42 - (114L * elapsed) / 250U);
        }
        invalidate();
    }
    const AppSettings settings = settings_app_get();
    if (settings.poetry_interval_min != last_poetry_interval) {
        last_poetry_interval = settings.poetry_interval_min;
        next_poetry = now + static_cast<uint32_t>(settings.poetry_interval_min) * 60000UL;
    }
    if (!settings.poetry_enabled && poetry_visible) ui_poetry_popup_dismiss();
    if (poetry_visible && now - poetry_started >= static_cast<uint32_t>(settings.poetry_duration_s) * 1000UL) ui_poetry_popup_dismiss();
    if (home_active && settings.poetry_enabled && settings.poetry_interval_min != 0U && static_cast<int32_t>(now - next_poetry) >= 0) {
        bool prepared = false;
        PoetryEntry entry = {};
        for (uint8_t retry = 0U; retry < POETRY_PREPARE_RETRIES && !prepared; ++retry) {
            prepared = poetry_app_get_random(PoetryCollection::Song3000, &entry) && prepare_poetry(entry);
        }
        if (prepared) {
            poetry_visible = true;
            system_visible = false;
            poetry_started = now;
            egui_view_remove_from_user_root(EGUI_VIEW_OF(&view));
            egui_core_add_user_root_view(EGUI_VIEW_OF(&view));
            invalidate();
            next_poetry = now + static_cast<uint32_t>(settings.poetry_interval_min) * 60000UL;
        } else {
            // LittleFS may finish mounting after the GUI task starts.
            next_poetry = now + 5000UL;
        }
    }
}

void ui_poetry_popup_show(const PoetryEntry *entry) {
    if (entry == nullptr || !prepare_poetry(*entry)) return;
    poetry_visible = true;
    system_visible = false;
    poetry_started = millis();
    egui_view_remove_from_user_root(EGUI_VIEW_OF(&view));
    egui_core_add_user_root_view(EGUI_VIEW_OF(&view));
    invalidate();
}
void ui_poetry_popup_dismiss() { poetry_visible = false; invalidate(); }
bool ui_poetry_popup_is_visible() { return poetry_visible; }
bool ui_poetry_popup_prepare_cached(const PoetryEntry *entry) {
    return entry != nullptr && prepare_poetry(*entry);
}
bool ui_poetry_popup_draw_cached(egui_canvas_t *canvas) {
    if (canvas == nullptr || !poetry_ready) return false;
    draw_poetry(canvas);
    return true;
}
void ui_system_popup_show(const SystemNotifyMessage &message) {
    system_message = message;
    poetry_visible = false;
    system_visible = true;
    system_phase = SystemPopupPhase::Entering;
    system_panel_y = -72;
    system_started = millis();
    invalidate();
}
void ui_system_popup_dismiss() {
    if (system_phase == SystemPopupPhase::Idle || system_phase == SystemPopupPhase::Exiting) return;
    system_phase = SystemPopupPhase::Exiting;
    system_started = millis();
    invalidate();
}
void ui_system_popup_dismiss_immediate() {
    system_phase = SystemPopupPhase::Idle;
    system_visible = false;
    system_panel_y = -72;
    invalidate();
}
bool ui_system_popup_is_visible() { return system_phase != SystemPopupPhase::Idle; }
