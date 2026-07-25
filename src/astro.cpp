#include "astro.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double deg2rad(double d) { return d * kPi / 180.0; }
double rad2deg(double r) { return r * 180.0 / kPi; }

// Wraps to [0, 360).
double norm_degrees(double d) {
  double r = std::fmod(d, 360.0);
  if (r < 0.0) r += 360.0;
  return r;
}

// Wraps to (-180, 180].
double norm_angle_signed(double d) { return norm_degrees(d + 180.0) - 180.0; }

}  // namespace

double julian_day(const std::tm& utc_tm) {
  int year = utc_tm.tm_year + 1900;
  int month = utc_tm.tm_mon + 1;
  double day = utc_tm.tm_mday +
               (utc_tm.tm_hour + utc_tm.tm_min / 60.0 + utc_tm.tm_sec / 3600.0) / 24.0;
  if (month <= 2) {
    year -= 1;
    month += 12;
  }
  double a = std::floor(year / 100.0);
  double b = 2 - a + std::floor(a / 4.0);
  return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day + b - 1524.5;
}

double julian_centuries(double jd) { return (jd - 2451545.0) / 36525.0; }

SolarPositionResult solar_position_full(double jd) {
  double n = jd - 2451545.0;

  double l0 = norm_degrees(280.460 + 0.9856474 * n);       // mean longitude
  double g = norm_degrees(357.528 + 0.9856003 * n);        // mean anomaly
  double g_rad = deg2rad(g);

  double lambda = norm_degrees(l0 + 1.915 * std::sin(g_rad) + 0.020 * std::sin(2 * g_rad));
  double lambda_rad = deg2rad(lambda);

  double epsilon = 23.439 - 0.0000004 * n;  // obliquity of the ecliptic
  double epsilon_rad = deg2rad(epsilon);

  double ra_deg = norm_degrees(
      rad2deg(std::atan2(std::cos(epsilon_rad) * std::sin(lambda_rad), std::cos(lambda_rad))));
  double dec_deg = rad2deg(std::asin(std::sin(epsilon_rad) * std::sin(lambda_rad)));

  // Meeus 28.1 form: EoT = 4*(L0 - 0.0057183 - RA), wrapped to a small angle
  // before converting degrees to minutes (1 degree = 4 minutes of time).
  double eot_deg = norm_angle_signed(l0 - 0.0057183 - ra_deg);
  double eot_minutes = 4.0 * eot_deg;

  return SolarPositionResult{EquatorialCoord{ra_deg, dec_deg}, eot_minutes};
}

