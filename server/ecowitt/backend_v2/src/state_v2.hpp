#pragma once
#include <string>
#include "json.hpp"


namespace state_v2 {

void init();
std::string current_weather_json();

std::string history_temperature_json(int days, int limit, int offset);
std::string history_humidity_json(int days, int limit, int offset);
std::string history_rain_json(int days, int limit, int offset);

nlohmann::json build_current_json();

}
