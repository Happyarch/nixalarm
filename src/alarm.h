#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "types.h"

std::optional<std::chrono::time_point<Clock>> parse_alarm_time(const std::string& s);
std::string format_local_time(const std::chrono::time_point<Clock>& t);
std::vector<std::chrono::time_point<Clock>> parse_alarm_times(const std::vector<std::string>& times);
