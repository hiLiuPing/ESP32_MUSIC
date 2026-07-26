#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

enum class SystemNotifyType : uint8_t {
    Info, Warning, Error, Storage, Audio, Player
};

struct SystemNotifyMessage {
    SystemNotifyType type;
    int16_t value;
    char text[48];
};

void system_notify_init();
bool system_notify_post(SystemNotifyType type, const char *text, int16_t value = 0);
bool system_notify_try_receive(SystemNotifyMessage *out);
