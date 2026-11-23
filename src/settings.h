// User settings, edited in the setup portal and kept in NVS (store.cpp). Plain data: the simulator uses it too.
// The struct is append-only: NVS keeps it as a blob, and an older, shorter blob loads as a prefix (new fields keep
// their defaults). Never reorder, resize or remove a field.
#pragma once
#include <stdint.h>

enum ScreenId : uint8_t {
  SCR_APRS, SCR_AIR, SCR_DRIVE, SCR_TRAIL, SCR_STATION, SCR_PROMO,  // layout 2
  SCR_MSG, SCR_WX, SCR_SAT,                                         // layout 3
  SCR_COUNT
};

struct Settings {
  static constexpr int kWifi = 3;
  char ssid[kWifi][33] = {};
  char pass[kWifi][65] = {};
  char callsign[12] = {};  // your station: card + QR to aprs.world/station/<call>
  char follow[12] = {};    // tracker to use as "my position" when there is no GPS fix (empty: callsign)
  bool homeSet = false;
  double homeLat = 0, homeLon = 0;
  uint16_t aprsRadiusKm = 50;
  uint16_t airRadiusKm = 60;
  uint8_t cycleS = 10;  // 0: change screens only with the button
  uint8_t screens = 0xFF;  // bits 0-7 of ScreenId
  uint8_t lang = 0;     // 0 English, 1 Türkçe
  bool metric = true;
  bool gps = true;
  bool record = true;   // keep the drive trail in flash
  bool buzzer = false;
  bool demo = false;    // simulated drive around the current position, for shows and testing without a GPS module
  uint8_t brightness = 100, nightBrightness = 25;  // percent
  uint8_t nightFrom = 22, nightTo = 7;              // local hours
  char tz[40] = "<+03>-3";                          // POSIX TZ, default Türkiye
  // ---- layout 3 ----
  uint8_t screensHi = 0xFF;  // bits 8-15 of ScreenId
  bool aprsis = false;       // log in to APRS-IS: messages to you (auto-acknowledged) and, with `beacon`, your position
  bool beacon = false;       // send the GPS position (never a demo or followed one) with SmartBeaconing
  char beaconCall[12] = {};  // login and beacon callsign with SSID (empty: callsign)
  char symbol[3] = "/>";     // APRS symbol table + code (car)
  char comment[44] = "APRS World Radar";
  char sats[40] = "25544";   // NORAD catalogue numbers, comma-separated (25544 = ISS, APRS digipeater 145.825 MHz)

  const char* followCall() const { return follow[0] ? follow : callsign; }
  const char* loginCall() const { return beaconCall[0] ? beaconCall : callsign; }
  bool enabled(ScreenId s) const { return s < 8 ? screens & (1 << s) : screensHi & (1 << (s - 8)); }
  void setEnabled(ScreenId s, bool on) {
    uint8_t& bits = s < 8 ? screens : screensHi;
    uint8_t mask = (uint8_t)(1 << (s < 8 ? s : s - 8));
    bits = on ? (uint8_t)(bits | mask) : (uint8_t)(bits & ~mask);
  }
};
