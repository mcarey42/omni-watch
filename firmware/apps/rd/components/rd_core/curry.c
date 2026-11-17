/* Curry Clock (`curry`, rd/faces.h; rd wave 1 Task 15, spec section 10.10; faces-quarters.js:350-503: buildBg(),
 * buildLid(), fork(), spoon(), hands(), sticker(), steam(), frame(), pointer() and sleepFrame()).
 *
 * The frame, in the mockup's order: black; the tray layer (rd_cu_tray, buildBg()'s canvas cut to the tray's box and
 * disc: RGB565 336 x 336 at (65, 65), black outside r 170); the 48 minute ticks (paintDim, 1.5 px, r 222-228) and the
 * twelve numerals (drawStencil(String(i), 30, ...) at r 201, their baseline 11 px below: the stencil 30 atlas, paint,
 * the eight between the quarters dimmed to .75 by colour), which buildBg() draws beside the tray; then, with the clock
 * set, the heat sticker (sticker(): a sprite composed at bind, below) and its chillies, the hands (the shadows of both,
 * then the fork and the spoon in steel with their black outlines, then the peppercorn), the steam (added) and the stale
 * marker; then the lid while it shows. The whole frame shifts across by the shudder (frame(): translate(shake, 0)).
 *
 * The heat sticker (PR23's biro aside, the mockup's own colours): a 148 x 64 card r 3 in #a39983 with its rgba(0, 0, 0,
 * .4) edge, turned .2 rad about (138, 330). Its text is drawn upright at bind (Chakra 13: HEAT tracked 1.8 in
 * hazardDark, MILD / MED / HOT untracked, INADVISABLE tracked in #a3201a) into a scratch card, which the sprite samples
 * (bilinear) through the turn; the card's edge, the three boxes (strokeRect 7 x 7, 1 px) and the red tick (2.2 px,
 * round caps) are worked out per pixel from 4 x 4 sample points in the card's frame, all in squared distances (no
 * square root). The sprite holds the composite and its alpha (RD_CU_STICKER). The chillies (heat of them, each an
 * ellipse 6.5 x 2.8 turned -.5 in the sticker's frame and its 3 x 2 stalk) are polygons drawn over it each frame.
 *
 * The hands (rd_hand_fork, rd_hand_spoon): the mockup's paths in the hand's frame (the spoon's two quadratic curves in
 * 8 steps each and its bowl's half ellipse in 24), turned about the centre through fk_sincos, filled by exact area
 * coverage (a polygon of any size, by the accumulated signed area of its edges per pixel row), the steel's gradient per
 * pixel; the outline is the band between the path offset out and in by .75 px (fk_offset_poly, mitred as canvas
 * strokes), the shadow the path moved by (4, 6) in black .55.
 *
 * The steam (steam(); wave 2 Task 12: the classic pre-rendered loop): four wisps of 30 puffs, puff k of wisp w at life
 * (T (28 + 4 w) / 60 000 + k / 30 + .23 w) mod 1 of its rise, T the clock's ms of the day (an integer phase: no drift,
 * no free timer), at y = 263 - 220 life, x = 233 + ox_w + sin(7 life + T / 800 + 1.7 w) (6 + 22 life), half-size 7 + 20
 * life, alpha amt x .085 sin(pi life), the blob added ('lighter') in cream (255, 248, 236), or (255, 150, 120) at three
 * chillies. Every wisp is one image G(phi, theta) moved across by ox_w (rd/faces.h, W2-D7), so the face draws it from
 * rd_cu_wisp: 24 theta steps over half a turn (mirrored for the other half) by two phi steps of the puff lattice, each
 * a half-resolution frame of G's light (exported from steam()'s recipe, each row kept to its extent). Per wisp and
 * frame, wisp_of() picks its four frame rows (the two phi frames about phi', each moved along the rise as the lattice
 * moves, each at two theta steps) and their weights; per screen row wisp_line() blends them, integer only, over each
 * stored row's extent, and doubles it across by (1, 3) / 4; the four wisps' light, summed per row, is added once, times
 * amt and the colour, rounded as wave 1's 8.4 row sums were. Clipped to RD_CU_STEAM, its whole reach.
 * The lid: rd_cu_lid indexes RD_CU_LID_RAMP (1-32: the rim's colour at k / 32 over the tray, 33-255 the card and its
 * stain). Unset, it is drawn shut from the A8 over the tray (the tray's own pixels under its edge), with --:-- and READY
 * SOON in biro on it. The wake's leaf (PR24) is the same card composed at bind over the tray layer, its printed label
 * (No 42 in Chakra 18 and EXTRA HOT. SORRY. in Chakra 13, biro) drawn on it: 336 x 336 RGB565 in the context. Its swing
 * is the mockup's flat squash (scale(1, cos(.55 pi swing)) about the top edge, its rows sampled nearest): fk_rows_blit,
 * one call a box row, cut to the card's disc on that row (the chord the A8 holds), so the box's corners keep the face;
 * then the black .3 swing over the lid's rows of the box. Past the top edge (the mockup's last 9 %, the card's back
 * above the box) nothing is drawn: the lid stays inside RD_CU_LIDBOX (the brief's box). The printed label rides the
 * leaf to the end of the swing (the mockup drops it at 30 %: a second leaf would cost 225 792 B more).
 * RD_CU_LID_HINGE names the hinge (the box's top edge, box-local, n 336); its focal length is unused: the mockup's lid
 * is flat, and a perspective swing would widen the leaf past its box (fk_rows_map's hinge, and its sinf).
 *
 * Pure: no esp_*, freertos or LVGL; no libm (lesson 6: every angle through fk_sincos, floor by casts, distances
 * squared); no allocation; no settings, store or NVS symbol (the chillies are the RAM variant's). draw() writes its
 * frame and the steam's scratch in the context (the wisps' taps, a stored row's sums and the row's light: off the LVGL
 * task's stack, the Task 15 review and R189), which no frame reads before it writes, so this face stays a canvas face (no .direct, no reveal: fk_view
 * draws it from the LVGL task alone; test_rd_cu_face asserts both). */
#include <stdint.h>
#include <string.h>
#include "rd/faces.h"
#include "rd/assets_meta.h"
#include "fk/compose.h"
#include "fk/rows.h"
#include "fk/shapes.h"
#include "omni/blend565.h"

#define W 466
#define C 233
#define TR 168         /* the tray's (and the lid's) radius */
#define BOX 336        /* the lid's box: TR x 2 */
#define BX 65          /* its top-left: C - TR */
#define PI 3.14159265359f
#define TAU 6.28318530718f
/* the R86 rounding rd_pack.py packs with (tools/facepack.py rgb565()) */
#define R86(r, g, b) \
    (uint16_t)((((r) * 31 + 127) / 255) << 11 | (((g) * 63 + 127) / 255) << 5 | (((b) * 31 + 127) / 255))
#define TRACK13 1.8f   /* lab(): (13 x .14).toFixed(1) px */
#define TRACK18 2.5f   /* (18 x .14).toFixed(1) */
#define NUM_R 201.f    /* the numerals' ring, their baseline 11 px below its point */
#define NUM_DY 11.f
#define GL_NUM_UP 3    /* sleepFrame()'s fillText(n, x, y + 1) on the 'middle' line: 3 px above the awake baseline */
#define TICK_R0 222.f
#define TICK_R1 228.f
#define FORK_LEN 108.f
#define SPOON_LEN 152.f
#define SHADOW_DX 4.f
#define SHADOW_DY 6.f
#define STALE_CY 159   /* staleMark(233, 155): the ring at 155 (cy - 4.5 of the shared marker's) and STALE on 159 */
#define Y_BIRO_TIME 249   /* drawTime('--:--', 64, biro, 0, 16) on the lid about its centre: the baseline 233 + 16 */
#define Y_BIRO_LINE 280   /* lab('READY SOON', 0, 44, 13): 277 middle + 3.4 */
#define Y_LABEL_N 161     /* lab('No 42', 0, -12, 18): 221 middle + 4.7, box-local 160.7 */
#define Y_LABEL_X 185     /* lab('EXTRA HOT. SORRY.', 0, 14, 13): 247 middle + 3.4, box-local 185.4 */
#define BIRO_TIME_A 217   /* drawTime(..., .85): the ink over the card */
#define WAKE_MS 1000
#define PUFF_MS 1500
#define SHAKE_MS 400      /* the pulse; the mockup's own decay runs to 420 */
#define SHAKE_DECAY 420.f
#define STEAM_FROZEN 4000u   /* reduced motion: the mockup's steam(ctx, 4000, .7) */
#define WISP_TURN (2 * RD_CU_WISP_N * 256)   /* a whole turn of theta, Q8 steps */
#define WISP_DTH 228   /* 7 / 60 rad (the lattice's theta per sixtieth of phi) in theta steps, Q8: 7 x 24 / (60 pi) x 256 */

