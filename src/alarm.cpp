#include "alarm.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

std::optional<std::chrono::time_point<Clock>> parse_alarm_time(const std::string& s) {
  static const std::regex pattern(R"(^\s*(\d{1,2}):(\d{2})\s*([AaPp]\.?[Mm]\.?)?\s*$)");
  std::smatch match;
  if (!std::regex_match(s, match, pattern)) return std::nullopt;
  int h = -1, m = -1;
  try {
    h = std::stoi(match[1].str());
    m = std::stoi(match[2].str());
  } catch (...) {
    return std::nullopt;
  }
  std::string meridiem = match[3].matched ? match[3].str() : "";
  std::transform(meridiem.begin(), meridiem.end(), meridiem.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  meridiem.erase(std::remove(meridiem.begin(), meridiem.end(), '.'), meridiem.end());
  if (m < 0 || m > 59) return std::nullopt;
  if (meridiem.empty()) {
    if (h < 0 || h > 23) return std::nullopt;
  } else {
    if (h < 1 || h > 12) return std::nullopt;
    if (meridiem == "am") h = (h == 12) ? 0 : h;
    else if (meridiem == "pm") h = (h == 12) ? 12 : h + 12;
    else return std::nullopt;
  }
  auto now = Clock::now();
  std::time_t tt = Clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&tt, &tm);
  tm.tm_hour = h;
  tm.tm_min = m;
  tm.tm_sec = 0;
  auto alarm = Clock::from_time_t(std::mktime(&tm));
  if (alarm <= now) alarm += std::chrono::hours(24);
  return alarm;
}

std::string format_local_time(const std::chrono::time_point<Clock>& t) {
  std::time_t tt = Clock::to_time_t(t);
  std::tm tm{};
  localtime_r(&tt, &tm);
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S %Z");
  return out.str();
}

std::vector<std::chrono::time_point<Clock>> parse_alarm_times(const std::vector<std::string>& times) {
  std::vector<std::chrono::time_point<Clock>> parsed;
  for (const auto& time : times) {
    if (time.empty()) continue;
    auto alarm = parse_alarm_time(time);
    if (!alarm) {
      std::cerr << "nixalarm: invalid config alarm entry: " << time << "\n";
      parsed.clear();
      return parsed;
    }
    parsed.push_back(*alarm);
  }
  std::sort(parsed.begin(), parsed.end());
  return parsed;
}
