#include "app/weather_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <cstdio>
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
constexpr uint32_t CITY_TIMEOUT_MS = 30000U;

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

bool http_json(const char *stage, const String &url, uint32_t timeout_ms, String *body) {
    if (body == nullptr) {
        Serial.printf("[WEATHER][%s] response buffer is null\n", stage);
        return false;
    }
    Serial.printf("[WEATHER][%s] GET %s timeout=%lu ms\n", stage, url.c_str(),
                  static_cast<unsigned long>(timeout_ms));
    HTTPClient client;
    client.setConnectTimeout(timeout_ms);
    client.setTimeout(timeout_ms);
    if (!client.begin(url)) {
        Serial.printf("[WEATHER][%s] HTTP begin failed\n", stage);
        return false;
    }
    client.addHeader("Authorization", "Bearer " + jwt());
    // QWeather can return gzip; identity keeps the common path small, while the
    // fallback below still accepts a compressed response from a proxy.
    client.addHeader("Accept-Encoding", "identity");
    const int code = client.GET();
    if (code != HTTP_CODE_OK) {
        const String error_body = client.getString();
        Serial.printf("[WEATHER][%s] HTTP=%d error_body=%s\n", stage, code,
                      error_body.substring(0, 160).c_str());
        client.end();
        return false;
    }
    *body = client.getString();
    Serial.printf("[WEATHER][%s] HTTP=200 body_len=%u\n", stage,
                  static_cast<unsigned>(body->length()));
    client.end();
    const bool gzip = body->length() >= 2U && static_cast<uint8_t>((*body)[0]) == 0x1FU &&
                      static_cast<uint8_t>((*body)[1]) == 0x8BU;
    const bool zlib = body->length() >= 2U && static_cast<uint8_t>((*body)[0]) == 0x78U;
    if (gzip || zlib) {
        const uint32_t output_capacity = 24576U;
        uint8_t *output = static_cast<uint8_t *>(malloc(output_capacity));
        if (output == nullptr) return false;
        uint32_t output_size = 0U;
        const int32_t result = ArduinoZlib::libmpq__decompress_zlib(
            reinterpret_cast<uint8_t *>(body->begin()), body->length(), output,
            output_capacity, output_size);
        if (result < 0 || output_size == 0U) {
            Serial.printf("[WEATHER][%s] decompress failed result=%ld size=%lu\n", stage,
                          static_cast<long>(result), static_cast<unsigned long>(output_size));
            free(output);
            return false;
        }
        if (output_size >= output_capacity) {
            Serial.printf("[WEATHER][%s] decompressed body too large size=%lu\n", stage,
                          static_cast<unsigned long>(output_size));
            free(output);
            return false;
        }
        output[output_size] = '\0';
        *body = reinterpret_cast<const char *>(output);
        free(output);
        Serial.printf("[WEATHER][%s] decompressed body_len=%u\n", stage,
                      static_cast<unsigned>(body->length()));
    }
    return true;
}

bool parse_json(const char *stage, const String &body, JsonDocument *document,
                bool require_success_code) {
    if (document == nullptr) {
        Serial.printf("[WEATHER][%s] JSON document is null\n", stage);
        return false;
    }
    const DeserializationError error = deserializeJson(*document, body);
    if (error != DeserializationError::Ok) {
        Serial.printf("[WEATHER][%s] JSON parse failed: %s body_len=%u\n", stage,
                      error.c_str(), static_cast<unsigned>(body.length()));
        return false;
    }
    const JsonVariantConst code_node = (*document)["code"];
    if (code_node.isNull()) {
        Serial.printf("[WEATHER][%s] JSON code is missing%s\n", stage,
                      require_success_code ? " (required)" : " (accepted for compatibility)");
        return !require_success_code;
    }
    const String code = code_node.as<String>();
    if (code.isEmpty()) {
        Serial.printf("[WEATHER][%s] JSON code is empty%s\n", stage,
                      require_success_code ? " (required)" : " (accepted for compatibility)");
        return !require_success_code;
    }
    Serial.printf("[WEATHER][%s] JSON code=%s\n", stage, code.c_str());
    if (code != "200") {
        Serial.printf("[WEATHER][%s] API rejected code=%s\n", stage, code.c_str());
        return false;
    }
    return true;
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

template <size_t N>
void copy_text(char (&destination)[N], const char *source) {
    if (N == 0U) return;
    std::strncpy(destination, source == nullptr ? "" : source, N - 1U);
    destination[N - 1U] = '\0';
}

template <size_t N>
void copy_variant(char (&destination)[N], JsonVariantConst value) {
    const String text = value.as<String>();
    copy_text(destination, text.c_str());
}
}