const omni_hinge_t RD_CU_LID_HINGE = {OMNI_HINGE_HORIZONTAL, 0.0f, 560.0f, BOX, 0.0f, 0.0f};
const omni_rect_t RD_CU_LIDBOX = {BX, BX, BX + BOX, BX + BOX};
/* the wisps' reach: x 233 + ox (-50 .. 60) +- (6 + 22 + 27) at the top of the rise, y 263 - 220 - 27 .. 263 + 7 */
#define STEAM_X0 128
#define STEAM_X1 349
const omni_rect_t RD_CU_STEAM = {STEAM_X0, 16, STEAM_X1, 271};
/* the sprite's box: the card's turned corners (59.1 .. 216.9, 284.0 .. 376.0), its edge's half pixel and one more */
#define STK_X0 57
#define STK_Y0 282
#define STK_W 162
#define STK_H 96
const omni_rect_t RD_CU_STICKER = {STK_X0, STK_Y0, STK_X0 + STK_W, STK_Y0 + STK_H};
const omni_rect_t RD_CU_STALE = {190, 146, 276, 166};
const omni_rect_t RD_CU_HUB = {221, 221, 245, 245};
const omni_rect_t RD_CU_HIT = {BX, BX, BX + BOX, BX + BOX};
#ifdef RD_TEST_HOOKS
void (*rd_cu_steam_ref)(uint32_t T, float amt, bool red, uint16_t *fb, int stride, omni_rect_t clip);
#endif

const rd_ramp_stop_t RD_CU_LID_RAMP[RD_CU_LID_RAMP_STOPS] = {
    {0, 0x00, 0x00, 0x00},   {32, 0xA8, 0xA3, 0x96},  {33, 0x7F, 0x79, 0x6C},
    {160, 0xC9, 0xC3, 0xB4}, {161, 0x82, 0x6C, 0x56}, {255, 0xBA, 0xA4, 0x8C},
};
#define LID_EDGE 32   /* indices 1..32: the rim's colour (ramp[32]) at k / 32 over the tray */

/* the sticker's frame: translate(138, 330) rotate(.2); its scratch card at bind, upright, local (u, v) = (i - 82,
 * j - 40.6): every text baseline (-15.6, 1.4, 18.4: the 'middle' rows -19, -2 and 15 plus Chakra's .263 em) lands on
 * a whole row */
#define STK_CX 138.f
#define STK_CY 330.f
#define STK_ROT 0.2f
#define TMP_X0 (-82)
#define TMP_Y0 (-40.6f)
#define TMP_W 164
#define TMP_H 82

/* a wisp's tap: one stored frame (its first row), mirrored or not, its weight (Q8) and its shift along the rise (Q8
 * px); a wisp blends at most four */
typedef struct { uint16_t base; uint8_t mir; uint16_t wt; int32_t sq; } wtap_t;
typedef struct { wtap_t t[4]; uint8_t n; } wisp_t;

