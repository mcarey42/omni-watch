/* Mop Bot (`mopbot`, rd/faces.h; rd wave 2 Task 10, spec section 10.13; faces-quarters.js:826-948: buildBg(), cleanArc(),
 * bootPrints(), wetSign(), bot(), botAngle(), frame(), pointer() and sleepFrame()).
 *
 * The deck (the ruling C5): the tread plate is one 44 x 44 RGB565 tile (rd_mb_tread, buildBg()'s canvas cut to its
 * top-left corner, past the grime's and the ticks' reach), looked up by x mod 44 and y mod 44 under every clip; its
 * plates alternate their tilt with the parity of column plus row, so a 22 px step flips them and the period is 44 each
 * way, not the notes' 22 x 44 (the tile is 3 872 B, not 1 936: R146, reported). Over it the shared rd_grime, tinted
 * toward a grime brown (#503610) by its alpha times k / 256 (k 600 from r 188 out, 520 inside r 168, a ramp between, in
 * r squared; at most 80 of 255), as buildBg()'s 280 ellipses lie mostly on the track (tuned so the deck's mean colour
 * and void share match the mockup's settled shot); then the 60 ticks (229 - L to 229:
 * every fifth 10 px, paint .8, 2.5 px; the rest 5 px, paintDim .45, 1.5 px; butt ends). Any clip draws its own pixels.
 * Over the deck, in frame()'s order, with the clock set:
 *   the clean wet arc (cleanArc()) from 12 to .04 rad behind the robot: the annulus r 192-222 filled #20262B, its six
 *   1 px rings (r 195 + 5 k, rgba(160, 190, 200, .05)) and the wet sheen (an 18 px stroke at r 205 over the last .28 rad
 *   of it, rgba(200, 225, 235, .2), butt ends), all clipped by the arc's sector;
 *   the boot prints (bootPrints(), from m 4, the wake over): five prints straight out across the track at r 170 + 15 k,
 *   alternately .03 rad either side of the stretch mopped 2.2 min ago (stepped once a minute with the minute, as the
 *   brief's rest motion says: the mockup's carries the seconds), each two rounded soles in rgba(112, 76, 40, .9) and
 *   three black .35 treads;
 *   the hazard ring (r 190, 2 px, hazard .35, dashed 10 on 10 from 3 o'clock round, crawling 1 px a second: the clock's
 *   second of the day; once a minute under reduced motion, R171; mono_ms while unset);
 *   30 (Chakra 13, paintDim .9, its middle 439) and the WET FLOOR sign (wetSign(): a hazard triangle on r 205 .1 rad
 *   before 12, its 1.5 px hazardDark round-joined edge and its !);
 *   the time in worn hazard stencil 132 (rd_f_s132 tinted, its digits on the mockup's fixed advance tightened to .9,
 *   centred, baseline 277; 12 h without the leading zero), the stale marker above it (staleMark(233, 161)), the date
 *   with its AM/PM suffix in 12 h and DECK 14 (Chakra 13, paintDim, middle 307);
 *   the robot (bot(): ours, polygons at any heading, scale 1.25): its mop (a 2.5 px #8C7A5A handle and 13 #7D776B
 *   strands, swishing .35 rad at t / 130), its treads (#121212, their 1 px #3A3A3A cleats rolling at t / 40), the
 *   squat hazard bucket (1.5 px black edge, a hazardDark stripe, two grime dots), the #B8861C lid and its glint, the
 *   eye (r 3.4, its .35 glow r 7; P.lamp and #6A1810 blinking each 500 ms) and the whip antenna with its P.lamp tip.
 * The robot is the minute hand: (m + s / 60) / 60 of a turn on r 207 (the track's centre line; the mockup's own
 * pointer and robot ride its r 205: the brief's 207 is used, 2 px out), scrubbing +-.012 rad at t / 420. t is
 * mono_ms (the motion reads no clock). Under reduced motion, unset or in a still the robot holds still.
 * The wake (1.2 s): the robot races from the sign (.11 rad past 12) to the minute by easeOutBack (c 1.4), mopping the
 * arc as it goes; no prints until it is over. A tap within RD_MB_HIT_R of the robot's centre at the input's clock
 * (tap_var; tap() taps nothing: it has no clock, I3) startles it: an 800 ms spin (easeInOut, a whole turn), a hop
 * over the first 400 ms (scale x (1 + .3 sin)) and an amber eye; under reduced motion only the eye flashes.
 * Unset: the robot parked asleep by the sign (its eye off), Z Z beside it, the track all grime, --:-- (stencil 132,
 * paintDim) and AWAITING TIME SIGNAL.
 * The chime picture (awake, set, not reduced): the eye swells with the thrum (its glow, the quarters glowSprite()
 * profile r 26 at .55 thrum, and the eye x (1 + .35 thrum)), the antenna's tip blinks with the beep (star0, r x (1 +
 * .8 beep)), and on the click the robot bumps the sign (it tips .35 click rad about its foot).
 * The glance (sleepFrame()): the outlined stencil time (rd_f_s132o) in rd.g.hazard, a r 6 rd.g.hazard dot where the
 * robot is (by the minute; parked unset), the stale marker; all through the orbit.
 *
 * Pure: no esp_*, freertos or LVGL; no libm (sines through fk_sincos, square roots by Newton's steps, the angles by
 * a polynomial arc tangent, floors by casts); no allocation; no settings, store or NVS symbol, and no state past a
 * frame (the face keeps nothing between draws). draw()'s path scratch lives in the context, as the Bowl's does. */
#include <stdint.h>
#include <string.h>
#include "rd/faces.h"
#include "rd/assets_meta.h"
#include "fk/compose.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"

#define W 466
#define C 233
#define PI 3.14159265359f
#define TAU 6.28318530718f
/* the R86 rounding rd_pack.py packs with (tools/facepack.py rgb565()) */
#define R86(r, g, b) \
    (uint16_t)((((r) * 31 + 127) / 255) << 11 | (((g) * 63 + 127) / 255) << 5 | (((b) * 31 + 127) / 255))
#define TILE 44
#define GR_N 256          /* rd_grime's tile */
#define RI 192.f          /* the track */
#define RO 222.f
#define RR 205.f          /* the mockup's track line: the sign, 30, the sheen */
#define BOT_R 207.f       /* the robot's line (the brief: the track's centre) */
#define RING_R 190.f      /* the hazard ring: RI - 2 */
#define DASH 10
#define TICK_R 229.f
#define PARK_TH 0.11f     /* the robot parked: .11 rad past 12 */
#define SIGN_TH (-0.1f)
#define ARC_LAG 0.04f     /* the arc ends .04 rad behind the robot */
#define SHEEN_LEN 0.28f
#define SHEEN_W 18.f
#define WAKE_MS 1200
#define HOP_MS 800        /* the spin; the hop over its first 400 ms */
#define PULSE_MS 900
#define BOT_K 1.25f       /* bot()'s scale */
#define BOT_REACH 44.f    /* the robot's farthest ink from its centre, unscaled: the mop's strands (43.1) and AA */
#define WOBBLE 0.012f
#define TIGHT 0.9f        /* drawTime(..., tight .9) */
#define Y_TIME 277        /* drawTime(..., cy + 44): the baseline */
#define Y_DATE 310        /* lab(..., cy + 74): 307 middle + 3.4 */
#define Y_30 442          /* lab('30', cx, cy + RR + 1): 439 middle + 3.4 */
#define STALE_CY 165      /* staleMark(233, 161): the ring at 161 (cy - 4.5 of the shared marker's) and STALE on 165 */
#define TRACK13 1.8f      /* lab(): (13 x .14).toFixed(1) px */
#define ZZ_DX 30.f        /* lab('Z Z', x + 30, y + 26): its baseline 3.4 below */
#define ZZ_DY 29.f
#define TAIL_HALF 20      /* the sheen's tail end's box, 40 x 40 */
#define BOT_HALF_MAX (BOT_REACH * BOT_K * 1.3f + 2.f + 3.f)   /* the hop's largest, its wobble and AA */
/* the grime's tint: k / 256 of its alpha (at most GA_MAX): GK_TR on the track (r >= 188), GK_IN inside r 168,
 * a ramp in r squared between */
