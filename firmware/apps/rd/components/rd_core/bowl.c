/* The Bowl (`bowl`, rd/faces.h; rd wave 2 Task 9, spec section 10.11; faces-quarters.js:508-673: buildBg(),
 * kibble(), kpos(), fishWhole(), fishBones(), paw(), frame(), pointer() and sleepFrame()).
 *
 * The base: the bowl (rd_cb_bowl, buildBg()'s canvas cut to (17, 17, 432 x 432) and to its disc, r 217: the chipped
 * enamel lip with its painted numerals, the steel wall and the polished floor engraved MINE) on black. Over it, in
 * frame()'s order, with the clock set:
 *   the kibbles (kibble(), kpos()): sixty round the wall, i at i / 60 of a turn less a quarter plus its da, r 152 plus
 *   its dr, from KIB (the mockup's rng(99): dr, da, rot, s, its colour #8a5a2b, #6d4420 or #a06a34, fx, fy, drawn in
 *   that order for each); i < m is a crumb (fillRect(x, y, 1.6, 1.6) in rgba(120, 80, 40, .5)), the rest a kibble: the
 *   triangle (0, -6), (5.5, 3.5), (-5.5, 3.5) turned by rot and scaled by s, stroked 3 px (round joins) and filled in
 *   its colour, then its highlight (r 2 at (-1, -1)) in rgba(255, 220, 170, .22). The wake rains them in round the ring
 *   (d = (wk - 80 - (i - m) / max(1, 60 - m) x 560) / 260: scale 1.9 to 1 by easeOutBack(d), dropped from 26 px above,
 *   alpha d; the stroke and the fill each at it, as the canvas lays them);
 *   the stale marker at the top of the floor (staleMark(233, 167));
 *   the hands at the minute (lesson 9: the mockup's mm carries the seconds; ours steps once a minute): their shadows
 *   (translate(3, 5): the kipper filled in black .5, the skeleton's strokes in black .5 each), the kipper (fishWhole():
 *   the tail fin, the two side fins and the body (three quadratic curves) as one path filled nonzero, so the left fin's
 *   root, wound against the body, is a notch as the canvas draws it; its fill hull to hullLit (.45) to hull across its
 *   own x from -12 to 12, its 1.5 px black outline, the paint stripe (3 px, round caps) and the eyes), the skeleton
 *   (fishBones(): the spine, the tail's V, seven ribs (two quadratic curves each) and the head's triangle, each stroked
 *   with round caps and joins, 6.2 px black then 3.2 px paint, and its eye) and the hub (steel r 6, its 1.5 px black
 *   ring and the steelHi glint);
 *   the paw (paw()) while it runs: about (233, 233) at (m + .5) / 60 of a turn plus .11 swipe, moved out 272 - 116
 *   reach: the forearm (its fur gradient, a 1.6 px edge, five tabby stripes), the claws (450-950 ms), the white sock (an
 *   ellipse and four toes, filled and stroked, then filled again inside) and the toe creases; the next kibble (m) is
 *   batted onto the floor from 650 ms (easeOut3 over 700 ms toward (233 + fx, 223 + fy), turning 9 rad), and is back in
 *   its place when the run ends.
 * Unset: the bowl empty (no kibbles, fish or hub), --:-- (stencil 64, paintDim, centred, its baseline 247) and BOWL
 * EMPTY (Chakra 13 tracked 1.8, paintDim, its 'middle' 273 on the baseline 276).
 * The paw's schedule (rd_cb_paw_ms; plan W2-D6): a tap's pulse first; then, while the wake is fresh, at fresh_ms 2 500 +
 * 20 000 k for 1.6 s (the mockup's wake-relative cycle); after the fresh minute, whenever the clock's second of the day
 * is a multiple of 20 (the face keeps no state). None unset, at m 59 (the mockup's m < 59: the last kibble stays),
 * under reduced motion or in the glance.
 * The chime picture (awake, set, not reduced): the floor's sheen brightens with the thrum (steelHi added through the
 * soft glow r 56 at the sheen's centre (197, 189), .45 thrum), and the next kibble hops 6 px up with the beep.
 * The glance (sleepFrame()): the lip's hairline (rd.g.hull 1.5 px at r 214), 12, 3, 6 and 9 (stencil 30, rd.g.paint),
 * the kipper's outline (rd.g.hull 1.4 px) and the skeleton (rd.g.paint 1.3 px, no outline); unset the --:-- outlines;
 * stale the marker in rd.g.amber; all through the orbit.
 *
 * Pure: no esp_*, freertos or LVGL; no libm (every angle through fk_sincos, square roots by Newton's steps, floors by
 * casts); no allocation; no settings, store or NVS symbol, and no state past a frame: the face keeps nothing between
 * draws (the paw's schedule is the input's). Its kibbles, fish and paw are polygons (the spec's IP note: a generic
 * tabby paw), filled by exact area coverage (the signed-area rasteriser Curry Clock's hands use, here over several
 * contours: nonzero) or, for the round-capped strokes, by the distance to their segments. */
#include <stdint.h>
#include <string.h>
#include "rd/faces.h"
#include "rd/assets_meta.h"
#include "fk/compose.h"
#include "fk/shapes.h"
#include "omni/blend565.h"

#define W 466
#define C 233
#define BOX 432        /* the bowl layer: its lip's box */
#define BX 17          /* its top-left: C - 216 */
#define PI 3.14159265359f
#define TAU 6.28318530718f
/* the R86 rounding rd_pack.py packs with (tools/facepack.py rgb565()) */
#define R86(r, g, b) \
    (uint16_t)((((r) * 31 + 127) / 255) << 11 | (((g) * 63 + 127) / 255) << 5 | (((b) * 31 + 127) / 255))
#define TRACK13 1.8f   /* lab(): (13 x .14).toFixed(1) px */
#define NUM_R 200.f    /* the numerals' ring (baked in the layer; the glance draws four), their baseline 11 px below */
#define NUM_DY 11.f
#define GL_NUM_UP 3    /* sleepFrame()'s fillText(n, x, y + 1) on the 'middle' line: 3 px above the awake baseline */
#define LIP_GR 214.f   /* the glance's lip hairline */
#define HOUR_LEN 100.f
#define MIN_LEN 152.f
#define SHADOW_DX 3.f
#define SHADOW_DY 5.f
#define KIB_N 60
#define KIB_R 152.f
#define WAKE_MS 1000
#define HANDS_MS 900   /* the fish swing to the time over 900 ms */
#define PAW_MS 1600
#define PAW_FIRST 2500   /* fresh_ms of the first run */
#define PAW_EVERY 20000
#define BAT_MS 650       /* the next kibble leaves the ring */
#define BAT_LEN 700
#define STALE_CY 171     /* staleMark(233, 167): the ring at 167 (cy - 4.5 of the shared marker's) and STALE on 171 */
#define Y_UNSET_TIME 247 /* drawTime('--:--', 64, paintDim, cx, cy + 14): the baseline 233 + 14 */
#define Y_UNSET_LINE 276 /* lab('BOWL EMPTY', cx, cy + 40, 13): 273 middle + 3.4 */
#define HOP_PX 6.f
#define SHEEN_X 197      /* the floor gradient's inner circle (cx - 36, cy - 44) */
#define SHEEN_Y 189
#define SHEEN_R 56       /* a soft glow wholly inside the floor (56.8 + 56 < 118) */
#define SHEEN_A 0.45f
/* a kibble's reach about its point: the triangle's farthest vertex (6.52) and the stroke's half (1.5), at scale 1 */
#define KIB_REACH 8.02f

