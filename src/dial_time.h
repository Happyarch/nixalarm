#pragma once

// What time a dial READS, and what it takes to make it read something else.
//
// Left alone, a sundial tells apparent solar time -- the sun's own time, which
// runs ahead of and behind a clock by up to about a quarter of an hour over
// the year (the equation of time), and is offset again by where the observer
// sits relative to their time zone's standard meridian. Correcting for that is
// what a heliochronometer does mechanically; here it is a shift applied to the
// light's hour angle, which is the one quantity the hour lines are a function
// of, so the shadow moves along the plate by exactly the correction and
// nothing else about the scene changes.
//
// Pure time/angle math with no rendering or SDL dependency, so it is testable
// without a window -- same rationale as dial_scene and dial_engrave.

#include "astro.h"

// Wraps an hour-of-day to [0, 24).
double wrap_hours_24(double hours);

// Wraps a DIFFERENCE between times to (-12, +12], so a correction always takes
// the short way round rather than 23 hours the wrong way.
double wrap_hours_pm12(double hours);

// The hour line a body of right ascension `ra_deg` currently stands over, in
// [0, 24) -- i.e. what the dial reads, uncorrected. The moon dial's hour lines
// are laid out half a turn out of phase (a full moon transits at midnight, see
// idealized_light_direction_world), so `moondial` undoes that shift.
double dial_reading_hours(bool moondial, double ra_deg, double lst_hours);

// Local mean solar time: apparent solar time less the equation of time. This
// is the sun's average day at the observer's own longitude -- still not civil
// time unless they sit on their zone's standard meridian and keep no daylight
// saving.
double local_mean_solar_time_hours(double jd, double lst_hours);

// The shift to add to a light's hour angle (expressed in hours) for a dial
// currently reading `reading_hours` to read `target_hours` instead.
double timebase_shift_hours(double target_hours, double reading_hours);
