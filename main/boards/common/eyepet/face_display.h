#pragma once
// ============================================================================
// EyePet face UI for M5Stack StopWatch (466x466 round AMOLED).
// Ported from the approved design spec: xiaozhi-face/index.html (Canvas2D).
// 21 emotions; keys equal xiaozhi emotion names so SetEmotion() maps 1:1.
// Immediate-mode drawing at 30fps inside LV_EVENT_DRAW_MAIN, same pattern as
// boards/common/zongzhu/eye_display.h.
//
// Approximations vs the HTML spec (LVGL primitive limits, revisit on device):
//  - eye/pupil ellipses drawn as stadium pills (lv_draw_rect radius clamps)
//  - pupil not clipped: kept inside the eye by construction (small offsets)
//  - open mouth blob approximated by a dark stadium under the wave line
//  - blush/shine flat low-alpha circles (no radial gradient)
//  - face rotation/scale skipped; '?' drawn as arc+dot, no unicode extras
// ============================================================================

#include "display/lcd_display.h"
#include <esp_log.h>
#include <cmath>
#include <cstring>

namespace eyepet {

constexpr int kW = 466, kC = 233;
constexpr float kPI = 3.14159265f;
constexpr float kTAU = 6.28318531f;

// palette (from the approved spec)
static const lv_color_t kColBg = lv_color_hex(0xFF5F0F);
static const lv_color_t kColLid = lv_color_hex(0xF2560B);
static const lv_color_t kColInk = lv_color_hex(0x16120F);
static const lv_color_t kColPup = lv_color_hex(0x1440A6);
static const lv_color_t kColWhite = lv_color_hex(0xFFFFFF);
static const lv_color_t kColTongue = lv_color_hex(0xF25C78);
static const lv_color_t kColTear = lv_color_hex(0x6FD3FA);
static const lv_color_t kColSweat = lv_color_hex(0xA8E6FF);
static const lv_color_t kColHeart = lv_color_hex(0xFF2D5A);
static const lv_color_t kColHeart2 = lv_color_hex(0xFF85A1);

struct EyeDef { int x, y, rx, ry; int side; };
static const EyeDef kEL = {170, 206, 57, 83, -1};
static const EyeDef kER = {296, 206, 57, 83, 1};
constexpr float kPupilRx = 0.65f, kPupilRy = 0.72f, kPupilGap = 3.0f;

// ---------- tiny draw layer (canvas origin folded in) ----------
struct Cv {
  lv_layer_t* layer;
  int ox, oy;
};

static void fillRect(Cv& c, int x, int y, int w, int h, lv_color_t col, int radius = 0,
                     lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col; d.bg_opa = opa; d.radius = radius;
  lv_area_t a = {c.ox + x, c.oy + y, c.ox + x + w - 1, c.oy + y + h - 1};
  lv_draw_rect(c.layer, &d, &a);
}
static void fillCircle(Cv& c, int cx, int cy, int r, lv_color_t col, lv_opa_t opa = LV_OPA_COVER) {
  fillRect(c, cx - r, cy - r, r * 2, r * 2, col, r, opa);
}
static void seg(Cv& c, int x1, int y1, int x2, int y2, int w, lv_color_t col) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = col; d.width = w; d.round_start = 1; d.round_end = 1;
  d.p1 = {(lv_value_precise_t)(c.ox + x1), (lv_value_precise_t)(c.oy + y1)};
  d.p2 = {(lv_value_precise_t)(c.ox + x2), (lv_value_precise_t)(c.oy + y2)};
  lv_draw_line(c.layer, &d);
}
static void tri(Cv& c, int x1, int y1, int x2, int y2, int x3, int y3, lv_color_t col) {
  lv_draw_triangle_dsc_t d;
  lv_draw_triangle_dsc_init(&d);
  d.p[0] = {(lv_value_precise_t)(c.ox + x1), (lv_value_precise_t)(c.oy + y1)};
  d.p[1] = {(lv_value_precise_t)(c.ox + x2), (lv_value_precise_t)(c.oy + y2)};
  d.p[2] = {(lv_value_precise_t)(c.ox + x3), (lv_value_precise_t)(c.oy + y3)};
  d.color = col; d.opa = LV_OPA_COVER;
  lv_draw_triangle(c.layer, &d);
}

// ---------- math / animation helpers ----------
inline float clampf(float v, float a = 0.0f, float b = 1.0f) { return v < a ? a : (v > b ? b : v); }
inline float sq(float v) { return v * v; }

inline float blinkF(float t, float period = 3.4f, float off = 0.0f) {
  float p = fmodf(t + off, period), d = 0.22f;
  if (p >= d) return 1.0f;
  return fabsf(1.0f - 2.0f * p / d);
}
inline float hashn(float n) {
  float s = sinf(n * 12.9898f) * 43758.5453f;
  return s - floorf(s);
}
inline float easeOutBack(float k) {
  const float s = 1.7f;
  k -= 1.0f;
  return k * k * ((s + 1.0f) * k + s) + 1.0f;
}
struct Gaze { float px, py; };
static Gaze gazeF(float t, float k = 1.0f) {
  float segf = floorf(t / 1.6f), f = (t / 1.6f) - segf;
  float e = clampf(f / 0.12f); e = e * e * (3 - 2 * e);
  float x0 = (hashn(segf) * 2 - 1) * 10, x1 = (hashn(segf + 1) * 2 - 1) * 10;
  float y0 = (hashn(segf + 50) * 2 - 1) * 5, y1 = (hashn(segf + 51) * 2 - 1) * 5;
  return {k * (x0 + (x1 - x0) * e), k * (y0 + (y1 - y0) * e)};
}

