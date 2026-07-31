#pragma once

#include "app/app_data.h"
#include "app/weather_network.h"

#include <cstdint>

struct WeatherServiceForecast {
    WeatherForecastDay days[APP_WEATHER_FORECAST_DAYS];
};

bool weather_service_sync_time(AppTime *time);
bool weather_service_resolve_location(WeatherNetworkProfile *profile);
bool weather_service_query_now(HomeWeatherData *weather);
bool weather_service_query_forecast(WeatherServiceForecast *forecast);
bool weather_service_query_air(HomeWeatherData *weather);
