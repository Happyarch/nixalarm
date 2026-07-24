#pragma once

// Small reusable OpenGL + matrix helpers, extracted from the dial renderer so
// future GL-based themes/faces don't re-implement them. Deliberately tiny:
// this project doesn't want a rendering framework, just the handful of
// primitives every GL face needs (shader building, a fixed-camera MVP, and
// procedural-texture upload).

#include <array>
#include <cstdint>
#include <vector>

#include "dial_geometry.h"  // Vec3

namespace glutil {

// Compiles and links a GLSL program; compile/link errors go to stderr with
// the given human-readable label. Returns the program id (0 on failure).
unsigned link_program(const char* label, const char* vs_src, const char* fs_src);

// Column-major 4x4, matching glUniformMatrix4fv(transpose=GL_FALSE).
using Mat4 = std::array<float, 16>;

Mat4 mat4_identity();
Mat4 mat4_multiply(const Mat4& a, const Mat4& b);
Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);
Mat4 mat4_ortho(float half_width, float half_height, float near_z, float far_z);

// Uploads an 8-bit RGB image as a repeating, mipmapped GL_TEXTURE_2D and
// returns the texture id. `pixels` is row-major RGB, width*height*3 bytes.
unsigned upload_texture_rgb8(const uint8_t* pixels, int width, int height);
unsigned upload_texture_rgb8(const std::vector<uint8_t>& pixels, int size);

// Same, for RGBA images (used for packed parameter maps: e.g. R=specular,
// G=ambient occlusion, B=height, A=metallic).
unsigned upload_texture_rgba8(const uint8_t* pixels, int width, int height);

}  // namespace glutil
