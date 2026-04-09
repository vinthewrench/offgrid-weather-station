#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

extern "C" {
#include <microhttpd.h>
#include <curl/curl.h>
#include <sqlite3.h>
}

#include "state_v2.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "astro.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>

using nlohmann::json;

// =========================================
// Config constants
// =========================================

static const char *WS90_URL          = "http://172.17.0.1:7890";
static const int   POLL_INTERVAL_SEC = 10;
static const size_t MAX_BODY_SIZE    = 8192;

static const char *DB_PATH_LOCAL     = "weather_history_v2.sqlite3";
static const char *DB_PATH_DOCKER    = "/state/weather_history_v2.sqlite3";

static const char *STATE_PATH_DOCKER = "/state/rain_state_v2.json";
static const char *STATE_PATH_LOCAL  = "rain_state_v2.json";

static std::string get_state_path()
{
    // If Docker directory exists, use the Docker path
    if (access("/state", F_OK) == 0) {
        return STATE_PATH_DOCKER;
    }
    // Otherwise default to local working directory
    return STATE_PATH_LOCAL;
}

static std::string get_db_path()
{
    // If Docker directory exists, use the Docker path
    if (access("/state", F_OK) == 0) {
        return DB_PATH_DOCKER;
    }
    // Otherwise default to local working directory
    return DB_PATH_LOCAL;
}

// Historical totals from migration moment
static const double HISTORICAL_TOTAL_IN   = 62.77;
static const double HISTORICAL_YEARLY_IN  = 62.77;
static const double HISTORICAL_MONTHLY_IN = 4.27;
static const double HISTORICAL_WEEKLY_IN  = 1.96;

static const int EVENT_GAP_MIN      = 30;
static const int HOURLY_WINDOW_SEC  = 3600;
static const int MIN_COVERAGE_SEC   = 12 * 3600;

// =========================================
// Types
// =========================================

struct RainDelta {
    std::time_t ts;
    double      inches;
};

struct WeatherStateV2 {
    // --- WS90 telemetry ---
    double battery_mV      = 0.0;
    double battery_ok      = 0.0;

    int    id              = 0;
    std::string model;
    int    firmware        = 0;

    double humidity        = 0.0;
    double temperature_C   = 0.0;
    double wind_dir_deg    = 0.0;
    double wind_avg_m_s    = 0.0;
    double wind_max_m_s    = 0.0;
    double light_lux       = 0.0;
    double uvi             = 0.0;
    double rain_mm         = 0.0;
    double supercap_V      = 0.0;

    std::string last_time_iso;

    // ---- RAIN & DAILY TRACKING ----
    double last_rain_mm      = 0.0;
    std::time_t last_update  = 0;

    double rain_daily_in     = 0.0;
    double rain_monthly_in   = 0.0;
    double rain_yearly_in    = 0.0;
    double rain_weekly_in    = 0.0;
    double rain_hourly_in    = 0.0;
    double rain_event_in     = 0.0;

    int daily_ymd            = 0;
    int month_ym             = 0;
    int year_y               = 0;
    int week_start_ymd       = 0;

    double historical_total_in   = HISTORICAL_TOTAL_IN;
    double historical_yearly_in  = HISTORICAL_YEARLY_IN;
    double historical_monthly_in = HISTORICAL_MONTHLY_IN;
    double historical_weekly_in  = HISTORICAL_WEEKLY_IN;
    bool   historical_seeded     = false;

    std::vector<RainDelta> deltas;
    std::time_t last_rain_ts     = 0;

    bool   have_temp      = false;
    double temp_high_c    = 0.0;
    double temp_low_c     = 0.0;

    bool   have_hum       = false;
    double hum_high       = 0.0;
    double hum_low        = 0.0;

    std::time_t day_first_ts = 0;
    std::time_t day_last_ts  = 0;

    // Daily wind tracking
    bool          have_wind         = false;
    double        wind_mean_m_s     = 0.0;   // running mean of wind_avg_m_s
    double        wind_max_gust_m_s = 0.0;   // max of wind_max_m_s
    std::uint64_t wind_sample_count = 0;
};

static WeatherStateV2 g_state;
static json           g_last_ws90;
static std::mutex     g_lock;
static sqlite3       *g_db         = nullptr;
static std::atomic<bool> g_running{true};

static bool        g_ws90_http_ok     = false;      // could we talk HTTP to ws90?
static bool        g_rtlsdr_ok        = false;      // is the SDR stream healthy?
static std::time_t g_ws90_last_poll   = 0;          // last time we polled ws90
static long        g_ws90_http_status = 0;          // last HTTP status code
static std::string g_ws90_error_code;               // "stale_data", "no_data", "curl_error", etc.
static std::string g_ws90_error_msg;                // human-ish description

// =========================================
// Time helpers
// =========================================

static int ymd_from_time(std::time_t t) {
    std::tm lt;
    localtime_r(&t, &lt);
    return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}