typedef struct {
    fk_img_t tray, lid, s30_img, s64_img, ch13_img, ch18_img;
    fk_atlas_t s30, s64, ch13, ch18;
    const uint8_t *g9;          /* the peppercorn's glint (RD_GLOW_LUT_9) */
    uint8_t ramp[256][3];       /* RD_CU_LID_RAMP's colours (the export's rampOf() rule, 8 bits a channel) */
    int16_t chord[BOX][2];      /* each lid row's columns [a, b) where the card has ink (rd_cu_lid > 0) */
    int16_t stale_cx;
    float num_cx[12];           /* the numerals' centres and baselines (i + 1 o'clock) */
    int16_t num_base[12];
    omni_rect_t num_box[12];
    uint16_t stk_rgb[STK_H * STK_W];   /* the sticker's sprite: its composite and its alpha */
    uint8_t stk_a[STK_H * STK_W];
    uint16_t leaf[BOX * BOX];   /* the wake's card (PR24); bind's scratch before it */
    const uint8_t *wisp_ext, *wisp_body;   /* rd_cu_wisp (ARW1): each stored row's extent [x0, x1), the rows' bytes */
    uint32_t wisp_off[RD_CU_WISP_ROWS];    /* each stored row's first byte in wisp_body */
    /* steam_draw()'s scratch (see the header): the wisps' taps, one wisp row's stored sums, the row's light (all zero
     * between rows) and the frame's samples */
    wisp_t wisp[4];
    uint32_t H[RD_CU_WISP_HW + 2];
    uint32_t sacc[STEAM_X1 - STEAM_X0];
    uint32_t samples;
} cu_t;

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
static uint16_t dim565(uint16_t c, uint8_t k)
{
    return omni_pack565(omni_ex_r(c) * k / 255, omni_ex_g(c) * k / 255, omni_ex_b(c) * k / 255);
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

/* ---- polygons by exact area coverage */
#define PMAX 64       /* points a path may have */
#define CW 128        /* the widest run of a row covered at once */
static void put_acc(float *acc, int w, int i, float v)
{
    if (i < 0) i = 0;   /* left of the run: it adds to every cell of it */
    if (i <= w + 1) acc[i] += v;
}
/* one edge's share of one pixel row (from x to xnext over its height d, signed by its direction), the accumulated area
 * of the cells it crosses (the signed-area rasteriser: a prefix sum of acc gives each pixel's coverage) */
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

/* the coverage (0..1) of the closed path p over pixel row y, columns [xa, xa + w) (w <= CW), into cov */
static void cover_row(const omni_pt_t *p, int n, int y, int xa, int w, float *cov)
{
    float acc[CW + 2];
    memset(acc, 0, sizeof acc);
    const float y0 = (float)y, y1 = y0 + 1.f, ox = (float)xa;
    for (int i = 0; i < n; i++) {
        omni_pt_t a = p[i], b = p[i + 1 < n ? i + 1 : 0];
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
    float s = 0;
    for (int x = 0; x < w; x++) {
        s += acc[x];
        const float v = s < 0 ? -s : s;
        cov[x] = v > 1.f ? 1.f : v;
    }
}

static void bbox_of(const omni_pt_t *p, int n, float m, omni_rect_t *out)
{
    float x0 = p[0].x, y0 = p[0].y, x1 = x0, y1 = y0;
    for (int i = 1; i < n; i++) {
        x0 = p[i].x < x0 ? p[i].x : x0, x1 = p[i].x > x1 ? p[i].x : x1;
        y0 = p[i].y < y0 ? p[i].y : y0, y1 = p[i].y > y1 ? p[i].y : y1;
    }
    const int a = ifl(x0 - m), b = ifl(y0 - m), c = icl(x1 + m) + 1, d = icl(y1 + m) + 1;
    out->x0 = (int16_t)(a < 0 ? 0 : a > W ? W : a), out->y0 = (int16_t)(b < 0 ? 0 : b > W ? W : b);
    out->x1 = (int16_t)(c > W ? W : c < 0 ? 0 : c), out->y1 = (int16_t)(d > W ? W : d < 0 ? 0 : d);
}

/* the steel's gradient (hands(): createLinearGradient(cx - 60, cy - 60, cx + 60, cy + 60), #eef0f1, steelHi at .5,
 * #e6e8ea) at a pixel centre */
static uint16_t steel_at(int x, int y)
{
    static const float S[3][3] = {{0xEE, 0xF0, 0xF1}, {0x8A, 0x90, 0x96}, {0xE6, 0xE8, 0xEA}};
    const float t = clamp01((((float)x + 0.5f - 173.f) + ((float)y + 0.5f - 173.f)) / 240.f);
    const int k = t < 0.5f ? 0 : 1;
    const float u = t < 0.5f ? t * 2.f : t * 2.f - 1.f;
    return rgb565_of(S[k][0] + (S[k + 1][0] - S[k][0]) * u, S[k][1] + (S[k + 1][1] - S[k][1]) * u,
                     S[k][2] + (S[k + 1][2] - S[k][2]) * u);
}

enum { M_SOLID, M_STEEL, M_GLANCE };
/* path p filled (M_SOLID: rgb at alpha a x coverage), in steel with its outline (M_STEEL: the outline the band between
 * p offset out and in by lw / 2, black, when lw > 0) or stroked alone (M_GLANCE: that band in rgb, 'lighten') */
static void poly_draw(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, int mode,
                      uint16_t rgb, float a, float lw)
{
    if (n < 3 || n > PMAX) return;
    const omni_rect_t cl = clip ? *clip : (omni_rect_t){0, 0, W, W};
    omni_pt_t po[PMAX], pi[PMAX];
    const bool band = mode == M_GLANCE || (mode == M_STEEL && lw > 0);
    if (band) {
        fk_offset_poly(p, n, lw * 0.5f, po);
        fk_offset_poly(p, n, -lw * 0.5f, pi);
    }
    omni_rect_t bb;
    bbox_of(band ? po : p, n, 1.f, &bb);
    const omni_rect_t r = cut(bb, cl);
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    float cf[CW], co[CW], ci[CW];
    /* the runs start at the path's box, never at the clip, so every pixel's sums run in one order whatever the clip
     * (band-exact to the bit) */
    for (int y = r.y0; y < r.y1; y++)
        for (int xa = bb.x0; xa < r.x1; xa += CW) {
            const int w = bb.x1 - xa < CW ? bb.x1 - xa : CW;
            if (xa + w <= r.x0) continue;
            if (mode != M_GLANCE) cover_row(p, n, y, xa, w, cf);
            if (band) cover_row(po, n, y, xa, w, co), cover_row(pi, n, y, xa, w, ci);
            const int x0 = xa < r.x0 ? r.x0 - xa : 0;
            uint16_t *d = px_at(fb, stride, cl, xa + x0, y) - x0;   /* d[x]: pixel xa + x, read from x0 on */
            for (int x = x0; x < w && xa + x < r.x1; x++) {
                if (mode == M_SOLID) {
                    const uint8_t q = a8_of(cf[x] * a);
                    if (q) d[x] = omni_blend565(d[x], rgb, q);
                    continue;
                }
                const uint8_t s = band ? a8_of(co[x] - ci[x]) : 0;
                if (mode == M_GLANCE) {
                    if (s) d[x] = lighten(d[x], rgb, s);
                    continue;
                }
                const uint8_t f = a8_of(cf[x]);
                if (f) d[x] = omni_blend565(d[x], steel_at(xa + x, y), f);
                if (s) d[x] = omni_blend565(d[x], 0x0000, s);
            }
        }
}

/* ---- the cutlery: the mockup's paths in the hand's frame (x across, y along, -y toward the tip), turned by a about
 * the centre (canvas rotate(a): (x cos a - y sin a, x sin a + y cos a)) and moved by (dx, dy) */
typedef struct { float s, c, dx, dy; } turn_t;
static omni_pt_t turned(const turn_t *t, float x, float y)
{
    return (omni_pt_t){(float)C + t->dx + x * t->c - y * t->s, (float)C + t->dy + x * t->s + y * t->c};
}

static int fork_path(float a, float dx, float dy, omni_pt_t *p)
{
    const float L = FORK_LEN;
    static const float P[22][2] = {
        {-4.5f, 18.f}, {-5.f, -0.5f},  {-3.f, -0.62f}, {-11.f, -0.72f}, {-11.f, -1.f}, {-8.f, -1.f}, {-8.f, -0.76f},
        {-4.f, -0.76f}, {-4.f, -1.f},  {-1.2f, -1.f},  {-1.2f, -0.76f}, {1.2f, -0.76f}, {1.2f, -1.f}, {4.f, -1.f},
        {4.f, -0.76f},  {8.f, -0.76f}, {8.f, -1.f},    {11.f, -1.f},    {11.f, -0.72f}, {3.f, -0.62f}, {5.f, -0.5f},
        {4.5f, 18.f},
    };
    turn_t t = {0, 0, dx, dy};
    fk_sincos(a, &t.s, &t.c);
    for (int i = 0; i < 22; i++) p[i] = turned(&t, P[i][0], i == 0 || i == 21 ? P[i][1] : P[i][1] * L);
    return 22;
}

#define QUAD_N 8
#define BOWL_N 24
static int spoon_path(float a, float dx, float dy, omni_pt_t *p)
{
    const float L = SPOON_LEN;
    turn_t t = {0, 0, dx, dy};
    fk_sincos(a, &t.s, &t.c);
    int n = 0;
    p[n++] = turned(&t, -3.5f, 22.f);
    p[n++] = turned(&t, -4.f, -0.68f * L);
    /* quadraticCurveTo(-13, -.75 L, -12.5, -.86 L) from (-4, -.68 L) */
    for (int k = 1; k <= QUAD_N; k++) {
        const float u = (float)k / (float)QUAD_N, m = 1.f - u;
        p[n++] = turned(&t, m * m * -4.f + 2.f * m * u * -13.f + u * u * -12.5f,
                        (m * m * -0.68f + 2.f * m * u * -0.75f + u * u * -0.86f) * L);
    }
    /* ellipse(0, -.86 L, 12.5, .14 L, 0, pi, 0): the bowl, from its left end over its tip to its right */
    for (int k = 1; k <= BOWL_N; k++) {
        float s, c;
        fk_sincos(PI + PI * (float)k / (float)BOWL_N, &s, &c);
        p[n++] = turned(&t, 12.5f * c, -0.86f * L + 0.14f * L * s);
    }
    /* quadraticCurveTo(13, -.75 L, 4, -.68 L) from (12.5, -.86 L) */
    for (int k = 1; k <= QUAD_N; k++) {
        const float u = (float)k / (float)QUAD_N, m = 1.f - u;
        p[n++] = turned(&t, m * m * 12.5f + 2.f * m * u * 13.f + u * u * 4.f,
                        (m * m * -0.86f + 2.f * m * u * -0.75f + u * u * -0.68f) * L);
    }
    p[n++] = turned(&t, 3.5f, 22.f);
    return n;
}

typedef int (*path_fn)(float a, float dx, float dy, omni_pt_t *p);
static void hand_pass(path_fn path, uint16_t *fb, int stride, const omni_rect_t *clip, float a, uint8_t which,
                      bool outline)
{
    omni_pt_t p[PMAX];
    if (which == RD_CU_PASS_SHADOW) {
        const int n = path(a, SHADOW_DX, SHADOW_DY, p);
        poly_draw(fb, stride, clip, p, n, M_SOLID, 0x0000, 0.55f, 0);
    } else if (which == RD_CU_PASS_STEEL) {
        const int n = path(a, 0, 0, p);
        poly_draw(fb, stride, clip, p, n, M_STEEL, 0, 1.f, outline ? 1.5f : 0.f);
    } else if (which == RD_CU_PASS_GLANCE) {
        const int n = path(a, 0, 0, p);
        poly_draw(fb, stride, clip, p, n, M_GLANCE, RD_G_PAINT, 1.f, 1.4f);
    }
}
static void hand_bbox(path_fn path, float a, uint8_t which, omni_rect_t *out)
{
    omni_pt_t p[PMAX];
    const int n = which == RD_CU_PASS_SHADOW ? path(a, SHADOW_DX, SHADOW_DY, p) : path(a, 0, 0, p);
    bbox_of(p, n, 2.5f, out);   /* the outline's half and its mitres (at most 10 x .75 at a tine's corner: 1.75) */
}

void rd_hand_fork(uint16_t *fb, int stride, const omni_rect_t *clip, float a, uint8_t which, bool outline)
{
    hand_pass(fork_path, fb, stride, clip, a, which, outline);
}
void rd_hand_spoon(uint16_t *fb, int stride, const omni_rect_t *clip, float a, uint8_t which, bool outline)
{
    hand_pass(spoon_path, fb, stride, clip, a, which, outline);
}
void rd_hand_fork_bbox(float a, uint8_t which, omni_rect_t *out) { hand_bbox(fork_path, a, which, out); }
void rd_hand_spoon_bbox(float a, uint8_t which, omni_rect_t *out) { hand_bbox(spoon_path, a, which, out); }

void rd_cu_hand_angles(const fk_face_in_t *in, float *hour, float *minute)
{
    const float m = (float)in->clk.m, h = (float)(in->clk.h % 12) + m / 60.f;
    *hour = h / 12.f * TAU;
    *minute = m / 60.f * TAU;
}

unsigned rd_cu_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_CU_STEAM;
    out[1] = RD_CU_HUB;
    return 2;
}

/* ---- the sticker's geometry, in its own frame */
/* the card: |u| <= 74, |v| <= 32, corners r 3; d2: the squared distance past the corner arcs' centres box */
static bool in_card(float u, float v, float grow)
{
    const float qx = (u < 0 ? -u : u) - 71.f, qy = (v < 0 ? -v : v) - 29.f, r = 3.f + grow;
    if (qx <= 0 && qy <= 0) return true;
    if (qx <= 0) return qy <= r;
    if (qy <= 0) return qx <= r;
    return qx * qx + qy * qy <= r * r;
}
/* strokeRect(x, -6, 7, 7) at 1 px: within .5 of its outline (square joins) */
static bool on_box(float u, float v, float x)
{
    const bool outer = u >= x - 0.5f && u <= x + 7.5f && v >= -6.5f && v <= 1.5f;
    const bool inner = u > x + 0.5f && u < x + 6.5f && v > -5.5f && v < 0.5f;
    return outer && !inner;
}
/* within r of the segment (x0, y0)-(x1, y1) (round caps) */
static bool near_seg(float u, float v, float x0, float y0, float x1, float y1, float r)
{
    const float ex = x1 - x0, ey = y1 - y0, wx = u - x0, wy = v - y0;
    float t = (wx * ex + wy * ey) / (ex * ex + ey * ey);
    t = clamp01(t);
    const float dx = wx - ex * t, dy = wy - ey * t;
    return dx * dx + dy * dy <= r * r;
}

/* the scratch card's colour at local (u, v), bilinear (the card colour past its edge) */
static void tmp_at(const uint16_t *tmp, float u, float v, float rgb[3])
{
    const float fx = u - (float)TMP_X0 - 0.5f, fy = v - TMP_Y0 - 0.5f;
    int ix = ifl(fx), iy = ifl(fy);
    const float tx = fx - (float)ix, ty = fy - (float)iy;
    float acc[3] = {0, 0, 0};
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int x = ix + i, y = iy + j;
            x = x < 0 ? 0 : x >= TMP_W ? TMP_W - 1 : x;
            y = y < 0 ? 0 : y >= TMP_H ? TMP_H - 1 : y;
            const uint16_t q = tmp[y * TMP_W + x];
            const float wgt = (i ? tx : 1.f - tx) * (j ? ty : 1.f - ty);
            acc[0] += (float)omni_ex_r(q) * wgt, acc[1] += (float)omni_ex_g(q) * wgt, acc[2] += (float)omni_ex_b(q) * wgt;
        }
    rgb[0] = acc[0], rgb[1] = acc[1], rgb[2] = acc[2];
}

