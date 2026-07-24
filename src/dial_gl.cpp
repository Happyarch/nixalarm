#include "dial_gl.h"

#include <epoxy/gl.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gl_util.h"
#include "procedural_texture.h"
#include "stb_image.h"

#ifndef NIXALARM_DATADIR
#define NIXALARM_DATADIR "/usr/local/share/nixalarm"
#endif

namespace {

// Fullscreen triangle from gl_VertexID -- no vertex buffer at all. vNdc spans
// [-1,1] over the visible viewport (the triangle overshoots to (3,-1)/(-1,3)
// and is clipped).
const char* kTraceVertexShaderSrc = R"GLSL(
#version 330 core
out vec2 vNdc;
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vNdc = p * 2.0 - 1.0;
  gl_Position = vec4(vNdc, 0.0, 1.0);
}
)GLSL";

// The ray tracer. One orthographic primary ray per pixel (matching the old
// pipeline's fixed ortho camera exactly), analytic intersections against the
// DialScene primitives, one hard shadow ray per shaded hit, and iterative
// tinted transmission through glass members (in-shader alpha blending).
// Ground and plate sample their map sets by planar projection along the local
// normal axis, the gnomon rod by cylindrical projection around its own axis;
// only the hour ticks are an untextured constant material. Materials:
// 0 = solid gnomon rod, 1 = glass frame, 2 = hour tick; the plate slab and
// ground plane are separate implicit primitives.
const char* kTraceFragmentShaderSrc = R"GLSL(
#version 330 core
in vec2 vNdc;
out vec4 FragColor;

const int kMaxRods = 32;

uniform vec3 uEye;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uForward;
uniform vec2 uHalfExtent;
uniform vec3 uLightDir;     // unit, local frame, surface -> light
uniform vec3 uBackground;

uniform vec3 uGroundColor;
uniform vec3 uPlateColor;
uniform vec3 uGnomonColor;
uniform vec3 uTickColor;
// The light grade. Both are pre-scaled for intensity, and together they are
// the whole difference between the sundial's daylight and the moondial's
// night: bright warm sun + blue sky fill vs. dim cool moon + near-black fill.
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;
uniform float uPlateRadius;
uniform float uPlateThickness;
uniform float uGroundZ;
uniform int uRodCount;
uniform vec4 uRodA[kMaxRods];  // xyz = endpoint a, w = radius
uniform vec4 uRodB[kMaxRods];  // xyz = endpoint b, w = material

// Each textured surface (plate, ground, gnomon rod) has a 3-texture set:
// diffuse (pure basecolor), tangent-space normal, and a packed PARAMS map
// using the full source material -- R = specular strength (inverted
// roughness; also drives shininess), G = ambient occlusion (attenuates the
// ambient term only, as AO should), B = height (one-tap parallax UV offset),
// A = metallic (tints the highlight with the albedo and dims diffuse).
// u*Mapped selects textured albedo vs. the flat uniform tint; normal/params
// always sample (procedural or neutral fallbacks are uploaded when no bitmap
// is staged).
uniform sampler2D uPlateDiffuse;
uniform sampler2D uPlateNormal;
uniform sampler2D uPlateParams;
uniform sampler2D uGroundDiffuse;
uniform sampler2D uGroundNormal;
uniform sampler2D uGroundParams;
uniform sampler2D uGnomonDiffuse;
uniform sampler2D uGnomonNormal;
uniform sampler2D uGnomonParams;
uniform int uPlateMapped;
uniform int uGroundMapped;
uniform int uGnomonMapped;
uniform float uPlateUvScale;
uniform float uGroundUvScale;
uniform vec2 uGnomonUvScale;  // (around-circumference, along-axis) repeats

const float kInf = 1e9;
const float kEps = 3e-4;

