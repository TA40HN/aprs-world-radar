// Host renderer: parses the captured API answers in sim/data with the firmware's own parsers, draws every screen with
// the firmware's own UI code in two 120-row strips (as on the device), and writes sim/out/<screen>.png.
//   pio run -e sim && .pio/build/sim/program
#include <LovyanGFX.hpp>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../src/parse.h"
#include "../src/ui.h"

namespace {

struct FileReader {
  FILE* f;
  explicit FileReader(const char* path) : f(fopen(path, "rb")) {}
  ~FileReader() { if (f) fclose(f); }
  int read() { return f ? fgetc(f) : -1; }
  int peek() {
    if (!f) return -1;
    int c = fgetc(f);
    if (c >= 0) ungetc(c, f);
    return c;
  }
  size_t readBytes(char* buf, size_t n) { return f ? fread(buf, 1, n, f) : 0; }
};

void render(lgfx::LGFX_Sprite& strip, lgfx::LGFX_Sprite& full, const UiState& s, const char* name) {
  for (int oy = 0; oy < SCREEN_H; oy += STRIP_H) {
    Canvas c(strip, oy);
    drawFrame(c, s);
    strip.pushSprite(&full, 0, oy);
  }
  size_t len = 0;
  void* png = full.createPng(&len, 0, 0, SCREEN_W, SCREEN_H);
  char path[128];
  snprintf(path, sizeof path, "sim/out/%s.png", name);
  FILE* f = fopen(path, "wb");
  if (f && png) fwrite(png, 1, len, f);
  if (f) fclose(f);
  free(png);
  printf("%s (%zu bytes)\n", path, len);
}

}  // namespace

int main() {
  mkdir("sim/out", 0755);
  static Settings cfg;
  strcpy(cfg.callsign, "TB1DVM-9");
  cfg.homeSet = true;
  cfg.homeLat = 41.0082;
  cfg.homeLon = 28.9784;
  const char* lang = getenv("LANG_UI");
  cfg.lang = lang && !strcmp(lang, "tr") ? 1 : 0;

  static Position pos;
  pos.valid = true;
  pos.source = PosSource::Follow;
  pos.lat = cfg.homeLat;
  pos.lon = cfg.homeLon;

  int64_t now = time(nullptr);
  static StationList stations;
  static AircraftList aircraft;
  static FollowedStation me;
  { FileReader r("sim/data/around.json"); stations.ok = parse::stations(r, pos.lat, pos.lon, now, cfg.callsign, stations); }
  { FileReader r("sim/data/aircraft.json"); aircraft.ok = parse::aircraft(r, pos.lat, pos.lon, aircraft); }
  { FileReader r("sim/data/station.json"); parse::followed(r, now, me); }
  printf("stations %d/%d ok=%d, aircraft %d/%d ok=%d, me %s valid=%d\n", stations.count, stations.total, stations.ok,
         aircraft.count, aircraft.total, aircraft.ok, me.call, me.valid);

  // A 40-minute drive: fixes every second along a winding road, with one GPS jump that the trail must reject.
  static Trail trail;
  double lat = 41.02, lon = 28.90, heading = 60;
  uint32_t t0 = 1000;
  int kept = 0, rejected = 0;
  for (int i = 0; i < 2400; i++) {
    heading += sin(i / 260.0) * 0.55 + (i % 700 == 0 ? 70 : 0);
    double v = 12 + 8 * sin(i / 300.0);  // m/s
    lat += v * cos(heading * geo::kDeg) / 111320.0;
    lon += v * sin(heading * geo::kDeg) / (111320.0 * cos(lat * geo::kDeg));
    bool jump = i == 1200;
    double jl = jump ? lat + 0.01 : lat;  // ~1.1 km jump
    if (i > 1500 && i < 1560) continue;   // tunnel: 60 s without fixes
    if (trail.offer(jl, lon, jump ? 8 : 5, t0 + i * 1000)) kept++;
    else if (jump) rejected++;
  }
  printf("trail: %d kept, jump rejected=%d, %.1f km\n", kept, rejected, trail.distanceKm());

  static NetStatus net;
  net.wifi = true;
  net.rssi = -58;

  lgfx::LGFX_Sprite strip, full;
  strip.setColorDepth(16);
  full.setColorDepth(16);
  strip.createSprite(SCREEN_W, STRIP_H);
  full.createSprite(SCREEN_W, SCREEN_H);

  UiState s;
  s.cfg = &cfg;
  s.pos = &pos;
  s.stations = &stations;
  s.aircraft = &aircraft;
  s.me = &me;
  s.trail = &trail;
  s.net = &net;
  s.localMin = 15 * 60 + 47;
  s.nowMs = t0 + 2400 * 1000 + 1700;

  s.splash = 0.35f; render(strip, full, s, "0_splash_a");
  s.splash = 1.0f; render(strip, full, s, "0_splash_b");
  s.splash = -1;
  s.screen = SCR_APRS; render(strip, full, s, "1_aprs");
  s.screen = SCR_AIR; render(strip, full, s, "2_air");
  s.screen = SCR_STATION; render(strip, full, s, "5_station");
  s.screen = SCR_PROMO; render(strip, full, s, "6_promo_a");
  s.nowMs += 5000; render(strip, full, s, "6_promo_b");

  // In the car: GPS fix, moving north-east, heading-up radar.
  pos.source = PosSource::Gps;
  pos.speedKmh = 87;
  pos.course = 62;
  s.gpsSats = 9;
  s.screen = SCR_DRIVE; render(strip, full, s, "3_drive");
  s.screen = SCR_TRAIL; render(strip, full, s, "4_trail");
  s.screen = SCR_APRS; render(strip, full, s, "1_aprs_moving");

  if (getenv("BENCH")) {
    // Relative cost per screen (host CPU; the ESP32 is ~50x slower but the proportions hold).
    for (int scr = 0; scr < SCR_COUNT; scr++) {
      s.screen = (ScreenId)scr;
      auto t0 = clock();
      for (int i = 0; i < 300; i++) {
        s.nowMs += 33;
        for (int oy = 0; oy < SCREEN_H; oy += STRIP_H) {
          Canvas c(strip, oy);
          drawFrame(c, s);
        }
      }
      printf("bench screen %d: %.3f ms/frame\n", scr, (clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 300);
    }
  }

  s.portal = true;
  strcpy(s.portalSsid, "APRSWorld-6288");
  strcpy(s.portalIp, "192.168.4.1");
  render(strip, full, s, "7_setup");
  return 0;
}
