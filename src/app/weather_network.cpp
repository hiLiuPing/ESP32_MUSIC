#include "app/weather_network.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include <algorithm>

namespace {
constexpr char PREF_NAMESPACE[] = "clock";
constexpr char AP_SSID[] = "DuduClock";
constexpr uint16_t DNS_PORT = 53U;
const IPAddress AP_IP(192, 168, 1, 1);
const IPAddress AP_GATEWAY(192, 168, 1, 1);
const IPAddress AP_NETMASK(255, 255, 255, 0);

Preferences prefs;
WebServer server(80);
DNSServer dns_server;
WeatherNetworkProfile profiles[WEATHER_MAX_WIFI_PROFILES] = {};
int8_t active_slot = -1;
uint32_t order_sequence = 0U;
bool initialized = false;
volatile bool ap_active = false;
volatile bool ap_new_config = false;
String ap_message;
String scan_options;

void persist_metadata();
void save_profile_to_prefs(const WeatherNetworkProfile &profile);

String key(int slot, const char *field) {
    return "wifi" + String(slot) + "_" + field;
}

void clear_cache() {
    for (uint8_t i = 0U; i < WEATHER_MAX_WIFI_PROFILES; ++i) {
        profiles[i] = {};
        profiles[i].slot = static_cast<int8_t>(i);
    }
    active_slot = -1;
    order_sequence = 0U;
}

void load_cache() {
    clear_cache();
    order_sequence = prefs.getUInt("wifi_order_seq", 0U);
    active_slot = static_cast<int8_t>(prefs.getInt("active_slot", -1));
    for (uint8_t i = 0U; i < WEATHER_MAX_WIFI_PROFILES; ++i) {
        WeatherNetworkProfile &profile = profiles[i];
        profile.used = prefs.getBool(key(i, "used").c_str(), false);
        profile.order = prefs.getUInt(key(i, "order").c_str(), 0U);
        if (!profile.used) continue;
        profile.ssid = prefs.getString(key(i, "ssid").c_str(), "");
        profile.pass = prefs.getString(key(i, "pass").c_str(), "");
        profile.city = prefs.getString(key(i, "city").c_str(), "");
        profile.adm = prefs.getString(key(i, "adm").c_str(), "");
        profile.location = prefs.getString(key(i, "location").c_str(), "");
        profile.lat = prefs.getString(key(i, "lat").c_str(), "");
        profile.lon = prefs.getString(key(i, "lon").c_str(), "");
        if (profile.ssid.isEmpty() || profile.city.isEmpty()) profile = {};
        profile.slot = static_cast<int8_t>(i);
    }
    if (active_slot < 0 || active_slot >= WEATHER_MAX_WIFI_PROFILES ||
        !profiles[active_slot].used) {
        active_slot = -1;
        for (uint8_t i = 0U; i < WEATHER_MAX_WIFI_PROFILES; ++i) {
            if (profiles[i].used &&
                (active_slot < 0 || profiles[i].order > profiles[active_slot].order)) {
                active_slot = static_cast<int8_t>(i);
            }
        }
        prefs.putInt("active_slot", active_slot);
    }
    // Migrate the single-profile keys used by the original firmware.
    if (active_slot < 0) {
        const String legacy_ssid = prefs.getString("ssid", "");
        const String legacy_city = prefs.getString("city", "");
        if (!legacy_ssid.isEmpty() && !legacy_city.isEmpty()) {
            profiles[0].used = true;
            profiles[0].slot = 0;
            profiles[0].order = ++order_sequence;
            profiles[0].ssid = legacy_ssid;
            profiles[0].pass = prefs.getString("pass", "");
            profiles[0].city = legacy_city;
            profiles[0].adm = prefs.getString("adm", "");
            profiles[0].location = prefs.getString("location", "");
            profiles[0].lat = prefs.getString("lat", "");
            profiles[0].lon = prefs.getString("lon", "");
            active_slot = 0;
            save_profile_to_prefs(profiles[0]);
            persist_metadata();
        }
    }
}

void persist_metadata() {
    prefs.putUInt("wifi_order_seq", order_sequence);
    prefs.putInt("active_slot", active_slot);
}

void save_profile_to_prefs(const WeatherNetworkProfile &profile) {
    prefs.putBool(key(profile.slot, "used").c_str(), profile.used);
    prefs.putUInt(key(profile.slot, "order").c_str(), profile.order);
    prefs.putString(key(profile.slot, "ssid").c_str(), profile.ssid);
    prefs.putString(key(profile.slot, "pass").c_str(), profile.pass);
    prefs.putString(key(profile.slot, "city").c_str(), profile.city);
    prefs.putString(key(profile.slot, "adm").c_str(), profile.adm);
    prefs.putString(key(profile.slot, "location").c_str(), profile.location);
    prefs.putString(key(profile.slot, "lat").c_str(), profile.lat);
    prefs.putString(key(profile.slot, "lon").c_str(), profile.lon);
}

int find_ssid(const String &target) {
    for (uint8_t i = 0U; i < WEATHER_MAX_WIFI_PROFILES; ++i) {
        if (profiles[i].used && profiles[i].ssid == target) return i;
    }
    return -1;
}

int find_slot() {
    for (uint8_t i = 0U; i < WEATHER_MAX_WIFI_PROFILES; ++i) {
        if (!profiles[i].used) return i;
    }
    int oldest = 0;
    for (uint8_t i = 1U; i < WEATHER_MAX_WIFI_PROFILES; ++i) {
        if (profiles[i].order < profiles[oldest].order) oldest = i;
    }
    return oldest;
}

String html_escape(const String &value) {
    String out = value;
    out.replace("&", "&amp;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    out.replace("\"", "&quot;");
    return out;
}

void handle_root() {
    String html = F("<!doctype html><html lang='zh'><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>DuduClock WiFi</title><style>body{font:16px sans-serif;max-width:520px;margin:24px auto;padding:0 16px}"
                    "input,button{box-sizing:border-box;width:100%;padding:12px;margin:6px 0}button{background:#111;color:white;border:0}</style>"
                    "<h2>DuduClock WiFi</h2><form method='post' action='/configwifi'>"
                    "<label>WiFi</label><select name='ssid'>");
    html += scan_options;
    html += F("</select><input name='pass' type='password' placeholder='WiFi password' required>"
               "<input name='city' placeholder='City' required><input name='adm' placeholder='Province (optional)'>"
               "<button type='submit'>Save</button></form></html>");
    server.send(200, "text/html", html);
}

void handle_config() {
    const String network_ssid = server.arg("ssid");
    const String network_pass = server.arg("pass");
    const String network_city = server.arg("city");
    const String network_adm = server.arg("adm");
    if (network_ssid.isEmpty() || network_pass.isEmpty() || network_city.isEmpty() ||
        !weather_network_save_profile(network_ssid, network_pass, network_city, network_adm)) {
        server.send(400, "text/plain", "SSID and city are required");
        return;
    }
    ap_new_config = true;
    server.send(200, "text/html", "<meta charset='utf-8'><h2>Saved. Synchronizing...</h2>");
}

void ensure_initialized() {
    if (initialized) return;
    prefs.begin(PREF_NAMESPACE, false);
    load_cache();
    initialized = true;
}
}