// Capsule (sphere-capped segment a-b, radius r). Returns entry t or -1.
float capsuleIntersect(vec3 ro, vec3 rd, vec3 pa, vec3 pb, float r, out vec3 n) {
  vec3 ba = pb - pa;
  vec3 oa = ro - pa;
  float baba = dot(ba, ba);
  float bard = dot(ba, rd);
  float baoa = dot(ba, oa);
  float rdoa = dot(rd, oa);
  float oaoa = dot(oa, oa);
  float a = baba - bard * bard;
  float b = baba * rdoa - baoa * bard;
  float c = baba * oaoa - baoa * baoa - r * r * baba;
  if (a > 1e-9) {
    float h = b * b - a * c;
    if (h >= 0.0) {
      float t = (-b - sqrt(h)) / a;
      float y = baoa + t * bard;
      if (t > 0.0 && y > 0.0 && y < baba) {
        vec3 p = ro + rd * t;
        n = (p - (pa + ba * (y / baba))) / r;
        return t;
      }
    }
  }
  // End caps (also the fallback when the ray runs parallel to the axis).
  float best = kInf;
  vec3 bestN = vec3(0.0);
  for (int e = 0; e < 2; ++e) {
    vec3 cen = (e == 0) ? pa : pb;
    vec3 oc = ro - cen;
    float bb = dot(rd, oc);
    float cc = dot(oc, oc) - r * r;
    float hh = bb * bb - cc;
    if (hh >= 0.0) {
      float t = -bb - sqrt(hh);
      if (t > 0.0 && t < best) {
        vec3 p = ro + rd * t;
        // Only accept the spherical region outside the cylindrical span, so
        // interior cap surfaces can't shadow-acne the body.
        float y = dot(p - pa, ba);
        if (y <= 0.0 || y >= baba) {
          best = t;
          bestN = (p - cen) / r;
        }
      }
    }
  }
  if (best < kInf) {
    n = bestN;
    return best;
  }
  return -1.0;
}

// The plate: a solid slab-of-a-disc, axis local +Z, top face at z=0, bottom
// at z=-thickness, radius R. Returns entry t or -1.
float plateIntersect(vec3 ro, vec3 rd, out vec3 n) {
  float best = kInf;
  vec3 bestN = vec3(0.0);
  float R2 = uPlateRadius * uPlateRadius;
  if (abs(rd.z) > 1e-9) {
    float tTop = (0.0 - ro.z) / rd.z;
    if (tTop > 0.0 && tTop < best) {
      vec3 p = ro + rd * tTop;
      if (p.x * p.x + p.y * p.y <= R2) { best = tTop; bestN = vec3(0.0, 0.0, 1.0); }
    }
    float tBot = (-uPlateThickness - ro.z) / rd.z;
    if (tBot > 0.0 && tBot < best) {
      vec3 p = ro + rd * tBot;
      if (p.x * p.x + p.y * p.y <= R2) { best = tBot; bestN = vec3(0.0, 0.0, -1.0); }
    }
  }
  float a = rd.x * rd.x + rd.y * rd.y;
  if (a > 1e-12) {
    float b = ro.x * rd.x + ro.y * rd.y;
    float c = ro.x * ro.x + ro.y * ro.y - R2;
    float h = b * b - a * c;
    if (h >= 0.0) {
      float t = (-b - sqrt(h)) / a;
      if (t > 0.0 && t < best) {
        vec3 p = ro + rd * t;
        if (p.z <= 0.0 && p.z >= -uPlateThickness) { best = t; bestN = normalize(vec3(p.x, p.y, 0.0)); }
      }
    }
  }
  if (best < 1e8) { n = bestN; return best; }
  return -1.0;
}

