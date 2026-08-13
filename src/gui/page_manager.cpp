#include "gui/page_manager.h"

#include <Arduino.h>
#include <cstring>

#include "anim/egui_animation_value.h"
#include "anim/egui_interpolator_decelerate.h"
#include "app_egui_config.h"
#include "gui/egui_port.h"
#include "gui/ui_popups.h"

namespace {
constexpr size_t MAX_PAGES = 7;
constexpr size_t HISTORY_DEPTH = 8;

GuiPageDescriptor *registered_pages[MAX_PAGES] = {};
size_t page_count = 0;
GuiPageDescriptor *current_page = nullptr;
UiPage history[HISTORY_DEPTH] = {};
size_t history_size = 0;
bool dirty = true;
bool internal_navigation = false;

constexpr uint16_t PAGE_TRANSITION_MS = 180U;

struct PageTransition {
    bool active = false;
    GuiPageDescriptor *outgoing = nullptr;
    GuiPageDescriptor *incoming = nullptr;
    int8_t direction = 1;
    egui_animation_value_t animation = {};
    egui_interpolator_decelerate_t interpolator = {};
};

PageTransition transition;
bool pending_navigation = false;
UiPage pending_page = UiPage::Home;
bool pending_record_history = false;
int8_t pending_direction = 1;
bool pending_pop_history = false;

GuiPageDescriptor *find_page(UiPage id) {
    for (size_t index = 0; index < page_count; ++index) {
        if ((registered_pages[index] != nullptr) && (registered_pages[index]->id == id)) {
            return registered_pages[index];
        }
    }
    return nullptr;
}

int8_t page_direction(UiPage from, UiPage to, int8_t fallback) {
    if ((from == UiPage::Boot) || (to == UiPage::Boot)) return fallback;

    int from_index = -1;
    int to_index = -1;
    int nav_index_value = 0;
    for (size_t index = 0; index < page_count; ++index) {
        GuiPageDescriptor *candidate = registered_pages[index];
        if ((candidate == nullptr) || !candidate->nav_enabled) continue;
        if (candidate->id == from) from_index = nav_index_value;
        if (candidate->id == to) to_index = nav_index_value;
        ++nav_index_value;
    }
    if ((from_index >= 0) && (to_index >= 0) && (from_index != to_index)) {
        return to_index > from_index ? 1 : -1;
    }
    return fallback;
}

void transition_on_value(egui_animation_t *, int32_t value) {
    if (!transition.active || (transition.outgoing == nullptr) ||
        (transition.incoming == nullptr)) {
        return;
    }

    const int16_t offset = static_cast<int16_t>(constrain(value, 0, EGUI_CONFIG_SCREEN_WIDTH));
    egui_view_set_position(transition.outgoing->view,
                           static_cast<egui_dim_t>(-transition.direction * offset), 0);
    egui_view_set_position(transition.incoming->view,
                           static_cast<egui_dim_t>(transition.direction *
                                                   (EGUI_CONFIG_SCREEN_WIDTH - offset)), 0);
    egui_view_invalidate_full(transition.outgoing->view);
    egui_view_invalidate_full(transition.incoming->view);
    dirty = true;
}

void queue_navigation(UiPage page, bool record_history, int8_t direction) {
    pending_navigation = true;
    pending_page = page;
    pending_record_history = record_history;
    pending_direction = direction;
    pending_pop_history = false;
}

void ensure_initialized(GuiPageDescriptor *page) {
    if ((page == nullptr) || page->initialized) {
        return;
    }
    if (page->init != nullptr) {
        page->init();
    }
    if (page->view == nullptr) {
        Serial.printf("[GUI] page %s did not create an EGUI view\n", page->name);
        abort();
    }
    egui_core_add_user_root_view(page->view);
    egui_view_set_visible(page->view, 0);
    page->initialized = true;
}

void push_history(UiPage page) {
    if ((page == UiPage::Boot) || (page == UiPage::Home && history_size == 0)) {
        if (history_size < HISTORY_DEPTH) {
            history[history_size++] = page;
        }
        return;
    }

    if (history_size < HISTORY_DEPTH) {
        history[history_size++] = page;
        return;
    }

    const size_t erase_index = (history[0] == UiPage::Home) ? 1 : 0;
    if (erase_index + 1 < HISTORY_DEPTH) {
        std::memmove(&history[erase_index], &history[erase_index + 1],
                     (HISTORY_DEPTH - erase_index - 1) * sizeof(history[0]));
    }
    history[HISTORY_DEPTH - 1] = page;
}

bool switch_page(GuiPageDescriptor *target, bool record_history, int8_t direction) {
    if ((target == nullptr) || (target == current_page)) {
        return false;
    }
    if (transition.active) {
        queue_navigation(target->id, record_history, direction);
        return true;
    }
    Serial.printf("[GUI] page %s -> %s\n",
                  current_page == nullptr ? "none" : current_page->name,
                  target->name);
    ui_poetry_popup_dismiss();
    ui_system_popup_dismiss_immediate();

    ensure_initialized(target);
    if (record_history && (current_page != nullptr) && (current_page->id != UiPage::Boot)) {
        push_history(current_page->id);
    }

    if ((current_page != nullptr) && internal_navigation &&
        (current_page->navigation_changed != nullptr)) {
        current_page->navigation_changed(false);
    }
    internal_navigation = false;
    if ((current_page != nullptr) && (current_page->exit != nullptr)) {
        current_page->exit();
    }
    GuiPageDescriptor *previous_page = current_page;
    if ((previous_page != nullptr) && (previous_page->view != nullptr)) {
        egui_view_set_position(previous_page->view, 0, 0);
        egui_view_set_visible(previous_page->view, 1);
    }
    current_page = target;
    // The file browser is entered from the music toolbar and immediately owns keys.
    internal_navigation = current_page->id == UiPage::FileBrowser;
    if (current_page->id == UiPage::Home) {
        history_size = 0;
    }
    if (current_page->enter != nullptr) {
        current_page->enter();
    }
    if (current_page->navigation_changed != nullptr) {
        current_page->navigation_changed(internal_navigation);
    }
    egui_view_set_position(current_page->view,
                           previous_page == nullptr
                               ? 0
                               : static_cast<egui_dim_t>(direction * EGUI_CONFIG_SCREEN_WIDTH),
                           0);
    egui_view_set_visible(current_page->view, 1);

    if (previous_page == nullptr) {
        egui_view_invalidate_full(current_page->view);
        egui_core_force_refresh(egui_port_core());
        dirty = true;
        return true;
    }

    transition.active = true;
    transition.outgoing = previous_page;
    transition.incoming = current_page;
    transition.direction = direction >= 0 ? 1 : -1;
    egui_animation_value_init(EGUI_ANIM_OF(&transition.animation));
    egui_animation_value_set_range(&transition.animation, 0, EGUI_CONFIG_SCREEN_WIDTH);
    egui_animation_value_set_on_value(&transition.animation, transition_on_value);
    egui_animation_target_view_set(EGUI_ANIM_OF(&transition.animation), current_page->view);
    egui_animation_duration_set(EGUI_ANIM_OF(&transition.animation), PAGE_TRANSITION_MS);
    egui_interpolator_decelerate_init(EGUI_INTERP_OF(&transition.interpolator));
    egui_interpolator_decelerate_factor_set(EGUI_INTERP_OF(&transition.interpolator),
                                            EGUI_FLOAT_VALUE(1.0f));
    egui_animation_interpolator_set(EGUI_ANIM_OF(&transition.animation),
                                    EGUI_INTERP_OF(&transition.interpolator));
    egui_animation_start(EGUI_ANIM_OF(&transition.animation));
    egui_view_invalidate_full(previous_page->view);
    egui_view_invalidate_full(current_page->view);
    egui_core_force_refresh(egui_port_core());
    dirty = true;
    return true;
}

int nav_index(UiPage page) {
    int current_nav_index = 0;
    for (size_t index = 0; index < page_count; ++index) {
        GuiPageDescriptor *candidate = registered_pages[index];
        if ((candidate == nullptr) || !candidate->nav_enabled) {
            continue;
        }
        if (candidate->id == page) {
            return current_nav_index;
        }
        ++current_nav_index;
    }
    return 0;
}

size_t nav_count() {
    size_t count = 0;
    for (size_t index = 0; index < page_count; ++index) {
        if ((registered_pages[index] != nullptr) && registered_pages[index]->nav_enabled) {
            ++count;
        }
    }
    return count;
}

GuiPageDescriptor *nav_page(size_t wanted_index) {
    size_t current_nav_index = 0;
    for (size_t index = 0; index < page_count; ++index) {
        GuiPageDescriptor *candidate = registered_pages[index];
        if ((candidate == nullptr) || !candidate->nav_enabled) {
            continue;
        }
        if (current_nav_index == wanted_index) {
            return candidate;
        }
        ++current_nav_index;
    }
    return nullptr;
}
}

