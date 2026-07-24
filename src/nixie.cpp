#include "nixie.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "stb_image.h"

#ifndef NIXALARM_DATADIR
#define NIXALARM_DATADIR "/usr/local/share/nixalarm"
#endif

namespace {

// One decoded image kept in CPU memory (RGBA) plus its GPU texture (created lazily).
struct Image {
  int w = 0, h = 0;
  SDL_Texture* tex = nullptr;
};

struct Box {
  int x, y, w, h;
};

// ---- minimal meta.json reader (we control the format; parse just what we need) --
long json_int(const std::string& s, const std::string& key) {
  auto p = s.find('"' + key + '"');
  if (p == std::string::npos) return 0;
  p = s.find(':', p);
  if (p == std::string::npos) return 0;
  return std::strtol(s.c_str() + p + 1, nullptr, 10);
}

std::vector<Box> json_boxes(const std::string& s, const std::string& key) {
  std::vector<Box> out;
  auto p = s.find('"' + key + '"');
  if (p == std::string::npos) return out;
  p = s.find('[', p);
  if (p == std::string::npos) return out;
  int depth = 0;
  std::vector<int> nums;
  for (size_t i = p; i < s.size(); ++i) {
    char c = s[i];
    if (c == '[') {
      ++depth;
    } else if (c == ']') {
      if (--depth == 0) break;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
      nums.push_back(static_cast<int>(std::strtol(s.c_str() + i, nullptr, 10)));
      while (i + 1 < s.size() && (s[i + 1] == '-' || (s[i + 1] >= '0' && s[i + 1] <= '9'))) ++i;
    }
  }
  for (size_t i = 0; i + 3 < nums.size(); i += 4)
    out.push_back({nums[i], nums[i + 1], nums[i + 2], nums[i + 3]});
  return out;
}

std::vector<unsigned char> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
}

// Resolve the runtime asset directory for a given tube layout ("nixie" for the
// 6-tube HH:MM:SS set, "nixie-hhmm" for the 4-tube HH:MM set): env override,
// then the installed data dir, then dev locations relative to the CWD and the
// executable. NIXALARM_ASSET_DIR is an unconditional override of whichever
// layout is being resolved, for pointing at a single staged dir during asset
// development.
std::string find_asset_dir(const std::string& subdir) {
  std::vector<std::string> candidates;
  if (const char* e = std::getenv("NIXALARM_ASSET_DIR"); e && *e) candidates.emplace_back(e);
  candidates.emplace_back(std::string(NIXALARM_DATADIR) + "/" + subdir);
  candidates.emplace_back("assets/runtime/" + subdir);
  if (char* base = SDL_GetBasePath()) {
    candidates.emplace_back(std::string(base) + "../share/nixalarm/" + subdir);
    candidates.emplace_back(std::string(base) + "assets/runtime/" + subdir);
    SDL_free(base);
  }
  for (const auto& c : candidates) {
    std::ifstream probe(c + "/meta.json");
    if (probe) return c;
  }
  return {};
}

class NixieClock : public ClockFace {
 public:
  void render(SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) override;

 private:
  bool ensure_loaded(SDL_Renderer* r, bool show_seconds);
  bool load_image(SDL_Renderer* r, const std::string& file, Image& out);
  void ensure_target(SDL_Renderer* r, int cw, int ch);
  void recompose(SDL_Renderer* r, const std::string& digits);
  static std::string time_digits(const Config& cfg, std::string& text);

  bool tried_load_ = false;
  bool ok_ = false;
  std::string dir_;
  int master_w_ = 0, master_h_ = 0;
  int cell_w_ = 0, cell_h_ = 0;
  std::vector<Box> tubes_;

  Image plate_, backing_, trans_, static_emis_;
  std::array<Image, 10> digits_{};

  SDL_Texture* target_ = nullptr;
  int tw_ = 0, th_ = 0;              // current target (content) size
  std::string composed_;            // digit string currently baked into target_
};

