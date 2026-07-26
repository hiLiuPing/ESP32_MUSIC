#include "app/app_data.h"

#include <ctime>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
SemaphoreHandle_t snapshot_mutex = nullptr;
AppDataSnapshot current_snapshot = {};
AppTime injected_time = {};
HomeWeatherData injected_weather = {};
WeatherForecastDay injected_forecast[APP_WEATHER_FORECAST_DAYS] = {};
uint32_t injected_forecast_version = 0U;
HomeEnvironmentData injected_environment = {};
HomeBatteryData injected_battery = {};
uint32_t version = 0;
bool external_time_source = false;

bool lock_snapshot(TickType_t timeout) {
    return snapshot_mutex != nullptr &&
           xSemaphoreTake(snapshot_mutex, timeout) == pdTRUE;
}

void unlock_snapshot() {
    if (snapshot_mutex != nullptr) {
        xSemaphoreGive(snapshot_mutex);
    }
}

bool read_system_time(AppTime *out) {
    if (out == nullptr) {
        return false;
    }
    const time_t now = std::time(nullptr);
    struct tm local = {};
    if ((now < 1577836800) || (localtime_r(&now, &local) == nullptr) ||
        (local.tm_year + 1900 < 2020)) {
        return false;
    }
    out->year = static_cast<uint16_t>(local.tm_year + 1900);
    out->month = static_cast<uint8_t>(local.tm_mon + 1);
    out->day = static_cast<uint8_t>(local.tm_mday);
    out->hour = static_cast<uint8_t>(local.tm_hour);
    out->minute = static_cast<uint8_t>(local.tm_min);
    out->second = static_cast<uint8_t>(local.tm_sec);
    out->valid = true;
    out->stale = false;
    return true;
}

bool time_is_valid(const AppTime &time) {
    return time.valid && time.year >= 2020 && time.month >= 1 && time.month <= 12 &&
           time.day >= 1 && time.day <= 31 && time.hour < 24 &&
           time.minute < 60 && time.second < 60;
}

uint8_t weekday(const AppTime &time) {
    struct tm value = {};
    value.tm_year = static_cast<int>(time.year) - 1900;
    value.tm_mon = static_cast<int>(time.month) - 1;
    value.tm_mday = time.day;
    value.tm_isdst = -1;
    (void)mktime(&value);
    return static_cast<uint8_t>(value.tm_wday);
}

// Howard Hinnant's civil-from-days conversion, anchored at the Unix epoch.
void civil_from_days(int64_t days, uint16_t *year, uint8_t *month, uint8_t *day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);              // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                 // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153;                                     // [0, 11]
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;                             // [1, 31]
    const unsigned m = (mp < 10) ? (mp + 3) : (mp - 9);                         // [1, 12]
    *year = static_cast<uint16_t>(y + (m <= 2));
    *month = static_cast<uint8_t>(m);
    *day = static_cast<uint8_t>(d);
}

