#include "gui/ui_popups.h"

#include <Arduino.h>

#include "app/settings_app.h"
#include "background/egui_background_color.h"
#include "gui/egui_port.h"
#include "gui/ui_heiti_font.h"
#include "gui/ui_poetry_cache.h"
#include "widget/egui_view_group.h"
#include "widget/egui_view_label.h"

namespace {
constexpr int16_t SYSTEM_PANEL_X = 42;
constexpr int16_t SYSTEM_PANEL_Y = 42;
constexpr int16_t SYSTEM_PANEL_W = 300;
constexpr int16_t SYSTEM_PANEL_H = 72;
constexpr int16_t SYSTEM_PANEL_HIDDEN_Y = -SYSTEM_PANEL_H;
constexpr uint32_t SYSTEM_ANIMATION_MS = 250U;
constexpr uint32_t SYSTEM_HOLD_MS = 4500U;
constexpr uint32_t MUSIC_HOLD_MS = 1500U;
constexpr const char *MUSIC_GLYPHS =
    "音乐控制播放暂停上一曲下一曲音量加减没有可歌曲失败操作已跳过无法的文件";

egui_background_color_param_t poetry_panel_background_param;
egui_background_params_t poetry_panel_background_params;
egui_background_color_t poetry_panel_background;
egui_background_color_param_t system_panel_background_param;
egui_background_params_t system_panel_background_params;
egui_background_color_t system_panel_background;

egui_view_group_t root;
egui_view_group_t poetry_panel;
egui_view_label_t poetry_title;
egui_view_label_t poetry_lines[UI_POETRY_MAX_LINES];
egui_view_group_t system_panel;
egui_view_label_t system_kind;
egui_view_label_t system_text;

bool initialized = false;
bool poetry_visible = false;
const UiPoetryCacheSlot *visible_poetry = nullptr;
uint32_t poetry_started = 0U;
uint32_t poetry_show_storage_reads = 0U;
uint32_t next_poetry = 0U;
uint16_t last_poetry_interval = 20U;

enum class SystemPopupPhase : uint8_t { Idle, Entering, Holding, Exiting };
SystemPopupPhase system_phase = SystemPopupPhase::Idle;
SystemNotifyMessage system_message = {};
uint32_t system_started = 0U;
int16_t system_panel_y = SYSTEM_PANEL_HIDDEN_Y;

void set_region(egui_view_t *view, int16_t x, int16_t y,
                int16_t width, int16_t height) {
    egui_region_t region = {{x, y}, {width, height}};
    egui_view_layout(view, &region);
}

void invalidate_region(int16_t x, int16_t y, int16_t width, int16_t height) {
    if (!initialized) return;
    egui_region_t region = {{x, y}, {width, height}};
    egui_view_invalidate_region(EGUI_VIEW_OF(&root), &region);
}

void initialize_label(egui_view_label_t &label, const egui_font_t *font,
                      egui_color_t color, uint8_t align) {
    egui_view_label_init(EGUI_VIEW_OF(&label), egui_port_core());
    egui_view_label_set_font(EGUI_VIEW_OF(&label), font);
    egui_view_label_set_font_color(EGUI_VIEW_OF(&label), color, EGUI_ALPHA_100);
    egui_view_label_set_align_type(EGUI_VIEW_OF(&label), align);
    egui_view_label_set_text(EGUI_VIEW_OF(&label), "");
}

void initialize_backgrounds() {
    poetry_panel_background_param = {};
    poetry_panel_background_param.type =
        EGUI_BACKGROUND_COLOR_TYPE_ROUND_RECTANGLE;
    poetry_panel_background_param.alpha = EGUI_ALPHA_100;
    poetry_panel_background_param.color = EGUI_COLOR_WHITE;
    poetry_panel_background_param.stroke_width = 1;
    poetry_panel_background_param.stroke_alpha = EGUI_ALPHA_100;
    poetry_panel_background_param.stroke_color = EGUI_COLOR_BLACK;
    poetry_panel_background_param.shape.round_rectangle.radius =
        UI_POETRY_PANEL_RADIUS;
    poetry_panel_background_params = {};
    poetry_panel_background_params.normal_param =
        &poetry_panel_background_param;
    egui_background_color_init_with_params(
        EGUI_BG_OF(&poetry_panel_background),
        &poetry_panel_background_params);

    system_panel_background_param = {};
    system_panel_background_param.type =
        EGUI_BACKGROUND_COLOR_TYPE_ROUND_RECTANGLE;
    system_panel_background_param.alpha = EGUI_ALPHA_100;
    system_panel_background_param.color = EGUI_COLOR_WHITE;
    system_panel_background_param.stroke_width = 2;
    system_panel_background_param.stroke_alpha = EGUI_ALPHA_100;
    system_panel_background_param.stroke_color = EGUI_COLOR_BLACK;
    system_panel_background_param.shape.round_rectangle.radius = 12;
    system_panel_background_params = {};
    system_panel_background_params.normal_param =
        &system_panel_background_param;
    egui_background_color_init_with_params(
        EGUI_BG_OF(&system_panel_background),
        &system_panel_background_params);
}

void configure_poetry_labels(const UiPoetryCacheSlot &slot) {
    egui_view_label_set_text(EGUI_VIEW_OF(&poetry_title), slot.title);
    set_region(EGUI_VIEW_OF(&poetry_title), UI_POETRY_TEXT_PAD_X,
               UI_POETRY_TEXT_PAD_Y, UI_POETRY_DISPLAY_W,
               UI_POETRY_TITLE_LINE_H);

    for (uint8_t i = 0U; i < UI_POETRY_MAX_LINES; ++i) {
        const bool visible = i < slot.line_count;
        egui_view_set_visible(EGUI_VIEW_OF(&poetry_lines[i]), visible ? 1 : 0);
        if (!visible) continue;
        egui_view_label_set_text(EGUI_VIEW_OF(&poetry_lines[i]), slot.lines[i]);
        set_region(EGUI_VIEW_OF(&poetry_lines[i]),
                   slot.line_x[i], slot.line_y[i],
                   static_cast<int16_t>(UI_POETRY_PANEL_W -
                                        UI_POETRY_TEXT_PAD_X - slot.line_x[i]),
                   UI_POETRY_FONT_SIZE);
    }
}

bool show_cached_poetry() {
    if (system_phase != SystemPopupPhase::Idle) return false;
    const UiPoetryCacheSlot *slot =
        ui_poetry_cache_select(PoetryCollection::Song3000);
    if (slot == nullptr) return false;
    visible_poetry = slot;
    configure_poetry_labels(*slot);
    poetry_visible = true;
    poetry_started = millis();
    poetry_show_storage_reads = ui_heiti_font_storage_read_count();
    egui_view_set_visible(EGUI_VIEW_OF(&poetry_panel), 1);
    egui_view_remove_from_user_root(EGUI_VIEW_OF(&root));
    egui_core_add_user_root_view(EGUI_VIEW_OF(&root));
    invalidate_region(UI_POETRY_PANEL_X, UI_POETRY_PANEL_Y,
                      UI_POETRY_PANEL_W, UI_POETRY_PANEL_H);
    return true;
}

void set_system_panel_y(int16_t next_y) {
    const int16_t old_y = system_panel_y;
    system_panel_y = next_y;
    egui_view_set_position(EGUI_VIEW_OF(&system_panel), SYSTEM_PANEL_X,
                           system_panel_y);
    invalidate_region(SYSTEM_PANEL_X, old_y, SYSTEM_PANEL_W, SYSTEM_PANEL_H);
    invalidate_region(SYSTEM_PANEL_X, system_panel_y,
                      SYSTEM_PANEL_W, SYSTEM_PANEL_H);
}
}

