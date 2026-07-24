#include "analog.h"

#include <SDL.h>
#include <librsvg/rsvg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef NIXALARM_DATADIR
#define NIXALARM_DATADIR "/usr/local/share/nixalarm"
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSvgCenter = 256.0;
constexpr double kSvgSize = 512.0;

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct DrawFrame {
  float x = 0.0f;
  float y = 0.0f;
  float size = 0.0f;
};

float clampf(float v, float lo, float hi) {
  return std::max(lo, std::min(v, hi));
}

void set_color(SDL_Renderer* r, Color c, Uint8 alpha = 255) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, alpha);
}

void fill_circle(SDL_Renderer* r, float cx, float cy, float radius, Color c, Uint8 alpha = 255) {
  set_color(r, c, alpha);
  int rr = static_cast<int>(std::ceil(radius));
  for (int y = -rr; y <= rr; ++y) {
    float half = std::sqrt(std::max(0.0f, radius * radius - static_cast<float>(y * y)));
    SDL_RenderDrawLineF(r, cx - half, cy + y, cx + half, cy + y);
  }
}

Point rotate_svg_point(Point p, double degrees, Point center) {
  double a = degrees * kPi / 180.0;
  double x = p.x - center.x;
  double y = p.y - center.y;
  return Point{x * std::cos(a) - y * std::sin(a) + center.x,
               x * std::sin(a) + y * std::cos(a) + center.y};
}

Point to_screen(Point p, const DrawFrame& frame) {
  double scale = frame.size / kSvgSize;
  return Point{frame.x + p.x * scale, frame.y + p.y * scale};
}

void fill_polygon(SDL_Renderer* r, const std::vector<Point>& svg_points, double degrees, Point center,
                  const DrawFrame& frame, Color color, Uint8 alpha = 255) {
  if (svg_points.size() < 3) return;
  SDL_Color sc{color.r, color.g, color.b, alpha};
  std::vector<SDL_Vertex> vertices;
  vertices.reserve(svg_points.size());
  for (Point p : svg_points) {
    Point screen = to_screen(rotate_svg_point(p, degrees, center), frame);
    vertices.push_back(SDL_Vertex{{static_cast<float>(screen.x), static_cast<float>(screen.y)}, sc, {0.0f, 0.0f}});
  }
  std::vector<int> indices;
  indices.reserve((svg_points.size() - 2) * 3);
  for (int i = 1; i + 1 < static_cast<int>(svg_points.size()); ++i) {
    indices.push_back(0);
    indices.push_back(i);
    indices.push_back(i + 1);
  }
  SDL_RenderGeometry(r, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                     indices.data(), static_cast<int>(indices.size()));
}

void draw_svg_hand(SDL_Renderer* r, const std::vector<Point>& polygon, double degrees, const DrawFrame& frame,
                   Color color, Uint8 alpha = 255, Point center = {kSvgCenter, kSvgCenter}) {
  fill_polygon(r, polygon, degrees, center, frame, color, alpha);
}

void fill_stroke(SDL_Renderer* r, float x1, float y1, float x2, float y2, Color c, float thickness,
                 Uint8 alpha = 255) {
  float dx = x2 - x1;
  float dy = y2 - y1;
  float len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0.0f) return;
  float nx = -dy / len * thickness * 0.5f;
  float ny = dx / len * thickness * 0.5f;
  SDL_Color sc{c.r, c.g, c.b, alpha};
  SDL_Vertex verts[4] = {
      {{x1 + nx, y1 + ny}, sc, {0.0f, 0.0f}},
      {{x1 - nx, y1 - ny}, sc, {0.0f, 0.0f}},
      {{x2 - nx, y2 - ny}, sc, {0.0f, 0.0f}},
      {{x2 + nx, y2 + ny}, sc, {0.0f, 0.0f}},
  };
  int indices[6] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(r, nullptr, verts, 4, indices, 6);
  fill_circle(r, x1, y1, thickness * 0.5f, c, alpha);
  fill_circle(r, x2, y2, thickness * 0.5f, c, alpha);
}

std::string analog_asset_path(const std::string& name) {
  std::array<std::string, 2> candidates = {
      std::string(NIXALARM_DATADIR) + "/analog/" + name,
      std::string("assets/runtime/analog/") + name,
  };
  for (const auto& path : candidates) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) continue;
    std::fclose(f);
    return path;
  }
  return candidates.front();
}

void log_gerror(const char* context, GError* error) {
  if (!error) return;
  std::cerr << "nixalarm: " << context << ": " << error->message << "\n";
  g_error_free(error);
}

class AnalogClock : public ClockFace {
 public:
  ~AnalogClock() override {
    if (face_texture_) SDL_DestroyTexture(face_texture_);
  }

  void render(SDL_Window* win, SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) override;

 private:
  bool ensure_face_texture(SDL_Renderer* r, int size, const std::string& asset_name);
  static DrawFrame frame_for(int ww, int wh);

  SDL_Texture* face_texture_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  int face_size_ = 0;
  std::string face_asset_name_;
  bool face_failed_ = false;
};

