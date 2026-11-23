// Network task (core 0): Wi-Fi, clock, and the three data sources. Every request is HTTPS
// (trust store: ca_roots.h) and is parsed as a stream (parse.h), so a 60 KB answer never sits in RAM.
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
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

// Cloudflare hosts (aprs.world) need ca_roots.h; api.adsb.lol fails mbedTLS verification against those roots yet passes
// with the ESP-IDF bundle, which holds its ISRG roots too.
template <class Fn>
bool httpGet(const char* url, Fn&& parseBody, bool idfBundle = false) {
  NetworkClientSecure tls;
  if (idfBundle) tls.useBuiltinCACertBundle();
  else tls.setCACert(kCaRoots);
  HTTPClient http;
  static const char* kHeaders[] = {"Transfer-Encoding"};
  http.collectHeaders(kHeaders, 1);
  http.setTimeout(10000);
  http.setUserAgent(kUserAgent);
  http.setReuse(false);
  if (!http.begin(tls, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    NetReader r{http.getStream(), http.header("Transfer-Encoding").equalsIgnoreCase("chunked")};
    ok = parseBody(r);
  }
  char err[100] = "";
  if (code < 0) tls.lastError(err, sizeof err);
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
  bool ok = httpGet(url, [&](NetReader& r) { return parse::aircraft(r, p.lat, p.lon, buf); }, true);
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
  uint32_t bootMs = millis(), lastIp = 0, lastFollow = 0, lastStations = 0, lastAir = 0;
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
    if (!ipValid && (lastIp == 0 || now - lastIp > 120000)) {
      lastIp = now;
      fetchIpPosition();
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
