// Standalone assert-based tests for manual SDR gain validation and the config
// loader extensions that support it. Matches this project's minimal-dependency
// test style (see tests/test_dial_time.cpp).

#include <cstdio>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>

#include <unistd.h>

#include "config.h"
#include "sdr_gain.h"
#include "types.h"

namespace fs = std::filesystem;

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
    std::fprintf(stderr, "FAIL %s: expected \"%s\", got \"%s\"\n", what, expected.c_str(),
                 actual.c_str());
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void expect_valid(const std::string& value, const char* what) {
  std::string error;
  bool ok = validate_sdr_gain(value, &error);
  if (!ok) {
    std::fprintf(stderr, "FAIL %s: \"%s\" rejected: %s\n", what, value.c_str(), error.c_str());
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void expect_invalid(const std::string& value, const char* what) {
  std::string error;
  bool ok = validate_sdr_gain(value, &error);
  if (ok) {
    std::fprintf(stderr, "FAIL %s: \"%s\" was accepted\n", what, value.c_str());
    ++g_failures;
  } else if (error.empty()) {
    std::fprintf(stderr, "FAIL %s: rejected without an error message\n", what);
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void expect_canonical(const std::string& value, const std::string& expected, const char* what) {
  std::string actual = canonical_sdr_gain(value);
  if (actual != expected) {
    std::fprintf(stderr, "FAIL %s: expected \"%s\", got \"%s\"\n", what, expected.c_str(),
                 actual.c_str());
    ++g_failures;
  } else {
    std::printf("ok   %s\n", what);
  }
}

void test_validation_accepts_manual_decimal_gain() {
  expect_valid("20", "integer dB accepted");
  expect_valid("20.0", "one decimal accepted");
  expect_valid("20.55", "extra precision accepted and resolved by rtl_fm");
  expect_valid("+20", "leading plus accepted");
  expect_valid("+20.5", "leading plus with decimal accepted");
  expect_valid("-9.9", "negative manual gain accepted");
  expect_valid("-10.1", "gain just below rtl_fm's sentinel accepted");
  expect_valid(".5", "leading-dot decimal accepted");
  expect_valid("5.", "trailing-dot decimal accepted");
  expect_valid("2e1", "positive exponent accepted");
  expect_valid("2E+1", "signed exponent accepted");
  expect_valid("0", "zero gain accepted");
  expect_valid("-0.0", "negative zero accepted");
  expect_valid("49.6", "common RTL-SDR gain accepted");
  expect_valid("100.0", "100 dB accepted");
  expect_valid("-100.0", "-100 dB accepted");
  expect_valid("100.1", "gain above 100 dB is not clipped by a tuner range");
  expect_valid("-100.1", "gain below -100 dB is not clipped by a tuner range");
  expect_valid("100.09", "value resolving to a whole tenth accepted");
  expect_valid("214748364.7", "largest signed int tenths accepted");
  expect_valid("214748364.79", "value truncating to largest int tenths accepted");
  expect_valid("-214748364.8", "smallest signed int tenths accepted");
  expect_valid(" 20.0 ", "surrounding ASCII whitespace ignored");
}

void test_validation_rejects_unset_auto_and_junk() {
  expect_invalid("", "empty gain rejected");
  expect_invalid("   ", "whitespace-only gain rejected");
  expect_invalid("auto", "auto rejected");
  expect_invalid("AUTO", "uppercase auto rejected");
  expect_invalid(" Auto ", "padded auto rejected");
  expect_invalid("automatic", "automatic rejected");
  expect_invalid("20 dB", "unit suffix rejected");
  expect_invalid("20.0x", "trailing character rejected");
  expect_invalid("0x10", "hex rejected");
  expect_invalid("0x1p3", "hex float rejected");
  expect_invalid("1,5", "locale comma rejected");
  expect_invalid("1_000", "digit separator rejected");
  expect_invalid("--1", "double sign rejected");
  expect_invalid("++1", "double plus rejected");
  expect_invalid("++20", "second plus rejected");
  expect_invalid("+-20", "plus followed by minus rejected");
  expect_invalid("+-10.1", "plus then negative decimal rejected");
  expect_invalid("-+20", "minus followed by plus rejected");
  expect_invalid("20..0", "malformed decimal rejected");
}

void test_validation_rejects_nonfinite_and_out_of_range() {
  expect_invalid("nan", "nan rejected");
  expect_invalid("NaN", "NaN rejected");
  expect_invalid("inf", "inf rejected");
  expect_invalid("-inf", "negative inf rejected");
  expect_invalid("infinity", "infinity rejected");
  expect_invalid("1e999", "overflowing exponent rejected");
  expect_invalid("-1e999", "negative overflowing exponent rejected");
  expect_invalid("1e-999", "underflowing exponent rejected");
  expect_invalid("214748364.8", "above largest signed int tenth rejected");
  expect_invalid("-214748364.9", "below smallest signed int tenth rejected");
  expect_invalid("1e10", "far above signed int tenth range rejected");
  expect_invalid("-1e10", "far below signed int tenth range rejected");
  expect_invalid("2147483647", "unrepresentable tuner tenths rejected");
  expect_invalid("999999999999999999999999999999999999", "huge value rejected");
}

void test_validation_rejects_auto_gain_sentinel() {
  // rtl_fm computes (int)(atof(value) * 10) and reserves -100 tenths for
  // automatic gain. Every spelling that truncates onto -100 must be rejected.
  expect_invalid("-10", "exact sentinel rejected");
  expect_invalid("-10.0", "sentinel with decimal rejected");
  expect_invalid("-10.001", "value truncating to sentinel rejected");
  expect_invalid("-10.05", "sentinel bucket rejected");
  expect_invalid("-10.09", "sentinel bucket edge rejected");
  expect_invalid("-10.099999", "value just above sentinel bucket rejected");
  expect_valid("-9.999999999999999", "value just above sentinel bucket accepted");
  expect_valid("-10.1", "value just below sentinel bucket accepted");
}

void test_canonical_form_is_locale_independent() {
  expect_canonical("20", "20.0", "integer canonicalized");
  expect_canonical("+20.5", "20.5", "leading plus removed");
  expect_canonical("2e1", "20.0", "exponent resolved");
  expect_canonical("20.55", "20.5", "precision reduced to backend tenths");
  expect_canonical("20.59", "20.5", "truncation matches rtl_fm");
  expect_canonical("0.09", "0.0", "sub-tenth value canonicalized");
  expect_canonical("-0.0", "0.0", "negative zero canonicalized");
  expect_canonical("100.09", "100.0", "whole tenth canonicalized");
  expect_canonical("49.6", "49.6", "common gain unchanged");
  expect_canonical("214748364.7", "214748364.7", "largest int tenth canonicalized");
  expect_canonical("214748364.79", "214748364.7", "truncation at the upper bound");
  expect_canonical("-214748364.8", "-214748364.8", "smallest int tenth canonicalized");
  expect_canonical("214748364.8", "", "above the int tenth range has no canonical form");
  expect_canonical("-214748364.9", "", "below the int tenth range has no canonical form");
  expect_canonical("", "", "invalid value has no canonical form");
  expect_canonical("auto", "", "auto has no canonical form");
  expect_canonical("-10.0", "", "sentinel has no canonical form");
}

std::string decimal_gain_for_tenths(long long tenths) {
  long long magnitude = tenths < 0 ? -tenths : tenths;
  return (tenths < 0 ? "-" : "") + std::to_string(magnitude / 10) + "." +
         std::to_string(magnitude % 10);
}

// rtl_fm does (int)(atof(value) * 10). Every canonical string must reproduce
// exactly the tenths validation accepted, with no locale comma, no truncation
// slip and without landing on the automatic-gain sentinel.
bool canonical_survives_backend_math(long long tenths) {
  if (tenths == -100) return true;  // rejected as rtl_fm's automatic-gain sentinel
  std::string input = decimal_gain_for_tenths(tenths);
  std::string error;
  if (!validate_sdr_gain(input, &error)) {
    std::fprintf(stderr, "FAIL backend tenths %lld: input \"%s\" rejected: %s\n", tenths,
                 input.c_str(), error.c_str());
    return false;
  }

  std::string canonical = canonical_sdr_gain(input);
  char* end = nullptr;
  double db = std::strtod(canonical.c_str(), &end);
  double scaled = db * 10.0;
  double truncated = std::trunc(scaled);
  const double min_tenths = static_cast<double>(std::numeric_limits<int>::min());
  const double max_tenths_exclusive = static_cast<double>(std::numeric_limits<int>::max()) + 1.0;
  if (canonical.empty() || *end != '\0' || !std::isfinite(scaled) || truncated < min_tenths ||
      truncated >= max_tenths_exclusive) {
    std::fprintf(stderr, "FAIL backend tenths %lld: canonical \"%s\" is not representable\n", tenths,
                 canonical.c_str());
    return false;
  }
  long long got = static_cast<long long>(truncated);
  if (got != tenths) {
    std::fprintf(stderr, "FAIL backend tenths %lld: input \"%s\" canonical \"%s\" -> %lld\n",
                 tenths, input.c_str(), canonical.c_str(), got);
    return false;
  }
  return true;
}

void test_canonical_survives_backend_math() {
  bool all = true;

  // The exhaustive small range is cheap and covers the sentinel's neighbours.
  for (int tenths = -1000; tenths <= 1000; ++tenths) {
    if (!canonical_survives_backend_math(tenths)) {
      all = false;
      break;
    }
  }

  // Then pin the signed int boundaries and a deterministic random sample,
  // rather than iterating the whole int range.
  if (all) {
    const long long boundaries[] = {
        static_cast<long long>(std::numeric_limits<int>::min()),
        static_cast<long long>(std::numeric_limits<int>::min()) + 1,
        static_cast<long long>(std::numeric_limits<int>::min()) + 2,
        static_cast<long long>(std::numeric_limits<int>::max()) - 2,
        static_cast<long long>(std::numeric_limits<int>::max()) - 1,
        static_cast<long long>(std::numeric_limits<int>::max()),
        -1001, -101, -100, -99, -1, 0, 1, 999, 1001,
        -1000000000, 1000000000, 2147483646, -2147483647,
    };
    for (long long tenths : boundaries) {
      if (!canonical_survives_backend_math(tenths)) {
        all = false;
        break;
      }
    }
  }

  if (all) {
    std::mt19937 rng(20261119u);
    std::uniform_int_distribution<int> dist(std::numeric_limits<int>::min(),
                                            std::numeric_limits<int>::max());
    for (int i = 0; i < 50000; ++i) {
      if (!canonical_survives_backend_math(dist(rng))) {
        all = false;
        break;
      }
    }
  }

  expect_true(all, "canonical gains reproduce rtl_fm's tenths across the int range");
}

void test_validation_never_throws_and_handles_null_error() {
  bool threw = false;
  try {
    expect_true(!validate_sdr_gain("nope", nullptr), "invalid gain with null error returns false");
    expect_true(validate_sdr_gain("20.0", nullptr), "valid gain with null error returns true");
    std::string error = "stale";
    expect_true(validate_sdr_gain("20.0", &error), "valid gain returns true with error buffer");
    expect_true(error.empty(), "successful validation clears the error buffer");
  } catch (...) {
    threw = true;
  }
  expect_true(!threw, "validation did not throw");

  std::string error;
  validate_sdr_gain("", &error);
  expect_true(error.find("for example") == std::string::npos,
              "unset-gain error does not suggest a default");
  validate_sdr_gain("not-a-number", &error);
  expect_true(error.find("for example") == std::string::npos,
              "malformed-gain error does not suggest a default");
}

std::string read_text(const fs::path& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const fs::path& path, const std::string& text) {
  std::ofstream out(path);
  out << text;
}

int count_occurrences(const std::string& haystack, const std::string& needle) {
  int count = 0;
  std::size_t at = 0;
  while ((at = haystack.find(needle, at)) != std::string::npos) {
    ++count;
    at += needle.size();
  }
  return count;
}

void test_defaults_and_read_only_load() {
  Source source;
  expect_true(source.gain.empty(), "Source gain defaults to unset");
  expect_true(Config{}.configured_sources.empty(), "configured_sources defaults empty");

  fs::path dir = fs::temp_directory_path() / ("nixalarm-sdr-gain-test-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);

  fs::path missing = dir / "missing.toml";
  Config cfg = load_config(missing, false);
  expect_true(!fs::exists(missing), "read-only load does not seed a missing config");
  expect_true(cfg.sources.count("generated") == 1, "read-only load still provides built-ins");
  expect_true(cfg.sources.count("weatherband_162_400") == 1, "weather presets are built in");
  expect_true(cfg.sources.at("weatherband_162_400").gain.empty(), "built-in SDR presets have unset gain");
  expect_true(cfg.configured_sources.empty(), "built-in presets are not user-configured");

  // A directory is refused; readable FIFOs and proc-fd streams stay valid.
  Config dir_cfg = load_config(dir, false);
  expect_true(dir_cfg.sources.count("generated") == 1, "directory config falls back to built-ins");
  expect_true(dir_cfg.configured_sources.empty(), "directory config records no user sections");

  fs::path seeded = dir / "seeded.toml";
  Config seeded_cfg = load_config(seeded, true);
  expect_true(fs::exists(seeded), "default load seeds a missing config");
  expect_true(seeded_cfg.sources.count("generated") == 1, "seeded config still has built-ins");
  std::string seeded_text = read_text(seeded);
  expect_true(count_occurrences(seeded_text, "gain = 20.0") >= 3,
              "generated SDR examples all carry a manual gain");
  expect_true(seeded_text.find("gain = \"auto\"") == std::string::npos,
              "generated examples no longer use auto gain");
  expect_true(seeded_text.find("recommendations") != std::string::npos,
              "generated gains are labelled illustrative, not recommendations");

  fs::remove_all(dir);
}

void test_config_parsing_and_metadata() {
  fs::path dir = fs::temp_directory_path() / ("nixalarm-sdr-gain-config-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  fs::path path = dir / "config.toml";

  write_text(path,
             "alarm_source = \"band\" # trailing comment\n"
             "alarms = ['07:30', \"08:00\"] # single and double quoted list\n"
             "\n"
             "[sources.band]\n"
             "type = \"sdr\" # trailing comment\n"
             "modulation = \"nbfm\"\n"
             "frequency_mhz = 162.400\n"
             "device_index = 1\n"
             "gain = \"20.0\" # quoted gain with trailing comment\n"
             "\n"
             "[sources.unquoted]\n"
             "type = \"sdr\"\n"
             "gain = 20.5 # unquoted gain with trailing comment\n"
             "\n"
             "[sources.single_quoted]\n"
             "type = 'sdr'\n"
             "gain = '20.0' # single-quoted gain with trailing comment\n"
             "\n"
             "[sources.legacy_auto]\n"
             "type = \"sdr\"\n"
             "gain = \"auto\" # legacy value must stay visible\n"
             "\n"
             "[sources.bad_gain]\n"
             "type = \"sdr\"\n"
             "gain = \"20.0 dB\" # invalid must not be silently repaired\n"
             "\n"
             "[sources.empty_gain]\n"
             "type = \"sdr\"\n"
             "gain = \"\"\n"
             "\n"
             "[sources.weatherband_162_400]\n"
             "type = \"sdr\"\n"
             "gain = 30.0\n"
             "\n"
             "[sources.stream]\n"
             "type = \"internet\"\n"
             "url = \"https://example.org/stream#fragment\" # comment\n"
             "\n"
             "[sources.apostrophe_path]\n"
             "type = \"file\"\n"
             "path = /home/o'brien/alarm.flac # unquoted apostrophe in path\n"
             "soundfont = /home/o'brien/fonts/alarm.sf2 # unquoted apostrophe in soundfont\n"
             "\n"
             "[sources.apostrophe_url]\n"
             "type = \"internet\"\n"
             "url = https://example.org/o'brien/stream # unquoted apostrophe in URL\n"
             "\n"
             "[sources.quoted_apostrophe_url]\n"
             "type = \"internet\"\n"
             "url = \"https://example.org/o'brien/stream#fragment\" # quoted hash preserved\n");

  std::string before = read_text(path);
  Config cfg = load_config(path, false);
  expect_eq(read_text(path), before, "read-only load does not rewrite the config");

  expect_eq(cfg.alarm_source, "band", "alarm_source parsed");
  expect_true(cfg.alarms.size() == 2 && cfg.alarms[0] == "07:30" && cfg.alarms[1] == "08:00",
              "single- and double-quoted list parses with a trailing comment");
  expect_true(cfg.sources.at("band").type == SourceType::Sdr, "sdr type parsed");
  expect_true(cfg.sources.at("band").device_index == 1, "device index parsed");
  expect_eq(cfg.sources.at("band").gain, "20.0", "quoted gain with trailing comment parsed raw");
  expect_eq(cfg.sources.at("unquoted").gain, "20.5", "unquoted gain with trailing comment parsed");
  expect_eq(cfg.sources.at("single_quoted").gain, "20.0",
            "single-quoted gain with trailing comment parsed");
  expect_eq(cfg.sources.at("legacy_auto").gain, "auto", "auto stays raw and distinguishable");
  expect_eq(cfg.sources.at("bad_gain").gain, "20.0 dB", "invalid gain stays raw");
  expect_eq(cfg.sources.at("empty_gain").gain, "", "explicit empty gain stays unset");
  expect_eq(cfg.sources.at("stream").url, "https://example.org/stream#fragment",
            "hash inside a quoted URL is preserved");
  expect_eq(cfg.sources.at("apostrophe_path").path, "/home/o'brien/alarm.flac",
            "apostrophe in an unquoted path does not swallow the comment");
  expect_eq(cfg.sources.at("apostrophe_path").soundfont, "/home/o'brien/fonts/alarm.sf2",
            "apostrophe in an unquoted soundfont does not swallow the comment");
  expect_eq(cfg.sources.at("apostrophe_url").url, "https://example.org/o'brien/stream",
            "apostrophe in an unquoted URL does not swallow the comment");
  expect_eq(cfg.sources.at("quoted_apostrophe_url").url,
            "https://example.org/o'brien/stream#fragment",
            "apostrophe and hash inside a quoted URL are preserved");

  expect_true(validate_sdr_gain(cfg.sources.at("band").gain), "quoted gain validates");
  expect_true(validate_sdr_gain(cfg.sources.at("unquoted").gain), "unquoted gain validates");
  expect_true(validate_sdr_gain(cfg.sources.at("single_quoted").gain),
              "single-quoted gain validates");
  expect_true(!validate_sdr_gain(cfg.sources.at("legacy_auto").gain), "auto does not validate");
  expect_true(!validate_sdr_gain(cfg.sources.at("bad_gain").gain), "junk gain does not validate");
  expect_true(!validate_sdr_gain(cfg.sources.at("empty_gain").gain), "empty gain does not validate");

  expect_true(cfg.configured_sources.count("band") == 1, "user section recorded");
  expect_true(cfg.configured_sources.count("unquoted") == 1, "second user section recorded");
  expect_true(cfg.configured_sources.count("legacy_auto") == 1, "legacy section recorded");
  expect_true(cfg.configured_sources.count("bad_gain") == 1, "invalid section still recorded");
  expect_true(cfg.configured_sources.count("weatherband_162_400") == 1,
              "overridden built-in recorded as user-configured");
  expect_true(cfg.configured_sources.count("generated") == 0,
              "untouched built-in not recorded as user-configured");
  expect_eq(cfg.sources.at("weatherband_162_400").gain, "30.0", "built-in preset overridden");

  fs::remove_all(dir);
}

void test_stream_config_from_proc_fd() {
  int fds[2];
  if (::pipe(fds) != 0) {
    expect_true(false, "pipe for stream config");
    return;
  }

  const std::string text =
      "alarm_source = \"stream\"\n"
      "[sources.stream]\n"
      "type = \"sdr\"\n"
      "gain = 20.0 # comment\n";
  std::size_t written = 0;
  bool write_ok = true;
  while (written < text.size()) {
    ssize_t n = ::write(fds[1], text.data() + written, text.size() - written);
    if (n <= 0) {
      write_ok = false;
      break;
    }
    written += static_cast<std::size_t>(n);
  }
  ::close(fds[1]);
  expect_true(write_ok && written == text.size(), "stream config written to pipe");

  std::string proc_path = "/proc/self/fd/" + std::to_string(fds[0]);
  Config cfg = load_config(proc_path, false);
  expect_eq(cfg.alarm_source, "stream", "load_config reads a proc fd stream");
  expect_eq(cfg.sources.at("stream").gain, "20.0", "stream config comments are stripped");

  ::close(fds[0]);
}

}  // namespace

int main() {
  std::setlocale(LC_NUMERIC, "C");
  test_validation_accepts_manual_decimal_gain();
  test_validation_rejects_unset_auto_and_junk();
  test_validation_rejects_nonfinite_and_out_of_range();
  test_validation_rejects_auto_gain_sentinel();
  test_canonical_form_is_locale_independent();
  test_canonical_survives_backend_math();
  test_validation_never_throws_and_handles_null_error();
  test_defaults_and_read_only_load();
  test_config_parsing_and_metadata();
  test_stream_config_from_proc_fd();

  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all sdr gain/config tests passed\n");
  return 0;
}
