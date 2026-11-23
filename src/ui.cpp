#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fonts_data.h"
#include "geo.h"
#include "i18n.h"

namespace {

using lgfx::textdatum_t;
constexpr textdatum_t D_TL = lgfx::top_left, D_TC = lgfx::top_center, D_TR = lgfx::top_right, D_ML = lgfx::middle_left,
                      D_MC = lgfx::middle_center, D_MR = lgfx::middle_right, D_BL = lgfx::baseline_left,
                      D_BC = lgfx::baseline_center, D_BR = lgfx::baseline_right;

// Palette: the aprs.world app icon (teal signal on near-black teal).
constexpr Color BG = 0x020B0C, DISC = 0x031516, PANEL = 0x07191B, GRID = 0x0D3331, GRID_HI = 0x16514C, ACCENT = 0x2EE6C8,
                ACCENT_DIM = 0x16695E, TEXT = 0xE9FFFB, MUTED = 0x6F9A95, WARN = 0xFFB020, REDC = 0xFF4D5E, GOOD = 0x5BE37D;

constexpr int HEADER_H = 24;
constexpr int FOOTER_Y = 222;

// ---------------------------------------------------------------------------------------------------------- format
void fmtDist(char* out, size_t n, float km, bool metric) {
  if (!metric) {
    float mi = km * 0.621371f;
    if (mi < 10) snprintf(out, n, "%.1f mi", mi);
    else snprintf(out, n, "%d mi", (int)lroundf(mi));
  } else if (km < 1) {
    snprintf(out, n, "%d m", (int)(lroundf(km * 100) * 10));
  } else if (km < 10) {
    snprintf(out, n, "%.1f km", km);
  } else {
    snprintf(out, n, "%d km", (int)lroundf(km));
  }
}

void fmtAge(char* out, size_t n, int32_t s, uint8_t lang) {
  const char* ago = tr(S::Ago, lang);
  if (s < 0) snprintf(out, n, "?");
  else if (s < 60) snprintf(out, n, lang ? "%d sn %s" : "%ds %s", (int)s, ago);
  else if (s < 3600) snprintf(out, n, lang ? "%d dk %s" : "%d min %s", (int)(s / 60), ago);
  else if (s < 86400) snprintf(out, n, lang ? "%d sa %s" : "%d h %s", (int)(s / 3600), ago);
  else snprintf(out, n, lang ? "%d gün %s" : "%d d %s", (int)(s / 86400), ago);
}

void fmtAlt(char* out, size_t n, int32_t ft, uint8_t lang) {
  if (ft < 0) snprintf(out, n, "%s", tr(S::Ground, lang));
  else if (ft >= 10000) snprintf(out, n, "FL%03d", (int)(ft / 100));
  else snprintf(out, n, "%d ft", (int)(ft / 50 * 50));
}

void fmtSpeed(char* out, size_t n, float kmh, bool metric) {
  if (kmh < 0) snprintf(out, n, "--");
  else snprintf(out, n, "%d", (int)lroundf(metric ? kmh : kmh * 0.621371f));
}

// ---------------------------------------------------------------------------------------------------------- colors
Color kindColor(StationKind k) {
  switch (k) {
    case StationKind::Fixed: return 0x2EE6C8;
    case StationKind::Mobile: return 0xFFD166;
    case StationKind::Igate: return 0x4CC9F0;
    case StationKind::Digi: return 0xB388FF;
    case StationKind::Weather: return 0x7CFC9A;
    case StationKind::Aircraft:
    case StationKind::Balloon: return 0xFF8C42;
    case StationKind::Boat: return 0x5DADE2;
    case StationKind::Object: return 0xF15BB5;
    default: return 0x9FB8B4;
  }
}

Color altColor(int32_t ft) {
  if (ft < 0) return 0x8A9A98;
  if (ft < 5000) return 0xFF8C42;
  if (ft < 15000) return 0xFFD166;
  if (ft < 30000) return 0x4CC9F0;
  return 0xB388FF;
}

// ---------------------------------------------------------------------------------------------------------- shapes
float rad(float deg) { return deg * (float)geo::kDeg; }

// The aprs.world mark: an "A" under two signal arcs, a dot between its legs. `s` = pixels per unit (1 -> ~120 px tall).
// `reveal` 0..1 builds it up (dot, legs, inner arc, outer arc).
void logo(Canvas& c, float cx, float cy, float s, float reveal = 1, Color col = ACCENT) {
  float w = 3.6f * s;
  auto part = [&](float from) { float t = (reveal - from) / 0.2f; return t < 0 ? 0.f : t > 1 ? 1.f : t; };
  float pDot = part(0), pLegs = part(0.15f), pIn = part(0.4f), pOut = part(0.6f);
  if (pDot > 0) c.dot(cx, cy + 47 * s, 8 * s * pDot, mix(BG, col, pDot));
  if (pLegs > 0) {
    Color lc = mix(BG, TEXT, pLegs);
    c.wideLine(cx, cy - 20 * s, cx - 30 * s, cy + 52 * s, w, lc);
    c.wideLine(cx, cy - 20 * s, cx + 30 * s, cy + 52 * s, w, lc);
  }
  float acx = cx, acy = cy - 4 * s;
  for (int i = 0; i < 2; i++) {
    float p = i == 0 ? pIn : pOut;
    if (p <= 0) continue;
    float r = i == 0 ? 30 * s : 56 * s;
    Color ac = mix(BG, col, p);
    c.fillArc((int)acx, (int)acy, (int)(r - w), (int)(r + w), 205, 335, ac);
    for (float a : {205.f, 335.f}) c.dot(acx + r * cosf(rad(a)), acy + r * sinf(rad(a)), w, ac);
  }
}

void wifiIcon(Canvas& c, int x, int y, bool on, int8_t rssi) {
  int bars = !on ? 0 : rssi > -60 ? 3 : rssi > -72 ? 2 : 1;
  for (int i = 0; i < 3; i++) {
    Color col = i < bars ? TEXT : GRID_HI;
    int r = 3 + i * 3;
    c.fillArc(x, y, r, r + 1, 225, 315, col);
  }
  c.dot(x, y - 1, 1.2f, on ? TEXT : GRID_HI);
}

// Plane silhouette, nose along `angleDeg` (0 = up, clockwise).
void plane(Canvas& c, float x, float y, float angleDeg, float s, Color col) {
  float a = rad(angleDeg), ca = cosf(a), sa = sinf(a);
  auto P = [&](float px, float py, float& ox, float& oy) { ox = x + (px * ca - py * sa) * s; oy = y + (px * sa + py * ca) * s; };
  float x0, y0, x1, y1, x2, y2;
  float x3, y3;
  P(-1.1f, -6.5f, x0, y0); P(1.1f, -6.5f, x1, y1); P(1.1f, 6, x2, y2); P(-1.1f, 6, x3, y3);
  c.triangle(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), lroundf(x2), lroundf(y2), col);
  c.triangle(lroundf(x0), lroundf(y0), lroundf(x2), lroundf(y2), lroundf(x3), lroundf(y3), col);
  P(-7, 1.5f, x0, y0); P(7, 1.5f, x1, y1); P(0, -3, x2, y2);
  c.triangle(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), lroundf(x2), lroundf(y2), col);
  P(-3, 6.5f, x0, y0); P(3, 6.5f, x1, y1); P(0, 3.5f, x2, y2);
  c.triangle(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), lroundf(x2), lroundf(y2), col);
}

