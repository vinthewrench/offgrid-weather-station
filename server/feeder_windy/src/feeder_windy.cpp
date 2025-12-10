// feeder_windy.cpp - WS90 Windy Feeder
// Uses:
//  • config.json-only configuration
//  • WS90 backend JSON from BACKEND_URL
//  • Metric upload to Windy PWS API
//  • WS90 status guard

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
    std::string("StellaPortaWS90-Windy-") + SOFTWARE_VERSION;

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
// BUILD WINDY URL
// ------------------------------------------------------------

static bool build_windy_url(CURL *curl,
                            const json &j,
                            const std::string &api_key,
                            std::string &out_url)
{
    // Require basic fields
    if (!j.contains("temperature_F") || !j.contains("humidity"))
        return false;

    // WS90 health check if present
    if (j.contains("ws90_status") && j["ws90_status"].is_object()) {
        const auto &s = j["ws90_status"];
        if (!s.value("http_ok", false) ||
            !s.value("rtlsdr_ok", false) ||
            s.value("stale", true)) {
            return false;
        }
    }

    double tempF = j.value("temperature_F", std::numeric_limits<double>::quiet_NaN());
    if (std::isnan(tempF))
        return false;

    int humidity      = j.value("humidity", 0);
    double wind_m     = j.value("wind_avg_m_s", 0.0);
    double gust_m     = j.value("wind_max_m_s", 0.0);
    int wind_dir      = j.value("wind_dir_deg", 0);

    // Convert to Celsius for Windy
    double tempC = (tempF - 32.0) * 5.0 / 9.0;

    // Rain handling (inches -> mm)
    double rain_mm = 0.0;
    double dailyrain_mm = 0.0;

    if (j.contains("rain") && j["rain"].is_object()) {
        const auto &r = j["rain"];
        double hourly_in = r.value("hourly_in", 0.0);
        double daily_in  = r.value("daily_in", 0.0);

        rain_mm      = hourly_in * 25.4;
        dailyrain_mm = daily_in  * 25.4;
    }

    // UV and solar
    double uv = j.value("uvi", 0.0);
    double solar_wm2 = 0.0;

    if (j.contains("light_lux")) {
        double lux = j.value("light_lux", 0.0);
        if (lux > 1.0)
            solar_wm2 = lux * LUX_TO_WM2;
    }

    // Base Windy URL
    std::string base =
        "https://stations.windy.com/pws/update/" + api_key;

    std::string q;

    // Required / primary fields
    q += "temp=" + std::to_string(tempC);
    q += "&humidity=" + std::to_string(humidity);
    q += "&windspeedms=" + std::to_string(wind_m);
    q += "&windgustms=" + std::to_string(gust_m);
    q += "&winddir=" + std::to_string(wind_dir);

    // Optional rain
    if (rain_mm > 0.0)
        q += "&rain=" + std::to_string(rain_mm);
    if (dailyrain_mm > 0.0)
        q += "&dailyrain=" + std::to_string(dailyrain_mm);

    // Optional UV and solar
    if (uv > 0.0)
        q += "&uv=" + std::to_string(uv);
    if (solar_wm2 > 0.0)
        q += "&solarradiation=" + std::to_string(solar_wm2);

    // Software tag and time
    q += "&softwaretype=" + url_encode(curl, SOFTWARE_TYPE);
    q += "&dateutc=now";

    out_url = base + "?" + q;
    return true;
}

// ------------------------------------------------------------
// SEND WINDY UPDATE
// ------------------------------------------------------------

static bool send_windy_update(const std::string &url) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    std::string body;
    long status = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    // Transport-level error
    if (rc != CURLE_OK) {
        std::cerr << "[feeder_windy] CURL error: "
                  << curl_easy_strerror(rc) << "\n";
        return false;
    }

    // Windy's 200 = OK
    if (status == 200) {
        return true;
    }

    // Try parsing Windy's JSON body
    try {
        auto j = json::parse(body);

        // Navigate into the windy structure
        if (j.contains("result")) {

            // The station number is always a stringified integer
            for (auto &entry : j["result"].items()) {
                auto &obj = entry.value();

                if (obj.contains("observations") &&
                    obj["observations"].is_array() &&
                    !obj["observations"].empty()) {

                    auto &obs = obj["observations"][0];

                    bool success = obs.value("success", true);

                    if (!success) {
                        std::string err = obs.value("error", "");

                        // WINDY RATE LIMIT / TOO SOON CASE
                        if (err.find("too soon") != std::string::npos ||
                            err.find("interval") != std::string::npos) {

                            std::cerr << "[feeder_windy] Windy rate limit: "
                                      << err << "\n";

                            // DO NOT retry
                            return true; // Treat as handled
                        }

                        // OTHER windy-side errors
                        std::cerr << "[feeder_windy] Windy error: "
                                  << err << "\n";
                        return false;
                    }
                }
            }
        }
    } catch (...) {
        // JSON parse failed; treat as real error
        std::cerr << "[feeder_windy] Invalid response from Windy: "
                  << body << "\n";
        return false;
    }

    // Fallback: any HTTP 400 with no explicit error message
    std::cerr << "[feeder_windy] HTTP " << status
              << " response='" << body << "'\n";
    return false;
}




// ------------------------------------------------------------
// MAIN LOOP
// ------------------------------------------------------------

int main() {
    json cfg;

    try {
        std::ifstream f("config.json");
        if (!f.good()) {
            std::cerr << "[feeder_windy] ERROR: config.json not found\n";
            return 1;
        }
        f >> cfg;
    } catch (...) {
        std::cerr << "[feeder_windy] ERROR: invalid config.json\n";
        return 1;
    }

    std::string api_key = cfg.value("WINDY_API_KEY", "");
    std::string backend_url = cfg.value(
        "BACKEND_URL",
        "http://localhost:8889/api/v2/weather"
    );
    int interval_sec = cfg.value("WINDY_INTERVAL_SEC", 60);

    g_interval_sec = interval_sec;

    if (api_key.empty()) {
        std::cerr << "[feeder_windy] ERROR: missing WINDY_API_KEY\n";
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::cout << "[feeder_windy] starting\n";
    std::cout << "  backend_url=" << backend_url << "\n";
    std::cout << "  interval=" << interval_sec << " sec\n";

    int fail_count = 0;

    while (true) {
        json j;
        if (!fetch_backend_json(backend_url, j)) {
            if (fail_count % 10 == 0)
                std::cerr << "[feeder_windy] backend offline (" << fail_count << " fails)\n";
            fail_count++;
            std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
            continue;
        }

        fail_count = 0;

        CURL *curl = curl_easy_init();
        if (curl) {
            std::string windy_url;
            if (build_windy_url(curl, j, api_key, windy_url)) {
                bool ok = send_windy_update(windy_url);
                if (!ok) {
                    std::cerr << "[feeder_windy] upload failed, retrying...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    send_windy_update(windy_url);
                }
            }
            curl_easy_cleanup(curl);
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
    }

    curl_global_cleanup();
    return 0;
}