void gui_page_manager_init() {
    std::memset(registered_pages, 0, sizeof(registered_pages));
    std::memset(history, 0, sizeof(history));
    page_count = 0;
    history_size = 0;
    current_page = nullptr;
    internal_navigation = false;
    transition = {};
    pending_navigation = false;
    pending_pop_history = false;
    dirty = true;
}

bool gui_page_manager_register(GuiPageDescriptor *page) {
    if ((page == nullptr) || (page_count >= MAX_PAGES) || (find_page(page->id) != nullptr)) {
        return false;
    }
    page->initialized = false;
    registered_pages[page_count++] = page;
    return true;
}

bool gui_page_manager_load(UiPage page) {
    return switch_page(find_page(page), false, 1);
}

UiPage gui_page_current() {
    return (current_page == nullptr) ? UiPage::Boot : current_page->id;
}

void gui_page_goto(UiPage page) {
    if (page == UiPage::Boot) {
        return;
    }
    const bool record_history = (current_page != nullptr) && (current_page->id != UiPage::Boot);
    const int8_t direction = page_direction(current_page == nullptr ? UiPage::Boot : current_page->id,
                                            page, 1);
    (void)switch_page(find_page(page), record_history, direction);
}

void gui_page_previous() {
    const size_t count = nav_count();
    if ((count == 0) || (current_page == nullptr) || (current_page->id == UiPage::Boot)) {
        return;
    }
    const int index = nav_index(current_page->id);
    gui_page_goto(nav_page(static_cast<size_t>((index + static_cast<int>(count) - 1) % count))->id);
}