#define GRIME_RGB R86(0x50, 0x36, 0x10)
#define GK_IN 520
#define GK_TR 600
#define GR_R0 168
#define GR_R1 188
#define GA_MAX 80

#define C_ARC R86(0x20, 0x26, 0x2B)
#define C_RINGLINE R86(160, 190, 200)
#define C_SHEEN R86(200, 225, 235)
#define C_PRINT R86(112, 76, 40)
#define C_MOP R86(0x8C, 0x7A, 0x5A)
#define C_STRAND R86(0x7D, 0x77, 0x6B)
#define C_TREAD R86(0x12, 0x12, 0x12)
#define C_CLEAT R86(0x3A, 0x3A, 0x3A)
#define C_DOT R86(40, 30, 10)
#define C_LID R86(0xB8, 0x86, 0x1C)
#define C_GLINT R86(255, 240, 200)
#define C_EYE_DIM R86(0x6A, 0x18, 0x10)
#define C_EYE_OFF R86(0x2A, 0x14, 0x10)
#define C_HAZARD R86(0xE2, 0xA6, 0x2A)
#define C_HAZARD_DARK R86(0x17, 0x12, 0x0B)
#define C_LAMP R86(0xFF, 0x3B, 0x24)
#define C_AMBER R86(0xFF, 0xA8, 0x3A)
#define C_STAR0 R86(0xFF, 0xD9, 0xA0)
#define C_STEEL_HI R86(0x8A, 0x90, 0x96)
#define C_PAINT R86(0xDC, 0xD3, 0xBE)
#define C_PAINT_DIM R86(0x8C, 0x84, 0x74)

const omni_rect_t RD_MB_TIME = {44, 176, 422, 288};
const omni_rect_t RD_MB_DATE = {50, 294, 416, 318};
const omni_rect_t RD_MB_STALE = {180, 150, 290, 172};
const omni_rect_t RD_MB_SIGN = {192, 10, 234, 44};
/* the robot can be anywhere on its line: every pixel it can reach (the chime's eye and antenna) */
const omni_rect_t RD_MB_TRACK = {0, 0, W, W};
/* the ring as the four rim bands of its box (R173's rule): a ring pixel (r >= 188.5) has |dx| or |dy| >= 132 */
static const omni_rect_t RING_BANDS[4] = {{39, 39, 427, 101}, {39, 365, 427, 427}, {39, 101, 101, 365},
                                          {365, 101, 427, 365}};

/* ---- paths: points in contours (end[k]: one past contour k's last point) */
#define PMAX 96
#define CMAX 8
#define QN 8          /* a quadratic curve's steps */
typedef struct { omni_pt_t p[PMAX]; uint8_t end[CMAX]; int n, nc; float cx, cy; } path_t;

typedef struct { omni_pt_t a, b; float w; uint16_t rgb; uint8_t a8; omni_rect_t box; } tick_t;

typedef struct {
    fk_img_t tread, grime, s132_img, s132o_img, ch13_img;
    fk_atlas_t s132, s132o, ch13;
    const uint8_t *glow;   /* the quarters glowSprite() profile, r 26 (rd_glow_lut) */
    tick_t tick[60];
    int16_t stale_cx;
    /* draw()'s scratch: a path and its two offsets (off the LVGL task's stack; written before each read) */
    path_t P, O, I;
} mb_t;

/* ---- small helpers */
static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float fabs_f(float v) { return v < 0 ? -v : v; }
static float min_f(float a, float b) { return a < b ? a : b; }
static int ifl(float v)
{
    const int i = (int)v;
    return (float)i > v ? i - 1 : i;
}
static int icl(float v)
{
    const int i = (int)v;
    return (float)i < v ? i + 1 : i;
}
static float sqrt_f(float v)
{
    if (!(v > 0)) return 0;
    uint32_t u;
    memcpy(&u, &v, 4);
    u = (u >> 1) + 0x1FC00000u;
    float x;
    memcpy(&x, &u, 4);
    for (int i = 0; i < 4; i++) x = 0.5f * (x + v / x);
    return x;
}
/* atan2 (radians, -pi..pi): Abramowitz and Stegun 4.4.49 on [0, 1] (|error| < 2e-8 there), by octant */
static float atan2_f(float y, float x)
{
    const float ax = fabs_f(x), ay = fabs_f(y);
    const float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (!(mx > 0)) return 0;
    const float a = mn / mx, s = a * a;
    float r = -0.0040540580f;
    r = r * s + 0.0218612288f;
    r = r * s - 0.0559098861f;
    r = r * s + 0.0964200441f;
    r = r * s - 0.1390853351f;
    r = r * s + 0.1994653599f;
    r = r * s - 0.3332985605f;
    r = r * s + 0.9999993329f;
    r *= a;
    if (ay > ax) r = PI / 2.f - r;
    if (x < 0) r = PI - r;
    return y < 0 ? -r : r;
}
/* the angle of (dx, dy) clockwise from 12, 0..2 pi */
static float theta_of(float dx, float dy)
{
    const float t = atan2_f(dx, -dy);
    return t < 0 ? t + TAU : t;
}
static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static omni_rect_t cut(omni_rect_t a, omni_rect_t b)
{
    omni_rect_t r = a;
    if (r.x0 < b.x0) r.x0 = b.x0;
    if (r.y0 < b.y0) r.y0 = b.y0;
    if (r.x1 > b.x1) r.x1 = b.x1;
    if (r.y1 > b.y1) r.y1 = b.y1;
    return r;
}
static omni_rect_t join(omni_rect_t a, omni_rect_t b)
{
    return (omni_rect_t){a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
                         a.y1 > b.y1 ? a.y1 : b.y1};
}
static omni_rect_t box_of(float x0, float y0, float x1, float y1)
{
    const int a = ifl(x0), b = ifl(y0), c = icl(x1) + 1, d = icl(y1) + 1;
    return (omni_rect_t){(int16_t)(a < 0 ? 0 : a > W ? W : a), (int16_t)(b < 0 ? 0 : b > W ? W : b),
                         (int16_t)(c < 0 ? 0 : c > W ? W : c), (int16_t)(d < 0 ? 0 : d > W ? W : d)};
}
static uint16_t *px_at(uint16_t *fb, int stride, omni_rect_t clip, int x, int y)
{
    return fb + (ptrdiff_t)(y - clip.y0) * stride + (x - clip.x0);
}
static float ease_out_back(float x)   /* the mockup's, c 1.4 */
{
    x = clamp01(x);
    const float u = x - 1.f;
    return 1.f + 2.4f * u * u * u + 1.4f * u * u;
}
static float ease_in_out(float x)   /* the mockup's easeInOut (cubic) */
{
    x = clamp01(x);
    if (x < 0.5f) return 4.f * x * x * x;
    const float u = -2.f * x + 2.f;
    return 1.f - u * u * u / 2.f;
}
/* sin(2 pi (t mod p) / p): a mockup's sin(t / k) with p = 2 pi k ms, kept exact over mono_ms's whole range */
static float wave(uint32_t t, uint32_t p)
{
    float s, c;
    fk_sincos(TAU * (float)(t % p) / (float)p, &s, &c);
    return s;
}
/* a point on radius r at angle th (from 12, clockwise) */
static void polar(float r, float th, float *x, float *y)
{
    float s, c;
    fk_sincos(th, &s, &c);
    *x = (float)C + r * s, *y = (float)C - r * c;
}