void weather_network_init() {
    ensure_initialized();
}

bool weather_network_has_profiles() {
    ensure_initialized();
    for (const WeatherNetworkProfile &profile : profiles) if (profile.used) return true;
    return false;
}

bool weather_network_get_active(WeatherNetworkProfile *profile) {
    ensure_initialized();
    if (profile == nullptr || active_slot < 0) return false;
    *profile = profiles[active_slot];
    return profile->used;
}

bool weather_network_connect(uint32_t timeout_ms) {
    ensure_initialized();
    int order[WEATHER_MAX_WIFI_PROFILES] = {};
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < WEATHER_MAX_WIFI_PROFILES; ++i) if (profiles[i].used) order[count++] = i;
    std::sort(order, order + count, [](int a, int b) { return profiles[a].order > profiles[b].order; });
    for (uint8_t i = 0U; i < count; ++i) {
        const int slot = order[i];
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true, true);
        WiFi.begin(profiles[slot].ssid.c_str(), profiles[slot].pass.c_str());
        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (WiFi.status() == WL_CONNECTED) {
            active_slot = static_cast<int8_t>(slot);
            profiles[slot].order = ++order_sequence;
            save_profile_to_prefs(profiles[slot]);
            persist_metadata();
            Serial.printf("[WEATHER] WiFi connected: %s\n", profiles[slot].ssid.c_str());
            return true;
        }
    }
    return false;
}

void weather_network_disconnect() {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
}

void weather_network_update_active_location(const String &new_location, const String &new_lat,
                                            const String &new_lon, const String &new_city) {
    ensure_initialized();
    if (active_slot < 0) return;
    WeatherNetworkProfile &profile = profiles[active_slot];
    profile.location = new_location;
    profile.lat = new_lat;
    profile.lon = new_lon;
    if (!new_city.isEmpty()) profile.city = new_city;
    save_profile_to_prefs(profile);
}

bool weather_network_save_profile(const String &new_ssid, const String &new_pass,
                                  const String &new_city, const String &new_adm) {
    ensure_initialized();
    if (new_ssid.isEmpty() || new_city.isEmpty()) return false;
    int slot = find_ssid(new_ssid);
    if (slot < 0) slot = find_slot();
    WeatherNetworkProfile &profile = profiles[slot];
    profile.used = true;
    profile.slot = static_cast<int8_t>(slot);
    profile.order = ++order_sequence;
    profile.ssid = new_ssid;
    profile.pass = new_pass;
    profile.city = new_city;
    profile.adm = new_adm;
    profile.location = "";
    profile.lat = "";
    profile.lon = "";
    active_slot = static_cast<int8_t>(slot);
    save_profile_to_prefs(profile);
    persist_metadata();
    return true;
}

bool weather_network_start_ap() {
    ensure_initialized();
    if (ap_active) return true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_NETMASK);
    if (!WiFi.softAP(AP_SSID)) return false;
    const int found = WiFi.scanNetworks(false, true);
    scan_options = "<option value=''>Select network</option>";
    for (int i = 0; i < found; ++i) {
        const String name = WiFi.SSID(i);
        if (!name.isEmpty()) scan_options += "<option value='" + html_escape(name) + "'>" + html_escape(name) + "</option>";
    }
    WiFi.scanDelete();
    server.on("/", HTTP_GET, handle_root);
    server.on("/configwifi", HTTP_POST, handle_config);
    server.onNotFound(handle_root);
    server.begin();
    dns_server.start(DNS_PORT, "*", AP_IP);
    ap_active = true;
    ap_new_config = false;
    ap_message = String("AP ") + AP_SSID + " " + WiFi.softAPIP().toString();
    Serial.printf("[WEATHER] %s\n", ap_message.c_str());
    return true;
}

void weather_network_stop_ap() {
    if (!ap_active) return;
    server.stop();
    dns_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    ap_active = false;
}

void weather_network_process_ap() {
    if (!ap_active) return;
    dns_server.processNextRequest();
    server.handleClient();
}

bool weather_network_ap_active() { return ap_active; }

bool weather_network_ap_has_new_config() {
    const bool result = ap_new_config;
    ap_new_config = false;
    return result;
}

String weather_network_ap_message() { return ap_message; }
