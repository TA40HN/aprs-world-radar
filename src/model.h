// Plain data shared by the network, GPS and UI layers. No Arduino types: the simulator (sim/) uses it too.
#pragma once
#include <stdint.h>
#include "config.h"

enum class StationKind : uint8_t { Other, Fixed, Mobile, Igate, Digi, Weather, Aircraft, Balloon, Boat, Object };

struct Station {
  char call[12];
  float lat, lon;
  float distKm;     // from the position the list was fetched for
  float bearing;    // degrees true
  int32_t ageS;     // seconds since last heard (at fetch time)
  int16_t speedKmh; // -1 unknown
  int16_t course;   // -1 unknown
  StationKind kind;
};

struct Aircraft {
  char flight[9];  // callsign, or registration / hex when there is none
  char type[5];    // ICAO type, e.g. A359
  float lat, lon;
  float distKm, bearing;
  int32_t altFt;   // -1 on the ground
  int16_t gsKt;
  int16_t track;   // -1 unknown
};

// Priority when several are available: GPS > followed station > home > IP (aprs.world's edge geolocation, ~city level).
enum class PosSource : uint8_t { None, Ip, Home, Follow, Gps };

struct Position {
  bool valid = false;
  PosSource source = PosSource::None;
  double lat = 0, lon = 0;
  float speedKmh = -1;  // -1 unknown
  float course = -1;    // -1 unknown
  float accuracyM = -1;
  uint32_t ageS = 0;
};

struct FollowedStation {
  bool valid = false;
  char call[12] = {0};
  double lat = 0, lon = 0;
  float speedKmh = -1, course = -1;
  int32_t ageS = -1;
  char comment[48] = {0};
  char device[32] = {0};
  uint32_t packets = 0;
};

struct StationList {
  Station items[MAX_STATIONS];
  int count = 0;      // kept (nearest first)
  int total = 0;      // in range, before the cap
  uint32_t fetchedMs = 0;
  bool ok = false;
};

struct AircraftList {
  Aircraft items[MAX_AIRCRAFT];
  int count = 0;
  int total = 0;
  uint32_t fetchedMs = 0;
  bool ok = false;
};

struct NetStatus {
  bool wifi = false;
  bool portal = false;
  char ssid[33] = {0};
  char ip[16] = {0};
  int8_t rssi = 0;
};
