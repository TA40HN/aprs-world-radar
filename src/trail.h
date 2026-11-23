// Drive trail: which GPS fixes are kept, the in-RAM copy the trail screen draws, and the 10-second chunks written to
// flash. Rules follow the "hareket izi" guide, tuned for a car:
//   - keep a fix when it is the first one, 15 s passed, or 3 s passed and it moved >= 3 m;
//   - drop a fix whose accuracy is worse than 50 m or that implies more than 70 m/s (252 km/h) since the last kept one;
//   - a gap longer than 30 s is not joined when drawn;
//   - stored point = [person, tenths of a second since start, lat*1e6, lon*1e6, accuracy m] (person is always 0).
#pragma once
#include <stdint.h>
#include "geo.h"

struct TrailPoint {
  int32_t latE6, lonE6;
  uint32_t tDs;  // tenths of a second since the session started
  uint8_t accM;
};

class Trail {
 public:
  static constexpr int kRam = 512;  // 8 KB; older points thin out, see push()
  static constexpr int kPending = 64;
  static constexpr float kMaxAccuracyM = 50;
  static constexpr float kMaxSpeedMps = 70;
  static constexpr uint32_t kGapDs = 300;  // 30 s

  // Returns true when the fix was kept.
  bool offer(double lat, double lon, float accuracyM, uint32_t nowMs) {
    if (accuracyM > kMaxAccuracyM) return false;
    if (!started_) {
      started_ = true;
      startMs_ = nowMs;
    }
    uint32_t tDs = (nowMs - startMs_) / 100;
    if (hasLast_) {
      uint32_t elapsedMs = (tDs - last_.tDs) * 100;
      double moved = geo::flatMeters(last_.latE6 / 1e6, last_.lonE6 / 1e6, lat, lon);
      if (elapsedMs == 0 || moved / (elapsedMs / 1000.0) > kMaxSpeedMps) return false;
      bool keep = elapsedMs >= 15000 || (elapsedMs >= 3000 && moved >= 3);
      if (!keep) return false;
      if (tDs - last_.tDs <= kGapDs) distanceM_ += moved;
    }
    TrailPoint p{(int32_t)lround(lat * 1e6), (int32_t)lround(lon * 1e6), tDs, (uint8_t)(accuracyM > 255 ? 255 : accuracyM)};
    push(p);
    if (pendingCount_ < kPending) pending_[pendingCount_++] = p;
    last_ = p;
    hasLast_ = true;
    return true;
  }

  // Chunk for flash: copies and clears the fixes kept since the last call.
  int takePending(TrailPoint* out, int cap) {
    int n = pendingCount_ < cap ? pendingCount_ : cap;
    for (int i = 0; i < n; i++) out[i] = pending_[i];
    pendingCount_ = 0;
    return n;
  }

  int count() const { return count_; }
  const TrailPoint& at(int i) const { return ram_[i]; }
  bool recording() const { return started_; }
  double distanceKm() const { return distanceM_ / 1000.0; }
  uint32_t durationS(uint32_t nowMs) const { return started_ ? (nowMs - startMs_) / 1000 : 0; }
  uint32_t nowDs(uint32_t nowMs) const { return started_ ? (nowMs - startMs_) / 100 : 0; }

 private:
  // When the RAM copy is full, every second point of the older half is dropped: the shape of the drive stays, the
  // last minutes keep full detail.
  void push(const TrailPoint& p) {
    if (count_ == kRam) {
      int half = kRam / 2, w = 0;
      for (int r = 0; r < half; r += 2) ram_[w++] = ram_[r];
      for (int r = half; r < kRam; r++) ram_[w++] = ram_[r];
      count_ = w;
    }
    ram_[count_++] = p;
  }

  TrailPoint ram_[kRam];
  int count_ = 0;
  TrailPoint pending_[kPending];
  int pendingCount_ = 0;
  TrailPoint last_{};
  bool hasLast_ = false;
  bool started_ = false;
  uint32_t startMs_ = 0;
  double distanceM_ = 0;
};
