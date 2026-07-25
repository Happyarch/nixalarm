#include "dial_clock.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>

#include "astro.h"
#include "dial_time.h"

namespace {

// A fixed isometric-ish rig, expressed entirely in the plate's own local
// {u, v, n} frame: above-and-behind the dial center, looking back at the
// origin, up-hint = the plate's own normal (n) so the horizon in frame stays
// level relative to the PLATE, not the world -- see dial_geometry.h's
// FixedCameraOffset docs and dial_gl.h for why this makes the rendered
// framing invariant to the optimizer's chosen tilt. Shared by both forms, so
// the changeover moves the dial under the camera, not the camera.
FixedCameraOffset default_camera() {
  return FixedCameraOffset{Vec3{-1.15, -1.65, 1.35}, Vec3{0.0, 0.0, 0.05}, Vec3{0.0, 0.0, 1.0}};
}

// The sun dial's daylight palette. The three surface tints only show through
// where a bitmap map set is missing (assets/runtime/dial/), so they name the
// intended material rather than fight it: warm sandstone plate, weathered
// bronze gnomon, dry summer grass. The tick color is a real albedo -- warm
// brass inlay, which the shader shades as polished metal. Midday sun is
// bright and slightly warm, with a cooler blue sky fill; the two sum to about
// a unit exposure (see DialPalette).
constexpr DialPalette kSunPalette{
    Vec3{0.30, 0.42, 0.20},  // ground
    Vec3{0.62, 0.56, 0.46},  // plate
    Vec3{0.55, 0.42, 0.22},  // gnomon
    Vec3{0.83, 0.66, 0.30},  // ticks
    Vec3{0.82, 0.79, 0.72},  // sunlight
    Vec3{0.20, 0.22, 0.27},  // sky fill
};

// The moon dial's night palette. Same staged bitmap maps as the sun dial (the
// two forms share assets/runtime/dial/), so what makes this one read as NIGHT
// is the light grade, not different textures: the surface tints are the cool
// counterpart of the sun dial's -- pale grey stone, tarnished silver gnomon,
// dew-dark grass -- and the ticks are cool pewter. Moonlight is deliberately
// about a third of the sun dial's exposure, bright enough to cast the sharp
// gnomon shadow the face is FOR and nothing more. The near-black sky fill is
// what sells it: unlit surfaces fall almost to silhouette.
constexpr DialPalette kMoonPalette{
    Vec3{0.16, 0.24, 0.18},  // ground
    Vec3{0.48, 0.50, 0.54},  // plate
    Vec3{0.42, 0.45, 0.50},  // gnomon
    Vec3{0.66, 0.70, 0.76},  // ticks
    Vec3{0.26, 0.30, 0.40},  // moonlight
    Vec3{0.05, 0.06, 0.10},  // sky fill
};

// Long enough to read as a dissolve rather than a glitch, short enough not to
// sit through.
constexpr double kCrossfadeSeconds = 2.5;

// Below roughly a quarter moon the terminator's own shadow swamps the
// gnomon's, so the moon dial is unusable however high the moon has risen.
constexpr double kMinReadableMoonPhase = 0.22;

Vec3 color_from_config(Color c) {
  return Vec3{c.r / 255.0, c.g / 255.0, c.b / 255.0};
}

DialTimebase parse_timebase(const std::string& s) {
  if (s == "mean") return DialTimebase::Mean;
  if (s == "clock") return DialTimebase::Clock;
  return DialTimebase::Apparent;  // config.cpp has already rejected anything else
}

}  // namespace

DialClockFace::Form DialClockFace::make_form(const Config& cfg, bool moondial, const DialPalette& palette) {
  Form form;
  form.moondial = moondial;
  form.orientation = optimize_dial_orientation(cfg.latitude, kDefaultGnomonLength, kDefaultPlateRadius,
                                                kDefaultLegibleRatio, moondial);
  // Substyle-aligned to match build_dial_scene's geometry frame -- the live
  // light projection and camera rig must live in the same frame the scene was
  // built in, or lighting/framing would be rotated against the geometry.
  form.frame = substyle_aligned_plate_frame(cfg.latitude, form.orientation.slant_deg,
                                             form.orientation.declination_deg);
  form.scene = build_dial_scene(cfg.latitude, form.orientation, kDefaultGnomonLength, kDefaultPlateRadius, moondial);
  form.palette = palette;
  return form;
}