bool NixieClock::load_image(SDL_Renderer* r, const std::string& file, Image& out) {
  std::vector<unsigned char> bytes = read_file(dir_ + "/" + file);
  if (bytes.empty()) {
    std::cerr << "nixalarm: missing asset " << dir_ << "/" << file << "\n";
    return false;
  }
  int n = 0;
  unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &out.w, &out.h, &n, 4);
  if (!px) {
    std::cerr << "nixalarm: decode failed for " << file << ": " << stbi_failure_reason() << "\n";
    return false;
  }
  out.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, out.w, out.h);
  if (out.tex) SDL_UpdateTexture(out.tex, nullptr, px, out.w * 4);
  stbi_image_free(px);
  return out.tex != nullptr;
}

bool NixieClock::ensure_loaded(SDL_Renderer* r, bool show_seconds) {
  if (tried_load_) return ok_;
  tried_load_ = true;

  // show_seconds picks the 4-tube HH:MM plate over the 6-tube HH:MM:SS one; fall
  // back to HH:MM:SS (leaving the extra tubes unlit) if HH:MM assets aren't staged.
  const std::string wanted = show_seconds ? "nixie" : "nixie-hhmm";
  dir_ = find_asset_dir(wanted);
  if (dir_.empty() && wanted != "nixie") dir_ = find_asset_dir("nixie");
  if (dir_.empty()) {
    std::cerr << "nixalarm: nixie assets not found (set NIXALARM_ASSET_DIR)\n";
    return false;
  }
  std::string meta;
  {
    auto b = read_file(dir_ + "/meta.json");
    meta.assign(b.begin(), b.end());
  }
  master_w_ = static_cast<int>(json_int(meta, "width"));
  master_h_ = static_cast<int>(json_int(meta, "height"));
  cell_w_ = static_cast<int>(json_int(meta, "cell_w"));
  cell_h_ = static_cast<int>(json_int(meta, "cell_h"));
  tubes_ = json_boxes(meta, "tube_boxes");
  if (master_w_ <= 0 || master_h_ <= 0 || cell_w_ <= 0 || tubes_.empty()) {
    std::cerr << "nixalarm: bad nixie meta.json\n";
    return false;
  }

  ok_ = load_image(r, "plate.png", plate_) &&
        load_image(r, "backing.png", backing_) &&
        load_image(r, "transmittance.png", trans_) &&
        load_image(r, "static_emission.png", static_emis_);
  for (int i = 0; i < 10 && ok_; ++i)
    ok_ = load_image(r, "digit_" + std::to_string(i) + ".png", digits_[i]);
  if (!ok_) return false;

  // Blend modes: emission accumulates additively, transmittance multiplies.
  SDL_SetTextureBlendMode(static_emis_.tex, SDL_BLENDMODE_ADD);
  for (auto& d : digits_) SDL_SetTextureBlendMode(d.tex, SDL_BLENDMODE_ADD);
  SDL_SetTextureBlendMode(trans_.tex, SDL_BLENDMODE_MOD);
  SDL_SetTextureBlendMode(plate_.tex, SDL_BLENDMODE_NONE);
  // backing is the backmost tube layer; it accumulates with the digits and is then
  // occluded by the transmittance (mesh in front of both). rgb is premultiplied.
  SDL_SetTextureBlendMode(backing_.tex, SDL_BLENDMODE_ADD);
  return true;
}

void NixieClock::ensure_target(SDL_Renderer* r, int cw, int ch) {
  if (target_ && tw_ == cw && th_ == ch) return;
  if (target_) SDL_DestroyTexture(target_);
  target_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, cw, ch);
  SDL_SetTextureBlendMode(target_, SDL_BLENDMODE_ADD);
  tw_ = cw;
  th_ = ch;
  composed_.clear();  // force a recompose at the new size
}

