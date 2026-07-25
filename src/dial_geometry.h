#pragma once

// General tilted-plate sundial/moondial geometry: the plate can be reclining
// (tilted from horizontal) and declining (facing any compass direction), not
// just the classic horizontal-plate special case. This generalizes the
// horizontal-only hour-line formula tan(dial_angle) = sin(lat)*tan(hour_angle)
// so it works at any latitude, including near the equator where a horizontal
// dial's gnomon degenerates to zero height.
//
// World frame convention throughout: right-handed, Z-up (Z=zenith, X=north,
// Y=west). West, not east: (north, east, up) is a LEFT-handed triple, and the
// renderer reads the plate-local image of this frame as ordinary right-handed
// space, so a left-handed basis renders the whole scene as its own mirror image
// -- the shadow and the hour marks stay mutually consistent, so the dial still
// tells the right time, but it sweeps backwards for its hemisphere.
//
// A graphics-convention basis swap (Z-up -> Y-up) happens only once, at the
// final camera/rendering stage -- never inside this module's math.
//
// All lengths (plate radius, gnomon/style length) are normalized dimensionless
// units: plate_radius = 1.0, gnomon_length = 0.5 by convention (see
// kDefaultPlateRadius/kDefaultGnomonLength). A single real-world scale factor
// is applied only when generating final mesh/render assets, never here.

#include "astro.h"

struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;
};

struct Vec2 {
  double x = 0.0, y = 0.0;
};

Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, double s);
double dot(Vec3 a, Vec3 b);
Vec3 cross(Vec3 a, Vec3 b);
double length(Vec3 v);
Vec3 normalize(Vec3 v);

constexpr double kDefaultPlateRadius = 1.0;
constexpr double kDefaultGnomonLength = 0.65;
constexpr double kDefaultLegibleRatio = 1.0;  // shadow may use the full plate radius

// The plate's local orthonormal frame, expressed in world coordinates, for a
// given (slant, declination). {u, v, n} is the image of the reference
// horizontal-plate frame {(1,0,0), (0,1,0), (0,0,1)} under the plate's
// rotation -- i.e. u is the plate's local "12 o'clock" axis, v is its local
// "3 o'clock" axis, n is its outward normal.
struct PlateFrame {
  Vec3 u, v, n;
};

// slant_deg: tilt away from horizontal (0 = flat/horizontal, 90 = vertical).
// declination_deg: compass facing, measured from due south, clockwise-positive
// (0 = south-facing reclining dial, the classical-gnomonics reference case;
// ignored/degenerate at slant=0, same as a horizontal plate has no facing).
PlateFrame compute_plate_frame(double slant_deg, double declination_deg);

// compute_plate_frame() with the frame then spun about its own normal so
// local +u points along the SUBSTYLE line (the style's projection onto the
// plate). The in-plane rotation of {u, v} is a free cosmetic parameter --
// nothing physical distinguishes one choice of "12 o'clock" axis -- but the
// rendered composition is not: with the raw frame the gnomon rises at
// whatever arbitrary-looking azimuth the optimizer's declination implies,
// as if mounted crooked. Substyle-aligning u makes the gnomon rise centered
// "up-screen" at every latitude, like a dial crafted for its orientation.
// Everything rendered together (gnomon mesh, hour ticks, camera rig, live
// light projection) must consistently use THIS frame or the raw one, never a
// mix. Falls back to the raw frame when the style is (near-)perpendicular to
// the plate, where the substyle direction is undefined.
PlateFrame substyle_aligned_plate_frame(double latitude_deg, double slant_deg, double declination_deg);

// The gnomon's style direction, fixed in world space regardless of plate tilt:
// it points at whichever celestial pole is ABOVE the horizon -- north at
// elevation = latitude in the northern hemisphere, due south at elevation
// = |latitude| in the southern, where the north pole is underground. Both are
// the same axis; only the ray differs. Non-negotiable astronomy, not a free
// parameter. A consequence worth knowing: because the plate then faces the
// southern pole down there, a southern dial's shadow sweeps COUNTER-clockwise,
// as real southern-hemisphere dials do.
Vec3 style_vector_world(double latitude_deg);

// Expresses a world-space vector in the plate's local {u, v, n} coordinates.
// Valid for any orthonormal PlateFrame (no explicit matrix inversion needed).
Vec3 project_to_local(Vec3 world_vec, const PlateFrame& frame);

