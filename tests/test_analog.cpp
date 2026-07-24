#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "analog.h"

namespace {

int g_failures = 0;

void expect_near(double actual, double expected, double tol, const char* what) {
  if (std::fabs(actual - expected) > tol) {
    std::fprintf(stderr, "FAIL %s: expected %.6f, got %.6f (tol %.6f)\n", what, expected, actual, tol);
    ++g_failures;
  } else {
    std::printf("ok   %s (%.6f ~= %.6f)\n", what, actual, expected);
  }
}

void expect_eq(const std::string& actual, const std::string& expected, const char* what) {
  if (actual != expected) {
    std::fprintf(stderr, "FAIL %s: expected %s, got %s\n", what, expected.c_str(), actual.c_str());
    ++g_failures;
  } else {
    std::printf("ok   %s (%s)\n", what, actual.c_str());
  }
}

void expect_size(const std::vector<std::string>& actual, size_t expected, const char* what) {
  if (actual.size() != expected) {
    std::fprintf(stderr, "FAIL %s: expected %zu labels, got %zu\n", what, expected, actual.size());
    ++g_failures;
  } else {
    std::printf("ok   %s (%zu labels)\n", what, actual.size());
  }
}

void test_12_hour_angles() {
  expect_near(analog::hour_hand_degrees({3, 0, 0}, false), 90.0, 1e-9, "12h 3:00 hour angle");
  expect_near(analog::hour_hand_degrees({6, 30, 0}, false), 195.0, 1e-9, "12h 6:30 hour angle");
  expect_near(analog::hour_hand_degrees({12, 0, 0}, false), 0.0, 1e-9, "12h 12:00 hour angle");
}

void test_24_hour_angles() {
  expect_near(analog::hour_hand_degrees({6, 0, 0}, true), 90.0, 1e-9, "24h 6:00 hour angle");
  expect_near(analog::hour_hand_degrees({12, 0, 0}, true), 180.0, 1e-9, "24h 12:00 hour angle");
  expect_near(analog::hour_hand_degrees({18, 0, 0}, true), 270.0, 1e-9, "24h 18:00 hour angle");
  expect_near(analog::hour_hand_degrees({0, 0, 0}, true), 0.0, 1e-9, "24h midnight hour angle");
}

void test_smooth_angles() {
  expect_near(analog::second_hand_degrees({0, 0, 0.5}), 3.0, 1e-9, "smooth second hand half second");
  expect_near(analog::minute_hand_degrees({0, 1, 30.0}), 9.0, 1e-9, "smooth minute hand includes seconds");
  expect_near(analog::hour_hand_degrees({3, 30, 30.0}, false), 105.25, 1e-9,
              "smooth hour hand includes fractional minutes");
}

void test_arabic_labels() {
  auto twelve = analog::dial_labels(false, false, "24");
  expect_size(twelve, 12, "12h Arabic label count");
  expect_eq(twelve.front(), "1", "12h Arabic starts at 1");
  expect_eq(twelve.back(), "12", "12h Arabic ends at 12");

  auto twenty_four = analog::dial_labels(true, false, "24");
  expect_size(twenty_four, 24, "24h Arabic label count");
  expect_eq(twenty_four.front(), "1", "24h Arabic starts at 1");
  expect_eq(twenty_four.back(), "24", "24h Arabic default midnight label");

  auto zero = analog::dial_labels(true, false, "0");
  expect_eq(zero.back(), "0", "24h Arabic zero midnight label");
}

void test_roman_labels() {
  auto twelve = analog::dial_labels(false, true, "0");
  expect_eq(twelve[3], "IV", "Roman IV");
  expect_eq(twelve[8], "IX", "Roman IX");
  expect_eq(twelve[11], "XII", "Roman XII");

  auto twenty_four = analog::dial_labels(true, true, "0");
  expect_size(twenty_four, 24, "24h Roman label count");
  expect_eq(twenty_four[23], "XXIV", "24h Roman midnight remains XXIV");
}

}  // namespace

int main() {
  test_12_hour_angles();
  test_24_hour_angles();
  test_smooth_angles();
  test_arabic_labels();
  test_roman_labels();

  if (g_failures > 0) {
    std::fprintf(stderr, "\n%d test(s) FAILED\n", g_failures);
    return 1;
  }
  std::printf("\nall tests passed\n");
  return 0;
}