void format_snapshot(AppDataSnapshot *snapshot) {
    static constexpr const char *const week[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
    if (snapshot == nullptr) {
        return;
    }
    std::memset(snapshot->time_text, 0, sizeof(snapshot->time_text));
    std::memset(snapshot->date_text, 0, sizeof(snapshot->date_text));
    std::memset(snapshot->week_text, 0, sizeof(snapshot->week_text));
    std::memset(snapshot->temp_range_text, 0, sizeof(snapshot->temp_range_text));
    std::memset(snapshot->pm25_text, 0, sizeof(snapshot->pm25_text));
    std::memset(snapshot->env_text, 0, sizeof(snapshot->env_text));

    if (snapshot->time.valid) {
        std::snprintf(snapshot->time_text, sizeof(snapshot->time_text), "%02u:%02u",
                      snapshot->time.hour, snapshot->time.minute);
        std::snprintf(snapshot->date_text, sizeof(snapshot->date_text), "%u\346\234\210%u\346\227\245",
                      snapshot->time.month, snapshot->time.day);
        std::snprintf(snapshot->week_text, sizeof(snapshot->week_text), "%s",
                      week[weekday(snapshot->time)]);
    } else {
        std::snprintf(snapshot->time_text, sizeof(snapshot->time_text), "--:--");
        std::snprintf(snapshot->date_text, sizeof(snapshot->date_text), "---- -- --");
        std::snprintf(snapshot->week_text, sizeof(snapshot->week_text), "---");
    }

    if (snapshot->weather.valid) {
        std::snprintf(snapshot->temp_range_text, sizeof(snapshot->temp_range_text),
                      "%s%d/%dC", snapshot->weather.stale ? "*" : "",
                      snapshot->weather.low_c, snapshot->weather.high_c);
        std::snprintf(snapshot->pm25_text, sizeof(snapshot->pm25_text),
                      "%sPM2.5 %d", snapshot->weather.stale ? "*" : "",
                      snapshot->weather.pm25);
    } else {
        std::snprintf(snapshot->temp_range_text, sizeof(snapshot->temp_range_text), "--");
        std::snprintf(snapshot->pm25_text, sizeof(snapshot->pm25_text), "PM2.5 --");
    }
    if (snapshot->environment.valid) {
        std::snprintf(snapshot->env_text, sizeof(snapshot->env_text),
                      "%sT %d.%dC H %u%%", snapshot->environment.stale ? "*" : "",
                      snapshot->environment.temperature_x10 / 10,
                      std::abs(snapshot->environment.temperature_x10 % 10),
                      snapshot->environment.humidity);
    } else {
        std::snprintf(snapshot->env_text, sizeof(snapshot->env_text), "T --.-C H --%%");
    }
    snapshot->is_day = !snapshot->time.valid ||
                       (snapshot->time.hour >= 6 && snapshot->time.hour < 19);
}

void rebuild_locked() {
    current_snapshot.time = injected_time;
    current_snapshot.time.valid = time_is_valid(injected_time);
    current_snapshot.weather = injected_weather;
    std::memcpy(current_snapshot.forecast, injected_forecast,
                sizeof(current_snapshot.forecast));
    current_snapshot.forecast_version = injected_forecast_version;
    current_snapshot.environment = injected_environment;
    current_snapshot.battery = injected_battery;
    if (!current_snapshot.weather.valid) {
        current_snapshot.weather.icon_id = 499;
        current_snapshot.weather.scene = WEATHER_SCENE_UNKNOWN;
    } else if (current_snapshot.weather.icon_id == 0) {
        current_snapshot.weather.icon_id = 499;
    }
    if (current_snapshot.battery.percent > 100U) {
        current_snapshot.battery.percent = 100U;
    }
    current_snapshot.environment_temp_x10 = current_snapshot.environment.temperature_x10;
    current_snapshot.environment_humidity = current_snapshot.environment.humidity;
    current_snapshot.weather_icon_id = current_snapshot.weather.icon_id;
    current_snapshot.weather_scene = static_cast<uint8_t>(current_snapshot.weather.scene);
    current_snapshot.battery_percent = current_snapshot.battery.percent;
    current_snapshot.charging = current_snapshot.battery.valid &&
                                current_snapshot.battery.charging;
    current_snapshot.charge_full = current_snapshot.battery.valid &&
                                   current_snapshot.battery.full;
    current_snapshot.environment_valid = current_snapshot.environment.valid;
    current_snapshot.environment_stale = current_snapshot.environment.stale;
    current_snapshot.battery_valid = current_snapshot.battery.valid;
    current_snapshot.battery_stale = current_snapshot.battery.stale;
    current_snapshot.weather_valid = current_snapshot.weather.valid;
    current_snapshot.weather_stale = current_snapshot.weather.stale;
    current_snapshot.version = ++version;
    format_snapshot(&current_snapshot);
}
}

void app_data_attach_mutex(SemaphoreHandle_t mutex) {
    snapshot_mutex = mutex;
    if (lock_snapshot(portMAX_DELAY)) {
        injected_weather.icon_id = 499;
        injected_weather.scene = WEATHER_SCENE_UNKNOWN;
        if (injected_forecast_version == 0U) {
            injected_forecast_version = 1U;
        }
        rebuild_locked();
        unlock_snapshot();
    }
}

