#include "app/system_notify.h"

#include <cstring>

namespace {
QueueHandle_t queue = nullptr;
SemaphoreHandle_t wake_semaphore = nullptr;
}

void system_notify_init() {
    if (queue == nullptr) queue = xQueueCreate(8, sizeof(SystemNotifyMessage));
}

void system_notify_attach_wake_semaphore(SemaphoreHandle_t semaphore) {
    wake_semaphore = semaphore;
}

bool system_notify_post(SystemNotifyType type, const char *text, int16_t value) {
    system_notify_init();
    if (queue == nullptr) return false;
    SystemNotifyMessage message = {};
    message.type = type;
    message.value = value;
    if (text != nullptr) std::strncpy(message.text, text, sizeof(message.text) - 1U);
    const bool posted = xQueueSend(queue, &message, 0) == pdTRUE;
    if (posted && wake_semaphore != nullptr) xSemaphoreGive(wake_semaphore);
    return posted;
}

bool system_notify_try_receive(SystemNotifyMessage *out) {
    system_notify_init();
    return out != nullptr && queue != nullptr && xQueueReceive(queue, out, 0) == pdTRUE;
}
