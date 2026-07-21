#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "alarm.h"
#include "audio.h"
#include "clock.h"
#include "config.h"
#include "types.h"

static void usage() {
  std::cout <<
      "nixalarm 0.1.0\n"
      "Usage: nixalarm [OPTIONS] [HH:MM]\n\n"
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

  std::vector<std::chrono::time_point<Clock>> alarm_times;
  if (test_source.empty()) {
    if (alarm_arg.empty()) {
      alarm_times = parse_alarm_times(cfg.alarms);
      if (!cfg.alarms.empty() && alarm_times.empty()) {
          std::cerr << "nixalarm: config alarms must be HH:MM, HH:MM AM/PM, or empty strings\n";
          return 2;
      }
    } else {
      auto alarm_time = parse_alarm_time(alarm_arg);
      if (!alarm_time) {
        std::cerr << "nixalarm: invalid alarm time, expected HH:MM or HH:MM AM/PM\n";
        return 2;
      }
      alarm_times.push_back(*alarm_time);
    }
  } else {
    cfg.alarm_source = test_source;
  }

  if (test_source.empty()) {
    if (alarm_times.empty()) {
      std::cerr << "nixalarm: no alarms configured; running as clock only\n";
    } else {
      for (const auto& scheduled : alarm_times) {
        std::cerr << "nixalarm: armed for " << format_local_time(scheduled) << "\n";
      }
    }
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

  auto clock_face = make_clock_face(cfg);
  AlarmScheduler sched(std::move(alarm_times), cfg.snooze_minutes, cfg.hold_to_stop_seconds);
  bool is_test = !test_source.empty();
  bool running = true;
  if (is_test) {
    sched.start_ringing();
    audio.start(source_it->second, cfg.fallback_source, cfg.sources);
  }

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
        if (e.key.keysym.sym == SDLK_SPACE) sched.press_hold();
      } else if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_SPACE) {
        if (sched.release_hold() == AlarmScheduler::Outcome::Snoozed) {
          audio.stop();
          if (is_test) {
            running = false;
          } else if (auto snooze = sched.pending_snooze()) {
            std::cerr << "nixalarm: snoozed until " << format_local_time(*snooze) << "\n";
          }
        }
      }
    }

    if (!sched.ringing() && sched.poll()) {
      source_it = cfg.sources.find(cfg.alarm_source);
      std::cerr << "nixalarm: alarm triggered; source=" << cfg.alarm_source << "\n";
      audio.start(source_it->second, cfg.fallback_source, cfg.sources);
    }

    if (sched.update_hold() == AlarmScheduler::Outcome::Dismissed) {
      audio.stop();
      std::cerr << "nixalarm: alarm dismissed\n";
      if (is_test) running = false;
    }

    int ww = 0, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    clock_face->render(renderer, ww, wh, cfg, {sched.ringing(), sched.hold_progress()});
    SDL_Delay(16);
  }

  audio.stop();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(win);
  return 0;
}
