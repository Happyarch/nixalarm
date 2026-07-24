#pragma once

#include <memory>

#include "clock.h"
#include "types.h"

// The moondial ClockFace: same rendering/geometry machinery as the sundial
// (src/sundial.h), but driven by the live moon direction and the moondial's
// midnight-centered orientation/hour-line convention. The moon's non-full
// phases mean the displayed time drifts from clock time without a rotating
// lunar-age correction ring; that ring is not implemented yet (see project
// memory) -- this face is currently only exactly correct at full moon.
std::unique_ptr<ClockFace> make_moondial_clock(const Config& cfg);