/* ---- a turn, a scale and a move: the canvas's translate(ox, oy) rotate(a) scale(k) */
typedef struct { float s, c, k, ox, oy; } xf_t;
static xf_t xf_of(float a, float k, float ox, float oy)
{
    xf_t t = {0, 0, k, ox, oy};
    fk_sincos(a, &t.s, &t.c);
    return t;
}
static omni_pt_t xp(const xf_t *t, float x, float y)
{
    return (omni_pt_t){t->ox + (x * t->c - y * t->s) * t->k, t->oy + (x * t->s + y * t->c) * t->k};
}

static void pbegin(path_t *P) { P->n = 0, P->nc = 0; }
static void pmove(path_t *P, const xf_t *t, float x, float y)
{
    const int s = P->nc ? P->end[P->nc - 1] : 0;
    if (P->n > s && P->nc < CMAX) P->end[P->nc++] = (uint8_t)P->n;   /* the open contour closes */
    if (P->n < PMAX) P->p[P->n++] = xp(t, x, y);
    P->cx = x, P->cy = y;
}
static void pline(path_t *P, const xf_t *t, float x, float y)
{
    if (P->n < PMAX) P->p[P->n++] = xp(t, x, y);
    P->cx = x, P->cy = y;
}
static void pclose(path_t *P)
{
    const int s = P->nc ? P->end[P->nc - 1] : 0;
    if (P->n > s && P->nc < CMAX) P->end[P->nc++] = (uint8_t)P->n;
}
/* roundRect(x, y, w, h, r) as its own contour: each corner's quarter circle in four steps */
static const float QC[5] = {1.f, 0.92387953f, 0.70710678f, 0.38268343f, 0.f};
static void prrect(path_t *P, const xf_t *t, float x, float y, float w, float h, float r)
{
    if (r > w / 2.f) r = w / 2.f;
    if (r > h / 2.f) r = h / 2.f;
    /* top-left, top-right, bottom-right, bottom-left: each corner from its first side's end */
    const float cx[4] = {x + r, x + w - r, x + w - r, x + r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    bool first = true;
    for (int q = 0; q < 4; q++)
        for (int k = 0; k < 5; k++) {
            /* the corner's unit vector from its first side to its second, clockwise on screen */
            float ux, uy;
            if (q == 0) ux = -QC[k], uy = -QC[4 - k];
            else if (q == 1) ux = QC[4 - k], uy = -QC[k];
            else if (q == 2) ux = QC[k], uy = QC[4 - k];
            else ux = -QC[4 - k], uy = QC[k];
            if (first) pmove(P, t, cx[q] + r * ux, cy[q] + r * uy), first = false;
            else pline(P, t, cx[q] + r * ux, cy[q] + r * uy);
        }
    pclose(P);
}
static void pbox(const path_t *P, float m, omni_rect_t *out)
{
    float x0 = P->p[0].x, y0 = P->p[0].y, x1 = x0, y1 = y0;
    for (int i = 1; i < P->n; i++) {
        x0 = P->p[i].x < x0 ? P->p[i].x : x0, x1 = P->p[i].x > x1 ? P->p[i].x : x1;
        y0 = P->p[i].y < y0 ? P->p[i].y : y0, y1 = P->p[i].y > y1 ? P->p[i].y : y1;
    }
    *out = box_of(x0 - m, y0 - m, x1 + m, y1 + m);
}

/* ---- exact area coverage (the signed-area rasteriser of curry.c and bowl.c), over several contours: nonzero */
#define CW 128        /* the widest run of a row covered at once */
static void put_acc(float *acc, int w, int i, float v)
{
    if (i < 0) i = 0;   /* left of the run: it adds to every cell of it */
    if (i <= w + 1) acc[i] += v;
}
static void deposit(float *acc, int w, float x, float xnext, float d)
{
    const float x0 = x < xnext ? x : xnext, x1 = x < xnext ? xnext : x;
    const int x0i = ifl(x0), x1i = icl(x1);
    const float x0floor = (float)x0i, x1ceil = (float)x1i;
    if (x1i <= x0i + 1) {
        const float xmf = 0.5f * (x + xnext) - x0floor;
        put_acc(acc, w, x0i, d - d * xmf);
        put_acc(acc, w, x0i + 1, d * xmf);
        return;
    }
    const float s = 1.f / (x1 - x0), x0f = x0 - x0floor, x1f = x1 - x1ceil + 1.f;
    const float a0 = 0.5f * s * (1.f - x0f) * (1.f - x0f), am = 0.5f * s * x1f * x1f;
    put_acc(acc, w, x0i, d * a0);
    if (x1i == x0i + 2) {
        put_acc(acc, w, x0i + 1, d * (1.f - a0 - am));
    } else {
        const float a1 = s * (1.5f - x0f);
        put_acc(acc, w, x0i + 1, d * (a1 - a0));
        for (int xi = x0i + 2; xi < x1i - 1 && xi <= w + 1; xi++) put_acc(acc, w, xi, d * s);
        const float a2 = a1 + (float)(x1i - x0i - 3) * s;
        put_acc(acc, w, x1i - 1, d * (1.f - a2 - am));
    }
    put_acc(acc, w, x1i, d * am);
}
/* the coverage (0..1) of contours [k0, k1) of P over pixel row y, columns [xa, xa + w) (w <= CW), into cov */
static void cover_row(const path_t *P, int k0, int k1, int y, int xa, int w, float *cov)
{
    float acc[CW + 2];
    memset(acc, 0, sizeof acc);
    const float y0 = (float)y, y1 = y0 + 1.f, ox = (float)xa;
    for (int k = k0; k < k1; k++) {
        const int s = k ? P->end[k - 1] : 0, e = P->end[k];
        for (int i = s; i < e; i++) {
            omni_pt_t a = P->p[i], b = P->p[i + 1 < e ? i + 1 : s];
            if (a.y == b.y) continue;
            float dir = 1.f;
            if (a.y > b.y) {
                const omni_pt_t t = a;
                a = b, b = t, dir = -1.f;
            }
            if (b.y <= y0 || a.y >= y1) continue;
            const float ya = a.y > y0 ? a.y : y0, yb = b.y < y1 ? b.y : y1;
            const float dxdy = (b.x - a.x) / (b.y - a.y);
            deposit(acc, w, a.x + (ya - a.y) * dxdy - ox, a.x + (yb - a.y) * dxdy - ox, (yb - ya) * dir);
        }
    }
    float s = 0;
    for (int x = 0; x < w; x++) {
        s += acc[x];
        const float v = s < 0 ? -s : s;
        cov[x] = v > 1.f ? 1.f : v;
    }
}

/* the path filled (nonzero) in rgb at alpha a */
static void fill_path(uint16_t *fb, int stride, omni_rect_t clip, const path_t *P, uint16_t rgb, float a)
{
    if (P->nc == 0) return;
    omni_rect_t bb;
    pbox(P, 1.f, &bb);
    const omni_rect_t r = cut(bb, clip);
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    float cf[CW];
    /* the runs start at the path's box, never at the clip, so every pixel's sums run in one order (band-exact) */
    for (int y = r.y0; y < r.y1; y++)
        for (int xa = bb.x0; xa < r.x1; xa += CW) {
            const int w = bb.x1 - xa < CW ? bb.x1 - xa : CW;
            if (xa + w <= r.x0) continue;
            cover_row(P, 0, P->nc, y, xa, w, cf);
            const int x0 = xa < r.x0 ? r.x0 - xa : 0;
            uint16_t *d = px_at(fb, stride, clip, xa + x0, y) - x0;
            for (int x = x0; x < w && xa + x < r.x1; x++) {
                const uint8_t v = a8_of(cf[x] * a);
                if (v) d[x] = omni_blend565(d[x], rgb, v);
            }
        }
}

/* the outline of every contour (closed, mitred as canvas strokes: the band between the contour offset out and in by
 * lw / 2), their union by the larger coverage, in rgb at alpha a */
static void stroke_path(uint16_t *fb, int stride, omni_rect_t clip, const path_t *P, path_t *O, path_t *I, float lw,
                        uint16_t rgb, float a)
{
    if (P->nc == 0) return;
    O->n = I->n = P->n, O->nc = I->nc = P->nc;
    for (int k = 0; k < P->nc; k++) {
        const int s = k ? P->end[k - 1] : 0, e = P->end[k];
        O->end[k] = I->end[k] = P->end[k];
        fk_offset_poly(P->p + s, e - s, lw * 0.5f, O->p + s);
        fk_offset_poly(P->p + s, e - s, -lw * 0.5f, I->p + s);
    }
    omni_rect_t bo, bi;
    pbox(O, 1.f, &bo);
    pbox(I, 1.f, &bi);
    const omni_rect_t bb = join(bo, bi), r = cut(bb, clip);
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    float co[CW], ci[CW], cs[CW];
    for (int y = r.y0; y < r.y1; y++)
        for (int xa = bb.x0; xa < r.x1; xa += CW) {
            const int w = bb.x1 - xa < CW ? bb.x1 - xa : CW;
            if (xa + w <= r.x0) continue;
            for (int x = 0; x < w; x++) cs[x] = 0;
            for (int k = 0; k < P->nc; k++) {
                cover_row(O, k, k + 1, y, xa, w, co);
                cover_row(I, k, k + 1, y, xa, w, ci);
                for (int x = 0; x < w; x++) {
                    const float v = co[x] > ci[x] ? co[x] - ci[x] : ci[x] - co[x];
                    cs[x] = v > cs[x] ? v : cs[x];
                }
            }
            const int x0 = xa < r.x0 ? r.x0 - xa : 0;
            uint16_t *d = px_at(fb, stride, clip, xa + x0, y) - x0;
            for (int x = x0; x < w && xa + x < r.x1; x++) {
                const uint8_t v = a8_of(cs[x] * a);
                if (v) d[x] = omni_blend565(d[x], rgb, v);
            }
        }
}

/* a polyline (closed: back to its first point) stroked lw with round caps and joins: each pixel's coverage by its
 * centre's distance to the nearest segment (lw / 2 + .5 - d), in rgb at alpha a */
static void stroke_round(uint16_t *fb, int stride, omni_rect_t clip, const omni_pt_t *p, int n, bool closed, float lw,
                         uint16_t rgb, float a)
{
    if (n < 2) return;
    const float hw = lw * 0.5f;
    float x0 = p[0].x, y0 = p[0].y, x1 = x0, y1 = y0;
    for (int i = 1; i < n; i++) {
        x0 = p[i].x < x0 ? p[i].x : x0, x1 = p[i].x > x1 ? p[i].x : x1;
        y0 = p[i].y < y0 ? p[i].y : y0, y1 = p[i].y > y1 ? p[i].y : y1;
    }
    const omni_rect_t r = cut(box_of(x0 - hw - 1.f, y0 - hw - 1.f, x1 + hw + 1.f, y1 + hw + 1.f), clip);
    const float lo = hw - 0.5f, hi = hw + 0.5f, lo2 = lo > 0 ? lo * lo : 0.f, hi2 = hi * hi;
    const int ns = closed ? n : n - 1;
    for (int y = r.y0; y < r.y1; y++) {
        uint16_t *d = px_at(fb, stride, clip, r.x0, y);
        const float py = (float)y + 0.5f;
        for (int x = r.x0; x < r.x1; x++, d++) {
            const float px = (float)x + 0.5f;
            float m2 = 1e30f;
            for (int i = 0; i < ns; i++) {
                const omni_pt_t A = p[i], B = p[i + 1 < n ? i + 1 : 0];
                const float ex = B.x - A.x, ey = B.y - A.y, wx = px - A.x, wy = py - A.y;
                const float ee = ex * ex + ey * ey;
                const float t = clamp01(ee > 0 ? (wx * ex + wy * ey) / ee : 0.f);
                const float dx = wx - ex * t, dy = wy - ey * t, d2 = dx * dx + dy * dy;
                m2 = d2 < m2 ? d2 : m2;
            }
            if (m2 >= hi2) continue;
            const float cov = m2 <= lo2 ? 1.f : hi - sqrt_f(m2);
            const uint8_t v = a8_of(cov * a);
            if (v) *d = omni_blend565(*d, rgb, v);
        }
    }
}

/* ---- the robot's line, the hit and the boxes */
float rd_mb_robot_turn(const fk_clock_t *c)
{
    if (!c->set) return PARK_TH / TAU;
    return ((float)(c->m % 60) + (float)(c->s % 60) / 60.f) / 60.f;
}

void rd_mb_robot_xy(const fk_clock_t *c, float *x, float *y) { polar(BOT_R, rd_mb_robot_turn(c) * TAU, x, y); }

/* the hit's centre: the robot's centre at the clock, to the nearest pixel (the harness's circle row rounds alike) */
static void hit_centre(const fk_clock_t *c, int *x, int *y)
{
    float fx, fy;
    rd_mb_robot_xy(c, &fx, &fy);
    *x = (int)(fx + 0.5f), *y = (int)(fy + 0.5f);
}

/* every pixel the robot drawn at th, scaled k (bot()'s 1.25 times its hop), can touch */
static omni_rect_t bot_box(float th, float k)
{
    float x, y;
    polar(BOT_R, th, &x, &y);
    const float h = BOT_REACH * k + 2.f;
    return box_of(x - h, y - h, x + h, y + h);
}
/* the sheen's tail end (its butt edge, r 196-214) about th - .32, clamped at 12 */
static omni_rect_t tail_box(float th)
{
    float t = th - ARC_LAG - SHEEN_LEN, x, y;
    if (t < 0) t = 0;
    polar(RR, t, &x, &y);
    return box_of(x - TAIL_HALF, y - TAIL_HALF, x + TAIL_HALF, y + TAIL_HALF);
}
/* the track's annulus (r RI - 1 .. RO + 1) over th0..th1: its box, by its ends and every .05 rad between */
static omni_rect_t arc_box(float th0, float th1)
{
    if (th1 - th0 >= TAU - 0.05f) return box_of(C - RO - 1.f, C - RO - 1.f, C + RO + 1.f, C + RO + 1.f);
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (float t = th0;; t += 0.05f) {
        const float tt = t > th1 ? th1 : t;
        for (int k = 0; k < 2; k++) {
            float x, y;
            polar(k ? RO + 1.f : RI - 1.f, tt, &x, &y);
            x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
        }
        if (t >= th1) break;
    }
    return box_of(x0 - 1.f, y0 - 1.f, x1 + 1.f, y1 + 1.f);
}

void rd_mb_robot_box(const fk_face_in_t *in, omni_rect_t *out)
{
    float x, y;
    rd_mb_robot_xy(&in->clk, &x, &y);
    *out = box_of(x - BOT_HALF_MAX, y - BOT_HALF_MAX, x + BOT_HALF_MAX, y + BOT_HALF_MAX);
}

void rd_mb_sheen_box(const fk_face_in_t *in, omni_rect_t *out)
{
    const float th = rd_mb_robot_turn(&in->clk) * TAU;
    omni_rect_t a = tail_box(th - WOBBLE), b = tail_box(th + WOBBLE);
    *out = join(a, b);
}

void rd_mb_ring_bands(omni_rect_t out[4])
{
    for (int i = 0; i < 4; i++) out[i] = RING_BANDS[i];
}

/* print k's place (k 0..4) and turn for the minute m */
static void print_at(unsigned m, int k, xf_t *t)
{
    const float a = ((float)m - 2.2f) / 60.f * TAU + (k % 2 ? 0.03f : -0.03f);
    float x, y;
    polar(RI - 22.f + 15.f * (float)k, a, &x, &y);
    *t = xf_of(a, 1.f, x, y);
}

void rd_mb_prints_box(unsigned m, omni_rect_t *out)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < 5; k++) {
        xf_t t;
        print_at(m % 60, k, &t);
        x0 = t.ox < x0 ? t.ox : x0, x1 = t.ox > x1 ? t.ox : x1, y0 = t.oy < y0 ? t.oy : y0, y1 = t.oy > y1 ? t.oy : y1;
    }
    *out = box_of(x0 - 13.f, y0 - 13.f, x1 + 13.f, y1 + 13.f);   /* a sole's reach (11.2) and AA */
}

