#include <SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

constexpr int kAudioRate = 48000;
constexpr SDL_AudioFormat kAudioFormat = AUDIO_S16SYS;
constexpr int kAudioChannels = 1;

struct Color {
  Uint8 r = 0;
  Uint8 g = 0;
  Uint8 b = 0;
  Uint8 a = 255;
};

enum class SourceType { Generated, File, Internet, SdrWeatherband };

struct Source {
  SourceType type = SourceType::Generated;
  std::string path;
  std::string url;
  double frequency_mhz = 162.45;
  int device_index = 0;
  std::string gain = "auto";
};

struct Config {
  std::string alarm_source = "generated";
  int snooze_minutes = 10;
  int hold_to_stop_seconds = 10;
  double volume = 0.9;
  int source_start_timeout_seconds = 8;
  std::string fallback_source = "generated";
  int width = 800;
  int height = 360;
  bool fullscreen = false;
  bool always_on_top = false;
  std::string theme = "terminal_glow";
  Color background{7, 24, 11, 255};
  Color segment_on{108, 255, 87, 255};
  Color segment_off{18, 51, 25, 255};
  bool glow = true;
  bool show_seconds = false;
  std::map<std::string, Source> sources;
};

static std::string trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

static std::string unquote(std::string s) {
  s = trim(s);
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

static bool parse_bool(const std::string& s) {
  return s == "true" || s == "1" || s == "yes" || s == "on";
}

static Color parse_color(const std::string& raw, Color fallback) {
  std::string s = unquote(raw);
  if (s.size() != 7 || s[0] != '#') return fallback;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
  };
  int vals[6];
  for (int i = 0; i < 6; ++i) {
    vals[i] = hex(s[i + 1]);
    if (vals[i] < 0) return fallback;
  }
  return Color{static_cast<Uint8>(vals[0] * 16 + vals[1]),
               static_cast<Uint8>(vals[2] * 16 + vals[3]),
               static_cast<Uint8>(vals[4] * 16 + vals[5]), 255};
}

static bool apply_theme(Config& cfg, const std::string& theme) {
  if (theme == "terminal_glow") {
    cfg.theme = theme;
    cfg.background = Color{7, 24, 11, 255};
    cfg.segment_on = Color{108, 255, 87, 255};
    cfg.segment_off = Color{18, 51, 25, 255};
    cfg.glow = true;
    return true;
  }
  if (theme == "sinnoh_green") {
    cfg.theme = theme;
    cfg.background = Color{113, 177, 108, 255};
    cfg.segment_on = Color{44, 91, 51, 255};
    cfg.segment_off = Color{104, 166, 99, 255};
    cfg.glow = false;
    return true;
  }
  return false;
}

static Source make_sdr(double mhz) {
  Source s;
  s.type = SourceType::SdrWeatherband;
  s.frequency_mhz = mhz;
  return s;
}

static std::map<std::string, Source> builtin_sources() {
  std::map<std::string, Source> m;
  m["generated"] = Source{};
  Source linville;
  linville.type = SourceType::Internet;
  linville.url = "https://wxradio.org/NC-Linville-WNG538";
  m["nwr_caldwell_internet_backup"] = linville;
  m["nwr_nc_linville_wng538"] = linville;
  Source mtj = make_sdr(162.500);
  m["nwr_caldwell_preferred"] = mtj;
  m["nwr_nc_mount_jefferson_wng588"] = mtj;
  m["nwr_nc_black_mountain_wng588"] = mtj;
  for (double f : {162.400, 162.425, 162.450, 162.475, 162.500, 162.525, 162.550}) {
    std::ostringstream name;
    name << "weatherband_" << std::fixed << std::setprecision(3) << f;
    std::string key = name.str();
    std::replace(key.begin(), key.end(), '.', '_');
    m[key] = make_sdr(f);
  }
  return m;
}

static fs::path default_config_path() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return fs::path(xdg) / "nixalarm" / "config.toml";
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / ".config" / "nixalarm" / "config.toml";
  }
  return "config.toml";
}

