#pragma once

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <thread>

#include "types.h"

class AudioPlayer {
 public:
  AudioPlayer(double volume, int source_timeout_seconds)
      : volume_(volume), source_timeout_seconds_(std::max(1, source_timeout_seconds)) {}
  ~AudioPlayer() { stop(); if (device_) SDL_CloseAudioDevice(device_); }

  bool open();
  void start(Source source, const std::string& fallback, const std::map<std::string, Source>& all);
  void stop();

 private:
  void queue_bytes(const Uint8* data, Uint32 bytes);
  bool play_generated();
  bool play_ffmpeg(const std::string& uri);
  static std::string auto_soundfont();
  bool play_midi(const Source& source);
  static bool source_is_file(const std::string& uri);
  bool play_sdr(const Source& source);

  SDL_AudioDeviceID device_ = 0;
  SDL_AudioSpec have_{};
  std::atomic<bool> stopping_{false};
  std::thread worker_;
  double volume_ = 1.0;
  int source_timeout_seconds_ = 8;
};
