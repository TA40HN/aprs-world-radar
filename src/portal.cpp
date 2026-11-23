// Setup page and trail downloads. Served on the device's Wi-Fi address (http://aprs-radar.local) and, while the setup
// access point is up, on http://192.168.4.1 with a captive DNS so phones open it by themselves.
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app_state.h"

namespace {

WebServer server(80);
DNSServer dns;
bool apActive = false;
char apSsid[24] = {0};

String esc(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += *s;
    }
  }
  return o;
}

const char kHead[] PROGMEM = R"(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>APRS World Radar</title><style>
body{font:15px system-ui,sans-serif;background:#020b0c;color:#e9fffb;margin:0;padding:16px;max-width:560px}
h1{font-size:20px;color:#2ee6c8;margin:0 0 4px}h2{font-size:15px;color:#2ee6c8;margin:22px 0 8px;border-bottom:1px solid #16514c;padding-bottom:4px}
label{display:block;margin:8px 0 2px;color:#9fc7c2;font-size:13px}input,select{width:100%;box-sizing:border-box;padding:9px;border-radius:8px;border:1px solid #16514c;background:#07191b;color:#e9fffb;font-size:15px}
.row{display:flex;gap:8px}.row>div{flex:1}.chk label{display:flex;gap:8px;align-items:center;color:#e9fffb;font-size:15px}.chk input{width:auto}
button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:10px;background:#2ee6c8;color:#021;font-weight:700;font-size:16px}
a{color:#2ee6c8}.muted{color:#6f9a95;font-size:13px}</style></head><body>)";

void sendPage(const String& body) {
  String html;
  html.reserve(body.length() + 1200);
  html += FPSTR(kHead);
  html += body;
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

String field(const char* label, const char* name, const String& value, const char* type = "text", const char* extra = "") {
  return String("<label>") + label + "</label><input name=\"" + name + "\" type=\"" + type + "\" value=\"" + value + "\" " + extra + ">";
}

String check(const char* label, const char* name, bool on) {
  return String("<div class=chk><label><input type=checkbox name=\"") + name + "\"" + (on ? " checked" : "") + ">" + label + "</label></div>";
}

void handleRoot() {
  bool tr = cfg.lang == 1;
  String b;
  b.reserve(7000);
  b += "<h1>APRS World Radar</h1><div class=muted>v" FW_VERSION;
  {
    Lock l;
    if (g.net.wifi) b += String(" · ") + g.net.ssid + " · " + g.net.ip;
  }
  b += " · <a href=/trails>";
  b += tr ? "Sürüş izleri" : "Drive trails";
  b += "</a></div><form method=post action=/save>";
  b += tr ? "<h2>Wi-Fi (ev + telefon hotspot)</h2>" : "<h2>Wi-Fi (home + phone hotspot)</h2>";
  for (int i = 0; i < Settings::kWifi; i++) {
    char n1[4], n2[4];
    snprintf(n1, sizeof n1, "s%d", i);
    snprintf(n2, sizeof n2, "p%d", i);
    b += "<div class=row><div>" + field(tr ? "Ağ adı" : "Network", n1, esc(cfg.ssid[i])) + "</div><div>" +
         field(tr ? "Şifre" : "Password", n2, "", "password", cfg.pass[i][0] ? (tr ? "placeholder=\"(değişmedi)\"" : "placeholder=\"(unchanged)\"") : "") +
         "</div></div>";
  }
  b += tr ? "<h2>İstasyon ve konum</h2>" : "<h2>Station and position</h2>";
  b += field(tr ? "Çağrı işaretin (QR ve istasyon kartı)" : "Your callsign (QR and station card)", "call", esc(cfg.callsign), "text",
             "autocapitalize=characters");
  b += field(tr ? "GPS yokken konumu alınacak tracker (boşsa çağrı işaretin)" : "Tracker to use as position without GPS (empty: your callsign)",
             "follow", esc(cfg.follow), "text", "autocapitalize=characters");
  char lat[16] = "", lon[16] = "";
  if (cfg.homeSet) {
    snprintf(lat, sizeof lat, "%.5f", cfg.homeLat);
    snprintf(lon, sizeof lon, "%.5f", cfg.homeLon);
  }
  b += "<div class=row><div>" + field(tr ? "Ev enlemi" : "Home latitude", "lat", lat, "text", "inputmode=decimal") + "</div><div>" +
       field(tr ? "Ev boylamı" : "Home longitude", "lon", lon, "text", "inputmode=decimal") + "</div></div>";
  b += "<div class=muted>";
  b += tr ? "Sıra: GPS → takip edilen tracker → ev → internet bağlantısının konumu." : "Order: GPS → followed tracker → home → your connection's location.";
  b += "</div>";
  b += tr ? "<h2>Ekranlar</h2>" : "<h2>Screens</h2>";
  const char* names[SCR_COUNT][2] = {{"APRS radar", "APRS radar"}, {"Air traffic", "Hava trafiği"}, {"Drive", "Sürüş"},
                                     {"Trail", "İz"},              {"My station", "İstasyonum"},     {"aprs.world promo", "aprs.world tanıtım"}};
  for (int i = 0; i < SCR_COUNT; i++) {
    char n[8];
    snprintf(n, sizeof n, "scr%d", i);
    b += check(names[i][tr], n, cfg.enabled((ScreenId)i));
  }
  b += "<div class=row><div>" + field(tr ? "APRS yarıçapı (km)" : "APRS radius (km)", "ar", String(cfg.aprsRadiusKm), "number", "min=5 max=500") +
       "</div><div>" + field(tr ? "Uçak yarıçapı (km)" : "Aircraft radius (km)", "rr", String(cfg.airRadiusKm), "number", "min=5 max=460") +
       "</div></div>";
  b += field(tr ? "Ekran değiştirme (sn, 0 = sadece buton)" : "Change screen every (s, 0 = button only)", "cyc", String(cfg.cycleS), "number",
             "min=0 max=120");
  b += tr ? "<h2>Cihaz</h2>" : "<h2>Device</h2>";
  b += String("<label>") + (tr ? "Dil" : "Language") + "</label><select name=lang><option value=0" + (cfg.lang == 0 ? " selected" : "") +
       ">English</option><option value=1" + (cfg.lang == 1 ? " selected" : "") + ">Türkçe</option></select>";
  b += String("<label>") + (tr ? "Birimler" : "Units") + "</label><select name=units><option value=1" + (cfg.metric ? " selected" : "") +
       ">km, km/h</option><option value=0" + (!cfg.metric ? " selected" : "") + ">mi, mph</option></select>";
  b += check(tr ? "GPS modülü takılı (GPIO 32/33)" : "GPS module fitted (GPIO 32/33)", "gps", cfg.gps);
  b += check(tr ? "Sürüş izini kaydet" : "Record the drive trail", "rec", cfg.record);
  b += check(tr ? "Demo sürüş (sahte GPS, GPS modülü yokken)" : "Demo drive (simulated GPS, when no module is fitted)", "demo", cfg.demo);
  b += check(tr ? "Buzzer (yakına uçak gelince)" : "Buzzer (aircraft close by)", "buz", cfg.buzzer);
  b += "<div class=row><div>" + field(tr ? "Parlaklık %" : "Brightness %", "br", String(cfg.brightness), "number", "min=5 max=100") +
       "</div><div>" + field(tr ? "Gece parlaklığı %" : "Night brightness %", "nbr", String(cfg.nightBrightness), "number", "min=0 max=100") +
       "</div></div>";
  b += "<div class=row><div>" + field(tr ? "Gece başlar (saat)" : "Night from (hour)", "nf", String(cfg.nightFrom), "number", "min=0 max=23") +
       "</div><div>" + field(tr ? "Gece biter (saat)" : "Night until (hour)", "nt", String(cfg.nightTo), "number", "min=0 max=23") + "</div></div>";
  b += field(tr ? "Saat dilimi (POSIX TZ)" : "Time zone (POSIX TZ)", "tz", esc(cfg.tz));
  b += String("<button>") + (tr ? "Kaydet ve yeniden başlat" : "Save and restart") + "</button></form>";
  b += "<p class=muted>Live map: <a href=https://aprs.world>aprs.world</a> · aircraft: adsb.lol (ODbL)</p>";
  sendPage(b);
}

void copyArg(const char* name, char* dst, size_t cap) {
  String v = server.arg(name);
  v.trim();
  snprintf(dst, cap, "%s", v.c_str());
}

long argLong(const char* name, long lo, long hi, long fallback) {
  if (!server.hasArg(name) || server.arg(name).isEmpty()) return fallback;
  long v = server.arg(name).toInt();
  return v < lo ? lo : v > hi ? hi : v;
}

void handleSave() {
  Settings s = cfg;
  for (int i = 0; i < Settings::kWifi; i++) {
    char n1[4], n2[4];
    snprintf(n1, sizeof n1, "s%d", i);
    snprintf(n2, sizeof n2, "p%d", i);
    copyArg(n1, s.ssid[i], sizeof s.ssid[i]);
    if (!s.ssid[i][0]) s.pass[i][0] = 0;
    else if (!server.arg(n2).isEmpty()) snprintf(s.pass[i], sizeof s.pass[i], "%s", server.arg(n2).c_str());
  }
  copyArg("call", s.callsign, sizeof s.callsign);
  copyArg("follow", s.follow, sizeof s.follow);
  for (char* p = s.callsign; *p; p++) *p = (char)toupper(*p);
  for (char* p = s.follow; *p; p++) *p = (char)toupper(*p);
  String lat = server.arg("lat"), lon = server.arg("lon");
  lat.replace(',', '.');
  lon.replace(',', '.');
  double la = lat.toDouble(), lo = lon.toDouble();
  s.homeSet = !lat.isEmpty() && !lon.isEmpty() && la >= -90 && la <= 90 && lo >= -180 && lo <= 180 && !(la == 0 && lo == 0);
  s.homeLat = s.homeSet ? la : 0;
  s.homeLon = s.homeSet ? lo : 0;
  s.screens = 0;
  for (int i = 0; i < SCR_COUNT; i++) {
    char n[8];
    snprintf(n, sizeof n, "scr%d", i);
    if (server.hasArg(n)) s.screens |= 1 << i;
  }
  if (!s.screens) s.screens = 1 << SCR_APRS;
  s.aprsRadiusKm = (uint16_t)argLong("ar", 5, 500, s.aprsRadiusKm);
  s.airRadiusKm = (uint16_t)argLong("rr", 5, 460, s.airRadiusKm);
  s.cycleS = (uint8_t)argLong("cyc", 0, 120, s.cycleS);
  s.lang = (uint8_t)argLong("lang", 0, 1, s.lang);
  s.metric = argLong("units", 0, 1, 1) == 1;
  s.gps = server.hasArg("gps");
  s.record = server.hasArg("rec");
  s.demo = server.hasArg("demo");
  s.buzzer = server.hasArg("buz");
  s.brightness = (uint8_t)argLong("br", 5, 100, s.brightness);
  s.nightBrightness = (uint8_t)argLong("nbr", 0, 100, s.nightBrightness);
  s.nightFrom = (uint8_t)argLong("nf", 0, 23, s.nightFrom);
  s.nightTo = (uint8_t)argLong("nt", 0, 23, s.nightTo);
  copyArg("tz", s.tz, sizeof s.tz);
  if (!s.tz[0]) snprintf(s.tz, sizeof s.tz, "%s", Settings{}.tz);
  saveSettings(s);
  sendPage(cfg.lang == 1 ? "<h1>Kaydedildi</h1><p>Cihaz yeniden başlıyor…</p>" : "<h1>Saved</h1><p>Restarting…</p>");
  delay(800);
  ESP.restart();
}

void handleTrails() {
  String b = "<h1>";
  b += cfg.lang == 1 ? "Sürüş izleri" : "Drive trails";
  b += "</h1><p><a href=/>&larr;</a></p><ul>";
  File dir = LittleFS.open("/t");
  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    long id = atol(f.name());
    b += "<li>#" + String(id) + " · " + String(f.size() / 1024.0, 1) + " KB · <a href=\"/trail.gpx?id=" + id + "\">GPX</a> · <a href=\"/trail.json?id=" +
         id + "\">JSON</a></li>";
    n++;
  }
  b += "</ul>";
  if (!n) b += cfg.lang == 1 ? "<p class=muted>Henüz iz yok (GPS gerekir).</p>" : "<p class=muted>No trails yet (needs GPS).</p>";
  b += "<p class=muted>" + String(LittleFS.usedBytes() / 1024) + " / " + String(LittleFS.totalBytes() / 1024) + " KB</p>";
  sendPage(b);
}

bool openTrail(File& f) {
  char path[24];
  snprintf(path, sizeof path, "/t/%ld.jsonl", server.arg("id").toInt());
  f = LittleFS.open(path, "r");
  if (!f) server.send(404, "text/plain", "no such trail");
  return (bool)f;
}

void handleTrailJson() {
  File f;
  if (!openTrail(f)) return;
  server.streamFile(f, "application/x-ndjson");
  f.close();
}

void isoTime(char* out, size_t n, int64_t ms) {
  time_t t = (time_t)(ms / 1000);
  struct tm tm;
  gmtime_r(&t, &tm);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

// GPX 1.1, one <trkseg> per stretch without a gap over 30 s.
void handleTrailGpx() {
  File f;
  if (!openTrail(f)) return;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"aprs-radar-" + server.arg("id") + ".gpx\"");
  server.send(200, "application/gpx+xml", "");
  String head = f.readStringUntil('\n');
  JsonDocument meta;
  deserializeJson(meta, head);
  int64_t start = meta["started_at"] | (int64_t)0;
  server.sendContent(String("<?xml version=\"1.0\"?><gpx version=\"1.1\" creator=\"APRS World Radar\" xmlns=\"http://www.topografix.com/GPX/1/1\"><trk><name>") +
                     esc(meta["call"] | "") + "</name><trkseg>");
  JsonDocument chunk;
  uint32_t lastT = 0;
  bool first = true;
  char buf[160], ts[24];
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (deserializeJson(chunk, line) != DeserializationError::Ok) continue;
    String out;
    for (JsonArray p : chunk.as<JsonArray>()) {
      uint32_t t = p[1];
      if (!first && t - lastT > Trail::kGapDs) out += "</trkseg><trkseg>";
      first = false;
      lastT = t;
      int n = snprintf(buf, sizeof buf, "<trkpt lat=\"%.6f\" lon=\"%.6f\">", p[2].as<long>() / 1e6, p[3].as<long>() / 1e6);
      out += String(buf, n);
      if (start) {
        isoTime(ts, sizeof ts, start + (int64_t)t * 100);
        out += String("<time>") + ts + "</time>";
      }
      out += "</trkpt>";
    }
    server.sendContent(out);
  }
  server.sendContent("</trkseg></trk></gpx>");
  server.sendContent("");
  f.close();
}

void handleStatus() {
  JsonDocument d;
  {
    Lock l;
    d["fw"] = FW_VERSION;
    d["wifi"] = g.net.wifi;
    d["ip"] = g.net.ip;
    d["pos"]["valid"] = g.pos.valid;
    d["pos"]["source"] = (int)g.pos.source;
    d["pos"]["lat"] = g.pos.lat;
    d["pos"]["lon"] = g.pos.lon;
    d["stations"] = g.stations.total;
    d["aircraft"] = g.aircraft.total;
  }
  d["heap"] = ESP.getFreeHeap();
  d["heapBlock"] = ESP.getMaxAllocHeap();
  String s;
  serializeJson(d, s);
  server.send(200, "application/json", s);
}

}  // namespace

void portalBegin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof apSsid, "APRSWorld-%02X%02X", mac[4], mac[5]);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/trails", HTTP_GET, handleTrails);
  server.on("/trail.json", HTTP_GET, handleTrailJson);
  server.on("/trail.gpx", HTTP_GET, handleTrailGpx);
  server.on("/status.json", HTTP_GET, handleStatus);
  server.onNotFound([] {
    if (apActive) {  // captive portal: every probe URL lands on the setup page
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "not found");
    }
  });
  server.begin();
}

void portalStartAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid);
  delay(100);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
}

void portalLoop() {
  if (apActive) dns.processNextRequest();
  server.handleClient();
}

bool portalApActive() { return apActive; }
const char* portalSsid() { return apSsid; }
