#include "app/weather_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstring>

#include "ArduinoZlib.h"
#include "DuduUtil.h"

namespace {
constexpr char API_HOST[] = "m87jpe3mh2.re.qweatherapi.com";
constexpr char CITY_PATH[] = "/geo/v2/city/lookup";
constexpr char NOW_PATH[] = "/v7/weather/now";
constexpr char FORECAST_PATH[] = "/v7/weather/7d";
constexpr char AIR_PATH[] = "/airquality/v1/current/";
constexpr char NTP_1[] = "ntp5.aliyun.com";
constexpr char NTP_2[] = "ntp5.ict.ac.cn";
constexpr char NTP_3[] = "ntp5.ntsc.ac.cn";
constexpr uint32_t HTTP_TIMEOUT_MS = 6000U;

// Kept private to the weather client so credentials are not part of the public API.
char private_key[] = "MC4CAQAwBQYDK2VwBCIEIPGcuf94j/gW3FRCN27EABzYDpdSnEoSFL6g/17M3EjM";
char public_key[] = "MCowBQYDK2VwAyEAtYrdA4gPF3a/swak2lUCNZlyz3wPw/NtgW4MD1hHmHM=";
String key_id = "CDB5H9RMRH";
String project_id = "25KX2B9QKN";

String url_encode(const String &value) {
    String encoded;
    for (size_t i = 0U; i < value.length(); ++i) {
        const char c = value[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char hex[4] = {};
            std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            encoded += hex;
        }
    }
    return encoded;
}

String jwt() {
    return generateJWT(private_key, public_key, key_id, project_id);
}

bool http_json(const String &url, String *body) {
    if (body == nullptr) return false;
    HTTPClient client;
    client.setConnectTimeout(HTTP_TIMEOUT_MS);
    client.setTimeout(HTTP_TIMEOUT_MS);
    if (!client.begin(url)) return false;
    client.addHeader("Authorization", "Bearer " + jwt());
    // QWeather can return gzip; identity keeps the common path small, while the
    // fallback below still accepts a compressed response from a proxy.
    client.addHeader("Accept-Encoding", "identity");
    const int code = client.GET();
    if (code != HTTP_CODE_OK) {
        client.end();
        return false;
    }
    *body = client.getString();
    client.end();
    if (body->length() >= 2U && static_cast<uint8_t>((*body)[0]) == 0x1FU &&
        static_cast<uint8_t>((*body)[1]) == 0x8BU) {
        const uint32_t output_capacity = 24577U;
        uint8_t *output = static_cast<uint8_t *>(malloc(output_capacity));
        if (output == nullptr) return false;
        uint32_t output_size = 0U;
        const int32_t result = ArduinoZlib::libmpq__decompress_zlib(
            reinterpret_cast<uint8_t *>(body->begin()), body->length(), output,
            output_capacity, output_size);
        if (result < 0 || output_size == 0U) {
            free(output);
            return false;
        }
        output[output_size] = '\0';
        *body = reinterpret_cast<const char *>(output);
        free(output);
    }
    return true;
}

bool parse_json(const String &body, JsonDocument *document) {
    if (document == nullptr) return false;
    if (deserializeJson(*document, body) != DeserializationError::Ok) return false;
    const char *code = (*document)["code"] | "";
    return std::strcmp(code, "200") == 0;
}

WeatherScene_t scene_for_icon(uint16_t icon) {
    if (icon == 100U || icon == 150U) return WEATHER_SCENE_CLEAR;
    if ((icon >= 101U && icon <= 104U) || (icon >= 151U && icon <= 154U)) {
        return WEATHER_SCENE_CLOUDY;
    }
    if ((icon >= 400U && icon <= 499U) || icon == 456U || icon == 457U) {
        return WEATHER_SCENE_SNOW;
    }
    if ((icon >= 307U && icon <= 318U) || icon == 301U || icon == 303U) {
        return WEATHER_SCENE_HEAVY_RAIN;
    }
    if (icon >= 306U && icon <= 310U) return WEATHER_SCENE_MODERATE_RAIN;
    if ((icon >= 300U && icon <= 305U) || icon == 350U || icon == 351U || icon == 399U) {
        return WEATHER_SCENE_LIGHT_RAIN;
    }
    return WEATHER_SCENE_UNKNOWN;
}

bool parse_date(const char *text, uint16_t *year, uint8_t *month, uint8_t *day) {
    unsigned int y = 0U, m = 0U, d = 0U;
    if (text == nullptr || std::sscanf(text, "%u-%u-%u", &y, &m, &d) != 3) return false;
    if (y < 2020U || m < 1U || m > 12U || d < 1U || d > 31U) return false;
    *year = static_cast<uint16_t>(y);
    *month = static_cast<uint8_t>(m);
    *day = static_cast<uint8_t>(d);
    return true;
}
}

