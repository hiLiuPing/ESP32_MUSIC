#include "app/settings_app.h"

#include <Preferences.h>

namespace {
Preferences prefs;
AppSettings current = {};
bool initialized = false;

AppSettings defaults() {
    return AppSettings{30U, 30U, 0U, 5U};
}

uint16_t normalize_interval(uint16_t value) {
    if (value == 0U) return 0U;
    value = static_cast<uint16_t>(constrain(value, 30U, 300U));
    value = static_cast<uint16_t>(((value + 5U) / 10U) * 10U);
    return static_cast<uint16_t>(constrain(value, 30U, 300U));
}

void normalize(AppSettings &s) {
    s.poetry_interval_min = normalize_interval(s.poetry_interval_min);
    s.poetry_duration_s = constrain(s.poetry_duration_s, 5U, 300U);
    s.weather_interval_min = normalize_interval(s.weather_interval_min);
    if (s.screen_idle_min != 0U) s.screen_idle_min = constrain(s.screen_idle_min, 1U, 360U);
}

void persist() {
    prefs.putUShort("pint", current.poetry_interval_min);
    prefs.putUShort("pdur", current.poetry_duration_s);
    prefs.putUShort("wint", current.weather_interval_min);
    prefs.putUShort("idle", current.screen_idle_min);
}
}

void settings_app_init() {
    if (initialized) return;
    prefs.begin("s3diandeng", false);
    current = defaults();
    current.poetry_interval_min = prefs.getUShort("pint", current.poetry_interval_min);
    current.poetry_duration_s = prefs.getUShort("pdur", current.poetry_duration_s);
    current.weather_interval_min = prefs.getUShort("wint", current.weather_interval_min);
    current.screen_idle_min = prefs.getUShort("idle", current.screen_idle_min);
    if (prefs.isKey("pen") && prefs.getUChar("pen", 0U) == 0U) {
        current.poetry_interval_min = 0U;
    }
    if (prefs.isKey("wen") && prefs.getUChar("wen", 0U) == 0U) {
        current.weather_interval_min = 0U;
    }
    normalize(current);
    persist();
    prefs.remove("pen");
    prefs.remove("wen");
    prefs.remove("theme");
    prefs.remove("off");
    initialized = true;
}

AppSettings settings_app_get() {
    settings_app_init();
    return current;
}

bool settings_app_update(const AppSettings &settings) {
    settings_app_init();
    AppSettings next = settings;
    normalize(next);
    if (next.poetry_interval_min == current.poetry_interval_min &&
        next.poetry_duration_s == current.poetry_duration_s &&
        next.weather_interval_min == current.weather_interval_min &&
        next.screen_idle_min == current.screen_idle_min) {
        return false;
    }
    current = next;
    persist();
    return true;
}

void settings_app_reset_defaults() {
    settings_app_init();
    current = defaults();
    persist();
}
