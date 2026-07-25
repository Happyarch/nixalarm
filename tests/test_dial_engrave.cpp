// Standalone assert-based tests for dial_engrave.cpp -- matches this project's
// minimal-dependency test style (see tests/test_dial_geometry.cpp).

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dial_engrave.h"
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

bool streq(const char* a, const char* b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

DialScene build_test_scene(bool moondial) {
  DialOrientation orientation = optimize_dial_orientation(35.0, kDefaultGnomonLength, kDefaultPlateRadius,
                                                            kDefaultLegibleRatio, moondial);
  return build_dial_scene(35.0, orientation, kDefaultGnomonLength, kDefaultPlateRadius, moondial);
}

// The dial reads as a clock only if the numerals match what the analog face's
// Roman dial uses -- subtractive IV and IX, not IIII and VIIII.
void test_roman_numerals() {
  expect_true(streq(roman_numeral(1), "I"), "1 is I");
  expect_true(streq(roman_numeral(4), "IV"), "4 is IV (subtractive, matching the analog Roman face)");
  expect_true(streq(roman_numeral(9), "IX"), "9 is IX");
  expect_true(streq(roman_numeral(12), "XII"), "12 is XII");
  expect_true(streq(roman_numeral(0), "") && streq(roman_numeral(13), ""), "hours outside 1..12 have no numeral");
}

// Each hour band must carry a numeral, or the dial would have unlabelled
// hours. The lines are the boundaries BETWEEN bands, so a dial with n readable
// hours has n numerals fenced by rather more than n lines -- what matters here
// is that the numerals exist and name sane hours.
void test_marks_accompany_ticks() {
  for (bool moondial : {false, true}) {
    DialScene scene = build_test_scene(moondial);
    size_t boundaries = 0;
    for (const SceneRod& r : scene.rods)
      if (r.material == DialMaterial::Tick) ++boundaries;
    expect_true(boundaries > 0, moondial ? "moondial has boundary lines" : "sundial has boundary lines");
    expect_true(!scene.hour_marks.empty(), moondial ? "moondial has hour numerals" : "sundial has hour numerals");
    expect_true(boundaries > scene.hour_marks.size(),
                "bands are fenced by more boundaries than the hours they enclose");

    bool hours_in_range = true, dirs_unit = true, inside_plate = true;
    for (const HourMark& m : scene.hour_marks) {
      if (m.hour < 1 || m.hour > 12) hours_in_range = false;
      if (std::fabs(std::sqrt(m.direction.x * m.direction.x + m.direction.y * m.direction.y) - 1.0) > 1e-9)
        dirs_unit = false;
      // The whole numeral, not just its center, has to fit on the plate.
      if (m.radius + 0.5 * m.height > scene.plate_radius) inside_plate = false;
    }
    expect_true(hours_in_range, "every mark names an hour in 1..12");
    expect_true(dirs_unit, "mark directions are unit vectors");
    expect_true(inside_plate, "numerals fit inside the plate radius");
  }
}

// A numeral must sit inside its band, not across a boundary line -- lettering
// with a line struck through it reads as damage.
//
// Measured against the ENGRAVING ITSELF rather than the mark's nominal height,
// because build_hour_numeral_engraving shrinks a numeral to the angular room
// its band actually has. Checking the nominal height would fail for the narrow
// bands at the ends of the day even though nothing is really struck through.
void test_numerals_are_not_struck_through_by_a_boundary() {
  for (bool moondial : {false, true}) {
    DialScene scene = build_test_scene(moondial);
    const int kSize = 256;
    EngravingMap map = build_hour_numeral_engraving(scene, kSize);
    const double texel = 2.0 * scene.plate_radius / kSize;

    double closest = 1e9;
    for (int j = 0; j < kSize; ++j) {
      for (int i = 0; i < kSize; ++i) {
        if (map.depth[static_cast<size_t>(j) * kSize + i] == 0) continue;
        double px = -scene.plate_radius + (i + 0.5) * texel;
        double py = -scene.plate_radius + (j + 0.5) * texel;
        for (const SceneRod& r : scene.rods) {
          if (r.material != DialMaterial::Tick) continue;
          double dx = r.b.x - r.a.x, dy = r.b.y - r.a.y;
          double len2 = dx * dx + dy * dy;
          double t = len2 > 1e-18 ? ((px - r.a.x) * dx + (py - r.a.y) * dy) / len2 : 0.0;
          t = std::max(0.0, std::min(1.0, t));
          double qx = r.a.x + t * dx - px, qy = r.a.y + t * dy - py;
          closest = std::min(closest, std::sqrt(qx * qx + qy * qy));
        }
      }
    }
    // The boundary rods have a radius of their own; cut lettering must stay
    // outside it, or the two run together on the plate.
    const double line_radius = 0.010 * scene.plate_radius;
    expect_true(closest > line_radius, moondial ? "no moon dial numeral is struck through by a boundary"
                                                : "no sun dial numeral is struck through by a boundary");
  }
}

void test_engraving_cuts_where_the_numerals_are() {
  DialScene scene = build_test_scene(false);
  const int kSize = 256;
  EngravingMap map = build_hour_numeral_engraving(scene, kSize);
  expect_true(map.size == kSize && map.depth.size() == static_cast<size_t>(kSize) * kSize,
              "engraving map is size x size bytes");

  size_t cut = 0;
  uint8_t deepest = 0;
  for (uint8_t v : map.depth) {
    if (v > 0) ++cut;
    deepest = std::max(deepest, v);
  }
  expect_true(cut > 0, "something is actually engraved");
  expect_true(deepest > 200, "stroke centerlines reach near-full depth (V-groove, not a faint scratch)");
  // Lettering is line work: it should touch a small fraction of the plate.
  expect_true(cut < map.depth.size() / 10, "engraving stays sparse rather than flooding the plate");

  // The map's center is the dial center, which carries no numeral.
  size_t mid = static_cast<size_t>(kSize / 2) * kSize + kSize / 2;
  expect_true(map.depth[mid] == 0, "the dial center is left uncut");

  // Sampling the map at a mark's own center should land in or beside its
  // numeral -- i.e. the plate-space -> texel mapping agrees with dial_scene.
  bool marks_land_on_ink = true;
  for (const HourMark& m : scene.hour_marks) {
    double cx = m.direction.x * m.radius, cy = m.direction.y * m.radius;
    int i = static_cast<int>((cx + scene.plate_radius) / (2.0 * scene.plate_radius) * kSize);
    int j = static_cast<int>((cy + scene.plate_radius) / (2.0 * scene.plate_radius) * kSize);
    int window = static_cast<int>(std::ceil(m.height / (2.0 * scene.plate_radius) * kSize));
    bool found = false;
    for (int dj = -window; dj <= window && !found; ++dj) {
      for (int di = -window; di <= window && !found; ++di) {
        int jj = j + dj, ii = i + di;
        if (ii < 0 || jj < 0 || ii >= kSize || jj >= kSize) continue;
        if (map.depth[static_cast<size_t>(jj) * kSize + ii] > 0) found = true;
      }
    }
    if (!found) marks_land_on_ink = false;
  }
  expect_true(marks_land_on_ink, "each hour mark has engraving at its own plate position");
}

void test_empty_scene_is_blank_not_empty() {
  DialScene empty;
  empty.plate_radius = 1.0;
  EngravingMap map = build_hour_numeral_engraving(empty, 64);
  expect_true(map.size == 64 && map.depth.size() == 64 * 64, "a scene with no marks still yields a full-size map");
  bool all_zero = true;
  for (uint8_t v : map.depth)
    if (v != 0) all_zero = false;
  expect_true(all_zero, "a scene with no marks engraves nothing");
}

}  // namespace

int main() {
  test_roman_numerals();
  test_marks_accompany_ticks();
  test_numerals_are_not_struck_through_by_a_boundary();
  test_engraving_cuts_where_the_numerals_are();
  test_empty_scene_is_blank_not_empty();
  if (g_failures > 0) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all dial engraving tests passed\n");
  return 0;
}
