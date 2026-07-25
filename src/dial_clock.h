#pragma once

// Shared ClockFace implementation behind both src/sundial.h and
// src/moondial.h. The two are the same dial read by two different lights, so
// this class builds BOTH forms up front -- the sun dial and the moon dial are
// genuinely different plates (the optimizer picks a different tilt for each,
// and the hour lines run in different directions), so they can't be one scene
// with a swapped light -- and shows whichever one the sky currently supports.
//
// A sundial that goes blank at dusk is a dead clock, so when the sun sets and
// the moon is up the face changes over to the moon dial, and back at sunrise.
// The changeover is visible: the plate re-tilts and the hour lines jump,
// because they really are two different instruments. When neither body is up
// there is no time to tell, so the face holds whichever form it was already
// showing (dark) rather than flipping to the other for no reason.

#include <memory>

#include "clock.h"
#include "dial_gl.h"
#include "dial_scene.h"
#include "types.h"

class DialClockFace : public ClockFace {
 public:
  // `prefer_moondial` is the configured theme. It decides only which form is
  // shown before the sky has been consulted and when neither body is up; from
  // then on the sky decides.
  DialClockFace(const Config& cfg, bool prefer_moondial);
  void render(SDL_Window* win, SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) override;

 private:
  // One complete dial: its optimized orientation, the plate frame that
  // orientation implies, the traced scene, and the light grade it's read by.
  // Everything here is fixed once built; only which of the two is current
  // changes at runtime.
  struct Form {
    bool moondial = false;
    DialOrientation orientation;
    PlateFrame frame;
    DialScene scene;
    DialPalette palette;
  };

  static Form make_form(const Config& cfg, bool moondial, const DialPalette& palette);

  // Unit vector in `form`'s own plate frame, pointing at the body that form
  // is read by. Per-form because the two have different plate frames.
  Vec3 light_for(const Form& form, double jd) const;

  // Whether the dial still tells the time -- see the .cpp; this is about the
  // gnomon's shadow landing on the plate, not merely about the body being up.
  bool is_readable(const Form& form, double jd) const;

  void update_form_choice(double jd, double now_seconds);

  double latitude_deg_;
  double longitude_deg_;
  Form sun_form_;
  Form moon_form_;
  FixedCameraOffset camera_;
  DialGlRenderer gl_renderer_;
  bool showing_moondial_;
  bool fading_ = false;
  double fade_start_seconds_ = 0.0;
};

std::unique_ptr<ClockFace> make_dial_clock(const Config& cfg, bool prefer_moondial);
