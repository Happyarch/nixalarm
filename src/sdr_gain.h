#pragma once

#include <limits>
#include <string>

// rtl_fm parses -g as a decimal dB value, multiplies it by ten and stores the
// result in a signed int. It also reserves -100 tenths (-10.0 dB) for automatic
// gain, which this program never asks for. These bounds are the safely
// representable signed int tenths themselves; they are not a tuner-specific
// gain range.
inline constexpr int kSdrGainMinTenths = std::numeric_limits<int>::min();
inline constexpr int kSdrGainMaxTenths = std::numeric_limits<int>::max();

// True when value is a finite decimal gain in dB that rtl_fm can be given as an
// explicit manual gain. Empty (unset), "auto" and values that collapse onto
// rtl_fm's automatic-gain sentinel are rejected. On failure a short explanation
// is stored in *error when error is non-null. Never throws.
bool validate_sdr_gain(const std::string& value, std::string* error = nullptr);

// The canonical, locale-independent decimal spelling of a valid gain, suitable
// for passing to rtl_fm as -g. Values with more precision than rtl_fm resolves
// are reduced to the tenth of a dB rtl_fm will actually use. Returns an empty
// string for invalid input.
std::string canonical_sdr_gain(const std::string& value);
