#include "auto_dial.h"

#include "dial_clock.h"

// The dial that follows the sky: it opens on whichever of the two can be read
// and hands over whenever the current one stops working -- which happens when
// the gnomon's shadow runs off the plate, some while before the body actually
// sets. See dial_clock.h for the changeover rule and the crossfade.
std::unique_ptr<ClockFace> make_auto_dial_clock(const Config& cfg) {
  return make_dial_clock(cfg, DialMode::Auto);
}
