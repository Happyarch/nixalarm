#pragma once

#include <memory>

#include "clock.h"
#include "types.h"

// The sundial ClockFace: an OpenGL-rendered tilted stone plate + bronze
// gnomon, tilt/facing chosen once at startup by dial_geometry.h's legibility
// optimizer for cfg.latitude, showing mean solar time via the true live sun
// direction (dial_geometry's fixed hour grooves absorb the difference from
// apparent solar time, not shown here yet -- see project memory for the
// still-pending equation-of-time correction ring).
std::unique_ptr<ClockFace> make_sundial_clock(const Config& cfg);