// Nearest hit over the whole scene. hitKind: 0 ground, 1 plate, 2..4 rod
// materials offset by 2 (2=gnomon, 3=glass, 4=tick); -1 = miss.
float traceScene(vec3 ro, vec3 rd, out vec3 n, out int hitKind, out float hitRadius) {
  float best = kInf;
  hitKind = -1;
  hitRadius = 0.0;
  vec3 tn;
  // Rays that START below the ground plane (the ortho frame's lowest rows)
  // count as immediate ground hits, so the ground runs to the bottom edge of
  // the screen instead of leaving a background-colored strip beneath its
  // horizon line.
  if (ro.z <= uGroundZ) {
    n = vec3(0.0, 0.0, 1.0);
    hitKind = 0;
    return 0.0;
  }
  if (rd.z < -1e-9) {
    float t = (uGroundZ - ro.z) / rd.z;
    if (t > 0.0 && t < best) { best = t; n = vec3(0.0, 0.0, 1.0); hitKind = 0; }
  }
  float tp = plateIntersect(ro, rd, tn);
  if (tp > 0.0 && tp < best) { best = tp; n = tn; hitKind = 1; }
  for (int i = 0; i < kMaxRods; ++i) {
    if (i >= uRodCount) break;
    float t = capsuleIntersect(ro, rd, uRodA[i].xyz, uRodB[i].xyz, uRodA[i].w, tn);
    if (t > 0.0 && t < best) {
      best = t;
      n = tn;
      hitKind = 2 + int(uRodB[i].w + 0.5);
      hitRadius = uRodA[i].w;
    }
  }
  return (hitKind >= 0) ? best : -1.0;
}

// Shadow ray toward the light: solid geometry fully blocks; each glass member
// crossed only attenuates -- so the frame's cast shadows read pale and thin
// next to the solid rod's, which is the whole point of the glass frame.
float shadowVisibility(vec3 p) {
  float vis = 1.0;
  vec3 n;
  float tp = plateIntersect(p, uLightDir, n);
  if (tp > 0.0) return 0.0;
  for (int i = 0; i < kMaxRods; ++i) {
    if (i >= uRodCount) break;
    float t = capsuleIntersect(p, uLightDir, uRodA[i].xyz, uRodB[i].xyz, uRodA[i].w, n);
    if (t > 0.0) {
      int mat = int(uRodB[i].w + 0.5);
      if (mat == 1) vis *= 0.55;
      else return 0.0;
    }
  }
  return vis;
}

// Tangent-space normal mapping for an arbitrary geometric normal. For the
// z-facing surfaces that actually sample maps (ground, plate top) this
// reduces to the planar {x, y} tangent basis matching their planar UVs.
vec3 perturbNormal(sampler2D map, vec3 n, vec2 uv) {
  vec3 t = (abs(n.z) > 0.99) ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 0.0, 1.0), n));
  vec3 b = cross(n, t);
  vec3 nm = texture(map, uv).rgb * 2.0 - 1.0;
  return normalize(t * nm.x + b * nm.y + n * max(nm.z, 0.2));
}

// One-tap parallax from the params map's height channel: shift the UV along
// the view direction projected into the surface's tangent plane, so bumps
// visually lean away from the eye. Single-sample (not iterative POM) -- the
// ortho camera's fixed grazing angle makes one tap look right and it's free.
vec2 parallaxUv(sampler2D params, vec2 uv, vec3 n, vec3 rd, float amount) {
  float h = texture(params, uv).b - 0.5;
  vec3 v = -rd;
  vec3 t = (abs(n.z) > 0.99) ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 0.0, 1.0), n));
  vec3 b = cross(n, t);
  vec2 view_ts = vec2(dot(v, t), dot(v, b)) / max(dot(v, n), 0.25);
  return uv + view_ts * h * amount;
}

