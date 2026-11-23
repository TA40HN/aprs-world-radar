// Streaming JSON parsers for the APRS World and adsb.lol responses. They read one array element at a time through an
// ArduinoJson filter, so memory stays at one element no matter how large the response is (the /around answer for a
// city is ~60 KB). `R` is any reader with int read(), int peek() and size_t readBytes(char*, size_t): an Arduino
// Stream on the device, sim/FileReader on the host.
#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>
#include "geo.h"
#include "model.h"

namespace parse {

// "2026-11-23T12:38:49.971Z" -> Unix seconds; 0 when malformed.
inline int64_t isoToEpoch(const char* s) {
  if (!s || strlen(s) < 19) return 0;
  int y = atoi(s), mo = atoi(s + 5), d = atoi(s + 8), h = atoi(s + 11), mi = atoi(s + 14), se = atoi(s + 17);
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  // days_from_civil (H. Hinnant)
  y -= mo <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
  return days * 86400 + h * 3600 + mi * 60 + se;
}

template <class R>
bool skipTo(R& in, const char* needle) {
  size_t n = strlen(needle), matched = 0;
  for (;;) {
    int c = in.read();
    if (c < 0) return false;
    if ((char)c == needle[matched]) {
      if (++matched == n) return true;
    } else {
      matched = (char)c == needle[0] ? 1 : 0;
    }
  }
}

template <class R>
int skipSpace(R& in) {
  for (;;) {
    int c = in.peek();
    if (c < 0) return -1;
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { in.read(); continue; }
    return c;
  }
}

// Positions the reader on the first element of the array under `key`. Returns false for a missing or empty array.
template <class R>
bool openArray(R& in, const char* key) {
  char quoted[32];
  snprintf(quoted, sizeof quoted, "\"%s\"", key);
  if (!skipTo(in, quoted) || !skipTo(in, "[")) return false;
  int c = skipSpace(in);
  if (c == ']') { in.read(); return false; }
  return c >= 0;
}

// After an element: true when another element follows.
template <class R>
bool nextElement(R& in) {
  int c = skipSpace(in);
  if (c != ',') return false;
  in.read();
  return skipSpace(in) >= 0;
}

inline StationKind kindOf(const char* t, bool object) {
  if (object) return StationKind::Object;
  if (!t) return StationKind::Other;
  if (!strcmp(t, "fixed")) return StationKind::Fixed;
  if (!strcmp(t, "mobile")) return StationKind::Mobile;
  if (!strcmp(t, "igate")) return StationKind::Igate;
  if (!strcmp(t, "digipeater")) return StationKind::Digi;
  if (!strcmp(t, "weather")) return StationKind::Weather;
  if (!strcmp(t, "aircraft")) return StationKind::Aircraft;
  if (!strcmp(t, "balloon")) return StationKind::Balloon;
  if (!strcmp(t, "boat")) return StationKind::Boat;
  return StationKind::Other;
}

inline void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) { dst[0] = 0; return; }
  size_t n = 0;
  while (src[n] && n + 1 < cap) { dst[n] = src[n]; n++; }
  while (n > 0 && dst[n - 1] == ' ') n--;
  dst[n] = 0;
}

inline bool sameCall(const char* a, const char* b) { return a && b && *b && strcasecmp(a, b) == 0; }

