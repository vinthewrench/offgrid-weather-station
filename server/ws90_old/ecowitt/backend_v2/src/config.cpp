#include "config.hpp"
#include "json.hpp"
#include "utils.hpp"
#include <iostream>

using json = nlohmann::json;

// Global instance
Config g_cfg;

bool load_config(const std::string &path)
{
    std::string raw;
    if (!utils::read_file(path, raw)) {
        std::cerr << "config.json missing, using defaults\n";
        g_cfg.loaded = false;
        return false;
    }

    try {
        json j = json::parse(raw);

        g_cfg.latitude  = j.value("latitude", 0.0);
        g_cfg.longitude = j.value("longitude", 0.0);
        g_cfg.tz_offset = j.value("tz_offset", 0);
        g_cfg.tz_name   = j.value("tz_name", "UTC");

        g_cfg.loaded = true;
        return true;
    }
    catch (...) {
        std::cerr << "config.json invalid, using defaults\n";
        g_cfg.loaded = false;
        return false;
    }
}
