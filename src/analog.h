#pragma once

#include <memory>
#include <string>
#include <vector>

#include "clock.h"
#include "types.h"

namespace analog {

struct TimeParts {
  int hour = 0;
  int minute = 0;
  double second = 0.0;
};

double hour_hand_degrees(TimeParts t, bool use_24_hour);
double minute_hand_degrees(TimeParts t);
double second_hand_degrees(TimeParts t);
std::vector<std::string> dial_labels(bool use_24_hour, bool roman_numerals,
                                     const std::string& analog_midnight_label);

}  // namespace analog

std::unique_ptr<ClockFace> make_analog_clock();
