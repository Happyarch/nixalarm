#include "dial_clock.h"

#include <ctime>

#include "astro.h"

namespace {

// A fixed isometric-ish rig, expressed entirely in the plate's own local
// {u, v, n} frame: above-and-behind the dial center, looking back at the
// origin, up-hint = the plate's own normal (n) so the horizon in frame stays
// level relative to the PLATE, not the world -- see dial_geometry.h's
// FixedCameraOffset docs and dial_gl.h for why this makes the rendered
// framing invariant to the optimizer's chosen tilt.
FixedCameraOffset default_camera() {
  return FixedCameraOffset{Vec3{-1.15, -1.65, 1.35}, Vec3{0.0, 0.0, 0.05}, Vec3{0.0, 0.0, 1.0}};
}

Vec3 color_from_config(Color c) {
  return Vec3{c.r / 255.0, c.g / 255.0, c.b / 255.0};
}

}  // namespace

DialClockFace::DialClockFace(const Config& cfg, bool moondial, const DialPalette& palette)
    : moondial_(moondial),
      latitude_deg_(cfg.latitude),
      longitude_deg_(cfg.longitude),
      orientation_(optimize_dial_orientation(cfg.latitude, kDefaultGnomonLength, kDefaultPlateRadius,
                                              kDefaultLegibleRatio, moondial)),
      // Substyle-aligned to match build_dial_mesh's geometry frame -- the live
      // light projection and camera rig must live in the same frame the
      // meshes were built in, or lighting/framing would be rotated against
      // the geometry.
      frame_(substyle_aligned_plate_frame(cfg.latitude, orientation_.slant_deg, orientation_.declination_deg)),
      scene_(build_dial_scene(cfg.latitude, orientation_, kDefaultGnomonLength, kDefaultPlateRadius, moondial)),
      camera_(default_camera()),
      palette_(palette) {}

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

  EquatorialCoord eq = moondial_ ? lunar_position(jd) : solar_position_full(jd).eq;
  double lst_hours = local_sidereal_time_hours(jd, longitude_deg_);
  HorizontalCoord hc = equatorial_to_horizontal(eq, latitude_deg_, lst_hours);
  Vec3 light_world = horizontal_to_world(hc);
  Vec3 light_local = light_direction_local(frame_, light_world);

  gl_renderer_.render(win, ww, wh, scene_, camera_, light_local, palette_, color_from_config(cfg.background));
}

std::unique_ptr<ClockFace> make_dial_clock(const Config& cfg, bool moondial, const DialPalette& palette) {
  return std::make_unique<DialClockFace>(cfg, moondial, palette);
}
