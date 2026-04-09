/* ---------------------------------------------------------------------------------------------------------------------
 * lunar.h
 * Header file for lunar library
 *  https://github.com/ciroque/lunar
 * Calculate functions are based on https://github.com/mourner/suncalc/blob/master/suncalc.js
 *
 */

#ifndef LUNAR_LUNAR_H
#define LUNAR_LUNAR_H

#include <array>
#include <string>
#include <cmath>

// Describes the visible segment of the moon.
enum Segment {
    New = 0,
    WaxingCrescent,
    FirstQuarter,
    WaxingGibbous,
    Full,
    WaningGibbous,
    ThirdQuarter,
    WaningCrescent,
};

// Describes a specific phase of the moon.
struct Phase {
    int julianDay;
    double phase;
    enum Segment segment;
    double visible;
};

// Used in intermediate calculations.
// Describes the well, position of a celestial body.
struct Position {
    double declination;
    double distance;
    double rightAscension;
};

// Describes the visible part of the moon.
struct Illumination {
    double angle;
    double phase;
    double visible;
};

class Lunar {
public:
    constexpr static const int EPOCH = 2444238; // [2444238.5] We do not need a fractional value here as we are not calculating intra-day values

    // Calculates the astronomical Julian day for the current date.
    // Params:
    // Return:
    //  An integer value representing the Julian day.
    static int CalculateJulianDay();

    // Calculates the astronomical Julian day for the specified date.
    // Params:
    //  - int year: the year to be used in the calculation
    //  - int month: the month to be used in the calculation
    //  - int day: the day to be used in the calculation
    // Return:
    //  An integer value representing the Julian day.
    static int CalculateJulianDay(int year, int month, double day);

    // Calculates the phase of the moon for the current date.
    // Params:
    // Return:
    //  A Phase instance containing the details of the moon's phase.
    static Phase CalculateMoonPhase();

    // Calculates the phase of the moon for the specified Julian date.
    // Params:
    //  -int julianDay: the Julian day to use in the calculation
    // Return:
    //  A Phase instance containing the details of the moon's phase.
    static Phase CalculateMoonPhase(int julianDay);

    // Calculates the phase of the moon for the specified Julian date.
    // Params:
    //  - int year: the year to be used in the calculation
    //  - int month: the month to be used in the calculation
    //  - int day: the day to be used in the calculation
    // Return:
    //  A Phase instance containing the details of the moon's phase.
    static Phase CalculateMoonPhase(int year, int month, double day);

    // Looks up the textual description of the given moon segment.
    // Params:
    //  - int segment: the segment designating which name should be returned
    // Return:
    //  A string representation of the segment
    static std::string GetSegmentName(int segment);

private:
    constexpr static const int MILLENNIUM_EPOCH = 2451545;
    constexpr static const double PI = 3.1415926535897932384626433832795;
    constexpr static const double RAD = (PI / 180.0);
    constexpr static const double EARTH_OBLIQUITY = RAD * 23.4397;
    constexpr static const double DISTANCE_FROM_EARTH_TO_SUN = 149598000;

    static Position CalculateSolarCoordinates(int julianDay);
    static Position CalculateLunarCoordinates(int julianDay);
    static Illumination CalculateIllumination(int julianDay);

    static inline double CalculateEclipticLongitude(double solarMeanAnomaly) {
        auto center = RAD * (1.9148 * sin(solarMeanAnomaly) + 0.02
            * sin(2 * solarMeanAnomaly) + 0.0003 * sin(3 * solarMeanAnomaly));
        auto earthPerihelion = RAD * 102.9372;

        return solarMeanAnomaly + center + earthPerihelion + PI;
    }

    static inline double CalculateDeclination(double l, double b) {
        return std::asin(std::sin(b)
            * std::cos(EARTH_OBLIQUITY) + std::cos(b)
            * std::sin(EARTH_OBLIQUITY) * std::sin(l));
    }

    static inline double CalculateRightAscension(double l, double b) {
        return std::atan2(std::sin(l)
            * std::cos(EARTH_OBLIQUITY) - std::tan(b)
            * std::sin(EARTH_OBLIQUITY), std::cos(l));
    }
};

#endif //LUNAR_LUNAR_H
