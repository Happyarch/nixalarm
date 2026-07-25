#pragma once

// The `auto_dial` theme: the sun dial and the moon dial as one clock, showing
// whichever can currently be read and crossfading between them. The two pinned
// halves are src/sundial.h and src/moondial.h; all three are the same face in
// different DialModes (src/dial_clock.h).

#include <memory>

#include "clock.h"
#include "types.h"

std::unique_ptr<ClockFace> make_auto_dial_clock(const Config& cfg);