// Arrow pointing along `angleDeg` (0 = up).
void arrow(Canvas& c, float x, float y, float angleDeg, float len, Color col) {
  float a = rad(angleDeg), dx = sinf(a), dy = -cosf(a);
  float tx = x + dx * len / 2, ty = y + dy * len / 2;
  c.wideLine(x - dx * len / 2, y - dy * len / 2, tx - dx * 3, ty - dy * 3, 1.4f, col);
  float px = -dy, py = dx;
  c.triangle(lroundf(tx), lroundf(ty), lroundf(tx - dx * 7 + px * 4.5f), lroundf(ty - dy * 7 + py * 4.5f),
             lroundf(tx - dx * 7 - px * 4.5f), lroundf(ty - dy * 7 - py * 4.5f), col);
}

// ---------------------------------------------------------------------------------------------------------- chrome
void header(Canvas& c, const UiState& s, const char* title) {
  if (!c.rows(0, HEADER_H)) return;
  c.gradientV(0, 0, 240, HEADER_H, 0x0B2B2A, BG);
  c.hline(0, HEADER_H - 1, 240, GRID_HI);
  logo(c, 12, 12, 0.15f);
  c.text(title, 25, 12, &fontTitle, TEXT, D_ML);
  char clk[12];
  if (s.localMin >= 0) snprintf(clk, sizeof clk, "%02d:%02d", (s.localMin / 60) % 24, s.localMin % 60);
  else snprintf(clk, sizeof clk, "--:--");
  int x = 236 - c.text(clk, 236, 12, &fontBody, TEXT, D_MR) - 12;
  wifiIcon(c, x, 17, s.net && s.net->wifi, s.net ? s.net->rssi : 0);
  bool fix = s.pos && s.pos->source == PosSource::Gps;
  if (fix || s.gpsSats >= 0) {
    c.text("GPS", x - 14, 12, &fontSmall, fix ? GOOD : (s.nowMs / 500) % 2 ? WARN : MUTED, D_MR);
  }
}

void footer(Canvas& c, const char* right) {
  if (!c.rows(FOOTER_Y, 240)) return;
  c.hline(0, FOOTER_Y, 240, GRID);
  c.text("aprs.world", 6, 231, &fontSmall, ACCENT, D_ML);
  if (right && *right) c.text(right, 234, 231, &fontSmall, MUTED, D_MR);
}

void sourceLabel(char* out, size_t n, const UiState& s) {
  const Position& p = *s.pos;
  uint8_t L = s.cfg->lang;
  switch (p.source) {
    case PosSource::Gps:
      if (s.gpsSats < 0) snprintf(out, n, "GPS");
      else snprintf(out, n, "GPS %d sat", s.gpsSats);
      break;
    case PosSource::Follow: snprintf(out, n, "%s %s", tr(S::PosFollow, L), s.cfg->followCall()); break;
    case PosSource::Home: snprintf(out, n, "%s", tr(S::PosHome, L)); break;
    case PosSource::Ip: snprintf(out, n, "≈ IP"); break;
    default: out[0] = 0;
  }
}

void centerMessage(Canvas& c, int y, const char* line1, const char* line2) {
  c.text(line1, 120, y, &fontTitle, TEXT, D_MC);
  if (line2) c.text(line2, 120, y + 20, &fontSmall, MUTED, D_MC);
}

// ---------------------------------------------------------------------------------------------------------- radar
struct Radar {
  int cx = 120, cy = 123, R = 96;
  float rangeKm = 50;
  float rot = 0;  // heading-up rotation (course while moving)

  bool project(float distKm, float bearing, float& x, float& y) const {
    if (distKm > rangeKm) return false;
    float r = distKm / rangeKm * R, a = rad(bearing - rot);
    x = cx + r * sinf(a);
    y = cy - r * cosf(a);
    return true;
  }
};

float headingUp(const Position& p) { return p.valid && p.speedKmh > 5 && p.course >= 0 ? p.course : 0; }

// 0..1: how recently the sweep passed screen angle `ang` (1 = just now). Phosphor-style afterglow.
float glow(float sweep, float ang) { return 1.f - geo::wrap360(sweep - ang) / 360.f; }

void radarBase(Canvas& c, const Radar& r, float sweep, bool metric) {
  if (!c.rows(r.cy - r.R, r.cy + r.R)) return;
  c.disc(r.cx, r.cy, r.R, DISC);
  // Afterglow wedge as a triangle fan: fillArc scans the whole bounding box per segment, which cost ~200 ms a frame.
  constexpr int N = 18;
  constexpr float SPAN = 90;
  for (int k = N - 1; k >= 0; --k) {
    float a1 = rad(sweep - k * SPAN / N + 0.4f), a0 = rad(sweep - (k + 1) * SPAN / N - 0.4f);
    float t = 1.f - (float)k / N;
    c.triangle(r.cx, r.cy, lroundf(r.cx + sinf(a0) * r.R), lroundf(r.cy - cosf(a0) * r.R), lroundf(r.cx + sinf(a1) * r.R),
               lroundf(r.cy - cosf(a1) * r.R), mix(DISC, ACCENT, 0.34f * t * t));
  }
  for (int i = 1; i <= 3; i++) c.circle(r.cx, r.cy, r.R * i / 3, i == 3 ? GRID_HI : GRID);
  c.circle(r.cx, r.cy, r.R + 1, GRID);
  for (int deg = 0; deg < 360; deg += 10) {
    float a = rad(deg - r.rot), len = deg % 90 == 0 ? 8 : deg % 30 == 0 ? 5 : 2;
    float sx = sinf(a), sy = -cosf(a);
    c.line(lroundf(r.cx + sx * (r.R - len)), lroundf(r.cy + sy * (r.R - len)), lroundf(r.cx + sx * r.R), lroundf(r.cy + sy * r.R),
           deg % 90 == 0 ? ACCENT_DIM : GRID_HI);
  }
  c.line(r.cx - r.R + 10, r.cy, r.cx + r.R - 10, r.cy, GRID);
  c.line(r.cx, r.cy - r.R + 10, r.cx, r.cy + r.R - 10, GRID);
  // North marker
  float na = rad(-r.rot);
  c.text("N", lroundf(r.cx + sinf(na) * (r.R - 15)), lroundf(r.cy - cosf(na) * (r.R - 15)), &fontSmall, ACCENT, D_MC);
  // Ring distances, along the lower-right diagonal
  for (int i = 1; i <= 2; i++) {
    char b[12];
    fmtDist(b, sizeof b, r.rangeKm * i / 3, metric);
    float rr = r.R * i / 3.f;
    c.text(b, lroundf(r.cx + rr * 0.7071f) + 2, lroundf(r.cy + rr * 0.7071f) + 1, &fontSmall, ACCENT_DIM, D_TL);
  }
  float sa = rad(sweep);
  int ex = lroundf(r.cx + sinf(sa) * r.R), ey = lroundf(r.cy - cosf(sa) * r.R);
  c.line(r.cx, r.cy, ex, ey, mix(ACCENT, TEXT, 0.35f));
  c.line(r.cx + 1, r.cy, ex + 1, ey, mix(DISC, ACCENT, 0.7f));
}

