#pragma once

#include <SDL.h>

#include <memory>

#include "types.h"

struct RingState {
  bool ringing = false;
  double hold_progress = 0.0;
};

// A ClockFace draws the current time (and ringing indicator) in one visual
// style. Themes that share a style differ only by the colors in Config; a
// genuinely different look is a new subclass selected by make_clock_face.
class ClockFace {
 public:
  virtual ~ClockFace() = default;
  virtual void render(SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) = 0;
};

std::unique_ptr<ClockFace> make_clock_face(const Config& cfg);