/* sticker() at bind: the upright text into tmp (the card's colour behind it), then the sprite through the turn */
static void sticker_build(cu_t *k, uint16_t *tmp)
{
    const uint16_t card = R86(0xA3, 0x99, 0x83), ink = RD_C_HAZARD_DARK, red = R86(0xA3, 0x20, 0x1A);
    for (int i = 0; i < TMP_W * TMP_H; i++) tmp[i] = card;
    const omni_rect_t tc = {0, 0, TMP_W, TMP_H};
    /* lab('HEAT', -66, -19, 13, hazardDark, 'left'); the three labels untracked at x + 10, -2; lab('INADVISABLE', -48,
     * 15, 13, #a3201a, 'left'): their pens at local x, baselines on rows 25, 42 and 59 */
    const float wh = fk_atlas_width_tracked(&k->ch13, "HEAT", TRACK13);
    fk_atlas_text_tracked(tmp, TMP_W, tc, &k->ch13, "HEAT", (float)(-66 - TMP_X0) + wh / 2.f, 25, TRACK13, true, ink,
                          255);
    static const char *const LV[3] = {"MILD", "MED", "HOT"};
    static const int LX[3] = {0, 50, 96};
    for (int i = 0; i < 3; i++)
        fk_atlas_text_rgb(tmp, TMP_W, tc, &k->ch13, LV[i], -66 + LX[i] + 10 - TMP_X0, 42, 0, ink, 255, 0);
    const float wi = fk_atlas_width_tracked(&k->ch13, "INADVISABLE", TRACK13);
    fk_atlas_text_tracked(tmp, TMP_W, tc, &k->ch13, "INADVISABLE", (float)(-48 - TMP_X0) + wi / 2.f, 59, TRACK13,
                          true, red, 255);
    float sn, cs;
    fk_sincos(STK_ROT, &sn, &cs);
    static const float INK[3] = {0x17, 0x12, 0x0B}, TICK[3] = {0xB3, 0x24, 0x1A};
    for (int py = 0; py < STK_H; py++)
        for (int px = 0; px < STK_W; px++) {
            int body = 0, edge = 0, box = 0, tick = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    const float X = (float)(STK_X0 + px) + 0.125f + 0.25f * (float)sx - STK_CX;
                    const float Y = (float)(STK_Y0 + py) + 0.125f + 0.25f * (float)sy - STK_CY;
                    const float u = X * cs + Y * sn, v = -X * sn + Y * cs;
                    const bool in = in_card(u, v, 0.f);
                    body += in;
                    edge += in_card(u, v, 0.5f) && !in_card(u, v, -0.5f);
                    box += on_box(u, v, -66.f) || on_box(u, v, -16.f) || on_box(u, v, 30.f);
                    tick += near_seg(u, v, -66.f, 15.f, -62.f, 20.f, 1.1f) || near_seg(u, v, -62.f, 20.f, -54.f, 8.f, 1.1f);
                }
            const float X = (float)(STK_X0 + px) + 0.5f - STK_CX, Y = (float)(STK_Y0 + py) + 0.5f - STK_CY;
            float rgb[3];
            tmp_at(tmp, X * cs + Y * sn, -X * sn + Y * cs, rgb);
            const float fb_ = (float)box / 16.f, ft = (float)tick / 16.f;
            for (int q = 0; q < 3; q++) {
                rgb[q] += (INK[q] - rgb[q]) * fb_;
                rgb[q] += (TICK[q] - rgb[q]) * ft;
            }
            /* the card over the face at its coverage, then its edge in black .4: one colour and alpha */
            const float a1 = (float)body / 16.f, a2 = 0.4f * (float)edge / 16.f;
            const float as = 1.f - (1.f - a1) * (1.f - a2);
            const size_t o = (size_t)py * STK_W + (size_t)px;
            k->stk_a[o] = a8_of(as);
            if (as > 0) {
                const float g = a1 * (1.f - a2) / as;
                k->stk_rgb[o] = rgb565_of(rgb[0] * g, rgb[1] * g, rgb[2] * g);
            } else {
                k->stk_rgb[o] = 0;
            }
        }
}

/* the chillies on the sticker: heat of them, translate(22 + 16 i, -19) rotate(-.5) in its frame: the pod (ellipse
 * 6.5 x 2.8, #d0301e) and its stalk (fillRect(-9, -1, 3, 2), #3f7a20) */
static void chillies(uint16_t *fb, int stride, const omni_rect_t *clip, unsigned heat)
{
    float s1, c1, s2, c2;
    fk_sincos(STK_ROT, &s1, &c1);
    fk_sincos(STK_ROT - 0.5f, &s2, &c2);
    for (unsigned i = 0; i < heat && i < 3; i++) {
        const float lx = 22.f + 16.f * (float)i, ly = -19.f;
        const float ox = STK_CX + lx * c1 - ly * s1, oy = STK_CY + lx * s1 + ly * c1;
        omni_pt_t pod[24];
        for (int k = 0; k < 24; k++) {
            float s, c;
            fk_sincos(TAU * (float)k / 24.f, &s, &c);
            const float x = 6.5f * c, y = 2.8f * s;
            pod[k] = (omni_pt_t){ox + x * c2 - y * s2, oy + x * s2 + y * c2};
        }
        poly_draw(fb, stride, clip, pod, 24, M_SOLID, R86(0xD0, 0x30, 0x1E), 1.f, 0);
        static const float ST[4][2] = {{-9.f, -1.f}, {-6.f, -1.f}, {-6.f, 1.f}, {-9.f, 1.f}};
        omni_pt_t stalk[4];
        for (int k = 0; k < 4; k++)
            stalk[k] = (omni_pt_t){ox + ST[k][0] * c2 - ST[k][1] * s2, oy + ST[k][0] * s2 + ST[k][1] * c2};
        poly_draw(fb, stride, clip, stalk, 4, M_SOLID, R86(0x3F, 0x7A, 0x20), 1.f, 0);
    }
}

/* ---- the lid's pixel from its index over what lies under it, at box-local (x, y): the card's smooth gradient spans
 * a few 565 steps a channel, so it goes to RGB565 through a 4 x 4 ordered dither (rounded alone it bands in rings, as
 * Bunk Clock's card blank did) */
static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static uint16_t dither565(int r, int g, int b, int x, int y)
{
    const int t = BAYER[y & 3][x & 3] * 2 + 1;   /* (k + .5) / 16 in 32nds */
    const int R = (r * 31 * 32 / 255 + t) / 32, G = (g * 63 * 32 / 255 + t) / 32, B = (b * 31 * 32 / 255 + t) / 32;
    return (uint16_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (B > 31 ? 31 : B));
}
static uint16_t lid_px(const cu_t *k, uint8_t idx, uint16_t under, int x, int y)
{
    if (idx == 0) return under;
    const uint8_t *c = k->ramp[idx > LID_EDGE ? idx : LID_EDGE];
    if (idx > LID_EDGE) return dither565(c[0], c[1], c[2], x, y);
    const int a = (idx * 256 + LID_EDGE / 2) / LID_EDGE;   /* the rim at k / 32 over the tray, 0..256 */
    return dither565((c[0] * a + omni_ex_r(under) * (256 - a)) >> 8, (c[1] * a + omni_ex_g(under) * (256 - a)) >> 8,
                     (c[2] * a + omni_ex_b(under) * (256 - a)) >> 8, x, y);
}