const omni_rect_t RD_CB_BOWL = {BX, BX, BX + BOX, BX + BOX};
const omni_rect_t RD_CB_HIT = {BX, BX, BX + BOX, BX + BOX};
const omni_rect_t RD_CB_FLOOR = {114, 114, 353, 353};
const omni_rect_t RD_CB_STALE = {190, 158, 276, 178};
const omni_rect_t RD_CB_RING = {62, 57, 405, 405};
const omni_rect_t RD_CB_SHEEN = {SHEEN_X - SHEEN_R - 1, SHEEN_Y - SHEEN_R - 1, SHEEN_X + SHEEN_R + 2,
                                 SHEEN_Y + SHEEN_R + 2};
const omni_rect_t RD_CB_HUB = {225, 225, 242, 242};

/* ---- paths: points in contours (end[k]: one past contour k's last point) */
#define PMAX 96
#define CMAX 8
#define QN 8          /* a quadratic curve's steps */
typedef struct { omni_pt_t p[PMAX]; uint8_t end[CMAX]; int n, nc; float cx, cy; } path_t;
typedef struct { float dr, da, rot, s, fx, fy; uint8_t c; } kib_t;

typedef struct {
    fk_img_t bowl, s30_img, s64_img, ch13_img;
    fk_atlas_t s30, s64, ch13;
    const uint8_t *sheen;       /* RD_GLOW_LUT_56 (soft) */
    kib_t kib[KIB_N];
    float kx[KIB_N], ky[KIB_N];  /* kpos(i) */
    omni_rect_t kbox[KIB_N];     /* kibble i at rest and its crumb */
    int16_t stale_cx;
    float num_cx[12];
    int16_t num_base[12];
    /* draw()'s scratch: a path and its two offsets (off the LVGL task's stack; written before each read) */
    path_t P, O, I;
} cb_t;

/* ---- small helpers */
static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
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
static uint16_t rgb565_of(float r, float g, float b)
{
    const int R = (int)(r + 0.5f), G = (int)(g + 0.5f), B = (int)(b + 0.5f);
    return R86(R < 0 ? 0 : R > 255 ? 255 : R, G < 0 ? 0 : G > 255 ? 255 : G, B < 0 ? 0 : B > 255 ? 255 : B);
}
/* 'lighten' (the glances' ink, as fk_ring_max): per field the larger of the pixel and rgb at a over black */
static uint16_t lighten(uint16_t d, uint16_t rgb, int a)
{
    const uint16_t s = omni_blend565(0, rgb, a);
    const uint16_t r = (d & 0xf800u) > (s & 0xf800u) ? d & 0xf800u : s & 0xf800u;
    const uint16_t g = (d & 0x07e0u) > (s & 0x07e0u) ? d & 0x07e0u : s & 0x07e0u;
    const uint16_t b = (d & 0x001fu) > (s & 0x001fu) ? d & 0x001fu : s & 0x001fu;
    return (uint16_t)(r | g | b);
}
static uint16_t *px_at(uint16_t *fb, int stride, omni_rect_t clip, int x, int y)
{
    return fb + (ptrdiff_t)(y - clip.y0) * stride + (x - clip.x0);
}
static float ease_out3(float x)
{
    const float u = 1.f - clamp01(x);
    return 1.f - u * u * u;
}
static float ease_in2(float x)
{
    x = clamp01(x);
    return x * x;
}
static float ease_out_back(float x)   /* the mockup's, c 1.4 */
{
    x = clamp01(x);
    const float u = x - 1.f;
    return 1.f + 2.4f * u * u * u + 1.4f * u * u;
}

/* ---- a turn and a move: the canvas's translate(ox, oy) rotate(a), a point (x, y) of the frame to the screen */
typedef struct { float s, c, ox, oy; } xf_t;
static xf_t xf_of(float a, float ox, float oy)
{
    xf_t t = {0, 0, ox, oy};
    fk_sincos(a, &t.s, &t.c);
    return t;
}
static omni_pt_t xp(const xf_t *t, float x, float y) { return (omni_pt_t){t->ox + x * t->c - y * t->s, t->oy + x * t->s + y * t->c}; }

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
static void pquad(path_t *P, const xf_t *t, float qx, float qy, float x, float y)
{
    const float x0 = P->cx, y0 = P->cy;
    for (int k = 1; k <= QN; k++) {
        const float u = (float)k / (float)QN, m = 1.f - u;
        pline(P, t, m * m * x0 + 2.f * m * u * qx + u * u * x, m * m * y0 + 2.f * m * u * qy + u * u * y);
    }
}
static void pclose(path_t *P)
{
    const int s = P->nc ? P->end[P->nc - 1] : 0;
    if (P->n > s && P->nc < CMAX) P->end[P->nc++] = (uint8_t)P->n;
}
/* an ellipse (rx, ry) about (x, y) of the frame, n points, as its own contour */
static void pellipse(path_t *P, const xf_t *t, float x, float y, float rx, float ry, int n)
{
    for (int k = 0; k < n; k++) {
        float s, c;
        fk_sincos(TAU * (float)k / (float)n, &s, &c);
        if (k == 0) pmove(P, t, x + rx * c, y + ry * s);
        else pline(P, t, x + rx * c, y + ry * s);
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

/* ---- exact area coverage (the signed-area rasteriser, curry.c's), over several contours: nonzero */
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

/* a paint: one colour, or a three-stop linear gradient (a, b at mid, a) along a frame's x from g0 to g1 */
typedef struct { bool grad; uint16_t rgb; xf_t t; float g0, g1, mid; uint8_t a[3], b[3]; } paint_t;
static uint16_t paint_at(const paint_t *q, int x, int y)
{
    if (!q->grad) return q->rgb;
    const float dx = (float)x + 0.5f - q->t.ox, dy = (float)y + 0.5f - q->t.oy;
    const float lx = dx * q->t.c + dy * q->t.s;   /* the frame's x */
    const float u = clamp01((lx - q->g0) / (q->g1 - q->g0));
    const float f = u < q->mid ? u / q->mid : (1.f - u) / (1.f - q->mid);
    return rgb565_of(q->a[0] + ((float)q->b[0] - q->a[0]) * f, q->a[1] + ((float)q->b[1] - q->a[1]) * f,
                     q->a[2] + ((float)q->b[2] - q->a[2]) * f);
}
static paint_t solid(uint16_t rgb)
{
    paint_t q;
    memset(&q, 0, sizeof q);
    q.rgb = rgb;
    return q;
}

/* the path filled (nonzero) in q at alpha a */
static void fill_path(uint16_t *fb, int stride, omni_rect_t clip, const path_t *P, const paint_t *q, float a)
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
                if (v) d[x] = omni_blend565(d[x], paint_at(q, xa + x, y), v);
            }
        }
}

