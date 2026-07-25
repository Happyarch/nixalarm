#include "dial_scene.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kGnomonRodRadiusRatio = 0.035;  // relative to gnomon_length
constexpr double kGlassLegRadiusRatio = 0.012;   // relative to gnomon_length; ~1/3 of the rod
constexpr double kTickRodRadiusRatio = 0.010;  // relative to plate_radius
// The hour lines run from the gnomon foot out to the rim, so the numerals sit
// just inside the edge, in the gap their two boundaries leave.
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

  // Glass frame: the other two edges of the classic right triangle. Either can
  // vanish -- the vertical leg when the style lies in the plate (ruled out by
  // the optimizer's elevation floor), the base leg when the style is
  // perpendicular to it (polar case, where the rod is the whole triangle).
  Vec3 foot{tip.x, tip.y, 0.0};
  double horiz = std::sqrt(tip.x * tip.x + tip.y * tip.y);
  if (horiz > 1e-9) {
    scene.rods.push_back(SceneRod{Vec3{0.0, 0.0, 0.0}, foot, leg_r, DialMaterial::Glass});
  }
  if (tip.z > 1e-9) {
    scene.rods.push_back(SceneRod{foot, tip, leg_r, DialMaterial::Glass});
  }

  double tick_r = plate_radius * kTickRodRadiusRatio;
  // Laid out for the observer's summer solstice: the longest day, so the dial
  // carries every hour its light can reach. Costs no accuracy -- the style is
  // polar-aligned, so declination slides the shadow along its hour line, never
  // off it.
  double construction_decl = construction_declination_deg(latitude_deg);

  // Shadow direction on the plate for an hour angle, if there is one. Shared
  // by boundaries and numerals, which keeps them half an hour apart.
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

  // Hour bands: the drawn lines are the boundaries between hours, at the half
  // hours, and each numeral sits in the band between two of them. The shadow
  // falling inside a band names that hour.
  //
  // Hours are collected first; boundaries are placed relative to them, and
  // every band needs both sides closed.
  struct HourEntry {
    double hour_angle;   // degrees from the dial's own noon
    double plate_angle;  // radians, direction on the plate
    int hour;            // 1..12
  };
  // Swept outward from the dial's own day centre: hour angle 0 for the sun,
  // 180 for the moon (a full moon transits at midnight). A -165..165 sweep
  // would split the moon dial's night across the +-180 wrap, reversing its
  // hour order and skipping hour angle 180 entirely. Angles stay continuous
  // (may exceed 180) so neighbour midpoints are plain arithmetic; only the
  // hour number wraps.
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

  // Boundaries at the half hours, one more than there are hours, so every band
  // is closed. Interior ones use the true half-hour line: the
  // hour-angle-to-plate-angle map is nonlinear, so it is not the bisector of
  // its neighbours. Past the ends of the day the half hour is below the
  // horizon and uncomputable, so the outer bands mirror their own width.
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

  // Numerals sit on their hour ray near the rim. The engraver needs to know
  // how much room each has: bands are asymmetric about their hour and narrow
  // sharply near midday, so measure to the nearer boundary.
  double numeral_radius = plate_radius * kNumeralRadiusRatio;
  for (size_t i = 0; i < hours.size(); ++i) {
    double to_lower = std::fabs(hours[i].plate_angle - bounds[i]);
    double to_upper = std::fabs(bounds[i + 1] - hours[i].plate_angle);
    double clearance = numeral_radius * std::sin(std::min(to_lower, to_upper)) - tick_r;
    Vec2 dir{std::cos(hours[i].plate_angle), std::sin(hours[i].plate_angle)};
    scene.hour_marks.push_back(HourMark{dir, numeral_radius, plate_radius * kNumeralHeightRatio,
                                         std::max(0.0, clearance), hours[i].hour});
  }

  // Boundaries are rays from the gnomon foot out to the rim. An hour line is
  // the plate's intersection with a plane containing the style, so every hour
  // line passes through the style-plate intersection -- here the plate centre.
  // Only the side away from the light casts shadow, hence a ray rather than a
  // full line (github.com/tpeach90/sundials keeps lambda >= 0 for the same
  // reason).
  //
  // Parallel hour lines, from a style lying in the plate, can't occur here:
  // kMinStyleElevationDeg rules it out.
  double foot_clearance = 2.0 * tick_r;  // just off the rod, so the lines read as converging on it
  for (double angle : bounds) {
    Vec2 dir{std::cos(angle), std::sin(angle)};
    // Centered on the plate surface, so half the capsule stands proud of it
    // as a raised ridge the tracer can shade and shadow like everything else.
    scene.rods.push_back(SceneRod{Vec3{dir.x * foot_clearance, dir.y * foot_clearance, 0.0},
                                   Vec3{dir.x * plate_radius, dir.y * plate_radius, 0.0}, tick_r,
                                   DialMaterial::Tick});
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
