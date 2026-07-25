#include "sundial.h"

#include "dial_clock.h"

// The sun dial, pinned: it is only ever the sun's dial, and goes dark when the
// sun is gone, like the real thing in a garden. Use the `auto_dial` theme if
// you want it to hand over to the moon dial after dark instead. All three dial
// themes are the same face in different DialModes -- see dial_clock.h.
std::unique_ptr<ClockFace> make_sundial_clock(const Config& cfg) {
  return make_dial_clock(cfg, DialMode::SunOnly);
}
