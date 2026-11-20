#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "types.h"

// Interactive SDR-gain wizard and the startup preflight that runs before SDL.
//
// Both are stream-based on purpose: main() supplies the real terminal, while
// tests drive them with std::istringstream/std::ostringstream. The wizard never
// opens, creates or rewrites a config file; it only prints a TOML snippet.

enum class WizardExit : int {
  Success = 0,
  Failure = 1,  // cancellation, EOF, or an output error
  Invalid = 2,  // bad invocation, unknown/non-SDR source, unsupported name
};

struct WizardOptions {
  std::string source_name;
  std::string program = "nixalarm";
  std::string config_path;
  // Set when check_config_readable() reported Missing, so the instructions can
  // say the file has to be created (and its parent directories with it).
  bool config_missing = false;
};

// Source names have to survive this project's deliberately small TOML reader:
// it takes everything between "sources." and "]" literally and does not unquote
// section headers, so only bare ASCII alnum/_/- keys can be represented.
bool is_supported_source_name(const std::string& name, std::string* error = nullptr);

// Prompts and explanations go to err; only the final TOML goes to out, and only
// after an explicit confirmation.
WizardExit run_source_wizard(const Config& cfg, const WizardOptions& options,
                             std::istream& in, std::ostream& out, std::ostream& err);

// main() requires both stdin and stderr to be terminals (stdout may be a pipe).
bool wizard_stdin_stderr_interactive();

enum class ConfigReadStatus { Missing, Readable, Error };

// Distinguishes a genuinely absent config from one that exists but cannot be
// read. The reader in config.cpp silently falls back to defaults, which would
// make the wizard describe the wrong source, so the wizard checks this first.
ConfigReadStatus check_config_readable(const std::filesystem::path& path,
                                       std::string* error = nullptr);

// Always single-quotes, so paths, source names and program names are safe to
// paste into a shell regardless of what they contain.
std::string shell_quote(const std::string& arg);

struct PreflightContext {
  std::string program = "nixalarm";
  std::string config_path;
};

struct PreflightResult {
  bool ok = true;
  int exit_code = 0;
};

// Validates only the SDR gains that may actually be used at startup. Nothing is
// checked when audio_will_start is false (clock-only runs), and a Generated
// primary skips the fallback entirely because it can never fail into it.
// Missing rtl_fm, missing hardware and other runtime failures stay in the
// existing play_sdr/fallback path; they are not startup errors.
PreflightResult preflight_audio_sources(const Config& cfg,
                                        const std::string& primary_name,
                                        const std::string& fallback_name,
                                        bool audio_will_start, std::ostream& err,
                                        const PreflightContext& context = {});
