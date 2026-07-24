// Standalone assert-based tests for dial_scene.cpp -- matches this project's
// minimal-dependency test style (see tests/test_dial_geometry.cpp).

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

  if (g_failures > 0) {
    std::fprintf(stderr, "\n%d test(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("\nall tests passed\n");
  return 0;
}