void app_data_set_snapshot(const AppDataSnapshot &snapshot) {
    if (lock_snapshot(portMAX_DELAY)) {
        current_snapshot = snapshot;
        injected_time = snapshot.time;
        injected_weather = snapshot.weather;
        std::memcpy(injected_forecast, snapshot.forecast, sizeof(injected_forecast));
        injected_forecast_version = snapshot.forecast_version;
        injected_environment = snapshot.environment;
        injected_battery = snapshot.battery;
        external_time_source = true;
        version = snapshot.version;
        rebuild_locked();
        unlock_snapshot();
    }
}

bool app_data_get_snapshot(AppDataSnapshot *snapshot) {
    if ((snapshot == nullptr) || !lock_snapshot(pdMS_TO_TICKS(20))) {
        return false;
    }
    *snapshot = current_snapshot;
    unlock_snapshot();
    return true;
}

void app_data_set_time(const AppTime &time) {
    if (lock_snapshot(portMAX_DELAY)) {
        injected_time = time;
        external_time_source = true;
        rebuild_locked();
        unlock_snapshot();
    }
}

void app_data_set_weather(const HomeWeatherData &weather) {
    if (lock_snapshot(portMAX_DELAY)) {
        injected_weather = weather;
        rebuild_locked();
        unlock_snapshot();
    }
}

void app_data_set_weather_forecast(const WeatherForecastDay *days, uint8_t count,
                                   uint32_t forecast_version, bool stale) {
    if ((days == nullptr) || (count == 0U)) {
        return;
    }
    if (count > APP_WEATHER_FORECAST_DAYS) {
        count = APP_WEATHER_FORECAST_DAYS;
    }
    if (lock_snapshot(portMAX_DELAY)) {
        std::memcpy(injected_forecast, days,
                    static_cast<size_t>(count) * sizeof(WeatherForecastDay));
        for (uint8_t i = 0U; i < count; ++i) {
            injected_forecast[i].stale = stale;
            injected_forecast[i].valid = true;
        }
        for (uint8_t i = count; i < APP_WEATHER_FORECAST_DAYS; ++i) {
            injected_forecast[i] = {};
        }
        injected_forecast_version = forecast_version == 0U ? injected_forecast_version + 1U
                                                            : forecast_version;
        rebuild_locked();
        unlock_snapshot();
    }
}

bool app_data_get_weather_forecast(WeatherForecastDay *days, uint8_t count,
                                   uint32_t *forecast_version) {
    if ((days == nullptr) || (count == 0U) || !lock_snapshot(pdMS_TO_TICKS(20))) {
        return false;
    }
    if (count > APP_WEATHER_FORECAST_DAYS) {
        count = APP_WEATHER_FORECAST_DAYS;
    }
    std::memcpy(days, current_snapshot.forecast,
                static_cast<size_t>(count) * sizeof(WeatherForecastDay));
    if (forecast_version != nullptr) {
        *forecast_version = current_snapshot.forecast_version;
    }
    unlock_snapshot();
    return true;
}

void app_data_set_environment(const HomeEnvironmentData &environment) {
    if (lock_snapshot(portMAX_DELAY)) {
        injected_environment = environment;
        rebuild_locked();
        unlock_snapshot();
    }
}

void app_data_set_battery(const HomeBatteryData &battery) {
    if (lock_snapshot(portMAX_DELAY)) {
        injected_battery = battery;
        rebuild_locked();
        unlock_snapshot();
    }
}

void app_data_update_runtime(uint32_t uptime_ms, uint32_t hardware_bits) {
    if (lock_snapshot(portMAX_DELAY)) {
        current_snapshot.uptime_ms = uptime_ms;
        current_snapshot.hardware_bits = hardware_bits;
        rebuild_locked();
        unlock_snapshot();
    }
}

