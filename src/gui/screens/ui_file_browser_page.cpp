#include "gui/screens/ui_file_browser_page.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

#include "app/file_browser.h"
#include "app/system_notify.h"
#include "gui/egui_port.h"
#include "gui/gui_common.h"
#include "gui/page_manager.h"
#include "gui/ui_heiti_font.h"
#include "task/task_system.h"

namespace {
constexpr uint8_t MAX_TREE_NODES = 96U;
constexpr uint8_t VISIBLE_ROWS = 6U;
constexpr int16_t ROW_HEIGHT = 22;

enum class BrowserMode : uint8_t { StorageSelect, Tree, ActionMenu, DeleteConfirm };

struct TreeNode {
    FileBrowserEntry entry;
    uint8_t depth;
    bool expanded;
};

GuiEguiView view;
TreeNode *nodes = nullptr;
uint8_t node_count = 0U;
uint8_t selected = 0U;
uint8_t visible_start = 0U;
uint8_t selected_storage = 0U;
BrowserMode mode = BrowserMode::StorageSelect;
FileBrowserStorage storage = FileBrowserStorage::SdCard;
uint8_t glyph_prefetch_cursor = 0U;
uint8_t glyph_prefetch_window_start = 0xFFU;

const char *storage_name(FileBrowserStorage value) {
    return value == FileBrowserStorage::SdCard ? "SD CARD" : "INTERNAL FLASH";
}

const egui_font_t *file_name_font() {
    return ui_heiti_font_get_cached(16U);
}

size_t utf8_character_length(const char *text) {
    if (text == nullptr || text[0] == '\0') return 0U;
    const uint8_t lead = static_cast<uint8_t>(text[0]);
    if (lead < 0x80U) return 1U;
    const size_t length = (lead & 0xE0U) == 0xC0U ? 2U :
                          (lead & 0xF0U) == 0xE0U ? 3U :
                          (lead & 0xF8U) == 0xF0U ? 4U : 0U;
    if (length == 0U) return 0U;
    for (size_t index = 1U; index < length; ++index) {
        if (text[index] == '\0' ||
            (static_cast<uint8_t>(text[index]) & 0xC0U) != 0x80U) return 0U;
    }
    return length;
}

void copy_utf8_filename_fitted(const char *source, char *destination,
                               size_t capacity, int16_t width) {
    if (destination == nullptr || capacity == 0U) return;
    destination[0] = '\0';
    if (source == nullptr || width <= 0) return;
    size_t used = 0U;
    int16_t used_width = 0;
    while (*source != '\0') {
        const size_t bytes = utf8_character_length(source);
        if (bytes == 0U || used + bytes >= capacity) break;
        const int16_t character_width = bytes == 1U ? 8 : 16;
        if (used_width + character_width > width) break;
        std::memcpy(destination + used, source, bytes);
        used += bytes;
        source += bytes;
        used_width += character_width;
    }
    destination[used] = '\0';
}

void draw_folder(egui_canvas_t *canvas, int16_t x, int16_t y, egui_color_t color) {
    egui_canvas_draw_rectangle(canvas, x, y + 3, 14, 10, 1, color, EGUI_ALPHA_100);
    egui_canvas_draw_rectangle_fill(canvas, x + 1, y + 1, 6, 3, color, EGUI_ALPHA_100);
}

void draw_music(egui_canvas_t *canvas, int16_t x, int16_t y, egui_color_t color) {
    egui_canvas_draw_line(canvas, x + 10, y + 1, x + 10, y + 11, 2, color, EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, x + 10, y + 1, x + 15, y + 3, 2, color, EGUI_ALPHA_100);
    egui_canvas_draw_circle_fill_basic(canvas, x + 5, y + 12, 3, color, EGUI_ALPHA_100);
}

void draw_book(egui_canvas_t *canvas, int16_t x, int16_t y, egui_color_t color) {
    egui_canvas_draw_rectangle(canvas, x, y + 1, 14, 13, 1, color, EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, x + 7, y + 2, x + 7, y + 13, 1, color, EGUI_ALPHA_100);
}

void draw_file(egui_canvas_t *canvas, int16_t x, int16_t y, egui_color_t color) {
    egui_canvas_draw_rectangle(canvas, x + 2, y + 1, 10, 13, 1, color, EGUI_ALPHA_100);
    egui_canvas_draw_line(canvas, x + 4, y + 5, x + 10, y + 5, 1, color, EGUI_ALPHA_100);
}

void draw_icon(egui_canvas_t *canvas, const FileBrowserEntry &entry, int16_t x, int16_t y,
               egui_color_t color) {
    if (entry.directory) draw_folder(canvas, x, y, color);
    else if (file_browser_is_audio(entry.path)) draw_music(canvas, x, y, color);
    else if (file_browser_is_text(entry.path)) draw_book(canvas, x, y, color);
    else draw_file(canvas, x, y, color);
}

void clamp_window() {
    if (node_count == 0U) { selected = 0U; visible_start = 0U; return; }
    if (selected >= node_count) selected = node_count - 1U;
    if (selected < visible_start) visible_start = selected;
    if (selected >= visible_start + VISIBLE_ROWS) visible_start = selected - VISIBLE_ROWS + 1U;
}

void clear_tree() {
    node_count = 0U;
    selected = 0U;
    visible_start = 0U;
    glyph_prefetch_cursor = 0U;
    glyph_prefetch_window_start = 0xFFU;
}

void reset_glyph_prefetch() {
    glyph_prefetch_window_start = visible_start;
    glyph_prefetch_cursor = visible_start;
}

bool load_root(FileBrowserStorage target) {
    clear_tree();
    FileBrowserEntry entries[FILE_BROWSER_LIST_LIMIT] = {};
    const size_t count = file_browser_list(target, "/", entries, FILE_BROWSER_LIST_LIMIT);
    for (size_t i = 0U; i < count && node_count < MAX_TREE_NODES; ++i) {
        nodes[node_count].entry = entries[i];
        nodes[node_count].depth = 0U;
        ++node_count;
    }
    reset_glyph_prefetch();
    return file_browser_storage_available(target);
}

void remove_descendants(uint8_t parent) {
    const uint8_t parent_depth = nodes[parent].depth;
    uint8_t end = parent + 1U;
    while (end < node_count && nodes[end].depth > parent_depth) ++end;
    const uint8_t removed = end - (parent + 1U);
    if (removed == 0U) return;
    std::memmove(&nodes[parent + 1U], &nodes[end],
                 (node_count - end) * sizeof(TreeNode));
    node_count -= removed;
}

void toggle_directory(uint8_t index) {
    if (index >= node_count || !nodes[index].entry.directory) return;
    if (nodes[index].expanded) {
        remove_descendants(index);
        nodes[index].expanded = false;
        clamp_window();
        reset_glyph_prefetch();
        return;
    }
    FileBrowserEntry entries[FILE_BROWSER_LIST_LIMIT] = {};
    const size_t count = file_browser_list(storage, nodes[index].entry.path, entries,
                                           FILE_BROWSER_LIST_LIMIT);
    const uint8_t available = MAX_TREE_NODES - node_count;
    const uint8_t inserted = static_cast<uint8_t>(count < available ? count : available);
    if (inserted > 0U) {
        std::memmove(&nodes[index + 1U + inserted], &nodes[index + 1U],
                     (node_count - index - 1U) * sizeof(TreeNode));
        for (uint8_t i = 0U; i < inserted; ++i) {
            nodes[index + 1U + i] = {};
            nodes[index + 1U + i].entry = entries[i];
            nodes[index + 1U + i].depth = nodes[index].depth + 1U;
        }
        node_count += inserted;
    }
    nodes[index].expanded = true;
    clamp_window();
    reset_glyph_prefetch();
}

void refresh_parent() {
    if (node_count == 0U) return;
    uint8_t parent = selected;
    while (parent > 0U && nodes[parent].depth > 0U) --parent;
    load_root(storage);
}

void draw_tree(egui_canvas_t *canvas) {
    gui_draw_header(canvas, storage_name(storage));
    if (node_count == 0U) {
        gui_draw_text(canvas, 12, 50, "EMPTY DIRECTORY");
        return;
    }
    for (uint8_t row = 0U; row < VISIBLE_ROWS && visible_start + row < node_count; ++row) {
        const uint8_t index = visible_start + row;
        const TreeNode &node = nodes[index];
        const int16_t y = static_cast<int16_t>(27 + row * ROW_HEIGHT);
        const bool focused = index == selected;
        if (focused) {
            egui_canvas_draw_round_rectangle_fill(canvas, 3, y - 2, 378, 20, 3,
                                                  EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        }
        const egui_color_t color = focused ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
        const int16_t indent = static_cast<int16_t>(8 + node.depth * 14);
        if (node.depth > 0U) {
            egui_canvas_draw_line(canvas, indent - 6, y + 7, indent, y + 7, 1, color,
                                  EGUI_ALPHA_100);
        }
        draw_icon(canvas, node.entry, indent, y, color);
        char name[FILE_BROWSER_NAME_LENGTH] = {};
        copy_utf8_filename_fitted(node.entry.name, name, sizeof(name),
                                  static_cast<int16_t>(172 - node.depth * 14));
        egui_canvas_draw_text(canvas, file_name_font(), name, indent + 20, y,
                              color, EGUI_ALPHA_100);
        if (!node.entry.directory) {
            char details[32] = {};
            const double mb = static_cast<double>(node.entry.size_bytes) / (1024.0 * 1024.0);
            tm time_info = {};
            if (node.entry.modified > 946684800 && localtime_r(&node.entry.modified, &time_info)) {
                std::snprintf(details, sizeof(details), "%.2f MB %04d-%02d-%02d", mb,
                              time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday);
            } else {
                std::snprintf(details, sizeof(details), "%.2f MB ---- -- --", mb);
            }
            egui_region_t region = {{210, static_cast<int16_t>(y - 1)}, {166, 18}};
            egui_canvas_draw_text_in_rect(canvas, reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT), details, &region,
                                          EGUI_ALIGN_RIGHT | EGUI_ALIGN_VCENTER, color,
                                          EGUI_ALPHA_100);
        }
    }
}

void draw(egui_canvas_t *canvas) {
    gui_draw_page_background(canvas);
    if (mode == BrowserMode::StorageSelect) {
        gui_draw_header(canvas, "FILE BROWSER");
        for (uint8_t i = 0U; i < 2U; ++i) {
            const FileBrowserStorage item = i == 0U ? FileBrowserStorage::SdCard :
                                                     FileBrowserStorage::InternalFlash;
            const bool focused = i == selected_storage;
            const int16_t y = static_cast<int16_t>(46 + i * 38);
            if (focused) egui_canvas_draw_round_rectangle_fill(canvas, 12, y - 5, 360, 28, 3,
                                                                EGUI_COLOR_BLACK, EGUI_ALPHA_100);
            const egui_color_t color = focused ? EGUI_COLOR_WHITE : EGUI_COLOR_BLACK;
            draw_folder(canvas, 26, y, color);
            egui_canvas_draw_text(canvas, reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT), storage_name(item), 52, y,
                                  color, EGUI_ALPHA_100);
            egui_canvas_draw_text(canvas, reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT),
                                  file_browser_storage_available(item) ? "READY" : "UNAVAILABLE",
                                  260, y, color, EGUI_ALPHA_100);
        }
        return;
    }
    draw_tree(canvas);
    if (mode == BrowserMode::ActionMenu || mode == BrowserMode::DeleteConfirm) {
        const int16_t y = 126;
        egui_canvas_draw_round_rectangle_fill(canvas, 12, y, 360, 32, 3,
                                              EGUI_COLOR_WHITE, EGUI_ALPHA_100);
        egui_canvas_draw_round_rectangle(canvas, 12, y, 360, 32, 3, 1,
                                         EGUI_COLOR_BLACK, EGUI_ALPHA_100);
        const char *text = mode == BrowserMode::ActionMenu ? "DELETE       OPEN       BACK" :
                                                             "CANCEL      DELETE      BACK";
        egui_region_t region = {{18, y + 2}, {348, 28}};
        egui_canvas_draw_text_in_rect(canvas, reinterpret_cast<const egui_font_t *>(EGUI_CONFIG_FONT_DEFAULT), text, &region,
                                      EGUI_ALIGN_CENTER | EGUI_ALIGN_VCENTER,
                                      EGUI_COLOR_BLACK, EGUI_ALPHA_100);
    }
}

