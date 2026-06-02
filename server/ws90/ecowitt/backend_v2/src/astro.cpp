#include "astro.hpp"
#include "config.hpp"
#include "sunset.h"
#include "lunar.hpp"

#include <ctime>
#include <cmath>
#include <string>

using nlohmann::json;

// Convert "minutes after UTC midnight" to absolute Unix timestamp
static time_t utc_minutes_to_ts(time_t now, double mins)
{
    std::tm gm = {};
    gmtime_r(&now, &gm);
    gm.tm_hour = 0;
    gm.tm_min  = 0;
    gm.tm_sec  = 0;
    time_t midnight_utc = timegm(&gm);
    return midnight_utc + (time_t)(mins * 60.0);
}

json compute_solar_and_moon(time_t now)
{
    json out;

    // -------------------------------
    // Config (UTC always)
    // -------------------------------
    double lat = g_cfg.latitude;
    double lon = g_cfg.longitude;

    // -------------------------------
    // Sunrise / sunset (UTC)
    // -------------------------------
    SunSet ss;
    ss.setPosition(lat, lon, 0);

    std::tm gm = {};
    gmtime_r(&now, &gm);
    ss.setCurrentDate(gm.tm_year + 1900, gm.tm_mon + 1, gm.tm_mday);

    double sr_utc_min  = ss.calcSunriseUTC();
    double ss_utc_min  = ss.calcSunsetUTC();
    double csr_utc_min = ss.calcCivilSunrise();
    double css_utc_min = ss.calcCivilSunset();

    time_t sunrise_ts     = utc_minutes_to_ts(now, sr_utc_min);
    time_t sunset_ts      = utc_minutes_to_ts(now, ss_utc_min);
    time_t civil_rise_ts  = utc_minutes_to_ts(now, csr_utc_min);
    time_t civil_set_ts   = utc_minutes_to_ts(now, css_utc_min);

    // -------------------------------
    // Moon phase (your real API)
    // -------------------------------
    int jday = Lunar::CalculateJulianDay(
        gm.tm_year + 1900,
        gm.tm_mon + 1,
        (double)gm.tm_mday
    );

    Phase ph = Lunar::CalculateMoonPhase(jday);
    std::string seg = Lunar::GetSegmentName(ph.segment);

    // Day lengths
       int length_of_day_sec     = (sunset_ts > sunrise_ts)
                                   ? (int)(sunset_ts - sunrise_ts)
                                   : 0;

       int length_of_visible_sec = (civil_set_ts > civil_rise_ts)
                                   ? (int)(civil_set_ts - civil_rise_ts)
                                   : 0;


    // -------------------------------
    // JSON output
    // -------------------------------
    json sun;
    sun["sunrise_ts"]       = sunrise_ts;
    sun["sunset_ts"]        = sunset_ts;
    sun["civil_sunrise_ts"] = civil_rise_ts;
    sun["civil_sunset_ts"]  = civil_set_ts;

    sun["length_of_day_sec"]   = length_of_day_sec;
    sun["length_of_visible_sec"] = length_of_visible_sec;

    json moon;
    moon["julian_day"] = ph.julianDay;
    moon["phase"]      = ph.phase;
    moon["segment"]    = seg;
    moon["visible"]    = ph.visible;

    out["gmt_offset"]  = 0;
    out["midnight_ts"] = utc_minutes_to_ts(now, 0);
    out["time_zone"]   = "UTC";
    out["sun"]         = sun;
    out["moon"]        = moon;

    // If you want to hide these, keep them commented
    // out["latitude"] = lat;
    // out["longitude"] = lon;

    return out;
}
