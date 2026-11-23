// One horizontal strip of the 240x240 frame. Screens draw in full-frame coordinates; Canvas shifts y by the strip's
// offset, so the same drawing code fills strip 0 (y 0..119) and strip 1 (y 120..239). Everything outside is clipped.
#pragma once
#include <LovyanGFX.hpp>
#include <math.h>
#include <stdint.h>

#include "font.h"

using Color = uint32_t;  // 0xRRGGBB

inline Color rgb(uint8_t r, uint8_t g, uint8_t b) { return ((Color)r << 16) | ((Color)g << 8) | b; }
inline Color mix(Color a, Color b, float t) {  // t=0 -> a, t=1 -> b
  if (t <= 0) return a;
  if (t >= 1) return b;
  auto ch = [&](int s) { return (uint8_t)(((a >> s) & 0xFF) + (((int)((b >> s) & 0xFF) - (int)((a >> s) & 0xFF)) * t)); };
  return rgb(ch(16), ch(8), ch(0));
}
inline Color scale(Color c, float f) { return mix(0x000000, c, f); }

class Canvas {
 public:
  Canvas(lgfx::LGFX_Sprite& s, int oy) : s_(s), oy_(oy) {}
  lgfx::LGFX_Sprite& raw() { return s_; }
  int top() const { return oy_; }
  int bottom() const { return oy_ + s_.height(); }
  bool rows(int y0, int y1) const { return y1 >= oy_ && y0 < bottom(); }

