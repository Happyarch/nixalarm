// Standalone assert-based tests for astro.cpp / dial_geometry.cpp -- no
// external test framework, matching this project's minimal-dependency style.
// Run via `ctest` or by executing the built binary directly.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <initializer_list>

#include "astro.h"
#include "dial_geometry.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
double deg2rad(double d) { return d * kPi / 180.0; }
double rad2deg(double r) { return r * 180.0 / kPi; }

int g_failures = 0;

void expect_near(double actual, double expected, double tol, const char* what) {
  if (std::fabs(actual - expected) > tol) {
    std::fprintf(stderr, "FAIL %s: expected %.6f, got %.6f (tol %.6f)\n", what, expected, actual, tol);
    ++g_failures;
  } else {
    std::printf("ok   %s (%.6f ~= %.6f)\n", what, actual, expected);
  }
}

void expect_true(bool cond, const char* what) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s\n", what);
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

// --- Regression: hour_line_direction at slant=0 must reduce EXACTLY to the
// existing closed-form horizontal-dial formula tan(dial_angle) = sin(lat)*tan(H).
void test_horizontal_closed_form_regression() {
  for (double lat : {5.0, 20.0, 35.0, 55.0, 80.0}) {
    // H=+-90 is excluded: tan(90deg) is a genuine mathematical singularity of
    // the closed form itself (shadow parallel to the plate), not something a
    // fixed-tolerance comparison of near-infinite floats can check.
    for (double h : {-150.0, -60.0, -15.0, 15.0, 60.0, 150.0}) {
      Vec2 dir = hour_line_direction(lat, /*slant=*/0.0, /*declination=*/0.0, h, /*moondial=*/false);
      double general_angle = rad2deg(std::atan2(dir.y, dir.x));

      double closed_form_angle = rad2deg(std::atan2(std::sin(deg2rad(lat)) * std::tan(deg2rad(h)), 1.0));
      // atan2(sin(lat)*tan(H), 1) == atan(sin(lat)*tan(H)) in (-90,90); the
      // general formula's atan2 output can differ by a sign/180 convention,
      // so compare via the tangent value itself (robust to that ambiguity)
      // rather than the raw angle.
      double general_tan = dir.y / dir.x;
      double expected_tan = std::sin(deg2rad(lat)) * std::tan(deg2rad(h));
      char label[128];
      std::snprintf(label, sizeof(label), "horizontal closed-form lat=%.0f H=%.0f", lat, h);
      expect_near(general_tan, expected_tan, 1e-6, label);
      (void)general_angle;
      (void)closed_form_angle;
    }
  }
}

// The whole justification for laying hour lines out at the solstice rather
// than the equinox: because the style is polar-aligned, declination slides the
// shadow ALONG its hour line but never off it, so choosing a construction
// declination changes only WHICH hours exist, never where any of them points.
// If this ever stops holding, the dial is silently mis-marked at every hour.
void test_hour_line_direction_is_declination_invariant() {
  double worst = 0.0;
  for (double lat : {-45.0, -10.0, 0.0, 23.0, 35.89, 60.0}) {
    DialOrientation o = optimize_dial_orientation(lat, kDefaultGnomonLength, kDefaultPlateRadius,
                                                    kDefaultLegibleRatio, false);
    PlateFrame frame = substyle_aligned_plate_frame(lat, o.slant_deg, o.declination_deg);
    for (double h = -75.0; h <= 75.0; h += 15.0) {
      ShadowSample a = shadow_point_on_plate(lat, frame, kDefaultGnomonLength, h, false, 0.0);
      ShadowSample b = shadow_point_on_plate(lat, frame, kDefaultGnomonLength, h, false, kObliquityDeg);
      if (!a.valid || !b.valid) continue;
      double la = std::sqrt(a.point_local.x * a.point_local.x + a.point_local.y * a.point_local.y);
      double lb = std::sqrt(b.point_local.x * b.point_local.x + b.point_local.y * b.point_local.y);
      if (la < 1e-9 || lb < 1e-9) continue;
      // Cross product of the two unit directions: zero iff they are parallel.
      double cross = (a.point_local.x / la) * (b.point_local.y / lb) - (a.point_local.y / la) * (b.point_local.x / lb);
      worst = std::max(worst, std::fabs(cross));
    }
  }
  expect_true(worst < 1e-12, "hour line direction is identical at equinox and solstice (polar style)");
}

// The construction declination is the observer's OWN summer solstice, so the
// dial is built for its longest day in either hemisphere.
void test_construction_declination_follows_the_hemisphere() {
  expect_near(construction_declination_deg(35.89), kObliquityDeg, 1e-12, "northern dials build to +23.44");
  expect_near(construction_declination_deg(-33.9), -kObliquityDeg, 1e-12, "southern dials build to -23.44");
}