/* the outline of every contour (closed, mitred as canvas strokes: the band between the contour offset out and in by
 * lw / 2), their union by the larger coverage, in rgb at alpha a; glance: 'lighten' */
static void stroke_path(uint16_t *fb, int stride, omni_rect_t clip, const path_t *P, path_t *O, path_t *I, float lw,
                        uint16_t rgb, float a, bool glance)
{
    if (P->nc == 0) return;
    O->n = I->n = P->n, O->nc = I->nc = P->nc;
    for (int k = 0; k < P->nc; k++) {
        const int s = k ? P->end[k - 1] : 0, e = P->end[k];
        O->end[k] = I->end[k] = P->end[k];
        fk_offset_poly(P->p + s, e - s, lw * 0.5f, O->p + s);
        fk_offset_poly(P->p + s, e - s, -lw * 0.5f, I->p + s);
    }
    omni_rect_t bo, bi, bb;
    pbox(O, 1.f, &bo);
    pbox(I, 1.f, &bi);
    bb = join(bo, bi);
    const omni_rect_t r = cut(bb, clip);
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
                if (!v) continue;
                d[x] = glance ? lighten(d[x], rgb, v) : omni_blend565(d[x], rgb, v);
            }
        }
}

/* an open polyline (closed: back to its first point) stroked lw with round caps and joins: each pixel's coverage by
 * its centre's distance to the nearest segment (lw / 2 + .5 - d), in rgb at alpha a; glance: 'lighten' */
static void stroke_round(uint16_t *fb, int stride, omni_rect_t clip, const omni_pt_t *p, int n, bool closed, float lw,
                         uint16_t rgb, float a, bool glance)
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
                float t = ee > 0 ? (wx * ex + wy * ey) / ee : 0.f;
                t = clamp01(t);
                const float dx = wx - ex * t, dy = wy - ey * t, d2 = dx * dx + dy * dy;
                m2 = d2 < m2 ? d2 : m2;
            }
            if (m2 >= hi2) continue;
            const float cov = m2 <= lo2 ? 1.f : hi - sqrt_f(m2);
            const uint8_t v = a8_of(cov * a);
            if (!v) continue;
            *d = glance ? lighten(*d, rgb, v) : omni_blend565(*d, rgb, v);
        }
    }
}

/* fillRect(x, y, w, h) in rgb at a: each pixel at its exact area */
static void fill_rect(uint16_t *fb, int stride, omni_rect_t clip, float x, float y, float w, float h, uint16_t rgb,
                      float a)
{
    const omni_rect_t r = cut(box_of(x, y, x + w, y + h), clip);
    for (int py = r.y0; py < r.y1; py++) {
        const float vy = (y + h < (float)py + 1.f ? y + h : (float)py + 1.f) - (y > (float)py ? y : (float)py);
        if (!(vy > 0)) continue;
        uint16_t *d = px_at(fb, stride, clip, r.x0, py);
        for (int px = r.x0; px < r.x1; px++, d++) {
            const float vx = (x + w < (float)px + 1.f ? x + w : (float)px + 1.f) - (x > (float)px ? x : (float)px);
            if (!(vx > 0)) continue;
            const uint8_t v = a8_of(vx * vy * a);
            if (v) *d = omni_blend565(*d, rgb, v);
        }
    }
}

/* ---- the kibbles (kibble(), kpos()) */
static const uint16_t KIB_RGB[3] = {R86(0x8A, 0x5A, 0x2B), R86(0x6D, 0x44, 0x20), R86(0xA0, 0x6A, 0x34)};
#define KIB_HI R86(255, 220, 170)
#define CRUMB R86(120, 80, 40)

/* the mockup's rng(seed): mulberry32, its value in [0, 1) */
static double mb_next(uint32_t *s)
{
    *s += 0x6D2B79F5u;
    uint32_t t = *s;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double)(t ^ (t >> 14)) / 4294967296.0;
}

static omni_rect_t kib_box_at(float x, float y, float sc, float up)
{
    const float r = KIB_REACH * sc + 1.f;
    return box_of(x - r, y - r - up, x + r, y + r);
}

/* the next kibble (m) at rest or hopped */
static omni_rect_t hop_box(const cb_t *k, int m) { return kib_box_at(k->kx[m], k->ky[m], k->kib[m].s, HOP_PX); }

/* kibble k at (x, y), turned rot, scaled sc, at alpha a: its stroke (3 px, round joins) and fill in its colour, then
 * the highlight */
static void kib_draw(uint16_t *fb, int stride, omni_rect_t clip, float x, float y, float rot, float sc, uint16_t rgb,
                     float a)
{
    const omni_rect_t r = cut(kib_box_at(x, y, sc, 0.f), clip);
    if (r.x0 >= r.x1 || r.y0 >= r.y1 || !(a > 0)) return;
    const xf_t t = xf_of(rot, x, y);
    const omni_pt_t V[3] = {xp(&t, 0.f, -6.f * sc), xp(&t, 5.5f * sc, 3.5f * sc), xp(&t, -5.5f * sc, 3.5f * sc)};
    const omni_pt_t H = xp(&t, -1.f * sc, -1.f * sc);
    const float hw = 1.5f * sc, hr = 2.f * sc;
    /* the triangle's winding (its edge functions' sign inside) */
    const float area = (V[1].x - V[0].x) * (V[2].y - V[0].y) - (V[1].y - V[0].y) * (V[2].x - V[0].x);
    const float sg = area > 0 ? 1.f : -1.f;
    for (int py = r.y0; py < r.y1; py++) {
        uint16_t *d = px_at(fb, stride, clip, r.x0, py);
        const float cy = (float)py + 0.5f;
        for (int px = r.x0; px < r.x1; px++, d++) {
            const float cx = (float)px + 0.5f;
            float m2 = 1e30f;
            bool in = true;
            for (int i = 0; i < 3; i++) {
                const omni_pt_t A = V[i], B = V[(i + 1) % 3];
                const float ex = B.x - A.x, ey = B.y - A.y, wx = cx - A.x, wy = cy - A.y;
                if ((ex * wy - ey * wx) * sg < 0) in = false;
                float u = (wx * ex + wy * ey) / (ex * ex + ey * ey);
                u = clamp01(u);
                const float dx = wx - ex * u, dy = wy - ey * u, d2 = dx * dx + dy * dy;
                m2 = d2 < m2 ? d2 : m2;
            }
            const float de = sqrt_f(m2);
            const float cs = clamp01(hw + 0.5f - de), cf = in ? clamp01(0.5f + de) : clamp01(0.5f - de);
            const float hx = cx - H.x, hy = cy - H.y;
            const float ch = clamp01(hr + 0.5f - sqrt_f(hx * hx + hy * hy));
            uint8_t v = a8_of(cs * a);
            if (v) *d = omni_blend565(*d, rgb, v);
            v = a8_of(cf * a);
            if (v) *d = omni_blend565(*d, rgb, v);
            v = a8_of(ch * 0.22f * a);
            if (v) *d = omni_blend565(*d, KIB_HI, v);
        }
    }
}

