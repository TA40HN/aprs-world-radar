#pragma once
#include <math.h>

namespace geo {

constexpr double kEarthKm = 6371.0088;
constexpr double kDeg = M_PI / 180.0;

inline double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * kDeg, dLon = (lon2 - lon1) * kDeg;
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * kDeg) * cos(lat2 * kDeg) * sin(dLon / 2) * sin(dLon / 2);
  return 2 * kEarthKm * asin(sqrt(a < 1 ? a : 1));
}

// Initial great-circle bearing, degrees true in [0, 360).
inline double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double y = sin((lon2 - lon1) * kDeg) * cos(lat2 * kDeg);
  double x = cos(lat1 * kDeg) * sin(lat2 * kDeg) - sin(lat1 * kDeg) * cos(lat2 * kDeg) * cos((lon2 - lon1) * kDeg);
  double b = atan2(y, x) / kDeg;
  return b < 0 ? b + 360 : b;
}

// Short-range flat distance in metres (the trail guide's formula; fine below a few km).
inline double flatMeters(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * 111320.0;
  double dLon = (lon2 - lon1) * 111320.0 * cos(lat1 * kDeg);
  return sqrt(dLat * dLat + dLon * dLon);
}

inline float wrap360(float a) {
  a = fmodf(a, 360.f);
  return a < 0 ? a + 360.f : a;
}

inline const char* compass8(float deg) {
  static const char* k[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return k[(int)((wrap360(deg) + 22.5f) / 45.f) % 8];
}

}  // namespace geo
