#include "gui/page_manager.h"

#include <cstring>

#include "gui/lv_port.h"
#include "gui/ui_popups.h"

namespace {
constexpr size_t kMaxPages = 6;
constexpr size_t kHistoryDepth = 8;
constexpr int16_t kScreenWidth = 384;

GuiPageDescriptor *s_pages[kMaxPages] = {};
UiPage s_history[kHistoryDepth] = {};
size_t s_page_count = 0;
size_t s_history_count = 0;
GuiPageDescriptor *s_current = nullptr;
lv_obj_t *s_stage = nullptr;
bool s_internal_navigation = false;

GuiPageDescriptor *find(UiPage page) {
    for (size_t i = 0; i < s_page_count; ++i) {
        if (s_pages[i] != nullptr && s_pages[i]->id == page) return s_pages[i];
    }
    return nullptr;
}

void make_root(GuiPageDescriptor *page) {
    if (page == nullptr || page->initialized) return;
    if (page->init != nullptr) page->init();
    if (page->view == nullptr) return;
    lv_obj_set_size(page->view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(page->view, 0, 0);
    lv_obj_set_style_pad_all(page->view, 0, 0);
    lv_obj_set_style_radius(page->view, 0, 0);
    lv_obj_add_flag(page->view, LV_OBJ_FLAG_HIDDEN);
    page->initialized = true;
}

void push_history(UiPage page) {
    if (page == UiPage::Boot || page == UiPage::Home) return;
    if (s_history_count < kHistoryDepth) s_history[s_history_count++] = page;
    else {
        std::memmove(s_history, s_history + 1, (kHistoryDepth - 1) * sizeof(s_history[0]));
        s_history[kHistoryDepth - 1] = page;
    }
}

void animate_x(void *object, int32_t value) {
    lv_obj_set_x(static_cast<lv_obj_t *>(object), value);
}

void hide_after_animation(lv_anim_t *animation) {
    lv_obj_t *page = static_cast<lv_obj_t *>(animation->var);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(page, 0);
}

bool show_page(GuiPageDescriptor *target, bool record_history, int direction) {
    if (target == nullptr || target == s_current) return false;
    make_root(target);
    if (target->view == nullptr) return false;

    ui_poetry_popup_dismiss();
    ui_system_popup_dismiss_immediate();
    if (record_history && s_current != nullptr) push_history(s_current->id);
    if (s_current != nullptr && s_current->exit != nullptr) s_current->exit();

    GuiPageDescriptor *old = s_current;
    s_current = target;
    if (target->id == UiPage::Home) s_history_count = 0;
    if (target->enter != nullptr) target->enter();
    if (target->navigation_changed != nullptr) target->navigation_changed(false);
    s_internal_navigation = false;

    lv_obj_clear_flag(target->view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(target->view);
    if (old == nullptr) {
        lv_obj_set_x(target->view, 0);
        lv_obj_invalidate(target->view);
        return true;
    }

    const int16_t signed_width = direction >= 0 ? kScreenWidth : -kScreenWidth;
    lv_obj_set_x(target->view, signed_width);
    lv_anim_t incoming;
    lv_anim_init(&incoming);
    lv_anim_set_var(&incoming, target->view);
    lv_anim_set_values(&incoming, signed_width, 0);
    lv_anim_set_time(&incoming, 180);
    lv_anim_set_exec_cb(&incoming, animate_x);
    lv_anim_set_path_cb(&incoming, lv_anim_path_ease_out);
    lv_anim_start(&incoming);

    lv_anim_t outgoing;
    lv_anim_init(&outgoing);
    lv_anim_set_var(&outgoing, old->view);
    lv_anim_set_values(&outgoing, 0, -signed_width);
    lv_anim_set_time(&outgoing, 180);
    lv_anim_set_exec_cb(&outgoing, animate_x);
    lv_anim_set_path_cb(&outgoing, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&outgoing, hide_after_animation);
    lv_anim_start(&outgoing);
    return true;
}

int nav_index(UiPage page) {
    int index = 0;
    for (size_t i = 0; i < s_page_count; ++i) {
        if (s_pages[i] == nullptr || !s_pages[i]->nav_enabled) continue;
        if (s_pages[i]->id == page) return index;
        ++index;
    }
    return 0;
}

size_t nav_count() {
    size_t count = 0;
    for (size_t i = 0; i < s_page_count; ++i) if (s_pages[i] != nullptr && s_pages[i]->nav_enabled) ++count;
    return count;
}

GuiPageDescriptor *nav_at(size_t wanted) {
    size_t index = 0;
    for (size_t i = 0; i < s_page_count; ++i) {
        if (s_pages[i] == nullptr || !s_pages[i]->nav_enabled) continue;
        if (index++ == wanted) return s_pages[i];
    }
    return nullptr;
}
}

void gui_page_manager_init() {
    std::memset(s_pages, 0, sizeof(s_pages));
    s_page_count = s_history_count = 0;
    s_current = nullptr;
    s_internal_navigation = false;
    s_stage = lv_screen_active();
    lv_obj_set_style_bg_color(s_stage, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_stage, 0, 0);
}

bool gui_page_manager_register(GuiPageDescriptor *page) {
    if (page == nullptr || s_page_count >= kMaxPages || find(page->id) != nullptr) return false;
    page->initialized = false;
    s_pages[s_page_count++] = page;
    return true;
}

bool gui_page_manager_load(UiPage page) { return show_page(find(page), false, 1); }
UiPage gui_page_current() { return s_current == nullptr ? UiPage::Boot : s_current->id; }
lv_obj_t *gui_page_active_root() { return s_current == nullptr ? s_stage : s_current->view; }

void gui_page_goto(UiPage page) {
    if (page == UiPage::Boot) return;
    const bool remember = s_current != nullptr && s_current->id != UiPage::Boot;
    const int direction = s_current != nullptr && nav_index(page) < nav_index(s_current->id) ? -1 : 1;
    (void)show_page(find(page), remember, direction);
}

void gui_page_previous() {
    const size_t count = nav_count();
    if (count == 0 || s_current == nullptr || s_current->id == UiPage::Boot) return;
    (void)show_page(nav_at((nav_index(s_current->id) + count - 1U) % count), false, -1);
}

void gui_page_next() {
    const size_t count = nav_count();
    if (count == 0 || s_current == nullptr || s_current->id == UiPage::Boot) return;
    (void)show_page(nav_at((nav_index(s_current->id) + 1U) % count), false, 1);
}

void gui_page_back() {
    if (s_current == nullptr || s_current->id == UiPage::Boot || s_current->id == UiPage::Home) return;
    const UiPage target = s_history_count == 0 ? UiPage::Home : s_history[--s_history_count];
    (void)show_page(find(target), false, -1);
}

void gui_page_handle_key(const KeyEvent &event) {
    if (s_current == nullptr || s_current->id == UiPage::Boot) return;
    if (ui_poetry_popup_is_visible() || ui_system_popup_is_blocking()) {
        ui_poetry_popup_dismiss(); ui_system_popup_dismiss_immediate(); return;
    }
    if (event.id == KeyId::Right && event.gesture == KeyGesture::LongPress) {
        if (s_internal_navigation && s_current->key_consume != nullptr && s_current->key_consume(event)) return;
        if (s_internal_navigation) {
            s_internal_navigation = false;
            if (s_current->navigation_changed != nullptr) s_current->navigation_changed(false);
        } else gui_page_back();
        return;
    }
    if (!s_internal_navigation) {
        if (event.gesture != KeyGesture::Click) {
            if (s_current->id == UiPage::Home && s_current->key_consume != nullptr) (void)s_current->key_consume(event);
            return;
        }
        if (event.id == KeyId::Middle && s_current->id != UiPage::Home) {
            s_internal_navigation = true;
            if (s_current->navigation_changed != nullptr) s_current->navigation_changed(true);
        } else if (event.id == KeyId::Left) gui_page_previous();
        else if (event.id == KeyId::Right) gui_page_next();
        else if (s_current->id == UiPage::Home && s_current->key_consume != nullptr) (void)s_current->key_consume(event);
        return;
    }
    if (s_current->key_consume != nullptr) (void)s_current->key_consume(event);
}

void gui_page_update_status(const PlayerStatus &status) {
    for (size_t i = 0; i < s_page_count; ++i) if (s_pages[i] != nullptr && s_pages[i]->update_status != nullptr) (void)s_pages[i]->update_status(status);
}

void gui_page_service() {
    ui_popups_service(gui_page_current());
    if (s_current != nullptr && s_current->service != nullptr) (void)s_current->service();
}

void gui_page_render(bool force) { if (force && s_current != nullptr) lv_obj_invalidate(s_current->view); }
const char *gui_page_name(UiPage page) { GuiPageDescriptor *item = find(page); return item == nullptr ? "unknown" : item->name; }
