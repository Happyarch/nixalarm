#pragma once

// Rasterizes the dial's Roman hour numerals into a height field. dial_gl feeds
// it to the tracer as a bump source, perturbing the plate normal by its
// gradient, so the numerals catch the same raking light as the stone instead
// of reading as a decal.
//
// Strokes are V-grooves: depth falls off linearly from the centerline. A
// flat-bottomed cut has no gradient in its interior and lights as a plateau.
//
// No GL dependency, so it is testable without a context -- as dial_scene.

#include <cstdint>
#include <vector>

#include "dial_scene.h"

// A single-channel depth image covering the plate's bounding square: texel
// (0, 0) is plate-local (-R, -R) and texel (size-1, size-1) is (+R, +R), with
// x running along the row and y up the columns. 0 = uncut plate surface,
// 255 = the deepest point of a stroke. Row-major, `size * size` bytes.
struct EngravingMap {
  int size = 0;
  std::vector<uint8_t> depth;
};

// Rasterizes scene.hour_marks. `size` is the edge length in texels; a scene
// with no hour marks yields an all-zero map of that size rather than an empty
// one, so the caller can upload it unconditionally.
EngravingMap build_hour_numeral_engraving(const DialScene& scene, int size);

// The Roman numeral for an hour in 1..12, using the subtractive IV/IX forms to
// match the analog face's Roman dial. Hours outside 1..12 return an empty
// string. Exposed for testing.
const char* roman_numeral(int hour);