void app_data_update_temporary_home_data(uint32_t uptime_ms,
                                         uint32_t hardware_bits) {
    if (!lock_snapshot(portMAX_DELAY)) {
        return;
    }

    const uint32_t day_seconds = 24U * 60U * 60U;
    const uint64_t total_seconds = (10U * 60U * 60U) + (8U * 60U) +
                                   (uptime_ms / 1000U);
    const int64_t elapsed_days = static_cast<int64_t>(total_seconds / day_seconds);
    const uint32_t seconds_today = static_cast<uint32_t>(total_seconds % day_seconds);
    AppTime temporary_time = {};
    temporary_time.version = injected_time.version + 1U;
    civil_from_days(20660 + elapsed_days, &temporary_time.year,
                    &temporary_time.month, &temporary_time.day);
    temporary_time.hour = static_cast<uint8_t>(seconds_today / 3600U);
    temporary_time.minute = static_cast<uint8_t>((seconds_today % 3600U) / 60U);
    temporary_time.second = static_cast<uint8_t>(seconds_today % 60U);
    temporary_time.valid = true;
    temporary_time.stale = false;

    injected_time = temporary_time;
    injected_weather.version++;
    injected_weather.high_c = 32;
    injected_weather.low_c = 24;
    injected_weather.pm25 = 18;
    injected_weather.icon_id = 100;
    injected_weather.scene = WEATHER_SCENE_CLEAR;
    injected_weather.valid = true;
    injected_weather.stale = false;

    injected_environment.version++;
    injected_environment.temperature_x10 = 265;
    injected_environment.humidity = 55;
    injected_environment.valid = true;
    injected_environment.stale = false;

    injected_battery.version++;
    injected_battery.percent = 76;
    injected_battery.charging = false;
    injected_battery.full = false;
    injected_battery.valid = true;
    injected_battery.stale = false;

    static constexpr int16_t highs[APP_WEATHER_FORECAST_DAYS] = {32, 33, 31, 30, 29, 31, 32};
    static constexpr int16_t lows[APP_WEATHER_FORECAST_DAYS] = {24, 25, 23, 22, 21, 23, 24};
    static constexpr uint16_t icons[APP_WEATHER_FORECAST_DAYS] = {100, 101, 101, 305, 305, 306, 100};
    for (uint8_t i = 0U; i < APP_WEATHER_FORECAST_DAYS; ++i) {
        WeatherForecastDay &day = injected_forecast[i];
        day.version = injected_forecast_version + 1U;
        day.year = temporary_time.year;
        day.month = temporary_time.month;
        day.day = static_cast<uint8_t>(temporary_time.day + i);
        day.high_c = highs[i];
        day.low_c = lows[i];
        day.icon_id = icons[i];
        day.scene = (i == 3U || i == 4U) ? WEATHER_SCENE_LIGHT_RAIN : WEATHER_SCENE_CLEAR;
        day.valid = true;
        day.stale = false;
    }
    injected_forecast_version++;

    current_snapshot.uptime_ms = uptime_ms;
    current_snapshot.hardware_bits = hardware_bits;
    external_time_source = true;
    rebuild_locked();
    unlock_snapshot();
}

void DataApp_HomeStatus_Update() {
    AppTime system_time = {};
    const bool system_time_valid = read_system_time(&system_time);
    if (lock_snapshot(portMAX_DELAY)) {
        if (!external_time_source && system_time_valid) {
            system_time.version = injected_time.version + 1U;
            injected_time = system_time;
        } else if (!external_time_source && !system_time_valid) {
            injected_time = {};
        }
        rebuild_locked();
        unlock_snapshot();
    }
}

void DataApp_HomeStatus_Get(DataApp_HomeStatus_t *out) {
    (void)app_data_get_snapshot(out);
}

void Time_Get(app_time_t *out) {
    if (out == nullptr) {
        return;
    }
    AppDataSnapshot snapshot = {};
    if (app_data_get_snapshot(&snapshot)) {
        *out = snapshot.time;
    }
}

uint8_t Time_GetColon() {
    AppTime time = {};
    Time_Get(&time);
    return static_cast<uint8_t>(time.valid && ((time.second & 1U) == 0U));
}

uint8_t Time_IsDaytime() {
    AppTime time = {};
    Time_Get(&time);
    return static_cast<uint8_t>(!time.valid || (time.hour >= 6 && time.hour < 19));
}
