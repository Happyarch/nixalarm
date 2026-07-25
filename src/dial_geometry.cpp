#include "dial_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
double deg2rad(double d) { return d * kPi / 180.0; }

// Standard right-hand-rule rotation about the world Y (east) axis.
Vec3 rotate_y(Vec3 v, double angle_deg) {
  double a = deg2rad(angle_deg);
  double c = std::cos(a), s = std::sin(a);
  return Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

// Standard right-hand-rule rotation about the world Z (zenith) axis.
Vec3 rotate_z(Vec3 v, double angle_deg) {
  double a = deg2rad(angle_deg);
  double c = std::cos(a), s = std::sin(a);
  return Vec3{v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

Vec3 az_alt_to_world(HorizontalCoord hc) {
  double alt_rad = deg2rad(hc.altitude_deg);
  double az_rad = deg2rad(hc.azimuth_deg);
  double ca = std::cos(alt_rad);
  return Vec3{ca * std::cos(az_rad), ca * std::sin(az_rad), std::sin(alt_rad)};
}

// Internal fast-path variants taking a precomputed frame/style vector, so the
// legibility optimizer's inner hour-angle sweep doesn't rebuild the plate
// frame (6 rotations) on every one of its ~288 samples per grid point.

Vec2 hour_line_direction_impl(double latitude_deg, Vec3 style_w, const PlateFrame& frame,
                               double hour_angle_deg, bool moondial) {
  Vec3 light_w = idealized_light_direction_world(latitude_deg, hour_angle_deg, moondial);
  Vec3 shadow_plane_normal = normalize(cross(style_w, light_w));
  Vec3 line_dir_w = normalize(cross(shadow_plane_normal, frame.n));
  Vec3 local = project_to_local(line_dir_w, frame);
  return Vec2{local.x, local.y};
}

ShadowSample shadow_point_impl(double /*latitude_deg*/, Vec3 style_w, const PlateFrame& frame,
                                double gnomon_length, double hour_angle_deg, double lat_for_light,
                                bool moondial, double declination_deg = 0.0) {
  Vec3 light_w = idealized_light_direction_world(lat_for_light, hour_angle_deg, moondial, declination_deg);
  Vec3 style_local = project_to_local(style_w, frame);
  if (style_local.z <= 0.0) return ShadowSample{Vec2{0.0, 0.0}, false};
  Vec3 style_tip_local = style_local * gnomon_length;

  Vec3 light_local = project_to_local(light_w, frame);
  if (light_local.z <= 0.0) return ShadowSample{Vec2{0.0, 0.0}, false};

  double t = style_tip_local.z / light_local.z;
  Vec3 shadow_local = style_tip_local - light_local * t;
  return ShadowSample{Vec2{shadow_local.x, shadow_local.y}, true};
}

struct ObjectiveResult {
  double contiguous_hours = 0.0;
  double avg_shadow_ratio = 1e9;  // lower is better; only meaningful when contiguous_hours > 0
};

constexpr double kHourStepDeg = 1.25;  // 5-minute resolution (15 deg/hour / 12)
constexpr int kHourSamples = static_cast<int>(360.0 / kHourStepDeg);  // 288

// Minimum angle the style must rise above the plate. Without this floor the
// optimizer converges on the degenerate "polar dial" family (style lying IN
// the plate plane, e.g. slant == latitude at declination 0): the gnomon
// triangle's vertical leg collapses to zero height, the rendered wedge
// flattens into an invisible sliver, and -- because the tip sits on the plate
// and its shadow barely moves -- every hour scores as "legible", so the
// objective actively PREFERS a dial that cannot tell time. 20 degrees keeps
// the gnomon a real, visible wedge at every latitude (a horizontal plate
// already satisfies it for |lat| >= 20; nearer the equator the optimizer
// finds a reclining/equatorial plate instead, which is also the classical
// solution there).
constexpr double kMinStyleElevationDeg = 20.0;

ObjectiveResult evaluate_objective(double latitude_deg, double slant_deg, double declination_deg,
                                    double gnomon_length, double plate_radius, double k_legible,
                                    bool moondial) {
  PlateFrame frame = compute_plate_frame(slant_deg, declination_deg);
  Vec3 style_w = style_vector_world(latitude_deg);
  if (project_to_local(style_w, frame).z < std::sin(deg2rad(kMinStyleElevationDeg))) {
    return ObjectiveResult{0.0, 1e9};  // degenerate/near-flat gnomon: reject outright
  }
  double legible_radius = k_legible * plate_radius;

  std::array<bool, kHourSamples> valid{};
  std::array<double, kHourSamples> shadow_ratio{};

  // Judged against the SAME construction declination the hour lines are laid
  // out for (the observer's summer solstice). Optimizing the plate's tilt for
  // the equinox and then drawing solstice hour lines would pick a tilt whose
  // own plane cuts off the very early and late hours the lines were added
  // for -- the two have to agree about what day the dial is built for.
  double construction_decl = construction_declination_deg(latitude_deg);

  for (int i = 0; i < kHourSamples; ++i) {
    double h = -180.0 + i * kHourStepDeg;
    Vec3 light_w = idealized_light_direction_world(latitude_deg, h, moondial, construction_decl);
    bool above_horizon = light_w.z > 0.0;
    ShadowSample s = shadow_point_impl(latitude_deg, style_w, frame, gnomon_length, h, latitude_deg, moondial,
                                        construction_decl);
    double len = std::sqrt(s.point_local.x * s.point_local.x + s.point_local.y * s.point_local.y);
    bool ok = above_horizon && s.valid && (len <= plate_radius) && (len <= legible_radius);
    valid[i] = ok;
    shadow_ratio[i] = ok ? (len / plate_radius) : 0.0;
  }

  bool all_valid = std::all_of(valid.begin(), valid.end(), [](bool b) { return b; });
  if (all_valid) {
    double avg = 0.0;
    for (double r : shadow_ratio) avg += r;
    return ObjectiveResult{24.0, avg / kHourSamples};
  }

  // Find an index where valid is false, to avoid needing wraparound logic --
  // rotate the conceptual start there so the longest true-run can't be split
  // across the array boundary.
  int start = 0;
  for (int i = 0; i < kHourSamples; ++i) {
    if (!valid[i]) {
      start = i;
      break;
    }
  }

  int best_run = 0, cur_run = 0;
  double best_run_sum = 0.0, cur_run_sum = 0.0;
  for (int k = 0; k < kHourSamples; ++k) {
    int i = (start + k) % kHourSamples;
    if (valid[i]) {
      cur_run += 1;
      cur_run_sum += shadow_ratio[i];
    } else {
      if (cur_run > best_run) {
        best_run = cur_run;
        best_run_sum = cur_run_sum;
      }
      cur_run = 0;
      cur_run_sum = 0.0;
    }
  }
  if (cur_run > best_run) {
    best_run = cur_run;
    best_run_sum = cur_run_sum;
  }

  double contiguous_hours = (best_run * kHourStepDeg) / 15.0;
  double avg_ratio = best_run > 0 ? (best_run_sum / best_run) : 1e9;
  return ObjectiveResult{contiguous_hours, avg_ratio};
}

// Deterministic scalar score for Nelder-Mead: contiguous hours dominate;
// among near-ties, prefer a lower average shadow-length ratio (shadow well
// within the legible zone, not just barely inside it).
double score_of(const ObjectiveResult& r) { return r.contiguous_hours - 1e-6 * r.avg_shadow_ratio; }

struct Point {
  double slant, declination;
};

// Small, self-contained 2D Nelder-Mead maximizer. The objective is a step
// function (quantized by kHourStepDeg), so this is deliberately simple rather
// than a fully general/robust implementation -- it only needs to polish a
// grid-search result to sub-grid-step precision, not converge from scratch.
Point nelder_mead_refine(double latitude_deg, double gnomon_length, double plate_radius,
                          double k_legible, bool moondial, Point start) {
  auto eval = [&](Point p) {
    return score_of(evaluate_objective(latitude_deg, p.slant, p.declination, gnomon_length,
                                        plate_radius, k_legible, moondial));
  };

  std::array<Point, 3> simplex = {
      start, Point{start.slant + 1.0, start.declination}, Point{start.slant, start.declination + 1.0}};
  std::array<double, 3> vals = {eval(simplex[0]), eval(simplex[1]), eval(simplex[2])};

  for (int iter = 0; iter < 60; ++iter) {
    int lo = 0, mid = 1, hi = 2;
    if (vals[mid] > vals[lo]) std::swap(lo, mid);
    if (vals[hi] > vals[lo]) std::swap(lo, hi);
    if (vals[hi] > vals[mid]) std::swap(mid, hi);
    // now vals[lo] best, vals[hi] worst, vals[mid] middle

    Point centroid{(simplex[lo].slant + simplex[mid].slant) / 2.0,
                    (simplex[lo].declination + simplex[mid].declination) / 2.0};
    Point reflected{centroid.slant + (centroid.slant - simplex[hi].slant),
                     centroid.declination + (centroid.declination - simplex[hi].declination)};
    double reflected_val = eval(reflected);

    if (reflected_val > vals[lo]) {
      Point expanded{centroid.slant + 2.0 * (centroid.slant - simplex[hi].slant),
                      centroid.declination + 2.0 * (centroid.declination - simplex[hi].declination)};
      double expanded_val = eval(expanded);
      if (expanded_val > reflected_val) {
        simplex[hi] = expanded;
        vals[hi] = expanded_val;
      } else {
        simplex[hi] = reflected;
        vals[hi] = reflected_val;
      }
    } else if (reflected_val > vals[mid]) {
      simplex[hi] = reflected;
      vals[hi] = reflected_val;
    } else {
      Point contracted{centroid.slant + 0.5 * (simplex[hi].slant - centroid.slant),
                        centroid.declination + 0.5 * (simplex[hi].declination - centroid.declination)};
      double contracted_val = eval(contracted);
      if (contracted_val > vals[hi]) {
        simplex[hi] = contracted;
        vals[hi] = contracted_val;
      } else {
        for (int i = 0; i < 3; ++i) {
          if (i == lo) continue;
          simplex[i].slant = simplex[lo].slant + 0.5 * (simplex[i].slant - simplex[lo].slant);
          simplex[i].declination = simplex[lo].declination + 0.5 * (simplex[i].declination - simplex[lo].declination);
          vals[i] = eval(simplex[i]);
        }
      }
    }
  }

  int best = 0;
  for (int i = 1; i < 3; ++i)
    if (vals[i] > vals[best]) best = i;
  return simplex[best];
}

}  // namespace

Vec3 operator+(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, double s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(Vec3 v) { return std::sqrt(dot(v, v)); }
Vec3 normalize(Vec3 v) {
  double len = length(v);
  if (len < 1e-12) return Vec3{0.0, 0.0, 0.0};
  return v * (1.0 / len);
}

PlateFrame compute_plate_frame(double slant_deg, double declination_deg) {
  Vec3 n0{0.0, 0.0, 1.0}, u0{1.0, 0.0, 0.0}, v0{0.0, 1.0, 0.0};
  // Tip away from horizontal about the east axis; negate the angle so a
  // positive slant tips the normal toward south by default (rotate_y's
  // standard convention tips toward north for positive angles).
  Vec3 n1 = rotate_y(n0, -slant_deg);
  Vec3 u1 = rotate_y(u0, -slant_deg);
  Vec3 v1 = rotate_y(v0, -slant_deg);
  // Spin about zenith by declination, measured clockwise from south.
  return PlateFrame{rotate_z(u1, declination_deg), rotate_z(v1, declination_deg), rotate_z(n1, declination_deg)};
}

PlateFrame substyle_aligned_plate_frame(double latitude_deg, double slant_deg, double declination_deg) {
  PlateFrame frame = compute_plate_frame(slant_deg, declination_deg);
  Vec3 s = project_to_local(style_vector_world(latitude_deg), frame);
  double horiz = std::sqrt(s.x * s.x + s.y * s.y);
  if (horiz < 1e-9) return frame;  // style perpendicular to plate: substyle undefined
  double c = s.x / horiz, sn = s.y / horiz;
  // Spin u toward the substyle within the plate plane; v follows to stay a
  // right-handed orthonormal frame with the same normal.
  Vec3 u = frame.u * c + frame.v * sn;
  Vec3 v = frame.v * c - frame.u * sn;
  return PlateFrame{u, v, frame.n};
}

Vec3 style_vector_world(double latitude_deg) {
  double lat_rad = deg2rad(latitude_deg);
  return Vec3{std::cos(lat_rad), 0.0, std::sin(lat_rad)};
}

Vec3 project_to_local(Vec3 world_vec, const PlateFrame& frame) {
  return Vec3{dot(world_vec, frame.u), dot(world_vec, frame.v), dot(world_vec, frame.n)};
}

Vec3 idealized_light_direction_world(double latitude_deg, double hour_angle_deg, bool moondial,
                                      double declination_deg) {
  double h = moondial ? hour_angle_deg + 180.0 : hour_angle_deg;
  EquatorialCoord eq{0.0, declination_deg};  // construction light: ra=0 (reference meridian)
  double lst_hours = h / 15.0;  // hour_angle = lst*15 - ra = lst*15 since ra=0
  return az_alt_to_world(equatorial_to_horizontal(eq, latitude_deg, lst_hours));
}

double construction_declination_deg(double latitude_deg) {
  return latitude_deg >= 0.0 ? kObliquityDeg : -kObliquityDeg;
}

Vec3 horizontal_to_world(HorizontalCoord hc) { return az_alt_to_world(hc); }

Vec2 hour_line_direction(double latitude_deg, double slant_deg, double declination_deg,
                          double hour_angle_deg, bool moondial) {
  PlateFrame frame = compute_plate_frame(slant_deg, declination_deg);
  Vec3 style_w = style_vector_world(latitude_deg);
  return hour_line_direction_impl(latitude_deg, style_w, frame, hour_angle_deg, moondial);
}

ShadowSample shadow_point_on_plate(double latitude_deg, double slant_deg, double declination_deg,
                                     double gnomon_length, double hour_angle_deg, bool moondial) {
  PlateFrame frame = compute_plate_frame(slant_deg, declination_deg);
  return shadow_point_on_plate(latitude_deg, frame, gnomon_length, hour_angle_deg, moondial);
}

ShadowSample shadow_point_on_plate(double latitude_deg, const PlateFrame& frame, double gnomon_length,
                                     double hour_angle_deg, bool moondial, double declination_deg) {
  Vec3 style_w = style_vector_world(latitude_deg);
  return shadow_point_impl(latitude_deg, style_w, frame, gnomon_length, hour_angle_deg, latitude_deg, moondial,
                            declination_deg);
}

DialOrientation optimize_dial_orientation(double latitude_deg, double gnomon_length,
                                           double plate_radius, double k_legible, bool moondial) {
  Point best{0.0, 0.0};
  ObjectiveResult best_result{-1.0, 1e9};

  for (double slant = 0.0; slant <= 180.0; slant += 2.0) {
    for (double decl = -180.0; decl <= 180.0; decl += 2.0) {
      ObjectiveResult r =
          evaluate_objective(latitude_deg, slant, decl, gnomon_length, plate_radius, k_legible, moondial);
      if (score_of(r) > score_of(best_result)) {
        best_result = r;
        best = Point{slant, decl};
      }
    }
  }

  Point refined = nelder_mead_refine(latitude_deg, gnomon_length, plate_radius, k_legible, moondial, best);
  ObjectiveResult refined_result = evaluate_objective(latitude_deg, refined.slant, refined.declination,
                                                        gnomon_length, plate_radius, k_legible, moondial);
  if (score_of(refined_result) < score_of(best_result)) {
    // Refinement can't do worse than the grid optimum it started from in
    // principle, but guard defensively against a step-function plateau edge
    // case where it wandered somewhere nominally-tied but slightly worse.
    refined = best;
    refined_result = best_result;
  }

  return DialOrientation{refined.slant, refined.declination, refined_result.contiguous_hours};
}

CameraWorldPose camera_world_pose(const PlateFrame& frame, const FixedCameraOffset& offset) {
  auto to_world = [&](Vec3 local) {
    return frame.u * local.x + frame.v * local.y + frame.n * local.z;
  };
  return CameraWorldPose{to_world(offset.position_in_plate_frame), to_world(offset.look_target_in_plate_frame),
                          to_world(offset.up_hint_in_plate_frame)};
}

Vec3 light_direction_local(const PlateFrame& frame, Vec3 light_dir_world) {
  return project_to_local(light_dir_world, frame);
}