void main() {
  vec3 ro = uEye + uRight * (vNdc.x * uHalfExtent.x) + uUp * (vNdc.y * uHalfExtent.y);
  vec3 rd = uForward;

  // Direct light only exists while the sun/moon is above the plate's own
  // horizon; fade it in over the first few degrees to avoid a hard pop.
  float daylight = smoothstep(0.0, 0.06, uLightDir.z);

  vec3 color = vec3(0.0);
  vec3 throughput = vec3(1.0);
  for (int bounce = 0; bounce < 4; ++bounce) {
    vec3 n;
    int kind;
    float radius;
    float t = traceScene(ro, rd, n, kind, radius);
    if (t < 0.0) {
      color += throughput * uBackground;
      break;
    }
    vec3 p = ro + rd * t;
    if (kind == 3) {
      // Glass: a grazing-angle rim highlight, then tinted transmission and
      // straight continuation (members are thin; refraction offset would be
      // subpixel). This is the renderer's alpha blending, done in-shader
      // where the blend order is always correct.
      float rim = pow(1.0 - abs(dot(n, rd)), 3.0);
      color += throughput * rim * vec3(0.90, 0.96, 1.0) * (uAmbientColor * 1.6 + uLightColor * daylight * 0.7);
      throughput *= vec3(0.78, 0.86, 0.90) * 0.85;
      ro = p + rd * (2.0 * radius + 0.004);
      continue;
    }

    // Material: albedo + packed params (R=specular, G=AO, B=height,
    // A=metallic) for the three textured surfaces; constants for ticks.
    vec3 albedo;
    vec4 prm = vec4(0.35, 1.0, 0.5, 0.0);
    float shininessLo = 8.0, shininessHi = 96.0;
    if (kind == 0) {
      vec2 uv = parallaxUv(uGroundParams, p.xy * uGroundUvScale, n, rd, 0.04);
      prm = texture(uGroundParams, uv);
      prm.r *= 0.5;  // grass never reads glossy
      vec3 tex = texture(uGroundDiffuse, uv).rgb;
      albedo = (uGroundMapped == 1) ? tex : uGroundColor * tex;
      n = perturbNormal(uGroundNormal, n, uv);
      shininessHi = 32.0;
    } else if (kind == 1) {
      vec2 uv = parallaxUv(uPlateParams, p.xy * uPlateUvScale, n, rd, 0.04);
      prm = texture(uPlateParams, uv);
      vec3 tex = texture(uPlateDiffuse, uv).rgb;
      albedo = (uPlateMapped == 1) ? tex : uPlateColor * tex;
      n = perturbNormal(uPlateNormal, n, uv);
    } else if (kind == 2) {
      // The solid rod is rods[0] by dial_scene contract: cylindrical UVs
      // around its axis (u = angle, v = distance along), so the metal maps
      // wrap the rod like sheet stock rolled into a tube. No parallax on a
      // thin rod -- the silhouette dominates, the offset would just swim.
      vec3 a0 = uRodA[0].xyz;
      vec3 ax = normalize(uRodB[0].xyz - a0);
      float along = dot(p - a0, ax);
      vec3 radial = p - a0 - ax * along;
      vec3 e1 = normalize((abs(ax.z) > 0.9) ? cross(ax, vec3(1.0, 0.0, 0.0)) : cross(ax, vec3(0.0, 0.0, 1.0)));
      vec3 e2 = cross(ax, e1);
      float ang = atan(dot(radial, e2), dot(radial, e1));
      vec2 uv = vec2(ang * 0.15915494 + 0.5, along) * uGnomonUvScale;
      prm = texture(uGnomonParams, uv);
      albedo = (uGnomonMapped == 1) ? texture(uGnomonDiffuse, uv).rgb : uGnomonColor;
      // Tangent basis on the rod: x = circumferential (increasing angle),
      // y = the rod axis, z = the geometric normal.
      vec3 tangent = normalize(cross(ax, n));
      vec3 nm = texture(uGnomonNormal, uv).rgb * 2.0 - 1.0;
      n = normalize(tangent * nm.x + ax * nm.y + n * max(nm.z, 0.2));
      shininessLo = 16.0;
      shininessHi = 128.0;
    } else {
      // Hour ticks: polished metal inlay set into the plate -- no map set of
      // their own (they're a few pixels across, so texture detail would never
      // read), just a constant fully-metallic, low-roughness material so they
      // catch a bright specular streak against the matte cobblestone. Their
      // albedo is the one place uTickColor is the actual surface color rather
      // than a fallback tint.
      //
      // Metallic is held just under 1 on purpose: a fully-metallic thin rod
      // keeps only the narrow band where the highlight lands and goes near
      // black everywhere else, so the ticks vanish into the plate. Leaving a
      // little diffuse in makes them read as aged brass at every angle.
      albedo = uTickColor;
      prm = vec4(0.85, 1.0, 0.5, 0.8);
      shininessLo = 48.0;
      shininessHi = 180.0;
    }

    float diff = max(dot(n, uLightDir), 0.0);
    float vis = (diff > 0.0 && daylight > 0.0) ? shadowVisibility(p + n * kEps) : 0.0;
    float lit = diff * vis * daylight;
    // Roughness (via the specular channel) drives both highlight strength
    // and tightness; metallic tints the highlight with the surface color and
    // dims the diffuse term, AO darkens only the ambient term.
    float shininess = mix(shininessLo, shininessHi, prm.r);
    vec3 halfway = normalize(uLightDir - rd);
    float spec = (lit > 0.0) ? prm.r * pow(max(dot(n, halfway), 0.0), shininess) : 0.0;
    vec3 specTint = mix(vec3(1.0), albedo, prm.a);
    vec3 diffuseCol = albedo * (1.0 - 0.6 * prm.a);
    color += throughput * (diffuseCol * (uAmbientColor * prm.g + uLightColor * lit) +
                           specTint * spec * vis * daylight * uLightColor);
    break;
  }
  FragColor = vec4(color, 1.0);
}
)GLSL";

