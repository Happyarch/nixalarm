#pragma once

// Low-precision solar/lunar ephemeris: pure trig, no SDL/rendering dependency,
// so it is unit-testable in isolation and reusable by any rendering backend.
// All angles are degrees at the API boundary; radians are used internally only.
//
// Sources: the standard "low precision" solar position formula (mean longitude,
// mean anomaly, the +1.915*sin(g)+0.020*sin(2g) equation-of-center correction,
// linear obliquity), as documented at en.wikipedia.org/wiki/Position_of_the_Sun
// and aa.usno.navy.mil/faq/sun_approx; equation of time via the Meeus 28.1 form
// EoT = 4*(L0 - 0.0057183 - RA). Lunar position via Paul Schlyter's low-precision
// two-body-plus-leading-perturbations formula (stjarnhimlen.se/comp/ppcomp.html),
// good to a few arcminutes -- adequate for driving a dial gnomon's shadow, not
// suitable for precision astronomy.

#include <ctime>

struct EquatorialCoord {
  double ra_deg = 0.0;
  double dec_deg = 0.0;
};

struct HorizontalCoord {
  double azimuth_deg = 0.0;   // measured from north, clockwise (east-positive)
  double altitude_deg = 0.0;  // above the horizon
};

struct SolarPositionResult {
  EquatorialCoord eq;
  double equation_of_time_minutes = 0.0;  // apparent solar time - mean solar time
};

// Julian Day Number (including fraction of day) for a UTC calendar date/time.
// tm_year is years since 1900, tm_mon is 0-11, per struct tm convention.
double julian_day(const std::tm& utc_tm);

// Julian centuries since J2000.0 (JD 2451545.0).
double julian_centuries(double jd);

// True equatorial position of the Sun, plus the equation of time for that instant.
SolarPositionResult solar_position_full(double jd);

// True equatorial position of the Moon (low precision).
EquatorialCoord lunar_position(double jd);

// Local (apparent) sidereal time, in hours [0,24), from JD (UTC) and longitude
// (signed degrees, +E/-W).
double local_sidereal_time_hours(double jd, double longitude_deg);

// Converts an equatorial coordinate to horizontal (az/alt) for an observer at
// the given latitude and local sidereal time.
HorizontalCoord equatorial_to_horizontal(EquatorialCoord eq, double lat_deg, double lst_hours);

// Fraction of the Moon's disc that is lit, 0 (new) to 1 (full), from the
// Sun-Moon elongation as seen from Earth. Geocentric and ignoring the small
// parallax and phase-angle refinements -- enough to tell a bright gibbous from
// a useless crescent, which is what a moondial cares about.
double moon_illuminated_fraction(double jd);
