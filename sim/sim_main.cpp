// Host renderer: parses the captured API answers in sim/data with the firmware's own parsers, draws every screen with
// the firmware's own UI code in two 120-row strips (as on the device), and writes sim/out/<screen>.png.
//   pio run -e sim && .pio/build/sim/program
#include <LovyanGFX.hpp>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../src/aprs_proto.h"
#include "../src/parse.h"
#include "../src/route_driver.h"
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
  cfg.aprsis = true;
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

  // A 25-minute demo drive on real roads (OSRM loop in sim/data/route.json), fixes every second, plus one GPS jump the
  // trail must reject.
  static Trail trail;
  static float rlat[300], rlon[300];
  int rn = 0;
  { FileReader r("sim/data/route.json"); rn = parse::route(r, rlat, rlon, 300); }
  RouteDriver drive;
  drive.load(rlat, rlon, rn);
  uint32_t t0 = 1000;
  int kept = 0, rejected = 0;
  for (int i = 0; i < 1500; i++) {
    drive.step(1.f);
    bool jump = i == 700;
    if (trail.offer(jump ? drive.lat() + 0.01 : drive.lat(), drive.lon(), 5, t0 + i * 1000)) kept++;
    else if (jump) rejected++;
  }
  printf("route %d points; trail: %d kept, jump rejected=%d, %.1f km\n", rn, kept, rejected, trail.distanceKm());

  // APRS protocol checks (the classic N0CALL passcode, packet shapes, message parsing, SmartBeaconing).
  {
    char buf[200];
    bool ok = aprs::passcode("N0CALL") == 13023 && aprs::passcode("n0call-9") == 13023;
    aprs::positionPacket(buf, sizeof buf, "TA4OHN-9", 41.0082, 28.9784, 90, 66.7f, NAN, "/>", "hi");
    ok &= strcmp(buf, "TA4OHN-9>APZAPW,TCPIP*:=4100.49N/02858.70E>090/036 hi") == 0;
    aprs::positionPacket(buf, sizeof buf, "X", -33.5, -70.25, 0, 0, NAN, "/>", "c");
    ok &= strstr(buf, ":=3330.00S/07015.00W>360/000 c") != nullptr;
    aprs::messagePacket(buf, sizeof buf, "TA4OHN-9", "TA1ABC", "selam", "12");
    ok &= strcmp(buf, "TA4OHN-9>APZAPW,TCPIP*::TA1ABC   :selam{12") == 0;
    aprs::Parsed p;
    ok &= aprs::parseMessage("TA1ABC>APDR16,TCPIP*,qAC,T2TURKEY::TA4OHN-9 :Merhaba usta{7", p) && !strcmp(p.from, "TA1ABC") &&
          !strcmp(p.addressee, "TA4OHN-9") && !strcmp(p.text, "Merhaba usta") && !strcmp(p.id, "7");
    ok &= aprs::parseMessage("TA1ABC>APDR16::TA4OHN-9 :ack12", p) && p.ack && !strcmp(p.id, "12");
    ok &= aprs::parseMessage("TA1ABC>APDR16::TA4OHN-9 :reply{MM}AA", p) && !strcmp(p.id, "MM") && !strcmp(p.text, "reply");
    ok &= !aprs::parseMessage("# logresp TA4OHN-9 verified, server T2X", p) && !aprs::parseMessage("TA1ABC>APRS:!4100.00N/02800.00E>", p);
    aprs::SmartBeacon sb;
    ok &= sb.due(50, 90, 0);
    sb.mark(90, 0);
    ok &= !sb.due(50, 92, 10000) && sb.due(50, 150, 20000) && sb.due(5, 90, 1800000) && !sb.due(5, 90, 1000000);
    printf("aprs protocol checks: %s\n", ok ? "OK" : "FAILED");
    if (!ok) return 1;
  }

  static NetStatus net;
  net.wifi = true;
  net.rssi = -58;

  lgfx::LGFX_Sprite strip, full;
  strip.setColorDepth(16);
  full.setColorDepth(16);
  strip.createSprite(SCREEN_W, STRIP_H);
  full.createSprite(SCREEN_W, SCREEN_H);

  static MessageLog messages;
  {
    AprsMessage m;
    snprintf(m.from, sizeof m.from, "TA1ABC");
    snprintf(m.to, sizeof m.to, "TA4OHN-9");
    snprintf(m.text, sizeof m.text, "Selam! Radarda seni Antalya'da görüyorum, iyi yolculuklar");
    m.epoch = now - 3600;
    messages.push(m);
    AprsMessage o;
    o.outgoing = true;
    o.state = MsgState::Acked;
    snprintf(o.from, sizeof o.from, "TA4OHN-9");
    snprintf(o.to, sizeof o.to, "TA1ABC");
    snprintf(o.text, sizeof o.text, "Tesekkurler, aksam frekansta gorusuruz 73");
    o.epoch = now - 1800;
    messages.push(o);
    snprintf(m.from, sizeof m.from, "YM1KTC");
    snprintf(m.text, sizeof m.text, "QSL, APRS World Radar cok iyi olmus");
    m.epoch = now - 90;
    messages.push(m);
  }
  static AprsIsStatus aprsisStatus;
  aprsisStatus.enabled = aprsisStatus.connected = aprsisStatus.verified = true;
  snprintf(aprsisStatus.server, sizeof aprsisStatus.server, "T2TURKEY");
  static Weather wx;
  { FileReader r("sim/data/weather.json"); parse::weather(r, now, wx); }
  snprintf(wx.call, sizeof wx.call, "LTAI");
  wx.distKm = 12.4f;
  wx.bearing = 300;
  static SatPass sat;
  {
    // A synthetic ISS pass 7 minutes away: NW → SE, peaking at 62°.
    sat.ok = true;
    snprintf(sat.name, sizeof sat.name, "ISS (ZARYA)");
    sat.aos = now + 7 * 60 + 12;
    sat.los = sat.aos + 9 * 60;
    sat.tca = (sat.aos + sat.los) / 2;
    sat.maxEl = 62;
    sat.azAos = 310;
    sat.azLos = 135;
    sat.n = SatPass::kTrack;
    for (int k = 0; k < sat.n; k++) {
      float f = k / (float)(sat.n - 1);
      sat.az[k] = geo::wrap360(310 + f * 185);
      sat.el[k] = 62 * sinf(f * (float)M_PI);
    }
  }

  UiState s;
  s.cfg = &cfg;
  s.pos = &pos;
  s.stations = &stations;
  s.aircraft = &aircraft;
  s.me = &me;
  s.trail = &trail;
  s.net = &net;
  s.messages = &messages;
  s.aprsis = &aprsisStatus;
  s.wx = &wx;
  s.sat = &sat;
  s.epoch = now;
  s.utcOffsetS = 3 * 3600;
  s.localMin = 15 * 60 + 47;
  s.nowMs = t0 + 1500 * 1000 + 1700;

  s.splash = 0.35f; render(strip, full, s, "0_splash_a");
  s.splash = 1.0f; render(strip, full, s, "0_splash_b");
  s.splash = -1;
  s.screen = SCR_APRS; render(strip, full, s, "1_aprs");
  s.screen = SCR_AIR; render(strip, full, s, "2_air");
  s.screen = SCR_STATION; render(strip, full, s, "5_station");
  s.screen = SCR_MSG; render(strip, full, s, "8_messages");
  for (float t : {0.15f, 0.4f, 0.6f, 0.9f}) {
    char name[32];
    snprintf(name, sizeof name, "8_envelope_%02d", (int)(t * 100));
    s.msgAnim = t;
    render(strip, full, s, name);
  }
  s.msgAnim = -1;
  s.screen = SCR_WX; render(strip, full, s, "9_weather");
  s.screen = SCR_SAT; render(strip, full, s, "10_sat");
  { int64_t keep = s.epoch; s.epoch = sat.aos + 200; render(strip, full, s, "10_sat_live"); s.epoch = keep; }
  s.screen = SCR_PROMO; render(strip, full, s, "6_promo_a");
  s.nowMs += 5000; render(strip, full, s, "6_promo_b");

  // In the car: GPS fix, moving north-east, heading-up radar.
  pos.source = PosSource::Gps;
  pos.lat = drive.lat();
  pos.lon = drive.lon();
  pos.speedKmh = drive.speedKmh();
  pos.course = drive.heading();
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
