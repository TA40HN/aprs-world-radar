// Network task (core 0): Wi-Fi, clock, and the three data sources. Every request is HTTPS
// (trust store: ca_roots.h) and is parsed as a stream (parse.h), so a 60 KB answer never sits in RAM.
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Sgp4.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <math.h>
#include <time.h>

#include "app_state.h"
#include "ca_roots.h"
#include "parse.h"

namespace {

constexpr const char* kUserAgent = "APRSWorldRadar/" FW_VERSION " (+https://github.com/aprs-world/aprs-world-radar)";

// Arduino clients return -1 from read() whenever the next TLS record has not arrived yet; the parsers need a reader
// that waits for data (up to the timeout) and reports -1 only at the real end of the body. It also undoes HTTP/1.1
// chunked transfer encoding (api.adsb.lol refuses HTTP/1.0 with 426).
struct NetReader {
  NetworkClient& c;
  bool chunked = false;
  uint32_t timeoutMs = 10000;
  uint32_t left = 0;  // bytes left in the current chunk
  bool started = false, done = false;

  bool wait() {
    uint32_t t0 = millis();
    while (!c.available()) {
      if (!c.connected() || millis() - t0 > timeoutMs) return false;
      delay(2);
    }
    return true;
  }
  int rawRead() { return wait() ? c.read() : -1; }
  // Moves to the next chunk: "\r\n" after the previous one, then "<hex size>[;ext]\r\n". Size 0 ends the body.
  bool nextChunk() {
    if (started && (rawRead() != '\r' || rawRead() != '\n')) return !(done = true);
    started = true;
    uint32_t size = 0;
    bool ext = false;
    for (int ch; (ch = rawRead()) >= 0;) {
      if (ch == '\n') break;
      if (ch == ';' || ch == '\r') ext = true;
      if (ext) continue;
      size = size * 16 + (ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
    }
    left = size;
    if (size == 0) done = true;
    return !done;
  }
  bool ready() { return !chunked || (!done && (left > 0 || nextChunk())); }
  int read() {
    if (!ready()) return -1;
    int ch = rawRead();
    if (chunked && ch >= 0) left--;
    return ch;
  }
  int peek() { return ready() && wait() ? c.peek() : -1; }
  size_t readBytes(char* buf, size_t n) {
    size_t i = 0;
    for (int ch; i < n && (ch = read()) >= 0;) buf[i++] = (char)ch;
    return i;
  }
};

// Trust per host: Cloudflare hosts (aprs.world) need ca_roots.h; api.adsb.lol fails mbedTLS verification against those
// roots yet passes with the ESP-IDF bundle, which holds its ISRG roots too. router.project-osrm.org speaks only TLS 1.3,
// which this mbedTLS build lacks, so it is fetched over plain HTTP (it carries nothing but a rounded demo position).
enum class Trust { Roots, Bundle, PlainHttp };

template <class Fn>
bool httpGet(const char* url, Fn&& parseBody, Trust trust = Trust::Roots) {
  NetworkClientSecure tls;
  NetworkClient plain;
  if (trust == Trust::Bundle) tls.useBuiltinCACertBundle();
  else if (trust == Trust::Roots) tls.setCACert(kCaRoots);
  HTTPClient http;
  static const char* kHeaders[] = {"Transfer-Encoding"};
  http.collectHeaders(kHeaders, 1);
  http.setTimeout(10000);
  http.setUserAgent(kUserAgent);
  http.setReuse(false);
  if (!(trust == Trust::PlainHttp ? http.begin(plain, url) : http.begin(tls, url))) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    NetReader r{http.getStream(), http.header("Transfer-Encoding").equalsIgnoreCase("chunked")};
    ok = parseBody(r);
  }
  char err[100] = "";
  if (code < 0 && trust != Trust::PlainHttp) tls.lastError(err, sizeof err);
  Serial.printf("[net] GET %s -> %d%s %s (heap %lu, block %lu, stack free %u)\n", url, code, code == 200 && !ok ? " parse failed" : "", err,
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  http.end();
  return ok;
}

// aprs.world bakes the visitor's Cloudflare geolocation into its HTML shell; it gives a first, city-level position
// with zero setup.
bool fetchIpPosition() {
  return httpGet("https://" WEB_HOST "/", [](NetReader& r) {
    if (!parse::skipTo(r, "__APRS_GEO__=")) return false;
    JsonDocument doc;
    if (deserializeJson(doc, r) != DeserializationError::Ok || doc["lat"].isNull()) return false;
    Lock l;
    g.ipLat = doc["lat"].as<double>();
    g.ipLon = doc["lon"].as<double>();
    g.ipValid = true;
    return true;
  });
}

// Queries carry the position rounded to ~1 km: enough for a 50 km radius, it hides the exact spot and lets
// aprs.world's cache (keyed on two decimals) answer nearby devices together. Distances use the exact position.
bool fetchStations(const Position& p) {
  static StationList buf;
  char url[160];
  snprintf(url, sizeof url, "https://" API_HOST "/api/stations/around?lat=%.2f&lng=%.2f&radius_km=%u&since=1800", p.lat, p.lon,
           (unsigned)cfg.aprsRadiusKm);
  bool ok = httpGet(url, [&](NetReader& r) { return parse::stations(r, p.lat, p.lon, epochNow(), cfg.followCall(), buf); });
  Lock l;
  buf.ok = ok;
  buf.fetchedMs = millis();
  if (ok || !g.stations.ok) g.stations = buf;
  g.dataVersion++;
  return ok;
}

bool fetchAircraft(const Position& p) {
  static AircraftList buf;
  char url[128];
  int nm = (int)ceilf(cfg.airRadiusKm / 1.852f);
  snprintf(url, sizeof url, "https://" ADSB_HOST "/v2/point/%.2f/%.2f/%d", p.lat, p.lon, nm > 250 ? 250 : nm);
  bool ok = httpGet(url, [&](NetReader& r) { return parse::aircraft(r, p.lat, p.lon, buf); }, Trust::Bundle);
  Lock l;
  buf.ok = ok;
  buf.fetchedMs = millis();
  if (ok || !g.aircraft.ok) g.aircraft = buf;
  g.dataVersion++;
  return ok;
}

bool fetchFollowed() {
  static FollowedStation buf;
  char url[96];
  snprintf(url, sizeof url, "https://" API_HOST "/api/stations/%s", cfg.followCall());
  bool ok = httpGet(url, [&](NetReader& r) { return parse::followed(r, epochNow(), buf); });
  if (!ok) return false;
  Lock l;
  g.me = buf;
  g.meFetchedMs = millis();
  g.dataVersion++;
  return true;
}

// Demo drive: a loop on real roads through three random points 1.5-3 km around the start, routed by the public OSRM
// demo server (OpenStreetMap data, © OpenStreetMap contributors). One request per boot; three tries at most.
bool fetchDemoRoute(double lat, double lon) {
  static float la[Shared::kRouteMax], lo[Shared::kRouteMax];
  char url[320];
  lat = round(lat * 1000) / 1000;  // ~100 m: the request goes out unencrypted
  lon = round(lon * 1000) / 1000;
  int n = snprintf(url, sizeof url, "http://router.project-osrm.org/route/v1/driving/%.5f,%.5f", lon, lat);
  float b0 = (float)(esp_random() % 360);
  for (int i = 0; i < 3; i++) {
    float b = (b0 + i * 120 + (float)(esp_random() % 41) - 20) * (float)geo::kDeg, km = 1.5f + (esp_random() % 1500) / 1000.f;
    n += snprintf(url + n, sizeof url - n, ";%.5f,%.5f", lon + km * sinf(b) / (111.32 * cos(lat * geo::kDeg)), lat + km * cosf(b) / 111.32);
  }
  snprintf(url + n, sizeof url - n, ";%.5f,%.5f?overview=full&geometries=polyline", lon, lat);
  int count = 0;
  bool ok = httpGet(url, [&](NetReader& r) { return (count = parse::route(r, la, lo, Shared::kRouteMax)) >= 2; }, Trust::PlainHttp);
  Lock l;
  if (ok) {
    memcpy(g.routeLat, la, sizeof la);
    memcpy(g.routeLon, lo, sizeof lo);
    g.routeCount = count;
  }
  return ok;
}

// Weather of the nearest APRS weather station in the current station list.
bool fetchWeather() {
  static Weather buf;
  buf = Weather{};
  {
    Lock l;
    for (int i = 0; i < g.stations.count; i++) {  // sorted by distance
      const Station& s = g.stations.items[i];
      if (s.kind != StationKind::Weather) continue;
      snprintf(buf.call, sizeof buf.call, "%s", s.call);
      buf.distKm = s.distKm;
      buf.bearing = s.bearing;
      break;
    }
  }
  if (!buf.call[0]) return false;
  char url[96];
  snprintf(url, sizeof url, "https://" API_HOST "/api/stations/%s/weather?hours=24", buf.call);
  bool ok = httpGet(url, [&](NetReader& r) { return parse::weather(r, epochNow(), buf); });
  Lock l;
  if (ok) g.wx = buf;
  g.dataVersion++;
  return ok;
}

// ---- satellites: TLEs from CelesTrak, passes by SGP4 (Hopperpop Sgp4, MIT) ----
struct Tle {
  char name[25], l1[130], l2[130];
};
Tle tles[3];
int tleCount = 0;

bool readLine(NetReader& r, char* out, size_t cap) {
  size_t n = 0;
  for (int ch; (ch = r.read()) >= 0 && ch != '\n';)
    if (ch != '\r' && n < cap - 1) out[n++] = (char)ch;
  out[n] = 0;
  while (n > 0 && out[n - 1] == ' ') out[--n] = 0;
  return n > 0;
}

bool fetchTles() {
  static Tle got[3];
  int n = 0;
  char list[sizeof cfg.sats];
  snprintf(list, sizeof list, "%s", cfg.sats);
  for (char* tok = strtok(list, ", "); tok && n < 3; tok = strtok(nullptr, ", ")) {
    long id = atol(tok);
    if (id <= 0) continue;
    char url[96];
    snprintf(url, sizeof url, "https://celestrak.org/NORAD/elements/gp.php?CATNR=%ld&FORMAT=TLE", id);
    Tle& t = got[n];
    bool ok = httpGet(url, [&](NetReader& r) {
      return readLine(r, t.name, sizeof t.name) && readLine(r, t.l1, sizeof t.l1) && readLine(r, t.l2, sizeof t.l2) && t.l1[0] == '1' &&
             t.l2[0] == '2' && strlen(t.l1) >= 69 && strlen(t.l2) >= 69;
    }, Trust::Bundle);
    if (ok) n++;
  }
  if (!n) return false;
  memcpy(tles, got, sizeof got);
  tleCount = n;
  return true;
}

// The next pass (or the one in progress) of each satellite above the horizon at `p`; the soonest wins.
void predictPasses(const Position& p) {
  int64_t now = epochNow();
  if (!tleCount || !now || !p.valid) return;
  static Sgp4 sgp;
  static SatPass best, cand;
  best = SatPass{};
  for (int i = 0; i < tleCount; i++) {
    static Tle t;
    t = tles[i];  // init() rewrites the TLE lines in place
    sgp.site(p.lat, p.lon, 0);
    if (!sgp.init(t.name, t.l1, t.l2)) continue;
    sgp.initpredpoint((unsigned long)(now - 1200), 0.0);  // 20 min back: a pass in progress is found too
    passinfo pass;
    bool found = false;
    for (int k = 0; k < 4 && !found; k++) {
      if (!sgp.nextpass(&pass, 20)) break;
      found = (pass.jdstop - 2440587.5) * 86400.0 > now;
    }
    if (!found) continue;
    cand = SatPass{};
    snprintf(cand.name, sizeof cand.name, "%.23s", tles[i].name);
    cand.aos = (int64_t)((pass.jdstart - 2440587.5) * 86400.0);
    cand.tca = (int64_t)((pass.jdmax - 2440587.5) * 86400.0);
    cand.los = (int64_t)((pass.jdstop - 2440587.5) * 86400.0);
    cand.maxEl = (float)pass.maxelevation;
    cand.azAos = (float)pass.azstart;
    cand.azLos = (float)pass.azstop;
    cand.n = SatPass::kTrack;
    for (int k = 0; k < cand.n; k++) {
      sgp.findsat((unsigned long)(cand.aos + (cand.los - cand.aos) * k / (cand.n - 1)));
      cand.az[k] = (float)sgp.satAz;
      cand.el[k] = (float)sgp.satEl;
    }
    cand.ok = true;
    if (!best.ok || cand.aos < best.aos) best = cand;
  }
  Lock l;
  g.sat = best;
  g.dataVersion++;
}

void updateStatus(bool connected) {
  Lock l;
  g.net.wifi = connected;
  if (connected) {
    snprintf(g.net.ssid, sizeof g.net.ssid, "%s", WiFi.SSID().c_str());
    snprintf(g.net.ip, sizeof g.net.ip, "%s", WiFi.localIP().toString().c_str());
    g.net.rssi = (int8_t)WiFi.RSSI();
  }
}

void netTask(void*) {
  WiFiMulti multi;
  bool any = false;
  for (int i = 0; i < Settings::kWifi; i++)
    if (cfg.ssid[i][0]) { multi.addAP(cfg.ssid[i], cfg.pass[i]); any = true; }
  if (!any) {
    Lock l;
    g.portalRequested = true;
  }
  uint32_t bootMs = millis(), lastIp = 0, lastFollow = 0, lastStations = 0, lastAir = 0, lastRoute = 0, lastWx = 0, lastTle = 0, lastPass = 0;
  int routeTries = 0;
  bool wxWanted = cfg.enabled(SCR_WX), satWanted = cfg.enabled(SCR_SAT) && cfg.sats[0];
  bool timeStarted = false, mdns = false;
  Position lastStationsPos;
  bool airWanted = cfg.enabled(SCR_AIR) || cfg.enabled(SCR_DRIVE);

  for (;;) {
    bool connected = WiFi.status() == WL_CONNECTED;
    if (!connected && any && !portalApActive()) {
      multi.run(10000);
      connected = WiFi.status() == WL_CONNECTED;
      if (!connected && millis() - bootMs > 60000) {
        Lock l;
        g.portalRequested = true;  // let the user fix the Wi-Fi from a phone
      }
    }
    updateStatus(connected);
    static bool wasConnected = false;
    if (connected != wasConnected) {
      wasConnected = connected;
      if (connected) Serial.printf("[net] Wi-Fi %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      else Serial.println("[net] Wi-Fi lost");
    }
    if (!connected) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    static uint32_t timeStartMs = 0;
    if (!timeStarted) {
      configTzTime(cfg.tz, "pool.ntp.org", "time.cloudflare.com");
      timeStarted = true;
      timeStartMs = millis();
    }
    // Certificate checks need the real date: until NTP (or the GPS) sets the clock every handshake fails.
    if (!epochNow() && millis() - timeStartMs < 20000) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    if (!mdns && MDNS.begin("aprs-radar")) {
      MDNS.addService("http", "tcp", 80);
      mdns = true;
    }
    uint32_t now = millis();
    bool ipValid;
    Position p;
    {
      Lock l;
      ipValid = g.ipValid;
      p = g.pos;
    }
    if (!ipValid && (lastIp == 0 || now - lastIp > 15000)) {
      lastIp = now;
      fetchIpPosition();
    }
    {
      bool want;
      double dl, dn;
      {
        Lock l;
        want = g.demoRouteWanted && !g.routeCount && !g.routeFailed;
        dl = g.demoLat;
        dn = g.demoLon;
      }
      if (want && (lastRoute == 0 || now - lastRoute > 20000)) {
        lastRoute = now;
        if (!fetchDemoRoute(dl, dn) && ++routeTries >= 3) {
          Lock l;
          g.routeFailed = true;
        }
      }
    }
    if (cfg.followCall()[0] && (lastFollow == 0 || now - lastFollow > FOLLOW_POLL_MS)) {
      lastFollow = now;
      fetchFollowed();
    }
    if (p.valid) {
      // Refetch on schedule, or sooner (but not more than every 10 s) after moving a tenth of the radius.
      bool moved = lastStations && geo::distanceKm(p.lat, p.lon, lastStationsPos.lat, lastStationsPos.lon) > cfg.aprsRadiusKm * 0.1;
      if (lastStations == 0 || now - lastStations > STATIONS_POLL_MS || (moved && now - lastStations > 10000)) {
        lastStations = now;
        lastStationsPos = p;
        fetchStations(p);
      }
      if (airWanted && (lastAir == 0 || now - lastAir > AIRCRAFT_POLL_MS)) {
        lastAir = now;
        fetchAircraft(p);
      }
      if (wxWanted && g.stations.ok && (lastWx == 0 || now - lastWx > 600000)) {
        lastWx = now;
        fetchWeather();
      }
      if (satWanted && (lastTle == 0 || now - lastTle > (tleCount ? 12UL * 3600000 : 120000))) {
        lastTle = now;
        if (fetchTles()) lastPass = 0;
      }
      if (satWanted && tleCount && (lastPass == 0 || now - lastPass > 60000)) {
        lastPass = now;
        predictPasses(p);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

}  // namespace

int64_t epochNow() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (int64_t)t : 0;
}

void netBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("aprs-world-radar");
  WiFi.setSleep(false);
  xTaskCreatePinnedToCore(netTask, "net", 10240, nullptr, 1, nullptr, 0);
}
