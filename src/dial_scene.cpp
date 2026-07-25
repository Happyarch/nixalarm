#include "dial_scene.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kGnomonRodRadiusRatio = 0.035;  // relative to gnomon_length
constexpr double kGlassLegRadiusRatio = 0.012;   // relative to gnomon_length; ~1/3 of the rod
constexpr double kTickRodRadiusRatio = 0.010;    // relative to plate_radius
constexpr double kTickInnerRadiusRatio = 0.22;
// The hour lines stop short of the rim to leave an annulus for the engraved
// numerals, which sit centered in the gap between the tick ends and the edge.
constexpr double kTickOuterRadiusRatio = 0.80;
constexpr double kNumeralRadiusRatio = 0.885;
constexpr double kNumeralHeightRatio = 0.105;
constexpr double kPi = 3.14159265358979323846;
constexpr double kPlateThicknessRatio = 0.05;  // relative to plate_radius
constexpr double kGroundGapRatio = 0.02;       // gap between plate underside and ground

}  // namespace

DialScene build_dial_scene(double latitude_deg, const DialOrientation& orientation, double gnomon_length,
                            double plate_radius, bool moondial) {
  DialScene scene;
  scene.plate_radius = plate_radius;
  scene.plate_thickness = plate_radius * kPlateThicknessRatio;
  scene.ground_z = -(scene.plate_thickness + plate_radius * kGroundGapRatio);

  // Substyle-aligned frame throughout: the rod rises along +u, and the tick
  // shadow points below are computed in this same frame so gnomon and ticks
  // can never spin apart (see dial_geometry.h).
  PlateFrame frame = substyle_aligned_plate_frame(latitude_deg, orientation.slant_deg, orientation.declination_deg);
  Vec3 style_local = project_to_local(style_vector_world(latitude_deg), frame);
  Vec3 tip = style_local * gnomon_length;
  scene.gnomon_tip_local = tip;

  double rod_r = gnomon_length * kGnomonRodRadiusRatio;
  double leg_r = gnomon_length * kGlassLegRadiusRatio;

  // Solid shadow-casting rod: dial center to the nodus, along the style edge.
  scene.rods.push_back(SceneRod{Vec3{0.0, 0.0, 0.0}, tip, rod_r, DialMaterial::Gnomon});

  // Glass frame: the other two edges of the classic right triangle. Both
  // degenerate honestly -- the vertical leg vanishes when the style lies in
  // the plate (excluded by the optimizer's elevation floor anyway) and the
  // base leg vanishes when the style is perpendicular to the plate (polar
  // case, where the rod alone IS the whole triangle).
  Vec3 foot{tip.x, tip.y, 0.0};
  double horiz = std::sqrt(tip.x * tip.x + tip.y * tip.y);
  if (horiz > 1e-9) {
    scene.rods.push_back(SceneRod{Vec3{0.0, 0.0, 0.0}, foot, leg_r, DialMaterial::Glass});
  }
  if (tip.z > 1e-9) {
    scene.rods.push_back(SceneRod{foot, tip, leg_r, DialMaterial::Glass});
  }

  double tick_r = plate_radius * kTickRodRadiusRatio;
  double r_inner = plate_radius * kTickInnerRadiusRatio;
  double r_outer = plate_radius * kTickOuterRadiusRatio;
  // Hour lines are laid out for the observer's SUMMER SOLSTICE, the longest
  // day: that is every hour the light can ever be up for, and a dial built to
  // any lesser declination is permanently missing its earliest and latest
  // hours. It costs nothing in accuracy -- the style is polar-aligned, so
  // declination moves the shadow along its hour line but never off it.
  double construction_decl = construction_declination_deg(latitude_deg);

  // The direction on the plate the shadow takes at a given hour angle, if it
  // takes one at all. Used for both the hour BOUNDARIES and the numerals,
  // which is what keeps the two exactly half an hour apart by construction.
  auto shadow_direction_at = [&](double hour_angle, Vec2* out) {
    Vec3 light_w = idealized_light_direction_world(latitude_deg, hour_angle, moondial, construction_decl);
    if (light_w.z <= 0.0) return false;  // sun/moon below the true horizon at this hour
    ShadowSample s = shadow_point_on_plate(latitude_deg, frame, gnomon_length, hour_angle, moondial,
                                            construction_decl);
    if (!s.valid) return false;
    double len = std::sqrt(s.point_local.x * s.point_local.x + s.point_local.y * s.point_local.y);
    if (len < 1e-9) return false;
    // The actual finite shadow point's direction, not hour_line_direction,
    // which only gives an undirected line -- so this lands on the correct side
    // of the noon/midnight line rather than its 180-degree mirror.
    *out = Vec2{s.point_local.x / len, s.point_local.y / len};
    return true;
  };

  // The dial is read as HOUR BANDS, not hour lines: the engraved lines are the
  // boundaries BETWEEN hours, drawn at the half hours, and each numeral sits
  // in the middle of the band its two boundaries enclose. So the shadow lying
  // inside a band names that hour, rather than having to coincide with a line.
  //
  // The readable hours, and where each sits on the plate. Collected first
  // because the boundaries are placed relative to them: a band has to be
  // closed on both sides or the numeral in it means nothing.
  struct HourEntry {
    double hour_angle;   // degrees from the dial's own noon
    double plate_angle;  // radians, direction on the plate
    int hour;            // 1..12
  };
  // Swept in the DIAL'S OWN day order, outward from the middle of its light's
  // time up. For the sun that middle is hour angle 0; for the moon it is 180,
  // because a full moon transits at midnight. Sweeping -165..165 instead would
  // split the moon dial's night across the wrap at +-180 -- leaving its hours
  // in the wrong order for the banding below, and dropping its midnight hour
  // altogether. Hour angles are kept CONTINUOUS here (they can run past 180)
  // so midpoints between neighbours stay arithmetic; only the hour number
  // needs the wrap.
  double day_center = moondial ? 180.0 : 0.0;
  std::vector<HourEntry> hours;
  for (int step = -12; step < 12; ++step) {
    double h = day_center + step * 15.0;
    Vec2 dir{0.0, 0.0};
    if (!shadow_direction_at(h, &dir)) continue;
    // The hour angle is measured from the dial's own noon (midnight for a
    // moondial) at 15 degrees per hour, so the clock hour is 12 + h/15 --
    // folded onto the 1..12 a Roman numeral can express.
    int hour24 = 12 + static_cast<int>(std::lround(h / 15.0));
    hours.push_back(HourEntry{h, std::atan2(dir.y, dir.x), ((hour24 % 12) + 12 - 1) % 12 + 1});
  }
  if (hours.empty()) return scene;

  // The hour lines sweep monotonically across the plate, but atan2 wraps at
  // the half turn; unwrap so "between two boundaries" is plain arithmetic.
  for (size_t i = 1; i < hours.size(); ++i) {
    while (hours[i].plate_angle - hours[i - 1].plate_angle > kPi) hours[i].plate_angle -= 2.0 * kPi;
    while (hours[i].plate_angle - hours[i - 1].plate_angle < -kPi) hours[i].plate_angle += 2.0 * kPi;
  }

  for (const HourEntry& e : hours) {
    Vec2 dir{std::cos(e.plate_angle), std::sin(e.plate_angle)};
    scene.hour_marks.push_back(HourMark{dir, plate_radius * kNumeralRadiusRatio,
                                         plate_radius * kNumeralHeightRatio, e.hour});
  }

  // Boundaries fall at the half hours, one more of them than there are hours,
  // so every band is closed on both sides.
  //
  // The interior ones are the real half-hour lines wherever the construction
  // light still reaches -- the hour-angle-to-plate-angle map is not linear, so
  // the true half hour is not quite the bisector of its neighbours and it is
  // worth asking for. Past the ends of the day it stops being computable (the
  // light is below the horizon half an hour later), so the outermost bands are
  // closed by mirroring their own width outward. Without that the first and
  // last numerals sit in bands open at one end.
  std::vector<double> bounds(hours.size() + 1, 0.0);
  for (size_t i = 1; i < hours.size(); ++i) {
    double midpoint = 0.5 * (hours[i - 1].plate_angle + hours[i].plate_angle);
    Vec2 dir{0.0, 0.0};
    if (shadow_direction_at(0.5 * (hours[i - 1].hour_angle + hours[i].hour_angle), &dir)) {
      double angle = std::atan2(dir.y, dir.x);
      while (angle - midpoint > kPi) angle -= 2.0 * kPi;
      while (angle - midpoint < -kPi) angle += 2.0 * kPi;
      bounds[i] = angle;
    } else {
      bounds[i] = midpoint;
    }
  }
  constexpr double kHalfHourFallback = 7.5 * kPi / 180.0;
  double first_half_width = hours.size() >= 2 ? bounds[1] - hours.front().plate_angle : kHalfHourFallback;
  double last_half_width =
      hours.size() >= 2 ? hours.back().plate_angle - bounds[hours.size() - 1] : kHalfHourFallback;
  bounds.front() = hours.front().plate_angle - first_half_width;
  bounds.back() = hours.back().plate_angle + last_half_width;

  // Each boundary spans the reading annulus, from the inner circle out to the
  // rim, cutting the plate rather than converging on the gnomon foot -- the
  // foot is left clear for the shadow to sweep. Boundaries run the full width
  // of the annulus however far the shadow reaches at that hour: they are the
  // dial's structure, not an indication of anything, and even bands read
  // better than ragged ones.
  for (double angle : bounds) {
    Vec2 dir{std::cos(angle), std::sin(angle)};
    // Centered on the plate surface, so half the capsule stands proud of it
    // as a raised ridge the tracer can shade and shadow like everything else.
    scene.rods.push_back(SceneRod{Vec3{dir.x * r_inner, dir.y * r_inner, 0.0},
                                   Vec3{dir.x * r_outer, dir.y * r_outer, 0.0}, tick_r, DialMaterial::Tick});
  }

  return scene;
}

bool shadow_falls_on_plate(const DialScene& scene, Vec3 light_local) {
  Vec3 light = normalize(light_local);
  if (light.z <= 1e-4) return false;  // at or below the plate's own horizon: no shadow at all

  // Walk from the tip directly away from the light until the plate plane.
  const Vec3& tip = scene.gnomon_tip_local;
  double t = tip.z / light.z;
  double sx = tip.x - light.x * t;
  double sy = tip.y - light.y * t;
  return std::sqrt(sx * sx + sy * sy) <= scene.plate_radius;
}