// ---------- signature wavy mouth ----------
// two concaves + a middle arch, hooked tips curling up
inline float sigF(float u) {
  float v = powf(fabsf(u), 1.45f);
  return -cosf(kTAU * v) + 0.6f * (1 - v) * (1 - v)
         - 0.3f * sq(clampf((v - 0.82f) / 0.18f));
}
struct WavePts {
  static constexpr int N = 33;
  lv_point_t p[N];
};
static WavePts wavePts(int x, int y, float w = 1.0f, float amp = 1.0f, int flip = 1,
                       float curve = 0.0f, float tilt = 0.0f, float wob = 0.0f, float ph = 0.0f) {
  WavePts o{};
  int hw = (int)(81 * w), A = (int)(10 * amp);
  for (int i = 0; i < WavePts::N; i++) {
    float u = -1.0f + 2.0f * i / (WavePts::N - 1);
    int px = x + (int)(hw * u);
    int py = y + (int)(flip * A * sigF(u) + curve * (1 - u * u) - curve * 0.5f - tilt * u
                       + wob * sinf(u * kPI * 3 + ph));
    o.p[i] = {px, py};
  }
  return o;
}
static void strokeWave(Cv& c, const WavePts& w, int lw) {
  for (int i = 1; i < WavePts::N; i++)
    seg(c, w.p[i - 1].x, w.p[i - 1].y, w.p[i].x, w.p[i].y, lw, kColInk);
}
// open laugh mouth: dark stadium under the wave + tongue (blob-fill approximation)
static void openMouth(Cv& c, int x, int y, float w, float amp, int flip, float curve, float h) {
  fillRect(c, x - 62, y + (int)(h * 0.25f), 124, (int)(h * 1.05f), kColInk, 40);
  fillRect(c, x - 34, y + (int)(h * 0.72f), 68, (int)(h * 0.42f), kColTongue, 18);
  WavePts wv = wavePts(x, y, w, amp, flip, curve);
  strokeWave(c, wv, 27);
}

// ---------- composited primitives ----------
static void heart(Cv& c, int x, int y, int s, lv_color_t col) {
  int r = (int)(s * 0.28f);
  fillCircle(c, x - (int)(s * 0.27f), y - (int)(s * 0.22f), r, col);
  fillCircle(c, x + (int)(s * 0.27f), y - (int)(s * 0.22f), r, col);
  tri(c, x - s / 2, y - (int)(s * 0.12f), x + s / 2, y - (int)(s * 0.12f), x, y + (int)(s * 0.55f), col);
}
static void tear(Cv& c, int x, int y, int s) {
  fillCircle(c, x, y, s, kColTear);
  tri(c, x - (int)(s * 0.7f), y - (int)(s * 0.2f), x + (int)(s * 0.7f), y - (int)(s * 0.2f),
      x, y - (int)(s * 1.8f), kColTear);
  fillCircle(c, x - (int)(s * 0.3f), y - s / 5, (int)(s * 0.24f), kColWhite);
}
static void sparkle(Cv& c, int x, int y, int s) {
  if (s <= 1) return;
  fillRect(c, x - s, y - s / 4 - 1, s * 2, s / 2 + 2, kColWhite);
  fillRect(c, x - s / 4 - 1, y - s, s / 2 + 2, s * 2, kColWhite);
}
static void tongue(Cv& c, int x, int y, int w, int h) {
  fillRect(c, x - w / 2, y, w, h - w / 2 + 1, kColTongue);
  fillRect(c, x - w / 2, y + h - w, w, w, kColTongue, w / 2);
  seg(c, x, y + 6, x, y + (int)(h * 0.55f), 4, lv_color_hex(0xC23A55));
}
static void kissMouth(Cv& c, int x, int y) {
  seg(c, x - 10, y - 30, x + 24, y - 31, 22, kColInk);
  seg(c, x + 24, y - 31, x, y - 2, 22, kColInk);
  seg(c, x, y - 2, x + 26, y + 1, 22, kColInk);
  seg(c, x + 26, y + 1, x - 10, y + 28, 22, kColInk);
}
static void blush(Cv& c, int x, int y, float a) {
  if (a <= 0.02f) return;
  fillCircle(c, x, y, 40, lv_color_hex(0xFF3264), (lv_opa_t)(a * 110));
}
static void cheeks(Cv& c, float a) { blush(c, 108, 300, a); blush(c, 358, 300, a); }
static void zMark(Cv& c, int x, int y, int s) {
  seg(c, x - s, y, x + s, y, 6, kColWhite);
  seg(c, x + s, y, x - s, y + s, 6, kColWhite);
  seg(c, x - s, y + s, x + s, y + s, 6, kColWhite);
}
static void qMark(Cv& c, int x, int y, int r) {
  seg(c, x + r, y, x + r, y - r / 2, 8, kColWhite);
  seg(c, x + r, y - r / 2, x, y - r, 8, kColWhite);
  seg(c, x, y - r, x - r, y - r / 4, 8, kColWhite);
  fillCircle(c, x, y + r + 8, 6, kColWhite);
}
static void noteMark(Cv& c, int x, int y, int s) {
  fillCircle(c, x, y, s / 2, kColWhite);
  seg(c, x + s / 2, y, x + s / 2, y - s * 2, 6, kColWhite);
  seg(c, x + s / 2, y - s * 2, x + s * 7 / 5, y - s * 17 / 10, 6, kColWhite);
}
static void fallingHearts(Cv& c, float t, int n = 5) {
  for (int i = 0; i < n; i++) {
    float p = fmodf(t * 0.45f + i / (float)n, 1.0f);
    bool left = (i % 2) == 0;
    int x = (left ? 72 : 394) + (int)(14 * sinf(p * 9 + i));
    int y = 380 - (int)(p * 250);
    lv_opa_t opa = (lv_opa_t)(clampf(1 - p * p) * clampf(p * 6) * 255);
    if (opa <= 6) continue;
    lv_color_t col = (i % 3) ? kColHeart : kColHeart2;
    heart(c, x, y, (int)(26 + 14 * p), col);
  }
}
static void tearRain(Cv& c, int x, int y0, float t, float off) {
  int prevX = x, prevY = y0;
  for (int y = y0; y <= 470; y += 12) {
    float k = (float)(y - y0) / (470 - y0);
    int w = 8 + (int)(12 * k);
    int cx = x + (int)(3 * sinf(y * 0.06f + t * 8 + off));
    seg(c, prevX, prevY, cx, y, w, kColTear);
    prevX = cx; prevY = y;
  }
  for (int i = 0; i < 3; i++) {
    float p = fmodf(t * 1.1f + i / 3.0f + off, 1.0f);
    tear(c, x + 26 * (x < kC ? -1 : 1), (int)(280 + p * 180), 8);
  }
}

