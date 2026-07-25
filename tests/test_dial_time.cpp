// Standalone assert-based tests for dial_time.cpp -- matches this project's
// minimal-dependency test style (see tests/test_dial_geometry.cpp).

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <ctime>

#include "astro.h"
#include "dial_time.h"

namespace {

int g_failures = 0;

void expect_true(bool cond, const char* what) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s\n", what);
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void expect_near(double actual, double expected, double tol, const char* what) {
  if (std::fabs(actual - expected) > tol) {
    std::fprintf(stderr, "FAIL %s: expected %.6f, got %.6f (tol %.6f)\n", what, expected, actual, tol);
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

double test_jd(int year, int month, int day, int hour) {
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  return julian_day(tm);
}

void test_wrapping() {
  expect_near(wrap_hours_24(25.5), 1.5, 1e-12, "24h wrap folds past midnight");
  expect_near(wrap_hours_24(-1.5), 22.5, 1e-12, "24h wrap folds negatives forward");
  expect_near(wrap_hours_pm12(13.0), -11.0, 1e-12, "a 13h correction is really -11h");
  expect_near(wrap_hours_pm12(-13.0), 11.0, 1e-12, "and symmetrically the other way");
  expect_true(std::fabs(wrap_hours_pm12(23.9)) <= 12.0, "corrections never exceed half a turn");
}

// The dial reads noon when the body is on the meridian -- that is what makes
// hour angle and hour line the same quantity.
void test_reading_is_noon_on_the_meridian() {
  // Body at ra=0 with lst=0 has hour angle 0: on the meridian.
  expect_near(dial_reading_hours(false, 0.0, 0.0), 12.0, 1e-12, "sun dial reads XII on the meridian");
  // An hour of hour angle is an hour on the dial.
  expect_near(dial_reading_hours(false, 0.0, 3.0), 15.0, 1e-12, "three hours past the meridian reads 15:00");
  // The moon dial's lines are half a turn out of phase: a full moon on the
  // meridian is MIDNIGHT, not noon.
  expect_near(dial_reading_hours(true, 0.0, 0.0), 0.0, 1e-12, "moon dial reads midnight on the meridian");
  expect_near(dial_reading_hours(true, 0.0, 12.0), 12.0, 1e-12, "moon dial reads noon half a turn away");
}

// The property the whole feature rests on: after shifting the light's hour
// angle by the correction, the dial reads the time that was asked for. Shifting
// the hour angle by N hours is the same as advancing sidereal time by N, which
// is exactly what DialClockFace does to the light.
void test_correction_makes_the_dial_read_the_target() {
  const double kRa = 137.5;  // arbitrary body
  for (bool moondial : {false, true}) {
    bool all_landed = true;
    for (double lst = 0.0; lst < 24.0; lst += 0.37) {
      for (double target = 0.0; target < 24.0; target += 1.13) {
        double reading = dial_reading_hours(moondial, kRa, lst);
        double shift = timebase_shift_hours(target, reading);
        double corrected = dial_reading_hours(moondial, kRa, lst + shift);
        // Compare on the circle: 0 and 24 are the same reading.
        double err = std::fabs(wrap_hours_pm12(corrected - target));
        if (err > 1e-9) all_landed = false;
      }
    }
    expect_true(all_landed, moondial ? "corrected moon dial reads the target hour"
                                      : "corrected sun dial reads the target hour");
  }
}

// Mean solar time is apparent solar time less the equation of time -- and the
// gap between them is never more than the ~17 minutes the equation of time can
// reach. If mean time were computed with the wrong sign, this would show up as
// a gap of twice the equation of time.
void test_mean_time_differs_from_apparent_by_the_equation_of_time() {
  bool within_bounds = true, sign_correct = true;
  double largest_gap = 0.0;
  for (int month = 1; month <= 12; ++month) {
    double jd = test_jd(2026, month, 15, 12);
    double lst = 7.3;  // arbitrary but fixed: the gap must not depend on it
    SolarPositionResult sun = solar_position_full(jd);
    double apparent = dial_reading_hours(false, sun.eq.ra_deg, lst);
    double mean = local_mean_solar_time_hours(jd, lst);

    double gap_minutes = wrap_hours_pm12(apparent - mean) * 60.0;
    largest_gap = std::max(largest_gap, std::fabs(gap_minutes));
    if (std::fabs(gap_minutes) > 20.0) within_bounds = false;
    // apparent - mean IS the equation of time, by definition.
    if (std::fabs(gap_minutes - sun.equation_of_time_minutes) > 1e-6) sign_correct = false;
  }
  expect_true(within_bounds, "mean and apparent time never differ by more than ~20 minutes");
  expect_true(sign_correct, "apparent minus mean is exactly the equation of time (sign included)");
  expect_true(largest_gap > 5.0, "the equation of time really does swing over a year");
}

// Mid-February sits near the equation of time's negative extreme (~-14 min),
// so the correction there must be substantial -- proof the feature does real
// work on a real date rather than only satisfying its own algebra.
void test_correction_is_substantial_in_mid_february() {
  const double kLst = 0.0;
  double jd = test_jd(2026, 2, 11, 12);
  double apparent = dial_reading_hours(false, solar_position_full(jd).eq.ra_deg, kLst);
  double mean = local_mean_solar_time_hours(jd, kLst);
  double gap_minutes = std::fabs(wrap_hours_pm12(apparent - mean)) * 60.0;
  expect_true(gap_minutes > 10.0, "mid-February needs a correction of more than ten minutes");
}

}  // namespace

int main() {
  test_wrapping();
  test_reading_is_noon_on_the_meridian();
  test_correction_makes_the_dial_read_the_target();
  test_mean_time_differs_from_apparent_by_the_equation_of_time();
  test_correction_is_substantial_in_mid_february();
  if (g_failures > 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all dial time tests passed\n");
  return 0;
}
