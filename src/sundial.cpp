#include "sundial.h"

#include "dial_clock.h"

// Both dial themes are the same face (see dial_clock.h): it builds the sun
// dial and the moon dial and shows whichever the sky currently supports,
// changing over when one stops being readable. The theme only says which one
// to open with, and which to fall back on when neither body can be read --
// so `sundial` is the dial that starts in the day and holds daylight when the
// night is moonless. The palettes for both live in dial_clock.cpp.
std::unique_ptr<ClockFace> make_sundial_clock(const Config& cfg) {
  return make_dial_clock(cfg, /*prefer_moondial=*/false);
}