unsigned rd_mb_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_MB_SIGN;
    out[1] = RD_MB_TRACK;
    return 2;
}

/* ---- the deck */
uint16_t rd_mb_tread_px(const void *ctx, int x, int y)
{
    const mb_t *k = ctx;
    return k->tread.rgb[(size_t)(y % TILE) * TILE + (size_t)(x % TILE)];
}

uint16_t rd_mb_deck_px(const void *ctx, int x, int y)
{
    const mb_t *k = ctx;
    const uint16_t t = k->tread.rgb[(size_t)(y % TILE) * TILE + (size_t)(x % TILE)];
    const int g = k->grime.a[(size_t)(y % GR_N) * GR_N + (size_t)(x % GR_N)];
    if (!g) return t;
    /* r squared in quarter pixels: (2x + 1 - 466)^2 + (2y + 1 - 466)^2 = 4 r^2 */
    const int dx = 2 * x + 1 - W, dy = 2 * y + 1 - W, q = dx * dx + dy * dy;
    const int q0 = 4 * GR_R0 * GR_R0, q1 = 4 * GR_R1 * GR_R1;
    const int kk = q >= q1 ? GK_TR : q <= q0 ? GK_IN : GK_IN + (int)((long)(GK_TR - GK_IN) * (q - q0) / (q1 - q0));
    int a = (g * kk) >> 8;
    if (a > GA_MAX) a = GA_MAX;
    return omni_blend565(t, GRIME_RGB, (uint8_t)a);
}

