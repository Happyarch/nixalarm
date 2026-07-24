#pragma once

// Procedural surface-map generation (diffuse / tangent-space normal /
// specular), GL-free and deterministic, so it's unit-testable and reusable by
// any future theme or clock face that wants material texture without shipping
// image assets. The dial faces tint the neutral diffuse map with their
// per-material color at shading time, so ONE generated set serves several
// differently-colored surfaces.
//
// The underlying noise is lattice-periodic: every map tiles seamlessly under
// GL_REPEAT by construction.

#include <cstdint>
#include <vector>

struct SurfaceMaps {
  int size = 0;
  std::vector<uint8_t> diffuse;   // RGB8, neutral gray modulation (tint with material color)
  std::vector<uint8_t> normal;    // RGB8 tangent-space, (128,128,255) = flat
  std::vector<uint8_t> specular;  // RGB8 gray = per-texel specular strength
};

struct SpeckledStoneParams {
  uint32_t seed = 1;
  int octaves = 5;             // fBm depth; more = finer grain detail
  double base_cells = 6.0;     // lattice cells across the tile at octave 0
  double bump_strength = 2.2;  // normal-map slope exaggeration
  double base_brightness = 0.82;  // mean diffuse level in [0,1]
  double contrast = 0.22;         // diffuse modulation depth
  double spec_base = 0.22;        // mean specular strength
  double spec_contrast = 0.45;    // specular modulation depth (worn/polished patches)
};

// A weathered speckled-stone look: fBm grain in the diffuse, matching bumps
// in the normal map, and broad worn-smooth patches in the specular map.
SurfaceMaps generate_speckled_stone(int size, const SpeckledStoneParams& params);