void ui_popups_init() {
    if (initialized) return;
    egui_core_t *core = egui_port_core();
    const bool poetry_cache_ready = ui_poetry_cache_init();
    const bool music_glyphs_ready =
        poetry_cache_ready && ui_heiti_font_cache_text(16U, MUSIC_GLYPHS);
    const egui_font_t *system_font = music_glyphs_ready
                                         ? ui_heiti_font_get_cached(16U)
                                         : ui_heiti_font_get(16U);
    egui_view_group_init(EGUI_VIEW_OF(&root), core);
    egui_view_set_size(EGUI_VIEW_OF(&root), EGUI_CONFIG_SCREEN_WIDTH,
                       EGUI_CONFIG_SCREEN_HEIGHT);
    initialize_backgrounds();

    egui_view_group_init(EGUI_VIEW_OF(&poetry_panel), core);
    set_region(EGUI_VIEW_OF(&poetry_panel), UI_POETRY_PANEL_X,
               UI_POETRY_PANEL_Y, UI_POETRY_PANEL_W, UI_POETRY_PANEL_H);
    egui_view_set_background(EGUI_VIEW_OF(&poetry_panel),
                             EGUI_BG_OF(&poetry_panel_background));
    initialize_label(poetry_title, ui_heiti_font_get_cached(UI_POETRY_FONT_SIZE),
                     EGUI_COLOR_BLACK, EGUI_ALIGN_CENTER);
    egui_view_group_add_child(EGUI_VIEW_OF(&poetry_panel),
                              EGUI_VIEW_OF(&poetry_title));
    for (egui_view_label_t &line : poetry_lines) {
        initialize_label(line, ui_heiti_font_get_cached(UI_POETRY_FONT_SIZE),
                         EGUI_COLOR_BLACK, EGUI_ALIGN_LEFT | EGUI_ALIGN_TOP);
        egui_view_group_add_child(EGUI_VIEW_OF(&poetry_panel), EGUI_VIEW_OF(&line));
    }
    egui_view_set_visible(EGUI_VIEW_OF(&poetry_panel), 0);
    egui_view_group_add_child(EGUI_VIEW_OF(&root), EGUI_VIEW_OF(&poetry_panel));

    egui_view_group_init(EGUI_VIEW_OF(&system_panel), core);
    set_region(EGUI_VIEW_OF(&system_panel), SYSTEM_PANEL_X,
               SYSTEM_PANEL_HIDDEN_Y, SYSTEM_PANEL_W, SYSTEM_PANEL_H);
    egui_view_set_background(EGUI_VIEW_OF(&system_panel),
                             EGUI_BG_OF(&system_panel_background));
    initialize_label(system_kind, system_font, EGUI_COLOR_BLACK,
                     EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER);
    initialize_label(system_text, system_font, EGUI_COLOR_BLACK,
                     EGUI_ALIGN_LEFT | EGUI_ALIGN_VCENTER);
    set_region(EGUI_VIEW_OF(&system_kind), 16, 7, 268, 24);
    set_region(EGUI_VIEW_OF(&system_text), 16, 34, 268, 26);
    egui_view_group_add_child(EGUI_VIEW_OF(&system_panel), EGUI_VIEW_OF(&system_kind));
    egui_view_group_add_child(EGUI_VIEW_OF(&system_panel), EGUI_VIEW_OF(&system_text));
    egui_view_set_visible(EGUI_VIEW_OF(&system_panel), 0);
    egui_view_group_add_child(EGUI_VIEW_OF(&root), EGUI_VIEW_OF(&system_panel));

    egui_core_add_user_root_view(EGUI_VIEW_OF(&root));
    egui_view_set_visible(EGUI_VIEW_OF(&root), 1);
    initialized = true;
    next_poetry = millis() + 1200000UL;
}

