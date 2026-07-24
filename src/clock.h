#pragma once

#include <SDL.h>

#include <memory>

#include "types.h"

struct RingState {
  bool ringing = false;
  double hold_progress = 0.0;
  bool alarm_armed = false;
};

// A ClockFace draws the current time (and ringing indicator) in one visual
// style. Themes that share a style differ only by the colors in Config; a
// genuinely different look is a new subclass selected by make_clock_face.
//
// `win` is the owning SDL_Window. Most faces ignore it and draw through `r`
// (the shared SDL_Renderer) as before. GL-based faces (sundial/moondial) use
// it instead to create/make-current their own SDL_GLContext and present via
// SDL_GL_SwapWindow, bypassing `r` for that frame -- see src/dial_gl.h.
class ClockFace {
 public:
  virtual ~ClockFace() = default;
  virtual void render(SDL_Window* win, SDL_Renderer* r, int ww, int wh, const Config& cfg,
                       const RingState& ring) = 0;
};

std::unique_ptr<ClockFace> make_clock_face(const Config& cfg);