// The projection half-extent, sized so a unit-radius plate plus its gnomon
// comfortably fits regardless of window aspect (same value the old
// rasterizer used, so the framing is unchanged).
constexpr float kHalfExtent = 1.9f;

constexpr int kSurfaceMapSize = 256;
constexpr float kPlateUvScale = 0.85f;
constexpr float kGroundUvScale = 0.35f;

// Resolve the optional user-provided dial surface-map directory: env
// override, installed data dir, then dev locations -- the same candidate
// order the nixie face uses for its tube art. A directory counts if it
// contains any dial map file. Empty result = no bitmaps staged, use the
// procedural/neutral fallbacks.
std::string find_dial_map_dir() {
  std::vector<std::string> candidates;
  if (const char* e = std::getenv("NIXALARM_ASSET_DIR"); e && *e) candidates.emplace_back(e);
  candidates.emplace_back(std::string(NIXALARM_DATADIR) + "/dial");
  candidates.emplace_back("assets/runtime/dial");
  if (char* base = SDL_GetBasePath()) {
    candidates.emplace_back(std::string(base) + "../share/nixalarm/dial");
    candidates.emplace_back(std::string(base) + "assets/runtime/dial");
    SDL_free(base);
  }
  for (const auto& c : candidates) {
    for (const char* probe_file : {"diffuse.png", "gnomon_diffuse.png"}) {
      std::ifstream probe(c + "/" + probe_file);
      if (probe) return c;
    }
  }
  return {};
}

// Loads and uploads one map bitmap; returns 0 if the file is absent or fails
// to decode. stb_image is built with STBI_NO_STDIO project-wide (see
// stb_image_impl.cpp), so read the bytes ourselves and decode from memory,
// same as the nixie face's loader.
unsigned try_load_map(const std::string& dir, const char* file, int channels = 3) {
  if (dir.empty()) return 0;
  std::ifstream in(dir + "/" + file, std::ios::binary);
  if (!in) return 0;
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, channels);
  if (!px) {
    std::fprintf(stderr, "nixalarm: dial map %s/%s failed to decode; using fallback\n", dir.c_str(), file);
    return 0;
  }
  unsigned tex = channels == 4 ? glutil::upload_texture_rgba8(px, w, h) : glutil::upload_texture_rgb8(px, w, h);
  stbi_image_free(px);
  return tex;
}