void ui_popups_service(UiPage current_page) {
    ui_popups_init();
    (void)ui_poetry_cache_service();
    const uint32_t now = millis();
    const bool boot_active = current_page == UiPage::Boot;
    const bool home_active = current_page == UiPage::Home;

    const AppSettings settings = settings_app_get();
    if (settings.poetry_interval_min != last_poetry_interval) {
        last_poetry_interval = settings.poetry_interval_min;
        next_poetry = now + static_cast<uint32_t>(settings.poetry_interval_min) * 60000UL;
    }

    if (boot_active) {
        ui_poetry_popup_dismiss();
        ui_system_popup_dismiss_immediate();
        return;
    }

    if (system_phase == SystemPopupPhase::Idle ||
        system_message.type == SystemNotifyType::Music) {
        SystemNotifyMessage message = {};
        if (system_notify_try_receive(&message)) ui_system_popup_show(message);
    }

    if (system_phase == SystemPopupPhase::Entering) {
        const uint32_t elapsed = now - system_started;
        if (elapsed >= SYSTEM_ANIMATION_MS) {
            set_system_panel_y(SYSTEM_PANEL_Y);
            system_phase = SystemPopupPhase::Holding;
            system_started = now;
        } else {
            set_system_panel_y(static_cast<int16_t>(
                SYSTEM_PANEL_HIDDEN_Y +
                ((SYSTEM_PANEL_Y - SYSTEM_PANEL_HIDDEN_Y) * elapsed) /
                    SYSTEM_ANIMATION_MS));
        }
    } else if (system_phase == SystemPopupPhase::Holding) {
        const uint32_t hold_ms = system_message.type == SystemNotifyType::Music
                                     ? MUSIC_HOLD_MS
                                     : SYSTEM_HOLD_MS;
        if (now - system_started >= hold_ms) ui_system_popup_dismiss();
    } else if (system_phase == SystemPopupPhase::Exiting) {
        const uint32_t elapsed = now - system_started;
        if (elapsed >= SYSTEM_ANIMATION_MS) {
            set_system_panel_y(SYSTEM_PANEL_HIDDEN_Y);
            egui_view_set_visible(EGUI_VIEW_OF(&system_panel), 0);
            system_phase = SystemPopupPhase::Idle;
            return;
        } else {
            set_system_panel_y(static_cast<int16_t>(
                SYSTEM_PANEL_Y -
                ((SYSTEM_PANEL_Y - SYSTEM_PANEL_HIDDEN_Y) * elapsed) /
                    SYSTEM_ANIMATION_MS));
        }
    }

    if (!home_active && poetry_visible) ui_poetry_popup_dismiss();
    if (settings.poetry_interval_min == 0U && poetry_visible) ui_poetry_popup_dismiss();
    if (poetry_visible &&
        now - poetry_started >=
            static_cast<uint32_t>(settings.poetry_duration_s) * 1000UL) {
        ui_poetry_popup_dismiss();
    }
    if (home_active && system_phase == SystemPopupPhase::Idle &&
        settings.poetry_interval_min != 0U &&
        static_cast<int32_t>(now - next_poetry) >= 0) {
        if (show_cached_poetry()) {
            next_poetry = now +
                static_cast<uint32_t>(settings.poetry_interval_min) * 60000UL;
        } else {
            next_poetry = now + 5000UL;
        }
    }
}

