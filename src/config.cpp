#include "config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

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
    cfg.clock_face = "seven_segment";
    cfg.background = Color{7, 24, 11, 255};
    cfg.segment_on = Color{108, 255, 87, 255};
    cfg.segment_off = Color{18, 51, 25, 255};
    cfg.glow = true;
    return true;
  }
  if (theme == "sinnoh_green") {
    cfg.theme = theme;
    cfg.clock_face = "seven_segment";
    cfg.background = Color{113, 177, 108, 255};
    cfg.segment_on = Color{44, 91, 51, 255};
    cfg.segment_off = Color{104, 166, 99, 255};
    cfg.glow = false;
    return true;
  }
  if (theme == "nixie") {
    cfg.theme = theme;
    cfg.clock_face = "nixie";
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

fs::path default_config_path() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return fs::path(xdg) / "nixalarm" / "config.toml";
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    return fs::path(home) / ".config" / "nixalarm" / "config.toml";
  }
  return "config.toml";
}

static std::string default_config_text() {
  return
      "# nixalarm config\n"
      "# See nixalarm(5) for the full format.\n"
      "\n"
      "alarm_source = \"generated\"\n"
      "# Desktop/no-argument launches use alarms. Leave empty for clock-only.\n"
      "alarms = [\"07:30\"]\n"
      "snooze_minutes = 10\n"
      "hold_to_stop_seconds = 10\n"
      "volume = 0.9\n"
      "source_start_timeout_seconds = 8\n"
      "fallback_source = \"generated\"\n"
      "use_24_hour = false\n"
      "\n"
      "[window]\n"
      "width = 800\n"
      "height = 360\n"
      "fullscreen = false\n"
      "always_on_top = false\n"
      "flash_hz = 2.0\n"
      "\n"
      "[style]\n"
      "# theme selects both the clock rendering style and its colors.\n"
      "# Built-in: terminal_glow, sinnoh_green, nixie.\n"
      "theme = \"terminal_glow\"\n"
      "show_seconds = false\n"
      "\n"
      "# No-SDR Caldwell/Lenoir NOAA backup.\n"
      "[sources.weather_stream]\n"
      "type = \"internet\"\n"
      "url = \"https://wxradio.org/NC-Linville-WNG538\"\n"
      "\n"
      "# MIDI example. Set soundfont if auto-detection does not find one.\n"
      "# [sources.midi_alarm]\n"
      "# type = \"midi\"\n"
      "# path = \"/home/user/Music/alarm.mid\"\n"
      "# soundfont = \"/usr/share/soundfonts/default.sf2\"\n"
      "\n"
      "# Future RTL-SDR WNG-588 preset, if SDR hardware is available.\n"
      "[sources.black_mountain_weatherband]\n"
      "type = \"sdr_weatherband\"\n"
      "frequency_mhz = 162.500\n"
      "device_index = 0\n"
      "gain = \"auto\"\n";
}

static void seed_default_config_if_missing(const fs::path& path) {
  if (path.empty() || fs::exists(path)) return;
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  if (ec) {
    std::cerr << "nixalarm: could not create config directory " << path.parent_path() << ": " << ec.message() << "\n";
    return;
  }
  std::ofstream out(path);
  if (!out) {
    std::cerr << "nixalarm: could not create default config: " << path << "\n";
    return;
  }
  out << default_config_text();
  std::cerr << "nixalarm: created default config: " << path << "\n";
}

static std::vector<std::string> parse_string_list(const std::string& raw) {
  std::string s = trim(raw);
  std::vector<std::string> out;
  if (s.size() < 2 || s.front() != '[' || s.back() != ']') return out;
  s = s.substr(1, s.size() - 2);
  std::string current;
  char quote = 0;
  bool escaping = false;
  for (char c : s) {
    if (escaping) {
      current.push_back(c);
      escaping = false;
      continue;
    }
    if (quote && c == '\\') {
      escaping = true;
      continue;
    }
    if (quote) {
      if (c == quote) quote = 0;
      else current.push_back(c);
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == ',') {
      std::string item = trim(current);
      if (!item.empty()) out.push_back(item);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  std::string item = trim(current);
  if (!item.empty()) out.push_back(item);
  return out;
}

Config load_config(const fs::path& path) {
  Config cfg;
  cfg.sources = builtin_sources();
  seed_default_config_if_missing(path);
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
        else if (key == "alarms") {
          cfg.alarms = parse_string_list(val);
        }
        else if (key == "snooze_minutes") cfg.snooze_minutes = std::max(1, std::stoi(val));
        else if (key == "hold_to_stop_seconds") cfg.hold_to_stop_seconds = std::max(1, std::stoi(val));
        else if (key == "volume") cfg.volume = std::clamp(std::stod(val), 0.0, 1.0);
        else if (key == "source_start_timeout_seconds") cfg.source_start_timeout_seconds = std::max(1, std::stoi(val));
        else if (key == "fallback_source") cfg.fallback_source = unquote(val);
        else if (key == "use_24_hour") cfg.use_24_hour = parse_bool(val);
      } else if (section == "window") {
        if (key == "width") cfg.width = std::max(240, std::stoi(val));
        else if (key == "height") cfg.height = std::max(160, std::stoi(val));
        else if (key == "fullscreen") cfg.fullscreen = parse_bool(val);
        else if (key == "always_on_top") cfg.always_on_top = parse_bool(val);
        else if (key == "flash_hz") cfg.flash_hz = std::max(0.0, std::stod(val));
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
          else if (type == "midi") s.type = SourceType::Midi;
          else if (type == "sdr_weatherband") s.type = SourceType::SdrWeatherband;
          else s.type = SourceType::Generated;
        } else if (key == "path") s.path = unquote(val);
        else if (key == "url") s.url = unquote(val);
        else if (key == "soundfont") s.soundfont = unquote(val);
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

void list_sources(const Config& cfg) {
  for (const auto& [name, source] : cfg.sources) {
    std::cout << name << "\t";
    if (source.type == SourceType::Generated) std::cout << "generated";
    else if (source.type == SourceType::File) std::cout << "file\t" << source.path;
    else if (source.type == SourceType::Internet) std::cout << "internet\t" << source.url;
    else if (source.type == SourceType::Midi) std::cout << "midi\t" << source.path;
    else if (source.type == SourceType::SdrWeatherband) std::cout << "sdr_weatherband\t" << source.frequency_mhz << " MHz";
    std::cout << "\n";
  }
}