EquatorialCoord lunar_position(double jd) {
  // Paul Schlyter's low-precision lunar position formula
  // (stjarnhimlen.se/comp/ppcomp.html): two-body Keplerian orbit plus the
  // largest periodic perturbation terms. Good to a few arcminutes.
  double d = jd - 2451543.5;  // days since 2000 Jan 0.0 (Schlyter's epoch)

  double moon_N = norm_degrees(125.1228 - 0.0529538083 * d);
  double moon_i = 5.1454;
  double moon_w = norm_degrees(318.0634 + 0.1643573223 * d);
  double moon_a = 60.2666;
  double moon_e = 0.054900;
  double moon_M = norm_degrees(115.3654 + 13.0649929509 * d);

  double m_rad = deg2rad(moon_M);
  double e_rad = m_rad + moon_e * std::sin(m_rad) * (1.0 + moon_e * std::cos(m_rad));
  for (int i = 0; i < 6; ++i) {
    double delta = e_rad - moon_e * std::sin(e_rad) - m_rad;
    double deriv = 1.0 - moon_e * std::cos(e_rad);
    e_rad -= delta / deriv;
  }

  double x = moon_a * (std::cos(e_rad) - moon_e);
  double y = moon_a * std::sqrt(1.0 - moon_e * moon_e) * std::sin(e_rad);
  double r = std::sqrt(x * x + y * y);
  double v_deg = rad2deg(std::atan2(y, x));  // true anomaly

  double vw_rad = deg2rad(v_deg + moon_w);
  double n_rad = deg2rad(moon_N);
  double i_rad = deg2rad(moon_i);

  double xeclip = r * (std::cos(n_rad) * std::cos(vw_rad) -
                        std::sin(n_rad) * std::sin(vw_rad) * std::cos(i_rad));
  double yeclip = r * (std::sin(n_rad) * std::cos(vw_rad) +
                        std::cos(n_rad) * std::sin(vw_rad) * std::cos(i_rad));
  double zeclip = r * (std::sin(vw_rad) * std::sin(i_rad));

  double lon = norm_degrees(rad2deg(std::atan2(yeclip, xeclip)));
  double lat = rad2deg(std::atan2(zeclip, std::sqrt(xeclip * xeclip + yeclip * yeclip)));

  // Sun elements (Schlyter's own Sun formula, kept internally consistent with
  // the Moon perturbation terms rather than reusing solar_position_full's
  // slightly different epoch convention).
  double sun_w = norm_degrees(282.9404 + 4.70935e-5 * d);
  double sun_Ms = norm_degrees(356.0470 + 0.9856002585 * d);
  double sun_Ls = norm_degrees(sun_w + sun_Ms);

  double moon_Lmoon = norm_degrees(moon_N + moon_w + moon_M);
  double D = norm_degrees(moon_Lmoon - sun_Ls);   // elongation
  double F = norm_degrees(moon_Lmoon - moon_N);   // argument of latitude

  auto sd = [](double deg) { return std::sin(deg2rad(deg)); };

  double lon_corr = -1.274 * sd(moon_M - 2 * D) + 0.658 * sd(2 * D) - 0.186 * sd(sun_Ms) -
                     0.059 * sd(2 * moon_M - 2 * D) - 0.057 * sd(moon_M - 2 * D + sun_Ms) +
                     0.053 * sd(moon_M + 2 * D) + 0.046 * sd(2 * D - sun_Ms) +
                     0.041 * sd(moon_M - sun_Ms) - 0.035 * sd(D) - 0.031 * sd(moon_M + sun_Ms) -
                     0.015 * sd(2 * F - 2 * D) + 0.011 * sd(moon_M - 4 * D);
  double lat_corr = -0.173 * sd(F - 2 * D) - 0.055 * sd(moon_M - F - 2 * D) -
                     0.046 * sd(moon_M + F - 2 * D) + 0.033 * sd(F + 2 * D) +
                     0.017 * sd(2 * moon_M + F);

  lon = norm_degrees(lon + lon_corr);
  lat = lat + lat_corr;

  double ecl = 23.4393 - 3.563e-7 * d;  // obliquity, Schlyter's own approximation
  double lon_rad = deg2rad(lon);
  double lat_rad = deg2rad(lat);
  double ecl_rad = deg2rad(ecl);

  double xeq = std::cos(lon_rad) * std::cos(lat_rad);
  double yeq = std::sin(lon_rad) * std::cos(lat_rad) * std::cos(ecl_rad) -
               std::sin(lat_rad) * std::sin(ecl_rad);
  double zeq = std::sin(lon_rad) * std::cos(lat_rad) * std::sin(ecl_rad) +
               std::sin(lat_rad) * std::cos(ecl_rad);

  double ra_deg = norm_degrees(rad2deg(std::atan2(yeq, xeq)));
  double dec_deg = rad2deg(std::atan2(zeq, std::sqrt(xeq * xeq + yeq * yeq)));

  return EquatorialCoord{ra_deg, dec_deg};
}

double local_sidereal_time_hours(double jd, double longitude_deg) {
  double t = julian_centuries(jd);
  double gmst_deg = 280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t -
                     t * t * t / 38710000.0;
  double lst_deg = norm_degrees(gmst_deg + longitude_deg);
  return lst_deg / 15.0;
}

HorizontalCoord equatorial_to_horizontal(EquatorialCoord eq, double lat_deg, double lst_hours) {
  double hour_angle_deg = lst_hours * 15.0 - eq.ra_deg;

  double lat_rad = deg2rad(lat_deg);
  double dec_rad = deg2rad(eq.dec_deg);
  double h_rad = deg2rad(hour_angle_deg);

  double sin_alt = std::sin(dec_rad) * std::sin(lat_rad) +
                    std::cos(dec_rad) * std::cos(lat_rad) * std::cos(h_rad);
  sin_alt = std::clamp(sin_alt, -1.0, 1.0);
  double alt_rad = std::asin(sin_alt);

  // Meeus azimuth (measured from South, positive westward); convert to
  // compass azimuth (from North, clockwise through East) by adding 180deg.
  double az_meeus_rad =
      std::atan2(std::sin(h_rad), std::cos(h_rad) * std::sin(lat_rad) - std::tan(dec_rad) * std::cos(lat_rad));
  double az_deg = norm_degrees(rad2deg(az_meeus_rad) + 180.0);

  return HorizontalCoord{az_deg, rad2deg(alt_rad)};
}

double moon_illuminated_fraction(double jd) {
  EquatorialCoord moon = lunar_position(jd);
  EquatorialCoord sun = solar_position_full(jd).eq;

  // Angular separation of the two bodies on the sky. At elongation 0 the Moon
  // sits with the Sun and is new; at 180 it is opposite and full, so the lit
  // fraction is (1 - cos elongation) / 2.
  double dec_m = deg2rad(moon.dec_deg), dec_s = deg2rad(sun.dec_deg);
  double d_ra = deg2rad(moon.ra_deg - sun.ra_deg);
  double cos_elong =
      std::sin(dec_m) * std::sin(dec_s) + std::cos(dec_m) * std::cos(dec_s) * std::cos(d_ra);
  cos_elong = std::clamp(cos_elong, -1.0, 1.0);
  return 0.5 * (1.0 - cos_elong);
}