bool weather_service_sync_time(AppTime *time) {
    if (time == nullptr) return false;
    configTime(8 * 3600, 0, NTP_1, NTP_2, NTP_3);
    struct tm local = {};
    if (!getLocalTime(&local, HTTP_TIMEOUT_MS) || local.tm_year + 1900 < 2020) {
        return false;
    }
    *time = {};
    time->year = static_cast<uint16_t>(local.tm_year + 1900);
    time->month = static_cast<uint8_t>(local.tm_mon + 1);
    time->day = static_cast<uint8_t>(local.tm_mday);
    time->hour = static_cast<uint8_t>(local.tm_hour);
    time->minute = static_cast<uint8_t>(local.tm_min);
    time->second = static_cast<uint8_t>(local.tm_sec);
    time->valid = true;
    time->stale = false;
    return true;
}

bool weather_service_resolve_location(WeatherNetworkProfile *profile) {
    if (profile == nullptr || profile->city.isEmpty()) {
        Serial.println("[WEATHER][City ID] missing city profile");
        return false;
    }
    if (!profile->location.isEmpty() && !profile->lat.isEmpty() && !profile->lon.isEmpty()) {
        Serial.printf("[WEATHER][City ID] cached location=%s lat=%s lon=%s\n",
                      profile->location.c_str(), profile->lat.c_str(), profile->lon.c_str());
        return true;
    }
    const String url = String("https://") + API_HOST + CITY_PATH + "?location=" +
                       url_encode(profile->city) + "&adm=" + url_encode(profile->adm);
    String body;
    JsonDocument document;
    if (!http_json("City ID", url, CITY_TIMEOUT_MS, &body) ||
        !parse_json("City ID", body, &document, true)) return false;
    JsonObject location = document["location"][0];
    const char *id = location["id"] | "";
    const char *lat = location["lat"] | "";
    const char *lon = location["lon"] | "";
    if (*id == '\0' || *lat == '\0' || *lon == '\0') {
        Serial.printf("[WEATHER][City ID] missing location fields id=%s lat=%s lon=%s\n",
                      id, lat, lon);
        return false;
    }
    profile->location = id;
    profile->lat = lat;
    profile->lon = lon;
    profile->city = location["name"] | profile->city;
    weather_network_update_active_location(profile->location, profile->lat, profile->lon,
                                            profile->city);
    Serial.printf("[WEATHER][City ID] resolved city=%s location=%s lat=%s lon=%s\n",
                  profile->city.c_str(), profile->location.c_str(), profile->lat.c_str(),
                  profile->lon.c_str());
    return true;
}

bool weather_service_query_now(HomeWeatherData *weather) {
    WeatherNetworkProfile profile = {};
    if (weather == nullptr) {
        Serial.println("[WEATHER][Current] output is null");
        return false;
    }
    if (!weather_network_get_active(&profile) || profile.location.isEmpty()) {
        Serial.printf("[WEATHER][Current] missing location active=%d location=%s\n",
                      profile.used ? 1 : 0, profile.location.c_str());
        return false;
    }
    String body;
    JsonDocument document;
    const String url = String("https://") + API_HOST + NOW_PATH + "?location=" + profile.location;
    if (!http_json("Current", url, HTTP_TIMEOUT_MS, &body) ||
        !parse_json("Current", body, &document, true)) return false;
    JsonObject now = document["now"].as<JsonObject>();
    const uint16_t icon = now["icon"].as<uint16_t>();
    if (icon == 0U) {
        Serial.println("[WEATHER][Current] missing now.icon");
        return false;
    }
    weather->icon_id = icon;
    weather->scene = scene_for_icon(icon);
    weather->temperature_c = now["temp"].as<int16_t>();
    weather->humidity = now["humidity"].as<uint8_t>();
    weather->valid = true;
    weather->stale = false;
    copy_variant(weather->text, now["text"]);
    copy_variant(weather->feels_like, now["feelsLike"]);
    copy_variant(weather->visibility, now["vis"]);
    String wind = now["windDir"].as<String>();
    wind += ",";
    wind += now["windScale"].as<String>();
    copy_text(weather->wind, wind.c_str());
    Serial.printf("[WEATHER][Current] ok icon=%u temp=%d humidity=%u text=%s\n",
                  static_cast<unsigned>(weather->icon_id), static_cast<int>(weather->temperature_c),
                  static_cast<unsigned>(weather->humidity), weather->text);
    return true;
}

