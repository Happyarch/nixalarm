// Standalone assert-based tests for the SDR-gain wizard and startup preflight
// (src/source_setup.cpp). Everything is driven through string streams, so no
// terminal, SDL, audio device or SDR hardware is needed.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

#include "source_setup.h"

namespace {

int g_failures = 0;

void expect_true(bool cond, const char* what) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s\n", what);
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void expect_eq(const std::string& actual, const std::string& expected, const char* what) {
  if (actual != expected) {
    std::fprintf(stderr, "FAIL %s: expected [%s], got [%s]\n", what, expected.c_str(),
                 actual.c_str());
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void expect_contains(const std::string& haystack, const std::string& needle, const char* what) {
  if (haystack.find(needle) == std::string::npos) {
    std::fprintf(stderr, "FAIL %s: [%s] does not contain [%s]\n", what, haystack.c_str(),
                 needle.c_str());
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void expect_not_contains(const std::string& haystack, const std::string& needle,
                         const char* what) {
  if (haystack.find(needle) != std::string::npos) {
    std::fprintf(stderr, "FAIL %s: [%s] unexpectedly contains [%s]\n", what, haystack.c_str(),
                 needle.c_str());
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

Source make_sdr(const std::string& gain) {
  Source s;
  s.type = SourceType::Sdr;
  s.frequency_mhz = 162.45;
  s.device_index = 1;
  s.modulation = Modulation::Wbfm;
  s.direct_sampling = true;
  s.gain = gain;
  return s;
}

Config make_config() {
  Config cfg;
  cfg.sources["generated"] = Source{};
  cfg.sources["weatherband_162_450"] = make_sdr("");
  Source file_source;
  file_source.type = SourceType::File;
  cfg.sources["local_file"] = file_source;
  cfg.sources["sdr_good"] = make_sdr("28.0");
  cfg.sources["sdr_bad"] = make_sdr("auto");
  return cfg;
}

WizardOptions options(const std::string& name) {
  WizardOptions o;
  o.source_name = name;
  o.program = "nixalarm";
  o.config_path = "/tmp/conf with space.toml";
  return o;
}

struct WizardRun {
  WizardExit exit = WizardExit::Invalid;
  std::string out;
  std::string err;
};

WizardRun run_wizard(const Config& cfg, const WizardOptions& opts, const std::string& input) {
  std::istringstream in(input);
  std::ostringstream out;
  std::ostringstream err;
  WizardRun result;
  result.exit = run_source_wizard(cfg, opts, in, out, err);
  result.out = out.str();
  result.err = err.str();
  return result;
}

void test_source_name_support() {
  expect_true(is_supported_source_name("weatherband_162_400"), "built-in weather name is supported");
  expect_true(is_supported_source_name("my-source_2"), "alnum, underscore and hyphen are supported");
  expect_true(is_supported_source_name("-radio"), "leading hyphen is supported");
  std::string error;
  expect_true(!is_supported_source_name("bad name", &error), "space is rejected");
  expect_true(!error.empty(), "rejected name gives a reason");
  expect_true(!is_supported_source_name("x.y"), "dot is rejected");
  expect_true(!is_supported_source_name("x/y"), "slash is rejected");
  expect_true(!is_supported_source_name(""), "empty name is rejected");
  expect_true(!is_supported_source_name("m\xc3\xbcnchen"), "non-ASCII name is rejected");
}

void test_builtin_wizard_emits_complete_section() {
  Config cfg = make_config();
  WizardRun r = run_wizard(cfg, options("weatherband_162_450"), "40.2\ny\n");
  expect_true(r.exit == WizardExit::Success, "built-in wizard succeeds");
  expect_contains(r.out, "[sources.weatherband_162_450]", "built-in output has a section header");
  expect_contains(r.out, "type = \"sdr\"", "built-in output preserves type");
  expect_contains(r.out, "frequency_mhz = 162.45", "built-in output preserves frequency");
  expect_contains(r.out, "device_index = 1", "built-in output preserves device");
  expect_contains(r.out, "modulation = \"wbfm\"", "built-in output preserves modulation");
  expect_contains(r.out, "direct_sampling = true", "built-in output preserves direct sampling");
  expect_contains(r.out, "gain = \"40.2\"", "built-in output has the chosen gain");
  expect_not_contains(r.out, "nixalarm:", "stdout contains only TOML");
  expect_contains(r.err, "not playback volume", "explanation separates gain from volume");
  expect_contains(r.err, "does not test your hardware",
                  "explanation does not imply hardware testing");
  expect_contains(r.err, "(unset; manual gain required)", "unset gain is explained as required");
  expect_not_contains(r.err, "rtl_fm default", "unset gain is not called a backend default");
  expect_contains(r.err, "--test-source 'weatherband_162_450'", "suggested command quotes source");
  expect_contains(r.err, "do not change alarm_source", "wizard does not select the alarm source");
  expect_contains(r.err, "--source 'weatherband_162_450' 07:30",
                  "wizard suggests an explicit scheduled source");
  expect_contains(r.err, "before any section headers", "persistent selection stays top-level");
  expect_contains(r.err, "--config '/tmp/conf with space.toml'",
                  "suggested command quotes config path");
  expect_contains(r.err, "complete section", "built-in instructions ask for the complete section");
}

void test_missing_config_instructions() {
  Config cfg = make_config();
  WizardOptions opts = options("weatherband_162_450");
  opts.config_path = "/tmp/new/dir/config.toml";
  opts.config_missing = true;
  WizardRun r = run_wizard(cfg, opts, "40.2\ny\n");
  expect_true(r.exit == WizardExit::Success, "missing-config wizard succeeds");
  expect_contains(r.out, "[sources.weatherband_162_450]", "missing-config wizard emits the section");
  expect_contains(r.err, "does not exist", "missing config is called out");
  expect_contains(r.err, "parent directories", "creating parent directories is explained");
  expect_contains(r.err, "not a whole config", "the snippet is not a whole config replacement");
}

void test_user_section_emits_gain_only() {
  Config cfg = make_config();
  cfg.sources["my_sdr"] = make_sdr("");
  cfg.configured_sources.insert("my_sdr");
  WizardRun r = run_wizard(cfg, options("my_sdr"), "40.2\ny\n");
  expect_true(r.exit == WizardExit::Success, "user-section wizard succeeds");
  expect_eq(r.out, "gain = \"40.2\"\n", "user section gets the gain assignment only");
  expect_contains(r.err, "--source 'my_sdr' 07:30", "user source selection is explained");
  expect_contains(r.err, "inside the existing [sources.my_sdr] section",
                  "user instructions name the exact section");
  expect_contains(r.err, "do not create a second", "user instructions forbid a second section");
}

void test_valid_current_gain_can_be_retained() {
  Config cfg = make_config();
  cfg.sources["sdr_good"].gain = "12.5";
  WizardRun r = run_wizard(cfg, options("sdr_good"), "y\ny\n");
  expect_true(r.exit == WizardExit::Success, "retaining a valid gain succeeds");
  expect_contains(r.out, "gain = \"12.5\"", "retained gain is emitted");
  expect_contains(r.err, "Keep current gain \"12.5\"?", "retention needs explicit confirmation");
}

void test_invalid_gain_is_retried() {
  Config cfg = make_config();
  WizardRun r = run_wizard(cfg, options("weatherband_162_450"), "auto\nloud\n40.2\ny\n");
  expect_true(r.exit == WizardExit::Success, "invalid gains are retried");
  expect_contains(r.out, "gain = \"40.2\"", "a later valid gain is emitted");
  expect_contains(r.err, "invalid gain", "invalid attempts are explained");
}

void test_eof_and_decline_emit_nothing() {
  Config cfg = make_config();
  WizardRun eof = run_wizard(cfg, options("weatherband_162_450"), "");
  expect_true(eof.exit == WizardExit::Failure, "EOF cancels");
  expect_eq(eof.out, "", "EOF writes no TOML");
  expect_contains(eof.err, "cancel", "EOF is reported as cancellation");

  WizardRun no = run_wizard(cfg, options("weatherband_162_450"), "40.2\nn\n");
  expect_true(no.exit == WizardExit::Failure, "declining confirmation cancels");
  expect_eq(no.out, "", "declined confirmation writes no TOML");
  expect_contains(no.err, "no configuration", "decline is reported");
}

void test_invalid_requests() {
  Config cfg = make_config();
  WizardRun bad_name = run_wizard(cfg, options("bad name"), "40.2\ny\n");
  expect_true(bad_name.exit == WizardExit::Invalid, "unsupported source name is invalid");
  expect_eq(bad_name.out, "", "invalid name writes no TOML");

  WizardRun unknown = run_wizard(cfg, options("nope"), "40.2\ny\n");
  expect_true(unknown.exit == WizardExit::Invalid, "unknown source is invalid");
  expect_eq(unknown.out, "", "unknown source writes no TOML");

  WizardRun non_sdr = run_wizard(cfg, options("generated"), "40.2\ny\n");
  expect_true(non_sdr.exit == WizardExit::Invalid, "non-SDR source is invalid");
  expect_eq(non_sdr.out, "", "non-SDR source writes no TOML");
}

void test_config_read_status() {
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("nixalarm-setup-test-" + std::to_string(static_cast<long long>(::getpid())));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  std::string error;
  expect_true(check_config_readable(dir / "missing.toml", &error) == ConfigReadStatus::Missing,
              "missing config is reported as missing");
  expect_true(check_config_readable(dir, &error) == ConfigReadStatus::Error,
              "a directory is a config read error, not a missing file");

  std::ofstream(dir / "config.toml") << "alarm_source = \"generated\"\n";
  expect_true(check_config_readable(dir / "config.toml", &error) == ConfigReadStatus::Readable,
              "a readable config is reported as readable");

  std::filesystem::remove_all(dir, ec);
}

void test_shell_quote() {
  expect_eq(shell_quote("nixalarm"), "'nixalarm'", "plain argument is quoted");
  expect_eq(shell_quote("a b"), "'a b'", "space stays inside quotes");
  expect_eq(shell_quote("a'b"), "'a'\\''b'", "embedded single quote is escaped");
  expect_eq(shell_quote(""), "''", "empty argument is still one argument");
}

void test_preflight_skips_unused_sources() {
  Config cfg = make_config();
  std::ostringstream err;
  PreflightContext ctx;
  ctx.program = "nixalarm";
  ctx.config_path = "/tmp/conf.toml";

  PreflightResult clock_only =
      preflight_audio_sources(cfg, "sdr_bad", cfg.fallback_source, false, err, ctx);
  expect_true(clock_only.ok, "clock-only run ignores an invalid unused SDR source");
  expect_eq(err.str(), "", "clock-only run prints nothing");

  PreflightResult generated =
      preflight_audio_sources(cfg, "generated", "sdr_bad", true, err, ctx);
  expect_true(generated.ok, "generated primary skips its unused SDR fallback");
}

void test_preflight_valid_and_invalid_settings() {
  Config cfg = make_config();
  PreflightContext ctx;
  ctx.program = "nixalarm";
  ctx.config_path = "/tmp/conf.toml";

  std::ostringstream ok_err;
  PreflightResult good =
      preflight_audio_sources(cfg, "sdr_good", cfg.fallback_source, true, ok_err, ctx);
  expect_true(good.ok, "valid SDR primary passes preflight");
  expect_not_contains(ok_err.str(), "rtl_fm", "valid gain preflight does not probe the receiver");

  std::ostringstream bad_err;
  PreflightResult bad =
      preflight_audio_sources(cfg, "sdr_bad", cfg.fallback_source, true, bad_err, ctx);
  expect_true(!bad.ok, "invalid SDR gain fails preflight");
  expect_true(bad.exit_code == 2, "invalid SDR gain is a configuration error");
  expect_contains(bad_err.str(), "invalid SDR gain", "invalid gain is named");
  expect_contains(bad_err.str(), "--configure-source 'sdr_bad'", "diagnostic names the wizard");
  expect_contains(bad_err.str(), "--config '/tmp/conf.toml'",
                  "diagnostic quotes the config path");
  expect_not_contains(bad_err.str(), "rtl_fm", "gain failure does not blame the receiver binary");
}

void test_preflight_checks_sdr_fallback_gain() {
  Config cfg = make_config();
  cfg.sources["sdr_fallback"] = make_sdr("auto");
  PreflightContext ctx;
  ctx.program = "nixalarm";
  ctx.config_path = "/tmp/conf.toml";

  std::ostringstream err;
  PreflightResult bad = preflight_audio_sources(cfg, "local_file", "sdr_fallback", true, err, ctx);
  expect_true(!bad.ok, "a failing non-SDR primary preflights its SDR fallback");
  expect_contains(err.str(), "fallback source \"sdr_fallback\"", "fallback is identified");

  cfg.sources["sdr_fallback"].gain = "20.0";
  std::ostringstream good_err;
  PreflightResult good =
      preflight_audio_sources(cfg, "local_file", "sdr_fallback", true, good_err, ctx);
  expect_true(good.ok, "a valid SDR fallback passes");
}

void test_preflight_unknown_primary() {
  Config cfg = make_config();
  std::ostringstream err;
  PreflightResult result = preflight_audio_sources(cfg, "nope", cfg.fallback_source, true, err, {});
  expect_true(!result.ok, "unknown primary fails preflight");
  expect_true(result.exit_code == 2, "unknown primary is an invocation error");
  expect_contains(err.str(), "unknown source", "unknown primary is named");
}

}  // namespace

int main() {
  test_source_name_support();
  test_builtin_wizard_emits_complete_section();
  test_missing_config_instructions();
  test_user_section_emits_gain_only();
  test_valid_current_gain_can_be_retained();
  test_invalid_gain_is_retried();
  test_eof_and_decline_emit_nothing();
  test_invalid_requests();
  test_config_read_status();
  test_shell_quote();
  test_preflight_skips_unused_sources();
  test_preflight_valid_and_invalid_settings();
  test_preflight_checks_sdr_fallback_gain();
  test_preflight_unknown_primary();

  if (g_failures) {
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all source_setup tests passed\n");
  return 0;
}
