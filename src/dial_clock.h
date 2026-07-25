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

// What the dial is made to READ. A dial left to itself tells apparent solar
// time: the sun's own time, which runs ahead of and behind the clock by up to
// a quarter of an hour over the year and is offset again by where you sit in
// your time zone. The corrected modes force the shadow onto the hour line for
// the time you actually want, by rendering the light at the hour angle a sun
// keeping that time would have. Declination is left alone, so shadows keep
// their real seasonal length -- it is the same trick a heliochronometer plays
// mechanically.
enum class DialTimebase {
  // Uncorrected. The instrument a real dial is.
  Apparent,
  // Apparent minus the equation of time: local mean solar time. Noon here is
  // the average of the sun's noons over the year, at YOUR longitude -- which
  // is still not your wall clock unless you happen to sit on your zone's
  // standard meridian and keep no daylight saving.
  Mean,
  // Local mean time carried the rest of the way to civil time. Taken from the
  // system clock rather than computed, so the zone's own rules and daylight
  // saving come along for free.
  Clock,
};

enum class DialMode {
  // Pinned. The dial never changes out from under you; it simply goes dark
  // when its own light is gone, the way the real instrument in a garden does.
  SunOnly,
  MoonOnly,
  // Follows the sky: whichever dial can currently be read is the one shown,
  // crossfading from one to the other at the changeover. A dial that goes
  // blank at dusk is a dead clock, and this is the mode that fixes that.
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