static int ym_from_time(std::time_t t) {
    std::tm lt;
    localtime_r(&t, &lt);
    return (lt.tm_year + 1900) * 100 + (lt.tm_mon + 1);
}

static int y_from_time(std::time_t t) {
    std::tm lt;
    localtime_r(&t, &lt);
    return lt.tm_year + 1900;
}

static double inches_from_mm(double mm) {
    return mm / 25.4;
}

static std::time_t day_start_ts(std::time_t t) {
    std::tm lt;
    localtime_r(&t, &lt);
    lt.tm_hour = 0;
    lt.tm_min  = 0;
    lt.tm_sec  = 0;
    return std::mktime(&lt);
}

// =========================================
// State initialization and load/save
// =========================================

static void init_state_defaults(WeatherStateV2 &st) {
    st = WeatherStateV2{};
    std::time_t now = std::time(nullptr);
    st.last_update  = now;

    st.daily_ymd      = ymd_from_time(now);
    st.month_ym       = ym_from_time(now);
    st.year_y         = y_from_time(now);
    st.week_start_ymd = st.daily_ymd;

    st.historical_seeded = true;
}

static void load_state(const std::string &path, WeatherStateV2 &st) {
    std::ifstream f(path);
    if (!f.good()) {
        init_state_defaults(st);
        return;
    }

    try {
        json j;
        f >> j;
        init_state_defaults(st);

        if (j.contains("last_rain_mm"))         st.last_rain_mm    = j["last_rain_mm"].get<double>();
        if (j.contains("last_update_ts"))       st.last_update     = (std::time_t)j["last_update_ts"].get<long long>();

        if (j.contains("rain_daily_in"))        st.rain_daily_in   = j["rain_daily_in"].get<double>();
        if (j.contains("rain_monthly_in"))      st.rain_monthly_in = j["rain_monthly_in"].get<double>();
        if (j.contains("rain_yearly_in"))       st.rain_yearly_in  = j["rain_yearly_in"].get<double>();
        if (j.contains("rain_weekly_in"))       st.rain_weekly_in  = j["rain_weekly_in"].get<double>();
        if (j.contains("rain_hourly_in"))       st.rain_hourly_in  = j["rain_hourly_in"].get<double>();
        if (j.contains("rain_event_in"))        st.rain_event_in   = j["rain_event_in"].get<double>();

        if (j.contains("daily_ymd"))            st.daily_ymd       = j["daily_ymd"].get<int>();
        if (j.contains("month_ym"))             st.month_ym        = j["month_ym"].get<int>();
        if (j.contains("year_y"))               st.year_y          = j["year_y"].get<int>();
        if (j.contains("week_start_ymd"))       st.week_start_ymd  = j["week_start_ymd"].get<int>();

        if (j.contains("historical_total_in"))   st.historical_total_in   = j["historical_total_in"].get<double>();
        if (j.contains("historical_yearly_in"))  st.historical_yearly_in  = j["historical_yearly_in"].get<double>();
        if (j.contains("historical_monthly_in")) st.historical_monthly_in = j["historical_monthly_in"].get<double>();
        if (j.contains("historical_weekly_in"))  st.historical_weekly_in  = j["historical_weekly_in"].get<double>();
        if (j.contains("historical_seeded"))     st.historical_seeded     = j["historical_seeded"].get<bool>();

        if (j.contains("temp_high_c"))          st.temp_high_c     = j["temp_high_c"].get<double>();
        if (j.contains("temp_low_c"))           st.temp_low_c      = j["temp_low_c"].get<double>();
        if (j.contains("have_temp"))            st.have_temp       = j["have_temp"].get<bool>();

        if (j.contains("hum_high"))             st.hum_high        = j["hum_high"].get<double>();
        if (j.contains("hum_low"))              st.hum_low         = j["hum_low"].get<double>();
        if (j.contains("have_hum"))             st.have_hum        = j["have_hum"].get<bool>();

        // Wind daily tracking
        if (j.contains("have_wind"))            st.have_wind         = j["have_wind"].get<bool>();
        if (j.contains("wind_mean_m_s"))        st.wind_mean_m_s     = j["wind_mean_m_s"].get<double>();
        if (j.contains("wind_max_gust_m_s"))    st.wind_max_gust_m_s = j["wind_max_gust_m_s"].get<double>();
        if (j.contains("wind_sample_count"))    st.wind_sample_count = j["wind_sample_count"].get<std::uint64_t>();

        if (j.contains("day_first_ts"))         st.day_first_ts    = (std::time_t)j["day_first_ts"].get<long long>();
        if (j.contains("day_last_ts"))          st.day_last_ts     = (std::time_t)j["day_last_ts"].get<long long>();
    }
    catch (...) {
        init_state_defaults(st);
    }
}

