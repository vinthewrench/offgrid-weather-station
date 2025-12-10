/*
 * adapted from: http://www.voidware.com/phase.c
 * CalculateJulianDate from: https://www.programmingassignmenthelper.com/convert-a-date-to-the-julian-day-number-in-c/
 */
#include "lunar.hpp"

#include <cmath>

#include <ctime>

int Lunar::CalculateJulianDay() {
    const int BASE_YEAR = 1900;
    time_t ttime = time(0);
    tm *local_time = localtime(&ttime);

    return CalculateJulianDay(local_time->tm_year + BASE_YEAR, local_time->tm_mon + 1, local_time->tm_mday);
}

int Lunar::CalculateJulianDay(int year, int month, double day) {
    int a, m, y, leap_days;
    a = ((14 - month) / 12);
    m = (month - 3) + (12 * a);
    y = year + 4800 - a;
    leap_days = (y / 4) - (y / 100) + (y / 400);
    return (int) (day + (((153 * m) + 2.0) / 5.0) + (365 * y) + leap_days - 32045);
}

Phase Lunar::CalculateMoonPhase() {
    auto julianDay = CalculateJulianDay();
    return CalculateMoonPhase(julianDay);
}

Phase Lunar::CalculateMoonPhase(int julianDay) {
    auto illumination = CalculateIllumination(julianDay);

    return Phase {
            .julianDay = julianDay,
            .phase = illumination.phase,
            .segment = (Segment) (int) (illumination.phase * 8),
            .visible = illumination.visible,
    };
}

Phase Lunar::CalculateMoonPhase(int year, int month, double day) {
    auto julianDay = CalculateJulianDay(year, month, day);
    return CalculateMoonPhase(julianDay);
}

std::string Lunar::GetSegmentName(int segment) {
    const std::array<std::string, 8> segmentNames = {
            "New",
            "Waxing Crescent",
            "First Quarter",
            "Waxing Gibbous",
            "Full",
            "Waning Gibbous",
            "Third Quarter",
            "Waning Crescent"
    };
    return segmentNames.at(segment);
}

Position Lunar::CalculateSolarCoordinates(int julianDay) {
    auto solarMeanAnomaly = RAD * (357.5291 + 0.98560028 * julianDay);
    auto eclipticLongitude = CalculateEclipticLongitude(solarMeanAnomaly);

    return Position {
        .declination =  CalculateDeclination(eclipticLongitude, 0),
        .distance = 0.0,
        .rightAscension = CalculateRightAscension(eclipticLongitude, 0)
    };
}

Position Lunar::CalculateLunarCoordinates(int julianDay) {
    auto eclipticLongitude = RAD * (218.316 + 13.176396 * julianDay);
    auto meanAnomaly = RAD * (134.963 + 13.064993 * julianDay);
    auto meanDistance = RAD * (93.272 + 13.229350 * julianDay);

    auto longitude = eclipticLongitude + RAD * 6.289 * std::sin(meanAnomaly);
    auto latitude = RAD * 5.128 * std::sin(meanDistance);
    auto distance = 385001 - 20905 * std::cos(meanAnomaly);

    return Position {
        .declination = CalculateDeclination(longitude, latitude),
        .distance = distance,
        .rightAscension = CalculateRightAscension(longitude, latitude)
    };
}

Illumination Lunar::CalculateIllumination(int julianDay) {
    auto day = julianDay - MILLENNIUM_EPOCH;
    auto solarCoordinates = CalculateSolarCoordinates(day);
    auto lunarCoordinates = CalculateLunarCoordinates(day);

    auto phi = std::acos(std::sin(solarCoordinates.declination)
        * std::sin(lunarCoordinates.declination)
        + std::cos(solarCoordinates.declination)
        * std::cos(lunarCoordinates.declination)
        * std::cos(solarCoordinates.rightAscension - lunarCoordinates.rightAscension));
    auto inc = std::atan2(DISTANCE_FROM_EARTH_TO_SUN
        * std::sin(phi),
        lunarCoordinates.distance - DISTANCE_FROM_EARTH_TO_SUN * cos(phi));
    auto angle = std::atan2(
        cos(solarCoordinates.declination) * sin(solarCoordinates.rightAscension - lunarCoordinates.rightAscension),
        sin(solarCoordinates.declination) * cos(lunarCoordinates.declination)
        - cos(solarCoordinates.declination) * sin(lunarCoordinates.declination)
        * cos(solarCoordinates.rightAscension - lunarCoordinates.rightAscension));

    return Illumination {
        .angle = angle,
        .phase = 0.5 + 0.5 * inc * (angle < 0 ? -1 : 1) / PI,
        .visible = (1 + std::cos(inc)) / 2
    };
}