void test_astro_sanity() {
  // 2026-03-20 ~ near equinox (UTC): Sun's declination should be close to 0.
  std::tm tm{};
  tm.tm_year = 2026 - 1900;
  tm.tm_mon = 2;  // March (0-indexed)
  tm.tm_mday = 20;
  tm.tm_hour = 12;
  double jd = julian_day(tm);
  SolarPositionResult sun = solar_position_full(jd);
  expect_near(sun.eq.dec_deg, 0.0, 2.0, "solar declination near equinox");
  expect_true(std::fabs(sun.equation_of_time_minutes) < 20.0, "equation of time within +-20 minutes");

  EquatorialCoord moon = lunar_position(jd);
  expect_true(moon.dec_deg > -30.0 && moon.dec_deg < 30.0, "lunar declination within +-30 degrees");

  double lst = local_sidereal_time_hours(jd, 0.0);
  expect_true(lst >= 0.0 && lst < 24.0, "local sidereal time in [0,24)");

  // Moon phase gates the moondial: a thin crescent casts no readable shadow,
  // so the face refuses to change over to it. Sweep a full synodic month and
  // check the fraction stays a fraction and actually spans new to full.
  double min_phase = 2.0, max_phase = -1.0;
  bool in_unit_range = true;
  for (int day = 0; day <= 30; ++day) {
    double f = moon_illuminated_fraction(jd + static_cast<double>(day));
    if (f < 0.0 || f > 1.0) in_unit_range = false;
    min_phase = std::min(min_phase, f);
    max_phase = std::max(max_phase, f);
  }
  expect_true(in_unit_range, "illuminated fraction stays within [0,1]");
  expect_true(min_phase < 0.05, "a synodic month reaches new moon");
  expect_true(max_phase > 0.95, "a synodic month reaches full moon");

  // An object with dec=0 crossing the meridian (H=0) culminates at altitude
  // = 90 - |lat - dec| = 90 - lat here; at lat=0 (equator) that's the zenith.
  HorizontalCoord hc_equator = equatorial_to_horizontal(EquatorialCoord{0.0, 0.0}, 0.0, 0.0);
  expect_near(hc_equator.altitude_deg, 90.0, 1e-6, "dec=0 object culminates at zenith for an equatorial observer");
  HorizontalCoord hc_45 = equatorial_to_horizontal(EquatorialCoord{0.0, 0.0}, 45.0, 0.0);
  expect_near(hc_45.altitude_deg, 45.0, 1e-6, "dec=0 object culminates at 90-lat for a lat=45 observer");
}

void test_style_vector_matches_latitude() {
  for (double lat : {0.0, 30.0, 60.0, 90.0}) {
    Vec3 s = style_vector_world(lat);
    expect_near(length(s), 1.0, 1e-9, "style vector is unit length");
    expect_near(rad2deg(std::asin(s.z)), lat, 1e-6, "style elevation equals latitude");
    expect_near(s.y, 0.0, 1e-9, "style has zero east component (azimuth = due north)");
  }
}

void test_plate_frame_degenerate_at_slant_zero() {
  PlateFrame f1 = compute_plate_frame(0.0, 0.0);
  PlateFrame f2 = compute_plate_frame(0.0, 137.0);
  expect_near(f1.n.z, 1.0, 1e-9, "horizontal plate normal is +Z");
  expect_near(f2.n.z, 1.0, 1e-9, "horizontal plate normal ignores declination (degenerate case)");
  expect_near(length(f1.n) , 1.0, 1e-9, "plate normal is unit length");
  expect_near(dot(f1.u, f1.v), 0.0, 1e-9, "plate frame u,v orthogonal");
  expect_near(dot(f1.u, f1.n), 0.0, 1e-9, "plate frame u,n orthogonal");
}

// Sanity check the legibility optimizer for a spread of latitudes, including
// near-equator and near-polar.
//
// Interesting emergent finding, worth recording rather than asserting away:
// with the OLD run/height right-triangle horizontal-only construction, the
// gnomon degenerated (height->0) as latitude->0, forcing a tilt to stay
// usable. With THIS model's fixed style LENGTH, the style just lies flatter
// near the equator without shrinking -- its tip settles near a fixed offset
// (~gnomon_length) from the dial center rather than collapsing to nothing.
// Combined with the equinox-only (delta=0) construction sun capping daylight
// at ~12h at every latitude by definition, the optimizer finds a horizontal
// (or near-horizontal) plate already achieves the full achievable window at
// most latitudes for this gnomon_length/plate_radius ratio -- i.e. switching
// to a fixed-length style already resolved the original equatorial
// degeneracy on its own, and the tilt optimizer's remaining job is mostly to
// handle the cases where it still matters (e.g. very close to the poles).
// So this test checks output validity/sanity, not a specific tilt magnitude.
void test_optimizer_sanity_across_latitudes() {
  for (double lat : {2.0, 35.0, 85.0}) {
    DialOrientation result = optimize_dial_orientation(lat, kDefaultGnomonLength, kDefaultPlateRadius,
                                                         kDefaultLegibleRatio, /*moondial=*/false);
    std::printf("lat=%.0f optimizer result: slant=%.2f declination=%.2f contiguous_hours=%.2f\n", lat,
                result.slant_deg, result.declination_deg, result.contiguous_hours);
    char label[128];
    std::snprintf(label, sizeof(label), "lat=%.0f slant within valid range", lat);
    expect_true(result.slant_deg >= 0.0 && result.slant_deg <= 180.0, label);
    std::snprintf(label, sizeof(label), "lat=%.0f declination within valid range", lat);
    expect_true(result.declination_deg >= -180.0 && result.declination_deg <= 180.0, label);
    std::snprintf(label, sizeof(label), "lat=%.0f finds a usable legible window", lat);
    expect_true(result.contiguous_hours > 0.0, label);
  }
}

