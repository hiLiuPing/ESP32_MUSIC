#include "gui/ui_popups.h"

#include <cstdio>
#include <cstring>

#include "app/settings_app.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/ui_heiti_font.h"

namespace {
GuiEguiView view;
bool initialized = false;
bool poetry_visible = false;
bool system_visible = false;
enum class SystemPopupPhase : uint8_t { Idle, Entering, Holding, Exiting };
SystemPopupPhase system_phase = SystemPopupPhase::Idle;
PoetryEntry poetry = {};
SystemNotifyMessage system_message = {};
uint32_t poetry_started = 0U;
uint32_t next_poetry = 0U;
uint32_t system_started = 0U;
int16_t system_panel_y = -72;
uint16_t last_poetry_interval = 20U;

void draw(egui_canvas_t *canvas) {
    if (poetry_visible) {
        egui_canvas_draw_rectangle_fill(canvas, 24, 2, 336, 164, EGUI_COLOR_WHITE, EGUI_ALPHA_100);
        egui_canvas_draw_rectangle(canvas, 24, 2, 336, 164, 2, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        if (poetry.valid) {
            egui_region_t title = {{36, 10}, {312, 22}};
            egui_canvas_draw_text_in_rect(canvas, ui_heiti_font_get(16), poetry.title, &title,
                                          EGUI_ALIGN_CENTER, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
            egui_region_t body = {{36, 38}, {312, 120}};
            egui_canvas_draw_text_in_rect(canvas, ui_heiti_font_get(14), poetry.body, &body,
                                          EGUI_ALIGN_LEFT, EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
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
        PoetryEntry entry = {};
        if (poetry_app_get_random(PoetryCollection::Song3000, &entry)) {
            ui_poetry_popup_show(&entry);
            next_poetry = now + static_cast<uint32_t>(settings.poetry_interval_min) * 60000UL;
        } else {
            // LittleFS may finish mounting after the GUI task starts.
            next_poetry = now + 5000UL;
        }
    }
}

void ui_poetry_popup_show(const PoetryEntry *entry) {
    if (entry == nullptr) return;
    poetry = *entry;
    poetry_visible = true;
    system_visible = false;
    poetry_started = millis();
    invalidate();
}
void ui_poetry_popup_dismiss() { poetry_visible = false; invalidate(); }
bool ui_poetry_popup_is_visible() { return poetry_visible; }
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