// GET /api/stations/around → nearest `MAX_STATIONS` (sorted), `total` counts every station in the answer.
template <class R>
bool stations(R& in, double lat, double lon, int64_t nowEpoch, const char* excludeCall, StationList& out) {
  out.count = 0;
  out.total = 0;
  JsonDocument filter;
  for (const char* k : {"callsign", "last_seen", "lat", "lon", "speed_kmh", "course_deg", "station_type", "is_object"}) filter[k] = true;
  if (!openArray(in, "stations")) return true;
  JsonDocument doc;
  do {
    if (deserializeJson(doc, in, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
    const char* call = doc["callsign"];
    if (!call || doc["lat"].isNull() || doc["lon"].isNull() || sameCall(call, excludeCall)) continue;
    Station s{};
    copyStr(s.call, sizeof s.call, call);
    s.lat = doc["lat"].as<float>();
    s.lon = doc["lon"].as<float>();
    s.distKm = (float)geo::distanceKm(lat, lon, s.lat, s.lon);
    s.bearing = (float)geo::bearingDeg(lat, lon, s.lat, s.lon);
    int64_t seen = isoToEpoch(doc["last_seen"]);
    s.ageS = seen && nowEpoch ? (int32_t)(nowEpoch - seen) : -1;
    s.speedKmh = doc["speed_kmh"].isNull() ? -1 : (int16_t)doc["speed_kmh"].as<float>();
    s.course = doc["course_deg"].isNull() ? -1 : (int16_t)doc["course_deg"].as<int>();
    s.kind = kindOf(doc["station_type"], doc["is_object"] | false);
    out.total++;
    // Insert sorted by distance; drop the farthest when full.
    int i;
    if (out.count < MAX_STATIONS) i = out.count++;
    else if (out.items[MAX_STATIONS - 1].distKm > s.distKm) i = MAX_STATIONS - 1;
    else continue;
    while (i > 0 && out.items[i - 1].distKm > s.distKm) { out.items[i] = out.items[i - 1]; i--; }
    out.items[i] = s;
  } while (nextElement(in));
  return true;
}

// GET https://api.adsb.lol/v2/point/{lat}/{lon}/{nm} → nearest `MAX_AIRCRAFT`.
template <class R>
bool aircraft(R& in, double lat, double lon, AircraftList& out) {
  out.count = 0;
  out.total = 0;
  JsonDocument filter;
  for (const char* k : {"flight", "r", "hex", "t", "lat", "lon", "alt_baro", "gs", "track"}) filter[k] = true;
  if (!openArray(in, "ac")) return true;
  JsonDocument doc;
  do {
    if (deserializeJson(doc, in, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
    if (doc["lat"].isNull() || doc["lon"].isNull()) continue;
    Aircraft a{};
    const char* id = doc["flight"] | (const char*)nullptr;
    if (!id || !*id || *id == ' ') id = doc["r"] | (const char*)nullptr;
    if (!id) id = doc["hex"] | "?";
    copyStr(a.flight, sizeof a.flight, id);
    copyStr(a.type, sizeof a.type, doc["t"] | "");
    a.lat = doc["lat"].as<float>();
    a.lon = doc["lon"].as<float>();
    a.distKm = (float)geo::distanceKm(lat, lon, a.lat, a.lon);
    a.bearing = (float)geo::bearingDeg(lat, lon, a.lat, a.lon);
    if (!doc["alt_baro"].is<int>()) continue;  // "ground": taxiing traffic would bury the airborne ones
    a.altFt = doc["alt_baro"].as<int>();
    a.gsKt = doc["gs"].isNull() ? -1 : (int16_t)doc["gs"].as<float>();
    a.track = doc["track"].isNull() ? -1 : (int16_t)doc["track"].as<float>();
    out.total++;
    int i;
    if (out.count < MAX_AIRCRAFT) i = out.count++;
    else if (out.items[MAX_AIRCRAFT - 1].distKm > a.distKm) i = MAX_AIRCRAFT - 1;
    else continue;
    while (i > 0 && out.items[i - 1].distKm > a.distKm) { out.items[i] = out.items[i - 1]; i--; }
    out.items[i] = a;
  } while (nextElement(in));
  return true;
}

// GET /api/stations/:callsign
template <class R>
bool followed(R& in, int64_t nowEpoch, FollowedStation& out) {
  JsonDocument filter;
  for (const char* k : {"callsign", "last_seen", "lat", "lon", "speed_kmh", "course_deg", "comment", "packet_count"}) filter[k] = true;
  filter["device"]["name"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, in, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
  if (!doc["callsign"] || doc["lat"].isNull()) { out.valid = false; return true; }
  copyStr(out.call, sizeof out.call, doc["callsign"]);
  out.lat = doc["lat"].as<double>();
  out.lon = doc["lon"].as<double>();
  out.speedKmh = doc["speed_kmh"].isNull() ? -1 : doc["speed_kmh"].as<float>();
  out.course = doc["course_deg"].isNull() ? -1 : doc["course_deg"].as<float>();
  int64_t seen = isoToEpoch(doc["last_seen"]);
  out.ageS = seen && nowEpoch ? (int32_t)(nowEpoch - seen) : -1;
  copyStr(out.comment, sizeof out.comment, doc["comment"] | "");
  copyStr(out.device, sizeof out.device, doc["device"]["name"] | "");
  out.packets = doc["packet_count"].is<const char*>() ? (uint32_t)strtoul(doc["packet_count"].as<const char*>(), nullptr, 10)
                                                       : doc["packet_count"].as<uint32_t>();
  out.valid = true;
  return true;
}

// GET /api/stations/:callsign/weather?hours=24 (samples oldest first) → the newest value of every field and the
// temperature averaged per hour.
template <class R>
bool weather(R& in, int64_t nowEpoch, Weather& out) {
  float sum[24] = {0};
  int cnt[24] = {0};
  JsonDocument filter;
  for (const char* k : {"time", "temp_c", "humidity", "pressure_hpa", "wind_dir_deg", "wind_speed_ms", "wind_gust_ms", "rain_1h_mm", "rain_24h_mm"}) filter[k] = true;
  out.sampleEpoch = 0;
  if (openArray(in, "samples")) {
    JsonDocument doc;
    do {
      if (deserializeJson(doc, in, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return false;
      int64_t t = isoToEpoch(doc["time"]);
      if (!t) continue;
      out.sampleEpoch = t;
      auto take = [&](const char* k, float& dst) {
        if (!doc[k].isNull()) dst = doc[k].as<float>();
      };
      take("temp_c", out.tempC);
      take("humidity", out.humidity);
      take("pressure_hpa", out.pressureHpa);
      take("wind_dir_deg", out.windDir);
      take("wind_speed_ms", out.windMs);
      take("wind_gust_ms", out.gustMs);
      take("rain_1h_mm", out.rain1h);
      take("rain_24h_mm", out.rain24h);
      int64_t hoursAgo = nowEpoch > t ? (nowEpoch - t) / 3600 : 0;
      if (hoursAgo < 24 && !doc["temp_c"].isNull()) {
        sum[hoursAgo] += doc["temp_c"].as<float>();
        cnt[hoursAgo]++;
      }
    } while (nextElement(in));
  }
  for (int h = 0; h < 24; h++) out.hourly[h] = cnt[h] ? sum[h] / cnt[h] : NAN;
  out.ok = out.sampleEpoch != 0;
  return true;
}

// Google encoded polyline (precision 5) → up to `cap` points, evenly thinned when longer. Returns the count.
inline int polyline(const char* p, float* lat, float* lon, int cap) {
  auto next = [&](const char*& q, int32_t& v) {
    int32_t result = 0, shift = 0, b;
    do {
      b = *q ? *q++ - 63 : 0;
      result |= (b & 0x1F) << shift;
      shift += 5;
    } while (b >= 0x20);
    v += (result & 1) ? ~(result >> 1) : (result >> 1);
  };
  int total = 0;
  int32_t la = 0, lo = 0;
  for (const char* q = p; *q;) { next(q, la); next(q, lo); total++; }
  int stride = total > cap ? (total + cap - 1) / cap : 1, n = 0, i = 0;
  la = lo = 0;
  for (const char* q = p; *q; i++) {
    next(q, la);
    next(q, lo);
    if ((i % stride == 0 || !*q) && n < cap) {
      lat[n] = la / 1e5f;
      lon[n] = lo / 1e5f;
      n++;
    }
  }
  return n;
}

// OSRM /route/v1 answer → the road geometry of the first route.
template <class R>
int route(R& in, float* lat, float* lon, int cap) {
  JsonDocument filter;
  filter["routes"][0]["geometry"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, in, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return 0;
  const char* g = doc["routes"][0]["geometry"];
  return g ? polyline(g, lat, lon, cap) : 0;
}

}  // namespace parse