static Config load_config(const fs::path& path) {
  Config cfg;
  cfg.sources = builtin_sources();
  std::ifstream in(path);
  if (!in) return cfg;

  std::string section;
  std::string source_name;
  std::string line;
  while (std::getline(in, line)) {
    auto comment = line.find('#');
    if (comment != std::string::npos && (line.find('"') == std::string::npos || comment < line.find('"'))) {
      line = line.substr(0, comment);
    }
    line = trim(line);
    if (line.empty()) continue;
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      source_name.clear();
      const std::string prefix = "sources.";
      if (section.rfind(prefix, 0) == 0) {
        source_name = section.substr(prefix.size());
        cfg.sources[source_name] = Source{};
      }
      continue;
    }
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));

    try {
      if (section.empty()) {
        if (key == "alarm_source") cfg.alarm_source = unquote(val);
        else if (key == "snooze_minutes") cfg.snooze_minutes = std::max(1, std::stoi(val));
        else if (key == "hold_to_stop_seconds") cfg.hold_to_stop_seconds = std::max(1, std::stoi(val));
        else if (key == "volume") cfg.volume = std::clamp(std::stod(val), 0.0, 1.0);
        else if (key == "source_start_timeout_seconds") cfg.source_start_timeout_seconds = std::max(1, std::stoi(val));
        else if (key == "fallback_source") cfg.fallback_source = unquote(val);
      } else if (section == "window") {
        if (key == "width") cfg.width = std::max(240, std::stoi(val));
        else if (key == "height") cfg.height = std::max(160, std::stoi(val));
        else if (key == "fullscreen") cfg.fullscreen = parse_bool(val);
        else if (key == "always_on_top") cfg.always_on_top = parse_bool(val);
      } else if (section == "style") {
        if (key == "theme") {
          std::string theme = unquote(val);
          if (!apply_theme(cfg, theme)) {
            std::cerr << "nixalarm: unknown theme: " << theme << "\n";
          }
        } else if (key == "background") cfg.background = parse_color(val, cfg.background);
        else if (key == "segment_on") cfg.segment_on = parse_color(val, cfg.segment_on);
        else if (key == "segment_off") cfg.segment_off = parse_color(val, cfg.segment_off);
        else if (key == "glow") cfg.glow = parse_bool(val);
        else if (key == "show_seconds") cfg.show_seconds = parse_bool(val);
      } else if (!source_name.empty()) {
        Source& s = cfg.sources[source_name];
        if (key == "type") {
          std::string type = unquote(val);
          if (type == "file") s.type = SourceType::File;
          else if (type == "internet") s.type = SourceType::Internet;
          else if (type == "sdr_weatherband") s.type = SourceType::SdrWeatherband;
          else s.type = SourceType::Generated;
        } else if (key == "path") s.path = unquote(val);
        else if (key == "url") s.url = unquote(val);
        else if (key == "frequency_mhz") s.frequency_mhz = std::stod(val);
        else if (key == "device_index") s.device_index = std::stoi(val);
        else if (key == "gain") s.gain = unquote(val);
      }
    } catch (const std::exception& e) {
      std::cerr << "nixalarm: ignoring invalid config value " << key << "=" << val << ": " << e.what() << "\n";
    }
  }
  return cfg;
}

class AudioPlayer {
 public:
  AudioPlayer(double volume, int source_timeout_seconds)
      : volume_(volume), source_timeout_seconds_(std::max(1, source_timeout_seconds)) {}
  ~AudioPlayer() { stop(); if (device_) SDL_CloseAudioDevice(device_); }