/* ---- bind */
/* rd_cu_wisp (ARW1, rd/faces.h): its size and every row's extent checked, each row's offset noted */
static bool wisp_bind(cu_t *k, const uint8_t *b, size_t n)
{
    if (n < 8 + 2u * RD_CU_WISP_ROWS || memcmp(b, "ARW1", 4) != 0) return false;
    if ((b[4] | b[5] << 8) != RD_CU_WISP_HW || (b[6] | b[7] << 8) != RD_CU_WISP_ROWS) return false;
    k->wisp_ext = b + 8;
    k->wisp_body = b + 8 + 2 * RD_CU_WISP_ROWS;
    uint32_t at = 0;
    for (int i = 0; i < RD_CU_WISP_ROWS; i++) {
        const uint8_t x0 = k->wisp_ext[2 * i], x1 = k->wisp_ext[2 * i + 1];
        if (x0 > x1 || x1 > RD_CU_WISP_HW) return false;
        k->wisp_off[i] = at;
        at += (uint32_t)(x1 - x0);
    }
    const size_t len = 8 + 2u * RD_CU_WISP_ROWS + at;
    return n == len + (len & 1u);
}

static bool bind(void *ctx, fk_asset_fn get)
{
    cu_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_CU_TRAY, &n);
    bool ok = b && fk_img_from_bin(&k->tray, b, n) && k->tray.rgb && !k->tray.a && k->tray.w == BOX &&
              k->tray.h == BOX;
    b = get(ASSET_RD_CU_LID, &n);
    ok = ok && b && fk_img_from_bin(&k->lid, b, n) && k->lid.a && !k->lid.rgb && k->lid.w == BOX && k->lid.h == BOX;
    b = get(ASSET_RD_CU_WISP, &n);
    ok = ok && b && wisp_bind(k, b, n);
    b = get(ASSET_RD_F_S30, &n);
    ok = ok && b && fk_img_from_bin(&k->s30_img, b, n) && k->s30_img.a;
    b = get(ASSET_RD_F_S64, &n);
    ok = ok && b && fk_img_from_bin(&k->s64_img, b, n) && k->s64_img.a;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    b = get(ASSET_RD_F_CH18, &n);
    ok = ok && b && fk_img_from_bin(&k->ch18_img, b, n) && k->ch18_img.a;
    if (!ok) return false;
    k->s30 = (fk_atlas_t){&k->s30_img, RD_CELLS_RD_F_S30, RD_GLYPHS_RD_F_S30, ASSET_RD_F_S30_LINE_H};
    k->s64 = (fk_atlas_t){&k->s64_img, RD_CELLS_RD_F_S64, RD_GLYPHS_RD_F_S64, ASSET_RD_F_S64_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->ch18 = (fk_atlas_t){&k->ch18_img, RD_CELLS_RD_F_CH18, RD_GLYPHS_RD_F_CH18, ASSET_RD_F_CH18_LINE_H};
    k->g9 = rd_glow_lut(9);
    if (!k->g9) return false;
    for (int j = 0; j + 1 < RD_CU_LID_RAMP_STOPS; j++) {   /* rampOf(): between two stops, rounded, integer */
        const rd_ramp_stop_t *s0 = &RD_CU_LID_RAMP[j], *s1 = &RD_CU_LID_RAMP[j + 1];
        const int d = s1->i - s0->i;
        for (int i = s0->i; i <= s1->i; i++) {
            k->ramp[i][0] = (uint8_t)((s0->r * (s1->i - i) + s1->r * (i - s0->i) + d / 2) / d);
            k->ramp[i][1] = (uint8_t)((s0->g * (s1->i - i) + s1->g * (i - s0->i) + d / 2) / d);
            k->ramp[i][2] = (uint8_t)((s0->b * (s1->i - i) + s1->b * (i - s0->i) + d / 2) / d);
        }
    }
    /* staleMark(233, 155)'s group: w = measureText('STALE') with its trailing 1.8 px spacings + 14 (as bunk's), the ring
     * at x0 + 4 and STALE from x0 + 14; the shared marker puts them at cx - 30 and cx - 20 */
    const float wt = fk_atlas_width_tracked(&k->ch13, "STALE", TRACK13) + TRACK13 + 14.f;
    k->stale_cx = (int16_t)((float)C - wt / 2.f + 34.f + 0.5f);
    /* the numerals: i o'clock at (233 + 201 cos, 233 + 201 sin) of i / 12 turn less a quarter, the baseline 11 below */
    for (int i = 0; i < 12; i++) {
        float s, c;
        fk_sincos((float)(i + 1) / 12.f * TAU - TAU / 4.f, &s, &c);
        char t[3] = {0};
        if (i + 1 >= 10) t[0] = '1', t[1] = (char)('0' + (i + 1) % 10);
        else t[0] = (char)('1' + i);
        k->num_cx[i] = (float)C + c * NUM_R;
        k->num_base[i] = (int16_t)ifl((float)C + s * NUM_R + NUM_DY + 0.5f);
        const float w = fk_atlas_width_tracked(&k->s30, t, 0.f);
        k->num_box[i] = (omni_rect_t){(int16_t)ifl(k->num_cx[i] - w / 2.f - 6.f), (int16_t)(k->num_base[i] - 30),
                                      (int16_t)icl(k->num_cx[i] + w / 2.f + 6.f), (int16_t)(k->num_base[i] + 8)};
    }
    /* the sticker, through the leaf's memory as its scratch card */
    sticker_build(k, k->leaf);
    /* the lid's chords and the wake's leaf: the card over the tray, then its printed label in biro */
    for (int y = 0; y < BOX; y++) {
        int a = BOX, z = 0;
        for (int x = 0; x < BOX; x++) {
            const uint8_t idx = k->lid.a[y * BOX + x];
            if (idx && x < a) a = x;
            if (idx) z = x + 1;
            k->leaf[y * BOX + x] = lid_px(k, idx, k->tray.rgb[y * BOX + x], x, y);
        }
        k->chord[y][0] = (int16_t)(a < z ? a : 0), k->chord[y][1] = (int16_t)(a < z ? z : 0);
    }
    const omni_rect_t lc = {0, 0, BOX, BOX};
    fk_atlas_text_tracked(k->leaf, BOX, lc, &k->ch18, "N\272 42", (float)TR, Y_LABEL_N, TRACK18, true, RD_CU_BIRO, 255);
    fk_atlas_text_tracked(k->leaf, BOX, lc, &k->ch13, "EXTRA HOT. SORRY.", (float)TR, Y_LABEL_X, TRACK13, true,
                          RD_CU_BIRO, 255);
    return true;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced;
    unsigned heat;
    int shake;          /* px across: the shudder */
    float ha, ma;       /* the hands */
    bool steam;         /* the steam shows (set) */
    uint32_t T;         /* its clock, ms */
    float amt;          /* its strength */
    bool red;
    bool lid_open;      /* the wake's lid shows (swinging) */
    float sy;           /* its squash (> 0) */
    uint8_t dark;       /* the black over its rows */
    uint8_t glint;      /* the peppercorn's glint */
} cu_state_t;

static float ease_in_out(float x)
{
    x = clamp01(x);
    if (x < 0.5f) return 4.f * x * x * x;
    const float q = -2.f * x + 2.f;
    return 1.f - q * q * q / 2.f;
}

static void state_of(const fk_face_in_t *in, cu_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    s->heat = in->variant & RD_CU_HEAT;
    const bool live = s->set && !in->reduced && !in->glance;
    rd_cu_hand_angles(in, &s->ha, &s->ma);
    if (!s->set) return;
    s->steam = true;
    s->red = s->heat == 3;
    /* the shudder: three chillies, for the pulse */
    if (live && s->heat == 3 && in->pulsing && in->pulse_ms < SHAKE_MS) {
        float sn, cs;
        fk_sincos((float)in->pulse_ms / 18.f, &sn, &cs);
        const float v = sn * 3.f * (1.f - (float)in->pulse_ms / SHAKE_DECAY);
        s->shake = v < 0 ? -ifl(-v + 0.5f) : ifl(v + 0.5f);
    }
    /* the steam: the clock's ms, the puff after a wake (its own wake's clock, then the fresh wake's), the chime */
    if (in->reduced) {
        s->T = STEAM_FROZEN;
        s->amt = 0.7f;
    } else {
        s->T = ((uint32_t)(in->clk.h * 60u + in->clk.m) * 60u + in->clk.s) * 1000u + in->clk.ms;
        uint32_t pt = PUFF_MS;
        if (in->wake_ms != UINT32_MAX) pt = in->wake_ms;
        else if (in->fresh) pt = in->fresh_ms;
        s->amt = pt < PUFF_MS ? 1.f + 1.5f * (1.f - (float)pt / (float)PUFF_MS) : 1.f;
        if (!in->glance && in->thrum > 0) s->amt *= 1.f + clamp01(in->thrum);
    }
    /* the wake's lid */
    if (live && in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_MS) {
        const float sw = ease_in_out((float)in->wake_ms / (float)WAKE_MS);
        float sn, cs;
        fk_sincos(sw * PI * 0.55f, &sn, &cs);
        if (cs > 0) {
            s->lid_open = true;
            s->sy = cs;
            s->dark = a8_of(0.3f * sw);
        }
    }
    if (live && in->beep > 0) s->glint = a8_of(0.8f * clamp01(in->beep));
}