static void deck_draw(const mb_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = px_at(fb, stride, clip, clip.x0, y);
        for (int x = clip.x0; x < clip.x1; x++) *d++ = rd_mb_deck_px(k, x, y);
    }
    for (int i = 0; i < 60; i++) {
        const tick_t *t = &k->tick[i];
        if (meets(t->box, clip)) fk_line(fb, stride, &clip, t->a.x, t->a.y, t->b.x, t->b.y, t->w, t->rgb, t->a8);
    }
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced, h24;
    char t[6];          /* the time */
    char d[40];         /* the date line (AWAITING TIME SIGNAL unset) */
    int m;              /* the minute (the prints) */
    bool prints;
    float th;           /* the robot's angle, its wobble and the wake's race in */
    float k, spin;      /* its scale (the hop) and its spin */
    float sw, tr, aw;   /* the mop's swish, the cleats' roll, the antenna's sway */
    uint32_t sph;       /* the strands' phase (t mod their period) */
    bool still;
    uint16_t eye;
    bool eye_on;
    uint8_t glow;       /* the chime: the eye's swell */
    float eye_k, tip;   /* the eye's radius factor, the tip's (the beep) */
    float bump;         /* the click: the sign's tip */
    int ring;           /* the dash's crawl, px */
    int wk;             /* the wake's ms, -1 settled */
} mb_state_t;

#define STRAND_P 565u   /* sin(t / 90): 2 pi 90 ms */

static void state_of(const fk_face_in_t *in, mb_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    s->h24 = in->h24;
    s->wk = -1;
    fk_time_str(&in->clk, in->h24, s->t);
    if (s->set) {
        char d[16];
        fk_date_str(&in->clk, d);
        strcpy(s->d, d);
        strcat(s->d, fk_ampm(&in->clk, in->h24));
        strcat(s->d, " \267 DECK 14");
    } else {
        strcpy(s->d, "AWAITING TIME SIGNAL");
    }
    const bool live = !in->reduced && !in->glance;
    const uint32_t t = in->mono_ms;
    /* the ring's crawl: a px a second (R171: a px a minute under reduced motion); unset, by mono_ms */
    if (s->set) {
        const long sod = (in->clk.h % 24) * 3600L + (in->clk.m % 60) * 60L + (in->clk.s % 60);
        s->ring = (int)((in->reduced ? sod / 60 : sod) % (2 * DASH));
    } else {
        s->ring = (int)((in->reduced ? t / 60000u : t / 1000u) % (2u * DASH));
    }
    s->still = !s->set || in->reduced || in->glance;
    s->k = BOT_K;
    s->m = in->clk.m % 60;
    const float target = rd_mb_robot_turn(&in->clk) * TAU;
    if (!s->set) {
        s->th = PARK_TH;
    } else if (live && in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_MS) {
        s->wk = (int)in->wake_ms;
        s->th = PARK_TH + (target - PARK_TH) * ease_out_back((float)s->wk / (float)WAKE_MS);
    } else {
        s->th = target + (live ? wave(t, 2639u) * WOBBLE : 0.f);   /* sin(t / 420) x .012 */
    }
    s->prints = s->set && s->m >= 4 && s->wk < 0;
    if (!s->still) {
        s->sw = wave(t, 817u) * 0.35f;                     /* sin(t / 130) x .35 */
        s->tr = (float)(t % 160u) / 40.f;                  /* (t / 40) % 4 */
        s->aw = wave(t, 1257u) * 2.f;                      /* sin(t / 200) x 2 */
        s->sph = t % STRAND_P;
    }
    /* the eye: blinking (each 500 ms), steady under reduced motion, amber in a tap's first 800 ms, off unset */
    s->eye = in->reduced ? C_LAMP : (t / 500u) % 2u ? C_LAMP : C_EYE_DIM;
    if (in->pulsing && in->pulse_ms < HOP_MS) {
        s->eye = C_AMBER;
        if (!in->reduced) {
            const float p = (float)in->pulse_ms / (float)HOP_MS;
            s->spin = ease_in_out(p) * TAU;
            float sn, cs;
            fk_sincos(clamp01((float)in->pulse_ms / 400.f) * PI, &sn, &cs);
            s->k = BOT_K * (1.f + sn * 0.3f);
        }
    }
    s->eye_on = s->set;
    if (!s->set) s->eye = C_EYE_OFF;
    s->eye_k = 1.f;
    if (live && s->set) {
        const float th = clamp01(in->thrum), be = clamp01(in->beep), cl = clamp01(in->click);
        s->glow = a8_of(0.55f * th);
        s->eye_k = 1.f + 0.35f * th;
        s->tip = 0.8f * be;
        s->bump = 0.35f * cl;
    }
}

/* the robot's look differs between a and b (its place, scale, spin, motion, eye or chime) */
static bool bot_differs(const mb_state_t *a, const mb_state_t *b)
{
    return a->th != b->th || a->k != b->k || a->spin != b->spin || a->sw != b->sw || a->tr != b->tr ||
           a->aw != b->aw || a->sph != b->sph || a->eye != b->eye || a->eye_on != b->eye_on || a->glow != b->glow ||
           a->eye_k != b->eye_k || a->tip != b->tip;
}

/* ---- drawing */
/* the coverage (0..1) of the angles a..b (clockwise from 12) at th, r px out: the distance to the nearer end */
static float ang_cov(float th, float a, float b, float r)
{
    if (!(b > a)) return 0;
    const float mid = 0.5f * (a + b);
    if (th > mid + PI) th -= TAU;
    else if (th < mid - PI) th += TAU;
    return clamp01(r * min_f(th - a, b - th) + 0.5f);
}