DrawFrame AnalogClock::frame_for(int ww, int wh) {
  float size = std::max(1.0f, std::min(static_cast<float>(ww), static_cast<float>(wh)) * 0.92f);
  return DrawFrame{(ww - size) * 0.5f, (wh - size) * 0.5f, size};
}

bool AnalogClock::ensure_face_texture(SDL_Renderer* r, int size, const std::string& asset_name) {
  if (face_texture_ && renderer_ == r && face_size_ == size && face_asset_name_ == asset_name) return true;
  if (face_texture_) {
    SDL_DestroyTexture(face_texture_);
    face_texture_ = nullptr;
  }
  renderer_ = r;
  face_size_ = size;
  face_asset_name_ = asset_name;

  std::string path = analog_asset_path(asset_name);
  GError* error = nullptr;
  RsvgHandle* raw_handle = rsvg_handle_new_from_file(path.c_str(), &error);
  if (!raw_handle) {
    if (!face_failed_) log_gerror("could not load analog clock SVG", error);
    face_failed_ = true;
    return false;
  }
  std::unique_ptr<RsvgHandle, decltype(&g_object_unref)> handle(raw_handle, g_object_unref);

  cairo_surface_t* raw_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  if (cairo_surface_status(raw_surface) != CAIRO_STATUS_SUCCESS) {
    std::cerr << "nixalarm: could not create analog clock SVG surface\n";
    cairo_surface_destroy(raw_surface);
    face_failed_ = true;
    return false;
  }
  std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface(raw_surface, cairo_surface_destroy);
  cairo_t* raw_cr = cairo_create(surface.get());
  std::unique_ptr<cairo_t, decltype(&cairo_destroy)> cr(raw_cr, cairo_destroy);
  double pad = std::max(10.0, size * 0.055);
  RsvgRectangle viewport{pad, pad, static_cast<double>(size) - 2.0 * pad,
                         static_cast<double>(size) - 2.0 * pad};
  if (!rsvg_handle_render_document(handle.get(), cr.get(), &viewport, &error)) {
    if (!face_failed_) log_gerror("could not render analog clock SVG", error);
    face_failed_ = true;
    return false;
  }
  cairo_surface_flush(surface.get());

  face_texture_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, size, size);
  if (!face_texture_) {
    std::cerr << "nixalarm: could not create analog clock texture: " << SDL_GetError() << "\n";
    face_failed_ = true;
    return false;
  }
  SDL_SetTextureBlendMode(face_texture_, SDL_BLENDMODE_BLEND);
  unsigned char* data = cairo_image_surface_get_data(surface.get());
  int stride = cairo_image_surface_get_stride(surface.get());
  if (SDL_UpdateTexture(face_texture_, nullptr, data, stride) != 0) {
    std::cerr << "nixalarm: could not upload analog clock texture: " << SDL_GetError() << "\n";
    SDL_DestroyTexture(face_texture_);
    face_texture_ = nullptr;
    face_failed_ = true;
    return false;
  }
  face_failed_ = false;
  return true;
}

void draw_alarm_indicator(SDL_Renderer* r, const DrawFrame& frame, const Config& cfg,
                          const RingState& ring, double pulse) {
  if (!ring.alarm_armed && !ring.ringing) return;

  float x = frame.x + frame.size * 0.50f;
  float y = frame.y + frame.size * 0.74f;
  float s = frame.size * 0.055f;
  Color color = ring.ringing ? Color{190, 0, 0, 255} : cfg.segment_on;
  Uint8 alpha = ring.ringing ? static_cast<Uint8>(190 + 65 * pulse) : 185;
  float t = std::max(1.0f, frame.size * 0.004f);

  constexpr int kSteps = 24;
  for (int i = 0; i < kSteps; ++i) {
    double a1 = (210.0 + i * 120.0 / kSteps) * kPi / 180.0;
    double a2 = (210.0 + (i + 1) * 120.0 / kSteps) * kPi / 180.0;
    fill_stroke(r, x + std::cos(a1) * s * 0.55f, y + std::sin(a1) * s * 0.55f,
                x + std::cos(a2) * s * 0.55f, y + std::sin(a2) * s * 0.55f, color, t, alpha);
  }
  fill_stroke(r, x - s * 0.55f, y + s * 0.28f, x + s * 0.55f, y + s * 0.28f, color, t, alpha);
  fill_stroke(r, x - s * 0.30f, y + s * 0.28f, x - s * 0.46f, y + s * 0.58f, color, t, alpha);
  fill_stroke(r, x + s * 0.30f, y + s * 0.28f, x + s * 0.46f, y + s * 0.58f, color, t, alpha);
  fill_circle(r, x, y + s * 0.45f, s * 0.11f, color, alpha);
}