bool weather_service_query_forecast(WeatherServiceForecast *forecast) {
    WeatherNetworkProfile profile = {};
    if (forecast == nullptr) {
        Serial.println("[WEATHER][Forecast] output is null");
        return false;
    }
    if (!weather_network_get_active(&profile) || profile.location.isEmpty()) {
        Serial.printf("[WEATHER][Forecast] missing location active=%d location=%s\n",
                      profile.used ? 1 : 0, profile.location.c_str());
        return false;
    }
    String body;
    JsonDocument document;
    const String url = String("https://") + API_HOST + FORECAST_PATH + "?location=" + profile.location;
    if (!http_json("Forecast", url, HTTP_TIMEOUT_MS, &body) ||
        !parse_json("Forecast", body, &document, true)) return false;
    for (uint8_t i = 0U; i < APP_WEATHER_FORECAST_DAYS; ++i) {
        JsonObject source = document["daily"][i];
        WeatherForecastDay &day = forecast->days[i];
        day = {};
        day.icon_id = source["iconDay"].as<uint16_t>();
        day.high_c = source["tempMax"].as<int16_t>();
        day.low_c = source["tempMin"].as<int16_t>();
        if (!parse_date(source["fxDate"] | "", &day.year, &day.month, &day.day) ||
            day.icon_id == 0U) {
            Serial.printf("[WEATHER][Forecast] invalid day=%u date=%s icon=%u\n",
                          static_cast<unsigned>(i), source["fxDate"] | "",
                          static_cast<unsigned>(day.icon_id));
            return false;
        }
        day.scene = scene_for_icon(day.icon_id);
        copy_variant(day.text_day, source["textDay"]);
        copy_variant(day.date_text, source["fxDate"]);
        day.valid = true;
    }
    Serial.println("[WEATHER][Forecast] ok days=7");
    return true;
}

bool weather_service_query_air(HomeWeatherData *weather) {
    WeatherNetworkProfile profile = {};
    if (weather == nullptr) {
        Serial.println("[WEATHER][Air] output is null");
        return false;
    }
    if (!weather_network_get_active(&profile) || profile.lat.isEmpty() || profile.lon.isEmpty()) {
        Serial.printf("[WEATHER][Air] missing coordinates active=%d lat=%s lon=%s\n",
                      profile.used ? 1 : 0, profile.lat.c_str(), profile.lon.c_str());
        return false;
    }
    String body;
    JsonDocument document;
    const String url = String("https://") + API_HOST + AIR_PATH + profile.lat + "/" + profile.lon;
    if (!http_json("Air", url, HTTP_TIMEOUT_MS, &body) ||
        !parse_json("Air", body, &document, false)) return false;
    weather->aqi = document["indexes"][0]["aqi"].as<int16_t>();
    if (weather->aqi < 0) {
        Serial.println("[WEATHER][Air] missing indexes[0].aqi");
        return false;
    }
    JsonArray pollutants = document["pollutants"].as<JsonArray>();
    bool found = false;
    uint8_t pollutant_count = 0U;
    for (JsonObject pollutant : pollutants) {
        const char *code = pollutant["code"] | "";
        char *destination = nullptr;
        if (std::strcmp(code, "pm10") == 0) destination = weather->pm10;
        else if (std::strcmp(code, "pm2p5") == 0) destination = weather->pm2p5;
        else if (std::strcmp(code, "no2") == 0) destination = weather->no2;
        else if (std::strcmp(code, "so2") == 0) destination = weather->so2;
        else if (std::strcmp(code, "co") == 0) destination = weather->co;
        else if (std::strcmp(code, "o3") == 0) destination = weather->o3;
        if (destination == nullptr) continue;
        const String value = pollutant["concentration"]["value"].as<String>();
        ++pollutant_count;
        Serial.printf("[WEATHER][Air] pollutant %s=%s\n", code, value.c_str());
        if (destination == weather->pm10) copy_text(weather->pm10, value.c_str());
        else if (destination == weather->pm2p5) copy_text(weather->pm2p5, value.c_str());
        else if (destination == weather->no2) copy_text(weather->no2, value.c_str());
        else if (destination == weather->so2) copy_text(weather->so2, value.c_str());
        else if (destination == weather->co) copy_text(weather->co, value.c_str());
        else if (destination == weather->o3) copy_text(weather->o3, value.c_str());
        if (std::strcmp(code, "pm2p5") == 0) {
            weather->pm25 = static_cast<int16_t>(value.toInt());
            found = true;
        }
    }
    if (!found) Serial.println("[WEATHER][Air] pm2p5 missing, keeping previous value");
    Serial.printf("[WEATHER][Air] ok aqi=%d pollutants=%u\n", static_cast<int>(weather->aqi),
                  static_cast<unsigned>(pollutant_count));
    return true;
}