/* an annulus r0..r1 (its AA included) row by row: the two spans of row y it covers, cut to clip */
static int annulus_spans(int y, float r0, float r1, omni_rect_t clip, int xs[4])
{
    const float dy = (float)y + 0.5f - (float)C, d2 = dy * dy;
    if (d2 >= r1 * r1) return 0;
    const float xo = sqrt_f(r1 * r1 - d2) + 1.f, xi = d2 < r0 * r0 ? sqrt_f(r0 * r0 - d2) - 1.f : 0.f;
    int n = 0;
    const int sp[2][2] = {{ifl((float)C - xo), icl((float)C - xi) + 1}, {ifl((float)C + xi), icl((float)C + xo) + 1}};
    for (int k = 0; k < 2; k++) {
        int a = sp[k][0], b = sp[k][1];
        if (k == 1 && xi <= 0 && n == 1 && a <= xs[1]) {   /* one span through the middle */
            xs[1] = b < clip.x1 ? b : clip.x1;
            continue;
        }
        if (a < clip.x0) a = clip.x0;
        if (b > clip.x1) b = clip.x1;
        if (a < b) xs[2 * n] = a, xs[2 * n + 1] = b, n++;
    }
    return n;
}

/* cleanArc(ba - .04): the annulus over 0..te (from 12) and its rings and sheen, clipped by its sector */
static void arc_draw(uint16_t *fb, int stride, omni_rect_t clip, float te)
{
    if (te > TAU) te = TAU;
    if (!(te > 0.001f)) return;
    const omni_rect_t r = cut(arc_box(0.f, te), clip);
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    const float ts = te - SHEEN_LEN > 0 ? te - SHEEN_LEN : 0.f;
    for (int y = r.y0; y < r.y1; y++) {
        int xs[4];
        const int n = annulus_spans(y, RI, RO, r, xs);
        const float dy = (float)y + 0.5f - (float)C;
        for (int q = 0; q < n; q++)
            for (int x = xs[2 * q]; x < xs[2 * q + 1]; x++) {
                const float dx = (float)x + 0.5f - (float)C, rr = sqrt_f(dx * dx + dy * dy);
                const float cr = clamp01(min_f(rr - RI, RO - rr) + 0.5f);
                if (!(cr > 0)) continue;
                const float th = theta_of(dx, dy), ca = ang_cov(th, 0.f, te, rr), cs = cr * ca;
                if (!(cs > 0)) continue;
                uint16_t *d = px_at(fb, stride, clip, x, y);
                *d = omni_blend565(*d, C_ARC, a8_of(cs));
                for (int k = 0; k < 6; k++) {
                    const float lc = clamp01(1.f - fabs_f(rr - (RI + 3.f + 5.f * (float)k)));
                    if (lc > 0) *d = omni_blend565(*d, C_RINGLINE, a8_of(lc * 0.05f * cs));
                }
                const float sr = clamp01(min_f(rr - (RR - SHEEN_W / 2.f), RR + SHEEN_W / 2.f - rr) + 0.5f);
                if (sr > 0) {
                    const float sa = ang_cov(th, ts, te, rr) * cr;
                    if (sa > 0) *d = omni_blend565(*d, C_SHEEN, a8_of(0.2f * sr * sa));
                }
            }
    }
}

/* the hazard ring: r 190, 2 px, hazard .35, dashed 10 on 10 along the circle from 3 o'clock, crawled off px on */
static void ring_draw(uint16_t *fb, int stride, omni_rect_t clip, int off)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        int xs[4];
        const int n = annulus_spans(y, RING_R - 1.5f, RING_R + 1.5f, clip, xs);
        const float dy = (float)y + 0.5f - (float)C;
        for (int q = 0; q < n; q++)
            for (int x = xs[2 * q]; x < xs[2 * q + 1]; x++) {
                const float dx = (float)x + 0.5f - (float)C, rr = sqrt_f(dx * dx + dy * dy);
                const float cr = clamp01(1.5f - fabs_f(rr - RING_R));
                if (!(cr > 0)) continue;
                float ph = atan2_f(dy, dx);
                if (ph < 0) ph += TAU;
                const float p = ph * RING_R - (float)off;
                float qd = p - (float)(2 * DASH) * (float)ifl(p / (float)(2 * DASH));
                if (qd >= 1.5f * DASH) qd -= (float)(2 * DASH);
                const float cs = clamp01(min_f(qd, (float)DASH - qd) + 0.5f);
                if (!(cs > 0)) continue;
                uint16_t *d = px_at(fb, stride, clip, x, y);
                *d = omni_blend565(*d, C_HAZARD, a8_of(0.35f * cr * cs));
            }
    }
}

static void prints_draw(const mb_t *k, uint16_t *fb, int stride, omni_rect_t clip, unsigned m)
{
    omni_rect_t b;
    rd_mb_prints_box(m, &b);
    if (!meets(b, clip)) return;
    path_t *P = (path_t *)(uintptr_t)&k->P;   /* the scratch (see the header) */
    for (int i = 0; i < 5; i++) {
        xf_t t;
        print_at(m, i, &t);
        pbegin(P);
        prrect(P, &t, -5.f, -10.f, 10.f, 12.f, 4.f);
        prrect(P, &t, -4.5f, 4.f, 9.f, 6.f, 2.5f);
        fill_path(fb, stride, clip, P, C_PRINT, 0.9f);
        pbegin(P);
        for (int j = 0; j < 3; j++) {
            const float y = -7.f + 3.5f * (float)j;
            pmove(P, &t, -3.5f, y), pline(P, &t, 3.5f, y), pline(P, &t, 3.5f, y + 1.f), pline(P, &t, -3.5f, y + 1.f);
            pclose(P);
        }
        fill_path(fb, stride, clip, P, 0, 0.35f);
    }
}

/* wetSign(): the triangle on r 205 at -.1 rad, tipped bump rad about its foot's middle (0, 9) */
static void sign_draw(const mb_t *k, uint16_t *fb, int stride, omni_rect_t clip, float bump)
{
    if (!meets(RD_MB_SIGN, clip)) return;
    float x, y;
    polar(RR, SIGN_TH, &x, &y);
    /* rotate(-bump) about (x, y + 9), then the sign's own frame from (x, y): the foot's middle stays put */
    const xf_t r = xf_of(-bump, 1.f, x, y + 9.f);
    path_t *P = (path_t *)(uintptr_t)&k->P;
    pbegin(P);
    pmove(P, &r, 0.f, -21.f), pline(P, &r, 12.f, 0.f), pline(P, &r, -12.f, 0.f), pclose(P);
    fill_path(fb, stride, clip, P, C_HAZARD, 1.f);
    const omni_pt_t tri[3] = {P->p[0], P->p[1], P->p[2]};
    stroke_round(fb, stride, clip, tri, 3, true, 1.5f, C_HAZARD_DARK, 1.f);
    pbegin(P);
    pmove(P, &r, -1.2f, -14.f), pline(P, &r, 1.2f, -14.f), pline(P, &r, 1.2f, -6.f), pline(P, &r, -1.2f, -6.f);
    pclose(P);
    pmove(P, &r, -1.2f, -4.f), pline(P, &r, 1.2f, -4.f), pline(P, &r, 1.2f, -2.f), pline(P, &r, -1.2f, -2.f);
    pclose(P);
    fill_path(fb, stride, clip, P, C_HAZARD_DARK, 1.f);
}

/* the time's characters on the mockup's fixed advances, tightened to .9, centred on cx: drawTime()'s layout (each
 * glyph's cell centred in its own tightened advance) */
