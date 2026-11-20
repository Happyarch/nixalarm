#include "source_setup.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#include "sdr_gain.h"

namespace {

std::string trim_copy(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Terminals only: diagnostics can carry names straight out of a config file.
std::string escape_visible(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    switch (c) {
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\x%02x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string quote_value(const std::string& s) { return "\"" + escape_visible(s) + "\""; }

// TOML basic strings for the generated snippet. Valid gains are numeric, but
// escaping here keeps the writer safe if validation ever broadens.
std::string toml_quote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out += '"';
  return out;
}

std::string format_number(double value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(10) << std::defaultfloat << value;
  return out.str();
}

const char* modulation_name(Modulation modulation) {
  switch (modulation) {
    case Modulation::Wbfm: return "wbfm";
    case Modulation::Am: return "am";
    case Modulation::Nbfm: break;
  }
  return "nbfm";
}

std::string command_prefix(const std::string& program, const std::string& config_path) {
  std::string command = shell_quote(program.empty() ? "nixalarm" : program);
  if (!config_path.empty()) command += " --config " + shell_quote(config_path);
  return command;
}

bool read_line(std::istream& in, std::string& line) {
  if (!std::getline(in, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return true;
}

enum class YesNo { Yes, No, Eof };

YesNo ask_yes_no(std::istream& in, std::ostream& err, const std::string& question) {
  for (;;) {
    err << question << " [y/N]: " << std::flush;
    std::string line;
    if (!read_line(in, line)) {
      err << "\n";
      return YesNo::Eof;
    }
    std::string answer = lower_copy(trim_copy(line));
    if (answer == "y" || answer == "yes") return YesNo::Yes;
    if (answer.empty() || answer == "n" || answer == "no") return YesNo::No;
    err << "nixalarm: please answer y or n\n";
  }
}

bool ask_for_gain(std::istream& in, std::ostream& err, std::string& gain) {
  for (;;) {
    err << "Gain in dB (a finite number, not \"auto\"): " << std::flush;
    std::string line;
    if (!read_line(in, line)) {
      err << "\n";
      return false;
    }
    std::string value = trim_copy(line);
    std::string reason;
    if (validate_sdr_gain(value, &reason)) {
      gain = value;
      return true;
    }
    err << "nixalarm: invalid gain " << quote_value(value) << ": "
        << (reason.empty() ? "expected a finite numeric value" : reason) << "\n";
    err << "nixalarm: try again, or press Ctrl-D to cancel.\n";
  }
}

}  // namespace

bool is_supported_source_name(const std::string& name, std::string* error) {
  if (name.empty()) {
    if (error) *error = "source names may not be empty";
    return false;
  }
  for (unsigned char c : name) {
    const bool ascii_alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9');
    if (!(ascii_alnum || c == '_' || c == '-')) {
      if (error) {
        *error =
            "source names must use only ASCII letters, digits, '_' and '-' "
            "(the config reader does not unquote section headers)";
      }
      return false;
    }
  }
  return true;
}

bool wizard_stdin_stderr_interactive() {
  return ::isatty(STDIN_FILENO) == 1 && ::isatty(STDERR_FILENO) == 1;
}

ConfigReadStatus check_config_readable(const std::filesystem::path& path, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return ConfigReadStatus::Error;
  };
  if (path.empty()) return fail("config path is empty");

  std::error_code ec;
  std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (ec) {
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) {
      return ConfigReadStatus::Missing;
    }
    return fail(ec.message());
  }
  if (!std::filesystem::exists(status)) return ConfigReadStatus::Missing;
  if (!std::filesystem::is_regular_file(status)) return fail("not a regular file");

  // Read the whole file now. config.cpp silently returns its defaults when the
  // file cannot be opened, which would make the wizard describe a source that
  // was never configured.
  std::ifstream in(path, std::ios::binary);
  if (!in) return fail("could not open for reading");
  char buffer[8192];
  while (in.read(buffer, sizeof buffer)) {
  }
  if (in.bad()) return fail("read failed");

  return ConfigReadStatus::Readable;
}

std::string shell_quote(const std::string& arg) {
  std::string out = "'";
  for (char c : arg) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  out += '\'';
  return out;
}

WizardExit run_source_wizard(const Config& cfg, const WizardOptions& options, std::istream& in,
                             std::ostream& out, std::ostream& err) {
  const std::string& name = options.source_name;
  const std::string list_command =
      command_prefix(options.program, options.config_path) + " --list-sources";

  std::string name_error;
  if (!is_supported_source_name(name, &name_error)) {
    err << "nixalarm: cannot configure source " << quote_value(name) << ": " << name_error << "\n";
    err << "nixalarm: list sources with: " << list_command << "\n";
    return WizardExit::Invalid;
  }

  auto source_it = cfg.sources.find(name);
  if (source_it == cfg.sources.end()) {
    err << "nixalarm: unknown source: " << quote_value(name) << "\n";
    err << "nixalarm: list sources with: " << list_command << "\n";
    return WizardExit::Invalid;
  }
  const Source& source = source_it->second;
  if (source.type != SourceType::Sdr) {
    err << "nixalarm: source " << quote_value(name)
        << " is not an SDR source; only tuner gain can be configured here\n";
    return WizardExit::Invalid;
  }

  err << "nixalarm: configuring SDR source " << quote_value(name) << "\n"
      << "  frequency:       " << format_number(source.frequency_mhz) << " MHz\n"
      << "  modulation:      " << modulation_name(source.modulation) << "\n"
      << "  device index:    " << source.device_index << "\n"
      << "  direct sampling: " << (source.direct_sampling ? "yes" : "no") << "\n"
      << "  current gain:    ";
  bool existing_gain_valid = false;
  if (source.gain.empty()) {
    err << "(unset; manual gain required)\n";
  } else {
    std::string reason;
    existing_gain_valid = validate_sdr_gain(source.gain, &reason);
    if (existing_gain_valid) {
      err << quote_value(source.gain) << "\n";
    } else {
      err << quote_value(source.gain) << " (invalid: "
          << (reason.empty() ? "expected a finite numeric value" : reason) << ")\n";
    }
  }

  err << "\n"
      << "SDR tuner gain is not playback volume. Gain is applied by the radio tuner;\n"
      << "volume is applied later in software. Too much gain can overload a strong\n"
      << "signal and too little can lose a weak one. There is no universal value,\n"
      << "and this wizard does not test your hardware or recommend a gain.\n\n";

  std::string gain;
  bool have_gain = false;
  if (existing_gain_valid) {
    YesNo keep = ask_yes_no(in, err, "Keep current gain " + quote_value(source.gain) + "?");
    if (keep == YesNo::Eof) {
      err << "nixalarm: cancelled; no configuration written.\n";
      return WizardExit::Failure;
    }
    if (keep == YesNo::Yes) {
      gain = source.gain;
      have_gain = true;
    }
  } else if (!source.gain.empty()) {
    err << "nixalarm: the current gain is not valid; enter a replacement.\n\n";
  }

  if (!have_gain && !ask_for_gain(in, err, gain)) {
    err << "nixalarm: cancelled; no configuration written.\n";
    return WizardExit::Failure;
  }

  const bool user_section = cfg.configured_sources.find(name) != cfg.configured_sources.end();
  std::string canonical_gain = canonical_sdr_gain(gain);
  if (canonical_gain.empty()) canonical_gain = gain;  // validation guarantees this is unreachable
  std::string snippet;
  if (user_section) {
    // A second [sources.NAME] header would reset the source in config.cpp's
    // parser, so never emit one for an existing user section.
    snippet = "gain = " + toml_quote(canonical_gain) + "\n";
  } else {
    std::ostringstream section;
    section.imbue(std::locale::classic());
    section << "[sources." << name << "]\n"
            << "type = \"sdr\"\n"
            << "frequency_mhz = " << format_number(source.frequency_mhz) << "\n"
            << "device_index = " << source.device_index << "\n"
            << "modulation = \"" << modulation_name(source.modulation) << "\"\n"
            << "direct_sampling = " << (source.direct_sampling ? "true" : "false") << "\n"
            << "gain = " << toml_quote(canonical_gain) << "\n";
    snippet = section.str();
  }

  err << (user_section ? "Generated gain assignment:\n" : "Generated complete section:\n")
      << snippet << "\n";
  YesNo generate = ask_yes_no(in, err, "Generate this configuration?");
  if (generate != YesNo::Yes) {
    err << "nixalarm: cancelled; no configuration written.\n";
    return WizardExit::Failure;
  }

  out << snippet;
  out.flush();
  if (!out) {
    err << "nixalarm: could not write the configuration to stdout.\n";
    return WizardExit::Failure;
  }

  const std::string config_display =
      options.config_path.empty() ? "your config file" : quote_value(options.config_path);
  err << "nixalarm: no files were changed.\n";
  if (user_section) {
    err << "nixalarm: replace or add the gain assignment inside the existing [sources." << name
        << "] section in " << config_display << ".\n"
        << "nixalarm: do not create a second [sources." << name
        << "] section; a later header resets the values already set for that source.\n";
  } else if (options.config_missing) {
    err << "nixalarm: config file " << config_display
        << " does not exist. Create it (and any missing parent directories), then add this\n"
        << "nixalarm: complete section. The snippet is only the [sources." << name
        << "] section, not a whole config\n"
        << "nixalarm: replacement; other settings keep their defaults.\n"
        << "nixalarm: keep every line; a bare [sources." << name
        << "] header would reset the built-in source's type, frequency, device, modulation\n"
        << "nixalarm: and direct sampling.\n";
  } else {
    err << "nixalarm: add this complete section to " << config_display << "; there is no [sources."
        << name << "] section yet.\n"
        << "nixalarm: keep every line; a bare [sources." << name
        << "] header would reset the built-in source's type, frequency, device, modulation\n"
        << "nixalarm: and direct sampling.\n";
  }
  err << "nixalarm: suggested command (after editing the config):\n  "
      << command_prefix(options.program, options.config_path) << " --test-source "
      << shell_quote(name) << "\n";
  err << "nixalarm: testing and this wizard do not change alarm_source.\n"
      << "nixalarm: to schedule this source explicitly (07:30 is illustrative):\n  "
      << command_prefix(options.program, options.config_path) << " --source "
      << shell_quote(name) << " 07:30\n"
      << "nixalarm: or set alarm_source = " << toml_quote(name)
      << " at the top level of the config, before any section headers.\n";

  return WizardExit::Success;
}

PreflightResult preflight_audio_sources(const Config& cfg, const std::string& primary_name,
                                        const std::string& fallback_name, bool audio_will_start,
                                        std::ostream& err, const PreflightContext& context) {
  PreflightResult result;
  if (!audio_will_start) return result;

  auto primary_it = cfg.sources.find(primary_name);
  if (primary_it == cfg.sources.end()) {
    err << "nixalarm: unknown source: " << quote_value(primary_name) << "\n";
    result.ok = false;
    result.exit_code = 2;
    return result;
  }

  struct Candidate {
    std::string name;
    const Source* source;
  };
  std::vector<Candidate> candidates;
  candidates.push_back({primary_name, &primary_it->second});
  // A Generated primary never fails into the fallback, so leave an unused SDR
  // fallback alone even if its settings are bad.
  if (primary_it->second.type != SourceType::Generated && fallback_name != primary_name) {
    auto fallback_it = cfg.sources.find(fallback_name);
    if (fallback_it != cfg.sources.end()) {
      candidates.push_back({fallback_name, &fallback_it->second});
    }
  }

  bool gains_ok = true;
  for (const Candidate& candidate : candidates) {
    if (candidate.source->type != SourceType::Sdr) continue;
    std::string reason;
    if (!validate_sdr_gain(candidate.source->gain, &reason)) {
      err << "nixalarm: " << (candidate.name == primary_name ? "" : "fallback ")
          << "source " << quote_value(candidate.name) << " has an invalid SDR gain "
          << quote_value(candidate.source->gain) << ": "
          << (reason.empty() ? "expected a finite numeric value" : reason) << "\n";
      err << "nixalarm: fix it with: "
          << command_prefix(context.program, context.config_path) << " --configure-source "
          << shell_quote(candidate.name) << "\n";
      gains_ok = false;
    }
  }
  if (!gains_ok) {
    result.ok = false;
    result.exit_code = 2;
  }
  // rtl_fm availability and hardware failures are deliberately left to
  // play_sdr(), which already reports them and falls back at play time.
  return result;
}