/* ---- the fish (fishWhole(), fishBones()) in their frame (x across, -y toward the nose), turned a about (ox, oy) */
static void kipper_path(path_t *P, const xf_t *t)
{
    const float L = HOUR_LEN;
    pbegin(P);
    pmove(P, t, 0.f, 6.f), pline(P, t, -14.f, 26.f), pquad(P, t, 0.f, 18.f, 14.f, 26.f), pclose(P);   /* the tail */
    pmove(P, t, -4.f, -L * 0.45f), pline(P, t, -17.f, -L * 0.34f), pline(P, t, -5.f, -L * 0.26f), pclose(P);
    pmove(P, t, 4.f, -L * 0.45f), pline(P, t, 17.f, -L * 0.34f), pline(P, t, 5.f, -L * 0.26f), pclose(P);
    pmove(P, t, 0.f, 8.f);
    pquad(P, t, -15.f, -L * 0.4f, -3.f, -L * 0.92f);
    pquad(P, t, 0.f, -L, 3.f, -L * 0.92f);
    pquad(P, t, 15.f, -L * 0.4f, 0.f, 8.f);
    P->n--;   /* the last step lands on the first point: the contour closes on it */
    pclose(P);
}

/* the skeleton's strokes, each its own (the canvas strokes each beginPath alone): 0 the spine, 1 the tail's V, 2-8 the
 * ribs, 9 the head (closed) */
#define BONES_N 10
static int bone_path(int j, const xf_t *t, omni_pt_t *p, bool *closed)
{
    const float L = MIN_LEN;
    *closed = false;
    if (j == 0) {
        p[0] = xp(t, 0.f, 16.f), p[1] = xp(t, 0.f, -L + 22.f);
        return 2;
    }
    if (j == 1) {
        p[0] = xp(t, -9.f, 27.f), p[1] = xp(t, 0.f, 16.f), p[2] = xp(t, 9.f, 27.f);
        return 3;
    }
    if (j == 9) {
        *closed = true;
        p[0] = xp(t, 0.f, -L), p[1] = xp(t, -10.f, -L + 22.f), p[2] = xp(t, 10.f, -L + 22.f);
        return 3;
    }
    const int i = j - 2;
    float sn, cs;
    fk_sincos((float)(i + 1) / 8.f * PI, &sn, &cs);
    const float y = -18.f - (float)i * (L - 50.f) / 7.f, rw = 5.f + sn * 10.f;
    /* M(-rw, y + 6) Q(-rw .4, y - 1, 0, y) Q(rw .4, y - 1, rw, y + 6) */
    int n = 0;
    p[n++] = xp(t, -rw, y + 6.f);
    for (int k = 1; k <= QN; k++) {
        const float u = (float)k / (float)QN, m = 1.f - u;
        p[n++] = xp(t, m * m * -rw + 2.f * m * u * -rw * 0.4f, m * m * (y + 6.f) + 2.f * m * u * (y - 1.f) + u * u * y);
    }
    for (int k = 1; k <= QN; k++) {
        const float u = (float)k / (float)QN, m = 1.f - u;
        p[n++] = xp(t, 2.f * m * u * rw * 0.4f + u * u * rw, m * m * y + 2.f * m * u * (y - 1.f) + u * u * (y + 6.f));
    }
    return n;
}

static void bones_draw(uint16_t *fb, int stride, omni_rect_t clip, const xf_t *t, float lw, uint16_t rgb, float a,
                       bool glance)
{
    omni_pt_t p[2 * QN + 2];
    for (int j = 0; j < BONES_N; j++) {
        bool closed;
        const int n = bone_path(j, t, p, &closed);
        stroke_round(fb, stride, clip, p, n, closed, lw, rgb, a, glance);
    }
}

/* the hands' frame bounds: the kipper x -17..17, y -100..26 (the tail fin's tip mitres 3.4 px out), the skeleton
 * x -15..15, y -152..27 (its outline 3.1 px out); each with its shadow (3, 5) */
static omni_rect_t frame_box(float a, float x0, float y0, float x1, float y1, float m)
{
    const xf_t t = xf_of(a, (float)C, (float)C);
    const omni_pt_t q[4] = {xp(&t, x0, y0), xp(&t, x1, y0), xp(&t, x1, y1), xp(&t, x0, y1)};
    float a0 = q[0].x, b0 = q[0].y, a1 = a0, b1 = b0;
    for (int i = 1; i < 4; i++) {
        a0 = q[i].x < a0 ? q[i].x : a0, a1 = q[i].x > a1 ? q[i].x : a1;
        b0 = q[i].y < b0 ? q[i].y : b0, b1 = q[i].y > b1 ? q[i].y : b1;
    }
    return box_of(a0 - m, b0 - m, a1 + m + SHADOW_DX, b1 + m + SHADOW_DY);
}
static omni_rect_t hands_box(float ha, float ma)
{
    return join(join(frame_box(ha, -17.f, -HOUR_LEN, 17.f, 26.f, 5.f), frame_box(ma, -15.f, -MIN_LEN, 15.f, 27.f, 4.5f)),
                RD_CB_HUB);
}