static int adv_of(const fk_atlas_t *at, char ch)
{
    for (int i = 0; at->glyphs[i]; i++)
        if (at->glyphs[i] == ch) return at->cells[i].adv;
    return 0;
}
static void time_draw(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *t, float cx, int y,
                      uint16_t rgb)
{
    float w = 0;
    for (const char *p = t; *p; p++) w += (float)adv_of(at, *p) * TIGHT;
    float x = cx - w / 2.f;
    for (const char *p = t; *p; p++) {
        const float a = (float)adv_of(at, *p), centre = x + a * TIGHT / 2.f;
        const char one[2] = {*p, 0};
        fk_atlas_text_rgb(fb, stride, clip, at, one, ifl(centre - a / 2.f + 0.5f), y, 0, rgb, 255, 0);
        x += a * TIGHT;
    }
}

/* bot(): the robot at (x, y), turned th + spin, scaled k */
static void bot_draw(const mb_t *k, const mb_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    float x, y;
    polar(BOT_R, s->th, &x, &y);
    const omni_rect_t bb = bot_box(s->th, s->k);
    if (!meets(bb, clip)) return;
    path_t *P = (path_t *)(uintptr_t)&k->P, *O = (path_t *)(uintptr_t)&k->O, *I = (path_t *)(uintptr_t)&k->I;
    const float hd = s->th + s->spin, kk = s->k;
    const xf_t t = xf_of(hd, kk, x, y), tm = xf_of(hd + s->sw, kk, x, y);
    /* the mop: its handle, then its 13 strands (quadratic curves), swishing */
    omni_pt_t a = xp(&tm, -10.f, 0.f), b = xp(&tm, -28.f, 0.f);
    fk_line(fb, stride, &clip, a.x, a.y, b.x, b.y, 2.5f * kk, C_MOP, 255);
    for (int i = -6; i <= 6; i++) {
        const float fi = (float)i, ai = (float)(i < 0 ? -i : i);
        float wy = 0.f;
        if (!s->still) {
            float sn, cs;
            fk_sincos(TAU * (float)s->sph / (float)STRAND_P + fi, &sn, &cs);   /* sin(t / 90 + i) */
            wy = sn * 0.8f;
        }
        const float x0 = -28.f, y0 = fi * 0.7f, qx = -34.f, qy = fi * 1.6f, x1 = -38.f - ai * 0.3f, y1 = fi * 2.2f + wy;
        omni_pt_t p[QN + 1];
        for (int j = 0; j <= QN; j++) {
            const float u = (float)j / (float)QN, m = 1.f - u;
            p[j] = xp(&tm, m * m * x0 + 2.f * m * u * qx + u * u * x1, m * m * y0 + 2.f * m * u * qy + u * u * y1);
        }
        stroke_round(fb, stride, clip, p, QN + 1, false, 1.6f * kk, C_STRAND, 1.f);
    }
    /* the treads and their cleats */
    pbegin(P);
    prrect(P, &t, -13.f, -16.f, 26.f, 7.f, 3.f);
    prrect(P, &t, -13.f, 9.f, 26.f, 7.f, 3.f);
    fill_path(fb, stride, clip, P, C_TREAD, 1.f);
    for (int i = -12; i < 13; i += 4) {
        const float cx = (float)i + s->tr;
        a = xp(&t, cx, -16.f), b = xp(&t, cx, -9.f);
        fk_line(fb, stride, &clip, a.x, a.y, b.x, b.y, kk, C_CLEAT, 255);
        a = xp(&t, cx, 9.f), b = xp(&t, cx, 16.f);
        fk_line(fb, stride, &clip, a.x, a.y, b.x, b.y, kk, C_CLEAT, 255);
    }
    /* the bucket: hazard, its black edge, the stripe, the grime dots */
    pbegin(P);
    prrect(P, &t, -12.f, -11.f, 25.f, 22.f, 6.f);
    fill_path(fb, stride, clip, P, C_HAZARD, 1.f);
    stroke_path(fb, stride, clip, P, O, I, 1.5f * kk, 0, 1.f);
    pbegin(P);
    pmove(P, &t, -3.f, -11.f), pline(P, &t, 1.f, -11.f), pline(P, &t, 1.f, 11.f), pline(P, &t, -3.f, 11.f), pclose(P);
    fill_path(fb, stride, clip, P, C_HAZARD_DARK, 1.f);
    a = xp(&t, -7.f, 6.f), b = xp(&t, 8.f, -7.f);
    fk_disc(fb, stride, &clip, a.x, a.y, 2.f * kk, C_DOT, a8_of(0.55f));
    fk_disc(fb, stride, &clip, b.x, b.y, 1.4f * kk, C_DOT, a8_of(0.55f));
    /* the lid and its glint */
    a = xp(&t, -4.f, 0.f), b = xp(&t, -5.5f, -2.f);
    fk_disc(fb, stride, &clip, a.x, a.y, 6.f * kk, C_LID, 255);
    fk_disc(fb, stride, &clip, b.x, b.y, 2.f * kk, C_GLINT, a8_of(0.35f));
    /* the eye, its glow (the chime: the quarters glow swelling with the thrum) */
    a = xp(&t, 11.f, 0.f);
    if (s->glow && meets(box_of(a.x - 27.f, a.y - 27.f, a.x + 27.f, a.y + 27.f), clip))
        fk_glow_add_lut(fb, stride, &clip, ifl(a.x + 0.5f), ifl(a.y + 0.5f), 26, k->glow, s->eye, s->glow);
    fk_disc(fb, stride, &clip, a.x, a.y, 3.4f * kk * s->eye_k, s->eye, 255);
    if (s->eye_on) fk_disc(fb, stride, &clip, a.x, a.y, 7.f * kk * s->eye_k, s->eye, a8_of(0.35f));
    /* the antenna and its tip (the chime's beep: it blinks star0, larger) */
    a = xp(&t, -6.f, -6.f), b = xp(&t, -14.f - s->aw, -14.f);
    fk_line(fb, stride, &clip, a.x, a.y, b.x, b.y, kk, C_STEEL_HI, 255);
    fk_disc(fb, stride, &clip, b.x, b.y, 1.8f * kk * (1.f + s->tip), s->tip > 0 ? C_STAR0 : C_LAMP, 255);
}

static void scene(const mb_t *k, const mb_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    deck_draw(k, fb, stride, clip);
    if (s->set) {
        arc_draw(fb, stride, clip, s->th - ARC_LAG);
        if (s->prints) prints_draw(k, fb, stride, clip, (unsigned)s->m);
    }
    ring_draw(fb, stride, clip, s->ring);
    fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "30", (float)C, Y_30, TRACK13, true, C_PAINT_DIM, a8_of(0.9f));
    sign_draw(k, fb, stride, clip, s->bump);
    if (meets(RD_MB_TIME, clip))
        time_draw(fb, stride, clip, &k->s132, s->t, (float)C, Y_TIME, s->set ? C_HAZARD : C_PAINT_DIM);
    if (s->stale && meets(RD_MB_STALE, clip)) rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx, STALE_CY, false);
    if (meets(RD_MB_DATE, clip))
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, s->d, (float)C, Y_DATE, TRACK13, true, C_PAINT_DIM, 255);
    bot_draw(k, s, fb, stride, clip);
    if (!s->set) {
        float x, y;
        polar(BOT_R, PARK_TH, &x, &y);
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "Z Z", x + ZZ_DX, ifl(y + ZZ_DY + 0.5f), TRACK13, true,
                              C_PAINT_DIM, 255);
    }
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    mb_state_t s;
    state_of(in, &s);
    scene(ctx, &s, fb, stride, clip);
}

