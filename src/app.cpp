// Device entry point (core 1): GPS, position choice, drive trail, buttons, backlight and the render loop.
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <sys/time.h>
#include <time.h>

#include "app_state.h"
#include "display.h"
#include "ui.h"

Shared g;
Settings cfg;

namespace {

Display lcd;
lgfx::LGFX_Sprite strip(&lcd);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
Trail trail;

// Render-side copies of the shared data, refreshed when the network task publishes a new version.
StationList dStations;
AircraftList dAircraft;
FollowedStation dMe;
uint32_t dMeFetchedMs = 0;
NetStatus dNet;
uint32_t seenVersion = UINT32_MAX;

ScreenId screen = SCR_APRS;
uint32_t screenSinceMs = 0, pauseCycleUntil = 0;
bool gpsFix = false;
uint32_t gpsFixMs = 0;

// ------------------------------------------------------------------------------------------------ GPS & position
void readGps() {
  if (!cfg.gps) return;
  while (gpsSerial.available()) gps.encode((char)gpsSerial.read());
  // No NTP yet (no internet in the car)? Take the clock from the satellites.
  if (!epochNow() && gps.date.isValid() && gps.time.isValid() && gps.date.year() >= 2025) {
    struct tm tm = {};
    tm.tm_year = gps.date.year() - 1900;
    tm.tm_mon = gps.date.month() - 1;
    tm.tm_mday = gps.date.day();
    tm.tm_hour = gps.time.hour();
    tm.tm_min = gps.time.minute();
    tm.tm_sec = gps.time.second();
    setenv("TZ", "UTC0", 1);
    tzset();
    timeval tv = {mktime(&tm), 0};
    settimeofday(&tv, nullptr);
    setenv("TZ", cfg.tz, 1);
    tzset();
  }
  if (gps.location.isUpdated() && gps.location.isValid()) {
    float hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99;
    gpsFix = hdop < 10;
    gpsFixMs = millis();
    float acc = hdop * 5.f;  // rough metres; the trail drops fixes worse than 50 m
    if (gpsFix) trail.offer(gps.location.lat(), gps.location.lng(), acc, millis());
  }
  if (millis() - gpsFixMs > 5000) gpsFix = false;
}

Position choosePosition() {
  Position p;
  if (gpsFix) {
    p.valid = true;
    p.source = PosSource::Gps;
    p.lat = gps.location.lat();
    p.lon = gps.location.lng();
    p.speedKmh = gps.speed.isValid() ? (float)gps.speed.kmph() : -1;
    p.course = gps.course.isValid() && p.speedKmh > 3 ? (float)gps.course.deg() : -1;
    p.accuracyM = gps.hdop.isValid() ? gps.hdop.hdop() * 5.f : -1;
    return p;
  }
  int32_t meAge = dMe.ageS < 0 ? -1 : dMe.ageS + (int32_t)((millis() - dMeFetchedMs) / 1000);
  if (dMe.valid && meAge >= 0 && meAge < FOLLOW_MAX_AGE_S) {
    p.valid = true;
    p.source = PosSource::Follow;
    p.lat = dMe.lat;
    p.lon = dMe.lon;
    p.speedKmh = meAge < 300 ? dMe.speedKmh : -1;
    p.course = meAge < 300 && dMe.speedKmh > 3 ? dMe.course : -1;
    p.ageS = (uint32_t)meAge;
    return p;
  }
  if (cfg.homeSet) {
    p.valid = true;
    p.source = PosSource::Home;
    p.lat = cfg.homeLat;
    p.lon = cfg.homeLon;
    return p;
  }
  Lock l;
  if (g.ipValid) {
    p.valid = true;
    p.source = PosSource::Ip;
    p.lat = g.ipLat;
    p.lon = g.ipLon;
  }
  return p;
}

// Simulated drive for shows and for testing without a GPS module: loops around the position it started from at
// 40-85 km/h, turns back toward it beyond 2.5 km, and feeds the trail like real fixes (never written to flash).
struct DemoDrive {
  bool on = false;
  double lat0 = 0, lon0 = 0, lat = 0, lon = 0;
  float heading = 35, speed = 0, t = 0;
  uint32_t lastMs = 0;
} demo;

bool demoStep(const Position& anchor, Position& out) {
  if (!cfg.demo || gpsFix) return false;
  uint32_t now = millis();
  if (!demo.on) {
    if (!anchor.valid) return false;
    demo = DemoDrive{};
    demo.on = true;
    demo.lat0 = demo.lat = anchor.lat;
    demo.lon0 = demo.lon = anchor.lon;
    demo.lastMs = now;
  }
  if (now - demo.lastMs >= 1000) {
    float dt = (now - demo.lastMs) / 1000.f;
    demo.lastMs = now;
    demo.t += dt;
    demo.speed = 62 + 23 * sinf(demo.t / 37.f);
    demo.heading += sinf(demo.t / 19.f) * 3.2f * dt;
    if (geo::distanceKm(demo.lat0, demo.lon0, demo.lat, demo.lon) > 2.5) {
      float back = (float)geo::bearingDeg(demo.lat, demo.lon, demo.lat0, demo.lon0);
      float diff = geo::wrap360(back - demo.heading + 180) - 180;
      demo.heading += constrain(diff, -6 * dt, 6 * dt);
    }
    demo.heading = geo::wrap360(demo.heading);
    double m = demo.speed / 3.6 * dt;
    demo.lat += m * cos(demo.heading * geo::kDeg) / 111320.0;
    demo.lon += m * sin(demo.heading * geo::kDeg) / (111320.0 * cos(demo.lat * geo::kDeg));
    trail.offer(demo.lat, demo.lon, 5, now);
  }
  out = Position{};
  out.valid = true;
  out.source = PosSource::Gps;
  out.lat = demo.lat;
  out.lon = demo.lon;
  out.speedKmh = demo.speed;
  out.course = demo.heading;
  out.accuracyM = 5;
  return true;
}

// ------------------------------------------------------------------------------------------------ buttons
struct Button {
  uint8_t pin;
  bool down = false;
  uint32_t since = 0;
  bool longFired = false;
};
Button buttons[] = {{PIN_BOOT}, {PIN_BUTTON}};

bool movingNow(const Position& p) { return p.valid && p.source == PosSource::Gps && p.speedKmh > 10; }

// While driving, only the screens that help on the road take part in the rotation.
bool screenAllowed(ScreenId s, bool moving) {
  if (!cfg.enabled(s)) return false;
  return !moving || s == SCR_APRS || s == SCR_AIR || s == SCR_DRIVE || s == SCR_TRAIL;
}

void nextScreen(bool moving) {
  for (int i = 1; i <= SCR_COUNT; i++) {
    ScreenId s = (ScreenId)((screen + i) % SCR_COUNT);
    if (screenAllowed(s, moving)) {
      screen = s;
      break;
    }
  }
  screenSinceMs = millis();
}

void pollButtons(bool moving) {
  for (Button& b : buttons) {
    bool pressed = digitalRead(b.pin) == LOW;
    uint32_t now = millis();
    if (pressed && !b.down) {
      b.down = true;
      b.since = now;
      b.longFired = false;
    } else if (pressed && b.down && !b.longFired && now - b.since > 3000) {
      b.longFired = true;  // long press: setup access point (a second long press restarts into normal mode)
      if (portalApActive()) ESP.restart();
      portalStartAp();
    } else if (!pressed && b.down) {
      b.down = false;
      if (!b.longFired && now - b.since > 30) {
        nextScreen(moving);
        pauseCycleUntil = now + 60000;
      }
    }
  }
}

// ------------------------------------------------------------------------------------------------ buzzer
void aircraftAlert() {
  if (!cfg.buzzer) return;
  static char alerted[8][9];
  static int next = 0;
  for (int i = 0; i < dAircraft.count; i++) {
    const Aircraft& a = dAircraft.items[i];
    if (a.distKm > 5) break;  // sorted by distance
    bool seen = false;
    for (auto& f : alerted) seen |= strcmp(f, a.flight) == 0;
    if (seen) continue;
    snprintf(alerted[next], sizeof alerted[next], "%s", a.flight);
    next = (next + 1) % 8;
    tone(PIN_BUZZER, 2600, 70);
    delay(110);
    tone(PIN_BUZZER, 3400, 90);
  }
}

// ------------------------------------------------------------------------------------------------ backlight
int localMinutes() {
  if (!epochNow()) return -1;
  struct tm tm;
  time_t t = time(nullptr);
  localtime_r(&t, &tm);
  return tm.tm_hour * 60 + tm.tm_min;
}

void updateBacklight(int localMin) {
  static int applied = -1;
  int pct = cfg.brightness;
  if (localMin >= 0) {
    int h = localMin / 60;
    bool night = cfg.nightFrom > cfg.nightTo ? (h >= cfg.nightFrom || h < cfg.nightTo) : (h >= cfg.nightFrom && h < cfg.nightTo);
    if (night) pct = cfg.nightBrightness;
  }
  if (pct == applied) return;
  applied = pct;
  lcd.setBrightness((uint8_t)(pct * 255 / 100));
}

// ------------------------------------------------------------------------------------------------ render
uint32_t drawUs = 0, pushUs = 0;  // accumulated per status period (serial log)

void render(UiState& s) {
  for (int oy = 0; oy < SCREEN_H; oy += STRIP_H) {
    Canvas c(strip, oy);
    uint32_t t0 = micros();
    drawFrame(c, s);
    uint32_t t1 = micros();
    strip.pushSprite(0, oy);
    drawUs += t1 - t0;
    pushUs += micros() - t1;
  }
}

UiState baseState(const Position& pos) {
  UiState s;
  s.cfg = &cfg;
  s.pos = &pos;
  s.stations = &dStations;
  s.aircraft = &dAircraft;
  s.me = &dMe;
  s.trail = &trail;
  s.net = &dNet;
  s.nowMs = millis();
  s.localMin = localMinutes();
  s.screen = screen;
  s.gpsSats = cfg.gps && gps.charsProcessed() > 20 ? (int)gps.satellites.value() : -1;
  return s;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  g.mutex = xSemaphoreCreateMutex();
  storeBegin();
  loadSettings(cfg);
  setenv("TZ", cfg.tz, 1);
  tzset();
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  lcd.init();
  lcd.setBrightness(0);
  lcd.fillScreen(TFT_BLACK);
  // Allocate the strip before Wi-Fi fragments the heap.
  strip.setColorDepth(16);
  strip.createSprite(SCREEN_W, STRIP_H);
  updateBacklight(-1);

  if (cfg.gps) gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  netBegin();
  portalBegin();
  Serial.printf(FW_NAME " " FW_VERSION " heap %lu, largest block %lu\n", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());

  // Boot animation; it holds on the last frame until Wi-Fi is up (at most 6 s).
  Position none;
  uint32_t t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    bool up;
    {
      Lock l;
      up = g.net.wifi || g.portalRequested;
    }
    if (el > 6000 || (el > 2400 && up)) break;
    UiState s = baseState(none);
    s.splash = fminf(1.f, el / 2200.f);
    render(s);
    portalLoop();
  }
  screenSinceMs = millis();
}

