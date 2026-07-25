#pragma once

// Shared ClockFace implementation behind src/sundial.h, src/moondial.h and
// src/auto_dial.h. The sun dial and the moon dial are the same instrument read
// by two different lights, but they are NOT the same plate: the optimizer
// picks a different tilt for each and the hour lines run in different
// directions, so they can't be one scene with a swapped light source. This
// class builds both and shows one.
//
// Which one, and whether it ever changes, is the DialMode -- the three dial
// themes are exactly these three modes.

#include <memory>

#include "clock.h"
#include "dial_gl.h"
#include "dial_scene.h"
#include "types.h"

// What the dial is made to read. Uncorrected it tells apparent solar time; the
// corrected modes render the light at the hour angle a sun keeping the wanted
// time would have, which puts the shadow on that hour's line. Declination is
// untouched, so shadows keep their real seasonal length.
enum class DialTimebase {
  // Uncorrected. The instrument a real dial is.
  Apparent,
  // Apparent minus the equation of time: local mean solar time. Keyed to the
  // observer's longitude, so it still won't match a wall clock off the zone's
  // standard meridian, or under DST.
  Mean,
  // Civil time, taken from the system clock, so zone rules and DST come free.
  Clock,
};

enum class DialMode {
  // Pinned: the dial never changes, and goes dark when its own light is gone.
  SunOnly,
  MoonOnly,
  // Shows whichever dial can currently be read, crossfading at the changeover.
  Auto,
};

class DialClockFace : public ClockFace {
 public:
  DialClockFace(const Config& cfg, DialMode mode);
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
  // is read by, already carrying any timebase correction. Per-form because
  // the two have different plate frames and read different bodies.
  // `civil_hours` is the local wall-clock time of day, needed only by
  // DialTimebase::Clock.
  Vec3 light_for(const Form& form, double jd, double civil_hours) const;

  // The hour this form's shadow points at, in [0,24), before correction. The
  // hour lines were laid out against hour angle, so this inverts that.
  double dial_reading_hours(const Form& form, double jd, double lst_hours) const;

  // How far to shift the light's hour angle, in hours, for the dial to read
  // the configured timebase. Zero for Apparent.
  double timebase_correction_hours(const Form& form, double jd, double lst_hours, double civil_hours) const;

  // Whether the dial still tells the time -- see the .cpp; this is about the
  // gnomon's shadow landing on the plate, not merely about the body being up.
  bool is_readable(const Form& form, double jd, double civil_hours) const;

  // No-op unless the mode is Auto. Picks the opening dial on the first frame
  // (without a crossfade -- there is nothing to fade from at startup), and
  // thereafter changes over when the current dial stops being readable.
  void update_form_choice(double jd, double now_seconds, double civil_hours);

  DialMode mode_;
  DialTimebase timebase_;
  double latitude_deg_;
  double longitude_deg_;
  Form sun_form_;
  Form moon_form_;
  FixedCameraOffset camera_;
  DialGlRenderer gl_renderer_;
  bool showing_moondial_;
  bool chose_opening_form_ = false;
  bool fading_ = false;
  double fade_start_seconds_ = 0.0;
};

std::unique_ptr<ClockFace> make_dial_clock(const Config& cfg, DialMode mode);
