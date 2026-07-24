#pragma once

// Shared ClockFace implementation behind both src/sundial.h and
// src/moondial.h -- the two faces differ only in `moondial` (which flips the
// hour-angle/horizon convention throughout dial_geometry.h) and their
// DialPalette (surface tints + the day/night light grade); everything else (orientation optimization, scene
// generation, raytraced GL rendering) is identical, so it isn't duplicated.

#include <memory>

#include "clock.h"
#include "dial_gl.h"
#include "dial_scene.h"
#include "types.h"

class DialClockFace : public ClockFace {
 public:
  DialClockFace(const Config& cfg, bool moondial, const DialPalette& palette);
  void render(SDL_Window* win, SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) override;

 private:
  bool moondial_;
  double latitude_deg_;
  double longitude_deg_;
  DialOrientation orientation_;
  PlateFrame frame_;
  DialScene scene_;
  FixedCameraOffset camera_;
  DialGlRenderer gl_renderer_;
  DialPalette palette_;
};

std::unique_ptr<ClockFace> make_dial_clock(const Config& cfg, bool moondial, const DialPalette& palette);