// Packed params fallback (R=specular, G=AO, B=height, A=metallic) derived
// from a procedural SurfaceMaps set: specular from its specular map, full
// ambient visibility, mid height, dielectric.
unsigned upload_procedural_params(const SurfaceMaps& maps) {
  std::vector<uint8_t> rgba(static_cast<size_t>(maps.size) * maps.size * 4);
  for (size_t i = 0; i < rgba.size() / 4; ++i) {
    rgba[i * 4 + 0] = maps.specular[i * 3];
    rgba[i * 4 + 1] = 255;
    rgba[i * 4 + 2] = 128;
    rgba[i * 4 + 3] = 0;
  }
  return glutil::upload_texture_rgba8(rgba.data(), maps.size, maps.size);
}

// User bitmap if staged, else the given procedural fallback. Each map
// resolves independently, so a partial asset drop still works.
unsigned load_map_or_procedural(const std::string& dir, const char* file, const std::vector<uint8_t>& fallback,
                                 int fallback_size) {
  if (unsigned tex = try_load_map(dir, file)) return tex;
  return glutil::upload_texture_rgb8(fallback, fallback_size);
}

// User bitmap if staged, else a 1x1 constant-color texture -- for maps whose
// "off" state is a well-defined neutral value (flat normal, uniform specular)
// rather than procedural noise.
unsigned load_map_or_neutral(const std::string& dir, const char* file, uint8_t r, uint8_t g, uint8_t b) {
  if (unsigned tex = try_load_map(dir, file)) return tex;
  const uint8_t px[3] = {r, g, b};
  return glutil::upload_texture_rgb8(px, 1, 1);
}

}  // namespace

DialGlRenderer::~DialGlRenderer() {
  if (gl_context_) SDL_GL_DeleteContext(gl_context_);
}