DialClockFace::DialClockFace(const Config& cfg, DialMode mode)
    : mode_(mode),
      timebase_(parse_timebase(cfg.dial_time)),
      latitude_deg_(cfg.latitude),
      longitude_deg_(cfg.longitude),
      sun_form_(make_form(cfg, /*moondial=*/false, kSunPalette)),
      moon_form_(make_form(cfg, /*moondial=*/true, kMoonPalette)),
      camera_(default_camera()),
      showing_moondial_(mode == DialMode::MoonOnly) {}

double DialClockFace::dial_reading_hours(const Form& form, double jd, double lst_hours) const {
  EquatorialCoord eq = form.moondial ? lunar_position(jd) : solar_position_full(jd).eq;
  return ::dial_reading_hours(form.moondial, eq.ra_deg, lst_hours);
}

// The correction is expressed as a shift in the light's HOUR ANGLE, which is
// the one quantity the hour lines are a function of. Shifting it moves the
// shadow along the plate exactly as far as the correction is worth, and
// leaves declination -- and so the shadow's seasonal length -- alone.
//
// Note this moves the rendered sun: in a corrected mode the light also rises
// and sets at corrected times.
double DialClockFace::timebase_correction_hours(const Form& form, double jd, double lst_hours,
                                                 double civil_hours) const {
  if (timebase_ == DialTimebase::Apparent) return 0.0;
  // Mean time is derived from the SUN even for the moon dial, so the two
  // dials in auto_dial agree with each other and with a mean-time sundial --
  // which also corrects the moon's own ~50 min/day retardation for free.
  double target = (timebase_ == DialTimebase::Clock) ? civil_hours : local_mean_solar_time_hours(jd, lst_hours);
  return timebase_shift_hours(target, dial_reading_hours(form, jd, lst_hours));
}

Vec3 DialClockFace::light_for(const Form& form, double jd, double civil_hours) const {
  EquatorialCoord eq = form.moondial ? lunar_position(jd) : solar_position_full(jd).eq;
  double lst_hours = local_sidereal_time_hours(jd, longitude_deg_);
  // Hour angle = lst*15 - ra, so advancing the sidereal time we hand to the
  // transform advances the hour angle by the same amount -- that IS the
  // correction, applied without touching declination.
  lst_hours += timebase_correction_hours(form, jd, lst_hours, civil_hours);
  HorizontalCoord hc = equatorial_to_horizontal(eq, latitude_deg_, lst_hours);
  return light_direction_local(form.frame, horizontal_to_world(hc));
}

// Whether the dial can be read now -- stricter than "the body has risen". The
// tip's shadow must land on the plate; below some elevation it slides off the
// edge, well before the body reaches the horizon.
bool DialClockFace::is_readable(const Form& form, double jd, double civil_hours) const {
  // Too thin a crescent casts no readable shadow at any elevation, so the
  // geometry test below never gets a say.
  if (form.moondial && moon_illuminated_fraction(jd) < kMinReadableMoonPhase) return false;
  return shadow_falls_on_plate(form.scene, light_for(form, jd, civil_hours));
}

