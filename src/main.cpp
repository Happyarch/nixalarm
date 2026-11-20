#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "alarm.h"
#include "audio.h"
#include "clock.h"
#include "config.h"
#include "source_setup.h"
#include "stb_image.h"
#include "types.h"

#ifndef NIXALARM_DATADIR
#define NIXALARM_DATADIR "/usr/local/share/nixalarm"
#endif

// Best-effort: set the window/taskbar icon from the rasterized app icon (SDL has
// no SVG support, so this loads the pre-rendered assets/runtime/icon.png rather
// than data/icons/.../nixalarm.svg). Silently does nothing if it can't be found --
// the window still works, just with the platform's default icon.
static void set_window_icon(SDL_Window* win) {
  std::vector<std::string> candidates;
  candidates.emplace_back(std::string(NIXALARM_DATADIR) + "/icon.png");
  candidates.emplace_back("assets/runtime/icon.png");
  if (char* base = SDL_GetBasePath()) {
    candidates.emplace_back(std::string(base) + "../share/nixalarm/icon.png");
    candidates.emplace_back(std::string(base) + "assets/runtime/icon.png");
    SDL_free(base);
  }
  for (const auto& path : candidates) {
    std::ifstream f(path, std::ios::binary);
    if (!f) continue;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
    int w, h, n;
    unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                               &w, &h, &n, 4);
    if (!px) continue;
    SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(
        px, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
    if (icon) {
      SDL_SetWindowIcon(win, icon);
      SDL_FreeSurface(icon);
    }
    stbi_image_free(px);
    return;
  }
}

static void usage() {
  std::cout <<
      "nixalarm " NIXALARM_VERSION "\n"
      "Usage: nixalarm [OPTIONS] [HH:MM]\n\n"
      "Options:\n"
      "  --config PATH        Read config from PATH instead of XDG config.\n"
      "  --source NAME        Use a configured or built-in source.\n"
      "  --fullscreen         Start fullscreen.\n"
      "  --windowed           Start windowed, overriding config fullscreen.\n"
      "  --list-sources       Print built-in and configured sources.\n"
      "  --test-source NAME   Play a source immediately.\n"
      "  --configure-source NAME\n"
      "                       Interactively choose an SDR gain and print TOML.\n"
      "  --help               Print help.\n"
      "  --version            Print version.\n";
}

static bool has_configure_source_flag(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    // Skip an option's operand so `--config --configure-source` (a legal path
    // spelled with leading dashes) is not mistaken for the wizard action.
    if (arg == "--config" || arg == "--source" || arg == "--test-source") {
      if (i + 1 < argc) ++i;
      continue;
    }
    if (arg == "--configure-source") return true;
  }
  return false;
}

static int configure_usage_error(const std::string& message) {
  std::cerr << "nixalarm: " << message << "\n"
            << "usage: nixalarm [--config PATH] --configure-source NAME\n";
  return 2;
}

// --configure-source is a standalone action: only --config may accompany it,
// in any order. Keeping this in its own parser means the normal CLI's flag
// handling (and its defaults) stays exactly as it was.
static int run_configure_source_mode(int argc, char** argv) {
  fs::path config_path = default_config_path();
  std::string source_name;
  bool have_source = false;
  bool have_config = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config") {
      if (have_config) return configure_usage_error("--config specified more than once");
      if (i + 1 >= argc) return configure_usage_error("--config requires a path");
      std::string value = argv[++i];
      if (value.empty()) return configure_usage_error("--config requires a path");
      config_path = value;
      have_config = true;
    } else if (arg == "--configure-source") {
      if (have_source) {
        return configure_usage_error("--configure-source specified more than once");
      }
      if (i + 1 >= argc) return configure_usage_error("--configure-source requires a source name");
      std::string value = argv[++i];
      if (value.empty()) {
        return configure_usage_error("--configure-source requires a source name");
      }
      source_name = value;
      have_source = true;
    } else {
      return configure_usage_error("option or alarm not allowed with --configure-source: " + arg);
    }
  }
  if (!have_source) return configure_usage_error("--configure-source requires a source name");

  if (!wizard_stdin_stderr_interactive()) {
    std::cerr << "nixalarm: --configure-source requires interactive stdin and stderr "
                 "(stdout may be redirected)\n";
    return 2;
  }

  // An existing but unreadable config must not look like an absent one: the
  // wizard would otherwise describe built-in defaults and print a misleading
  // snippet. A missing file is fine; load_config(..., false) will not seed it.
  std::string read_error;
  ConfigReadStatus read_status = check_config_readable(config_path, &read_error);
  if (read_status == ConfigReadStatus::Error) {
    std::cerr << "nixalarm: cannot read config " << shell_quote(config_path.string()) << ": "
              << read_error << "\n";
    return 1;
  }

  Config cfg = load_config(config_path, false);
  WizardOptions options;
  options.source_name = source_name;
  options.program = (argv[0] && *argv[0]) ? argv[0] : "nixalarm";
  options.config_path = config_path.string();
  options.config_missing = (read_status == ConfigReadStatus::Missing);
  return static_cast<int>(run_source_wizard(cfg, options, std::cin, std::cout, std::cerr));
}