/* sleepFrame(): the outlined time in rd.g.hazard, the robot's dot (by the minute), the stale marker; through the orbit */
static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const mb_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(px_at(fb, stride, clip, clip.x0, y), 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const float ox = (float)in->odx, oy = (float)in->ody;
    char t[6];
    fk_time_str(&in->clk, in->h24, t);
    time_draw(fb, stride, clip, &k->s132o, t, (float)C + ox, Y_TIME + in->ody, RD_G_HAZARD);
    float x, y;
    polar(BOT_R, in->clk.set ? (float)(in->clk.m % 60) / 60.f * TAU : PARK_TH, &x, &y);
    fk_disc(fb, stride, &clip, x + ox, y + oy, 6.f, RD_G_HAZARD, 255);
    if (in->clk.set && in->clk.stale)
        rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx + in->odx, STALE_CY + in->ody, true);
}

/* ---- dirty(): what changed between a and b */
typedef struct { omni_rect_t r[8], u; unsigned n; } rects_t;
static void add(rects_t *L, omni_rect_t r)
{
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    if (L->n < 8) L->r[L->n] = r;
    L->u = L->n == 0 ? r : join(L->u, r);
    L->n++;
}
static uint8_t whole(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody &&
                          (!a->clk.set || (a->clk.h == b->clk.h && a->clk.m == b->clk.m && a->h24 == b->h24));
        return same ? 0 : whole(out);
    }
    mb_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || sa.reduced != sb.reduced || sa.h24 != sb.h24) return whole(out);
    rects_t L;
    memset(&L, 0, sizeof L);
    /* the robot, the arc's ends and the sheen: a small step moves only the arc's two ends (its body and the sheen's are
     * even along the track); a larger one (the wake's race, a skip) redraws the track between */
    if (bot_differs(&sa, &sb)) {
        const float d = sa.th > sb.th ? sa.th - sb.th : sb.th - sa.th;
        if (d <= 0.04f) {
            add(&L, join(bot_box(sa.th, sa.k), bot_box(sb.th, sb.k)));
            if (sa.th != sb.th && sa.set) add(&L, join(tail_box(sa.th), tail_box(sb.th)));
        } else {
            add(&L, bot_box(sa.th, sa.k));
            add(&L, bot_box(sb.th, sb.k));
            if (sa.set) {
                const float lo = sa.th < sb.th ? sa.th : sb.th, hi = sa.th < sb.th ? sb.th : sa.th;
                add(&L, arc_box(lo - ARC_LAG - SHEEN_LEN - 0.01f < 0 ? 0.f : lo - ARC_LAG - SHEEN_LEN - 0.01f, hi));
            }
        }
    }
    if (sa.prints != sb.prints || (sa.prints && sa.m != sb.m)) {
        omni_rect_t pa = {0, 0, 0, 0}, pb = {0, 0, 0, 0};
        if (sa.prints) rd_mb_prints_box((unsigned)sa.m, &pa);
        if (sb.prints) rd_mb_prints_box((unsigned)sb.m, &pb);
        add(&L, !sa.prints ? pb : !sb.prints ? pa : join(pa, pb));
    }
    if (sa.ring != sb.ring)
        for (int i = 0; i < 4; i++) add(&L, RING_BANDS[i]);
    const bool tch = strcmp(sa.t, sb.t) != 0, dch = strcmp(sa.d, sb.d) != 0;
    if (tch && dch) add(&L, join(RD_MB_TIME, RD_MB_DATE));
    else if (tch) add(&L, RD_MB_TIME);
    else if (dch) add(&L, RD_MB_DATE);
    if (sa.stale != sb.stale) add(&L, RD_MB_STALE);
    if (sa.bump != sb.bump) add(&L, RD_MB_SIGN);
    if (L.n > 8) {
        out[0] = L.u;
        return 1;
    }
    for (unsigned i = 0; i < L.n; i++) out[i] = L.r[i];
    return (uint8_t)L.n;
}

/* no clock, no robot: tap() taps nothing (tap_var holds the robot's place at the input's clock, I3) */
static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    (void)x;
    (void)y;
    return FK_TAP_NONE;
}

/* a tap within RD_MB_HIT_R of the robot's centre at in's clock (inclusive, M10: the centre to its nearest pixel)
 * startles it (the pulse; the variant left: the face has none). Under reduced motion the pulse plays too: its eye
 * flashes. Unset it is parked by the sign. A NULL in (only from outside the runtime, fk/face.h) has no clock: nothing */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    (void)ctx;
    (void)variant;
    if (in == NULL || in->glance) return FK_TAP_NONE;
    int cx, cy;
    hit_centre(&in->clk, &cx, &cy);
    const int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= RD_MB_HIT_R * RD_MB_HIT_R ? FK_TAP_PULSE : FK_TAP_NONE;
}

static bool bind(void *ctx, fk_asset_fn get)
{
    mb_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_MB_TREAD, &n);
    bool ok = b && fk_img_from_bin(&k->tread, b, n) && k->tread.rgb && !k->tread.a && k->tread.w == TILE &&
              k->tread.h == TILE;
    b = get(ASSET_RD_GRIME, &n);
    ok = ok && b && fk_img_from_bin(&k->grime, b, n) && k->grime.a && k->grime.w == GR_N && k->grime.h == GR_N;
    b = get(ASSET_RD_F_S132, &n);
    ok = ok && b && fk_img_from_bin(&k->s132_img, b, n) && k->s132_img.a;
    b = get(ASSET_RD_F_S132O, &n);
    ok = ok && b && fk_img_from_bin(&k->s132o_img, b, n) && k->s132o_img.a;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    if (!ok) return false;
    k->s132 = (fk_atlas_t){&k->s132_img, RD_CELLS_RD_F_S132, RD_GLYPHS_RD_F_S132, ASSET_RD_F_S132_LINE_H};
    k->s132o = (fk_atlas_t){&k->s132o_img, RD_CELLS_RD_F_S132O, RD_GLYPHS_RD_F_S132O, ASSET_RD_F_S132O_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->glow = rd_glow_lut(26);
    if (!k->glow) return false;
    for (int i = 0; i < 60; i++) {
        tick_t *t = &k->tick[i];
        const bool five = i % 5 == 0;
        const float th = (float)i / 60.f * TAU, len = five ? 10.f : 5.f;
        float x0, y0, x1, y1;
        polar(TICK_R - len, th, &x0, &y0);
        polar(TICK_R, th, &x1, &y1);
        t->a = (omni_pt_t){x0, y0}, t->b = (omni_pt_t){x1, y1};
        t->w = five ? 2.5f : 1.5f;
        t->rgb = five ? C_PAINT : C_PAINT_DIM;
        t->a8 = a8_of(five ? 0.8f : 0.45f);
        t->box = box_of((x0 < x1 ? x0 : x1) - 2.f, (y0 < y1 ? y0 : y1) - 2.f, (x0 > x1 ? x0 : x1) + 2.f,
                        (y0 > y1 ? y0 : y1) + 2.f);
    }
    /* staleMark(233, 161)'s group (as curry.c's and bowl.c's): w = measureText('STALE') with its trailing spacing + 14 */
    const float wt = fk_atlas_width_tracked(&k->ch13, "STALE", TRACK13) + TRACK13 + 14.f;
    k->stale_cx = (int16_t)((float)C - wt / 2.f + 34.f + 0.5f);
    return true;
}

const fk_face_ops_t RD_FACE_MOPBOT = {
    .key = "mopbot",
    .ctx_bytes = sizeof(mb_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = WAKE_MS,
    .pulse_len_ms = PULSE_MS,   /* the hop and spin (800 ms) and its settle */
    .pulse_guard_ms = HOP_MS,   /* a tap during the spin leaves it (the mockup's 800 ms guard) */
    .tap_var = tap_var,
    .stills_extra = 1,          /* still 11: the robot startled, a tap RD_MB_HOP_STILL_MS ago (rd_still_pulse_ms) */
};
