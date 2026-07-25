#include "sundial.h"

#include "dial_clock.h"

// The sun dial, pinned: never changes, goes dark when the sun does. `auto_dial`
// is the theme that hands over to the moon dial instead. All three dial themes
// are the same face in different DialModes -- see dial_clock.h.
std::unique_ptr<ClockFace> make_sundial_clock(const Config& cfg) {
  return make_dial_clock(cfg, DialMode::SunOnly);
}