static void hands_draw(cb_t *sc, uint16_t *fb, int stride, omni_rect_t clip, float ha, float ma)
{
    path_t *P = &sc->P;
    if (meets(frame_box(ha, -17.f, -HOUR_LEN, 17.f, 26.f, 5.f), clip)) {
        /* the shadows: the kipper filled black .5, the skeleton's strokes black .5 each */
        const xf_t ts = xf_of(ha, (float)C + SHADOW_DX, (float)C + SHADOW_DY);
        kipper_path(P, &ts);
        const paint_t black = solid(0x0000);
        fill_path(fb, stride, clip, P, &black, 0.5f);
    }
    const bool mm = meets(frame_box(ma, -15.f, -MIN_LEN, 15.f, 27.f, 4.5f), clip);
    if (mm) {
        const xf_t ts = xf_of(ma, (float)C + SHADOW_DX, (float)C + SHADOW_DY);
        bones_draw(fb, stride, clip, &ts, 3.2f, 0x0000, 0.5f, false);
    }
    if (meets(frame_box(ha, -17.f, -HOUR_LEN, 17.f, 26.f, 5.f), clip)) {
        const xf_t t = xf_of(ha, (float)C, (float)C);
        kipper_path(P, &t);
        paint_t q;
        memset(&q, 0, sizeof q);
        q.grad = true, q.t = t, q.g0 = -12.f, q.g1 = 12.f, q.mid = 0.45f;
        q.a[0] = 0x8E, q.a[1] = 0x2B, q.a[2] = 0x1E;   /* hull */
        q.b[0] = 0xC4, q.b[1] = 0x49, q.b[2] = 0x2F;   /* hullLit */
        fill_path(fb, stride, clip, P, &q, 1.f);
        stroke_path(fb, stride, clip, P, &sc->O, &sc->I, 1.5f, 0x0000, 1.f, false);
        const omni_pt_t s[2] = {xp(&t, 0.f, -HOUR_LEN * 0.14f), xp(&t, 0.f, -HOUR_LEN * 0.72f)};
        stroke_round(fb, stride, clip, s, 2, false, 3.f, RD_C_PAINT, 1.f, false);   /* the stripe */
        const omni_pt_t e0 = xp(&t, -4.f, -HOUR_LEN * 0.8f), e1 = xp(&t, 4.f, -HOUR_LEN * 0.8f);
        fk_disc(fb, stride, &clip, e0.x, e0.y, 2.2f, 0x0000, 255);
        fk_disc(fb, stride, &clip, e1.x, e1.y, 2.2f, 0x0000, 255);
    }
    if (mm) {
        const xf_t t = xf_of(ma, (float)C, (float)C);
        bones_draw(fb, stride, clip, &t, 6.2f, 0x0000, 1.f, false);
        bones_draw(fb, stride, clip, &t, 3.2f, RD_C_PAINT, 1.f, false);
        const omni_pt_t e = xp(&t, -2.5f, -MIN_LEN + 13.f);
        fk_disc(fb, stride, &clip, e.x, e.y, 2.f, 0x0000, 255);
    }
    if (meets(RD_CB_HUB, clip)) {   /* the hub: steel r 6, its 1.5 px black ring, the steelHi glint */
        fk_disc(fb, stride, &clip, (float)C, (float)C, 6.f, RD_C_STEEL, 255);
        fk_ring(fb, stride, &clip, (float)C, (float)C, 6.f, 1.5f, 0x0000, 255);
        fill_rect(fb, stride, clip, (float)C - 2.f, (float)C - 3.f, 1.5f, 1.5f, RD_C_STEEL_HI, 1.f);
    }
}

/* ---- the paw (paw()): its frame about (233, 233) turned ang + .11 swipe and moved out 272 - 116 reach */
typedef struct { bool on; float ang, reach, swipe; bool claws; float bat; } paw_t;   /* bat: the kibble's flight 0..1 */
static void paw_of(int pt, int m, paw_t *w)
{
    memset(w, 0, sizeof *w);
    if (pt < 0) return;
    const float t = (float)pt;
    w->on = true;
    w->ang = ((float)m + 0.5f) / 60.f * TAU;
    w->reach = pt < 550 ? ease_out3(t / 550.f) : pt < 950 ? 1.f : 1.f - ease_in2((t - 950.f) / 650.f);
    w->swipe = pt < 550 ? -1.f : pt < 820 ? -1.f + 2.f * ease_out3((t - 550.f) / 270.f) : 1.f;
    w->claws = pt > 450 && pt < 950;
    w->bat = pt > BAT_MS ? ease_out3((t - (float)BAT_MS) / (float)BAT_LEN) : -1.f;
}
static xf_t paw_xf(float ang, float reach, float swipe)
{
    float s, c;
    fk_sincos(ang + swipe * 0.11f, &s, &c);
    const float D = 272.f - reach * 116.f;   /* translate(0, -D) in the turned frame */
    return (xf_t){s, c, (float)C + D * s, (float)C - D * c};
}
/* its frame's bounds: x -25..25, y -26..132 (the claws' tips, the sock's edge, the forearm's end and its edge) */
static omni_rect_t paw_box_xf(const xf_t *t)
{
    const omni_pt_t q[4] = {xp(t, -25.f, -26.f), xp(t, 25.f, -26.f), xp(t, 25.f, 132.f), xp(t, -25.f, 132.f)};
    float a0 = q[0].x, b0 = q[0].y, a1 = a0, b1 = b0;
    for (int i = 1; i < 4; i++) {
        a0 = q[i].x < a0 ? q[i].x : a0, a1 = q[i].x > a1 ? q[i].x : a1;
        b0 = q[i].y < b0 ? q[i].y : b0, b1 = q[i].y > b1 ? q[i].y : b1;
    }
    return box_of(a0 - 1.f, b0 - 1.f, a1 + 1.f, b1 + 1.f);
}

void rd_cb_paw_rect(unsigned m, omni_rect_t *out)
{
    /* every reach (0..1) and swipe (-1..1) the run can take, on a grid finer than a pixel's sweep at the frame's far
     * corner (132 px out x .22 / 22 rad: 1.3 px, inside the box's own pixel of margin) */
    const float ang = ((float)(m % 60u) + 0.5f) / 60.f * TAU;
    omni_rect_t u = {0, 0, 0, 0};
    for (int i = 0; i <= 22; i++)
        for (int j = 0; j <= 58; j++) {
            const xf_t t = paw_xf(ang, (float)j / 58.f, -1.f + 2.f * (float)i / 22.f);
            const omni_rect_t b = paw_box_xf(&t);
            u = i == 0 && j == 0 ? b : join(u, b);
        }
    u.x0 = (int16_t)(u.x0 - 1 < 0 ? 0 : u.x0 - 1), u.y0 = (int16_t)(u.y0 - 1 < 0 ? 0 : u.y0 - 1);
    u.x1 = (int16_t)(u.x1 + 1 > W ? W : u.x1 + 1), u.y1 = (int16_t)(u.y1 + 1 > W ? W : u.y1 + 1);
    *out = u;
}

