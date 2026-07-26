#include "task/task_system.h"

#include <Arduino.h>

#include "app/app_data.h"
#include "app/music_library.h"
#include "app/player_app.h"
#include "task/user_AppDataTask.h"
#include "task/user_KeyManllegeTask.h"
#include "task/user_KeyTask.h"

QueueHandle_t KeyEventQueue = nullptr;
QueueHandle_t GuiKeyQueue = nullptr;
QueueHandle_t PlayerCommandQueue = nullptr;
QueueHandle_t PlayerStatusQueue = nullptr;
SemaphoreHandle_t GuiWakeSemaphore = nullptr;
SemaphoreHandle_t MusicLibraryMutex = nullptr;
SemaphoreHandle_t PlayerStatusMutex = nullptr;
SemaphoreHandle_t AppDataMutex = nullptr;
EventGroupHandle_t HardwareEventGroup = nullptr;

void hardware_init_task(void *parameter);
void player_task(void *parameter);
void gui_task(void *parameter);

namespace {
void require_handle(const void *handle, const char *name) {
    if (handle == nullptr) {
        Serial.printf("[FATAL] failed to create %s\n", name);
        abort();
    }
}

void require_task(BaseType_t result, const char *name) {
    if (result != pdPASS) {
        Serial.printf("[FATAL] failed to create task %s\n", name);
        abort();
    }
}
}

void task_system_init() {
    KeyEventQueue = xQueueCreate(16, sizeof(KeyEvent));
    GuiKeyQueue = xQueueCreate(16, sizeof(KeyEvent));
    PlayerCommandQueue = xQueueCreate(12, sizeof(PlayerCommand));
    PlayerStatusQueue = xQueueCreate(1, sizeof(PlayerStatus));
    GuiWakeSemaphore = xSemaphoreCreateBinary();
    MusicLibraryMutex = xSemaphoreCreateMutex();
    PlayerStatusMutex = xSemaphoreCreateMutex();
    AppDataMutex = xSemaphoreCreateMutex();
    HardwareEventGroup = xEventGroupCreate();

    require_handle(KeyEventQueue, "KeyEventQueue");
    require_handle(GuiKeyQueue, "GuiKeyQueue");
    require_handle(PlayerCommandQueue, "PlayerCommandQueue");
    require_handle(PlayerStatusQueue, "PlayerStatusQueue");
    require_handle(GuiWakeSemaphore, "GuiWakeSemaphore");
    require_handle(MusicLibraryMutex, "MusicLibraryMutex");
    require_handle(PlayerStatusMutex, "PlayerStatusMutex");
    require_handle(AppDataMutex, "AppDataMutex");
    require_handle(HardwareEventGroup, "HardwareEventGroup");

    music_library_attach_mutex(MusicLibraryMutex);
    player_app_attach_mutex(PlayerStatusMutex);
    app_data_attach_mutex(AppDataMutex);

    require_task(xTaskCreate(hardware_init_task, "HardwareInit", 4096, nullptr, 5, nullptr),
                 "HardwareInit");
    require_task(xTaskCreate(user_KeyTask, "KeyTask", 3072, nullptr, 3, nullptr),
                 "KeyTask");
    require_task(xTaskCreate(user_KeyManllegeTask, "KeyMgrTask", 3072, nullptr, 3, nullptr),
                 "KeyMgrTask");
    require_task(xTaskCreatePinnedToCore(player_task, "Player", 8192, nullptr, 4, nullptr, 1),
                 "Player");
    require_task(xTaskCreate(user_AppDataTask, "AppData", 3072, nullptr, 2, nullptr),
                 "AppData");
    require_task(xTaskCreatePinnedToCore(gui_task, "Gui", 6144, nullptr, 2, nullptr, 0),
                 "Gui");
}

bool task_post_player_command(PlayerCommandType type, int16_t value) {
    const PlayerCommand command{type, value};
    const bool posted = (PlayerCommandQueue != nullptr) &&
                        (xQueueSend(PlayerCommandQueue, &command, pdMS_TO_TICKS(20)) == pdTRUE);
    if (!posted) {
        Serial.println("[PLAYER] command queue full");
    }
    return posted;
}

void task_publish_player_status(const PlayerStatus &status) {
    player_app_set_status(status);
    if (PlayerStatusQueue != nullptr) {
        xQueueOverwrite(PlayerStatusQueue, &status);
    }
    if (GuiWakeSemaphore != nullptr) {
        xSemaphoreGive(GuiWakeSemaphore);
    }
}
