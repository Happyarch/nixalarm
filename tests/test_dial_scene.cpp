// Standalone assert-based tests for dial_scene.cpp -- matches this project's
// minimal-dependency test style (see tests/test_dial_geometry.cpp).

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dial_geometry.h"
#include "dial_scene.h"

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

DialScene build_test_scene(bool moondial) {
  DialOrientation orientation = optimize_dial_orientation(35.0, kDefaultGnomonLength, kDefaultPlateRadius,
                                                            kDefaultLegibleRatio, moondial);
  return build_dial_scene(35.0, orientation, kDefaultGnomonLength, kDefaultPlateRadius, moondial);
}

std::vector<SceneRod> rods_of(const DialScene& s, DialMaterial m) {
  std::vector<SceneRod> out;
  for (const SceneRod& r : s.rods)
    if (r.material == m) out.push_back(r);
  return out;
}

void test_scene_layout() {
  DialScene scene = build_test_scene(false);

  expect_true(scene.plate_radius > 0.0, "plate has a radius");
  expect_true(scene.plate_thickness > 0.0, "plate has thickness");
  expect_true(scene.ground_z < -scene.plate_thickness, "ground sits below the plate underside");

  auto gnomon = rods_of(scene, DialMaterial::Gnomon);
  expect_true(gnomon.size() == 1, "exactly one solid gnomon rod");
  expect_true(!scene.rods.empty() && scene.rods.front().material == DialMaterial::Gnomon,
              "solid rod is first in the rod list");

  auto ticks = rods_of(scene, DialMaterial::Tick);
  expect_true(!ticks.empty(), "at least one legible hour tick rod");
  bool ticks_on_plate = true, ticks_in_radius = true;
  for (const SceneRod& t : ticks) {
    if (std::fabs(t.a.z) > 1e-9 || std::fabs(t.b.z) > 1e-9) ticks_on_plate = false;
    for (const Vec3& p : {t.a, t.b}) {
      if (std::sqrt(p.x * p.x + p.y * p.y) > scene.plate_radius + 1e-6) ticks_in_radius = false;
    }
  }
  expect_true(ticks_on_plate, "tick rods lie on the plate surface (z=0 centerline)");
  expect_true(ticks_in_radius, "tick rods stay within the plate radius");
}

// An hour line is named by its DIRECTION; how far along it the gnomon tip's
// shadow happens to reach is incidental. Morning and evening shadows run many
// plate-radii out, and an earlier version dropped those hours entirely for
// being "off the plate" -- which silently cost the dial its whole early and
// late range, leaving a stub of midday hours. Every hour whose light is up and
// whose shadow direction is defined must get a line, clipped to the stone.
void test_long_shadows_are_clipped_not_dropped() {
  const double kLat = 35.0;
  DialOrientation orientation = optimize_dial_orientation(kLat, kDefaultGnomonLength, kDefaultPlateRadius,
                                                            kDefaultLegibleRatio, false);
  PlateFrame frame = substyle_aligned_plate_frame(kLat, orientation.slant_deg, orientation.declination_deg);
  DialScene scene = build_dial_scene(kLat, orientation, kDefaultGnomonLength, kDefaultPlateRadius, false);

  // Same construction declination the scene lays its lines out for -- the
  // observer's summer solstice, not the equinox.
  const double kDecl = construction_declination_deg(kLat);
  size_t expected = 0, overrunning = 0;
  for (double h = -165.0; h <= 165.0; h += 15.0) {
    if (idealized_light_direction_world(kLat, h, false, kDecl).z <= 0.0) continue;
    ShadowSample s = shadow_point_on_plate(kLat, frame, kDefaultGnomonLength, h, false, kDecl);
    if (!s.valid) continue;
    double len = std::sqrt(s.point_local.x * s.point_local.x + s.point_local.y * s.point_local.y);
    if (len < 1e-9) continue;
    ++expected;
    if (len > scene.plate_radius) ++overrunning;
  }
  expect_true(overrunning > 0, "this latitude really does have hours whose shadow overruns the plate");
  expect_true(scene.hour_marks.size() == expected, "every hour with a usable light direction gets a mark");

  // The drawn lines are the dial's structure -- the boundaries between hour
  // bands -- not an indication of where any shadow reached. Each is a secant
  // of the plate: it runs rim to rim, and it clears the gnomon foot by exactly
  // the tangent circle's radius.
  double shortest_reach = 1e9, longest_reach = 0.0;
  double closest_approach = 1e9, furthest_approach = 0.0;
  for (const SceneRod& t : scene.rods) {
    if (t.material != DialMaterial::Tick) continue;
    for (const Vec3& p : {t.a, t.b}) {
      double r = std::sqrt(p.x * p.x + p.y * p.y);
      shortest_reach = std::min(shortest_reach, r);
      longest_reach = std::max(longest_reach, r);
    }
    // Distance from the dial centre to the chord's line: |(b-a) x (-a)| / |b-a|.
    double dx = t.b.x - t.a.x, dy = t.b.y - t.a.y;
    double approach = std::fabs(dx * (-t.a.y) - dy * (-t.a.x)) / std::sqrt(dx * dx + dy * dy);
    closest_approach = std::min(closest_approach, approach);
    furthest_approach = std::max(furthest_approach, approach);
  }
  expect_near(shortest_reach, scene.plate_radius, 1e-9, "every boundary reaches the rim at one end");
  expect_near(longest_reach, scene.plate_radius, 1e-9, "and at the other -- they are secants, rim to rim");
  expect_true(closest_approach > 0.0, "no boundary passes through the gnomon foot");
  expect_near(furthest_approach, closest_approach, 1e-9,
              "all boundaries are tangent to one circle, which is what makes them cross into a rosette");
}