void gui_page_next() {
    const size_t count = nav_count();
    if ((count == 0) || (current_page == nullptr) || (current_page->id == UiPage::Boot)) {
        return;
    }
    const int index = nav_index(current_page->id);
    gui_page_goto(nav_page((static_cast<size_t>(index) + 1) % count)->id);
}

void gui_page_back() {
    if ((current_page == nullptr) || (current_page->id == UiPage::Boot) ||
        (current_page->id == UiPage::Home)) {
        return;
    }

    UiPage target = UiPage::Home;
    if (history_size > 0) {
        target = history[history_size - 1];
    }
    if (transition.active) {
        queue_navigation(target, false, -1);
        pending_pop_history = history_size > 0;
        return;
    }
    if (history_size > 0) --history_size;
    (void)switch_page(find_page(target), false, -1);
}

void gui_page_handle_key(const KeyEvent &event) {
    if ((current_page == nullptr) || (current_page->id == UiPage::Boot)) {
        return;
    }

    if (transition.active) {
        // Keep navigation responsive without interrupting the current slide.
        // The public navigation helpers collapse this to the last requested page.
        if ((event.id == KeyId::Left) && (event.gesture == KeyGesture::Click)) {
            gui_page_previous();
        } else if ((event.id == KeyId::Right) && (event.gesture == KeyGesture::Click)) {
            gui_page_next();
        } else if ((event.id == KeyId::Right) && (event.gesture == KeyGesture::LongPress) &&
                   !internal_navigation) {
            gui_page_back();
        }
        return;
    }

    if (ui_poetry_popup_is_visible() || ui_system_popup_is_blocking()) {
        ui_poetry_popup_dismiss();
        ui_system_popup_dismiss_immediate();
        dirty = true;
        return;
    }

    // Home handles playback shortcuts while it is outside internal navigation.
    if ((current_page->id == UiPage::Home) && !internal_navigation &&
        (event.gesture != KeyGesture::Click) &&
        (current_page->key_consume != nullptr)) {
        if (current_page->key_consume(event)) dirty = true;
        return;
    }

    if ((event.id == KeyId::Right) && (event.gesture == KeyGesture::LongPress)) {
        if (internal_navigation) {
            // Let nested page views consume Back before leaving page navigation.
            if ((current_page->key_consume != nullptr) && current_page->key_consume(event)) {
                dirty = true;
                return;
            }
            internal_navigation = false;
            if (current_page->navigation_changed != nullptr) {
                current_page->navigation_changed(false);
            }
            dirty = true;
        } else {
            gui_page_back();
        }
        return;
    }

    if (!internal_navigation) {
        if (event.gesture != KeyGesture::Click) {
            return;
        }
        if (event.id == KeyId::Middle) {
            if (current_page->id == UiPage::Home) {
                return;
            }
            internal_navigation = true;
            if (current_page->navigation_changed != nullptr) {
                current_page->navigation_changed(true);
            }
            dirty = true;
        } else if (event.id == KeyId::Left) {
            gui_page_previous();
        } else if (event.id == KeyId::Right) {
            gui_page_next();
        }
        return;
    }

    if ((current_page->key_consume != nullptr) && current_page->key_consume(event)) {
        dirty = true;
    }
}

