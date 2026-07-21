#pragma once

#include <SDL.h>

#include <chrono>
#include <map>
#include <string>
#include <vector>

using Clock = std::chrono::system_clock;

constexpr int kAudioRate = 48000;
constexpr SDL_AudioFormat kAudioFormat = AUDIO_S16SYS;
constexpr int kAudioChannels = 2;

struct Color {
  Uint8 r = 0;
  Uint8 g = 0;
  Uint8 b = 0;
  Uint8 a = 255;
};

enum class SourceType { Generated, File, Internet, Midi, SdrWeatherband };

struct Source {
  SourceType type = SourceType::Generated;
  std::string path;
  std::string url;
  std::string soundfont;
  double frequency_mhz = 162.45;
  int device_index = 0;
  std::string gain = "auto";
};

struct Config {
  std::string alarm_source = "generated";
  std::vector<std::string> alarms{"07:30"};
  int snooze_minutes = 10;
  int hold_to_stop_seconds = 10;
  double volume = 0.9;
  int source_start_timeout_seconds = 8;
  std::string fallback_source = "generated";
  bool use_24_hour = false;
  int width = 800;
  int height = 360;
  bool fullscreen = false;
  bool always_on_top = false;
  double flash_hz = 2.0;
  std::string theme = "terminal_glow";
  Color background{7, 24, 11, 255};
  Color segment_on{108, 255, 87, 255};
  Color segment_off{18, 51, 25, 255};
  bool glow = true;
  bool show_seconds = false;
  std::map<std::string, Source> sources;
};
