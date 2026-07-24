#include "moondial.h"

#include "dial_clock.h"

namespace {

// The moondial's night palette. Same staged bitmap maps as the sundial (the
// two faces share assets/runtime/dial/), so what makes this one read as NIGHT
// is the light grade below, not different textures: the surface tints here
// only show through when a map set is missing, and are the cool counterpart
// of the sundial's -- pale grey stone, tarnished silver gnomon, dew-dark
// grass. The ticks are a real albedo: cool pewter inlay.
constexpr Vec3 kGroundColor{0.16, 0.24, 0.18};
constexpr Vec3 kPlateColor{0.48, 0.50, 0.54};
constexpr Vec3 kGnomonColor{0.42, 0.45, 0.50};
constexpr Vec3 kTickColor{0.66, 0.70, 0.76};

// Moonlight: dim and blue. Deliberately sums to roughly a third of the
// sundial's exposure so a full-moon scene reads as night rather than as an
// overcast afternoon -- the moon is bright enough to cast the sharp gnomon
// shadow the face is FOR, and nothing more. The near-black sky fill is what
// sells it: unlit surfaces fall almost to silhouette.
constexpr Vec3 kMoonColor{0.26, 0.30, 0.40};
constexpr Vec3 kSkyFill{0.05, 0.06, 0.10};

}  // namespace

std::unique_ptr<ClockFace> make_moondial_clock(const Config& cfg) {
  return make_dial_clock(cfg, /*moondial=*/true,
                          DialPalette{kGroundColor, kPlateColor, kGnomonColor, kTickColor, kMoonColor, kSkyFill});
}