/* ---- drawing */
/* the steam (the header; rd/faces.h's RD_CU_WISP_*): each wisp's light from rd_cu_wisp's frames, summed per row, then
 * added. A wisp's taps: the frame rows it blends (two phi sides, each two theta steps), their weights and shifts */
static void wisp_of(int32_t theta_q, int32_t phi_q, wisp_t *o)
{
    const int32_t p = ((phi_q % 512) + 512) % 512, g = p & 255, p0 = p >> 8;
    o->n = 0;
    for (int side = 0; side < 2; side++) {
        const int32_t ws = side ? g : 256 - g;
        if (!ws) continue;
        /* the side's own frame (p0, or the next: frame 2 is frame 0) at phi' - its phi, d = g or -(256 - g) 256ths of
         * a sixtieth: its theta moved by 7 d (WISP_DTH a 256th) and its rows by 220 d px (11 / 3 a 256th, Q8) */
        const int32_t d = side ? -(256 - g) : g, fp = side ? (p0 + 1) % RD_CU_WISP_NPHI : p0;
        const int32_t dth = d >= 0 ? (d * WISP_DTH + 128) >> 8 : -((-d * WISP_DTH + 128) >> 8);
        const int32_t sq = d >= 0 ? (11 * d + 1) / 3 : -((11 * -d + 1) / 3);
        const int32_t th = (((theta_q + dth) % WISP_TURN) + WISP_TURN) % WISP_TURN;
        const int32_t i = th >> 8, f = th & 255;
        for (int z = 0; z < 2; z++) {
            const int32_t wt = ((z ? f : 256 - f) * ws + 128) >> 8;
            if (!wt) continue;
            const int32_t ti = (i + z) % (2 * RD_CU_WISP_N);
            wtap_t *t = &o->t[o->n++];
            t->base = (uint16_t)(((ti % RD_CU_WISP_N) * RD_CU_WISP_NPHI + fp) * RD_CU_WISP_HH);
            t->mir = ti >= RD_CU_WISP_N;
            t->wt = (uint16_t)wt;
            t->sq = sq;
        }
    }
}

/* one wisp's light on its local row u (0 .. RD_CU_WISP_H - 1) added into acc[x] for local columns x in [0,
 * RD_CU_WISP_W), H the scratch of RD_CU_WISP_HW + 2 sums; its span [*lo, *hi) (empty: *lo = *hi = 0). Integer only;
 * each stored row read over its own extent alone */
static void wisp_line(const cu_t *k, const wisp_t *wp, int u, uint32_t *H, uint32_t *acc, int *lo, int *hi)
{
    enum { HW = RD_CU_WISP_HW, HH = RD_CU_WISP_HH };
    int j0 = HW, j1 = 0;
    *lo = *hi = 0;
    for (int q = 0; q < wp->n; q++) {   /* the rows' extents, mirrored where the tap is */
        const wtap_t *t = &wp->t[q];
        const int vq = ((u * 256 + t->sq + 128) >> 1) - 128, kr = vq >> 8;
        for (int z = 0; z < 2; z++) {
            const int kk = kr + z;
            if (kk < 0 || kk >= HH) continue;
            int a = k->wisp_ext[2 * (t->base + kk)], b = k->wisp_ext[2 * (t->base + kk) + 1];
            if (a >= b) continue;
            if (t->mir) {
                const int c = HW - b;
                b = HW - a, a = c;
            }
            if (a < j0) j0 = a;
            if (b > j1) j1 = b;
        }
    }
    if (j0 >= j1) return;
    memset(H + j0, 0, sizeof H[0] * (size_t)(j1 - j0 + 2));   /* H[j + 1]: stored column j; H[j0], H[j1 + 1] zero */
    for (int q = 0; q < wp->n; q++) {
        const wtap_t *t = &wp->t[q];
        const int vq = ((u * 256 + t->sq + 128) >> 1) - 128, kr = vq >> 8, fr = vq & 255;
        for (int z = 0; z < 2; z++) {
            const int kk = kr + z;
            if (kk < 0 || kk >= HH) continue;
            const uint32_t row = t->base + (uint32_t)kk, w = (uint32_t)(z ? fr : 256 - fr) * t->wt;
            const int a = k->wisp_ext[2 * row], b = k->wisp_ext[2 * row + 1];
            if (!w || a >= b) continue;
            const uint8_t *s = k->wisp_body + k->wisp_off[row];   /* stored column a .. b - 1 */
            if (!t->mir) {
                uint32_t *h = H + 1 + a;
                for (int j = 0; j < b - a; j++) h[j] += w * s[j];
            } else {                                           /* stored column c lands on HW - 1 - c */
                uint32_t *h = H + HW - a;
                for (int j = 0; j < b - a; j++) h[-j] += w * s[j];
            }
        }
    }
    /* x 2 across: local 2m from (1, 3) / 4 of stored m - 1, m; 2m + 1 from (3, 1) / 4 of m, m + 1 */
    for (int m = j0; m < j1; m++) {
        acc[2 * m] += H[m] + 3 * H[m + 1];
        acc[2 * m + 1] += 3 * H[m + 1] + H[m + 2];
    }
    *lo = 2 * j0, *hi = 2 * j1;
    if (j0 > 0) acc[2 * j0 - 1] += H[j0 + 1], *lo = 2 * j0 - 1;   /* the span's edge pixels: a quarter of its ends */
    if (j1 < HW) acc[2 * j1] += H[j1], *hi = 2 * j1 + 1;
}

/* the wisps' clock (W2-D7): theta = wob + 1.7 w in steps of pi / RD_CU_WISP_N (Q8), phi' = frac(base_w + .23 w) in
 * sixtieths (Q8) */
static void wisps_at(uint32_t T, wisp_t W4[4])
{
    const float wob = (float)(T % 50265u) / 800.f;   /* t / 800, its period 2 pi x 8 000 ms near enough */
    for (int w = 0; w < 4; w++) {
        const float th = (wob + 1.7f * (float)w) * ((float)RD_CU_WISP_N * 256.f / PI);
        const uint32_t base = ((T % 60000u) * (uint32_t)(28 + 4 * w)) % 60000u;   /* 60 000ths of the rise */
        const int32_t ph = (int32_t)((base * 32u + 62u) / 125u) + 3533 * w;     /* x 15 360 / 60 000; .23 x 15 360 */
        wisp_of((int32_t)(th + 0.5f), ph, &W4[w]);
    }
}