void ui_poetry_popup_dismiss() {
    if (!poetry_visible) return;
    poetry_visible = false;
    visible_poetry = nullptr;
    egui_view_set_visible(EGUI_VIEW_OF(&poetry_panel), 0);
    invalidate_region(UI_POETRY_PANEL_X, UI_POETRY_PANEL_Y,
                      UI_POETRY_PANEL_W, UI_POETRY_PANEL_H);
    const uint32_t reads = ui_heiti_font_storage_read_count();
    if (reads != poetry_show_storage_reads) {
        Serial.printf("[POETRY_CACHE] WARNING: popup caused %lu storage read(s)\n",
                      static_cast<unsigned long>(reads - poetry_show_storage_reads));
    }
}

bool ui_poetry_popup_is_visible() { return poetry_visible; }

void ui_system_popup_show(const SystemNotifyMessage &message) {
    ui_poetry_popup_dismiss();
    const bool replacing_music =
        system_phase != SystemPopupPhase::Idle &&
        system_message.type == SystemNotifyType::Music;
    system_message = message;
    const bool error_kind = message.type == SystemNotifyType::Error ||
                            message.type == SystemNotifyType::Storage ||
                            message.type == SystemNotifyType::Audio ||
                            message.type == SystemNotifyType::Player;
    const char *kind = message.type == SystemNotifyType::Music ? "音乐控制" :
                       error_kind ? "ERROR" :
                       message.type == SystemNotifyType::Warning ? "WARNING" : "NOTICE";
    egui_view_label_set_text(EGUI_VIEW_OF(&system_kind), kind);
    egui_view_label_set_text(EGUI_VIEW_OF(&system_text), system_message.text);
    egui_view_set_visible(EGUI_VIEW_OF(&system_panel), 1);
    system_started = millis();
    if (replacing_music) {
        system_phase = SystemPopupPhase::Holding;
        set_system_panel_y(SYSTEM_PANEL_Y);
    } else {
        system_phase = SystemPopupPhase::Entering;
        set_system_panel_y(SYSTEM_PANEL_HIDDEN_Y);
    }
    egui_view_remove_from_user_root(EGUI_VIEW_OF(&root));
    egui_core_add_user_root_view(EGUI_VIEW_OF(&root));
}

void ui_system_popup_dismiss() {
    if (system_phase == SystemPopupPhase::Idle ||
        system_phase == SystemPopupPhase::Exiting) return;
    system_phase = SystemPopupPhase::Exiting;
    system_started = millis();
}

void ui_system_popup_dismiss_immediate() {
    if (!initialized) return;
    system_phase = SystemPopupPhase::Idle;
    egui_view_set_visible(EGUI_VIEW_OF(&system_panel), 0);
    set_system_panel_y(SYSTEM_PANEL_HIDDEN_Y);
}

bool ui_system_popup_is_visible() {
    return system_phase != SystemPopupPhase::Idle;
}

bool ui_system_popup_is_blocking() {
    return ui_system_popup_is_visible() &&
           system_message.type != SystemNotifyType::Music;
}