bool weather_service_sync_time() {
    configTime(8 * 3600, 0, NTP_1, NTP_2, NTP_3);
    struct tm local = {};
    return getLocalTime(&local, HTTP_TIMEOUT_MS);
}

bool weather_service_resolve_location(WeatherNetworkProfile *profile) {
    if (profile == nullptr || profile->city.isEmpty()) return false;
    if (!profile->location.isEmpty() && !profile->lat.isEmpty() && !profile->lon.isEmpty()) return true;
    const String url = String("https://") + API_HOST + CITY_PATH + "?location=" +
                       url_encode(profile->city) + "&adm=" + url_encode(profile->adm);
    String body;
    JsonDocument document;
    if (!http_json(url, &body) || !parse_json(body, &document)) return false;
    JsonObject location = document["location"][0];
    const char *id = location["id"] | "";
    const char *lat = location["lat"] | "";
    const char *lon = location["lon"] | "";
    if (*id == '\0' || *lat == '\0' || *lon == '\0') return false;
    profile->location = id;
    profile->lat = lat;
    profile->lon = lon;
    profile->city = location["name"] | profile->city;
    weather_network_update_active_location(profile->location, profile->lat, profile->lon,
                                            profile->city);
    return true;
}

bool weather_service_query_now(uint16_t *icon_id, WeatherScene_t *scene) {
    WeatherNetworkProfile profile = {};
    if (icon_id == nullptr || scene == nullptr || !weather_network_get_active(&profile) ||
        profile.location.isEmpty()) return false;
    String body;
    JsonDocument document;
    const String url = String("https://") + API_HOST + NOW_PATH + "?location=" + profile.location;
    if (!http_json(url, &body) || !parse_json(body, &document)) return false;
    const uint16_t icon = document["now"]["icon"] | 0U;
    *icon_id = icon;
    *scene = scene_for_icon(icon);
    return icon != 0U;
}

bool weather_service_query_forecast(WeatherServiceForecast *forecast) {
    WeatherNetworkProfile profile = {};
    if (forecast == nullptr || !weather_network_get_active(&profile) || profile.location.isEmpty()) return false;
    String body;
    JsonDocument document;
    const String url = String("https://") + API_HOST + FORECAST_PATH + "?location=" + profile.location;
    if (!http_json(url, &body) || !parse_json(body, &document)) return false;
    for (uint8_t i = 0U; i < APP_WEATHER_FORECAST_DAYS; ++i) {
        JsonObject source = document["daily"][i];
        WeatherForecastDay &day = forecast->days[i];
        day = {};
        day.icon_id = source["iconDay"] | 0U;
        day.high_c = source["tempMax"] | 0;
        day.low_c = source["tempMin"] | 0;
        if (!parse_date(source["fxDate"] | "", &day.year, &day.month, &day.day) ||
            day.icon_id == 0U) return false;
        day.scene = scene_for_icon(day.icon_id);
        day.valid = true;
    }
    return true;
}

bool weather_service_query_air(int16_t *pm25) {
    WeatherNetworkProfile profile = {};
    if (pm25 == nullptr || !weather_network_get_active(&profile) ||
        profile.lat.isEmpty() || profile.lon.isEmpty()) return false;
    String body;
    JsonDocument document;
    const String url = String("https://") + API_HOST + AIR_PATH + profile.lat + "/" + profile.lon;
    if (!http_json(url, &body) || deserializeJson(document, body) != DeserializationError::Ok) return false;
    JsonArray pollutants = document["pollutants"].as<JsonArray>();
    for (JsonObject pollutant : pollutants) {
        if (std::strcmp(pollutant["code"] | "", "pm2p5") == 0) {
            *pm25 = pollutant["concentration"]["value"] | -1;
            return *pm25 >= 0;
        }
    }
    return false;
}