static void paw_draw(cb_t *sc, uint16_t *fb, int stride, omni_rect_t clip, const paw_t *w)
{
    const xf_t t = paw_xf(w->ang, w->reach, w->swipe);
    if (!meets(paw_box_xf(&t), clip)) return;
    path_t *P = &sc->P;
    /* the forearm: its fur (#4b443e, #6e655c at .5, #4b443e across x -20..20) and its 1.6 px #a39a8f edge */
    pbegin(P);
    pmove(P, &t, -15.f, 130.f), pline(P, &t, -17.f, 14.f), pline(P, &t, 17.f, 14.f), pline(P, &t, 15.f, 130.f);
    pclose(P);
    paint_t fur;
    memset(&fur, 0, sizeof fur);
    fur.grad = true, fur.t = t, fur.g0 = -20.f, fur.g1 = 20.f, fur.mid = 0.5f;
    fur.a[0] = 0x4B, fur.a[1] = 0x44, fur.a[2] = 0x3E;
    fur.b[0] = 0x6E, fur.b[1] = 0x65, fur.b[2] = 0x5C;
    fill_path(fb, stride, clip, P, &fur, 1.f);
    stroke_path(fb, stride, clip, P, &sc->O, &sc->I, 1.6f, R86(0xA3, 0x9A, 0x8F), 1.f, false);
    /* the tabby stripes: #2a2521, 3.5 px, round caps */
    for (int i = 0; i < 5; i++) {
        omni_pt_t s[QN + 1];
        const float y = 40.f + 18.f * (float)i;
        for (int k = 0; k <= QN; k++) {
            const float u = (float)k / (float)QN, m = 1.f - u;
            s[k] = xp(&t, m * m * -14.f + u * u * 14.f, m * m * y + 2.f * m * u * (y - 7.f) + u * u * y);
        }
        stroke_round(fb, stride, clip, s, QN + 1, false, 3.5f, R86(0x2A, 0x25, 0x21), 1.f, false);
    }
    static const float TOES[4][3] = {{-15.f, -2.f, 7.5f}, {-5.5f, -10.f, 8.f}, {5.5f, -10.f, 8.f}, {15.f, -2.f, 7.5f}};
    if (w->claws)   /* M(x, y - r + 2) Q(x - 1, y - r - 5, x + 2.5, y - r - 6): #f4efe4, 1.6 px, round caps */
        for (int k = 0; k < 4; k++) {
            omni_pt_t s[QN + 1];
            const float x = TOES[k][0], y = TOES[k][1], r = TOES[k][2];
            for (int q = 0; q <= QN; q++) {
                const float u = (float)q / (float)QN, m = 1.f - u;
                s[q] = xp(&t, m * m * x + 2.f * m * u * (x - 1.f) + u * u * (x + 2.5f),
                          m * m * (y - r + 2.f) + 2.f * m * u * (y - r - 5.f) + u * u * (y - r - 6.f));
            }
            stroke_round(fb, stride, clip, s, QN + 1, false, 1.6f, R86(0xF4, 0xEF, 0xE4), 1.f, false);
        }
    /* the sock: #cdc4b2, its 1.6 px #6d665d edges, the ellipse and each toe filled and stroked in turn, then filled
     * again inside (the ellipse 21.5 x 18.5, the toes r - 1.5) */
    const paint_t sock = solid(R86(0xCD, 0xC4, 0xB2));
    const uint16_t edge = R86(0x6D, 0x66, 0x5D);
    pbegin(P), pellipse(P, &t, 0.f, 10.f, 23.f, 20.f, 48);
    fill_path(fb, stride, clip, P, &sock, 1.f);
    stroke_path(fb, stride, clip, P, &sc->O, &sc->I, 1.6f, edge, 1.f, false);
    for (int k = 0; k < 4; k++) {
        pbegin(P), pellipse(P, &t, TOES[k][0], TOES[k][1], TOES[k][2], TOES[k][2], 24);
        fill_path(fb, stride, clip, P, &sock, 1.f);
        stroke_path(fb, stride, clip, P, &sc->O, &sc->I, 1.6f, edge, 1.f, false);
    }
    pbegin(P), pellipse(P, &t, 0.f, 10.f, 21.5f, 18.5f, 48);
    fill_path(fb, stride, clip, P, &sock, 1.f);
    for (int k = 0; k < 4; k++) {
        pbegin(P), pellipse(P, &t, TOES[k][0], TOES[k][1], TOES[k][2] - 1.5f, TOES[k][2] - 1.5f, 24);
        fill_path(fb, stride, clip, P, &sock, 1.f);
    }
    /* the toe creases: rgba(80, 70, 60, .5), 1 px, round caps (the stripes' lineCap stays set) */
    static const float CR[3][2] = {{-10.f, -2.f}, {0.f, -4.f}, {10.f, -2.f}};
    for (int k = 0; k < 3; k++) {
        const omni_pt_t s[2] = {xp(&t, CR[k][0], CR[k][1]), xp(&t, CR[k][0] * 0.8f, CR[k][1] + 7.f)};
        stroke_round(fb, stride, clip, s, 2, false, 1.f, R86(80, 70, 60), 0.5f, false);
    }
}

/* ---- the schedule and the count */
unsigned rd_cb_kibbles_left(const fk_clock_t *c) { return c->set ? 60u - (unsigned)(c->m % 60u) : 0u; }

int rd_cb_paw_ms(const fk_face_in_t *in)
{
    if (!in->clk.set || in->reduced || in->glance || in->clk.m >= 59) return -1;
    if (in->pulsing && in->pulse_ms < PAW_MS) return in->pulse_ms;
    if (in->fresh) {
        if (in->fresh_ms < PAW_FIRST) return -1;
        const int k = (int)((in->fresh_ms - PAW_FIRST) % PAW_EVERY);
        return k < PAW_MS ? k : -1;
    }
    const long ms = (((long)in->clk.h * 60 + in->clk.m) * 60 + in->clk.s) * 1000L + in->clk.ms;
    const int k = (int)(ms % PAW_EVERY);
    return k < PAW_MS ? k : -1;
}

void rd_cb_hand_angles(const fk_face_in_t *in, float *hour, float *minute)
{
    const float m = (float)in->clk.m, h = (float)(in->clk.h % 12) + m / 60.f;
    *hour = h / 12.f * TAU;
    *minute = m / 60.f * TAU;
}

unsigned rd_cb_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_CB_SHEEN;
    out[1] = RD_CB_RING;
    return 2;
}

