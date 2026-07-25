#include "dial_time.h"

#include <cmath>

double wrap_hours_24(double hours) {
  double h = std::fmod(hours, 24.0);
  return h < 0.0 ? h + 24.0 : h;
}

double wrap_hours_pm12(double hours) {
  double h = wrap_hours_24(hours);
  return h > 12.0 ? h - 24.0 : h;
}

double dial_reading_hours(bool moondial, double ra_deg, double lst_hours) {
  double body_hour_angle = lst_hours * 15.0 - ra_deg;
  double line_hour_angle = moondial ? body_hour_angle - 180.0 : body_hour_angle;
  return wrap_hours_24(12.0 + line_hour_angle / 15.0);
}

double local_mean_solar_time_hours(double jd, double lst_hours) {
  SolarPositionResult sun = solar_position_full(jd);
  double apparent = dial_reading_hours(/*moondial=*/false, sun.eq.ra_deg, lst_hours);
  // equation_of_time is apparent minus mean, so mean is apparent less it.
  return wrap_hours_24(apparent - sun.equation_of_time_minutes / 60.0);
}

double timebase_shift_hours(double target_hours, double reading_hours) {
  return wrap_hours_pm12(target_hours - reading_hours);
}
