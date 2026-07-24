#include "procedural_texture.h"

#include <algorithm>
#include <cmath>

namespace {

// Deterministic 2D lattice hash -> [0,1). splitmix64-style mixing so nearby
// lattice coordinates decorrelate fully; no global state, so generation is
// reproducible for a given seed across platforms.
double lattice_value(uint32_t seed, uint32_t octave, int64_t x, int64_t y) {
  uint64_t h = (static_cast<uint64_t>(seed) << 32) ^ (static_cast<uint64_t>(octave) << 40) ^
               (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 20) ^ static_cast<uint32_t>(y);
  h += 0x9e3779b97f4a7c15ULL;
  h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
  h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
  h = h ^ (h >> 31);
  return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);  // 53-bit mantissa
}

double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// Periodic value noise: the lattice wraps at `cells`, so noise(u+1, v) ==
// noise(u, v) for u,v in tile units -- this is what makes every derived map
// seamless under GL_REPEAT.
double periodic_noise(uint32_t seed, uint32_t octave, double u, double v, int cells) {
  double x = u * cells, y = v * cells;
  int64_t x0 = static_cast<int64_t>(std::floor(x)), y0 = static_cast<int64_t>(std::floor(y));
  double fx = x - static_cast<double>(x0), fy = y - static_cast<double>(y0);
  auto wrap = [&](int64_t a) { return ((a % cells) + cells) % cells; };
  double v00 = lattice_value(seed, octave, wrap(x0), wrap(y0));
  double v10 = lattice_value(seed, octave, wrap(x0 + 1), wrap(y0));
  double v01 = lattice_value(seed, octave, wrap(x0), wrap(y0 + 1));
  double v11 = lattice_value(seed, octave, wrap(x0 + 1), wrap(y0 + 1));
  double sx = fade(fx), sy = fade(fy);
  double a = v00 + (v10 - v00) * sx;
  double b = v01 + (v11 - v01) * sx;
  return a + (b - a) * sy;
}

// Standard fBm over the periodic noise, normalized back to [0,1].
double fbm(uint32_t seed, double u, double v, int base_cells, int octaves) {
  double sum = 0.0, amp = 1.0, amp_total = 0.0;
  int cells = base_cells;
  for (int o = 0; o < octaves; ++o) {
    sum += amp * periodic_noise(seed, static_cast<uint32_t>(o), u, v, cells);
    amp_total += amp;
    amp *= 0.5;
    cells *= 2;
  }
  return sum / amp_total;
}

uint8_t to_byte(double x) {
  return static_cast<uint8_t>(std::clamp(x, 0.0, 1.0) * 255.0 + 0.5);
}

}  // namespace

SurfaceMaps generate_speckled_stone(int size, const SpeckledStoneParams& p) {
  SurfaceMaps maps;
  maps.size = size;
  maps.diffuse.resize(static_cast<size_t>(size) * size * 3);
  maps.normal.resize(static_cast<size_t>(size) * size * 3);
  maps.specular.resize(static_cast<size_t>(size) * size * 3);

  int base_cells = std::max(1, static_cast<int>(p.base_cells));

  // Height field first (one channel), then diffuse/normal/specular are all
  // derived from it so the grain visibly agrees across the three maps --
  // bumps darken the diffuse and dull the specular exactly where the normal
  // map says the surface is rough.
  std::vector<double> height(static_cast<size_t>(size) * size);
  std::vector<double> patches(static_cast<size_t>(size) * size);
  for (int yi = 0; yi < size; ++yi) {
    for (int xi = 0; xi < size; ++xi) {
      double u = (xi + 0.5) / size, v = (yi + 0.5) / size;
      height[static_cast<size_t>(yi) * size + xi] = fbm(p.seed, u, v, base_cells, p.octaves);
      // Broad, low-frequency patches (independent seed lane) for wear:
      // polished smooth areas vs rough matte ones in the specular map.
      patches[static_cast<size_t>(yi) * size + xi] = fbm(p.seed ^ 0x5157u, u, v, std::max(1, base_cells / 2), 2);
    }
  }

  auto at = [&](int x, int y) {
    x = (x % size + size) % size;
    y = (y % size + size) % size;
    return height[static_cast<size_t>(y) * size + x];
  };

  for (int yi = 0; yi < size; ++yi) {
    for (int xi = 0; xi < size; ++xi) {
      size_t idx = (static_cast<size_t>(yi) * size + xi) * 3;
      double h = at(xi, yi);

      double d = p.base_brightness + (h - 0.5) * 2.0 * p.contrast;
      maps.diffuse[idx + 0] = maps.diffuse[idx + 1] = maps.diffuse[idx + 2] = to_byte(d);

      // Central-difference slope of the height field (wrapped, so the normal
      // map tiles too), scaled by bump_strength.
      double sx = (at(xi + 1, yi) - at(xi - 1, yi)) * p.bump_strength * size / 64.0;
      double sy = (at(xi, yi + 1) - at(xi, yi - 1)) * p.bump_strength * size / 64.0;
      double inv_len = 1.0 / std::sqrt(sx * sx + sy * sy + 1.0);
      maps.normal[idx + 0] = to_byte((-sx * inv_len) * 0.5 + 0.5);
      maps.normal[idx + 1] = to_byte((-sy * inv_len) * 0.5 + 0.5);
      maps.normal[idx + 2] = to_byte((1.0 * inv_len) * 0.5 + 0.5);

      double wear = patches[static_cast<size_t>(yi) * size + xi];
      double s = p.spec_base + (wear - 0.5) * 2.0 * p.spec_contrast - (h - 0.5) * 0.2;
      maps.specular[idx + 0] = maps.specular[idx + 1] = maps.specular[idx + 2] = to_byte(s);
    }
  }
  return maps;
}