// ---------- eyes ----------
struct EyeOpt {
  float open = 1.0f;
  float ps = 1.0f;
  float px = 0, py = 0;
  float s = 1.0f;
  float tilt = 0.0f;
  float low = 0.0f;
  bool shine = false;
  bool heart = false;
};
static void drawEye(Cv& c, const EyeDef& E, const EyeOpt& o) {
  int rx = (int)(E.rx * o.s), ry = (int)(E.ry * o.s);
  int ex = E.x + (int)(o.px * 0.4f), ey = E.y;
  fillRect(c, ex - rx, ey - ry, rx * 2, ry * 2, kColWhite, rx);
  int prx = (int)(rx * kPupilRx * o.ps), pry = (int)(ry * kPupilRy * o.ps);
  int px = -E.side * (rx - prx - (int)kPupilGap) + (int)o.px;
  int py = 3 + (int)o.py;
  fillRect(c, ex + px - prx, ey + py - pry, prx * 2, pry * 2, kColPup, prx);
  if (o.shine) fillCircle(c, ex + px - (int)(prx * 0.3f), ey + py - (int)(pry * 0.5f),
                          (int)(prx * 0.26f) + 1, kColWhite);
  if (o.heart) heart(c, ex + px, ey + py, (int)(prx * 1.5f), kColWhite);
  if (o.low > 0.0f) {
    int ly = ey + ry - (int)(2.0f * ry * o.low);
    fillCircle(c, ex, ly + (int)(ry * 1.25f), (int)(rx * 1.9f), kColBg);
  }
  if (o.open < 1.0f) {
    int lidDrop = (int)(2.0f * ry * (1.0f - o.open));
    int top = ey - ry - 8;
    int lyL = ey - ry + lidDrop - (int)(o.tilt * 40.0f);
    int lyR = ey - ry + lidDrop + (int)(o.tilt * 40.0f);
    tri(c, ex - rx - 4, top, ex + rx + 4, top, ex + rx + 4, lyR, kColLid);
    tri(c, ex + rx + 4, lyR, ex - rx - 4, lyL, ex - rx - 4, top, kColLid);
    seg(c, ex - rx - 4, lyL, ex + rx + 4, lyR, 6, kColInk);
  }
}
static void bothEyes(Cv& c, const EyeOpt& o) { drawEye(c, kEL, o); drawEye(c, kER, o); }

enum ClosedType { CLOSED_FLAT, CLOSED_HAPPY, CLOSED_CALM, CLOSED_CHEV };
static void closedEye(Cv& c, const EyeDef& E, ClosedType type, int dy = 0, float sc = 0.8f,
                      int lw = 20) {
  int w = (int)(E.rx * sc);
  int x = E.x, y = E.y + dy;
  if (type == CLOSED_HAPPY) {
    seg(c, x - w, y + 14, x, y - 44, lw, kColInk);
    seg(c, x, y - 44, x + w, y + 14, lw, kColInk);
  } else if (type == CLOSED_CALM) {
    seg(c, x - w, y - 8, x, y + 32, lw, kColInk);
    seg(c, x, y + 32, x + w, y - 8, lw, kColInk);
  } else if (type == CLOSED_CHEV) {
    int s = -E.side;
    seg(c, x - (int)(w * 0.6f) * s, y - 34, x + (int)(w * 0.6f) * s, y, lw, kColInk);
    seg(c, x + (int)(w * 0.6f) * s, y, x - (int)(w * 0.6f) * s, y + 34, lw, kColInk);
  } else {
    seg(c, x - w, y, x + w, y, lw, kColInk);
  }
}
static void browSeg(Cv& c, int x1, int y1, int x2, int y2, int bend = 0, int lw = 12) {
  int mx = (x1 + x2) / 2, my = (y1 + y2) / 2 + bend;
  seg(c, x1, y1, mx, my, lw, kColInk);
  seg(c, mx, my, x2, y2, lw, kColInk);
}
static void sadBrows(Cv& c, int dy = 0) {
  browSeg(c, 122, 122 + dy, 190, 100 + dy, -5, 10);
  browSeg(c, 276, 100 + dy, 344, 122 + dy, -5, 10);
}