/* ---- bind */
static bool bind(void *ctx, fk_asset_fn get)
{
    cb_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_CB_BOWL, &n);
    bool ok = b && fk_img_from_bin(&k->bowl, b, n) && k->bowl.rgb && !k->bowl.a && k->bowl.w == BOX &&
              k->bowl.h == BOX;
    b = get(ASSET_RD_F_S30, &n);
    ok = ok && b && fk_img_from_bin(&k->s30_img, b, n) && k->s30_img.a;
    b = get(ASSET_RD_F_S64, &n);
    ok = ok && b && fk_img_from_bin(&k->s64_img, b, n) && k->s64_img.a;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    if (!ok) return false;
    k->s30 = (fk_atlas_t){&k->s30_img, RD_CELLS_RD_F_S30, RD_GLYPHS_RD_F_S30, ASSET_RD_F_S30_LINE_H};
    k->s64 = (fk_atlas_t){&k->s64_img, RD_CELLS_RD_F_S64, RD_GLYPHS_RD_F_S64, ASSET_RD_F_S64_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->sheen = rd_glow_lut(SHEEN_R);
    if (!k->sheen) return false;
    /* KIB: rng(99), seven draws a kibble: dr, da, rot, s, its colour, fx, fy */
    uint32_t seed = 99u;
    for (int i = 0; i < KIB_N; i++) {
        kib_t *q = &k->kib[i];
        q->dr = (float)((mb_next(&seed) - 0.5) * 10.0);
        q->da = (float)((mb_next(&seed) - 0.5) * 0.04);
        q->rot = (float)(mb_next(&seed) * 3.0);
        q->s = (float)(0.85 + mb_next(&seed) * 0.35);
        const int c = (int)(mb_next(&seed) * 3.0);
        q->c = (uint8_t)(c > 2 ? 2 : c);
        q->fx = (float)((mb_next(&seed) - 0.5) * 70.0);
        q->fy = (float)((mb_next(&seed) - 0.5) * 50.0);
        float s, co;
        fk_sincos((float)i / 60.f * TAU - TAU / 4.f + q->da, &s, &co);
        k->kx[i] = (float)C + co * (KIB_R + q->dr);
        k->ky[i] = (float)C + s * (KIB_R + q->dr);
        k->kbox[i] = kib_box_at(k->kx[i], k->ky[i], q->s, 0.f);
    }
    /* staleMark(233, 167)'s group (as curry.c's): w = measureText('STALE') with its trailing spacing + 14 */
    const float wt = fk_atlas_width_tracked(&k->ch13, "STALE", TRACK13) + TRACK13 + 14.f;
    k->stale_cx = (int16_t)((float)C - wt / 2.f + 34.f + 0.5f);
    for (int i = 0; i < 12; i++) {
        float s, c;
        fk_sincos((float)(i + 1) / 12.f * TAU - TAU / 4.f, &s, &c);
        k->num_cx[i] = (float)C + c * NUM_R;
        k->num_base[i] = (int16_t)ifl((float)C + s * NUM_R + NUM_DY + 0.5f);
    }
    return true;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced;
    int m;             /* the kibbles eaten */
    float ha, ma;      /* the hands, the wake's swing included */
    int wk;            /* the wake's ms, -1 settled */
    int pt;            /* the paw's ms, -1 none */
    float hop;         /* the next kibble's hop, px */
    uint8_t sheen;     /* the sheen's added level */
} cb_state_t;

static void state_of(const fk_face_in_t *in, cb_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    s->wk = -1, s->pt = -1;
    if (!s->set) return;
    s->m = in->clk.m % 60;
    rd_cb_hand_angles(in, &s->ha, &s->ma);
    const bool live = !in->reduced && !in->glance;
    if (live && in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_MS) {
        s->wk = (int)in->wake_ms;
        if (s->wk < HANDS_MS) {
            const float e = ease_out_back((float)s->wk / (float)HANDS_MS);
            s->ha *= e, s->ma *= e;
        }
    }
    s->pt = s->wk >= 0 ? -1 : rd_cb_paw_ms(in);
    if (live && in->beep > 0) s->hop = HOP_PX * clamp01(in->beep);
    if (live && in->thrum > 0) s->sheen = a8_of(SHEEN_A * clamp01(in->thrum));
}

/* ---- drawing */
static void base_draw(const cb_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++)
        memset(px_at(fb, stride, clip, clip.x0, y), 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const omni_rect_t t = cut(clip, RD_CB_BOWL);
    for (int y = t.y0; y < t.y1 && t.x0 < t.x1; y++)
        memcpy(px_at(fb, stride, clip, t.x0, y), k->bowl.rgb + (size_t)(y - BX) * BOX + (t.x0 - BX),
               (size_t)(t.x1 - t.x0) * 2);
}

/* kibble i's place and look this frame: the wake's rain, the paw's bat, the chime's hop */
static void kibbles_draw(const cb_t *k, const cb_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    paw_t w;
    paw_of(s->pt, s->m, &w);
    for (int i = 0; i < KIB_N; i++) {
        const kib_t *q = &k->kib[i];
        const float x = k->kx[i], y = k->ky[i];
        if (i < s->m) {
            if (meets(k->kbox[i], clip)) fill_rect(fb, stride, clip, x, y, 1.6f, 1.6f, CRUMB, 0.5f);
            continue;
        }
        if (w.on && i == s->m && w.bat >= 0) {
            const float p = w.bat;
            kib_draw(fb, stride, clip, x + ((float)C + q->fx - x) * p, y + ((float)C - 10.f + q->fy - y) * p,
                     q->rot + p * 9.f, q->s, KIB_RGB[q->c], 1.f);
            continue;
        }
        if (s->wk >= 0) {
            const int left = 60 - s->m > 1 ? 60 - s->m : 1;
            const float d = clamp01(((float)s->wk - 80.f - (float)(i - s->m) / (float)left * 560.f) / 260.f);
            const float sc = 1.9f + (1.f - 1.9f) * ease_out_back(d);
            kib_draw(fb, stride, clip, x, y - (1.f - d) * 26.f, q->rot, q->s * sc, KIB_RGB[q->c], d);
            continue;
        }
        if (!meets(i == s->m ? hop_box(k, i) : k->kbox[i], clip)) continue;
        kib_draw(fb, stride, clip, x, i == s->m ? y - s->hop : y, q->rot, q->s, KIB_RGB[q->c], 1.f);
    }
}

static void scene(const cb_t *k, const cb_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    base_draw(k, fb, stride, clip);
    if (!s->set) {   /* the empty bowl: --:-- and BOWL EMPTY on the floor, in paintDim */
        const int w = fk_atlas_width(&k->s64, "--:--", 0);
        fk_atlas_text_rgb(fb, stride, clip, &k->s64, "--:--", C - w / 2, Y_UNSET_TIME, 0, RD_C_PAINT_DIM, 255, 0);
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "BOWL EMPTY", (float)C, Y_UNSET_LINE, TRACK13, true,
                              RD_C_PAINT_DIM, 255);
        return;
    }
    if (s->sheen && meets(RD_CB_SHEEN, clip))
        fk_glow_add_lut(fb, stride, &clip, SHEEN_X, SHEEN_Y, SHEEN_R, k->sheen, RD_C_STEEL_HI, s->sheen);
    kibbles_draw(k, s, fb, stride, clip);
    if (s->stale && meets(RD_CB_STALE, clip)) rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx, STALE_CY, false);
    cb_t *sc = (cb_t *)(uintptr_t)k;   /* the scratch: draw()'s one write outside its frame (see the header) */
    hands_draw(sc, fb, stride, clip, s->ha, s->ma);
    paw_t w;
    paw_of(s->pt, s->m, &w);
    if (w.on) paw_draw(sc, fb, stride, clip, &w);
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    cb_state_t s;
    state_of(in, &s);
    scene(ctx, &s, fb, stride, clip);
}

/* sleepFrame(): the lip's hairline, 12, 3, 6 and 9, the fish as outlines (or the unset --:-- as outlines), the stale
 * marker; through the orbit */