void gui_page_update_status(const PlayerStatus &status) {
    for (size_t index = 0; index < page_count; ++index) {
        GuiPageDescriptor *page = registered_pages[index];
        if ((page != nullptr) && (page->update_status != nullptr)) {
            const bool changed = page->update_status(status);
            if (changed && (page == current_page)) {
                dirty = true;
            }
        }
    }
}

void gui_page_service() {
    ui_popups_service(gui_page_current());

    if (transition.active &&
        !egui_animation_is_running(EGUI_ANIM_OF(&transition.animation))) {
        GuiPageDescriptor *outgoing = transition.outgoing;
        GuiPageDescriptor *incoming = transition.incoming;
        transition.active = false;
        transition.outgoing = nullptr;
        transition.incoming = nullptr;
        if (outgoing != nullptr && outgoing->view != nullptr) {
            egui_view_set_position(outgoing->view, 0, 0);
            egui_view_set_visible(outgoing->view, 0);
            egui_view_invalidate_full(outgoing->view);
        }
        if (incoming != nullptr && incoming->view != nullptr) {
            egui_view_set_position(incoming->view, 0, 0);
            egui_view_set_visible(incoming->view, 1);
            egui_view_invalidate_full(incoming->view);
        }
        dirty = true;

        if (pending_navigation) {
            const UiPage next_page = pending_page;
            const bool record_history = pending_record_history;
            const int8_t direction = pending_direction;
            const bool pop_history = pending_pop_history;
            pending_navigation = false;
            pending_pop_history = false;
            if (pop_history && (history_size > 0)) --history_size;
            (void)switch_page(find_page(next_page), record_history, direction);
        }
    }

    if ((current_page != nullptr) && (current_page->service != nullptr) &&
        current_page->service()) {
        dirty = true;
    }
}

void gui_page_render(bool force) {
    if ((current_page == nullptr) || (!dirty && !force)) {
        return;
    }
    egui_view_invalidate_full(current_page->view);
    dirty = false;
}

const char *gui_page_name(UiPage page) {
    GuiPageDescriptor *descriptor = find_page(page);
    return (descriptor == nullptr) ? "unknown" : descriptor->name;
}