// ---------- 21 emotions (keys == xiaozhi emotion names) ----------
enum Emotion : uint8_t {
  E_NEUTRAL, E_HAPPY, E_LAUGHING, E_FUNNY, E_SAD, E_ANGRY, E_CRYING, E_LOVING,
  E_EMBARRASSED, E_SURPRISED, E_SHOCKED, E_THINKING, E_WINKING, E_COOL, E_RELAXED,
  E_DELICIOUS, E_KISSY, E_CONFIDENT, E_SLEEPY, E_SILLY, E_CONFUSED, E_COUNT
};

static void emNeutral(Cv& c, float t) {
  Gaze gz = gazeF(t);
  float o = blinkF(t, 3.6f);
  int dy = (int)(3 * sinf(t * kTAU / 2.6f));
  EyeOpt o1; o1.open = o; o1.px = gz.px; o1.py = gz.py + dy / 2;
  bothEyes(c, o1);
  strokeWave(c, wavePts(233, 328 + dy / 2, 1, 1, 1, 0, 0, 1.5f * sinf(t * 1.7f), t * 2), 27);
}
static void emHappy(Cv& c, float t) {
  float b = fabsf(sinf(t * kPI / 0.55f));
  float o = blinkF(t, 3.0f, 0.5f);
  int dy = (int)(-8 * b);
  cheeks(c, 0.35f);
  EyeOpt o1; o1.open = o; o1.low = 0.22f; o1.py = -4 - dy;
  bothEyes(c, o1);
  strokeWave(c, wavePts(233, 324 + dy / 2, 1, 1, 1, 14 + 3 * b), 27);
}
static void emLaughing(Cv& c, float t) {
  float k = fabsf(sinf(t * kTAU * 2.2f));
  int dy = (int)(-6 * k);
  cheeks(c, 0.4f);
  closedEye(c, kEL, CLOSED_HAPPY, -2 + dy);
  closedEye(c, kER, CLOSED_HAPPY, -2 + dy);
  openMouth(c, 233, 316 + dy, 1, 1, 1, 10, 48 + 18 * k);
}
static void emFunny(Cv& c, float t) {
  float k = fabsf(sinf(t * kTAU * 2.5f));
  int dy = (int)(-4 * k);
  cheeks(c, 0.4f);
  closedEye(c, kEL, CLOSED_CHEV, dy);
  closedEye(c, kER, CLOSED_CHEV, dy);
  for (int i = 0; i < 4; i++) {
    float p = fmodf(t * 1.3f + i / 4.0f, 1.0f);
    tear(c, kEL.x - 58 - (int)(p * 70), kEL.y + 10 - (int)(70 * p) + (int)(140 * p * p) + dy, 9);
    tear(c, kER.x + 58 + (int)(p * 70), kER.y + 10 - (int)(70 * p) + (int)(140 * p * p) + dy, 9);
  }
  openMouth(c, 233, 316 + dy, 1, 1, 1, 10, 52 + 14 * k);
}
static void emSad(Cv& c, float t) {
  float o = 0.85f * blinkF(t, 4.2f);
  int dy = 4 + (int)(4 * sinf(t * kTAU / 3));
  EyeOpt o1; o1.open = o; o1.tilt = -0.3f; o1.py = 10 + dy;
  bothEyes(c, o1);
  sadBrows(c, dy);
  strokeWave(c, wavePts(233, 330 + dy, 1, 0.9f, -1, 0, 0, 2, t * 12), 27);
}
static void emAngry(Cv& c, float t) {
  int dx = (int)(2 * sinf(t * kTAU * 9));
  EyeOpt o1; o1.open = 0.76f; o1.tilt = 0.4f; o1.py = 8;
  bothEyes(c, o1);
  browSeg(c, 116, 104, 204, 140, 0, 18);
  browSeg(c, 262, 140, 350, 104, 0, 18);
  strokeWave(c, wavePts(233 + dx, 332, 1, 1.1f, -1), 27);
  // pulsing anger hooks around the top-right corner
  int s = (int)(1 + 6 * fabsf(sinf(t * kTAU * 1.5f)));
  int R2 = 24 + s;
  seg(c, 372 - R2, 96 - R2 + 6, 372 - R2 + 12, 96 - R2, 8, lv_color_hex(0xB80F0F));
  seg(c, 372 - R2 + 12, 96 - R2, 372 - R2 + 12, 96 - R2 + 12, 8, lv_color_hex(0xB80F0F));
  seg(c, 372 + R2, 96 - R2 + 6, 372 + R2 - 12, 96 - R2, 8, lv_color_hex(0xB80F0F));
  seg(c, 372 + R2 - 12, 96 - R2, 372 + R2 - 12, 96 - R2 + 12, 8, lv_color_hex(0xB80F0F));
  seg(c, 372 - R2, 96 + R2 - 6, 372 - R2 + 12, 96 + R2, 8, lv_color_hex(0xB80F0F));
  seg(c, 372 + R2, 96 + R2 - 6, 372 + R2 - 12, 96 + R2, 8, lv_color_hex(0xB80F0F));
}
static void emCrying(Cv& c, float t) {
  float sob = sinf(t * kTAU * 3);
  int dy = (int)(3 * sob);
  EyeOpt o1; o1.open = 0.75f; o1.tilt = -0.32f; o1.py = 10 + dy; o1.shine = true;
  bothEyes(c, o1);
  sadBrows(c, -4 + dy);
  tearRain(c, kEL.x - 22, 270 + dy, t, 0);
  tearRain(c, kER.x + 22, 270 + dy, t, 0.4f);
  strokeWave(c, wavePts(233, 334 + dy, 1, 1.2f + 0.15f * sob, -1, 0, 0, 2.5f, t * 14), 27);
}
static void emLoving(Cv& c, float t) {
  fallingHearts(c, t);
  float ps = 1.0f + 0.05f * (sinf(t * kTAU * 1.6f) > 0 ? sinf(t * kTAU * 1.6f) : 0);
  int dy = (int)(4 * sinf(t * kTAU / 2));
  cheeks(c, 0.45f);
  EyeOpt o1; o1.ps = ps; o1.heart = true; o1.py = -2 + dy; o1.open = blinkF(t, 3.5f);
  bothEyes(c, o1);
  strokeWave(c, wavePts(233, 328 + dy / 2, 1, 1, 1, 12), 27);
}
static void emEmbarrassed(Cv& c, float t) {
  float o = 0.88f * blinkF(t, 2.6f);
  int px = 12 + (int)(2 * sinf(t * 2));
  int dy = (int)(2 * sinf(t * kTAU));
  cheeks(c, 0.55f + 0.15f * sinf(t * kTAU));
  lv_color_t tick = lv_color_hex(0xBE143C);
  for (int ci = 0; ci < 2; ci++) {
    int x = ci ? 358 : 108;
    for (int i = -1; i <= 1; i++)
      seg(c, x + i * 14 + 5, 290 + dy, x + i * 14 - 5, 308 + dy, 4, tick);
  }
  EyeOpt o1; o1.open = o; o1.tilt = -0.1f; o1.px = (float)px; o1.py = 6 + dy;
  bothEyes(c, o1);
  tear(c, 90, 122, 13);
  strokeWave(c, wavePts(233, 328 + dy, 0.85f, 0.8f, 1, 0, 0, 3, t * 7), 27);
}
static void emSurprised(Cv& c, float t) {
  float p = fmodf(t, 2.4f) / 2.4f;
  float pop = p < 0.15f ? easeOutBack(p / 0.15f) : 1.0f;
  float es = 1.0f + 0.06f * pop;
  float o = blinkF(t, 2.4f, 1.4f);
  int dy = (int)(-3 * pop);
  EyeOpt o1; o1.s = es; o1.ps = 0.88f; o1.open = o; o1.py = dy;
  bothEyes(c, o1);
  int lift = (int)(8 * pop);
  browSeg(c, 124, 96 - lift, 196, 90 - lift, -12, 10);
  browSeg(c, 270, 90 - lift, 342, 96 - lift, -12, 10);
  strokeWave(c, wavePts(233, 334 + dy / 2, 0.55f, 1.1f + 0.1f * sinf(t * kTAU), 1), 27);
}
static void emShocked(Cv& c, float t) {
  lv_color_t line = lv_color_hex(0x281E78);
  for (int i = 0; i < 7; i++) {
    int x = 140 + i * 31;
    seg(c, x, 26 + (i % 2) * 12, x, 90 + (i % 2) * 12, 6, line);
  }
  int dx = (int)(3 * sinf(t * kTAU * 12));
  EyeOpt o1; o1.s = 1.08f; o1.ps = 0.78f; o1.px = 2 * sinf(t * kTAU * 15) + dx;
  bothEyes(c, o1);
  strokeWave(c, wavePts(233 + dx, 338, 0.9f, 1.5f, -1, 0, 0, 2, t * 30), 27);
  tear(c, 80, 160, 12);
  tear(c, 388, 150, 10);
}
static void emThinking(Cv& c, float t) {
  float o = 0.92f * blinkF(t, 4.0f, 2.0f);
  EyeOpt ol; ol.open = o; ol.px = 10; ol.py = -20;
  drawEye(c, kEL, ol);
  EyeOpt or_; or_.open = o; or_.px = 10; or_.py = -20;
  drawEye(c, kER, or_);
  browSeg(c, 128, 112, 194, 112, -3, 10);
  browSeg(c, 270, 86, 342, 94, -12, 10);
  strokeWave(c, wavePts(246, 328, 0.7f, 1, 1, 0, 8, 2, t * 2.5f), 27);
  int n = (int)((fmodf(t, 2.4f)) / 0.6f);
  static const int bx[3] = {322, 344, 364};
  static const int by[3] = {102, 80, 60};
  static const int br[3] = {7, 10, 12};
  for (int i = 0; i < n && i < 3; i++) {
    fillCircle(c, bx[i], by[i], br[i], kColWhite);
    fillCircle(c, bx[i], by[i], br[i] - 3, kColBg);
  }
}
static void emWinking(Cv& c, float t) {
  float p = fmodf(t, 2.2f);
  float wo = 1.0f;
  if (p > 0.6f && p < 0.72f) wo = 1 - (p - 0.6f) / 0.12f;
  else if (p >= 0.72f && p < 1.3f) wo = 0;
  else if (p >= 1.3f && p < 1.42f) wo = (p - 1.3f) / 0.12f;
  float k = 1 - wo;
  int dy = (int)(-3 * k);
  cheeks(c, 0.3f);
  EyeOpt ol; ol.low = 0.12f;
  drawEye(c, kEL, ol);
  if (wo < 0.12f) closedEye(c, kER, CLOSED_CHEV, dy);
  else { EyeOpt or_; or_.low = 0.12f; or_.open = wo; or_.py = dy; drawEye(c, kER, or_); }
  strokeWave(c, wavePts(233, 328 + dy / 2, 1, 1, 1, 10 + 4 * k, 4 * k), 27);
  sparkle(c, 382, 132, (int)(22 * k));
  sparkle(c, 404, 170, (int)(12 * k));
}
static void emCool(Cv& c, float t) {
  constexpr float T = 4.0f;
  float p = fmodf(t, T);
  int gy = p < 0.5f ? (int)(-260 * (1 - easeOutBack(p / 0.5f))) : 0;
  int dy = (int)(-4 * fabsf(sinf(t * kPI * 2)));
  EyeOpt o1; o1.open = 0.62f; o1.py = dy;
  bothEyes(c, o1);
  // sunglasses drop-in
  int gx = 0, gyy = gy + dy;
  fillRect(c, 104, 150 + gyy, 128, 98, lv_color_hex(0x141416), 40);
  fillRect(c, 234, 150 + gyy, 128, 98, lv_color_hex(0x141416), 40);
  seg(c, 104, 164 + gyy, 74, 156 + gyy, 10, kColInk);
  seg(c, 362, 164 + gyy, 392, 156 + gyy, 10, kColInk);
  fillRect(c, 96, 148 + gyy, 272, 26, lv_color_hex(0xFFFFFF), 0, (lv_opa_t)(46));
  if (p > 1.0f && p < 1.7f) {
    int x = 60 + (int)((p - 1.0f) / 0.7f * 380);
    tri(c, x, 140 + gyy, x + 30, 140 + gyy, x - 18, 260 + gyy, lv_color_hex(0xFFFFFF));
    tri(c, x + 30, 140 + gyy, x - 18, 260 + gyy, x - 48, 260 + gyy, lv_color_hex(0xFFFFFF));
  }
  strokeWave(c, wavePts(233, 328, 0.85f, 1, 1, 4, 10), 27);
}
static void emRelaxed(Cv& c, float t) {
  float br = sinf(t * kTAU / 4);
  int dy = (int)(-3 * br);
  cheeks(c, 0.25f);
  closedEye(c, kEL, CLOSED_CALM, 6 + dy);
  closedEye(c, kER, CLOSED_CALM, 6 + dy);
  strokeWave(c, wavePts(233, 328 + dy / 2, 1, 0.8f, 1, 8, 0, 1.2f, t), 27);
  float p = fmodf(t * 0.35f, 1.0f);
  if (p < 0.55f) noteMark(c, 356 + (int)(10 * sinf(p * 8)), 170 - (int)(p * 90), 14);
}
static void emDelicious(Cv& c, float t) {
  float sw = sinf(t * kTAU * 0.8f);
  int dy = (int)(-3 * fabsf(sinf(t * kTAU)));
  cheeks(c, 0.35f);
  closedEye(c, kEL, CLOSED_HAPPY, dy);
  closedEye(c, kER, CLOSED_HAPPY, dy);
  WavePts w = wavePts(233, 328 + dy, 1, 1, 1, 14);
  int idx = 16 + (int)(8 * sw);
  if (idx < 0) idx = 0;
  if (idx > WavePts::N - 1) idx = WavePts::N - 1;
  tongue(c, w.p[idx].x, w.p[idx].y, 44, 46 + (int)(6 * fabsf(cosf(t * kTAU * 0.8f))));
  strokeWave(c, w, 27);
  sparkle(c, 368, 250 + dy, (int)(16 * fabsf(sinf(t * kTAU * 0.7f))));
  sparkle(c, 98, 232 + dy, (int)(12 * fabsf(cosf(t * kTAU * 0.7f))));
}
static void emKissy(Cv& c, float t) {
  int dy = 0;
  cheeks(c, 0.45f);
  EyeOpt ol; ol.low = 0.1f; ol.open = blinkF(t, 3.0f); ol.py = dy;
  drawEye(c, kEL, ol);
  closedEye(c, kER, CLOSED_CHEV, dy);
  kissMouth(c, 236, 326 + dy);
  for (int i = 0; i < 3; i++) {
    float p = fmodf(t * 0.7f + i / 3.0f, 1.0f);
    lv_opa_t opa = (lv_opa_t)(clampf(1 - p * p) * clampf(p * 8) * 255);
    if (opa <= 6) continue;
    lv_color_t col = kColHeart;
    int hx = 266 + (int)(p * 110), hy = 310 - (int)(p * 160) + (int)(10 * sinf(p * 10));
    int s = (int)(18 + 30 * p);
    fillCircle(c, hx - (int)(s * 0.27f), hy - (int)(s * 0.22f), (int)(s * 0.28f), col, opa);
    fillCircle(c, hx + (int)(s * 0.27f), hy - (int)(s * 0.22f), (int)(s * 0.28f), col, opa);
    tri(c, hx - s / 2, hy - (int)(s * 0.12f), hx + s / 2, hy - (int)(s * 0.12f), hx, hy + (int)(s * 0.55f), col);
  }
}
static void emConfident(Cv& c, float t) {
  float o = 0.62f * blinkF(t, 4.0f);
  int dy = -4;
  EyeOpt o1; o1.open = o; o1.tilt = 0.06f; o1.px = 6; o1.py = dy;
  bothEyes(c, o1);
  browSeg(c, 126, 120 + dy, 194, 122 + dy, 0, 10);
  browSeg(c, 270, 104 + dy, 342, 94 + dy, -10, 10);
  strokeWave(c, wavePts(233, 328, 0.85f, 1, 1, 6, 12), 27);
  sparkle(c, 384, 124, (int)(20 * fabsf(sinf(t * kTAU * 0.8f))));
}
static void emSleepy(Cv& c, float t) {
  constexpr float T = 5.0f;
  float p = fmodf(t, T);
  float o, nod;
  if (p < 3.6f) {
    o = 0.45f * (1.0f - powf(p / 3.6f, 1.6f));
    nod = powf(p / 3.6f, 2.0f);
  } else if (p < 4.2f) {
    o = 0.0f; nod = 1.0f;
  } else {
    float k = clampf((p - 4.2f) / 0.15f);
    o = 0.52f * k - 0.07f * clampf((p - 4.35f) / 0.65f);
    nod = 1.0f - k;
  }
  int dy = (int)(14 * nod);
  EyeOpt o1; o1.open = (o > 0.02f ? o : 0.02f); o1.tilt = -0.12f; o1.py = 12 + dy;
  bothEyes(c, o1);
  strokeWave(c, wavePts(233, 330 + dy, 0.7f - 0.1f * nod, 0.7f + 0.3f * nod, 1), 27);
  for (int i = 0; i < 3; i++) {
    float q = fmodf(t * 0.4f + i / 3.0f, 1.0f);
    if (q < 0.85f) zMark(c, 336 + (int)(q * 46) + (int)(6 * sinf(q * 8)), 150 - (int)(q * 80),
                         (int)(11 + 10 * q));
  }
}
static void emSilly(Cv& c, float t) {
  int dy = (int)(-3 * fabsf(sinf(t * kTAU * 1.8f)));
  cheeks(c, 0.35f);
  closedEye(c, kEL, CLOSED_CHEV, dy);
  EyeOpt or_; or_.s = 1.05f; or_.px = -6; or_.py = -6 + dy;
  drawEye(c, kER, or_);
  WavePts w = wavePts(233, 328 + dy, 1, 1, 1, 14);
  tongue(c, w.p[22].x, w.p[22].y, 48, 56);
  strokeWave(c, w, 27);
}
static void emConfused(Cv& c, float t) {
  float o = blinkF(t, 3.2f, 1.0f);
  EyeOpt ol; ol.px = -8; ol.py = -12; ol.open = o;
  drawEye(c, kEL, ol);
  EyeOpt or_; or_.px = 4; or_.py = 8; or_.open = o * 0.85f; or_.tilt = -0.15f;
  drawEye(c, kER, or_);
  browSeg(c, 126, 118, 194, 110, 0, 10);
  browSeg(c, 270, 84, 342, 98, -8, 10);
  strokeWave(c, wavePts(233, 328, 1, 1, 1, 0, -6, 4, t * 4), 27);
  qMark(c, 372 + (int)(4 * sinf(t * 2)), 106 + (int)(6 * sinf(t * kTAU * 0.8f)), 39);
}

