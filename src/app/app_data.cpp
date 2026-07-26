#include "app/app_data.h"

namespace {
SemaphoreHandle_t snapshot_mutex = nullptr;
AppDataSnapshot current_snapshot = {};
}

void app_data_attach_mutex(SemaphoreHandle_t mutex) {
    snapshot_mutex = mutex;
}

void app_data_set_snapshot(const AppDataSnapshot &snapshot) {
    if ((snapshot_mutex != nullptr) &&
        (xSemaphoreTake(snapshot_mutex, portMAX_DELAY) == pdTRUE)) {
        current_snapshot = snapshot;
        xSemaphoreGive(snapshot_mutex);
    }
}

bool app_data_get_snapshot(AppDataSnapshot *snapshot) {
    if ((snapshot == nullptr) || (snapshot_mutex == nullptr) ||
        (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(20)) != pdTRUE)) {
        return false;
    }
    *snapshot = current_snapshot;
    xSemaphoreGive(snapshot_mutex);
    return true;
}
