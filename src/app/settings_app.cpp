#include "app/settings_app.h"

#include <Preferences.h>
#include <cstring>

namespace {
Preferences prefs;
AppSettings current = {};
bool initialized = false;

AppSettings defaults() {
    return AppSettings{20U, 30U, 30U, 5U, 480U, 1U, 1U, 0U};
}

void normalize(AppSettings &s) {
    s.poetry_enabled = s.poetry_enabled ? 1U : 0U;
    s.home_theme = (s.home_theme == 2U) ? 2U : 1U;
    s.weather_sync_enabled = s.weather_sync_enabled ? 1U : 0U;
    if (s.poetry_interval_min != 0U) s.poetry_interval_min = constrain(s.poetry_interval_min, 5U, 60U);
    s.poetry_duration_s = constrain(s.poetry_duration_s, 5U, 300U);
    s.weather_interval_min = constrain(s.weather_interval_min, 30U, 180U);
    if (s.screen_idle_min != 0U) s.screen_idle_min = constrain(s.screen_idle_min, 1U, 360U);
    if (s.auto_off_min != 0U) s.auto_off_min = constrain(s.auto_off_min, 30U, 480U);
}

void persist() {
    prefs.putUShort("pint", current.poetry_interval_min);
    prefs.putUShort("pdur", current.poetry_duration_s);
    prefs.putUShort("wint", current.weather_interval_min);
    prefs.putUShort("idle", current.screen_idle_min);
    prefs.putUShort("off", current.auto_off_min);
    prefs.putUChar("pen", current.poetry_enabled);
    prefs.putUChar("theme", current.home_theme);
    prefs.putUChar("wen", current.weather_sync_enabled);
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
    current.auto_off_min = prefs.getUShort("off", current.auto_off_min);
    current.poetry_enabled = prefs.getUChar("pen", current.poetry_enabled);
    current.home_theme = prefs.getUChar("theme", current.home_theme);
    current.weather_sync_enabled = prefs.getUChar("wen", current.weather_sync_enabled);
    normalize(current);
    persist();
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
    if (memcmp(&next, &current, sizeof(next)) == 0) return false;
    current = next;
    persist();
    return true;
}

void settings_app_reset_defaults() {
    settings_app_init();
    current = defaults();
    persist();
}
