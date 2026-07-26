#include "app/system_notify.h"

#include <cstring>

namespace {
QueueHandle_t queue = nullptr;
}

void system_notify_init() {
    if (queue == nullptr) queue = xQueueCreate(8, sizeof(SystemNotifyMessage));
}

bool system_notify_post(SystemNotifyType type, const char *text, int16_t value) {
    system_notify_init();
    if (queue == nullptr) return false;
    SystemNotifyMessage message = {};
    message.type = type;
    message.value = value;
    if (text != nullptr) std::strncpy(message.text, text, sizeof(message.text) - 1U);
    return xQueueSend(queue, &message, 0) == pdTRUE;
}

bool system_notify_try_receive(SystemNotifyMessage *out) {
    system_notify_init();
    return out != nullptr && queue != nullptr && xQueueReceive(queue, out, 0) == pdTRUE;
}