void ownMarker(Canvas& c, const Radar& r, bool moving, uint32_t nowMs) {
  int x = r.cx, y = r.cy;
  if (moving) {
    c.triangle(x, y - 9, x - 7, y + 7, x, y + 3, TEXT);
    c.triangle(x, y - 9, x, y + 3, x + 7, y + 7, mix(TEXT, ACCENT, 0.5f));
  } else {
    float p = (nowMs % 1800) / 1800.f;
    c.circle(x, y, (int)(4 + p * 12), mix(DISC, ACCENT, 1 - p));
    c.dot(x, y, 3.5f, TEXT);
  }
}

struct LabelBoxes {
  int n = 0;
  int16_t x0[16], y0[16], x1[16], y1[16];
  bool place(int ax, int ay, int bx, int by) {
    for (int i = 0; i < n; i++)
      if (ax < x1[i] && bx > x0[i] && ay < y1[i] && by > y0[i]) return false;
    if (n < 16) { x0[n] = ax; y0[n] = ay; x1[n] = bx; y1[n] = by; n++; }
    return true;
  }
};

// Puts a label beside (x,y) on the side facing the centre (so it stays on the disc), or the other side when that
// collides with an earlier label.
void radarLabel(Canvas& c, const Radar& r, LabelBoxes& boxes, float x, float y, const char* text, Color col) {
  int w = c.textWidth(text, &fontSmall), h = 11;
  bool rightFirst = x <= r.cx;
  for (int i = 0; i < 2; i++) {
    bool right = (i == 0) == rightFirst;
    int lx = right ? (int)x + 6 : (int)x - 6 - w, ly = (int)y - h / 2;
    float fx = lx + (right ? w : 0) - r.cx, fy = ly + h / 2.f - r.cy;
    if (fx * fx + fy * fy > (float)(r.R - 2) * (r.R - 2)) continue;
    if (!boxes.place(lx - 1, ly - 1, lx + w + 1, ly + h + 1)) continue;
    c.text(text, lx, ly, &fontSmall, col, D_TL);
    return;
  }
}

void hudCorners(Canvas& c, const char* bigTL, const char* smallTL, const char* bigTR, const char* smallTR, const char* smallBL,
                const char* bigBL, const char* smallBR, const char* bigBR) {
  if (bigTL) c.text(bigTL, 5, HEADER_H + 4, &fontMid, TEXT, D_TL);
  if (smallTL) c.text(smallTL, 5, HEADER_H + 25, &fontSmall, MUTED, D_TL);
  if (bigTR) c.text(bigTR, 235, HEADER_H + 4, &fontBody, TEXT, D_TR);
  if (smallTR) c.text(smallTR, 235, HEADER_H + 21, &fontSmall, MUTED, D_TR);
  if (smallBL) c.text(smallBL, 5, FOOTER_Y - 30, &fontSmall, MUTED, D_TL);
  if (bigBL) c.text(bigBL, 5, FOOTER_Y - 17, &fontBody, TEXT, D_TL);
  if (smallBR) c.text(smallBR, 235, FOOTER_Y - 30, &fontSmall, MUTED, D_TR);
  if (bigBR) c.text(bigBR, 235, FOOTER_Y - 17, &fontBody, TEXT, D_TR);
}

float sweepAngle(uint32_t nowMs) { return (nowMs % 4000) * (360.f / 4000.f); }

// Smallest round range that holds the k nearest targets (distances sorted ascending), capped at the search radius:
// in a city the nearest stations sit within a few km and would collapse into the centre of a 50 km disc.
float autoRange(float kthNearestKm, float maxKm) {
  static const float steps[] = {1, 2, 3, 5, 10, 15, 25, 50, 100, 200, 500};
  float need = kthNearestKm * 1.12f;
  for (float st : steps)
    if (st >= need) return st < maxKm ? st : maxKm;
  return maxKm;
}

void noPosition(Canvas& c, const UiState& s) {
  uint8_t L = s.cfg->lang;
  c.fillRoundRect(20, 100, 200, 46, 8, PANEL);
  centerMessage(c, 114, tr(S::NoPosition, L), s.cfg->gps ? tr(S::WaitingGps, L) : tr(S::SetCallsign, L));
}

void screenAprs(Canvas& c, const UiState& s) {
  const Settings& cfg = *s.cfg;
  uint8_t L = cfg.lang;
  header(c, s, tr(S::TitleAprs, L));
  Radar r;
  {
    const StationList& st = *s.stations;
    int k = st.count < 14 ? st.count - 1 : 13;
    r.rangeKm = k >= 0 ? autoRange(st.items[k].distKm, cfg.aprsRadiusKm) : cfg.aprsRadiusKm;
  }
  r.rot = headingUp(*s.pos);
  float sweep = sweepAngle(s.nowMs);
  radarBase(c, r, sweep, cfg.metric);
  const StationList& st = *s.stations;
  LabelBoxes boxes;
  boxes.place(r.cx - 9, r.cy - 9, r.cx + 9, r.cy + 9);
  int labels = 0;
  // Farthest first so the nearest blips end up on top.
  for (int i = st.count - 1; i >= 0; --i) {
    const Station& t = st.items[i];
    float x, y;
    if (!r.project(t.distKm, t.bearing, x, y)) continue;
    float g = glow(sweep, t.bearing - r.rot);
    Color k = kindColor(t.kind);
    float d = 360.f * (1 - g);
    if (d < 45) c.circle(lroundf(x), lroundf(y), (int)(3 + d * 0.22f), mix(DISC, k, (1 - d / 45) * 0.8f));
    c.dot(x, y, t.kind == StationKind::Mobile ? 3.2f : 2.6f, mix(scale(k, 0.35f), mix(k, TEXT, 0.3f), g * g));
  }
  for (int i = 0; i < st.count && labels < 7; i++) {
    const Station& t = st.items[i];
    float x, y;
    if (!r.project(t.distKm, t.bearing, x, y)) continue;
    float g = glow(sweep, t.bearing - r.rot);
    radarLabel(c, r, boxes, x, y, t.call, mix(MUTED, TEXT, g));
    labels++;
  }
  if (s.pos->valid) ownMarker(c, r, r.rot != 0, s.nowMs);

  char count[8], range[12], src[28], nd[16], near[32];
  snprintf(count, sizeof count, "%d", st.total);
  fmtDist(range, sizeof range, r.rangeKm, cfg.metric);
  sourceLabel(src, sizeof src, s);
  const Station* n = st.count ? &st.items[0] : nullptr;
  if (n) {
    fmtDist(nd, sizeof nd, n->distKm, cfg.metric);
    snprintf(near, sizeof near, "%s %s", nd, geo::compass8(n->bearing));
  }
  hudCorners(c, st.ok ? count : "--", tr(S::Stations, L), range, src, n ? tr(S::Nearest, L) : nullptr, n ? n->call : nullptr,
             n ? near : nullptr, nullptr);
  if (!s.pos->valid) noPosition(c, s);
  else if (st.ok && st.count == 0) centerMessage(c, 170, tr(S::NoStations, L), nullptr);
  footer(c, nullptr);
}