static void save_state(const WeatherStateV2 &st) {
    json j;
    j["last_rain_mm"]       = st.last_rain_mm;
    j["last_update_ts"]     = (long long)st.last_update;

    j["rain_daily_in"]      = st.rain_daily_in;
    j["rain_monthly_in"]    = st.rain_monthly_in;
    j["rain_yearly_in"]     = st.rain_yearly_in;
    j["rain_weekly_in"]     = st.rain_weekly_in;
    j["rain_hourly_in"]     = st.rain_hourly_in;
    j["rain_event_in"]      = st.rain_event_in;

    j["daily_ymd"]          = st.daily_ymd;
    j["month_ym"]           = st.month_ym;
    j["year_y"]             = st.year_y;
    j["week_start_ymd"]     = st.week_start_ymd;

    j["historical_total_in"]   = st.historical_total_in;
    j["historical_yearly_in"]  = st.historical_yearly_in;
    j["historical_monthly_in"] = st.historical_monthly_in;
    j["historical_weekly_in"]  = st.historical_weekly_in;
    j["historical_seeded"]     = st.historical_seeded;

    j["temp_high_c"]        = st.temp_high_c;
    j["temp_low_c"]         = st.temp_low_c;
    j["have_temp"]          = st.have_temp;

    j["hum_high"]           = st.hum_high;
    j["hum_low"]            = st.hum_low;
    j["have_hum"]           = st.have_hum;

    // Wind daily tracking
    j["have_wind"]          = st.have_wind;
    j["wind_mean_m_s"]      = st.wind_mean_m_s;
    j["wind_max_gust_m_s"]  = st.wind_max_gust_m_s;
    j["wind_sample_count"]  = (long long)st.wind_sample_count;

    j["day_first_ts"]       = (long long)st.day_first_ts;
    j["day_last_ts"]        = (long long)st.day_last_ts;

    utils::write_file(get_state_path(), j.dump(2));
}

// =========================================
// DB setup
// =========================================