  bool open() {
    SDL_AudioSpec want{};
    want.freq = kAudioRate;
    want.format = kAudioFormat;
    want.channels = kAudioChannels;
    want.samples = 2048;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have_, 0);
    if (!device_) {
      std::cerr << "nixalarm: audio open failed: " << SDL_GetError() << "\n";
      return false;
    }
    SDL_PauseAudioDevice(device_, 0);
    return true;
  }

  void start(Source source, const std::string& fallback, const std::map<std::string, Source>& all) {
    stop();
    stopping_ = false;
    SDL_ClearQueuedAudio(device_);
    worker_ = std::thread([this, source, fallback, all]() {
      bool ok = false;
      if (source.type == SourceType::Generated) ok = play_generated();
      else if (source.type == SourceType::File) ok = play_ffmpeg(source.path);
      else if (source.type == SourceType::Internet) ok = play_ffmpeg(source.url);
      else if (source.type == SourceType::SdrWeatherband) ok = play_sdr(source);
      if (!ok && !stopping_) {
        auto it = all.find(fallback);
        if (it != all.end()) {
          std::cerr << "nixalarm: source failed; falling back to " << fallback << "\n";
          Source fb = it->second;
          if (fb.type == SourceType::Generated) play_generated();
          else if (fb.type == SourceType::File) play_ffmpeg(fb.path);
          else if (fb.type == SourceType::Internet) play_ffmpeg(fb.url);
          else if (fb.type == SourceType::SdrWeatherband) play_sdr(fb);
        } else {
          play_generated();
        }
      }
    });
  }

  void stop() {
    stopping_ = true;
    if (worker_.joinable()) worker_.join();
    if (device_) SDL_ClearQueuedAudio(device_);
  }

 private:
  void queue_bytes(const Uint8* data, Uint32 bytes) {
    while (!stopping_ && SDL_GetQueuedAudioSize(device_) > static_cast<Uint32>(kAudioRate * 2)) {
      SDL_Delay(20);
    }
    if (!stopping_) SDL_QueueAudio(device_, data, bytes);
  }

  bool play_generated() {
    double phase = 0.0;
    const double two_pi = 6.283185307179586;
    std::vector<int16_t> buf(1024);
    while (!stopping_) {
      auto now = Clock::now().time_since_epoch();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
      double freq = ((ms / 450) % 2 == 0) ? 880.0 : 660.0;
      for (auto& sample : buf) {
        double gate = ((ms / 900) % 2 == 0) ? 1.0 : 0.35;
        sample = static_cast<int16_t>(std::sin(phase) * 30000.0 * volume_ * gate);
        phase += two_pi * freq / kAudioRate;
        if (phase > two_pi) phase -= two_pi;
      }
      queue_bytes(reinterpret_cast<Uint8*>(buf.data()), static_cast<Uint32>(buf.size() * sizeof(int16_t)));
    }
    return true;
  }

  bool play_ffmpeg(const std::string& uri) {
    if (uri.empty()) return false;
    AVFormatContext* fmt = nullptr;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    std::string timeout_us = std::to_string(source_timeout_seconds_ * 1000000);
    av_dict_set(&opts, "rw_timeout", timeout_us.c_str(), 0);
    if (avformat_open_input(&fmt, uri.c_str(), nullptr, &opts) < 0) {
      av_dict_free(&opts);
      std::cerr << "nixalarm: could not open audio source: " << uri << "\n";
      return false;
    }
    av_dict_free(&opts);
    auto fmt_deleter = [](AVFormatContext* p) {
      if (p) avformat_close_input(&p);
    };
    std::unique_ptr<AVFormatContext, decltype(fmt_deleter)> fmt_guard(fmt, fmt_deleter);
    if (avformat_find_stream_info(fmt, nullptr) < 0) return false;
    int stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) return false;
    AVStream* stream = fmt->streams[stream_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;
    AVCodecContext* raw_ctx = avcodec_alloc_context3(codec);
    if (!raw_ctx) return false;
    auto codec_deleter = [](AVCodecContext* p) {
      if (p) avcodec_free_context(&p);
    };
    std::unique_ptr<AVCodecContext, decltype(codec_deleter)> ctx(raw_ctx, codec_deleter);
    if (avcodec_parameters_to_context(ctx.get(), stream->codecpar) < 0) return false;
    if (avcodec_open2(ctx.get(), codec, nullptr) < 0) return false;

    SwrContext* raw_swr = nullptr;
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, 1);
    AVChannelLayout in_layout = ctx->ch_layout;
    if (in_layout.nb_channels == 0) av_channel_layout_default(&in_layout, ctx->ch_layout.nb_channels ? ctx->ch_layout.nb_channels : 2);
    if (swr_alloc_set_opts2(&raw_swr, &out_layout, AV_SAMPLE_FMT_S16, kAudioRate,
                            &in_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr) < 0) return false;
    auto swr_deleter = [](SwrContext* p) {
      if (p) swr_free(&p);
    };
    std::unique_ptr<SwrContext, decltype(swr_deleter)> swr(raw_swr, swr_deleter);
    if (swr_init(swr.get()) < 0) return false;

    AVPacket* raw_pkt = av_packet_alloc();
    AVFrame* raw_frame = av_frame_alloc();
    if (!raw_pkt || !raw_frame) return false;
    auto packet_deleter = [](AVPacket* p) {
      if (p) av_packet_free(&p);
    };
    auto frame_deleter = [](AVFrame* p) {
      if (p) av_frame_free(&p);
    };
    std::unique_ptr<AVPacket, decltype(packet_deleter)> pkt(raw_pkt, packet_deleter);
    std::unique_ptr<AVFrame, decltype(frame_deleter)> frame(raw_frame, frame_deleter);
    bool played = false;
    while (!stopping_) {
      int read = av_read_frame(fmt, pkt.get());
      if (read < 0) {
        if (fmt->duration > 0 && source_is_file(uri)) {
          av_seek_frame(fmt, stream_index, 0, AVSEEK_FLAG_BACKWARD);
          continue;
        }
        break;
      }
      if (pkt->stream_index != stream_index) {
        av_packet_unref(pkt.get());
        continue;
      }
      if (avcodec_send_packet(ctx.get(), pkt.get()) == 0) {
        while (!stopping_ && avcodec_receive_frame(ctx.get(), frame.get()) == 0) {
          int out_count = av_rescale_rnd(swr_get_delay(swr.get(), ctx->sample_rate) + frame->nb_samples,
                                         kAudioRate, ctx->sample_rate, AV_ROUND_UP);
          std::vector<int16_t> out(out_count);
          uint8_t* out_data = reinterpret_cast<uint8_t*>(out.data());
          int converted = swr_convert(swr.get(), &out_data, out_count,
                                      const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
          if (converted > 0) {
            for (int i = 0; i < converted; ++i) {
              out[i] = static_cast<int16_t>(std::clamp(out[i] * volume_, -32768.0, 32767.0));
            }
            queue_bytes(reinterpret_cast<Uint8*>(out.data()), static_cast<Uint32>(converted * sizeof(int16_t)));
            played = true;
          }
          av_frame_unref(frame.get());
        }
      }
      av_packet_unref(pkt.get());
    }
    return played;
  }

  static bool source_is_file(const std::string& uri) {
    return uri.find("://") == std::string::npos;
  }

  bool play_sdr(const Source& source) {
    std::ostringstream cmd;
    cmd << "rtl_fm -f " << std::fixed << std::setprecision(3) << source.frequency_mhz
        << "M -M fm -s 12000 -r " << kAudioRate << " -E deemp -d " << source.device_index;
    if (source.gain != "auto") cmd << " -g " << source.gain;
    cmd << " 2>/dev/null";
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
      std::cerr << "nixalarm: failed to start rtl_fm\n";
      return false;
    }
    std::vector<int16_t> buf(2048);
    bool played = false;
    while (!stopping_) {
      size_t n = fread(buf.data(), sizeof(int16_t), buf.size(), pipe);
      if (n == 0) break;
      for (size_t i = 0; i < n; ++i) {
        buf[i] = static_cast<int16_t>(std::clamp(buf[i] * volume_, -32768.0, 32767.0));
      }
      queue_bytes(reinterpret_cast<Uint8*>(buf.data()), static_cast<Uint32>(n * sizeof(int16_t)));
      played = true;
    }
    pclose(pipe);
    return played;
  }

  SDL_AudioDeviceID device_ = 0;
  SDL_AudioSpec have_{};
  std::atomic<bool> stopping_{false};
  std::thread worker_;
  double volume_ = 1.0;
  int source_timeout_seconds_ = 8;
};

