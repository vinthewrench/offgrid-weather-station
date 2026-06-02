#ifndef ASTRO_HPP
#define ASTRO_HPP

#include <ctime>
#include "json.hpp"

// The backend exposes one function that produces:
//   - UTC sunrise/sunset timestamps
//   - Civil twilight UTC timestamps
//   - Moon phase (0..1), visible fraction (0..1), segment string
//   - Config-derived gmt_offset, latitude, longitude, timezone name
//
// All timestamps are Unix UTC.

nlohmann::json compute_solar_and_moon(std::time_t now);

#endif // ASTRO_HPP
