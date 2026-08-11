#include "gui/ui.h"

#include <Arduino.h>

#include "gui/page_manager.h"
#include "gui/screens/ui_boot_page.h"
#include "gui/screens/ui_home_page.h"
#include "gui/screens/ui_music_page.h"
#include "gui/screens/ui_setting_page.h"
#include "gui/screens/ui_poetry_page.h"
#include "gui/ui_popups.h"
#include "gui/ui_heiti_font.h"
#include "app/settings_app.h"

namespace {
constexpr const char *MENU_GLYPHS =
    "音频设置编辑音频音量扬声器低音高音环绕定时关闭开启分钟分贝最小最大中键保存"
    "设置编辑设置诗词弹窗展示时长天气同步立即同步无线网络文件管理屏幕休眠关闭秒执行等待"
    "启动停止中键同步中键网络中键文件";
}

void gui_init() {
    gui_page_manager_init();

    GuiPageDescriptor *pages[] = {
        &ui_boot_page_descriptor(),
        &ui_home_page_descriptor(),
        &ui_music_page_descriptor(),
        &ui_poetry_page_descriptor(),
        &ui_setting_page_descriptor(),
    };
    for (GuiPageDescriptor *page : pages) {
        if (!gui_page_manager_register(page)) {
            Serial.printf("[GUI] failed to register page %s\n", page->name);
            abort();
        }
    }
    if (!gui_page_manager_load(UiPage::Boot)) {
        Serial.println("[GUI] failed to load boot page");
        abort();
    }
    settings_app_init();
    ui_popups_init();
    (void)ui_heiti_font_cache_text(16U, MENU_GLYPHS);
}