// The idealized "construction sun" direction (world frame) used to build the
// FIXED groove geometry, at declination `declination_deg` and mean hour angle
// H (moondial: phase-shifted 180 deg, since a full/opposition moon transits
// the meridian at local midnight, behaving like an "anti-sun" 12h out of
// phase). This is NOT the live/true sun or moon position -- it reuses
// equatorial_to_horizontal (astro.h) purely as a coordinate-transform
// primitive (ra=0, lst=H/15h), not as a date-dependent ephemeris call, so the
// fixed-groove construction and the live shadow render share one transform.
//
// The default delta=0 is the equinox, the mean day and the reference case the
// closed-form hour-angle identities are stated against.
Vec3 idealized_light_direction_world(double latitude_deg, double hour_angle_deg, bool moondial,
                                      double declination_deg = 0.0);

// Earth's axial tilt: the extreme solar declination, reached at the solstices.
constexpr double kObliquityDeg = 23.44;

// The declination a dial's HOUR LINES are laid out for: the observer's own
// summer solstice, north or south. That is the longest day, so it is the full
// set of hours the light can ever be up for -- lay the lines out for any
// lesser declination and the dial is permanently missing its earliest and
// latest hours. Note this affects only WHICH hours get a line, never where a
// line points: the style is polar-aligned (style_vector_world), so the plane
// containing the style and the light cuts the plate along the same direction
// whatever the declination.
double construction_declination_deg(double latitude_deg);

// Converts a horizontal (az/alt) coordinate to a unit world-space direction
// vector (Z-up, X-north, Y-west). Exposed so callers can turn astro.h's live
// sun/moon position (via equatorial_to_horizontal) into the same world-frame
// representation this module's math already uses internally.
Vec3 horizontal_to_world(HorizontalCoord hc);

// Direction (unit 2D vector in the plate's own {u, v} frame) of the fixed
// hour-groove for hour angle H, found via shadow-plane/plate-plane
// intersection. Reduces EXACTLY to tan(dial_angle)=sin(lat)*tan(H) at
// slant=0 -- verified by a regression test, since that's the existing
// closed-form horizontal case.
Vec2 hour_line_direction(double latitude_deg, double slant_deg, double declination_deg,
                          double hour_angle_deg, bool moondial);

struct ShadowSample {
  Vec2 point_local;  // gnomon-tip shadow position, plate-local {u,v}, magnitude = shadow length
  bool valid = false;  // false if the light is behind the plate or the style tip casts no forward shadow
};

// The actual finite shadow cast by the gnomon TIP (not just the infinite
// hour-line ray direction) -- used by the legibility optimizer to measure
// shadow length against the plate radius.
ShadowSample shadow_point_on_plate(double latitude_deg, double slant_deg, double declination_deg,
                                    double gnomon_length, double hour_angle_deg, bool moondial);

// Same computation, but expressed in a caller-supplied frame (e.g. the
// substyle-aligned one) instead of re-deriving the raw frame from
// slant/declination -- so mesh generation can keep its shadow-derived tick
// directions in the same frame its gnomon geometry uses.
ShadowSample shadow_point_on_plate(double latitude_deg, const PlateFrame& frame, double gnomon_length,
                                    double hour_angle_deg, bool moondial, double declination_deg = 0.0);

struct DialOrientation {
  double slant_deg = 0.0;
  double declination_deg = 0.0;
  double contiguous_hours = 0.0;
};

// Runs once (startup-time, not per-frame): searches (slant, declination) to
// maximize the largest contiguous run of hour angle for which the shadow
// stays on the plate (<= plate_radius) and legible (shadow length <=
// k_legible * plate_radius). moondial=true sweeps hour angle centered on
// midnight with a moon-above-horizon test instead of sundial's noon/daylight.
DialOrientation optimize_dial_orientation(double latitude_deg, double gnomon_length,
                                           double plate_radius, double k_legible, bool moondial);

// A camera rig fixed relative to the plate's own local frame -- so the
// rendered framing is invariant to whatever (slant, declination) the
// optimizer picks; physical tilt affects only the light-direction math below,
// never the on-screen composition.
struct FixedCameraOffset {
  Vec3 position_in_plate_frame;
  Vec3 look_target_in_plate_frame{0.0, 0.0, 0.0};
  Vec3 up_hint_in_plate_frame{0.0, 0.0, 1.0};
};

struct CameraWorldPose {
  Vec3 position, look_target, up;
};

// Camera_world = R_plate * Camera_plate_local_fixed, applied uniformly so the
// camera moves rigidly with the plate.
CameraWorldPose camera_world_pose(const PlateFrame& frame, const FixedCameraOffset& offset);

// Projects a live world-space light direction (from astro.h's true
// instantaneous sun/moon position) into the plate's local frame, for
// per-frame shading/shadow computation. Same projection as
// project_to_local, named separately for call-site clarity.
Vec3 light_direction_local(const PlateFrame& frame, Vec3 light_dir_world);
