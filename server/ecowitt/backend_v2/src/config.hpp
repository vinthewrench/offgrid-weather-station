#pragma once

#include <string>

// Simple global configuration loaded from config.json
struct Config {
    double latitude  = 0.0;     // degrees
    double longitude = 0.0;     // degrees
    int    tz_offset = 0;       // hours from UTC, e.g. -6
    std::string tz_name;        // "CST", etc.
    bool   loaded    = false;
};

// Global instance
extern Config g_cfg;

// Load configuration from JSON file.
// Returns true if loaded successfully, false if file missing or invalid.
// On failure, g_cfg is filled with sane defaults.
bool load_config(const std::string &path = "config.json");
