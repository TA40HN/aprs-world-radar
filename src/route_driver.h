// Drives a simulated vehicle along a road polyline (demo drive): cruises at 40-65 km/h, slows for turns, loops at the
// end. Plain C++, shared by the firmware and the simulator.
#pragma once
#include <math.h>

#include "geo.h"

class RouteDriver {
 public:
  void load(const float* lat, const float* lon, int n) {
    lat_ = lat;
    lon_ = lon;
    n_ = n;
    seg_ = 0;
    along_ = 0;
    speed_ = 0;
    t_ = 0;
    heading_ = n > 1 ? segBearing(0) : 0;
  }
  bool loaded() const { return n_ >= 2; }

  void step(float dt) {
    if (!loaded() || dt <= 0) return;
    t_ += dt;
    // Target: a cruise that drifts between 40 and 65 km/h, 25 km/h into a sharp turn within 60 m.
    float target = 52 + 13 * sinf(t_ / 45.f);
    float left = segLen(seg_) - along_;
    float turn = fabsf(geo::wrap360(segBearing(next(seg_)) - segBearing(seg_) + 180) - 180);
    if (turn > 50 && left < 60) target = 25;
    float accel = target > speed_ ? 6.f : 10.f;  // km/h per second
    speed_ += fmaxf(-accel * dt, fminf(accel * dt, target - speed_));
    along_ += speed_ / 3.6f * dt;
    while (along_ >= segLen(seg_)) {
      along_ -= segLen(seg_);
      seg_ = next(seg_);
    }
    // The heading eases into the segment's bearing instead of snapping at each vertex.
    float diff = geo::wrap360(segBearing(seg_) - heading_ + 180) - 180;
    heading_ = geo::wrap360(heading_ + diff * fminf(1.f, dt * 4));
  }

  double lat() const { return lat_[seg_] + (lat_[next(seg_)] - lat_[seg_]) * frac(); }
  double lon() const { return lon_[seg_] + (lon_[next(seg_)] - lon_[seg_]) * frac(); }
  float speedKmh() const { return speed_; }
  float heading() const { return heading_; }

 private:
  int next(int i) const { return (i + 1) % n_; }  // the last vertex joins the first: OSRM loops end where they start
  float segLen(int i) const {
    float d = (float)geo::flatMeters(lat_[i], lon_[i], lat_[next(i)], lon_[next(i)]);
    return d < 0.5f ? 0.5f : d;  // zero-length segments (duplicate vertices) must still be stepped over
  }
  float segBearing(int i) const { return (float)geo::bearingDeg(lat_[i], lon_[i], lat_[next(i)], lon_[next(i)]); }
  float frac() const { return fminf(1.f, along_ / segLen(seg_)); }

  const float* lat_ = nullptr;
  const float* lon_ = nullptr;
  int n_ = 0, seg_ = 0;
  float along_ = 0, speed_ = 0, heading_ = 0, t_ = 0;
};