  void clear(Color c) { s_.fillScreen(c); }
  void fillRect(int x, int y, int w, int h, Color c) { if (rows(y, y + h)) s_.fillRect(x, y - oy_, w, h, c); }
  void rect(int x, int y, int w, int h, Color c) { if (rows(y, y + h)) s_.drawRect(x, y - oy_, w, h, c); }
  void roundRect(int x, int y, int w, int h, int r, Color c) { if (rows(y, y + h)) s_.drawRoundRect(x, y - oy_, w, h, r, c); }
  void fillRoundRect(int x, int y, int w, int h, int r, Color c) { if (rows(y, y + h)) s_.fillSmoothRoundRect(x, y - oy_, w, h, r, c); }
  void gradientV(int x, int y, int w, int h, Color top, Color bottom) {
    if (rows(y, y + h)) s_.fillGradientRect(x, y - oy_, w, h, top, bottom, lgfx::VLINEAR);
  }
  void hline(int x, int y, int w, Color c) { if (rows(y, y)) s_.drawFastHLine(x, y - oy_, w, c); }
  void vline(int x, int y, int h, Color c) { if (rows(y, y + h)) s_.drawFastVLine(x, y - oy_, h, c); }
  void line(int x0, int y0, int x1, int y1, Color c) { if (rows(y0 < y1 ? y0 : y1, y0 < y1 ? y1 : y0)) s_.drawLine(x0, y0 - oy_, x1, y1 - oy_, c); }
  void wideLine(float x0, float y0, float x1, float y1, float r, Color c) {
    if (rows((int)fminf(y0, y1) - (int)r - 1, (int)fmaxf(y0, y1) + (int)r + 1)) s_.drawWideLine(x0, y0 - oy_, x1, y1 - oy_, r, c);
  }
  void wedge(float x0, float y0, float x1, float y1, float r0, float r1, Color c) {
    float r = fmaxf(r0, r1);
    if (rows((int)fminf(y0, y1) - (int)r - 1, (int)fmaxf(y0, y1) + (int)r + 1)) s_.drawWedgeLine(x0, y0 - oy_, x1, y1 - oy_, r0, r1, c);
  }
  void circle(int x, int y, int r, Color c) { if (rows(y - r, y + r)) s_.drawCircle(x, y - oy_, r, c); }
  void fillCircle(int x, int y, int r, Color c) { if (rows(y - r, y + r)) s_.fillSmoothCircle(x, y - oy_, r, c); }
  void disc(int x, int y, int r, Color c) { if (rows(y - r, y + r)) s_.fillCircle(x, y - oy_, r, c); }  // no anti-aliasing
  // Anti-aliased filled circle blended straight into the buffer (drawWideLine costs ~0.35 ms for a 3 px dot).
  void dot(float x, float y, float r, Color c) {
    int y0 = (int)floorf(y - r - 1), y1 = (int)ceilf(y + r + 1), x0 = (int)floorf(x - r - 1), x1 = (int)ceilf(x + r + 1);
    if (!rows(y0, y1)) return;
    for (int py = y0; py <= y1; py++)
      for (int px = x0; px <= x1; px++) {
        float dx = px + 0.5f - x, dy = py + 0.5f - y, cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
        if (cov > 0) blend(px, py, c, cov >= 1 ? 255 : (uint8_t)(cov * 255));
      }
  }
  // LovyanGFX angles: degrees, 0 = east (3 o'clock), clockwise.
  void arc(int x, int y, int r0, int r1, float a0, float a1, Color c) { if (rows(y - r1, y + r1)) s_.drawArc(x, y - oy_, r0, r1, a0, a1, c); }
  void fillArc(int x, int y, int r0, int r1, float a0, float a1, Color c) { if (rows(y - r1, y + r1)) s_.fillArc(x, y - oy_, r0, r1, a0, a1, c); }
  void triangle(int x0, int y0, int x1, int y1, int x2, int y2, Color c) {
    int lo = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), hi = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (rows(lo, hi)) s_.fillTriangle(x0, y0 - oy_, x1, y1 - oy_, x2, y2 - oy_, c);
  }
  void pixel(int x, int y, Color c) { if (rows(y, y)) s_.drawPixel(x, y - oy_, c); }

  // Text. `datum` is a LovyanGFX textdatum (top_left, middle_center, baseline_left, ...). Returns the text width.
  int text(const char* str, int x, int y, const FontData* f, Color fg, lgfx::textdatum_t datum = lgfx::top_left) {
    int w = textWidth(str, f);
    int h = datum & 3;
    if (h == 1) x -= w / 2;
    else if (h == 2) x -= w;
    int v = datum & ~3, base;
    if (v == lgfx::middle_left) base = y + f->cap / 2;
    else if (v == lgfx::bottom_left) base = y - f->descent;
    else if (v == lgfx::baseline_left) base = y;
    else base = y + f->ascent;
    if (!rows(base - f->ascent, base + f->descent)) return w;
    uint16_t fg565 = swap565(fg);
    for (const char* p = str; *p;) {
      const Glyph* g = glyph(f, nextCodepoint(p));
      if (!g) continue;
      const uint8_t* a = f->bitmap + g->off;
      int gx = x + g->dx, gy = base - g->dy;
      for (int j = 0; j < g->h; j++)
        for (int i = 0; i < g->w; i++, a++)
          if (*a) blend(gx + i, gy + j, fg, *a, fg565);
      x += g->adv;
    }
    return w;
  }
  // Black-on-white QR code filling a w x w square; the caller draws the white quiet zone around it.
  void qr(const char* text, int x, int y, int w) { if (rows(y, y + w)) s_.qrcode(text, x, y - oy_, w, 1, false); }
  static int textWidth(const char* str, const FontData* f) {
    int w = 0;
    for (const char* p = str; *p;)
      if (const Glyph* g = glyph(f, nextCodepoint(p))) w += g->adv;
    return w;
  }

 private:
  // LovyanGFX keeps 16-bit sprites as byte-swapped RGB565.
  static uint16_t swap565(Color c) {
    uint16_t v = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    return (uint16_t)((v >> 8) | (v << 8));
  }
  void blend(int x, int y, Color c, uint8_t a, uint16_t c565 = 0) {
    y -= oy_;
    if (x < 0 || y < 0 || x >= s_.width() || y >= s_.height()) return;
    uint16_t* px = (uint16_t*)s_.getBuffer() + y * s_.width() + x;
    if (a >= 250) {
      *px = c565 ? c565 : swap565(c);
      return;
    }
    uint16_t d = (uint16_t)((*px >> 8) | (*px << 8));
    int dr = (d >> 11) << 3, dg = ((d >> 5) & 0x3F) << 2, db = (d & 0x1F) << 3;
    int r = dr + ((((int)(c >> 16) & 0xFF) - dr) * a >> 8), g = dg + ((((int)(c >> 8) & 0xFF) - dg) * a >> 8),
        b = db + ((((int)c & 0xFF) - db) * a >> 8);
    *px = swap565(((Color)r << 16) | ((Color)g << 8) | (Color)b);
  }
  static uint16_t nextCodepoint(const char*& p) {
    uint8_t c = (uint8_t)*p++;
    if (c < 0x80) return c;
    int extra = c >= 0xE0 ? 2 : 1;
    uint16_t cp = c & (extra == 2 ? 0x0F : 0x1F);
    while (extra-- && (*p & 0xC0) == 0x80) cp = (uint16_t)((cp << 6) | (*p++ & 0x3F));
    return cp;
  }
  static const Glyph* glyph(const FontData* f, uint16_t cp) {
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
      if (f->glyphs[mid].cp < cp) lo = mid + 1;
      else hi = mid - 1;
    }
    return nullptr;
  }

  lgfx::LGFX_Sprite& s_;
  int oy_;
};
