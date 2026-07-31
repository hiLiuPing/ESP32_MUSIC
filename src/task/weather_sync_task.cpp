#include "task/weather_sync_task.h"

#include <algorithm>

#include "app/app_data.h"
#include "app/sensor_service.h"
#include "app/settings_app.h"
#include "app/system_notify.h"
#include "app/weather_network.h"
#include "app/weather_service.h"
#include "task/task_system.h"

TaskHandle_t WeatherSyncTaskHandle = nullptr;

namespace {
constexpr uint8_t MAX_RETRIES = 5U;
constexpr uint32_t RETRY_DELAY_MS = 1000U;
TimerHandle_t sync_timer = nullptr;

void sync_timer_callback(TimerHandle_t timer) {
    (void)timer;
    if (WeatherSyncTaskHandle != nullptr) {
        xTaskNotify(WeatherSyncTaskHandle, WEATHER_SYNC_NOW, eSetBits);
    }
}

template <typename Callable>
bool retry_call(const char *name, Callable callable) {
    Serial.printf("[WEATHER] %s start (max=%u)\n", name, MAX_RETRIES);
    for (uint8_t attempt = 0U; attempt < MAX_RETRIES; ++attempt) {
        if (callable()) {
            Serial.printf("[WEATHER] %s success attempt=%u\n", name,
                          static_cast<unsigned>(attempt + 1U));
            return true;
        }
        Serial.printf("[WEATHER] %s attempt=%u failed\n", name,
                      static_cast<unsigned>(attempt + 1U));
        if (attempt + 1U < MAX_RETRIES) vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    }
    Serial.printf("[WEATHER] %s failed after %u attempts\n", name, MAX_RETRIES);
    return false;
}

void notify_gui() {
    if (GuiWakeSemaphore != nullptr) xSemaphoreGive(GuiWakeSemaphore);
}

void stop_timer() {
    if (sync_timer != nullptr) xTimerStop(sync_timer, 0);
}

void arm_timer(uint16_t interval_min) {
    if (sync_timer == nullptr) return;
    xTimerChangePeriod(sync_timer, pdMS_TO_TICKS(static_cast<uint32_t>(interval_min) * 60000UL), 0);
    xTimerStart(sync_timer, 0);
}

void mark_sync_failure(bool ntp_ok, bool weather_ok) {
    if (!ntp_ok) app_data_mark_time_stale();
    if (!weather_ok) app_data_mark_weather_stale();
    notify_gui();
    system_notify_post(SystemNotifyType::Warning, "WEATHER SYNC FAILED");
}

bool perform_sync() {
    Serial.println("[WEATHER] sync begin");
    if (!weather_network_has_profiles()) {
        Serial.println("[WEATHER] sync skipped: no saved WiFi profile");
        weather_network_disconnect();
        mark_sync_failure(false, false);
        return false;
    }
    if (!retry_call("WiFi", []() { return weather_network_connect(10000U); })) {
        weather_network_disconnect();
        mark_sync_failure(false, false);
        return false;
    }

    AppTime network_time = {};
    const bool ntp_ok = retry_call("NTP", [&network_time]() {
        return weather_service_sync_time(&network_time);
    });
    bool rtc_ok = false;
    if (ntp_ok) {
        AppTime verified_time = {};
        rtc_ok = sensor_service_write_rtc(network_time, &verified_time);
        if (rtc_ok) {
            app_data_set_time(verified_time);
            app_data_mark_time_fresh();
            Serial.println("[WEATHER] NTP time written to PCF8563");
        } else {
            network_time.stale = true;
            app_data_set_time(network_time);
            app_data_mark_time_stale();
            Serial.println("[WEATHER] PCF8563 write/readback failed");
        }
        DataApp_HomeStatus_Update();
    } else {
        app_data_mark_time_stale();
    }
    const bool time_ok = ntp_ok && rtc_ok;

    WeatherNetworkProfile profile = {};
    bool location_ok = weather_network_get_active(&profile);
    Serial.printf("[WEATHER] active profile=%d ssid=%s city=%s location=%s lat=%s lon=%s\n",
                  location_ok ? 1 : 0, profile.ssid.c_str(), profile.city.c_str(),
                  profile.location.c_str(), profile.lat.c_str(), profile.lon.c_str());
    if (location_ok && profile.location.isEmpty()) {
        location_ok = retry_call("City ID", [&profile]() {
            return weather_service_resolve_location(&profile);
        });
    }

    AppDataSnapshot snapshot = {};
    (void)app_data_get_snapshot(&snapshot);
    HomeWeatherData weather = snapshot.weather;

    const bool now_ok = location_ok && retry_call("Current weather", [&]() {
        HomeWeatherData candidate = weather;
        if (!weather_service_query_now(&candidate)) return false;
        weather = candidate;
        return true;
    });
    WeatherServiceForecast forecast = {};
    const bool forecast_ok = location_ok && retry_call("Forecast", [&]() {
        return weather_service_query_forecast(&forecast);
    });
    const bool air_ok = location_ok && retry_call("Air quality", [&]() {
        HomeWeatherData candidate = weather;
        if (!weather_service_query_air(&candidate)) return false;
        weather = candidate;
        return true;
    });

    Serial.printf("[WEATHER] stage summary ntp=%d rtc=%d city=%d current=%d forecast=%d air=%d\n",
                  ntp_ok ? 1 : 0, rtc_ok ? 1 : 0, location_ok ? 1 : 0, now_ok ? 1 : 0,
                  forecast_ok ? 1 : 0, air_ok ? 1 : 0);

    if (forecast_ok) {
        weather.high_c = forecast.days[0].high_c;
        weather.low_c = forecast.days[0].low_c;
        app_data_set_weather_forecast(forecast.days, APP_WEATHER_FORECAST_DAYS, 0U,
                                      !(now_ok && air_ok));
        WeatherForecastDay stored[APP_WEATHER_FORECAST_DAYS] = {};
        uint32_t stored_version = 0U;
        if (app_data_get_weather_forecast(stored, APP_WEATHER_FORECAST_DAYS,
                                           &stored_version)) {
            Serial.printf("[WEATHER] forecast stored version=%lu day0=%04u-%02u-%02u icon=%u high=%d low=%d\n",
                          static_cast<unsigned long>(stored_version),
                          static_cast<unsigned>(stored[0].year),
                          static_cast<unsigned>(stored[0].month),
                          static_cast<unsigned>(stored[0].day),
                          static_cast<unsigned>(stored[0].icon_id),
                          static_cast<int>(stored[0].high_c),
                          static_cast<int>(stored[0].low_c));
        } else {
            Serial.println("[WEATHER] forecast store readback failed");
        }
    }
    const bool weather_ok = now_ok || forecast_ok || air_ok;
    if (weather_ok) {
        weather.valid = true;
        weather.stale = !(now_ok && forecast_ok && air_ok);
        weather.version = snapshot.weather.version + 1U;
        app_data_set_weather(weather);
    } else {
        app_data_mark_weather_stale();
    }
    weather_network_disconnect();
    if (!time_ok || !location_ok || !now_ok || !forecast_ok || !air_ok) {
        Serial.println("[WEATHER] sync completed with failures");
        mark_sync_failure(time_ok, weather_ok);
        return false;
    }
    notify_gui();
    Serial.println("[WEATHER] sync completed successfully");
    Serial.println("[WEATHER] network sync complete");
    return true;
}

void show_ap_notice() {
    if (weather_network_start_ap()) {
        const String message = weather_network_ap_message();
        system_notify_post(SystemNotifyType::Info, message.c_str());
    } else {
        system_notify_post(SystemNotifyType::Error, "WEATHER AP START FAILED");
    }
}
}


