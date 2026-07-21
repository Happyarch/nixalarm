#include "clock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

// Seven-segment clock: the original nixalarm look. Colors and glow come from
// Config (the theme), so this class holds no per-frame state of its own.
class SevenSegmentClock : public ClockFace {
 public:
  void render(SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) override;

 private:
  static const bool kSegments[10][7];

  static void set_color(SDL_Renderer* r, Color c, Uint8 alpha = 255);
  static void fill_rect(SDL_Renderer* r, float x, float y, float w, float h, Color c, Uint8 alpha = 255);
  static void draw_digit(SDL_Renderer* r, int digit, float x, float y, float w, float h, const Config& cfg);
  static void draw_colon(SDL_Renderer* r, float x, float y, float size, const Config& cfg);
};

const bool SevenSegmentClock::kSegments[10][7] = {
    {true, true, true, true, true, true, false},
    {false, true, true, false, false, false, false},
    {true, true, false, true, true, false, true},
    {true, true, true, true, false, false, true},
    {false, true, true, false, false, true, true},
    {true, false, true, true, false, true, true},
    {true, false, true, true, true, true, true},
    {true, true, true, false, false, false, false},
    {true, true, true, true, true, true, true},
    {true, true, true, true, false, true, true},
};

void SevenSegmentClock::set_color(SDL_Renderer* r, Color c, Uint8 alpha) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, alpha);
}

void SevenSegmentClock::fill_rect(SDL_Renderer* r, float x, float y, float w, float h, Color c, Uint8 alpha) {
  set_color(r, c, alpha);
  SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRectF(r, &rect);
}

void SevenSegmentClock::draw_digit(SDL_Renderer* r, int digit, float x, float y, float w, float h, const Config& cfg) {
  float t = std::max(5.0f, w * 0.14f);
  float gap = t * 0.45f;
  struct Seg { float x, y, w, h; };
  Seg segs[7] = {
      {x + t, y, w - 2 * t, t},
      {x + w - t, y + t + gap, t, h / 2 - 1.5f * t - gap},
      {x + w - t, y + h / 2 + t / 2, t, h / 2 - 1.5f * t - gap},
      {x + t, y + h - t, w - 2 * t, t},
      {x, y + h / 2 + t / 2, t, h / 2 - 1.5f * t - gap},
      {x, y + t + gap, t, h / 2 - 1.5f * t - gap},
      {x + t, y + h / 2 - t / 2, w - 2 * t, t},
  };
  for (int i = 0; i < 7; ++i) {
    bool on = kSegments[digit][i];
    if (on && cfg.glow) fill_rect(r, segs[i].x - 4, segs[i].y - 4, segs[i].w + 8, segs[i].h + 8, cfg.segment_on, 45);
    fill_rect(r, segs[i].x, segs[i].y, segs[i].w, segs[i].h, on ? cfg.segment_on : cfg.segment_off, on ? 255 : 170);
  }
}

void SevenSegmentClock::draw_colon(SDL_Renderer* r, float x, float y, float size, const Config& cfg) {
  float d = size * 0.12f;
  fill_rect(r, x, y + size * 0.32f, d, d, cfg.segment_on);
  fill_rect(r, x, y + size * 0.62f, d, d, cfg.segment_on);
}

void SevenSegmentClock::render(SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) {
  bool flash_digits_off = false;
  if (ring.ringing && cfg.flash_hz > 0.0) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    double phase = std::fmod(ms / 1000.0 * cfg.flash_hz, 1.0);
    flash_digits_off = phase >= 0.5;
  }
  Config draw_cfg = cfg;
  if (flash_digits_off) {
    draw_cfg.segment_on = cfg.segment_off;
    draw_cfg.glow = false;
  }
  set_color(r, cfg.background);
  SDL_RenderClear(r);
  auto now = Clock::to_time_t(Clock::now());
  std::tm tm{};
  localtime_r(&now, &tm);
  std::ostringstream ss;
  int display_hour = tm.tm_hour;
  if (!cfg.use_24_hour) {
    display_hour %= 12;
    if (display_hour == 0) display_hour = 12;
  }
  ss << std::setfill('0') << std::setw(2) << display_hour << ":" << std::setw(2) << tm.tm_min;
  if (cfg.show_seconds) ss << ":" << std::setw(2) << tm.tm_sec;
  std::string text = ss.str();

  int digits = static_cast<int>(std::count_if(text.begin(), text.end(), ::isdigit));
  int colons = static_cast<int>(std::count(text.begin(), text.end(), ':'));
  float unit_w = static_cast<float>(ww) / (digits + colons * 0.35f + 1.0f);
  float digit_w = unit_w * 0.82f;
  float digit_h = std::min(static_cast<float>(wh) * 0.72f, digit_w * 1.85f);
  digit_w = digit_h / 1.85f;
  float colon_w = digit_w * 0.22f;
  float total_w = digits * digit_w + colons * colon_w + (text.size() - 1) * digit_w * 0.12f;
  float x = (ww - total_w) / 2.0f;
  float y = (wh - digit_h) / 2.0f;
  for (char c : text) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      draw_digit(r, c - '0', x, y, digit_w, digit_h, draw_cfg);
      x += digit_w + digit_w * 0.12f;
    } else {
      draw_colon(r, x + colon_w * 0.25f, y, digit_h, draw_cfg);
      x += colon_w + digit_w * 0.12f;
    }
  }
  if (ring.ringing) {
    Color c = cfg.segment_on;
    fill_rect(r, ww * 0.08f, wh * 0.90f, ww * 0.84f, 8, cfg.segment_off);
    fill_rect(r, ww * 0.08f, wh * 0.90f, ww * 0.84f * static_cast<float>(ring.hold_progress), 8, c);
  }
  SDL_RenderPresent(r);
}

}  // namespace

std::unique_ptr<ClockFace> make_clock_face(const Config& cfg) {
  if (cfg.clock_face == "seven_segment") {
    return std::make_unique<SevenSegmentClock>();
  }
  std::cerr << "nixalarm: unknown clock face: " << cfg.clock_face << "; using seven_segment\n";
  return std::make_unique<SevenSegmentClock>();
}
