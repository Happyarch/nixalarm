#include "sdr_gain.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <system_error>

namespace {

constexpr int kAutoGainTenths = -100;

struct ParsedGain {
  bool ok = false;
  int tenths = 0;
  const char* error = nullptr;
};

bool is_ascii_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool equals_auto(const std::string& s) {
  if (s.size() != 4) return false;
  auto lower = [](char c) -> char {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  };
  return lower(s[0]) == 'a' && lower(s[1]) == 'u' && lower(s[2]) == 't' && lower(s[3]) == 'o';
}

ParsedGain parse_gain(const std::string& value) {
  ParsedGain out;

  // Quoted config values can keep whitespace that an unquoted value loses to
  // the parser's trim. Treat surrounding ASCII whitespace as separators, not as
  // part of the number.
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && is_ascii_space(value[begin])) ++begin;
  while (end > begin && is_ascii_space(value[end - 1])) --end;

  if (begin == end) {
    out.error = "gain is not set; SDR sources need an explicit manual gain in dB";
    return out;
  }

  std::string text = value.substr(begin, end - begin);
  if (equals_auto(text)) {
    out.error = "automatic gain is not supported; set an explicit manual gain in dB";
    return out;
  }

  // std::from_chars is locale-independent and does not accept a leading '+',
  // though a sign is otherwise allowed. Skip exactly one leading '+' so "+20"
  // works, but reject a second sign: "+-20" and "++20" are not numbers.
  const char* first = text.data();
  const char* last = first + text.size();
  if (first != last && *first == '+') {
    ++first;
    if (first != last && (*first == '+' || *first == '-')) {
      out.error = "gain must not have two leading signs";
      return out;
    }
  }

  double db = 0.0;
  auto result = std::from_chars(first, last, db, std::chars_format::general);
  if (result.ec == std::errc::invalid_argument) {
    out.error = "gain must be a decimal number in dB";
    return out;
  }
  if (result.ec == std::errc::result_out_of_range) {
    out.error = "gain is outside rtl_fm's safely representable tenths-of-dB integer range";
    return out;
  }
  if (result.ec != std::errc()) {
    out.error = "gain is not a usable decimal number";
    return out;
  }
  if (result.ptr != last) {
    out.error = "gain has trailing characters after the number";
    return out;
  }
  if (!std::isfinite(db)) {
    out.error = "gain must be a finite number";
    return out;
  }

  // Mirror rtl_fm exactly: it multiplies the decimal dB value by ten and
  // truncates towards zero into a signed int. Reject before the cast if that
  // value cannot be represented, then reject the automatic-gain sentinel
  // (including every value that truncates onto it, such as -10.05).
  double scaled = db * 10.0;
  if (!std::isfinite(scaled)) {
    out.error = "gain is outside rtl_fm's safely representable tenths-of-dB integer range";
    return out;
  }
  double truncated = std::trunc(scaled);
  const double min_tenths = static_cast<double>(kSdrGainMinTenths);
  const double max_tenths_exclusive = static_cast<double>(kSdrGainMaxTenths) + 1.0;
  if (!(truncated >= min_tenths && truncated < max_tenths_exclusive)) {
    out.error = "gain is outside rtl_fm's safely representable tenths-of-dB integer range";
    return out;
  }

  int tenths = static_cast<int>(truncated);
  if (tenths == kAutoGainTenths) {
    out.error = "gain around -10.0 dB collides with rtl_fm's automatic-gain sentinel; choose another value";
    return out;
  }

  out.ok = true;
  out.tenths = tenths;
  return out;
}

std::string format_tenths(int tenths) {
  // Built from integers so a locale with a comma decimal separator can never
  // leak into the string handed to rtl_fm (whose atof expects a dot).
  unsigned int magnitude = 0;
  std::string out;
  if (tenths < 0) {
    out.push_back('-');
    magnitude = static_cast<unsigned int>(-(tenths + 1)) + 1u;
  } else {
    magnitude = static_cast<unsigned int>(tenths);
  }
  out += std::to_string(magnitude / 10u);
  out.push_back('.');
  out.push_back(static_cast<char>('0' + magnitude % 10u));
  return out;
}

}  // namespace

bool validate_sdr_gain(const std::string& value, std::string* error) {
  ParsedGain parsed = parse_gain(value);
  if (error) {
    if (parsed.ok) error->clear();
    else *error = parsed.error ? parsed.error : "invalid gain";
  }
  return parsed.ok;
}

std::string canonical_sdr_gain(const std::string& value) {
  ParsedGain parsed = parse_gain(value);
  if (!parsed.ok) return {};
  return format_tenths(parsed.tenths);
}
