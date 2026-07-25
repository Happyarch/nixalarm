#pragma once

// Analytic scene description for the raytraced sundial/moondial faces --
// successor to the retired triangle-mesh pipeline (src/dial_mesh.*). The
// renderer (src/dial_gl.cpp) traces rays against these primitives directly in
// a fragment shader, so geometry here is exact math (capsules, a disc slab, a
// plane), not tessellated approximations. Backend-agnostic and unit-testable
// without a window/context, same as dial_mesh was.
//
// All positions are in the plate's SUBSTYLE-ALIGNED local {u, v, n} frame
// (see substyle_aligned_plate_frame() in dial_geometry.h): +u is the gnomon's
// horizontal direction, n the plate's outward normal. The camera rig
// (FixedCameraOffset) lives in this same frame, so the renderer never leaves
// it; only the live sun/moon direction crosses over from world space, already
// projected by light_direction_local().
//
// Gnomon design (user-directed): the shadow-caster is a SOLID thin rod along
// the style edge, from the dial center straight to the style tip (the tip is
// the nodus -- all tick math keys off it, unchanged). It is held by a GLASS
// wireframe of the rest of the classic right triangle: one thin leg lying in
// the plate from the center to the point beneath the tip, one thin leg rising
// from there, parallel to the plate normal, up to the tip. The full triangle
// silhouette still explains the tilt structurally, but only its skeleton
// edges are drawn -- and in glass, so the frame's cast shadows read obviously
// thinner/fainter than the solid rod's.

#include <vector>

#include "dial_geometry.h"

enum class DialMaterial {
  Gnomon = 0,  // solid, opaque, colored by the face's gnomon color
  Glass = 1,   // transmissive support frame
  Tick = 2,    // opaque hour markers lying on the plate
};

// A capsule: the volume within `radius` of segment a-b. Chosen over true
// cylinders because the ray intersection is simpler and the sphere-capped
// ends read naturally as finished rod tips.
struct SceneRod {
  Vec3 a, b;
  double radius = 0.0;
  DialMaterial material = DialMaterial::Gnomon;
};

// One hour's label: which numeral to cut, and where. One per readable hour, in
// hour order. The boundary rods are separate and there is one more of them --
// they fall at the half hours, fencing the band each numeral sits in.
//
// Geometry, not rendering: dial_engrave cuts these into the plate as a height
// field the tracer reads as bump.
struct HourMark {
  Vec2 direction;   // unit, on the plate, from the dial center toward the mark
  double radius;    // distance from the center to the numeral's center
  double height;    // cap height of the numeral, in plate units
  // Perpendicular distance to the nearer of the two boundaries fencing this
  // band, less the boundary's own width. Bands are asymmetric about their hour
  // and narrow sharply near midday, so sizing a numeral from anything else
  // (neighbouring hour spacing, say) strikes it through. The engraver scales
  // the lettering to fit this.
  double clearance = 0.0;
  int hour = 12;    // 1..12; the numeral to cut
};

struct DialScene {
  double plate_radius = 0.0;
  double plate_thickness = 0.0;  // plate occupies local z in [-thickness, 0]
  double ground_z = 0.0;         // local z of the infinite ground plane (< -thickness)
  std::vector<SceneRod> rods;
  std::vector<HourMark> hour_marks;
  Vec3 gnomon_tip_local;  // the nodus; end of the solid rod
};

// Builds the full analytic scene for one dial at its already-chosen
// orientation (from optimize_dial_orientation(); not run here so the startup
// cost is paid once by the caller). Rod order in the result is: solid gnomon
// rod first, then any glass frame legs, then hour tick rods.
DialScene build_dial_scene(double latitude_deg, const DialOrientation& orientation, double gnomon_length,
                            double plate_radius, bool moondial);

// Whether the dial can be read under this light -- stricter than "the body has
// risen". As the light drops toward the plate's horizon the tip's shadow
// lengthens and slides off the edge, well before the body sets. That crossing
// is when the dial stops working.
//
// light_local: in the same plate frame as the scene, pointing FROM the
// surface TOWARD the body (light_direction_local()'s convention); need not be
// normalized.
bool shadow_falls_on_plate(const DialScene& scene, Vec3 light_local);