void loop() {
  readGps();
  bool wantPortal;
  {
    Lock l;
    if (seenVersion != g.dataVersion) {
      seenVersion = g.dataVersion;
      dStations = g.stations;
      dAircraft = g.aircraft;
      dMe = g.me;
      dMeFetchedMs = g.meFetchedMs;
      aircraftAlert();
    }
    dNet = g.net;
    wantPortal = g.portalRequested;
  }
  if (wantPortal && !portalApActive()) portalStartAp();

  static Position pos;
  pos = choosePosition();
  bool demoPos = demoStep(pos, pos);
  {
    Lock l;
    g.pos = pos;
  }

  bool moving = movingNow(pos);
  pollButtons(moving);
  uint32_t now = millis();
  if (!screenAllowed(screen, moving)) nextScreen(moving);
  if (cfg.cycleS && now > pauseCycleUntil && now - screenSinceMs > cfg.cycleS * 1000UL) nextScreen(moving);

  static uint32_t lastFlush = 0, frames = 0;
  frames++;
  if (now - lastFlush > 10000) {
    int64_t e = epochNow();
    trailFlush(trail, e ? e * 1000 : 0);
    Serial.printf("[app] %.1f fps (draw %lu ms, push %lu ms per frame), screen %d, pos %d (%.4f, %.4f) %s, stations %d/%d, aircraft %d, trail %d, epoch %lld, heap %lu/%lu\n",
                  frames * 1000.f / (now - lastFlush), (unsigned long)(drawUs / 1000 / frames), (unsigned long)(pushUs / 1000 / frames), screen, (int)pos.source, pos.lat, pos.lon, demoPos ? "demo" : "",
                  dStations.count, dStations.total, dAircraft.total, trail.count(), (long long)time(nullptr), (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxAllocHeap());
    lastFlush = now;
    frames = 0;
    drawUs = pushUs = 0;
  }

  UiState s = baseState(pos);
  // The setup screen shows while the access point is up and the device has no internet yet.
  s.portal = portalApActive() && !dNet.wifi;
  snprintf(s.portalSsid, sizeof s.portalSsid, "%s", portalSsid());
  snprintf(s.portalIp, sizeof s.portalIp, "192.168.4.1");
  updateBacklight(s.localMin);
  render(s);
  portalLoop();
}
