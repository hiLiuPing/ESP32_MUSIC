#pragma once

#include <Arduino.h>

#include "app/key_types.h"
#include "app/player_types.h"
#include "gui/egui_port.h"

enum class UiPage : uint8_t {
    Boot,
    Home,
    Music,
    Read,
    Setting,
};

struct GuiPageDescriptor {
    UiPage id;
    void (*init)();
    void (*enter)();
    void (*exit)();
    bool (*key_consume)(const KeyEvent &event);
    bool (*service)();
    bool (*update_status)(const PlayerStatus &status);
    egui_view_t *view;
    const char *name;
    bool nav_enabled;
    bool initialized;
};
