// Settings in NVS; drive trails in LittleFS (/t/<session>.jsonl).
// Trail file: line 1 {"started_at":<unix ms or 0>,"call":"<callsign>"}, then one line per 10 s chunk:
// [[0,<tenths of a second since start>,<lat*1e6>,<lon*1e6>,<accuracy m>],...]  (the "hareket izi" guide's format).
#include <LittleFS.h>
#include <Preferences.h>

#include "app_state.h"

#if __has_include("secrets.h") && !defined(RELEASE_BUILD)
#include "secrets.h"  // local, git-ignored defaults for your own build (include/secrets.example.h); never in a release
#endif

namespace {
constexpr uint32_t kSettingsVersion = 2;
Preferences prefs;
File session;
bool fsOk = false;

void applyDefaults(Settings& s) {
  s = Settings{};
#ifdef DEFAULT_WIFI_SSID
  snprintf(s.ssid[0], sizeof s.ssid[0], "%s", DEFAULT_WIFI_SSID);
  snprintf(s.pass[0], sizeof s.pass[0], "%s", DEFAULT_WIFI_PASS);
#endif
#ifdef DEFAULT_CALLSIGN
  snprintf(s.callsign, sizeof s.callsign, "%s", DEFAULT_CALLSIGN);
#endif
#ifdef DEFAULT_LANG
  s.lang = DEFAULT_LANG;
#endif
#ifdef DEFAULT_DEMO
  s.demo = DEFAULT_DEMO;
#endif
}

// Keeps the oldest sessions from filling the flash.
void pruneTrails() {
  while (LittleFS.usedBytes() > LittleFS.totalBytes() * 85 / 100) {
    File dir = LittleFS.open("/t");
    long oldest = -1;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      long n = atol(f.name());
      if (oldest < 0 || n < oldest) oldest = n;
    }
    dir.close();
    if (oldest < 0) return;
    char path[24];
    snprintf(path, sizeof path, "/t/%ld.jsonl", oldest);
    if (session && strcmp(session.path(), path) == 0) return;  // never delete the open one
    LittleFS.remove(path);
  }
}
}  // namespace

void loadSettings(Settings& s) {
  applyDefaults(s);
  prefs.begin("radar", true);
  if (prefs.getUInt("ver", 0) == kSettingsVersion && prefs.getBytesLength("cfg") == sizeof(Settings)) prefs.getBytes("cfg", &s, sizeof s);
  prefs.end();
}

void saveSettings(const Settings& s) {
  prefs.begin("radar", false);
  prefs.putBytes("cfg", &s, sizeof s);
  prefs.putUInt("ver", kSettingsVersion);
  prefs.end();
}

void storeBegin() {
  fsOk = LittleFS.begin(true);
  if (fsOk && !LittleFS.exists("/t")) LittleFS.mkdir("/t");
}

void trailFlush(Trail& t, int64_t epochMs) {
  static TrailPoint buf[Trail::kPending];
  int n = t.takePending(buf, Trail::kPending);
  if (!fsOk || n == 0 || !cfg.record || cfg.demo) return;  // demo drives stay in RAM
  if (!session) {
    prefs.begin("radar", false);
    uint32_t seq = prefs.getUInt("tseq", 0) + 1;
    prefs.putUInt("tseq", seq);
    prefs.end();
    char path[24];
    snprintf(path, sizeof path, "/t/%lu.jsonl", (unsigned long)seq);
    session = LittleFS.open(path, "a", true);
    if (!session) return;
    // started_at = the wall-clock time of the first stored fix (t = 0).
    int64_t startMs = epochMs ? epochMs - (int64_t)buf[0].tDs * 100 : 0;
    session.printf("{\"started_at\":%lld,\"call\":\"%s\"}\n", (long long)startMs, cfg.followCall());
  }
  session.print('[');
  for (int i = 0; i < n; i++)
    session.printf("%s[0,%lu,%ld,%ld,%u]", i ? "," : "", (unsigned long)buf[i].tDs, (long)buf[i].latE6, (long)buf[i].lonE6, buf[i].accM);
  session.print("]\n");
  session.flush();
  pruneTrails();
}
