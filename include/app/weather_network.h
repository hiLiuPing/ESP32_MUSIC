#pragma once

#include <Arduino.h>

#include <cstdint>

constexpr uint8_t WEATHER_MAX_WIFI_PROFILES = 5U;

struct WeatherNetworkProfile {
    bool used;
    int8_t slot;
    uint32_t order;
    String ssid;
    String pass;
    String city;
    String adm;
    String location;
    String lat;
    String lon;
};

void weather_network_init();
bool weather_network_has_profiles();
bool weather_network_get_active(WeatherNetworkProfile *profile);
bool weather_network_connect(uint32_t timeout_ms);
void weather_network_disconnect();
void weather_network_update_active_location(const String &location, const String &lat,
                                            const String &lon, const String &city);
bool weather_network_save_profile(const String &ssid, const String &pass,
                                  const String &city, const String &adm);

bool weather_network_start_ap();
void weather_network_stop_ap();
void weather_network_process_ap();
bool weather_network_ap_active();
bool weather_network_ap_has_new_config();
String weather_network_ap_message();