void DialGlRenderer::ensure_initialized(SDL_Window* win, const DialScene& scene, const DialPalette& palette) {
  if (initialized_) return;
  gl_context_ = SDL_GL_CreateContext(win);
  if (!gl_context_) {
    std::fprintf(stderr, "nixalarm: dial GL context creation failed: %s\n", SDL_GetError());
    return;
  }
  SDL_GL_MakeCurrent(win, gl_context_);
  SDL_GL_SetSwapInterval(1);

  program_ = glutil::link_program("dial tracer", kTraceVertexShaderSrc, kTraceFragmentShaderSrc);
  uniform_eye_ = glGetUniformLocation(program_, "uEye");
  uniform_right_ = glGetUniformLocation(program_, "uRight");
  uniform_up_ = glGetUniformLocation(program_, "uUp");
  uniform_forward_ = glGetUniformLocation(program_, "uForward");
  uniform_half_extent_ = glGetUniformLocation(program_, "uHalfExtent");
  uniform_light_dir_ = glGetUniformLocation(program_, "uLightDir");
  uniform_background_ = glGetUniformLocation(program_, "uBackground");
  glGenVertexArrays(1, &trace_vao_);

  // Scene + material uniforms are static after startup: upload once here.
  glUseProgram(program_);
  auto set3 = [&](const char* name, Vec3 v) {
    glUniform3f(glGetUniformLocation(program_, name), static_cast<float>(v.x), static_cast<float>(v.y),
                static_cast<float>(v.z));
  };
  set3("uGroundColor", palette.ground_color);
  set3("uPlateColor", palette.plate_color);
  set3("uGnomonColor", palette.gnomon_color);
  set3("uTickColor", palette.tick_color);
  set3("uLightColor", palette.light_color);
  set3("uAmbientColor", palette.ambient_color);
  glUniform1f(glGetUniformLocation(program_, "uPlateRadius"), static_cast<float>(scene.plate_radius));
  glUniform1f(glGetUniformLocation(program_, "uPlateThickness"), static_cast<float>(scene.plate_thickness));
  glUniform1f(glGetUniformLocation(program_, "uGroundZ"), static_cast<float>(scene.ground_z));

  int count = static_cast<int>(scene.rods.size());
  if (count > kMaxRods) {
    std::fprintf(stderr, "nixalarm: dial scene has %d rods; truncating to %d\n", count, kMaxRods);
    count = kMaxRods;
  }
  std::array<float, kMaxRods * 4> rod_a{}, rod_b{};
  for (int i = 0; i < count; ++i) {
    const SceneRod& r = scene.rods[static_cast<size_t>(i)];
    rod_a[i * 4 + 0] = static_cast<float>(r.a.x);
    rod_a[i * 4 + 1] = static_cast<float>(r.a.y);
    rod_a[i * 4 + 2] = static_cast<float>(r.a.z);
    rod_a[i * 4 + 3] = static_cast<float>(r.radius);
    rod_b[i * 4 + 0] = static_cast<float>(r.b.x);
    rod_b[i * 4 + 1] = static_cast<float>(r.b.y);
    rod_b[i * 4 + 2] = static_cast<float>(r.b.z);
    rod_b[i * 4 + 3] = static_cast<float>(static_cast<int>(r.material));
  }
  glUniform1i(glGetUniformLocation(program_, "uRodCount"), count);
  glUniform4fv(glGetUniformLocation(program_, "uRodA"), kMaxRods, rod_a.data());
  glUniform4fv(glGetUniformLocation(program_, "uRodB"), kMaxRods, rod_b.data());

  // Surface maps: user-provided bitmaps from assets/runtime/dial/ (or the
  // installed data dir / NIXALARM_ASSET_DIR) when staged, procedural
  // speckled-stone placeholders (procedural_texture.h) otherwise. Plate
  // (Cobblestone Irregular Floor 001) and ground (Grass 003) each have their
  // own set; a "mapped" flag per surface switches the shader from
  // tint-times-texture (procedural) to the bitmap's own colors.
  std::string map_dir = find_dial_map_dir();
  SurfaceMaps stone = generate_speckled_stone(kSurfaceMapSize, SpeckledStoneParams{});
  texture_plate_diffuse_ = try_load_map(map_dir, "plate_diffuse.png");
  bool plate_mapped = texture_plate_diffuse_ != 0;
  if (!plate_mapped) texture_plate_diffuse_ = glutil::upload_texture_rgb8(stone.diffuse, stone.size);
  texture_plate_normal_ = load_map_or_procedural(map_dir, "plate_normal.png", stone.normal, stone.size);
  texture_plate_params_ = try_load_map(map_dir, "plate_params.png", 4);
  if (!texture_plate_params_) texture_plate_params_ = upload_procedural_params(stone);
  texture_ground_diffuse_ = try_load_map(map_dir, "ground_diffuse.png");
  bool ground_mapped = texture_ground_diffuse_ != 0;
  if (!ground_mapped) texture_ground_diffuse_ = glutil::upload_texture_rgb8(stone.diffuse, stone.size);
  texture_ground_normal_ = load_map_or_procedural(map_dir, "ground_normal.png", stone.normal, stone.size);
  texture_ground_params_ = try_load_map(map_dir, "ground_params.png", 4);
  if (!texture_ground_params_) texture_ground_params_ = upload_procedural_params(stone);
  glUniform1i(glGetUniformLocation(program_, "uPlateDiffuse"), 0);
  glUniform1i(glGetUniformLocation(program_, "uPlateNormal"), 1);
  glUniform1i(glGetUniformLocation(program_, "uPlateParams"), 2);
  glUniform1i(glGetUniformLocation(program_, "uGroundDiffuse"), 6);
  glUniform1i(glGetUniformLocation(program_, "uGroundNormal"), 7);
  glUniform1i(glGetUniformLocation(program_, "uGroundParams"), 8);
  glUniform1i(glGetUniformLocation(program_, "uPlateMapped"), plate_mapped ? 1 : 0);
  glUniform1i(glGetUniformLocation(program_, "uGroundMapped"), ground_mapped ? 1 : 0);
  glUniform1f(glGetUniformLocation(program_, "uPlateUvScale"), kPlateUvScale);
  glUniform1f(glGetUniformLocation(program_, "uGroundUvScale"), kGroundUvScale);

  // Gnomon rod maps (Metal 007, CC0 -- see assets/ATTRIBUTION.md). Albedo
  // only counts as "mapped" when the diffuse is actually staged; normal and
  // params fall back to neutral constants so the shader samples them
  // unconditionally.
  texture_gnomon_diffuse_ = try_load_map(map_dir, "gnomon_diffuse.png");
  bool gnomon_mapped = texture_gnomon_diffuse_ != 0;
  if (!gnomon_mapped) {
    const uint8_t white[3] = {255, 255, 255};
    texture_gnomon_diffuse_ = glutil::upload_texture_rgb8(white, 1, 1);
  }
  texture_gnomon_normal_ = load_map_or_neutral(map_dir, "gnomon_normal.png", 128, 128, 255);
  texture_gnomon_params_ = try_load_map(map_dir, "gnomon_params.png", 4);
  if (!texture_gnomon_params_) {
    const uint8_t neutral[4] = {217, 255, 128, 230};  // glossy near-metal fallback rod
    texture_gnomon_params_ = glutil::upload_texture_rgba8(neutral, 1, 1);
  }
  glUniform1i(glGetUniformLocation(program_, "uGnomonDiffuse"), 3);
  glUniform1i(glGetUniformLocation(program_, "uGnomonNormal"), 4);
  glUniform1i(glGetUniformLocation(program_, "uGnomonParams"), 5);
  glUniform1i(glGetUniformLocation(program_, "uGnomonMapped"), gnomon_mapped ? 1 : 0);
  // The rod is thin: its circumference is ~1/5 of its length, so repeat the
  // square texture ~5x along the axis to keep texels roughly isotropic.
  glUniform2f(glGetUniformLocation(program_, "uGnomonUvScale"), 1.0f, 5.0f);

  // The tracer composites glass in-shader, but blending stays enabled for
  // any rasterized overlay a future pass might draw on top of the traced
  // frame.
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  initialized_ = true;
}