// The dial is read as bands: the lines are boundaries at the half hours and
// each numeral sits in the middle of the band between two of them. A numeral
// sitting ON a line would be ambiguous -- it is the whole point of the layout
// that it does not.
void test_numerals_sit_between_the_boundary_lines() {
  for (bool moondial : {false, true}) {
    DialScene scene = build_test_scene(moondial);

    // A boundary chord's DIRECTION is the angle of its midpoint -- the point
    // where it touches the inner circle. Its endpoints are out on the rim in
    // two entirely different directions, so reading the angle off an endpoint
    // would make this test pass without checking anything.
    std::vector<double> boundaries;
    for (const SceneRod& t : scene.rods) {
      if (t.material != DialMaterial::Tick) continue;
      boundaries.push_back(std::atan2(0.5 * (t.a.y + t.b.y), 0.5 * (t.a.x + t.b.x)));
    }
    expect_true(boundaries.size() >= 2, "the dial has boundary lines to sit between");

    bool all_between = true, none_on_a_line = true;
    for (const HourMark& m : scene.hour_marks) {
      double angle = std::atan2(m.direction.y, m.direction.x);
      bool has_before = false, has_after = false;
      double closest = 1e9;
      for (double b : boundaries) {
        double delta = angle - b;
        while (delta > 3.14159265358979323846) delta -= 2.0 * 3.14159265358979323846;
        while (delta < -3.14159265358979323846) delta += 2.0 * 3.14159265358979323846;
        if (delta > 0.0) has_after = true;
        if (delta < 0.0) has_before = true;
        closest = std::min(closest, std::fabs(delta));
      }
      if (!has_before || !has_after) all_between = false;
      // Half an hour of arc is 7.5 degrees of hour angle; on the plate the
      // bands are uneven, so just insist the numeral is clearly off the line.
      if (closest < 0.5 * 3.14159265358979323846 / 180.0) none_on_a_line = false;
    }
    expect_true(all_between, moondial ? "every moon dial numeral has a boundary on each side"
                                       : "every sun dial numeral has a boundary on each side");
    expect_true(none_on_a_line, "no numeral sits on top of a boundary line");
  }
}