static const bool kSegments[10][7] = {
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

static void set_color(SDL_Renderer* r, Color c, Uint8 alpha = 255) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, alpha);
}

static void fill_rect(SDL_Renderer* r, float x, float y, float w, float h, Color c, Uint8 alpha = 255) {
  set_color(r, c, alpha);
  SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRectF(r, &rect);
}

static void draw_digit(SDL_Renderer* r, int digit, float x, float y, float w, float h, const Config& cfg) {
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

static void draw_colon(SDL_Renderer* r, float x, float y, float size, const Config& cfg) {
  float d = size * 0.12f;
  fill_rect(r, x, y + size * 0.32f, d, d, cfg.segment_on);
  fill_rect(r, x, y + size * 0.62f, d, d, cfg.segment_on);
}

static void draw_clock(SDL_Renderer* r, int ww, int wh, const Config& cfg, bool ringing, double hold_progress) {
  set_color(r, cfg.background);
  SDL_RenderClear(r);
  auto now = Clock::to_time_t(Clock::now());
  std::tm tm{};
  localtime_r(&now, &tm);
  std::ostringstream ss;
  ss << std::setfill('0') << std::setw(2) << tm.tm_hour << ":" << std::setw(2) << tm.tm_min;
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
      draw_digit(r, c - '0', x, y, digit_w, digit_h, cfg);
      x += digit_w + digit_w * 0.12f;
    } else {
      draw_colon(r, x + colon_w * 0.25f, y, digit_h, cfg);
      x += colon_w + digit_w * 0.12f;
    }
  }
  if (ringing) {
    Color c = cfg.segment_on;
    fill_rect(r, ww * 0.08f, wh * 0.90f, ww * 0.84f, 8, cfg.segment_off);
    fill_rect(r, ww * 0.08f, wh * 0.90f, ww * 0.84f * static_cast<float>(hold_progress), 8, c);
  }
  SDL_RenderPresent(r);
}