void DialGlRenderer::render(SDL_Window* win, int ww, int wh, const DialScene& scene,
                             const FixedCameraOffset& camera, Vec3 light_dir_local, const DialPalette& palette,
                             Vec3 background_color) {
  ensure_initialized(win, scene, palette);
  if (!gl_context_) return;
  SDL_GL_MakeCurrent(win, gl_context_);

  glViewport(0, 0, ww, wh);
  glDisable(GL_DEPTH_TEST);  // the tracer resolves visibility itself

  // Orthographic camera basis, identical to the old mat4_look_at-derived one.
  Vec3 eye = camera.position_in_plate_frame;
  Vec3 forward = normalize(camera.look_target_in_plate_frame - eye);
  Vec3 right = normalize(cross(forward, camera.up_hint_in_plate_frame));
  Vec3 up = cross(right, forward);
  float aspect = wh > 0 ? static_cast<float>(ww) / static_cast<float>(wh) : 1.0f;
  float half_w = aspect >= 1.0f ? kHalfExtent * aspect : kHalfExtent;
  float half_h = aspect >= 1.0f ? kHalfExtent : kHalfExtent / aspect;

  glUseProgram(program_);
  auto set3 = [](int loc, Vec3 v) {
    glUniform3f(loc, static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
  };
  set3(uniform_eye_, eye);
  set3(uniform_right_, right);
  set3(uniform_up_, up);
  set3(uniform_forward_, forward);
  glUniform2f(uniform_half_extent_, half_w, half_h);
  set3(uniform_light_dir_, normalize(light_dir_local));
  set3(uniform_background_, background_color);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture_plate_diffuse_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, texture_plate_normal_);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, texture_plate_params_);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, texture_gnomon_diffuse_);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, texture_gnomon_normal_);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D, texture_gnomon_params_);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D, texture_ground_diffuse_);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, texture_ground_normal_);
  glActiveTexture(GL_TEXTURE8);
  glBindTexture(GL_TEXTURE_2D, texture_ground_params_);

  glBindVertexArray(trace_vao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  SDL_GL_SwapWindow(win);
}
