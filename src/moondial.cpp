#include "moondial.h"

#include "dial_clock.h"

// The moon dial, pinned -- the night half of the pair, dark by day. See
// src/sundial.cpp and dial_clock.h; `auto_dial` is the theme that moves
// between the two.
std::unique_ptr<ClockFace> make_moondial_clock(const Config& cfg) {
  return make_dial_clock(cfg, DialMode::MoonOnly);
}
