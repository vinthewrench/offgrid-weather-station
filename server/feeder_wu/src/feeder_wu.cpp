
// feeder.cpp - WS90 Weather Underground Feeder (patched full version)
// Includes:
//  • config.json-only configuration
//  • rain-rate corrected for interval
//  • dewpoint guard
//  • solar radiation threshold
//  • backend offline backoff logging
//  • SOFTWARE_VERSION auto-injection support

#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <chrono>
#include <thread>
#include <limits>
#include <algorithm>

#include <curl/curl.h>
#include "json.hpp"

using json = nlohmann::json;

// ------------------------------------------------------------
// SOFTWARE VERSION SUPPORT
// ------------------------------------------------------------
#ifndef SOFTWARE_VERSION
#define SOFTWARE_VERSION "dev"
#endif

static const std::string SOFTWARE_TYPE =
    std::string("StellaPortaWS90-") + SOFTWARE_VERSION;

// ------------------------------------------------------------
// CONSTANTS
// ------------------------------------------------------------
static constexpr double LUX_TO_WM2 = 0.0079;

// Report interval (assigned in main)
int g_interval_sec = 60;

// ------------------------------------------------------------
// UTILITY FUNCTIONS
// ------------------------------------------------------------

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *out = static_cast<std::string *>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string url_encode(CURL *curl, const std::string &s) {
    char *esc = curl_easy_escape(curl, s.c_str(), (int)s.size());
    if (!esc) return "";
    std::string out(esc);
    curl_free(esc);
    return out;
}

// ------------------------------------------------------------
// FETCH BACKEND JSON
// ------------------------------------------------------------

static bool fetch_backend_json(const std::string &backend_url, json &out_j) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, backend_url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || status != 200)
        return false;

    try {
        out_j = json::parse(body);
        return true;
    } catch (...) {
        return false;
    }
}

// ------------------------------------------------------------
// RAIN STATE
// ------------------------------------------------------------

static double g_last_hourly_in = std::numeric_limits<double>::quiet_NaN();

// ------------------------------------------------------------
// BUILD WU URL
// ------------------------------------------------------------

static bool build_wu_url(CURL *curl, const json &j,
                         const std::string &station_id,
                         const std::string &station_key,
                         std::string &out_url)
{
    if (!j.contains("temperature_F") || !j.contains("humidity"))
        return false;

    if (j.contains("ws90_status")) {
        const auto &s = j["ws90_status"];
        if (!s.value("http_ok", false) ||
            !s.value("rtlsdr_ok", false) ||
            s.value("stale", true)) {
            return false;
        }
    }

    double tempF = j.value("temperature_F", NAN);
    int humidity = j.value("humidity", 0);
    double wind_m = j.value("wind_avg_m_s", 0.0);
    double gust_m = j.value("wind_max_m_s", 0.0);
    int wind_dir = j.value("wind_dir_deg", 0);

    double wind_mph = wind_m * 2.23694;
    double gust_mph = gust_m * 2.23694;

    double dailyrain_in = 0.0;
    double rain_interval_in = 0.0;

    if (j.contains("rain")) {
        const auto &r = j["rain"];
        double hourly_in = r.value("hourly_in", 0.0);
        dailyrain_in = r.value("daily_in", 0.0);

        if (!std::isnan(g_last_hourly_in)) {
            double delta = hourly_in - g_last_hourly_in;
            if (delta < 0) delta = 0;
            rain_interval_in = delta;
        }
        g_last_hourly_in = hourly_in;
    }

    // Correct rain-rate calculation for arbitrary interval
    double rain_rate_in_hr = 0.0;
    if (g_interval_sec > 0)
        rain_rate_in_hr = rain_interval_in * (3600.0 / g_interval_sec);

    // Dew point calculation
    double tempC = (tempF - 32.0) * 5.0 / 9.0;
    double rh = std::clamp<double>(humidity, 1.0, 100.0);
    double gamma = std::log(rh / 100.0) + (17.625 * tempC) / (243.04 + tempC);
    double dewC = 243.04 * gamma / (17.625 - gamma);
    double dewF = dewC * 9.0 / 5.0 + 32.0;

    // Base WU URL
    std::string base =
        "https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php";

    std::string q;
    q += "ID=" + url_encode(curl, station_id);
    q += "&PASSWORD=" + url_encode(curl, station_key);
    q += "&action=updateraw&dateutc=now";

    q += "&tempf=" + std::to_string(tempF);
    q += "&humidity=" + std::to_string(humidity);
    q += "&windspeedmph=" + std::to_string(wind_mph);
    q += "&windgustmph=" + std::to_string(gust_mph);
    q += "&winddir=" + std::to_string(wind_dir);

    if (humidity > 0)
        q += "&dewptf=" + std::to_string(dewF);

    q += "&rainin=" + std::to_string(rain_interval_in);
    q += "&dailyrainin=" + std::to_string(dailyrain_in);
    q += "&rainratein=" + std::to_string(rain_rate_in_hr);

    if (j.contains("uvi"))
        q += "&UV=" + std::to_string(j.value("uvi", 0.0));

    if (j.contains("light_lux")) {
        double lux = j.value("light_lux", 0.0);
        if (lux > 1.0)
            q += "&solarradiation=" + std::to_string(lux * LUX_TO_WM2);
    }

    q += "&softwaretype=" + url_encode(curl, SOFTWARE_TYPE);

    out_url = base + "?" + q;
    return true;
}

// ------------------------------------------------------------
// SEND WU UPDATE
// ------------------------------------------------------------

static bool send_wu_update(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    std::string body;
    long status = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    // Only log errors, stay silent on success
    if (status != 200) {
        std::cerr << "[feeder] upload error: HTTP " << status
                  << " response='" << body << "'\n";
    }

    return status == 200;
}


// ------------------------------------------------------------
// MAIN LOOP
// ------------------------------------------------------------

int main() {
    json cfg;

    try {
        std::ifstream f("config.json");
        if (!f.good()) {
            std::cerr << "[feeder] ERROR: config.json not found\n";
            return 1;
        }
        f >> cfg;
    } catch (...) {
        std::cerr << "[feeder] ERROR: invalid config.json\n";
        return 1;
    }

    std::string station_id = cfg.value("WU_STATION_ID", "");
    std::string station_key = cfg.value("WU_STATION_KEY", "");
    std::string backend_url = cfg.value(
        "BACKEND_URL",
        "http://localhost:8889/api/v2/weather"
    );
    int interval_sec = cfg.value("WU_INTERVAL_SEC", 60);

    g_interval_sec = interval_sec;

    if (station_id.empty() || station_key.empty()) {
        std::cerr << "[feeder] ERROR: missing station credentials\n";
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::cout << "[feeder] starting\n";
    std::cout << "  backend_url=" << backend_url << "\n";
    std::cout << "  interval=" << interval_sec << " sec\n";

    int fail_count = 0;

    while (true) {
        json j;
        if (!fetch_backend_json(backend_url, j)) {
            if (fail_count % 10 == 0)
                std::cerr << "[feeder] backend offline (" << fail_count << " fails)\n";
            fail_count++;
            std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
            continue;
        }

        fail_count = 0;

        CURL *curl = curl_easy_init();
        if (curl) {
            std::string wu_url;
            if (build_wu_url(curl, j, station_id, station_key, wu_url)) {
                bool ok = send_wu_update(wu_url);
                if (!ok) {
                    std::cerr << "[feeder] upload failed, retrying...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    send_wu_update(wu_url);
                }
            }
            curl_easy_cleanup(curl);
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
    }

    curl_global_cleanup();
    return 0;
}