static void init_db() {
    std::string db_path = get_db_path();

    if (sqlite3_open(db_path.c_str(), &g_db) != SQLITE_OK) {
        fprintf(stderr, "Failed to open DB at %s: %s\n",
                db_path.c_str(),
                sqlite3_errmsg(g_db));
        g_db = nullptr;
        return;
    }


    const char *sql =
        "CREATE TABLE IF NOT EXISTS daily_weather ("
        "  day_ts INTEGER PRIMARY KEY,"
        "  temp_high_c REAL,"
        "  temp_low_c REAL,"
        "  humidity_high REAL,"
        "  humidity_low REAL,"
        "  rain_in REAL"
        ");";

    char *err = nullptr;
    if (sqlite3_exec(g_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        fprintf(stderr, "DB init error: %s\n", err);
        sqlite3_free(err);
    }
}

static void log_daily_to_db(std::time_t day_ts, const WeatherStateV2 &st, double rain_in)
{
    if (!g_db) return;

    const char *sql =
        "INSERT OR REPLACE INTO daily_weather "
        "(day_ts, temp_high_c, temp_low_c, humidity_high, humidity_low, rain_in) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)day_ts);

    if (st.have_temp) {
        sqlite3_bind_double(stmt, 2, st.temp_high_c);
        sqlite3_bind_double(stmt, 3, st.temp_low_c);
    } else {
        sqlite3_bind_null(stmt, 2);
        sqlite3_bind_null(stmt, 3);
    }

    if (st.have_hum) {
        sqlite3_bind_double(stmt, 4, st.hum_high);
        sqlite3_bind_double(stmt, 5, st.hum_low);
    } else {
        sqlite3_bind_null(stmt, 4);
        sqlite3_bind_null(stmt, 5);
    }

    sqlite3_bind_double(stmt, 6, rain_in);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// =========================================
// Rain logic
// =========================================

static void recompute_hourly(WeatherStateV2 &st, std::time_t now)
{
    double sum = 0.0;
    auto &v = st.deltas;
    size_t w = 0;

    for (size_t i = 0; i < v.size(); ++i) {
        if (now - v[i].ts <= HOURLY_WINDOW_SEC) {
            v[w++] = v[i];
            sum += v[i].inches;
        }
    }
    v.resize(w);
    st.rain_hourly_in = sum;
}

static void rollover_if_needed(WeatherStateV2 &st, std::time_t now)
{
    int d = ymd_from_time(now);
    int m = ym_from_time(now);
    int y = y_from_time(now);

    if (st.daily_ymd == 0) st.daily_ymd = d;
    if (st.month_ym  == 0) st.month_ym  = m;
    if (st.year_y    == 0) st.year_y    = y;
    if (st.week_start_ymd == 0) st.week_start_ymd = d;

    // --- DAY ROLLOVER (LOCAL MIDNIGHT RESET) ---
    if (d != st.daily_ymd) {
        std::time_t prev_day_ts = day_start_ts(st.day_first_ts ? st.day_first_ts : (now - 86400));

        bool ok = (st.day_first_ts && st.day_last_ts &&
                   (st.day_last_ts - st.day_first_ts) >= MIN_COVERAGE_SEC);

        if (ok)
            log_daily_to_db(prev_day_ts, st, st.rain_daily_in);

        st.rain_daily_in  = 0.0;
        st.daily_ymd      = d;
        st.day_first_ts   = now;
        st.day_last_ts    = now;

        // Reset daily temp/humidity tracking
        st.have_temp      = false;
        st.have_hum       = false;

        // Reset daily wind tracking
        st.have_wind         = false;
        st.wind_mean_m_s     = 0.0;
        st.wind_max_gust_m_s = 0.0;
        st.wind_sample_count = 0;
    }

    // --- MONTH ROLLOVER ---
    if (m != st.month_ym) {
        st.rain_monthly_in = 0.0;
        st.month_ym        = m;
    }

    // --- YEAR ROLLOVER ---
    if (y != st.year_y) {
        st.rain_yearly_in = 0.0;
        st.year_y         = y;
    }

    // --- WEEK ROLLOVER ---
    std::tm ws_tm{};
    ws_tm.tm_year = st.week_start_ymd / 10000 - 1900;
    ws_tm.tm_mon  = (st.week_start_ymd / 100 % 100) - 1;
    ws_tm.tm_mday = st.week_start_ymd % 100;
    std::time_t ws_ts = mktime(&ws_tm);
    std::time_t today_ts = day_start_ts(now);

    if (difftime(today_ts, ws_ts) >= 7 * 86400) {
        st.rain_weekly_in = 0.0;
        st.week_start_ymd = d;
    }
}

// =========================================
// Parse WS90 JSON
// =========================================

static void process_ws90_json_locked(const json &j)
{
    std::time_t now = std::time(nullptr);

    auto get_num = [&](const char *k, double d=0.0) {
        return (j.contains(k) && j[k].is_number()) ? j[k].get<double>() : d;
    };
    auto get_int = [&](const char *k, int d=0) {
        return (j.contains(k) && j[k].is_number_integer()) ? j[k].get<int>() : d;
    };
    auto get_str = [&](const char *k, const std::string &d="") {
        return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : d;
    };

    // Basic WS90 telemetry
    g_state.battery_mV    = get_num("battery_mV");
    g_state.battery_ok    = get_num("battery_ok");
    g_state.id            = get_int("id");
    g_state.model         = get_str("model");
    g_state.firmware      = get_int("firmware");
    g_state.humidity      = get_num("humidity");
    g_state.temperature_C = get_num("temperature_C");
    g_state.wind_dir_deg  = get_num("wind_dir_deg");
    g_state.wind_avg_m_s  = get_num("wind_avg_m_s");
    g_state.wind_max_m_s  = get_num("wind_max_m_s");
    g_state.light_lux     = get_num("light_lux");
    g_state.uvi           = get_num("uvi");
    g_state.rain_mm       = get_num("rain_mm");
    g_state.supercap_V    = get_num("supercap_V");
    g_state.last_time_iso = get_str("time");

    if (!j.contains("rain_mm")) {
        g_state.last_update = now;
        return;
    }

    double rain_mm = j["rain_mm"].get<double>();
    if (rain_mm < 0 || rain_mm > 20000) {
        g_state.last_update = now;
        return;
    }

    rollover_if_needed(g_state, now);

    // Track coverage of valid WS90 samples for the current day
    if (g_state.day_first_ts == 0) {
        g_state.day_first_ts = now;
    }
    g_state.day_last_ts = now;

    // First valid rain sample since boot / state reset
    if (g_state.last_rain_mm == 0.0) {
        g_state.last_rain_mm = rain_mm;
        g_state.last_update  = now;
        save_state(g_state);
        return;
    }

    // Rain accumulation based on delta
    double delta = rain_mm - g_state.last_rain_mm;
    if (delta > 0.0001 && delta < 5000) {
        double di = inches_from_mm(delta);

        g_state.rain_daily_in   += di;
        g_state.rain_monthly_in += di;
        g_state.rain_yearly_in  += di;
        g_state.rain_weekly_in  += di;

        // rolling 1-hour rainfall
        g_state.deltas.push_back({now, di});
        recompute_hourly(g_state, now);

        // event tracking
        if (g_state.last_rain_ts == 0 ||
            (now - g_state.last_rain_ts) > EVENT_GAP_MIN * 60) {
            g_state.rain_event_in = 0.0;
        }

        g_state.rain_event_in += di;
        g_state.last_rain_ts   = now;
    }

    g_state.last_rain_mm = rain_mm;
    g_state.last_update  = now;

    // High/low temperature tracking
    double tC = get_num("temperature_C", NAN);
    if (!std::isnan(tC)) {
        if (!g_state.have_temp) {
            g_state.temp_high_c = tC;
            g_state.temp_low_c  = tC;
            g_state.have_temp   = true;
        } else {
            g_state.temp_high_c = std::max(g_state.temp_high_c, tC);
            g_state.temp_low_c  = std::min(g_state.temp_low_c,  tC);
        }
    }

    // High/low humidity tracking
    double h = get_num("humidity", NAN);
    if (!std::isnan(h)) {
        if (!g_state.have_hum) {
            g_state.hum_high = h;
            g_state.hum_low  = h;
            g_state.have_hum = true;
        } else {
            g_state.hum_high = std::max(g_state.hum_high, h);
            g_state.hum_low  = std::min(g_state.hum_low,  h);
        }
    }

    // High/low wind tracking (daily mean and max gust)
    if (!std::isnan(g_state.wind_avg_m_s) && !std::isnan(g_state.wind_max_m_s)) {
        if (!g_state.have_wind) {
            g_state.have_wind         = true;
            g_state.wind_mean_m_s     = g_state.wind_avg_m_s;
            g_state.wind_max_gust_m_s = g_state.wind_max_m_s;
            g_state.wind_sample_count = 1;
        } else {
            std::uint64_t n = g_state.wind_sample_count;
            g_state.wind_mean_m_s =
                ((g_state.wind_mean_m_s * static_cast<double>(n)) + g_state.wind_avg_m_s)
                / static_cast<double>(n + 1);
            g_state.wind_sample_count = n + 1;

            if (g_state.wind_max_m_s > g_state.wind_max_gust_m_s) {
                g_state.wind_max_gust_m_s = g_state.wind_max_m_s;
            }
        }
    }

    save_state(g_state);
}

// =========================================
// Poller thread
// =========================================

struct MemChunk {
    char  *data = nullptr;
    size_t size = 0;
};

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    MemChunk *m = static_cast<MemChunk*>(userp);

    if (m->size + realsize >= MAX_BODY_SIZE - 1)
        realsize = MAX_BODY_SIZE - 1 - m->size;

    if (realsize == 0)
        return 0;

    char *ptr = static_cast<char*>(realloc(m->data, m->size + realsize + 1));
    if (!ptr)
        return 0;

    m->data = ptr;
    memcpy(m->data + m->size, contents, realsize);
    m->size += realsize;
    m->data[m->size] = 0;

    return realsize;
}

static void poller_thread_func()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    while (g_running.load()) {
        CURL *c = curl_easy_init();
        if (c) {
            MemChunk chunk;

            curl_easy_setopt(c, CURLOPT_URL, WS90_URL);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, (void*)&chunk);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
            curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);  // <-- IMPORTANT: let us see 503 + body
            curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

            CURLcode   res       = curl_easy_perform(c);
            std::time_t now      = std::time(nullptr);
            long       http_code = 0;

            if (res == CURLE_OK) {
                curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
            }

            {
                std::lock_guard<std::mutex> guard(g_lock);

                g_ws90_last_poll   = now;
                g_ws90_http_status = http_code;

                if (res != CURLE_OK) {
                    // Transport-level failure: ws90 likely crashed / unreachable
                    g_ws90_http_ok      = false;
                    g_rtlsdr_ok         = false;
                    g_ws90_error_code   = "curl_error";
                    g_ws90_error_msg    = curl_easy_strerror(res);
                } else if (http_code == 200 && chunk.size > 0) {
                    // Normal good sample
                    try {
                        json j = json::parse(chunk.data);
                        process_ws90_json_locked(j);

                        g_ws90_http_ok    = true;
                        g_rtlsdr_ok       = true;   // ws90 + SDR both look alive
                        g_ws90_error_code.clear();
                        g_ws90_error_msg.clear();
                    } catch (...) {
                        g_ws90_http_ok    = true;   // HTTP worked
                        g_rtlsdr_ok       = false;  // but payload is garbage
                        g_ws90_error_code = "parse_error";
                        g_ws90_error_msg  = "invalid JSON from ws90";
                    }
                } else {
                    // HTTP error from ws90: try to parse {"error": "...", "message": "..."}
                    std::string err_code;
                    std::string err_msg;

                    if (chunk.size > 0) {
                        try {
                            json ej = json::parse(chunk.data);
                            if (ej.contains("error") && ej["error"].is_string())
                                err_code = ej["error"].get<std::string>();
                            if (ej.contains("message") && ej["message"].is_string())
                                err_msg = ej["message"].get<std::string>();
                        } catch (...) {
                            err_msg = "non-200 from ws90 with non-JSON body";
                        }
                    }

                    g_ws90_http_ok = (http_code != 0);

                    // Classify what likely died
                    if (http_code == 503 && err_code == "stale_data") {
                        // ws90 still answering; RTL-SDR stream stalled or dead
                        g_rtlsdr_ok = false;
                    } else if (http_code == 503 && err_code == "no_data") {
                        // startup / no samples yet; SDR may not be feeding yet
                        g_rtlsdr_ok = false;
                    } else {
                        // Unknown HTTP error; be conservative
                        g_rtlsdr_ok = false;
                    }

                    if (!err_code.empty())
                        g_ws90_error_code = err_code;
                    else
                        g_ws90_error_code = "http_" + std::to_string(http_code);

                    g_ws90_error_msg = err_msg;
                }
            }

            curl_easy_cleanup(c);
            if (chunk.data) free(chunk.data);
        }

        std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SEC));
    }

    curl_global_cleanup();
}


