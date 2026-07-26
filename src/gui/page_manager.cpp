#include "gui/page_manager.h"

#include <Arduino.h>
#include <cstring>

#include "gui/egui_port.h"

namespace {
constexpr size_t MAX_PAGES = 5;
constexpr size_t HISTORY_DEPTH = 8;

GuiPageDescriptor *registered_pages[MAX_PAGES] = {};
size_t page_count = 0;
GuiPageDescriptor *current_page = nullptr;
UiPage history[HISTORY_DEPTH] = {};
size_t history_size = 0;
bool dirty = true;

GuiPageDescriptor *find_page(UiPage id) {
    for (size_t index = 0; index < page_count; ++index) {
        if ((registered_pages[index] != nullptr) && (registered_pages[index]->id == id)) {
            return registered_pages[index];
        }
    }
    return nullptr;
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

bool switch_page(GuiPageDescriptor *target, bool record_history) {
    if ((target == nullptr) || (target == current_page)) {
        return false;
    }

    ensure_initialized(target);
    if (record_history && (current_page != nullptr) && (current_page->id != UiPage::Boot)) {
        push_history(current_page->id);
    }

    if ((current_page != nullptr) && (current_page->exit != nullptr)) {
        current_page->exit();
    }
    if ((current_page != nullptr) && (current_page->view != nullptr)) {
        egui_view_set_visible(current_page->view, 0);
    }
    current_page = target;
    if (current_page->id == UiPage::Home) {
        history_size = 0;
    }
    if (current_page->enter != nullptr) {
        current_page->enter();
    }
    egui_view_set_visible(current_page->view, 1);
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
    return switch_page(find_page(page), false);
}

UiPage gui_page_current() {
    return (current_page == nullptr) ? UiPage::Boot : current_page->id;
}

void gui_page_goto(UiPage page) {
    if (page == UiPage::Boot) {
        return;
    }
    const bool record_history = (current_page != nullptr) && (current_page->id != UiPage::Boot);
    (void)switch_page(find_page(page), record_history);
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
        target = history[--history_size];
    }
    (void)switch_page(find_page(target), false);
}

void gui_page_handle_key(const KeyEvent &event) {
    if ((current_page == nullptr) || (current_page->id == UiPage::Boot)) {
        return;
    }

    if ((event.id == KeyId::Right) && (event.gesture == KeyGesture::LongPress)) {
        gui_page_back();
        return;
    }

    if ((current_page->key_consume != nullptr) && current_page->key_consume(event)) {
        dirty = true;
        return;
    }

    if (event.gesture != KeyGesture::Click) {
        return;
    }
    if (event.id == KeyId::Left) {
        gui_page_previous();
    } else if (event.id == KeyId::Right) {
        gui_page_next();
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
