// User settings, edited in the setup portal and kept in NVS (app.cpp). Plain data: the simulator uses it too.
#pragma once
#include <stdint.h>

enum ScreenId : uint8_t { SCR_APRS, SCR_AIR, SCR_DRIVE, SCR_TRAIL, SCR_STATION, SCR_PROMO, SCR_COUNT };

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
  uint8_t screens = (1 << SCR_COUNT) - 1;
  uint8_t lang = 0;     // 0 English, 1 Türkçe
  bool metric = true;
  bool gps = true;
  bool record = true;   // keep the drive trail in flash
  bool buzzer = false;
  bool demo = false;    // simulated drive around the current position, for shows and testing without a GPS module
  uint8_t brightness = 100, nightBrightness = 25;  // percent
  uint8_t nightFrom = 22, nightTo = 7;              // local hours
  char tz[40] = "<+03>-3";                          // POSIX TZ, default Türkiye

  const char* followCall() const { return follow[0] ? follow : callsign; }
  bool enabled(ScreenId s) const { return screens & (1 << s); }
};