void init() {
    if (nodes == nullptr) {
        nodes = static_cast<TreeNode *>(
            heap_caps_calloc(MAX_TREE_NODES, sizeof(TreeNode),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (nodes == nullptr) {
            (void)system_notify_post(SystemNotifyType::Error,
                                     "FILE BROWSER MEMORY FAILED");
        }
    }
    gui_egui_view_init(&view, egui_port_core(), draw);
}
void enter() { mode = BrowserMode::StorageSelect; selected_storage = 0U; clear_tree(); }
void exit() { mode = BrowserMode::StorageSelect; }
void navigation_changed(bool) {}

bool key_consume(const KeyEvent &event) {
    if (event.id == KeyId::Right && event.gesture == KeyGesture::LongPress) {
        if (mode == BrowserMode::StorageSelect) {
            gui_page_back();
            return true;
        }
        mode = BrowserMode::StorageSelect;
        return true;
    }
    if (event.gesture != KeyGesture::Click) return false;
    if (mode == BrowserMode::StorageSelect) {
        if (event.id == KeyId::Left || event.id == KeyId::Right) {
            selected_storage = selected_storage == 0U ? 1U : 0U;
            return true;
        }
        if (event.id == KeyId::Middle) {
            storage = selected_storage == 0U ? FileBrowserStorage::SdCard :
                                              FileBrowserStorage::InternalFlash;
            if (!load_root(storage)) {
                (void)system_notify_post(SystemNotifyType::Storage, "STORAGE UNAVAILABLE");
            } else {
                mode = BrowserMode::Tree;
            }
            return true;
        }
        return false;
    }
    if (mode == BrowserMode::ActionMenu) {
        if (event.id == KeyId::Left) mode = BrowserMode::DeleteConfirm;
        else if (event.id == KeyId::Middle) {
            const FileBrowserEntry &entry = nodes[selected].entry;
            if (storage == FileBrowserStorage::SdCard && file_browser_is_audio(entry.path)) {
                (void)task_post_player_path(entry.path, true);
                mode = BrowserMode::Tree;
            } else {
                (void)system_notify_post(SystemNotifyType::Info,
                                         file_browser_is_text(entry.path) ? "TXT NOT SUPPORTED" :
                                                                           "FILE NOT SUPPORTED");
                mode = BrowserMode::Tree;
            }
        } else if (event.id == KeyId::Right) mode = BrowserMode::Tree;
        return true;
    }
    if (mode == BrowserMode::DeleteConfirm) {
        if (event.id == KeyId::Middle) {
            const bool deleted = file_browser_delete(storage, nodes[selected].entry.path);
            (void)system_notify_post(deleted ? SystemNotifyType::Info : SystemNotifyType::Error,
                                     deleted ? "FILE DELETED" : "DELETE FAILED");
            refresh_parent();
            mode = BrowserMode::Tree;
        } else if (event.id == KeyId::Left || event.id == KeyId::Right) {
            mode = BrowserMode::Tree;
        }
        return true;
    }
    if (event.id == KeyId::Left || event.id == KeyId::Right) {
        if (node_count > 0U) {
            selected = event.id == KeyId::Left
                           ? static_cast<uint8_t>((selected + node_count - 1U) % node_count)
                           : static_cast<uint8_t>((selected + 1U) % node_count);
            clamp_window();
            reset_glyph_prefetch();
        }
        return true;
    }
    if (event.id == KeyId::Middle && node_count > 0U) {
        if (nodes[selected].entry.directory) toggle_directory(selected);
        else mode = BrowserMode::ActionMenu;
        return true;
    }
    return false;
}
bool service() {
    if (mode != BrowserMode::Tree || node_count == 0U) return false;
    if (glyph_prefetch_window_start != visible_start) {
        glyph_prefetch_window_start = visible_start;
        glyph_prefetch_cursor = visible_start;
    }
    const uint8_t end = static_cast<uint8_t>(
        std::min<uint16_t>(node_count, static_cast<uint16_t>(visible_start + VISIBLE_ROWS)));
    if (glyph_prefetch_cursor >= end) return false;
    const bool cached = ui_heiti_font_cache_text(16U, nodes[glyph_prefetch_cursor].entry.name);
    ++glyph_prefetch_cursor;
    return cached;
}
bool update_status(const PlayerStatus &) { return false; }
GuiPageDescriptor descriptor = {UiPage::FileBrowser, init, enter, exit, key_consume, service,
                                update_status, EGUI_VIEW_OF(&view), "file-browser", false,
                                false, navigation_changed};
}

GuiPageDescriptor &ui_file_browser_page_descriptor() { return descriptor; }