// Hold the dial we are on for as long as it still tells the time, and change
// over only when it has stopped and the other one works. When NEITHER works
// -- the small hours under a new moon -- stay put and go dark rather than
// swapping to an equally unreadable dial. Staying put is also what keeps this
// from flapping around the moment either condition is marginal.
void DialClockFace::update_form_choice(double jd, double now_seconds, double civil_hours) {
  if (mode_ != DialMode::Auto) return;  // pinned: the dial never changes

  // Opening frame: there is no dial to fade FROM, so pick the right one
  // outright. Prefer whichever can be read; failing that (launched into a
  // moonless night) open on the dial whose body is at least on the correct
  // side of the horizon, so the dark plate on screen is the plausible one.
  if (!chose_opening_form_) {
    chose_opening_form_ = true;
    if (is_readable(sun_form_, jd, civil_hours)) {
      showing_moondial_ = false;
    } else if (is_readable(moon_form_, jd, civil_hours)) {
      showing_moondial_ = true;
    } else {
      showing_moondial_ = normalize(light_for(sun_form_, jd, civil_hours)).z <= 0.0;
    }
    return;
  }

  const Form& current = showing_moondial_ ? moon_form_ : sun_form_;
  if (is_readable(current, jd, civil_hours)) return;

  const Form& other = showing_moondial_ ? sun_form_ : moon_form_;
  if (!is_readable(other, jd, civil_hours)) return;

  showing_moondial_ = !showing_moondial_;
  fading_ = true;
  fade_start_seconds_ = now_seconds;
}

void DialClockFace::render(SDL_Window* win, SDL_Renderer* /*r*/, int ww, int wh, const Config& cfg,
                            const RingState& /*ring*/) {
  // The dial faces deliberately draw NO alarm-ringing indicator: the flash /
  // progress overlay the other faces render (see SevenSegmentClock/NixieClock/
  // AnalogClock) would have to be either extra GL geometry or a 2D
  // SDL_Renderer pass composited after SDL_GL_SwapWindow, and a flat rectangle
  // slapped over a raytraced scene looks wrong. A dial-native indicator is
  // planned instead; until then a ringing alarm is signalled by sound alone,
  // exactly as on any other face. `ring` is unused for that reason.
  std::time_t now = std::time(nullptr);
  std::tm utc_tm{};
  gmtime_r(&now, &utc_tm);
  double jd = julian_day(utc_tm);
  double now_seconds = static_cast<double>(SDL_GetTicks64()) / 1000.0;

  // Wall-clock time of day, straight from the system's own zone rules, so
  // DialTimebase::Clock inherits this zone's offset and daylight saving
  // without this code knowing anything about either.
  std::tm local_tm{};
  localtime_r(&now, &local_tm);
  double civil_hours = local_tm.tm_hour + local_tm.tm_min / 60.0 + local_tm.tm_sec / 3600.0;

  update_form_choice(jd, now_seconds, civil_hours);

  const Form& incoming = showing_moondial_ ? moon_form_ : sun_form_;
  const Form& outgoing = showing_moondial_ ? sun_form_ : moon_form_;

  float fade = 1.0f;
  if (fading_) {
    double elapsed = now_seconds - fade_start_seconds_;
    if (elapsed >= kCrossfadeSeconds) {
      fading_ = false;
    } else {
      // Smoothstep, so the dissolve eases in and out instead of starting and
      // stopping abruptly.
      double t = std::clamp(elapsed / kCrossfadeSeconds, 0.0, 1.0);
      fade = static_cast<float>(t * t * (3.0 - 2.0 * t));
    }
  }

  if (!gl_renderer_.begin_frame(win, ww, wh)) return;
  Vec3 background = color_from_config(cfg.background);
  // The outgoing dial underneath at full opacity, the incoming one over it at
  // a rising alpha. Two different plates can't be interpolated geometrically,
  // so this dissolve is the transition.
  if (fading_) {
    gl_renderer_.draw(outgoing.scene, camera_, light_for(outgoing, jd, civil_hours), outgoing.palette, background, 1.0f);
  }
  gl_renderer_.draw(incoming.scene, camera_, light_for(incoming, jd, civil_hours), incoming.palette, background, fade);
  gl_renderer_.end_frame(win);
}

std::unique_ptr<ClockFace> make_dial_clock(const Config& cfg, DialMode mode) {
  return std::make_unique<DialClockFace>(cfg, mode);
}
