#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "app/key_types.h"
#include "app/player_types.h"
#include "app/system_notify.h"
#include "task/weather_sync_task.h"

constexpr EventBits_t HW_EVENT_DISPLAY_READY = BIT0;
constexpr EventBits_t HW_EVENT_SD_READY = BIT1;
constexpr EventBits_t HW_EVENT_CODEC_READY = BIT2;
constexpr EventBits_t HW_EVENT_LIBRARY_READY = BIT3;
constexpr EventBits_t HW_EVENT_INIT_DONE = BIT4;

extern QueueHandle_t KeyEventQueue;
extern QueueHandle_t GuiKeyQueue;
extern QueueHandle_t PlayerCommandQueue;
extern QueueHandle_t PlayerStatusQueue;
extern SemaphoreHandle_t GuiWakeSemaphore;
extern SemaphoreHandle_t MusicLibraryMutex;
extern SemaphoreHandle_t PlayerStatusMutex;
extern SemaphoreHandle_t AppDataMutex;
extern EventGroupHandle_t HardwareEventGroup;

void task_system_init();
bool task_post_player_command(PlayerCommandType type, int16_t value = 0,
                              bool show_feedback = false);
bool task_post_player_audio_settings(const AudioSettings &settings, bool persist,
                                     bool restart_sleep_timer = false);
void task_publish_player_status(const PlayerStatus &status);