static void usage() {
  std::cout <<
      "nixalarm 0.1.0\n"
      "Usage: nixalarm [OPTIONS] HH:MM\n\n"
      "Options:\n"
      "  --config PATH        Read config from PATH instead of XDG config.\n"
      "  --source NAME        Use a configured or built-in source.\n"
      "  --fullscreen         Start fullscreen.\n"
      "  --windowed           Start windowed, overriding config fullscreen.\n"
      "  --list-sources       Print built-in and configured sources.\n"
      "  --test-source NAME   Play a source immediately.\n"
      "  --help               Print help.\n"
      "  --version            Print version.\n";
}

static std::optional<std::chrono::time_point<Clock>> parse_alarm_time(const std::string& s) {
  int h = -1, m = -1;
  char colon = 0;
  std::istringstream iss(s);
  if (!(iss >> h >> colon >> m) || colon != ':' || h < 0 || h > 23 || m < 0 || m > 59) return std::nullopt;
  auto now = Clock::now();
  std::time_t tt = Clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&tt, &tm);
  tm.tm_hour = h;
  tm.tm_min = m;
  tm.tm_sec = 0;
  auto alarm = Clock::from_time_t(std::mktime(&tm));
  if (alarm <= now) alarm += std::chrono::hours(24);
  return alarm;
}

static void list_sources(const Config& cfg) {
  for (const auto& [name, source] : cfg.sources) {
    std::cout << name << "\t";
    if (source.type == SourceType::Generated) std::cout << "generated";
    else if (source.type == SourceType::File) std::cout << "file\t" << source.path;
    else if (source.type == SourceType::Internet) std::cout << "internet\t" << source.url;
    else if (source.type == SourceType::SdrWeatherband) std::cout << "sdr_weatherband\t" << source.frequency_mhz << " MHz";
    std::cout << "\n";
  }
}