int main(int argc, char** argv) {
  if (has_configure_source_flag(argc, argv)) return run_configure_source_mode(argc, argv);

  fs::path config_path = default_config_path();
  std::string source_override;
  std::string test_source;
  std::string alarm_arg;
  bool do_list = false;
  std::optional<bool> fullscreen_override;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") { usage(); return 0; }
    if (arg == "--version") { std::cout << "nixalarm " NIXALARM_VERSION "\n"; return 0; }
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

  auto source_it = cfg.sources.find(cfg.alarm_source);
  if (source_it == cfg.sources.end()) {
    std::cerr << "nixalarm: unknown source: " << shell_quote(cfg.alarm_source) << "\n";
    return 2;
  }

  // Reject an unusable SDR setup before claiming the clock is armed, and before
  // opening SDL or an audio device. A Generated alarm source never fails into
  // its fallback, so that fallback stays unvalidated; clock-only runs validate
  // nothing at all.
  const bool audio_will_start = !test_source.empty() || !alarm_times.empty();
  PreflightContext preflight;
  preflight.program = (argv[0] && *argv[0]) ? argv[0] : "nixalarm";
  preflight.config_path = config_path.string();
  PreflightResult ready = preflight_audio_sources(
      cfg, cfg.alarm_source, cfg.fallback_source, audio_will_start, std::cerr, preflight);
  if (!ready.ok) return ready.exit_code;

  if (test_source.empty()) {
    if (alarm_times.empty()) {
      std::cerr << "nixalarm: no alarms configured; running as clock only\n";
    } else {
      for (const auto& scheduled : alarm_times) {
        std::cerr << "nixalarm: armed for " << format_local_time(scheduled) << "\n";
      }
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
    std::cerr << "nixalarm: SDL init failed: " << SDL_GetError() << "\n";
    return 1;
  }
  std::atexit(SDL_Quit);

  // SDL_WINDOW_OPENGL is requested unconditionally: the sundial/moondial
  // faces create their own SDL_GLContext on this window (src/dial_gl.cpp) and
  // need the window to have been created with this flag. It's harmless for
  // every other clock face, which keeps using the ordinary SDL_Renderer --
  // SDL's own "opengl" renderer backend (the default accelerated backend on
  // Linux) already requires this flag internally, so this doesn't change
  // their behavior.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
  if (cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  if (cfg.always_on_top) flags |= SDL_WINDOW_ALWAYS_ON_TOP;
  SDL_Window* win = SDL_CreateWindow("nixalarm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cfg.width, cfg.height, flags);
  if (!win) {
    std::cerr << "nixalarm: window create failed: " << SDL_GetError() << "\n";
    return 1;
  }
  set_window_icon(win);
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
    clock_face->render(win, renderer, ww, wh, cfg, {sched.ringing(), sched.hold_progress(), sched.has_pending()});
    SDL_Delay(16);
  }

  audio.stop();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(win);
  return 0;
}