void AnalogClock::render(SDL_Window* /*win*/, SDL_Renderer* r, int ww, int wh, const Config& cfg,
                          const RingState& ring) {
  set_color(r, cfg.background);
  SDL_RenderClear(r);

  DrawFrame frame = frame_for(ww, wh);
  int texture_size = std::max(64, static_cast<int>(std::ceil(frame.size)));
  std::string face_asset = cfg.roman_numerals ? "clock_face_roman.svg" : "clock_face.svg";
  if (ensure_face_texture(r, texture_size, face_asset)) {
    SDL_FRect dst{frame.x, frame.y, frame.size, frame.size};
    SDL_RenderCopyF(r, face_texture_, nullptr, &dst);
  } else {
    fill_circle(r, ww * 0.5f, wh * 0.5f, frame.size * 0.42f, cfg.background);
  }

  auto current = Clock::now();
  auto now = Clock::to_time_t(current);
  std::tm tm{};
  localtime_r(&now, &tm);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(current.time_since_epoch()).count();
  double fractional_second = tm.tm_sec + (ms % 1000) / 1000.0;
  analog::TimeParts t{tm.tm_hour, tm.tm_min, fractional_second};
  double pulse = (std::sin(ms / 1000.0 * 2.0 * kPi) + 1.0) * 0.5;

  static const std::vector<Point> kHourHand{{252.5, 156}, {252.5, 286}, {259.5, 286}, {259.5, 156}};
  static const std::vector<Point> kMinuteHand{{253.5, 141}, {253.5, 301}, {258.5, 301}, {258.5, 141}};
  static const std::vector<Point> kSecondHand{{258, 271}, {257, 271}, {257, 126}, {255, 126},
                                              {255, 271}, {254, 271}, {252, 321}, {260, 321}};

  Color black{0, 0, 0, 255};
  Color shadow{0, 0, 0, 255};
  Color red{190, 0, 0, 255};
  double hour_degrees = analog::hour_hand_degrees(t, false);
  double minute_degrees = analog::minute_hand_degrees(t);
  double second_degrees = analog::second_hand_degrees(t);

  draw_svg_hand(r, kHourHand, hour_degrees, frame, shadow, 70, {255, 257});
  draw_svg_hand(r, kHourHand, hour_degrees, frame, black);
  draw_svg_hand(r, kMinuteHand, minute_degrees, frame, shadow, 70, {254, 258});
  draw_svg_hand(r, kMinuteHand, minute_degrees, frame, black);
  if (cfg.show_seconds) {
    draw_svg_hand(r, kSecondHand, second_degrees, frame, shadow, 70, {253, 259});
    draw_svg_hand(r, kSecondHand, second_degrees, frame, red);
  }

  Point center = to_screen({kSvgCenter, kSvgCenter}, frame);
  fill_circle(r, static_cast<float>(center.x), static_cast<float>(center.y), frame.size * 0.0146f, black, 255);
  fill_circle(r, static_cast<float>(center.x - frame.size * 0.004f),
              static_cast<float>(center.y - frame.size * 0.004f), frame.size * 0.0045f, Color{255, 255, 255, 255}, 170);
  draw_alarm_indicator(r, frame, cfg, ring, pulse);

  if (ring.ringing) {
    float bar_w = ww * 0.84f;
    float bar_h = clampf(wh * 0.018f, 5.0f, 10.0f);
    SDL_FRect bg{ww * 0.08f, wh * 0.92f, bar_w, bar_h};
    SDL_FRect fg{ww * 0.08f, wh * 0.92f, bar_w * static_cast<float>(ring.hold_progress), bar_h};
    set_color(r, Color{116, 116, 116, 255}, 220);
    SDL_RenderFillRectF(r, &bg);
    set_color(r, red, 255);
    SDL_RenderFillRectF(r, &fg);
  }

  SDL_RenderPresent(r);
}

std::string roman(int value) {
  struct Entry {
    int value;
    const char* label;
  };
  static constexpr Entry kEntries[] = {{10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
  std::string out;
  for (const auto& entry : kEntries) {
    while (value >= entry.value) {
      out += entry.label;
      value -= entry.value;
    }
  }
  return out;
}

}  // namespace

namespace analog {

double hour_hand_degrees(TimeParts t, bool use_24_hour) {
  int cycle = use_24_hour ? 24 : 12;
  int hour = ((t.hour % cycle) + cycle) % cycle;
  double fraction = (static_cast<double>(hour) + t.minute / 60.0 + t.second / 3600.0) / cycle;
  return std::fmod(fraction * 360.0 + 360.0, 360.0);
}

double minute_hand_degrees(TimeParts t) {
  return std::fmod((t.minute + t.second / 60.0) * 6.0 + 360.0, 360.0);
}

double second_hand_degrees(TimeParts t) {
  return std::fmod(t.second * 6.0 + 360.0, 360.0);
}

std::vector<std::string> dial_labels(bool use_24_hour, bool roman_numerals,
                                     const std::string& analog_midnight_label) {
  int count = use_24_hour ? 24 : 12;
  std::vector<std::string> labels;
  labels.reserve(count);
  for (int i = 1; i <= count; ++i) {
    int value = i;
    if (use_24_hour && !roman_numerals && i == 24 && analog_midnight_label == "0") {
      value = 0;
    }
    labels.push_back(roman_numerals ? roman(i) : std::to_string(value));
  }
  return labels;
}

}  // namespace analog

std::unique_ptr<ClockFace> make_analog_clock() {
  return std::make_unique<AnalogClock>();
}