// Regression: the optimizer must never pick an orientation whose style lies
// (nearly) in the plate plane -- e.g. slant == latitude at declination 0, the
// "polar dial" family. There the gnomon's vertical leg collapses to zero and
// the rendered wedge degenerates into an invisible flat sliver, yet the
// legibility objective scores it highly because the tip's shadow barely
// moves. The style must rise a real minimum angle off the plate for both
// sundial and moondial, at every latitude.
void test_optimizer_rejects_in_plane_style() {
  constexpr double kMinSin = 0.3420;  // sin(20deg), matching the optimizer's floor
  for (bool moondial : {false, true}) {
    for (double lat : {2.0, 20.0, 35.0, 55.0, 85.0}) {
      DialOrientation result = optimize_dial_orientation(lat, kDefaultGnomonLength, kDefaultPlateRadius,
                                                           kDefaultLegibleRatio, moondial);
      PlateFrame frame = compute_plate_frame(result.slant_deg, result.declination_deg);
      Vec3 style_local = project_to_local(style_vector_world(lat), frame);
      char label[128];
      std::snprintf(label, sizeof(label), "%s lat=%.0f style rises >= 20deg off the plate (sin=%.4f)",
                    moondial ? "moondial" : "sundial", lat, style_local.z);
      expect_true(style_local.z >= kMinSin - 1e-6, label);
    }
  }
}

void test_moondial_shares_geometry() {
  // Full/opposition moon at hour angle H should behave like the sundial's
  // equinox construction sun 180deg out of phase -- same closed-form check,
  // phase-shifted.
  double lat = 35.0;
  Vec2 moon_dir = hour_line_direction(lat, 0.0, 0.0, /*hour_angle=*/0.0, /*moondial=*/true);
  Vec2 sun_dir_at_180 = hour_line_direction(lat, 0.0, 0.0, /*hour_angle=*/180.0, /*moondial=*/false);
  expect_near(moon_dir.x, sun_dir_at_180.x, 1e-9, "moondial H=0 matches sundial H=180 (x)");
  expect_near(moon_dir.y, sun_dir_at_180.y, 1e-9, "moondial H=0 matches sundial H=180 (y)");
}

// The substyle-aligned frame must put the style's plate-projection exactly on
// local +u (y component zero, x positive), leave the normal untouched, and
// stay orthonormal/right-handed.
void test_substyle_aligned_frame() {
  for (double lat : {2.0, 35.0, 55.0, 85.0}) {
    for (double slant : {0.0, 30.0, 75.0}) {
      for (double decl : {0.0, -52.5, 120.0}) {
        PlateFrame raw = compute_plate_frame(slant, decl);
        PlateFrame aligned = substyle_aligned_plate_frame(lat, slant, decl);
        Vec3 s = project_to_local(style_vector_world(lat), aligned);
        char label[160];
        double horiz = std::sqrt(s.x * s.x + s.y * s.y);
        if (horiz < 1e-9) continue;  // style perpendicular to plate: alignment is a no-op by design
        std::snprintf(label, sizeof(label), "lat=%.0f slant=%.0f decl=%.1f substyle on +u (y=0)", lat, slant, decl);
        expect_near(s.y, 0.0, 1e-9, label);
        std::snprintf(label, sizeof(label), "lat=%.0f slant=%.0f decl=%.1f substyle x positive", lat, slant, decl);
        expect_true(s.x > 0.0, label);
        std::snprintf(label, sizeof(label), "lat=%.0f slant=%.0f decl=%.1f normal unchanged", lat, slant, decl);
        expect_near(length(aligned.n - raw.n), 0.0, 1e-9, label);
        std::snprintf(label, sizeof(label), "lat=%.0f slant=%.0f decl=%.1f frame stays orthonormal", lat, slant, decl);
        expect_true(std::fabs(dot(aligned.u, aligned.v)) < 1e-9 && std::fabs(length(aligned.u) - 1.0) < 1e-9 &&
                        std::fabs(length(aligned.v) - 1.0) < 1e-9 &&
                        length(cross(aligned.u, aligned.v) - aligned.n) < 1e-9,
                    label);
      }
    }
  }
}

}  // namespace

int main() {
  test_horizontal_closed_form_regression();
  test_hour_line_direction_is_declination_invariant();
  test_construction_declination_follows_the_hemisphere();
  test_astro_sanity();
  test_style_vector_matches_latitude();
  test_plate_frame_degenerate_at_slant_zero();
  test_moondial_shares_geometry();
  test_optimizer_sanity_across_latitudes();
  test_optimizer_rejects_in_plane_style();
  test_substyle_aligned_frame();

  if (g_failures > 0) {
    std::fprintf(stderr, "\n%d test(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("\nall tests passed\n");
  return 0;
}
