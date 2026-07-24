#pragma once

// Cuts the dial's Roman hour numerals into the plate as a HEIGHT FIELD rather
// than painting them on as a decal. The result is fed to the tracer as a bump
// source (src/dial_gl.cpp perturbs the plate normal by its gradient), so the
// numerals are lit by the same raking sun or moonlight as the stone around
// them: they fill with shadow when the light is low and nearly vanish when it
// is overhead, exactly as a real chiselled dial does. Nothing here knows about
// OpenGL -- it is plain CPU rasterization, so it is unit-testable without a
// window or context, same as dial_scene.
//
// Strokes are cut as V-grooves (depth falls off linearly from the stroke
// centerline to its edge) because a flat-bottomed cut has no gradient in its
// interior and would light up as a flat plateau instead of a groove.

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
