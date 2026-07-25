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
  for (double h = -165.0; h <= 165.0; h += 15.0) {
    Vec3 light_w = idealized_light_direction_world(latitude_deg, h, moondial, construction_decl);
    if (light_w.z <= 0.0) continue;  // sun/moon below the true horizon at this hour
    ShadowSample s = shadow_point_on_plate(latitude_deg, frame, gnomon_length, h, moondial, construction_decl);
    if (!s.valid) continue;
    double len = std::sqrt(s.point_local.x * s.point_local.x + s.point_local.y * s.point_local.y);
    if (len < 1e-9) continue;
    // Use the actual finite shadow point's direction (not hour_line_direction,
    // which only gives an undirected line) so the tick lands on the correct
    // side of the noon/midnight line, not its 180-degree mirror.
    Vec2 dir{s.point_local.x / len, s.point_local.y / len};

    // The hour line is CLIPPED to the plate, never dropped for being long.
    // What names the hour is the line's direction, not how far along it the
    // tip's shadow happens to reach; morning and evening shadows run many
    // plate-radii out, and discarding those hours cost the dial its whole
    // early and late range. So every hour the light is up gets its line, and
    // the line simply ends where the shadow does or where the stone does,
    // whichever comes first -- which is also why the lines vary in length,
    // shortest around noon when the shadow is shortest.
    double r_end = std::min(len, r_outer);
    if (r_end <= r_inner) continue;  // shadow that short never reaches the ring
    // Centered on the plate surface, so half the capsule stands proud of it
    // as a raised ridge the tracer can shade and shadow like everything else.
    scene.rods.push_back(SceneRod{Vec3{dir.x * r_inner, dir.y * r_inner, 0.0},
                                   Vec3{dir.x * r_end, dir.y * r_end, 0.0}, tick_r, DialMaterial::Tick});

    // h is the hour angle from the dial's own noon (midnight for a moondial),
    // 15 degrees per hour, so the clock hour is 12 + h/15 -- folded onto the
    // 1..12 a Roman numeral can express.
    int hour24 = 12 + static_cast<int>(std::lround(h / 15.0));
    scene.hour_marks.push_back(HourMark{dir, plate_radius * kNumeralRadiusRatio,
                                         plate_radius * kNumeralHeightRatio, ((hour24 + 11) % 12) + 1});
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