bool weather_sync_request(uint32_t request) {
    return WeatherSyncTaskHandle != nullptr &&
           xTaskNotify(WeatherSyncTaskHandle, request, eSetBits) == pdPASS;
}

bool weather_sync_is_provisioning() {
    return weather_network_ap_active();
}

void weather_sync_task(void *parameter) {
    (void)parameter;
    (void)xEventGroupWaitBits(HardwareEventGroup, HW_EVENT_INIT_DONE,
                              pdFALSE, pdTRUE, portMAX_DELAY);
    weather_network_init();
    sync_timer = xTimerCreate("WeatherSyncTimer", pdMS_TO_TICKS(60000U), pdFALSE, nullptr,
                             sync_timer_callback);
    if (sync_timer == nullptr) {
        system_notify_post(SystemNotifyType::Error, "WEATHER TIMER FAILED");
    }

    AppSettings settings = settings_app_get();
    uint32_t pending = settings.weather_interval_min != 0U ? WEATHER_SYNC_NOW : 0U;
    bool force_sync = false;
    for (;;) {
        uint32_t request = 0U;
        if (xTaskNotifyWait(0U, UINT32_MAX, &request, pdMS_TO_TICKS(100U)) == pdTRUE) {
            pending |= request;
        }
        if (weather_network_ap_active()) {
            weather_network_process_ap();
            if (weather_network_ap_has_new_config()) {
                weather_network_stop_ap();
                pending |= WEATHER_SYNC_NOW;
                force_sync = true;
            }
        }
        if ((pending & WEATHER_SYNC_START_AP) != 0U) {
            pending &= ~WEATHER_SYNC_START_AP;
            show_ap_notice();
        }
        if ((pending & WEATHER_SYNC_STOP_AP) != 0U) {
            pending &= ~WEATHER_SYNC_STOP_AP;
            weather_network_stop_ap();
        }
        if ((pending & WEATHER_SYNC_SETTINGS_CHANGED) != 0U) {
            pending &= ~WEATHER_SYNC_SETTINGS_CHANGED;
            settings = settings_app_get();
            if (settings.weather_interval_min == 0U) {
                stop_timer();
                if (!force_sync) weather_network_stop_ap();
            } else {
                pending |= WEATHER_SYNC_NOW;
            }
        }
        settings = settings_app_get();
        const bool due = (pending & WEATHER_SYNC_NOW) != 0U;
        if (due && !weather_network_ap_active() && (settings.weather_interval_min != 0U || force_sync)) {
            if (!weather_network_has_profiles()) {
                pending &= ~WEATHER_SYNC_NOW;
                show_ap_notice();
                continue;
            }
            pending &= ~WEATHER_SYNC_NOW;
            const bool success = perform_sync();
            force_sync = false;
            settings = settings_app_get();
            if (settings.weather_interval_min != 0U) arm_timer(settings.weather_interval_min);
            else stop_timer();
        } else if (due && settings.weather_interval_min == 0U && !force_sync) {
            pending &= ~WEATHER_SYNC_NOW;
            stop_timer();
        }
    }
}