// ---------- emotion dispatch ----------
static void drawEmotion(Cv& c, int id, float t) {
  switch (id) {
    case E_HAPPY: emHappy(c, t); break;
    case E_LAUGHING: emLaughing(c, t); break;
    case E_FUNNY: emFunny(c, t); break;
    case E_SAD: emSad(c, t); break;
    case E_ANGRY: emAngry(c, t); break;
    case E_CRYING: emCrying(c, t); break;
    case E_LOVING: emLoving(c, t); break;
    case E_EMBARRASSED: emEmbarrassed(c, t); break;
    case E_SURPRISED: emSurprised(c, t); break;
    case E_SHOCKED: emShocked(c, t); break;
    case E_THINKING: emThinking(c, t); break;
    case E_WINKING: emWinking(c, t); break;
    case E_COOL: emCool(c, t); break;
    case E_RELAXED: emRelaxed(c, t); break;
    case E_DELICIOUS: emDelicious(c, t); break;
    case E_KISSY: emKissy(c, t); break;
    case E_CONFIDENT: emConfident(c, t); break;
    case E_SLEEPY: emSleepy(c, t); break;
    case E_SILLY: emSilly(c, t); break;
    case E_CONFUSED: emConfused(c, t); break;
    default: emNeutral(c, t); break;
  }
}

