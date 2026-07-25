#pragma once

// The OpenGL rendering backend for the sundial/moondial ClockFace faces --
// a REAL-TIME RAY TRACER, not a rasterizer: every frame draws one fullscreen
// triangle whose fragment shader casts an orthographic primary ray per pixel
// against the analytic scene (src/dial_scene.h), plus a hard shadow ray from
// each hit toward the live sun/moon. That gives true cast shadows (the solid
// gnomon rod's thick shadow vs. the glass frame's thin, pale ones) and
// in-shader alpha-blended glass transmission -- none of which the old
// rasterized-mesh pipeline could do. The scene is tiny (a few dozen analytic
// primitives), so per-pixel tracing is comfortably real-time on a GPU.
//
// Everything is traced directly in the plate's own LOCAL {u, v, n} frame (see
// dial_scene.h) -- there is no separate "world space" transform in this
// renderer. The camera (FixedCameraOffset, already expressed in that same
// local frame) and the dial geometry move together rigidly by construction,
// which is exactly the "camera pinned to the plate" requirement: the rendered
// framing never changes no matter what (slant, declination) the legibility
// optimizer picked. The only thing that ever crosses from world space into
// this local frame is the live sun/moon direction, already projected by
// dial_geometry.h's light_direction_local() before it reaches this class.

#include <SDL.h>

#include <vector>

#include "dial_geometry.h"
#include "dial_scene.h"

// Everything about a dial face's look that isn't geometry: the surface tints
// and the light grade. The tints only show through where a bitmap map set is
// missing (see the u*Mapped uniforms) EXCEPT tick_color, which is the hour
// ticks' actual albedo -- they're untextured polished metal inlay. The light
// grade is what separates the two faces visually: the sundial gets bright
// warm sunlight, the moondial dim cool moonlight, so a full-moon scene reads
// as night rather than as a slightly grey afternoon.
struct DialPalette {
  Vec3 ground_color;
  Vec3 plate_color;
  Vec3 gnomon_color;
  Vec3 tick_color;
  // Both carry intensity as well as hue. They should sum to roughly a unit
  // exposure for a fully-lit surface (light + ambient ~= 1) so a face doesn't
  // blow out; the moondial sums to far less, which is what makes it night.
  Vec3 light_color;    // direct sun/moon light
  Vec3 ambient_color;  // sky fill, attenuated by each surface's AO map
};

class DialGlRenderer {
 public:
  DialGlRenderer() = default;
  ~DialGlRenderer();
  DialGlRenderer(const DialGlRenderer&) = delete;
  DialGlRenderer& operator=(const DialGlRenderer&) = delete;

  // A frame is begin_frame / one or more draw() / end_frame. More than one
  // draw exists for the day/night changeover: the sun dial and the moon dial
  // are different plates, so the only way from one to the other is to draw
  // both and crossfade, which means a frame may carry two scenes.
  //
  // begin_frame lazily creates the GL context/shaders on the first call (the
  // window doesn't exist yet when the owning ClockFace is constructed, so this
  // can't happen in a constructor -- mirrors NixieClock's lazy asset loading).
  // It returns false if the context could not be created, in which case the
  // caller should skip the frame entirely.
  bool begin_frame(SDL_Window* win, int ww, int wh);

  // Traces one dial over the whole viewport. `opacity` is the alpha the frame
  // is blended at: draw the base dial at 1, then any dial fading in over it.
  //
  // light_dir_local: unit vector in the plate-local frame, pointing FROM the
  // surface TOWARD the sun/moon (light_direction_local()'s convention).
  // Colors are linear RGB in [0,1].
  void draw(const DialScene& scene, const FixedCameraOffset& camera, Vec3 light_dir_local,
            const DialPalette& palette, Vec3 background_color, float opacity);

  void end_frame(SDL_Window* win);

  // The shader's fixed uniform-array capacity for scene rods. Scenes larger
  // than this are truncated with a warning (never expected: gnomon + 2 glass
  // legs + at most 23 hour ticks).
  static constexpr int kMaxRods = 32;

 private:
  void ensure_initialized(SDL_Window* win);
  unsigned engraving_for(const DialScene& scene);

  bool initialized_ = false;
  SDL_GLContext gl_context_ = nullptr;
  unsigned program_ = 0;
  unsigned trace_vao_ = 0;  // empty VAO; the fullscreen triangle comes from gl_VertexID
  // Surface map sets sampled by the tracer, one per textured surface:
  // diffuse (basecolor), tangent-space normal, and a packed params map
  // (R=specular/inv-roughness, G=ambient occlusion, B=height for parallax,
  // A=metallic). Plate = Cobblestone Irregular Floor 001, ground = Grass 003,
  // gnomon rod = Metal 007 (all CC0, assets/runtime/dial/, credited in
  // assets/ATTRIBUTION.md); procedural/neutral fallbacks when not staged.
  unsigned texture_plate_diffuse_ = 0, texture_plate_normal_ = 0, texture_plate_params_ = 0;
  unsigned texture_ground_diffuse_ = 0, texture_ground_normal_ = 0, texture_ground_params_ = 0;
  unsigned texture_gnomon_diffuse_ = 0, texture_gnomon_normal_ = 0, texture_gnomon_params_ = 0;
  // The engraved Roman hour numerals (src/dial_engrave.h), one map per scene.
  // Generated rather than loaded: which numerals land where depends on the
  // orientation the optimizer chose for the configured latitude, so these
  // can't ship as bitmaps. Bound to unit 9, clamped, single channel. Kept as
  // a cache because a crossfade frame alternates between two scenes and
  // rasterizing a 1024-square map per draw is out of the question.
  struct EngravingSlot {
    const DialScene* scene = nullptr;
    unsigned texture = 0;
  };
  std::vector<EngravingSlot> engraving_slots_;
  int uniform_eye_ = -1, uniform_right_ = -1, uniform_up_ = -1, uniform_forward_ = -1;
  int uniform_half_extent_ = -1, uniform_pixel_ndc_ = -1;
  int uniform_light_dir_ = -1;
  int uniform_background_ = -1;
  int uniform_opacity_ = -1;
  int viewport_w_ = 0, viewport_h_ = 0;
};
