#include "dial_engrave.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Glyph space: cap height 1, baseline at y = -0.5, cap line at y = +0.5, and
// each letter centered on x = 0 within its own advance width. Only three
// letters exist in I..XII, and each is a couple of straight strokes, so the
// "font" is this table rather than an outline format.
constexpr double kStrokeHalfWidth = 0.085;  // in glyph units
constexpr double kLetterSpacing = 0.10;

struct Stroke {
  double x0, y0, x1, y1;
};

struct Glyph {
  double advance;
  int stroke_count;
  Stroke strokes[2];
};

Glyph glyph_for(char c) {
  switch (c) {
    case 'I':
      return Glyph{0.34, 1, {Stroke{0.0, -0.5, 0.0, 0.5}, Stroke{}}};
    case 'V':
      return Glyph{0.76, 2, {Stroke{-0.30, 0.5, 0.0, -0.5}, Stroke{0.30, 0.5, 0.0, -0.5}}};
    case 'X':
      return Glyph{0.76, 2, {Stroke{-0.30, 0.5, 0.30, -0.5}, Stroke{0.30, 0.5, -0.30, -0.5}}};
    default:
      return Glyph{0.0, 0, {Stroke{}, Stroke{}}};
  }
}

double advance_width(const char* text) {
  double total = 0.0;
  for (const char* c = text; *c; ++c) total += glyph_for(*c).advance + kLetterSpacing;
  return total > 0.0 ? total - kLetterSpacing : 0.0;
}

// Hour lines are not evenly spaced -- they crowd together toward the ends of
// the day, where a full-size XII would run straight through its neighbours.
// Each numeral is therefore shrunk to fit the angular gap it actually has,
// which is what a dial maker does by hand. Returns the cap height to use.
double fitted_height(const std::vector<HourMark>& marks, size_t i, double advance) {
  const HourMark& m = marks[i];
  if (advance <= 0.0) return 0.0;
  double angle = std::atan2(m.direction.y, m.direction.x);
  double smallest_gap = 3.14159265358979323846;  // no neighbour: keep nominal size
  for (size_t k = 0; k < marks.size(); ++k) {
    if (k == i) continue;
    double other = std::atan2(marks[k].direction.y, marks[k].direction.x);
    double d = std::fabs(angle - other);
    if (d > 3.14159265358979323846) d = 2.0 * 3.14159265358979323846 - d;
    smallest_gap = std::min(smallest_gap, d);
  }
  // Chord available at the numeral's radius, with a margin so neighbours
  // don't touch even at their widest.
  double available = 0.86 * smallest_gap * m.radius;
  return std::min(m.height, available / advance);
}

double distance_to_segment(double px, double py, double ax, double ay, double bx, double by) {
  double vx = bx - ax, vy = by - ay;
  double wx = px - ax, wy = py - ay;
  double vv = vx * vx + vy * vy;
  double t = (vv > 1e-18) ? std::clamp((wx * vx + wy * vy) / vv, 0.0, 1.0) : 0.0;
  double dx = wx - t * vx, dy = wy - t * vy;
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

const char* roman_numeral(int hour) {
  static const char* kNumerals[12] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
  if (hour < 1 || hour > 12) return "";
  return kNumerals[hour - 1];
}

EngravingMap build_hour_numeral_engraving(const DialScene& scene, int size) {
  EngravingMap map;
  map.size = std::max(size, 1);
  map.depth.assign(static_cast<size_t>(map.size) * static_cast<size_t>(map.size), 0);
  if (scene.plate_radius <= 0.0) return map;

  // Plate-local units per texel, and its inverse, for the two directions we
  // convert between: laying strokes out in plate space, and walking the texels
  // a stroke's bounding box covers.
  const double extent = 2.0 * scene.plate_radius;
  const double units_per_texel = extent / static_cast<double>(map.size);

  for (size_t mark_index = 0; mark_index < scene.hour_marks.size(); ++mark_index) {
    const HourMark& mark = scene.hour_marks[mark_index];
    const char* text = roman_numeral(mark.hour);
    if (!*text) continue;

    // The numeral stands upright along the hour line with its cap line facing
    // the rim: `up` is the outward radial, `right` its clockwise perpendicular
    // so the lettering isn't mirrored when read from above the plate.
    double ux = mark.direction.x, uy = mark.direction.y;
    double rx = uy, ry = -ux;
    double cx = ux * mark.radius, cy = uy * mark.radius;

    double total_advance = advance_width(text);
    double scale = fitted_height(scene.hour_marks, mark_index, total_advance);
    if (scale <= 0.0) continue;

    double pen = -0.5 * total_advance;
    for (const char* c = text; *c; ++c) {
      Glyph g = glyph_for(*c);
      double letter_center = pen + 0.5 * g.advance;
      for (int s = 0; s < g.stroke_count; ++s) {
        const Stroke& st = g.strokes[s];
        // Stroke endpoints in plate-local coordinates.
        double gx0 = letter_center + st.x0, gx1 = letter_center + st.x1;
        double ax = cx + (rx * gx0 + ux * st.y0) * scale;
        double ay = cy + (ry * gx0 + uy * st.y0) * scale;
        double bx = cx + (rx * gx1 + ux * st.y1) * scale;
        double by = cy + (ry * gx1 + uy * st.y1) * scale;
        double half_w = kStrokeHalfWidth * scale;

        // Only the texels this stroke's bounding box covers -- the numerals
        // occupy a sliver of the plate, so rasterizing the whole image per
        // stroke would be hundreds of times the work for the same result.
        double min_x = std::min(ax, bx) - half_w, max_x = std::max(ax, bx) + half_w;
        double min_y = std::min(ay, by) - half_w, max_y = std::max(ay, by) + half_w;
        int i0 = std::max(0, static_cast<int>(std::floor((min_x + scene.plate_radius) / units_per_texel)));
        int i1 = std::min(map.size - 1, static_cast<int>(std::ceil((max_x + scene.plate_radius) / units_per_texel)));
        int j0 = std::max(0, static_cast<int>(std::floor((min_y + scene.plate_radius) / units_per_texel)));
        int j1 = std::min(map.size - 1, static_cast<int>(std::ceil((max_y + scene.plate_radius) / units_per_texel)));

        for (int j = j0; j <= j1; ++j) {
          double py = -scene.plate_radius + (static_cast<double>(j) + 0.5) * units_per_texel;
          for (int i = i0; i <= i1; ++i) {
            double px = -scene.plate_radius + (static_cast<double>(i) + 0.5) * units_per_texel;
            double d = distance_to_segment(px, py, ax, ay, bx, by);
            if (d >= half_w) continue;
            // V-groove: deepest on the centerline, rising linearly to the lip.
            double depth = 1.0 - d / half_w;
            uint8_t v = static_cast<uint8_t>(std::lround(std::clamp(depth, 0.0, 1.0) * 255.0));
            size_t idx = static_cast<size_t>(j) * static_cast<size_t>(map.size) + static_cast<size_t>(i);
            map.depth[idx] = std::max(map.depth[idx], v);
          }
        }
      }
      pen += g.advance + kLetterSpacing;
    }
  }
  return map;
}