static int EmotionFor(const char* name) {
  static const char* kKeys[E_COUNT] = {
      "neutral", "happy", "laughing", "funny", "sad", "angry", "crying", "loving",
      "embarrassed", "surprised", "shocked", "thinking", "winking", "cool", "relaxed",
      "delicious", "kissy", "confident", "sleepy", "silly", "confused"};
  if (name && name[0]) {
    for (int i = 0; i < E_COUNT; i++)
      if (strcmp(name, kKeys[i]) == 0) return i;
    // aliases seen on the wire
    if (strcmp(name, "singing") == 0) return E_HAPPY;
    if (strcmp(name, "dancing") == 0 || strcmp(name, "rapper") == 0 ||
        strcmp(name, "basketball") == 0)
      return E_LAUGHING;
    if (strcmp(name, "listening") == 0 || strcmp(name, "searching") == 0) return E_NEUTRAL;
  }
  return E_NEUTRAL;
}

// ---------- display ----------
constexpr int kFaceBandX = 46, kFaceBandY = 96, kFaceBandW = 374, kFaceBandH = 310;

class FaceDisplay : public SpiLcdDisplay {
 public:
  using SpiLcdDisplay::SpiLcdDisplay;
  ~FaceDisplay() override {
    DisplayLockGuard lock(this);
    if (frame_timer_) lv_timer_delete(frame_timer_);
  }

