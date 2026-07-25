#include "moondial.h"

#include "dial_clock.h"

// The night-first half of the pair -- see src/sundial.cpp and dial_clock.h.
// Same face and the same changeover; this theme opens on the moon dial and
// falls back to it when neither body can be read.
std::unique_ptr<ClockFace> make_moondial_clock(const Config& cfg) {
  return make_dial_clock(cfg, /*prefer_moondial=*/true);
}
