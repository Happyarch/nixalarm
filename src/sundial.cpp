#include "sundial.h"

#include "dial_clock.h"

namespace {

// The sundial's daylight palette. The three surface tints only show through
// where a bitmap map set is missing (assets/runtime/dial/), so they name the
// intended material rather than fight it: warm sandstone plate, weathered
// bronze gnomon, dry summer grass. The tick color is a real albedo -- warm
// brass inlay, which the shader shades as polished metal.
constexpr Vec3 kGroundColor{0.30, 0.42, 0.20};
constexpr Vec3 kPlateColor{0.62, 0.56, 0.46};
constexpr Vec3 kGnomonColor{0.55, 0.42, 0.22};
constexpr Vec3 kTickColor{0.83, 0.66, 0.30};

// Midday sun: bright and slightly warm, with a cooler blue sky fill. The two
// sum to about a unit exposure (see DialPalette).
constexpr Vec3 kSunColor{0.82, 0.79, 0.72};
constexpr Vec3 kSkyFill{0.20, 0.22, 0.27};

}  // namespace

std::unique_ptr<ClockFace> make_sundial_clock(const Config& cfg) {
  return make_dial_clock(cfg, /*moondial=*/false,
                          DialPalette{kGroundColor, kPlateColor, kGnomonColor, kTickColor, kSunColor, kSkyFill});
}