// Bake  T = (sum of digit emission + bulbs) * transmittance  at content resolution.
void NixieClock::recompose(SDL_Renderer* r, const std::string& digits) {
  const double sx = static_cast<double>(tw_) / master_w_;
  const double sy = static_cast<double>(th_) / master_h_;

  SDL_SetRenderTarget(r, target_);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  SDL_RenderClear(r);

  // draw order backing -> (bulbs + unlit cathode ghosts) -> lit digits, all summed,
  // then multiplied by the transmittance (mesh + glass) so the mesh sits in front.
  SDL_RenderCopy(r, backing_.tex, nullptr, nullptr);
  SDL_RenderCopy(r, static_emis_.tex, nullptr, nullptr);
  for (size_t i = 0; i < digits.size() && i < tubes_.size(); ++i) {
    int d = digits[i] - '0';
    if (d < 0 || d > 9) continue;
    const Box& t = tubes_[i];
    double ccx = (t.x + t.w / 2.0) * sx;
    double ccy = (t.y + t.h / 2.0) * sy;
    SDL_FRect dst{static_cast<float>(ccx - cell_w_ * sx / 2.0),
                  static_cast<float>(ccy - cell_h_ * sy / 2.0),
                  static_cast<float>(cell_w_ * sx),
                  static_cast<float>(cell_h_ * sy)};
    SDL_RenderCopyF(r, digits_[d].tex, nullptr, &dst);
  }
  // multiply the accumulated emission by the glass+mesh transmittance
  SDL_RenderCopy(r, trans_.tex, nullptr, nullptr);

  SDL_SetRenderTarget(r, nullptr);
  composed_ = digits;
}

std::string NixieClock::time_digits(const Config& cfg, std::string& text) {
  std::time_t now = Clock::to_time_t(Clock::now());
  std::tm tm{};
  localtime_r(&now, &tm);
  int hour = tm.tm_hour;
  if (!cfg.use_24_hour) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  char buf[16];
  if (cfg.show_seconds)
    std::snprintf(buf, sizeof(buf), "%02d%02d%02d", hour, tm.tm_min, tm.tm_sec);
  else
    std::snprintf(buf, sizeof(buf), "%02d%02d", hour, tm.tm_min);
  text = buf;
  return text;
}

void NixieClock::render(SDL_Renderer* r, int ww, int wh, const Config& cfg, const RingState& ring) {
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  SDL_RenderClear(r);

  if (!ensure_loaded(r, cfg.show_seconds)) {
    SDL_RenderPresent(r);  // black frame; error already logged once
    return;
  }

  // letterbox a 16:9 content rect inside the window
  double target_ar = static_cast<double>(master_w_) / master_h_;
  int cw = ww, ch = wh;
  if (static_cast<double>(ww) / wh > target_ar)
    cw = static_cast<int>(std::lround(wh * target_ar));
  else
    ch = static_cast<int>(std::lround(ww / target_ar));
  if (cw < 1 || ch < 1) {
    SDL_RenderPresent(r);
    return;
  }
  SDL_Rect content{(ww - cw) / 2, (wh - ch) / 2, cw, ch};

  std::string text;
  std::string digits = time_digits(cfg, text);

  ensure_target(r, cw, ch);
  if (digits != composed_) recompose(r, digits);

  // flash: while ringing, blank the glow on the off phase (bulbs + digits go dark)
  bool glow_off = false;
  if (ring.ringing && cfg.flash_hz > 0.0) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  Clock::now().time_since_epoch())
                  .count();
    glow_off = std::fmod(ms / 1000.0 * cfg.flash_hz, 1.0) >= 0.5;
  }

  SDL_RenderCopy(r, plate_.tex, nullptr, &content);
  if (!glow_off) SDL_RenderCopy(r, target_, nullptr, &content);

  if (ring.ringing) {
    SDL_Rect track{content.x + cw / 12, content.y + ch * 92 / 100, cw * 10 / 12, std::max(3, ch / 160)};
    SDL_SetRenderDrawColor(r, 60, 22, 0, 255);
    SDL_RenderFillRect(r, &track);
    SDL_Rect fill{track.x, track.y,
                  static_cast<int>(track.w * std::clamp(ring.hold_progress, 0.0, 1.0)),
                  track.h};
    SDL_SetRenderDrawColor(r, 255, 106, 0, 255);
    SDL_RenderFillRect(r, &fill);
  }

  SDL_RenderPresent(r);
}

}  // namespace

std::unique_ptr<ClockFace> make_nixie_clock() {
  return std::make_unique<NixieClock>();
}