  void SetupUI() override {
    if (setup_ui_called_) return;
    SpiLcdDisplay::SetupUI();
    {
      DisplayLockGuard lock(this);
      // round-glass insets (same as RoundLcdDisplay)
      lv_obj_set_style_pad_left(status_bar_, kW * 0.2, 0);
      lv_obj_set_style_pad_right(status_bar_, kW * 0.2, 0);
      lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 56);
      lv_obj_set_width(status_label_, kW * 0.6);
      lv_obj_set_width(notification_label_, kW * 0.6);
      lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);
      lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
      if (bottom_bar_ != nullptr) {
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, -66);
        lv_obj_set_width(chat_message_label_, kW * 0.75);
      }
      if (top_bar_ != nullptr) {
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 28);
        lv_obj_set_style_pad_top(top_bar_, 14, 0);
      }
      // white-on-orange theme, transparent bars
      lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_TRANSP, 0);
      lv_obj_set_style_text_color(status_label_, kColWhite, 0);
      lv_obj_set_style_text_color(notification_label_, kColWhite, 0);
      lv_obj_set_style_text_color(chat_message_label_, kColWhite, 0);
      lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

      canvas_ = lv_obj_create(emoji_box_);
      lv_obj_remove_style_all(canvas_);
      lv_obj_set_size(canvas_, LV_PCT(100), LV_PCT(100));
      lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_event_cb(
          canvas_,
          [](lv_event_t* e) {
            static_cast<FaceDisplay*>(lv_event_get_user_data(e))->Draw(lv_event_get_layer(e));
          },
          LV_EVENT_DRAW_MAIN, this);
      frame_timer_ = lv_timer_create(
          [](lv_timer_t* timer) {
            static_cast<FaceDisplay*>(lv_timer_get_user_data(timer))->Render();
          },
          33, this);  // 30fps
      ESP_LOGI("EyePet", "face UI ready");
    }
  }

  bool SetEmotion(const char* emotion) override {
    emotion_id_ = EmotionFor(emotion);
    emotion_t0_ = lv_tick_get();
    return true;
  }

 private:
  static void DrawCb(lv_event_t* e) {
    static_cast<FaceDisplay*>(lv_event_get_user_data(e))->Draw(lv_event_get_layer(e));
  }
  static void FrameCb(lv_timer_t* timer) {
    static_cast<FaceDisplay*>(lv_timer_get_user_data(timer))->Render();
  }

  void Render() {
    if (canvas_ == nullptr) return;
    lv_area_t band;
    lv_obj_get_coords(canvas_, &band);
    band.x1 += kFaceBandX; band.y1 += kFaceBandY;
    band.x2 = band.x1 + kFaceBandW - 1;
    band.y2 = band.y1 + kFaceBandH - 1;
    lv_obj_invalidate_area(canvas_, &band);
  }

  void Draw(lv_layer_t* layer) {
    lv_area_t a;
    lv_obj_get_coords(canvas_, &a);
    Cv c = {layer, a.x1, a.y1};
    fillRect(c, 0, 0, kW, kW, kColBg);
    float t = (float)(lv_tick_get() - emotion_t0_) / 1000.0f;
    drawEmotion(c, emotion_id_, t);
  }

  lv_obj_t* canvas_ = nullptr;
  lv_timer_t* frame_timer_ = nullptr;
  int emotion_id_ = E_NEUTRAL;
  uint32_t emotion_t0_ = 0;
};

}  // namespace eyepet