// =========================================
// API JSON BUILD
// =========================================

namespace state_v2 {

void init() {
    load_config();
    load_state(get_state_path(), g_state);
    init_db();
    std::thread p(poller_thread_func);
    p.detach();
}

json build_current_json()
{
    json out;

    out["api_version"] = "2.1.0";

    out["battery_mV"]      = g_state.battery_mV;
    out["battery_ok"]      = g_state.battery_ok;
    out["id"]              = g_state.id;
    out["model"]           = g_state.model;
    out["firmware"]        = g_state.firmware;

    out["humidity"]        = g_state.humidity;
    out["temperature_F"]   = g_state.temperature_C * 9.0/5.0 + 32.0;
    out["wind_dir_deg"]    = g_state.wind_dir_deg;
    out["wind_avg_m_s"]    = g_state.wind_avg_m_s;
    out["wind_max_m_s"]    = g_state.wind_max_m_s;
    out["light_lux"]       = g_state.light_lux;
    out["uvi"]             = g_state.uvi;
    out["supercap_V"]      = g_state.supercap_V;
    out["time"]            = g_state.last_time_iso;

    nlohmann::json astro = compute_solar_and_moon(std::time(nullptr));
    out["astro"] = astro;

    json rain;
    rain["daily_in"]    = g_state.rain_daily_in;
    rain["event_in"]    = g_state.rain_event_in;
    rain["hourly_in"]   = g_state.rain_hourly_in;
    rain["weekly_in"]   = g_state.rain_weekly_in;
    rain["monthly_in"]  = g_state.rain_monthly_in;
    rain["yearly_in"]   = g_state.rain_yearly_in;

    double total_in = g_state.historical_total_in;
    if (g_state.rain_yearly_in > g_state.historical_yearly_in)
        total_in += (g_state.rain_yearly_in - g_state.historical_yearly_in);

    rain["total_in"] = total_in;
    out["rain"] = rain;

    json daily;
    if (g_state.have_temp) {
        daily["temp_high_F"] = g_state.temp_high_c * 9.0/5.0 + 32.0;
        daily["temp_low_F"]  = g_state.temp_low_c  * 9.0/5.0 + 32.0;
    } else {
        daily["temp_high_F"] = nullptr;
        daily["temp_low_F"]  = nullptr;
    }

    if (g_state.have_hum) {
        daily["humidity_high"] = g_state.hum_high;
        daily["humidity_low"]  = g_state.hum_low;
    } else {
        daily["humidity_high"] = nullptr;
        daily["humidity_low"]  = nullptr;
    }

    // Daily wind summary (mean and max gust, mph)
    if (g_state.have_wind) {
        daily["wind_mean_mph"]     = g_state.wind_mean_m_s * 2.2369;
        daily["wind_gust_max_mph"] = g_state.wind_max_gust_m_s * 2.2369;
    } else {
        daily["wind_mean_mph"]     = nullptr;
        daily["wind_gust_max_mph"] = nullptr;
    }

    daily["meaningful"] = (g_state.have_temp || g_state.have_hum || g_state.have_wind);
    out["daily"] = daily;

    // out["rain_daily_in"]   = g_state.rain_daily_in;
    // out["rain_event_in"]   = g_state.rain_event_in;
    // out["rain_hourly_in"]  = g_state.rain_hourly_in;
    // out["rain_weekly_in"]  = g_state.rain_weekly_in;
    // out["rain_monthly_in"] = g_state.rain_monthly_in;
    // out["rain_yearly_in"]  = g_state.rain_yearly_in;

    std::time_t now   = std::time(nullptr);
    int         age   = g_state.last_update ? (int)(now - g_state.last_update) : -1;
    bool        stale = (g_state.last_update != 0 && age > 60);  // adjust threshold to taste

    json ws;
    ws["http_ok"]        = g_ws90_http_ok;
    ws["rtlsdr_ok"]      = g_rtlsdr_ok;
    ws["last_poll_ts"]   = (long long)g_ws90_last_poll;
    ws["last_update_ts"] = (long long)g_state.last_update;
    ws["age_sec"]        = age;
    ws["stale"]          = stale;
    ws["http_status"]    = g_ws90_http_status;

    if (!g_ws90_error_code.empty())
        ws["error"] = g_ws90_error_code;
    if (!g_ws90_error_msg.empty())
        ws["error_message"] = g_ws90_error_msg;

    out["ws90_status"] = ws;

    return out;
}

std::string current_weather_json() {
    std::lock_guard<std::mutex> g(g_lock);
    return build_current_json().dump();
}

std::string history_temperature_json(int days, int limit, int offset) {
    json out;
    out["days"] = json::array();
    if (!g_db) return out.dump();

    // Modes:
    //  simple:     no params => full history, no filter, no limit
    //  time_only:  days only => time filter, no limit/offset
    //  paged:      any limit/offset => paging, optional time filter
    bool simple    = (days < 0 && limit < 0 && offset < 0);
    bool time_only = (days >= 0 && limit < 0 && offset < 0);
    bool paged     = !simple && !time_only;

    std::string sql;

    if (simple) {
        sql =
            "SELECT day_ts, temp_high_c, temp_low_c "
            "FROM daily_weather "
            "ORDER BY day_ts";

    } else if (time_only) {
        if (days == 0) {
            // days=0 with no limit/offset -> same as simple
            sql =
                "SELECT day_ts, temp_high_c, temp_low_c "
                "FROM daily_weather "
                "ORDER BY day_ts";
        } else {
            // days only -> time filter, no limit/offset
            sql =
                "SELECT day_ts, temp_high_c, temp_low_c "
                "FROM daily_weather "
                "WHERE day_ts >= ?1 "
                "ORDER BY day_ts";
        }

    } else { // paged
        bool use_since = (days > 0);

        if (use_since) {
            sql =
                "SELECT day_ts, temp_high_c, temp_low_c "
                "FROM daily_weather "
                "WHERE day_ts >= ?1 "
                "ORDER BY day_ts "
                "LIMIT ?2 OFFSET ?3";
        } else {
            sql =
                "SELECT day_ts, temp_high_c, temp_low_c "
                "FROM daily_weather "
                "ORDER BY day_ts "
                "LIMIT ?1 OFFSET ?2";
        }
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return out.dump();

    // Bind params only where needed
    if (time_only && days > 0) {
        std::time_t now_ts   = std::time(nullptr);
        std::time_t since_ts = now_ts - static_cast<std::time_t>(days) * 86400;
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(since_ts));

    } else if (paged) {
        int bind_idx = 1;
        bool use_since = (days > 0);

        if (use_since) {
            std::time_t now_ts   = std::time(nullptr);
            std::time_t since_ts = now_ts - static_cast<std::time_t>(days) * 86400;

            sqlite3_bind_int64(stmt, bind_idx++, static_cast<sqlite3_int64>(since_ts));
            sqlite3_bind_int(stmt,   bind_idx++, limit);
            sqlite3_bind_int(stmt,   bind_idx++, offset);
        } else {
            sqlite3_bind_int(stmt, bind_idx++, limit);
            sqlite3_bind_int(stmt, bind_idx++, offset);
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::time_t ts = sqlite3_column_int64(stmt, 0);
        double hi = (sqlite3_column_type(stmt,1)==SQLITE_NULL)
                    ? NAN : sqlite3_column_double(stmt,1);
        double lo = (sqlite3_column_type(stmt,2)==SQLITE_NULL)
                    ? NAN : sqlite3_column_double(stmt,2);

        json row;
        row["day"] = static_cast<long long>(ts);

        if (std::isnan(hi) || std::isnan(lo)) {
            row["temp_high_F"] = nullptr;
            row["temp_low_F"]  = nullptr;
        } else {
            double hiF = hi * 9.0/5.0 + 32.0;
            double loF = lo * 9.0/5.0 + 32.0;
            row["temp_high_F"] = hiF;
            row["temp_low_F"]  = loF;
        }

        out["days"].push_back(row);
    }

    sqlite3_finalize(stmt);
    return out.dump();
}

std::string history_humidity_json(int days, int limit, int offset) {
    json out;
    out["days"] = json::array();
    if (!g_db) return out.dump();

    bool simple    = (days < 0 && limit < 0 && offset < 0);
    bool time_only = (days >= 0 && limit < 0 && offset < 0);
    bool paged     = !simple && !time_only;

    std::string sql;

    if (simple) {
        sql =
            "SELECT day_ts, humidity_high, humidity_low "
            "FROM daily_weather "
            "ORDER BY day_ts";

    } else if (time_only) {
        if (days == 0) {
            sql =
                "SELECT day_ts, humidity_high, humidity_low "
                "FROM daily_weather "
                "ORDER BY day_ts";
        } else {
            sql =
                "SELECT day_ts, humidity_high, humidity_low "
                "FROM daily_weather "
                "WHERE day_ts >= ?1 "
                "ORDER BY day_ts";
        }

    } else { // paged
        bool use_since = (days > 0);

        if (use_since) {
            sql =
                "SELECT day_ts, humidity_high, humidity_low "
                "FROM daily_weather "
                "WHERE day_ts >= ?1 "
                "ORDER BY day_ts "
                "LIMIT ?2 OFFSET ?3";
        } else {
            sql =
                "SELECT day_ts, humidity_high, humidity_low "
                "FROM daily_weather "
                "ORDER BY day_ts "
                "LIMIT ?1 OFFSET ?2";
        }
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return out.dump();

    if (time_only && days > 0) {
        std::time_t now_ts   = std::time(nullptr);
        std::time_t since_ts = now_ts - static_cast<std::time_t>(days) * 86400;
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(since_ts));

    } else if (paged) {
        int bind_idx = 1;
        bool use_since = (days > 0);

        if (use_since) {
            std::time_t now_ts   = std::time(nullptr);
            std::time_t since_ts = now_ts - static_cast<std::time_t>(days) * 86400;

            sqlite3_bind_int64(stmt, bind_idx++, static_cast<sqlite3_int64>(since_ts));
            sqlite3_bind_int(stmt,   bind_idx++, limit);
            sqlite3_bind_int(stmt,   bind_idx++, offset);
        } else {
            sqlite3_bind_int(stmt, bind_idx++, limit);
            sqlite3_bind_int(stmt, bind_idx++, offset);
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::time_t ts = sqlite3_column_int64(stmt, 0);
        double hi = (sqlite3_column_type(stmt,1)==SQLITE_NULL)
                    ? NAN : sqlite3_column_double(stmt,1);
        double lo = (sqlite3_column_type(stmt,2)==SQLITE_NULL)
                    ? NAN : sqlite3_column_double(stmt,2);

        json row;
        row["day"] = static_cast<long long>(ts);

        if (std::isnan(hi) || std::isnan(lo)) {
            row["humidity_high"] = nullptr;
            row["humidity_low"]  = nullptr;
        } else {
            row["humidity_high"] = hi;
            row["humidity_low"]  = lo;
        }

        out["days"].push_back(row);
    }

    sqlite3_finalize(stmt);
    return out.dump();
}

std::string history_rain_json(int days, int limit, int offset) {
    json out;
    out["days"] = json::array();
    if (!g_db) return out.dump();

    bool simple    = (days < 0 && limit < 0 && offset < 0);
    bool time_only = (days >= 0 && limit < 0 && offset < 0);
    bool paged     = !simple && !time_only;

    std::string sql;

    if (simple) {
        sql =
            "SELECT day_ts, rain_in "
            "FROM daily_weather "
            "ORDER BY day_ts";

    } else if (time_only) {
        if (days == 0) {
            sql =
                "SELECT day_ts, rain_in "
                "FROM daily_weather "
                "ORDER BY day_ts";
        } else {
            sql =
                "SELECT day_ts, rain_in "
                "FROM daily_weather "
                "WHERE day_ts >= ?1 "
                "ORDER BY day_ts";
        }

    } else { // paged
        bool use_since = (days > 0);

        if (use_since) {
            sql =
                "SELECT day_ts, rain_in "
                "FROM daily_weather "
                "WHERE day_ts >= ?1 "
                "ORDER BY day_ts "
                "LIMIT ?2 OFFSET ?3";
        } else {
            sql =
                "SELECT day_ts, rain_in "
                "FROM daily_weather "
                "ORDER BY day_ts "
                "LIMIT ?1 OFFSET ?2";
        }
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return out.dump();

    if (time_only && days > 0) {
        std::time_t now_ts   = std::time(nullptr);
        std::time_t since_ts = now_ts - static_cast<std::time_t>(days) * 86400;
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(since_ts));

    } else if (paged) {
        int bind_idx = 1;
        bool use_since = (days > 0);

        if (use_since) {
            std::time_t now_ts   = std::time(nullptr);
            std::time_t since_ts = now_ts - static_cast<std::time_t>(days) * 86400;

            sqlite3_bind_int64(stmt, bind_idx++, static_cast<sqlite3_int64>(since_ts));
            sqlite3_bind_int(stmt,   bind_idx++, limit);
            sqlite3_bind_int(stmt,   bind_idx++, offset);
        } else {
            sqlite3_bind_int(stmt, bind_idx++, limit);
            sqlite3_bind_int(stmt, bind_idx++, offset);
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::time_t ts = sqlite3_column_int64(stmt, 0);

        int rain_type = sqlite3_column_type(stmt, 1);
        if (rain_type == SQLITE_NULL) {
            // No rain data for this day - skip it
            continue;
        }

        double r = sqlite3_column_double(stmt, 1);

        json row;
        row["day"]     = static_cast<long long>(ts);
        row["rain_in"] = r;

        out["days"].push_back(row);
    }

    sqlite3_finalize(stmt);
    return out.dump();
}


} // namespace state_v2