static void steam_draw(const cu_t *k, const cu_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    const omni_rect_t r = cut(clip, RD_CU_STEAM);
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    cu_t *m = (cu_t *)(uintptr_t)k;   /* the scratch: draw()'s one write outside its frame (see the header) */
    static const int16_t OX[4] = {-50, -12, 24, 60};
    wisps_at(s->T, m->wisp);
    /* a channel c adds c x amt x S / 256: S = acc / (4 x 65536 x Q); a = acc >> 12, then (a K + 2^21) >> 22 (the 8.4
     * sums' rounding) with K = c x amt x 2^34 / (256 x 4 x 65536 x Q) */
    const float kk = s->amt * (17179869184.f / (256.f * 4.f * 65536.f * (float)RD_CU_WISP_Q));
    const uint32_t KR = (uint32_t)(255.f * kk + 0.5f), KG = (uint32_t)((s->red ? 150.f : 248.f) * kk + 0.5f),
                   KB = (uint32_t)((s->red ? 120.f : 236.f) * kk + 0.5f);
    uint32_t *acc = m->sacc;   /* screen column STEAM_X0 + i; all zero between rows */
    m->samples = 0;
    for (int y = r.y0; y < r.y1; y++) {
        int lo = STEAM_X1 - STEAM_X0, hi = 0;
        for (int w = 0; w < 4; w++) {
            const int x0 = C + OX[w] - RD_CU_WISP_W / 2 - STEAM_X0;   /* the wisp's local column 0 */
            int a, b;
            wisp_line(k, &m->wisp[w], y - RD_CU_STEAM.y0, m->H, acc + x0, &a, &b);
            if (a >= b) continue;
            m->samples += (uint32_t)(b - a);
            if (x0 + a < lo) lo = x0 + a;
            if (x0 + b > hi) hi = x0 + b;
        }
        if (lo >= hi) continue;
        const int xa = r.x0 - STEAM_X0 > lo ? r.x0 - STEAM_X0 : lo, xb = r.x1 - STEAM_X0 < hi ? r.x1 - STEAM_X0 : hi;
        uint16_t *d = px_at(fb, stride, clip, STEAM_X0, y);
        for (int x = xa; x < xb; x++) {
            uint32_t a = acc[x] >> 12;
            if (!a) continue;
            if (a > 0xFFFFu) a = 0xFFFFu;
            const uint32_t ar = (a * KR + 2097152u) >> 22, ag = (a * KG + 2097152u) >> 22, ab = (a * KB + 2097152u) >> 22;
            if (!(ar | ag | ab)) continue;
            const uint32_t R = (uint32_t)omni_ex_r(d[x]) + ar, G = (uint32_t)omni_ex_g(d[x]) + ag,
                           B = (uint32_t)omni_ex_b(d[x]) + ab;
            d[x] = omni_pack565((int)(R > 255 ? 255 : R), (int)(G > 255 ? 255 : G), (int)(B > 255 ? 255 : B));
        }
        memset(acc + lo, 0, sizeof acc[0] * (size_t)(hi - lo));
    }
}

bool rd_cu_wisp_row(const void *ctx, int32_t theta_q, int32_t phi_q, int u, uint32_t out[RD_CU_WISP_W])
{
    memset(out, 0, sizeof(uint32_t) * RD_CU_WISP_W);
    if (!ctx || u < 0 || u >= RD_CU_WISP_H) return false;
    wisp_t wp;
    uint32_t H[RD_CU_WISP_HW + 2];
    int a, b;
    wisp_of(theta_q, phi_q, &wp);
    wisp_line(ctx, &wp, u, H, out, &a, &b);
    return true;
}

uint32_t rd_cu_wisp_samples(const void *ctx) { return ctx ? ((const cu_t *)ctx)->samples : 0; }

/* the tray layer, then the ticks and the numerals beside it */
static void base_draw(const cu_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++)
        memset(px_at(fb, stride, clip, clip.x0, y), 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const omni_rect_t t = cut(clip, RD_CU_LIDBOX);
    for (int y = t.y0; y < t.y1 && t.x0 < t.x1; y++)
        memcpy(px_at(fb, stride, clip, t.x0, y), k->tray.rgb + (size_t)(y - BX) * BOX + (t.x0 - BX),
               (size_t)(t.x1 - t.x0) * 2);
    /* the ticks: 48 of the 60 (not the fifth), 1.5 px from r 222 to 228 */
    for (int i = 0; i < 60; i++) {
        if (i % 5 == 0) continue;
        float s, c;
        fk_sincos((float)i / 60.f * TAU, &s, &c);
        const float x0 = (float)C + s * TICK_R0, y0 = (float)C - c * TICK_R0;
        const float x1 = (float)C + s * TICK_R1, y1 = (float)C - c * TICK_R1;
        const omni_rect_t b = {(int16_t)ifl((x0 < x1 ? x0 : x1) - 2.f), (int16_t)ifl((y0 < y1 ? y0 : y1) - 2.f),
                               (int16_t)icl((x0 > x1 ? x0 : x1) + 2.f), (int16_t)icl((y0 > y1 ? y0 : y1) + 2.f)};
        if (meets(b, clip)) fk_line(fb, stride, &clip, x0, y0, x1, y1, 1.5f, RD_C_PAINT_DIM, 255);
    }
    for (int i = 0; i < 12; i++) {
        if (!meets(k->num_box[i], clip)) continue;
        char t2[3] = {0};
        if (i + 1 >= 10) t2[0] = '1', t2[1] = (char)('0' + (i + 1) % 10);
        else t2[0] = (char)('1' + i);
        const uint16_t col = (i + 1) % 3 == 0 ? RD_C_PAINT : dim565(RD_C_PAINT, 191);
        fk_atlas_text_tracked(fb, stride, clip, &k->s30, t2, k->num_cx[i], k->num_base[i], 0.f, true, col, 255);
    }
}

static void sticker_draw(const cu_t *k, const cu_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    const omni_rect_t r = cut(clip, RD_CU_STICKER);
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    for (int y = r.y0; y < r.y1; y++) {
        uint16_t *d = px_at(fb, stride, clip, r.x0, y);
        const size_t o = (size_t)(y - STK_Y0) * STK_W + (size_t)(r.x0 - STK_X0);
        for (int x = 0; x < r.x1 - r.x0; x++) {
            const uint8_t a = k->stk_a[o + (size_t)x];
            if (a) d[x] = a == 255 ? k->stk_rgb[o + (size_t)x] : omni_blend565(d[x], k->stk_rgb[o + (size_t)x], a);
        }
    }
    chillies(fb, stride, &clip, s->heat);
}

static void hands_draw(const cu_t *k, const cu_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    omni_rect_t b;
    rd_hand_fork_bbox(s->ha, RD_CU_PASS_SHADOW, &b);
    if (meets(b, clip)) rd_hand_fork(fb, stride, &clip, s->ha, RD_CU_PASS_SHADOW, false);
    rd_hand_spoon_bbox(s->ma, RD_CU_PASS_SHADOW, &b);
    if (meets(b, clip)) rd_hand_spoon(fb, stride, &clip, s->ma, RD_CU_PASS_SHADOW, false);
    rd_hand_fork_bbox(s->ha, RD_CU_PASS_STEEL, &b);
    if (meets(b, clip)) rd_hand_fork(fb, stride, &clip, s->ha, RD_CU_PASS_STEEL, true);
    rd_hand_spoon_bbox(s->ma, RD_CU_PASS_STEEL, &b);
    if (meets(b, clip)) rd_hand_spoon(fb, stride, &clip, s->ma, RD_CU_PASS_STEEL, true);
    if (meets(RD_CU_HUB, clip)) {   /* a peppercorn: #2a1a10 r 5, its 1 px paint ring; the chime's glint */
        fk_disc(fb, stride, &clip, (float)C, (float)C, 5.f, R86(0x2A, 0x1A, 0x10), 255);
        fk_ring(fb, stride, &clip, (float)C, (float)C, 5.f, 1.f, RD_C_PAINT, 255);
        if (s->glint) fk_glow_add_lut(fb, stride, &clip, C - 2, C - 2, 9, k->g9, RD_C_PAINT, s->glint);
    }
}

/* the lid shut: the card from its index over what the frame holds (the tray), then the biro */
static void lid_shut(const cu_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    const omni_rect_t r = cut(clip, RD_CU_LIDBOX);
    for (int y = r.y0; y < r.y1; y++) {
        const int ly = y - BX;
        const int a = BX + k->chord[ly][0] > r.x0 ? BX + k->chord[ly][0] : r.x0;
        const int z = BX + k->chord[ly][1] < r.x1 ? BX + k->chord[ly][1] : r.x1;
        uint16_t *d = px_at(fb, stride, clip, a, y);
        for (int x = a; x < z; x++) d[x - a] = lid_px(k, k->lid.a[ly * BOX + (x - BX)], d[x - a], x - BX, ly);
    }
    /* drawTime('--:--', 64, biro, 0, 16, .85) and lab('READY SOON', 0, 44, 13, biro), about the lid's centre */
    const int w = fk_atlas_width(&k->s64, "--:--", 0);
    fk_atlas_text_rgb(fb, stride, clip, &k->s64, "--:--", C - w / 2, Y_BIRO_TIME, 0, RD_CU_BIRO, BIRO_TIME_A, 0);
    fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "READY SOON", (float)C, Y_BIRO_LINE, TRACK13, true, RD_CU_BIRO,
                          255);
}

/* the wake's swing: the leaf's rows squashed to sy about the box's top edge (RD_CU_LID_HINGE's line), one
 * fk_rows_blit a box row cut to the card's chord on its source row; then black at dark over the lid's rows */