int main(int argc, char** argv) {
  fs::path config_path = default_config_path();
  std::string source_override;
  std::string test_source;
  std::string alarm_arg;
  bool do_list = false;
  std::optional<bool> fullscreen_override;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") { usage(); return 0; }
    if (arg == "--version") { std::cout << "nixalarm 0.1.0\n"; return 0; }
    if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
    else if (arg == "--source" && i + 1 < argc) source_override = argv[++i];
    else if (arg == "--test-source" && i + 1 < argc) test_source = argv[++i];
    else if (arg == "--fullscreen") fullscreen_override = true;
    else if (arg == "--windowed") fullscreen_override = false;
    else if (arg == "--list-sources") do_list = true;
    else if (!arg.empty() && arg[0] != '-') alarm_arg = arg;
    else {
      std::cerr << "nixalarm: unknown or incomplete option: " << arg << "\n";
      return 2;
    }
  }

  Config cfg = load_config(config_path);
  if (!source_override.empty()) cfg.alarm_source = source_override;
  if (fullscreen_override) cfg.fullscreen = *fullscreen_override;
  if (do_list) { list_sources(cfg); return 0; }

  std::optional<std::chrono::time_point<Clock>> alarm_time;
  if (test_source.empty()) {
    if (alarm_arg.empty()) { usage(); return 2; }
    alarm_time = parse_alarm_time(alarm_arg);
    if (!alarm_time) {
      std::cerr << "nixalarm: invalid alarm time, expected HH:MM\n";
      return 2;
    }
  } else {
    cfg.alarm_source = test_source;
  }

  auto source_it = cfg.sources.find(cfg.alarm_source);
  if (source_it == cfg.sources.end()) {
    std::cerr << "nixalarm: unknown source: " << cfg.alarm_source << "\n";
    return 2;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    std::cerr << "nixalarm: SDL init failed: " << SDL_GetError() << "\n";
    return 1;
  }
  std::atexit(SDL_Quit);

  Uint32 flags = SDL_WINDOW_RESIZABLE;
  if (cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  if (cfg.always_on_top) flags |= SDL_WINDOW_ALWAYS_ON_TOP;
  SDL_Window* win = SDL_CreateWindow("nixalarm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cfg.width, cfg.height, flags);
  if (!win) {
    std::cerr << "nixalarm: window create failed: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    std::cerr << "nixalarm: renderer create failed: " << SDL_GetError() << "\n";
    return 1;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  AudioPlayer audio(cfg.volume, cfg.source_start_timeout_seconds);
  if (!audio.open()) return 1;

  bool ringing = !test_source.empty();
  bool running = true;
  bool space_down = false;
  auto space_started = Clock::now();
  double hold_progress = 0.0;
  if (ringing) audio.start(source_it->second, cfg.fallback_source, cfg.sources);

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = false;
        if (e.key.keysym.sym == SDLK_F11 || e.key.keysym.sym == SDLK_f) {
          cfg.fullscreen = !cfg.fullscreen;
          SDL_SetWindowFullscreen(win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        if (e.key.keysym.sym == SDLK_SPACE && ringing) {
          space_down = true;
          space_started = Clock::now();
        }
      } else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_SPACE && ringing) {
        if (space_down) {
          auto held = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - space_started).count();
          if (held < cfg.hold_to_stop_seconds) {
            audio.stop();
            ringing = false;
            hold_progress = 0.0;
            if (test_source.empty()) {
              alarm_time = Clock::now() + std::chrono::minutes(cfg.snooze_minutes);
            } else {
              running = false;
            }
          }
        }
        space_down = false;
      }
    }

    if (!ringing && alarm_time && Clock::now() >= *alarm_time) {
      ringing = true;
      source_it = cfg.sources.find(cfg.alarm_source);
      audio.start(source_it->second, cfg.fallback_source, cfg.sources);
    }

    if (ringing && space_down) {
      auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - space_started).count();
      hold_progress = std::clamp(held_ms / (cfg.hold_to_stop_seconds * 1000.0), 0.0, 1.0);
      if (hold_progress >= 1.0) {
        audio.stop();
        ringing = false;
        space_down = false;
        hold_progress = 0.0;
        if (test_source.empty()) alarm_time.reset();
        else running = false;
      }
    }

    int ww = 0, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    draw_clock(renderer, ww, wh, cfg, ringing, hold_progress);
    SDL_Delay(16);
  }

  audio.stop();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(win);
  return 0;
}
