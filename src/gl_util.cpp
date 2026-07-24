#include "gl_util.h"

#include <epoxy/gl.h>

#include <cstdio>

namespace glutil {

namespace {

unsigned compile_shader(const char* label, GLenum type, const char* src) {
  unsigned shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  int ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::fprintf(stderr, "nixalarm: %s shader compile failed: %s\n", label, log);
  }
  return shader;
}

}  // namespace

unsigned link_program(const char* label, const char* vs_src, const char* fs_src) {
  unsigned vs = compile_shader(label, GL_VERTEX_SHADER, vs_src);
  unsigned fs = compile_shader(label, GL_FRAGMENT_SHADER, fs_src);
  unsigned prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  int ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
    std::fprintf(stderr, "nixalarm: %s shader link failed: %s\n", label, log);
    glDeleteProgram(prog);
    prog = 0;
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  return prog;
}

Mat4 mat4_identity() {
  Mat4 m{};
  m[0] = m[5] = m[10] = m[15] = 1.0f;
  return m;
}

Mat4 mat4_multiply(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[col * 4 + k];
      r[col * 4 + row] = sum;
    }
  }
  return r;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
  Vec3 f = normalize(target - eye);
  Vec3 s = normalize(cross(f, up));
  Vec3 u = cross(s, f);
  Mat4 m = mat4_identity();
  m[0] = static_cast<float>(s.x);
  m[4] = static_cast<float>(s.y);
  m[8] = static_cast<float>(s.z);
  m[1] = static_cast<float>(u.x);
  m[5] = static_cast<float>(u.y);
  m[9] = static_cast<float>(u.z);
  m[2] = static_cast<float>(-f.x);
  m[6] = static_cast<float>(-f.y);
  m[10] = static_cast<float>(-f.z);
  m[12] = static_cast<float>(-dot(s, eye));
  m[13] = static_cast<float>(-dot(u, eye));
  m[14] = static_cast<float>(dot(f, eye));
  return m;
}

Mat4 mat4_ortho(float half_width, float half_height, float near_z, float far_z) {
  Mat4 m{};
  m[0] = 1.0f / half_width;
  m[5] = 1.0f / half_height;
  m[10] = -2.0f / (far_z - near_z);
  m[14] = -(far_z + near_z) / (far_z - near_z);
  m[15] = 1.0f;
  return m;
}

unsigned upload_texture_rgb8(const std::vector<uint8_t>& pixels, int size) {
  return upload_texture_rgb8(pixels.data(), size, size);
}

namespace {

unsigned upload_texture(const uint8_t* pixels, int width, int height, GLint internal_format, GLenum format,
                        GLint wrap) {
  unsigned tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return tex;
}

}  // namespace

unsigned upload_texture_rgb8(const uint8_t* pixels, int width, int height) {
  return upload_texture(pixels, width, height, GL_RGB8, GL_RGB, GL_REPEAT);
}

unsigned upload_texture_rgba8(const uint8_t* pixels, int width, int height) {
  return upload_texture(pixels, width, height, GL_RGBA8, GL_RGBA, GL_REPEAT);
}

unsigned upload_texture_r8_clamped(const uint8_t* pixels, int width, int height) {
  return upload_texture(pixels, width, height, GL_R8, GL_RED, GL_CLAMP_TO_EDGE);
}

}  // namespace glutil