void screenAir(Canvas& c, const UiState& s) {
  const Settings& cfg = *s.cfg;
  uint8_t L = cfg.lang;
  header(c, s, tr(S::TitleAir, L));
  Radar r;
  {
    const AircraftList& al = *s.aircraft;
    int k = al.count < 10 ? al.count - 1 : 9;
    r.rangeKm = k >= 0 ? autoRange(al.items[k].distKm, cfg.airRadiusKm) : cfg.airRadiusKm;
  }
  r.rot = headingUp(*s.pos);
  float sweep = sweepAngle(s.nowMs);
  radarBase(c, r, sweep, cfg.metric);
  const AircraftList& ac = *s.aircraft;
  LabelBoxes boxes;
  boxes.place(r.cx - 9, r.cy - 9, r.cx + 9, r.cy + 9);
  for (int i = ac.count - 1; i >= 0; --i) {
    const Aircraft& a = ac.items[i];
    float x, y, ang = a.bearing - r.rot;
    Color k = altColor(a.altFt);
    if (!r.project(a.distKm, a.bearing, x, y)) {
      // Beyond the ring: a dot on the rim shows where it is coming from.
      float aa = rad(ang);
      c.dot(r.cx + sinf(aa) * (r.R - 3), r.cy - cosf(aa) * (r.R - 3), 1.7f, scale(k, 0.8f));
      continue;
    }
    float g = glow(sweep, ang);
    float d = 360.f * (1 - g);
    if (d < 45) c.circle(lroundf(x), lroundf(y), (int)(5 + d * 0.25f), mix(DISC, k, (1 - d / 45) * 0.7f));
    if (a.gsKt > 0 && a.track >= 0) {
      // Where it will be in one minute, clipped at the ring.
      float len = a.gsKt * 1.852f / 60.f / r.rangeKm * r.R, ta = rad(a.track - r.rot);
      float dx = sinf(ta) * len, dy = -cosf(ta) * len, ox = x - r.cx, oy = y - r.cy;
      float A = dx * dx + dy * dy, B = 2 * (ox * dx + oy * dy), C = ox * ox + oy * oy - (float)(r.R - 1) * (r.R - 1);
      float disc = B * B - 4 * A * C, t = 1;
      if (A > 0 && disc >= 0) t = fminf(1.f, (-B + sqrtf(disc)) / (2 * A));
      if (t > 0) c.line(lroundf(x), lroundf(y), lroundf(x + dx * t), lroundf(y + dy * t), scale(k, 0.4f + 0.4f * g));
    }
    plane(c, x, y, a.track < 0 ? 0 : a.track - r.rot, 0.9f, mix(scale(k, 0.55f), k, g));
  }
  int labels = 0;
  for (int i = 0; i < ac.count && labels < 6; i++) {
    const Aircraft& a = ac.items[i];
    float x, y;
    if (!r.project(a.distKm, a.bearing, x, y)) continue;
    radarLabel(c, r, boxes, x, y, a.flight, mix(MUTED, TEXT, glow(sweep, a.bearing - r.rot)));
    labels++;
  }
  if (s.pos->valid) ownMarker(c, r, r.rot != 0, s.nowMs);

  char count[8], range[12], nd[16], info[32], alt[12];
  snprintf(count, sizeof count, "%d", ac.total);
  fmtDist(range, sizeof range, r.rangeKm, cfg.metric);
  const Aircraft* n = ac.count ? &ac.items[0] : nullptr;
  if (n) {
    fmtDist(nd, sizeof nd, n->distKm, cfg.metric);
    fmtAlt(alt, sizeof alt, n->altFt, L);
    snprintf(info, sizeof info, "%s%s%s", n->type, n->type[0] ? " · " : "", alt);
  }
  hudCorners(c, ac.ok ? count : "--", tr(S::Aircraft, L), range, "adsb.lol", n ? info : nullptr, n ? n->flight : nullptr,
             n ? geo::compass8(n->bearing) : nullptr, n ? nd : nullptr);
  if (!s.pos->valid) noPosition(c, s);
  else if (ac.ok && ac.count == 0) centerMessage(c, 170, tr(S::NoAircraft, L), nullptr);
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- drive
void headingTape(Canvas& c, int y, float course) {
  constexpr int X0 = 10, W = 220, H = 24;
  constexpr float SPAN = 120;  // degrees across the tape
  c.fillRoundRect(X0, y, W, H, 6, PANEL);
  bool known = course >= 0;
  float hc = known ? course : 0;
  for (int d = (int)floorf((hc - SPAN / 2) / 5) * 5; d <= hc + SPAN / 2; d += 5) {
    float x = X0 + W / 2 + (d - hc) * (W / SPAN);
    if (x < X0 + 6 || x > X0 + W - 6) continue;
    int dd = ((d % 360) + 360) % 360;
    if (dd % 45 == 0) {
      c.text(geo::compass8((float)dd), lroundf(x), y + H / 2 + 1, &fontSmall, dd == 0 ? ACCENT : TEXT, D_MC);
    } else {
      int len = dd % 15 == 0 ? 7 : 3;
      c.vline(lroundf(x), y + H - 3 - len, len, dd % 15 == 0 ? MUTED : GRID_HI);
    }
  }
  c.triangle(120, y + 5, 115, y - 3, 125, y - 3, known ? ACCENT : MUTED);
}

void driveTile(Canvas& c, int x, int y, const char* head, const char* name, const char* dist, float relBearing, bool has, Color col) {
  constexpr int W = 112, H = 46;
  c.fillRoundRect(x, y, W, H, 8, PANEL);
  c.fillRect(x, y + 8, 3, H - 16, col);
  c.text(head, x + 10, y + 6, &fontSmall, MUTED, D_TL);
  if (!has) return;
  c.text(name, x + 10, y + 19, &fontBody, TEXT, D_TL);
  c.text(dist, x + 10, y + 34, &fontSmall, col, D_TL);
  arrow(c, x + W - 16, y + H / 2 + 3, relBearing, 18, col);
}

void screenDrive(Canvas& c, const UiState& s) {
  const Settings& cfg = *s.cfg;
  uint8_t L = cfg.lang;
  const Position& p = *s.pos;
  header(c, s, tr(S::TitleDrive, L));
  // REC + trip, position source
  const Trail& t = *s.trail;
  if (cfg.record && t.recording() && t.count() > 0) {
    if ((s.nowMs / 600) % 2) c.dot(10, HEADER_H + 11, 3.5f, REDC);
    char trip[24], d[12];
    fmtDist(d, sizeof d, (float)t.distanceKm(), cfg.metric);
    snprintf(trip, sizeof trip, "%s  %s", tr(S::Rec, L), d);
    c.text(trip, 18, HEADER_H + 11, &fontSmall, TEXT, D_ML);
  }
  char src[28];
  sourceLabel(src, sizeof src, s);
  c.text(src, 235, HEADER_H + 11, &fontSmall, MUTED, D_MR);

  char spd[8];
  fmtSpeed(spd, sizeof spd, p.valid ? p.speedKmh : -1, cfg.metric);
  c.text(spd, 120, 98, &fontHuge, TEXT, D_MC);
  c.text(cfg.metric ? "km/h" : "mph", 120, 134, &fontSmall, MUTED, D_MC);

  float course = p.valid ? p.course : -1;
  headingTape(c, 146, course);

  const StationList& st = *s.stations;
  const AircraftList& ac = *s.aircraft;
  char h1[24], h2[24], d1[20], d2[40];
  snprintf(h1, sizeof h1, "APRS · %d", st.total);
  snprintf(h2, sizeof h2, "%s · %d", tr(S::Aircraft, L), ac.total);
  float base = course >= 0 ? course : 0;
  const Station* ns = st.count ? &st.items[0] : nullptr;
  const Aircraft* na = ac.count ? &ac.items[0] : nullptr;
  if (ns) fmtDist(d1, sizeof d1, ns->distKm, cfg.metric);
  if (na) {
    char dd[12], alt[12];
    fmtDist(dd, sizeof dd, na->distKm, cfg.metric);
    fmtAlt(alt, sizeof alt, na->altFt, L);
    snprintf(d2, sizeof d2, "%s · %s", dd, alt);
  }
  driveTile(c, 4, 174, h1, ns ? ns->call : "", d1, ns ? ns->bearing - base : 0, ns != nullptr, ACCENT);
  driveTile(c, 124, 174, h2, na ? na->flight : "", d2, na ? na->bearing - base : 0, na != nullptr, 0xFFD166);
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- trail
void screenTrail(Canvas& c, const UiState& s) {
  const Settings& cfg = *s.cfg;
  uint8_t L = cfg.lang;
  header(c, s, tr(S::TitleTrail, L));
  const Trail& t = *s.trail;
  if (t.count() < 2) {
    logo(c, 120, 95, 0.45f, 1, ACCENT_DIM);
    char sats[24];
    if (s.gpsSats >= 0) snprintf(sats, sizeof sats, "GPS · %d sat", s.gpsSats);
    centerMessage(c, 150, tr(S::WaitingGps, L), s.gpsSats >= 0 ? sats : "NEO-6M → GPIO 32/33");
    footer(c, nullptr);
    return;
  }
  // Bounding box in metres around the first point.
  constexpr int MX0 = 8, MY0 = HEADER_H + 6, MW = 224, MH = 172;
  double lat0 = t.at(0).latE6 / 1e6, lon0 = t.at(0).lonE6 / 1e6, k = cos(lat0 * geo::kDeg);
  auto ex = [&](const TrailPoint& p) { return (p.lonE6 / 1e6 - lon0) * 111320.0 * k; };
  auto ny = [&](const TrailPoint& p) { return (p.latE6 / 1e6 - lat0) * 111320.0; };
  double minX = 0, maxX = 0, minY = 0, maxY = 0;
  for (int i = 0; i < t.count(); i++) {
    double x = ex(t.at(i)), y = ny(t.at(i));
    minX = fmin(minX, x); maxX = fmax(maxX, x); minY = fmin(minY, y); maxY = fmax(maxY, y);
  }
  double spanX = fmax(maxX - minX, 300), spanY = fmax(maxY - minY, 300);
  double sc = fmin(MW / spanX, MH / spanY) * 0.9;
  double cxm = (minX + maxX) / 2, cym = (minY + maxY) / 2;
  auto SX = [&](double x) { return (float)(MX0 + MW / 2 + (x - cxm) * sc); };
  auto SY = [&](double y) { return (float)(MY0 + MH / 2 - (y - cym) * sc); };
  // Grid
  static const int steps[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
  int grid = steps[0];
  for (int st : steps) { grid = st; if (st * sc >= 40) break; }
  for (double gx = floor((cxm - MW / 2 / sc) / grid) * grid; SX(gx) <= MX0 + MW; gx += grid)
    if (SX(gx) >= MX0) c.vline(lroundf(SX(gx)), MY0, MH, GRID);
  for (double gy = floor((cym - MH / 2 / sc) / grid) * grid; SY(gy) >= MY0; gy += grid)
    if (SY(gy) <= MY0 + MH) c.hline(MX0, lroundf(SY(gy)), MW, GRID);
  // Nearby stations inside the map
  const StationList& st = *s.stations;
  for (int i = 0; i < st.count; i++) {
    TrailPoint q{(int32_t)lroundf(st.items[i].lat * 1e6f), (int32_t)lroundf(st.items[i].lon * 1e6f), 0, 0};
    float x = SX(ex(q)), y = SY(ny(q));
    if (x > MX0 && x < MX0 + MW && y > MY0 && y < MY0 + MH) c.dot(x, y, 1.8f, scale(kindColor(st.items[i].kind), 0.6f));
  }
  // Path: whole drive thin and dim, the last minute thick and bright; gaps over 30 s stay open.
  uint32_t nowDs = t.nowDs(s.nowMs);
  for (int i = 1; i < t.count(); i++) {
    const TrailPoint &a = t.at(i - 1), &b = t.at(i);
    if (b.tDs - a.tDs > Trail::kGapDs) continue;
    float x0 = SX(ex(a)), y0 = SY(ny(a)), x1 = SX(ex(b)), y1 = SY(ny(b));
    if (nowDs - b.tDs <= 600) c.wideLine(x0, y0, x1, y1, 2.0f, ACCENT);
    else c.line(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), ACCENT_DIM);
  }
  const TrailPoint &first = t.at(0), &last = t.at(t.count() - 1);
  c.circle(lroundf(SX(ex(first))), lroundf(SY(ny(first))), 4, GOOD);
  float hx = SX(ex(last)), hy = SY(ny(last)), p = (s.nowMs % 1600) / 1600.f;
  c.circle(lroundf(hx), lroundf(hy), (int)(4 + p * 10), mix(BG, ACCENT, 1 - p));
  c.dot(hx, hy, 3.5f, TEXT);
  // Scale bar
  char gs[12];
  fmtDist(gs, sizeof gs, grid / 1000.f, cfg.metric);
  int bw = (int)lround(grid * sc);
  c.hline(MX0 + 4, MY0 + MH - 6, bw, TEXT);
  c.vline(MX0 + 4, MY0 + MH - 9, 6, TEXT);
  c.vline(MX0 + 4 + bw, MY0 + MH - 9, 6, TEXT);
  c.text(gs, MX0 + 8 + bw, MY0 + MH - 6, &fontSmall, TEXT, D_ML);
  // Stats
  uint32_t dur = t.durationS(s.nowMs);
  char dist[12], line[48];
  fmtDist(dist, sizeof dist, (float)t.distanceKm(), cfg.metric);
  float avg = dur > 0 ? (float)(t.distanceKm() / (dur / 3600.0)) : 0;
  snprintf(line, sizeof line, "%s  ·  %u:%02u  ·  %s %d %s", dist, (unsigned)(dur / 3600), (unsigned)(dur / 60 % 60),
           tr(S::Average, L), (int)lroundf(cfg.metric ? avg : avg * 0.621371f), cfg.metric ? "km/h" : "mph");
  c.text(line, 120, FOOTER_Y - 10, &fontSmall, TEXT, D_MC);
  if (cfg.record && (s.nowMs / 600) % 2) c.dot(230, HEADER_H + 10, 3.5f, REDC);
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- station
void screenStation(Canvas& c, const UiState& s) {
  const Settings& cfg = *s.cfg;
  uint8_t L = cfg.lang;
  header(c, s, tr(S::TitleStation, L));
  const char* call = cfg.followCall();
  if (!*call) {
    logo(c, 120, 100, 0.5f);
    centerMessage(c, 175, tr(S::SetCallsign, L), "aprs.world");
    footer(c, nullptr);
    return;
  }
  const FollowedStation& me = *s.me;
  c.text(call, 120, HEADER_H + 22, &fontBig, TEXT, D_MC);
  char line[64];
  if (me.valid) {
    int32_t age = me.ageS < 0 ? -1 : me.ageS;
    fmtAge(line, sizeof line, age, L);
    Color dot = age >= 0 && age < 600 ? GOOD : age >= 0 && age < 3600 ? WARN : MUTED;
    int w = c.textWidth(line, &fontBody);
    c.dot(120 - w / 2 - 8, HEADER_H + 46, 3.5f, dot);
    c.text(line, 120 + 4, HEADER_H + 46, &fontBody, TEXT, D_MC);
    if (me.device[0] || me.packets) {
      if (me.device[0] && me.packets) snprintf(line, sizeof line, "%s · %lu %s", me.device, (unsigned long)me.packets, tr(S::Packets, L));
      else if (me.device[0]) snprintf(line, sizeof line, "%s", me.device);
      else snprintf(line, sizeof line, "%lu %s", (unsigned long)me.packets, tr(S::Packets, L));
      c.text(line, 120, HEADER_H + 64, &fontSmall, MUTED, D_MC);
    }
  } else {
    c.text(tr(S::NoData, L), 120, HEADER_H + 46, &fontBody, MUTED, D_MC);
  }
  char url[64];
  snprintf(url, sizeof url, "https://" WEB_HOST "/station/%s", call);
  c.fillRoundRect(62, 100, 116, 116, 10, 0xFFFFFF);
  c.qr(url, 68, 106, 104);
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- promo
void screenPromo(Canvas& c, const UiState& s) {
  uint8_t L = s.cfg->lang;
  bool qrPhase = (s.nowMs / 5000) % 2 == 1;
  if (!qrPhase) {
    // Radio waves rippling out of the mark
    for (int i = 0; i < 3; i++) {
      float p = fmodf(s.nowMs / 2400.f + i / 3.f, 1.f);
      int r = (int)(40 + p * 80);
      c.circle(120, 86, r, mix(BG, ACCENT_DIM, 1 - p));
    }
    logo(c, 120, 80, 0.62f);
    c.text("APRS WORLD", 120, 148, &fontBig, TEXT, D_MC);
    c.text(tr(S::Tagline, L), 120, 172, &fontSmall, ACCENT, D_MC);
    char tag[48];
    snprintf(tag, sizeof tag, "%s · %s", tr(S::Free, L), tr(S::OpenSource, L));
    c.text(tag, 120, 190, &fontSmall, MUTED, D_MC);
  } else {
    c.text(tr(S::ScanLiveMap, L), 120, 20, &fontTitle, TEXT, D_MC);
    c.fillRoundRect(48, 36, 144, 144, 12, 0xFFFFFF);
    c.qr("https://" WEB_HOST, 54, 42, 132);
    c.text(WEB_HOST, 120, 198, &fontMid, ACCENT, D_MC);
  }
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- messages
// Draws `text` wrapped into at most `maxLines` lines of `width` px; the last line ends with "…" when cut.
int wrapText(Canvas& c, const char* text, int x, int y, int width, int maxLines, const FontData* f, Color col) {
  char line[72];
  int lines = 0;
  const char* p = text;
  while (*p && lines < maxLines) {
    size_t n = 0, lastSpace = 0;
    while (p[n] && n < sizeof line - 2) {
      line[n] = p[n];
      line[n + 1] = 0;
      if (Canvas::textWidth(line, f) > width) break;
      if (p[n] == ' ') lastSpace = n;
      n++;
    }
    bool more = p[n] != 0;
    if (more && lastSpace > 0 && lines < maxLines - 1) n = lastSpace;
    line[n] = 0;
    if (more && lines == maxLines - 1) {
      while (n > 0 && Canvas::textWidth(line, f) + Canvas::textWidth("…", f) > width) line[--n] = 0;
      strcat(line, "…");
    }
    c.text(line, x, y + lines * (f->ascent + f->descent + 1), f, col, D_TL);
    lines++;
    p += n;
    while (*p == ' ') p++;
  }
  return lines;
}

void screenMessages(Canvas& c, const UiState& s) {
  uint8_t L = s.cfg->lang;
  header(c, s, tr(S::TitleMsg, L));
  const AprsIsStatus& st = *s.aprsis;
  char status[48];
  Color dot = MUTED;
  if (!s.cfg->aprsis) snprintf(status, sizeof status, "%s", tr(S::AprsIsOff, L));
  else if (!st.connected) snprintf(status, sizeof status, "APRS-IS · %s", tr(S::AprsIsConnecting, L)), dot = WARN;
  else if (!st.verified) snprintf(status, sizeof status, "APRS-IS · %s", tr(S::Unverified, L)), dot = WARN;
  else snprintf(status, sizeof status, "APRS-IS · %s", st.server), dot = GOOD;
  c.dot(10, HEADER_H + 11, 3.5f, dot);
  c.text(status, 18, HEADER_H + 11, &fontSmall, TEXT, D_ML);
  c.text(s.cfg->loginCall(), 235, HEADER_H + 11, &fontSmall, MUTED, D_MR);
  const MessageLog& log = *s.messages;
  if (log.count == 0) {
    centerMessage(c, 118, tr(S::NoMessages, L), s.cfg->aprsis ? tr(S::MsgHint, L) : nullptr);
    footer(c, nullptr);
    return;
  }
  int y = HEADER_H + 26;
  for (int i = 0; i < log.count; i++) {
    const AprsMessage& m = log.items[i];
    bool fresh = !m.outgoing && i == 0 && s.epoch && s.epoch - m.epoch < 300;
    int lines = i == 0 ? 2 : 1, h = 30 + lines * 16;  // the newest gets two lines of text
    if (y + h > FOOTER_Y - 2) break;
    c.fillRoundRect(4, y, 232, h, 7, fresh ? 0x0B2E2B : PANEL);
    char who[24], age[24] = "";
    snprintf(who, sizeof who, m.outgoing ? "→ %s" : "%s", m.outgoing ? m.to : m.from);
    c.text(who, 11, y + 4, &fontTitle, m.outgoing ? MUTED : ACCENT, D_TL);
    if (m.outgoing) {
      S k = m.state == MsgState::Acked ? S::MsgDelivered : m.state == MsgState::Failed || m.state == MsgState::Rejected ? S::MsgFailed : S::MsgSending;
      snprintf(age, sizeof age, "%s", tr(k, L));
    } else if (s.epoch && m.epoch) {
      fmtAge(age, sizeof age, (int32_t)(s.epoch - m.epoch), L);
    }
    Color ageCol = m.outgoing && m.state == MsgState::Acked ? GOOD : m.outgoing && m.state == MsgState::Failed ? REDC : MUTED;
    c.text(age, 229, y + 6, &fontSmall, ageCol, D_TR);
    wrapText(c, m.text, 11, y + 22, 218, lines, &fontBody, TEXT);
    y += h + 4;
  }
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- weather
void screenWeather(Canvas& c, const UiState& s) {
  const Settings& cfg = *s.cfg;
  uint8_t L = cfg.lang;
  header(c, s, tr(S::TitleWx, L));
  const Weather& w = *s.wx;
  if (!w.ok) {
    logo(c, 120, 100, 0.45f, 1, ACCENT_DIM);
    centerMessage(c, 160, tr(S::NoWeather, L), nullptr);
    footer(c, nullptr);
    return;
  }
  char line[64], d[12], age[24] = "";
  fmtDist(d, sizeof d, w.distKm, cfg.metric);
  if (s.epoch) fmtAge(age, sizeof age, (int32_t)(s.epoch - w.sampleEpoch), L);
  snprintf(line, sizeof line, "%s · %s %s · %s", w.call, d, geo::compass8(w.bearing), age);
  c.text(line, 120, HEADER_H + 11, &fontSmall, MUTED, D_MC);
  // Temperature
  char t[12];
  if (isnan(w.tempC)) snprintf(t, sizeof t, "--");
  else snprintf(t, sizeof t, "%.0f", cfg.metric ? w.tempC : w.tempC * 1.8f + 32);
  int tw = c.text(t, 70, 80, &fontHuge, TEXT, D_MC);
  c.text(cfg.metric ? "°C" : "°F", 70 + tw / 2 + 2, 58, &fontMid, ACCENT, D_TL);
  // Wind rose: the arrow flies with the wind (from `windDir` toward the opposite side).
  const int wx = 190, wy = 78, wr = 26;
  c.circle(wx, wy, wr, GRID_HI);
  c.triangle(wx, wy - wr - 1, wx - 4, wy - wr - 8, wx + 4, wy - wr - 8, ACCENT);  // north
  if (!isnan(w.windDir) && !isnan(w.windMs) && w.windMs > 0.2f) arrow(c, wx, wy, w.windDir + 180, wr * 1.5f, ACCENT);
  else c.dot(wx, wy, 3, MUTED);
  char ws[24];
  if (isnan(w.windMs)) snprintf(ws, sizeof ws, "--");
  else snprintf(ws, sizeof ws, "%.0f %s", cfg.metric ? w.windMs * 3.6f : w.windMs * 2.23694f, cfg.metric ? "km/h" : "mph");
  c.text(ws, wx, wy + wr + 11, &fontBody, TEXT, D_MC);
  // Tiles
  struct Tile { S label; float v; const char* fmt; } tiles[] = {
    {S::Pressure, w.pressureHpa, "%.0f hPa"}, {S::Humidity, w.humidity, "%.0f%%"}, {S::Rain, isnan(w.rain24h) ? w.rain1h : w.rain24h, "%.1f mm"}};
  for (int i = 0; i < 3; i++) {
    int x = 4 + i * 78;
    c.fillRoundRect(x, 132, 74, 38, 7, PANEL);
    c.text(tr(tiles[i].label, L), x + 37, 141, &fontSmall, MUTED, D_MC);
    char v[16];
    if (isnan(tiles[i].v)) snprintf(v, sizeof v, "--");
    else snprintf(v, sizeof v, tiles[i].fmt, tiles[i].v);
    c.text(v, x + 37, 158, &fontBody, TEXT, D_MC);
  }
  // 24 h temperature, oldest left
  float lo = 1e9f, hi = -1e9f;
  for (float v : w.hourly)
    if (!isnan(v)) lo = fminf(lo, v), hi = fmaxf(hi, v);
  if (hi >= lo) {
    const int gx = 30, gy = 180, gw = 180, gh = 34;
    float span = fmaxf(hi - lo, 1.f);
    float px = -1, py = 0;
    for (int h = 23; h >= 0; h--) {
      float v = w.hourly[h];
      if (isnan(v)) { px = -1; continue; }
      float x = gx + (23 - h) * gw / 23.f, y = gy + gh - (v - lo) / span * gh;
      if (px >= 0) c.wideLine(px, py, x, y, 1.2f, ACCENT);
      px = x;
      py = y;
    }
    char a[8], b[8];
    snprintf(a, sizeof a, "%.0f°", cfg.metric ? hi : hi * 1.8f + 32);
    snprintf(b, sizeof b, "%.0f°", cfg.metric ? lo : lo * 1.8f + 32);
    c.text(a, gx - 4, gy, &fontSmall, MUTED, D_TR);
    c.text(b, gx - 4, gy + gh, &fontSmall, MUTED, D_MR);
    c.text("24h", gx + gw + 4, gy + gh, &fontSmall, MUTED, D_ML);
  }
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- satellite
void skyPoint(float az, float el, int cx, int cy, int R, float& x, float& y) {
  float r = R * (90 - fmaxf(el, 0)) / 90.f, a = rad(az);
  x = cx + r * sinf(a);
  y = cy - r * cosf(a);
}

void screenSatellite(Canvas& c, const UiState& s) {
  uint8_t L = s.cfg->lang;
  header(c, s, tr(S::TitleSat, L));
  const SatPass& p = *s.sat;
  if (!p.ok || !s.epoch) {
    logo(c, 120, 100, 0.45f, 1, ACCENT_DIM);
    centerMessage(c, 160, tr(S::NoSatData, L), s.cfg->sats);
    footer(c, nullptr);
    return;
  }
  // Sky plot: horizon ring, 30° and 60° rings, cardinal points, the predicted track.
  const int cx = 74, cy = 122, R = 58;
  c.disc(cx, cy, R, DISC);
  for (int el : {0, 30, 60}) c.circle(cx, cy, R * (90 - el) / 90, el == 0 ? GRID_HI : GRID);
  c.line(cx - R, cy, cx + R, cy, GRID);
  c.line(cx, cy - R, cx, cy + R, GRID);
  const char* card[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; i++) {
    float a = rad(i * 90.f);
    c.text(card[i], lroundf(cx + (R + 8) * sinf(a)), lroundf(cy - (R + 8) * cosf(a)), &fontSmall, i == 0 ? ACCENT : MUTED, D_MC);
  }
  float lx = 0, ly = 0;
  for (int k = 0; k < p.n; k++) {
    float x, y;
    skyPoint(p.az[k], p.el[k], cx, cy, R, x, y);
    if (k) c.wideLine(lx, ly, x, y, 1.3f, ACCENT);
    lx = x;
    ly = y;
  }
  float ax, ay, bx, by;
  skyPoint(p.az[0], p.el[0], cx, cy, R, ax, ay);
  skyPoint(p.az[p.n - 1], p.el[p.n - 1], cx, cy, R, bx, by);
  c.dot(ax, ay, 3.5f, GOOD);
  c.dot(bx, by, 3.5f, REDC);
  bool live = s.epoch >= p.aos && s.epoch <= p.los;
  if (live) {
    float f = (float)(s.epoch - p.aos) / (float)(p.los - p.aos) * (p.n - 1);
    int k = (int)f;
    float t = f - k, az = p.az[k], el = p.el[k];
    if (k + 1 < p.n) {
      az = p.az[k] + (geo::wrap360(p.az[k + 1] - p.az[k] + 180) - 180) * t;
      el = p.el[k] + (p.el[k + 1] - p.el[k]) * t;
    }
    float x, y, pulse = (s.nowMs % 1200) / 1200.f;
    skyPoint(az, el, cx, cy, R, x, y);
    c.circle(lroundf(x), lroundf(y), (int)(4 + pulse * 9), mix(DISC, TEXT, 1 - pulse));
    c.dot(x, y, 4, TEXT);
  }
  // Right column
  const int rx = 146;
  char name[16];
  snprintf(name, sizeof name, "%.15s", p.name);
  if (char* sp = strchr(name, ' ')) *sp = 0;  // "ISS (ZARYA)" → "ISS"
  c.text(name, rx, HEADER_H + 8, &fontMid, TEXT, D_TL);
  char a[32], b[32];
  if (live) {
    c.text(tr(S::Live, L), rx, HEADER_H + 36, &fontTitle, REDC, D_TL);
    int32_t left = (int32_t)(p.los - s.epoch);
    snprintf(a, sizeof a, "%d:%02d", (int)(left / 60), (int)(left % 60));
    c.text(a, rx, HEADER_H + 56, &fontBig, TEXT, D_TL);
  } else {
    c.text(tr(S::NextPass, L), rx, HEADER_H + 36, &fontSmall, MUTED, D_TL);
    int32_t wait = (int32_t)(p.aos - s.epoch);
    if (wait >= 3600) snprintf(a, sizeof a, "%d:%02d h", (int)(wait / 3600), (int)(wait / 60 % 60));
    else snprintf(a, sizeof a, "%d:%02d", (int)(wait / 60), (int)(wait % 60));
    c.text(a, rx, HEADER_H + 50, &fontBig, TEXT, D_TL);
  }
  int64_t aosL = p.aos + s.utcOffsetS, losL = p.los + s.utcOffsetS;
  snprintf(a, sizeof a, "%02d:%02d → %02d:%02d", (int)(aosL / 3600 % 24), (int)(aosL / 60 % 60), (int)(losL / 3600 % 24), (int)(losL / 60 % 60));
  c.text(a, rx, HEADER_H + 92, &fontBody, TEXT, D_TL);
  snprintf(b, sizeof b, "%s %.0f°", tr(S::MaxElevation, L), p.maxEl);
  c.text(b, rx, HEADER_H + 112, &fontBody, p.maxEl >= 30 ? GOOD : TEXT, D_TL);
  snprintf(b, sizeof b, "%s → %s", geo::compass8(p.azAos), geo::compass8(p.azLos));
  c.text(b, rx, HEADER_H + 132, &fontBody, MUTED, D_TL);
  if (strncmp(p.name, "ISS", 3) == 0) {
    c.text("APRS digi", rx, HEADER_H + 152, &fontSmall, MUTED, D_TL);
    c.text("145.825 MHz", rx, HEADER_H + 166, &fontSmall, ACCENT, D_TL);
  }
  footer(c, nullptr);
}

// ---------------------------------------------------------------------------------------------------------- setup / splash
void screenSetup(Canvas& c, const UiState& s) {
  uint8_t L = s.cfg->lang;
  header(c, s, tr(S::TitleSetup, L));
  c.text(tr(S::ConnectTo, L), 120, HEADER_H + 12, &fontSmall, MUTED, D_MC);
  c.text(s.portalSsid, 120, HEADER_H + 30, &fontMid, ACCENT, D_MC);
  char wifi[64];
  snprintf(wifi, sizeof wifi, "WIFI:T:nopass;S:%s;;", s.portalSsid);
  c.fillRoundRect(64, 76, 112, 112, 10, 0xFFFFFF);
  c.qr(wifi, 70, 82, 100);
  char open[48];
  snprintf(open, sizeof open, "%s  http://%s", tr(S::ThenOpen, L), s.portalIp);
  c.text(open, 120, 203, &fontSmall, TEXT, D_MC);
  footer(c, nullptr);
}

void splash(Canvas& c, const UiState& s) {
  float p = s.splash;
  for (int i = 0; i < 2; i++) {
    float q = fmodf(p * 1.6f + i * 0.5f, 1.f);
    c.circle(120, 92, (int)(30 + q * 90), mix(BG, GRID_HI, (1 - q) * (p > 0.5f ? 1 : p * 2)));
  }
  logo(c, 120, 86, 0.62f, fminf(1.f, p * 1.4f));
  float tp = fminf(1.f, fmaxf(0.f, (p - 0.55f) / 0.3f));
  c.text("APRS WORLD", 120, 156, &fontBig, mix(BG, TEXT, tp), D_MC);
  c.text(tr(S::Tagline, s.cfg->lang), 120, 180, &fontSmall, mix(BG, ACCENT, tp), D_MC);
  c.text(tr(S::ConnectingWifi, s.cfg->lang), 120, 214, &fontSmall, mix(BG, MUTED, tp), D_MC);
  c.text("v" FW_VERSION, 234, 232, &fontSmall, mix(BG, GRID_HI, tp), D_MR);
}

}  // namespace

void drawFrame(Canvas& c, const UiState& s) {
  c.clear(BG);
  if (s.splash >= 0) return splash(c, s);
  if (s.portal) return screenSetup(c, s);
  switch (s.screen) {
    case SCR_APRS: return screenAprs(c, s);
    case SCR_AIR: return screenAir(c, s);
    case SCR_DRIVE: return screenDrive(c, s);
    case SCR_TRAIL: return screenTrail(c, s);
    case SCR_STATION: return screenStation(c, s);
    case SCR_PROMO: return screenPromo(c, s);
    case SCR_MSG: return screenMessages(c, s);
    case SCR_WX: return screenWeather(c, s);
    case SCR_SAT: return screenSatellite(c, s);
    default: return screenAprs(c, s);
  }
}