static void lid_swing(const cu_t *k, const cu_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    const omni_rect_t r = cut(clip, RD_CU_LIDBOX);
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    fk_row_t rows[BOX];
    const float e = (float)RD_CU_LID_HINGE.n * s->sy;
    for (int y = 0; y < BOX; y++) {
        const float c = (float)y + 0.5f - RD_CU_LID_HINGE.pos;
        rows[y] = (fk_row_t){-1, 65536u};
        if (c >= 0 && c < e) {
            const int v = (int)(c / s->sy);
            rows[y].v = (int16_t)(v > BOX - 1 ? BOX - 1 : v);
        }
    }
    const fk_rows_state_t st = {.theta = 0, .dy = 0, .shade = 0, .fade = 255};
    for (int y = r.y0; y < r.y1; y++) {
        const int v = rows[y - BX].v;
        if (v < 0) continue;
        const int a = BX + k->chord[v][0] > r.x0 ? BX + k->chord[v][0] : r.x0;
        const int z = BX + k->chord[v][1] < r.x1 ? BX + k->chord[v][1] : r.x1;
        if (a >= z) continue;
        const omni_rect_t rc = {(int16_t)a, (int16_t)y, (int16_t)z, (int16_t)(y + 1)};
        fk_rows_blit(&st, rows, k->leaf, BOX, BOX, RD_CU_LIDBOX, rc, px_at(fb, stride, clip, a, y), stride);
    }
    if (!s->dark) return;
    /* fillRect(cx - TR, top, TR x 2, TR x 2 x sy) in black .3 swing: its last row at its share */
    for (int y = r.y0; y < r.y1; y++) {
        const float cov = clamp01(e - (float)(y - BX));
        const uint8_t q = a8_of(cov * (float)s->dark / 255.f);
        if (!q) continue;
        uint16_t *d = px_at(fb, stride, clip, r.x0, y);
        for (int x = 0; x < r.x1 - r.x0; x++) d[x] = omni_blend565(d[x], 0x0000, q);
    }
}

static void scene(const cu_t *k, const cu_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    base_draw(k, fb, stride, clip);
    if (s->set) {
        sticker_draw(k, s, fb, stride, clip);
        hands_draw(k, s, fb, stride, clip);
#ifdef RD_TEST_HOOKS
        if (rd_cu_steam_ref) rd_cu_steam_ref(s->T, s->amt, s->red, fb, stride, clip);
        else
#endif
            steam_draw(k, s, fb, stride, clip);
        if (s->stale && meets(RD_CU_STALE, clip))
            rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx, STALE_CY, false);
        if (s->lid_open) lid_swing(k, s, fb, stride, clip);
    } else {
        lid_shut(k, fb, stride, clip);
    }
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const cu_t *k = ctx;
    cu_state_t s;
    state_of(in, &s);
    if (!s.shake) {
        scene(k, &s, fb, stride, clip);
        return;
    }
    /* translate(shake, 0): pixel (x, y) shows the scene's (x - shake, y); off the scene's edge, black */
    for (int y = clip.y0; y < clip.y1; y++)
        memset(px_at(fb, stride, clip, clip.x0, y), 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int a = clip.x0 - s.shake < 0 ? 0 : clip.x0 - s.shake;
    const int z = clip.x1 - s.shake > W ? W : clip.x1 - s.shake;
    if (a >= z) return;
    const omni_rect_t sc = {(int16_t)a, clip.y0, (int16_t)z, clip.y1};
    scene(k, &s, fb + (a + s.shake - clip.x0), stride, sc);
}

/* sleepFrame(): the rim, 12, 3, 6 and 9, the cutlery's outlines (or the unset --:-- as outlines), the stale marker;
 * through the orbit */
/* strokeTime('--:--', 64, ..., cx, cy + 22, 1.4): the stencil's dashes and colon are plain boxes, their outlines on
 * these edges (measured on the mockup's own frame at its orbit (2, -2), less the orbit: x0, x1, y0, y1 about the
 * centre) */
static const float UNSET_BOX[6][4] = {
    {-61.15f, -43.f, -0.5f, 9.f}, {-34.f, -15.85f, -0.5f, 9.f}, {15.85f, 34.f, -0.5f, 9.f},
    {43.f, 61.15f, -0.5f, 9.f},   {-6.35f, 6.55f, -10.f, 0.3f}, {-6.35f, 6.55f, 11.7f, 22.f},
};
static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const cu_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(px_at(fb, stride, clip, clip.x0, y), 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const float ox = (float)in->odx, oy = (float)in->ody;
    const bool set = in->clk.set, stale = set && in->clk.stale;
    fk_ring_max(fb, stride, &clip, (float)C + ox, (float)C + oy, (float)TR, 1.5f, RD_G_STEEL, 255);
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
    rd_cu_hand_angles(in, &ha, &ma);
    /* the cutlery about the orbit's centre: drawn at the centre into a clip moved by the orbit */
    const omni_rect_t oc = {(int16_t)(clip.x0 - in->odx), (int16_t)(clip.y0 - in->ody), (int16_t)(clip.x1 - in->odx),
                            (int16_t)(clip.y1 - in->ody)};
    rd_hand_fork(fb, stride, &oc, ha, RD_CU_PASS_GLANCE, false);
    rd_hand_spoon(fb, stride, &oc, ma, RD_CU_PASS_GLANCE, false);
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx + in->odx, STALE_CY + in->ody, true);
}

/* ---- dirty(): what changed between a and b */
typedef struct { omni_rect_t r[8], u; unsigned n; } rects_t;
static void add(rects_t *L, omni_rect_t r)
{
    if (L->n < 8) L->r[L->n] = r;
    if (L->n == 0) L->u = r;
    L->u.x0 = r.x0 < L->u.x0 ? r.x0 : L->u.x0, L->u.y0 = r.y0 < L->u.y0 ? r.y0 : L->u.y0;
    L->u.x1 = r.x1 > L->u.x1 ? r.x1 : L->u.x1, L->u.y1 = r.y1 > L->u.y1 ? r.y1 : L->u.y1;
    L->n++;
}
static uint8_t whole(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}
/* every pixel the hands (their shadows, steel and outlines) at (ha, ma) can touch */
static void hands_box(float ha, float ma, rects_t *L)
{
    omni_rect_t b;
    rd_hand_fork_bbox(ha, RD_CU_PASS_SHADOW, &b);
    add(L, b);
    rd_hand_fork_bbox(ha, RD_CU_PASS_STEEL, &b);
    add(L, b);
    rd_hand_spoon_bbox(ma, RD_CU_PASS_SHADOW, &b);
    add(L, b);
    rd_hand_spoon_bbox(ma, RD_CU_PASS_STEEL, &b);
    add(L, b);
}

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody &&
                          (!a->clk.set || (a->clk.h % 12 == b->clk.h % 12 && a->clk.m == b->clk.m));
        return same ? 0 : whole(out);
    }
    cu_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || sa.reduced != sb.reduced) return whole(out);
    if (sa.shake || sb.shake) return memcmp(&sa, &sb, sizeof sa) == 0 ? 0 : whole(out);   /* the frame moves */
    if (!sa.set) return 0;   /* the shut lid: nothing moves */
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.lid_open != sb.lid_open || sa.sy != sb.sy || sa.dark != sb.dark) add(&L, RD_CU_LIDBOX);
    if (sa.ha != sb.ha || sa.ma != sb.ma) {
        rects_t H;
        memset(&H, 0, sizeof H);
        hands_box(sa.ha, sa.ma, &H);
        hands_box(sb.ha, sb.ma, &H);
        add(&L, H.u);
    }
    if (sa.heat != sb.heat) add(&L, RD_CU_STICKER);
    if (sa.T != sb.T || sa.amt != sb.amt || sa.red != sb.red) add(&L, RD_CU_STEAM);
    if (sa.stale != sb.stale) add(&L, RD_CU_STALE);
    if (sa.glint != sb.glint) add(&L, RD_CU_HUB);
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
    return in_rect(&RD_CU_HIT, x, y) ? FK_TAP_PULSE : FK_TAP_NONE;
}

/* a tap on the tray: one more chilli (0 -> 1 -> 2 -> 3 -> 0, the variant's bits 0-1; RAM only) and the pulse (the
 * shudder at three); unset, nothing (the lid is on: FK_TAP_NONE, the variant left). A NULL in (only from outside the
 * runtime, fk/face.h) counts as set */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    (void)ctx;
    if (!in_rect(&RD_CU_HIT, x, y)) return FK_TAP_NONE;
    if (in != NULL && !in->clk.set) return FK_TAP_NONE;
    *variant = (uint16_t)((*variant & ~RD_CU_HEAT) | ((*variant + 1u) & RD_CU_HEAT));
    return FK_TAP_PULSE;
}

const fk_face_ops_t RD_FACE_CURRY = {
    .key = "curry",
    .ctx_bytes = sizeof(cu_t),
    .cadence_ms = 50,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = WAKE_MS,
    .pulse_len_ms = SHAKE_MS,   /* the shudder */
    .tap_var = tap_var,
    .stills_extra = 1,          /* still 11: three chillies (RD_CU_TAPPED_VARIANT) */
};