/* strokeTime('--:--', 64, ..., cx, cy + 22, 1.4): curry.c's boxes (the same call there) */
static const float UNSET_BOX[6][4] = {
    {-61.15f, -43.f, -0.5f, 9.f}, {-34.f, -15.85f, -0.5f, 9.f}, {15.85f, 34.f, -0.5f, 9.f},
    {43.f, 61.15f, -0.5f, 9.f},   {-6.35f, 6.55f, -10.f, 0.3f}, {-6.35f, 6.55f, 11.7f, 22.f},
};
static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const cb_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(px_at(fb, stride, clip, clip.x0, y), 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const float ox = (float)in->odx, oy = (float)in->ody;
    const bool set = in->clk.set, stale = set && in->clk.stale;
    fk_ring_max(fb, stride, &clip, (float)C + ox, (float)C + oy, LIP_GR, 1.5f, RD_G_HULL, 255);
    static const int Q[4] = {11, 2, 5, 8};   /* 12, 3, 6 and 9 */
    for (int q = 0; q < 4; q++) {
        const int i = Q[q];
        const char *t = i == 11 ? "12" : i == 2 ? "3" : i == 5 ? "6" : "9";
        fk_atlas_text_tracked(fb, stride, clip, &k->s30, t, k->num_cx[i] + ox, k->num_base[i] - GL_NUM_UP + in->ody,
                              0.f, true, RD_G_PAINT, 255);
    }
    if (!set) {
        for (int i = 0; i < 6; i++) {
            const float *b = UNSET_BOX[i];
            const float x0 = (float)C + ox + b[0], x1 = (float)C + ox + b[1];
            const float y0 = (float)C + oy + b[2], y1 = (float)C + oy + b[3];
            const omni_pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
            fk_poly_stroke_max(fb, stride, &clip, p, 4, 1.4f, RD_G_PAINT, 255);
        }
        return;
    }
    float ha, ma;
    rd_cb_hand_angles(in, &ha, &ma);
    cb_t *sc = (cb_t *)(uintptr_t)k;   /* the scratch (see the header) */
    const xf_t th = xf_of(ha, (float)C + ox, (float)C + oy), tm = xf_of(ma, (float)C + ox, (float)C + oy);
    kipper_path(&sc->P, &th);
    stroke_path(fb, stride, clip, &sc->P, &sc->O, &sc->I, 1.4f, RD_G_HULL, 1.f, true);
    bones_draw(fb, stride, clip, &tm, 1.3f, RD_G_PAINT, 1.f, true);
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx + in->odx, STALE_CY + in->ody, true);
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
/* every pixel the paw and the batted kibble of state s can touch */
static void paw_rects(const cb_t *k, const cb_state_t *s, rects_t *L)
{
    paw_t w;
    paw_of(s->pt, s->m, &w);
    if (!w.on) return;
    const xf_t t = paw_xf(w.ang, w.reach, w.swipe);
    add(L, paw_box_xf(&t));
    if (w.bat >= 0) {
        const kib_t *q = &k->kib[s->m];
        const float x = k->kx[s->m], y = k->ky[s->m];
        add(L, kib_box_at(x + ((float)C + q->fx - x) * w.bat, y + ((float)C - 10.f + q->fy - y) * w.bat, q->s, 0.f));
    }
}

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    const cb_t *k = ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody &&
                          (!a->clk.set || (a->clk.h % 12 == b->clk.h % 12 && a->clk.m == b->clk.m));
        return same ? 0 : whole(out);
    }
    cb_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || sa.reduced != sb.reduced) return whole(out);
    if (!sa.set) return 0;   /* the empty bowl: nothing moves */
    rects_t L;
    memset(&L, 0, sizeof L);
    /* the rain and the swing: the bowl's box holds the hands, the ring, the sheen and the marker, so only the paw (its
     * run reaches past the bowl) is added to it. One wake time over another minute or hour moves the rain too (its d
     * reads i - m) and the swing's targets (wave 2 Task 10: the Task 9 review's m3) */
    const bool wake = (sa.wk >= 0 || sb.wk >= 0) &&
                      (sa.wk != sb.wk || sa.m != sb.m || sa.ha != sb.ha || sa.ma != sb.ma);
    if (wake) add(&L, RD_CB_BOWL);
    if (!wake && (sa.ha != sb.ha || sa.ma != sb.ma)) add(&L, join(hands_box(sa.ha, sa.ma), hands_box(sb.ha, sb.ma)));
    if (!wake && sa.m != sb.m) {   /* a kibble eaten (or back: a step back over the minute), the hour's refill */
        const int lo = sa.m < sb.m ? sa.m : sb.m, hi = sa.m < sb.m ? sb.m : sa.m;
        if (hi - lo > 3) {
            add(&L, RD_CB_RING);
        } else {
            for (int i = lo; i < hi; i++) add(&L, k->kbox[i]);   /* each between a kibble and a crumb */
        }
    }
    /* the paw, the batted kibble in flight, and its place in the ring (left or back); the paw turns with the minute, so
     * a minute's change moves a running paw at the same ms */
    if (sa.pt != sb.pt || (sa.m != sb.m && (sa.pt >= 0 || sb.pt >= 0))) {
        paw_rects(k, &sa, &L);
        paw_rects(k, &sb, &L);
        add(&L, hop_box(k, sa.m));
        if (sb.m != sa.m) add(&L, hop_box(k, sb.m));
    }
    if (!wake && (sa.hop != sb.hop || (sa.m != sb.m && (sa.hop > 0 || sb.hop > 0)))) {   /* the next kibble's hop */
        add(&L, hop_box(k, sa.m));
        if (sb.m != sa.m) add(&L, hop_box(k, sb.m));
    }
    if (!wake && sa.sheen != sb.sheen) add(&L, RD_CB_SHEEN);
    if (!wake && sa.stale != sb.stale) add(&L, RD_CB_STALE);
    if (L.n > 8) {
        out[0] = L.u;
        return 1;
    }
    for (unsigned i = 0; i < L.n; i++) out[i] = L.r[i];
    return (uint8_t)L.n;
}

static bool in_rect(const omni_rect_t *r, int16_t x, int16_t y)
{
    return x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1;
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return in_rect(&RD_CB_HIT, x, y) ? FK_TAP_PULSE : FK_TAP_NONE;
}

/* a tap on the bowl summons the paw (the pulse; the variant left: the face has none); under reduced motion there is
 * no paw, and a tap does nothing (FK_TAP_NONE). Unset the pulse plays and shows nothing (the bowl is empty: no paw
 * is drawn). A NULL in (only from outside the runtime, fk/face.h) counts as a set clock in full motion */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    (void)ctx;
    (void)variant;
    if (!in_rect(&RD_CB_HIT, x, y)) return FK_TAP_NONE;
    if (in != NULL && in->reduced) return FK_TAP_NONE;
    return FK_TAP_PULSE;
}

const fk_face_ops_t RD_FACE_BOWL = {
    .key = "bowl",
    .ctx_bytes = sizeof(cb_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = WAKE_MS,
    .pulse_len_ms = PAW_MS,     /* the paw's run */
    .pulse_guard_ms = PAW_MS,   /* a tap during a run leaves it (the mockup's pawT guard) */
    .tap_var = tap_var,
    .stills_extra = 1,          /* still 11: the paw at full reach, a tap 900 ms ago (rd_still_pulse_ms) */
};
