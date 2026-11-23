// APRS-IS text protocol pieces that are pure functions: passcode, position and message packets, message parsing and
// SmartBeaconing. Plain C++ so sim/ checks them on the host.
#pragma once
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "geo.h"

namespace aprs {

// aprs.world's experimental-range tocall, shared by its apps (backend tocallRegistry: APZAPW → "APRS World").
constexpr const char* kTocall = "APZAPW";

// The APRS-IS passcode: a 15-bit hash of the base callsign (no SSID), case-insensitive. Not a secret.
inline int passcode(const char* call) {
  char base[10];
  int n = 0;
  while (call[n] && call[n] != '-' && n < 9) {
    base[n] = (char)toupper((unsigned char)call[n]);
    n++;
  }
  uint16_t hash = 0x73e2;
  for (int i = 0; i < n; i += 2) {
    hash ^= (uint16_t)(base[i] << 8);
    if (i + 1 < n) hash ^= (uint16_t)base[i + 1];
  }
  return hash & 0x7fff;
}

inline bool sameCall(const char* a, const char* b) {
  if (!a || !b || !*a || !*b) return false;
  while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) a++, b++;
  return !*a && !*b;
}

// "DDMM.mmN" / "DDDMM.mmE"
inline void latLon(char* lat, char* lon, double la, double lo) {
  char ns = la < 0 ? 'S' : 'N', ew = lo < 0 ? 'W' : 'E';
  la = fabs(la);
  lo = fabs(lo);
  int ld = (int)la, od = (int)lo;
  double lm = (la - ld) * 60, om = (lo - od) * 60;
  if (lm >= 59.995) { ld++; lm = 0; }
  if (om >= 59.995) { od++; om = 0; }
  sprintf(lat, "%02d%05.2f%c", ld, lm, ns);
  sprintf(lon, "%03d%05.2f%c", od, om, ew);
}

// Uncompressed position with course/speed and altitude, messaging capable ('='):
// CALL>APZAPW,TCPIP*:=4054.83N/02858.70E>090/036/A=000210 comment
inline int positionPacket(char* out, size_t cap, const char* call, double lat, double lon, float course, float speedKmh, float altM,
                          const char* symbol, const char* comment) {
  char la[12], lo[12], ext[24] = "", alt[12] = "";
  latLon(la, lo, lat, lon);
  if (course >= 0 && speedKmh >= 0) {
    int c = (int)lroundf(course) % 360;
    snprintf(ext, sizeof ext, "%03d/%03d", c == 0 ? 360 : c, (int)lroundf(speedKmh / 1.852f));
  }
  if (!isnan(altM)) snprintf(alt, sizeof alt, "/A=%06ld", lroundf(altM * 3.28084f));
  return snprintf(out, cap, "%s>%s,TCPIP*:=%s%c%s%c%s%s %s", call, kTocall, la, symbol[0], lo, symbol[1], ext, alt, comment);
}

// ":TOCALL___:text{id"; addressee padded to 9 characters.
inline int messagePacket(char* out, size_t cap, const char* from, const char* to, const char* text, const char* id) {
  return snprintf(out, cap, "%s>%s,TCPIP*::%-9.9s:%s%s%s", from, kTocall, to, text, id && *id ? "{" : "", id ? id : "");
}
inline int ackPacket(char* out, size_t cap, const char* from, const char* to, const char* id) {
  return snprintf(out, cap, "%s>%s,TCPIP*::%-9.9s:ack%s", from, kTocall, to, id);
}

struct Parsed {
  char from[10] = {0}, addressee[10] = {0}, text[68] = {0}, id[6] = {0};
  bool ack = false, rej = false;
};

// A message line from APRS-IS ("SRC>DST,PATH::ADDRESSEE:text{id"). False for anything else, including comments and
// third-party ("}") traffic.
inline bool parseMessage(const char* line, Parsed& p) {
  p = Parsed{};
  if (!line || line[0] == '#') return false;
  const char* gt = strchr(line, '>');
  const char* colon = gt ? strchr(gt, ':') : nullptr;
  if (!gt || !colon || gt - line < 1 || gt - line > 9) return false;
  memcpy(p.from, line, gt - line);
  const char* info = colon + 1;
  if (info[0] != ':' || strlen(info) < 11 || info[10] != ':') return false;
  int n = 9;
  while (n > 0 && info[n] == ' ') n--;
  memcpy(p.addressee, info + 1, n);
  const char* body = info + 11;
  if ((strncmp(body, "ack", 3) == 0 || strncmp(body, "rej", 3) == 0) && strlen(body) <= 3 + 5) {
    (body[0] == 'a' ? p.ack : p.rej) = true;
    snprintf(p.id, sizeof p.id, "%s", body + 3);
    return true;
  }
  const char* brace = strrchr(body, '{');
  size_t len = brace ? (size_t)(brace - body) : strlen(body);
  if (len >= sizeof p.text) len = sizeof p.text - 1;
  memcpy(p.text, body, len);
  if (brace) {
    size_t idLen = strcspn(brace + 1, "}\r\n");  // reply-ack form "{MM}AA": the id is MM
    if (idLen >= sizeof p.id) idLen = sizeof p.id - 1;
    memcpy(p.id, brace + 1, idLen);
  }
  return true;
}

// SmartBeaconing (HamHUD defaults, km/h): fast rate above `highKmh`, slow rate below `lowKmh`, proportional between,
// and corner pegging when the course changes more than turnMin + turnSlope / speed(mph) degrees.
struct SmartBeacon {
  float lowKmh = 10, highKmh = 90;
  uint32_t slowS = 1800, fastS = 60, turnTimeS = 15;
  float turnMin = 20, turnSlope = 255;
  bool sent = false;
  uint32_t lastMs = 0;
  float lastCourse = -1;

  bool due(float speedKmh, float course, uint32_t nowMs) const {
    if (!sent) return true;
    float elapsed = (nowMs - lastMs) / 1000.f;
    float rate = speedKmh < lowKmh ? (float)slowS : speedKmh > highKmh ? (float)fastS : fastS * highKmh / speedKmh;
    if (elapsed >= rate) return true;
    if (speedKmh >= lowKmh && course >= 0 && lastCourse >= 0 && elapsed >= turnTimeS) {
      float threshold = turnMin + turnSlope / (speedKmh * 0.621371f);
      if (fabsf(geo::wrap360(course - lastCourse + 180) - 180) > threshold) return true;
    }
    return false;
  }
  void mark(float course, uint32_t nowMs) {
    sent = true;
    lastMs = nowMs;
    lastCourse = course;
  }
};

}  // namespace aprs