// The dial faces change over from sun to moon when the current dial stops
// being readable, so this predicate decides when that happens. It must key on
// the shadow leaving the plate, NOT on the light merely being above the
// horizon -- the shadow runs off the edge well before the body sets.
void test_readability_follows_the_shadow_off_the_plate() {
  DialScene scene = build_test_scene(false);

  // Straight down the plate normal: the shadow is as short as it gets.
  expect_true(shadow_falls_on_plate(scene, Vec3{0.0, 0.0, 1.0}), "overhead light is readable");

  // Below the plate's own horizon, and exactly along it: no shadow at all.
  expect_true(!shadow_falls_on_plate(scene, Vec3{0.3, 0.0, -0.5}), "light below the plate horizon is unreadable");
  expect_true(!shadow_falls_on_plate(scene, Vec3{1.0, 0.0, 0.0}), "light along the plate plane is unreadable");

  // Sweeping the light down from the zenith toward the horizon, readability
  // must fail once and stay failed -- if it flickered the face would flap
  // between dials.
  const Vec3& tip = scene.gnomon_tip_local;
  bool seen_unreadable = false, flapped = false;
  for (int i = 0; i <= 200; ++i) {
    double elevation = 90.0 - 90.0 * (static_cast<double>(i) / 200.0);
    double e = elevation * 3.14159265358979323846 / 180.0;
    // Sweep on the side the tip leans toward, so the shadow runs outward.
    Vec3 light{-std::cos(e) * (tip.x >= 0.0 ? 1.0 : -1.0), 0.0, std::sin(e)};
    bool readable = shadow_falls_on_plate(scene, light);
    if (!readable) seen_unreadable = true;
    else if (seen_unreadable) flapped = true;
  }
  expect_true(seen_unreadable, "a low enough light pushes the shadow off the plate");
  expect_true(!flapped, "readability fails once as the light descends, and stays failed");
}

// The user-specified triangle: the solid rod is the hypotenuse from the dial
// center to the tip; the glass frame is the other two edges -- one leg lying
// IN the plate plane out to the point beneath the tip, one leg parallel to
// the plate NORMAL from there up to the tip.
void test_gnomon_rod_and_glass_frame_form_right_triangle() {
  DialScene scene = build_test_scene(false);
  Vec3 tip = scene.gnomon_tip_local;

  expect_true(tip.z > 0.0, "style tip rises above the plate (optimizer elevation floor)");
  // Substyle-aligned frame: the rod's horizontal direction IS local +u.
  expect_near(tip.y, 0.0, 1e-9, "tip lies on the +u (substyle) axis");
  expect_true(tip.x > 0.0, "tip is on the positive u side");

  const SceneRod& rod = scene.rods.front();
  expect_near(length(rod.a - Vec3{0.0, 0.0, 0.0}), 0.0, 1e-12, "solid rod starts at the dial center");
  expect_near(length(rod.b - tip), 0.0, 1e-12, "solid rod ends exactly at the tip/nodus");

  auto glass = rods_of(scene, DialMaterial::Glass);
  expect_true(glass.size() == 2, "two glass frame legs");
  if (glass.size() == 2) {
    const SceneRod& base = glass[0];
    const SceneRod& vertical = glass[1];
    expect_near(base.a.z, 0.0, 1e-12, "base leg start is in the plate plane");
    expect_near(base.b.z, 0.0, 1e-12, "base leg end is in the plate plane");
    expect_near(length(base.b - Vec3{tip.x, tip.y, 0.0}), 0.0, 1e-12, "base leg ends beneath the tip");
    Vec3 v = vertical.b - vertical.a;
    expect_near(v.x, 0.0, 1e-12, "vertical leg is parallel to the plate normal (x)");
    expect_near(v.y, 0.0, 1e-12, "vertical leg is parallel to the plate normal (y)");
    expect_true(v.z > 0.0, "vertical leg rises toward the tip");
    expect_near(length(vertical.b - tip), 0.0, 1e-12, "vertical leg ends exactly at the tip");
    // Right angle where the two legs meet, by construction.
    expect_near(dot(base.b - base.a, vertical.b - vertical.a), 0.0, 1e-9, "legs meet at a right angle");
    expect_true(base.radius < rod.radius && vertical.radius < rod.radius,
                "glass legs are thinner than the solid rod");
  }
}

void test_moondial_also_produces_scene() {
  DialScene scene = build_test_scene(true);
  expect_true(!scene.rods.empty(), "moondial scene has rods");
  expect_true(rods_of(scene, DialMaterial::Gnomon).size() == 1, "moondial has one solid gnomon rod");
  expect_true(!rods_of(scene, DialMaterial::Tick).empty(), "moondial has hour tick rods");
}

}  // namespace

int main() {
  test_scene_layout();
  test_gnomon_rod_and_glass_frame_form_right_triangle();
  test_moondial_also_produces_scene();
  test_long_shadows_are_clipped_not_dropped();
  test_numerals_sit_between_the_boundary_lines();
  test_readability_follows_the_shadow_off_the_plate();

  if (g_failures > 0) {
    std::fprintf(stderr, "\n%d test(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("\nall tests passed\n");
  return 0;
}
