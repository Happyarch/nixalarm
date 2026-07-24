#pragma once

#include <memory>

#include "clock.h"

// The photoreal nixie-tube face. Composites a baked empty-tube plate with
// per-digit glow-wire emission through a static transmittance map (glass + anode
// mesh). Assets are loaded from disk at runtime; see assets/runtime/nixie.
std::unique_ptr<ClockFace> make_nixie_clock();
