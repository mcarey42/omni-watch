/* Hull Plate (`hull`, rd/faces.h; rd wave 1 Task 13, spec section 10.8; faces-bridge.js:806-1002: plateTexture(),
 * build(), hazardArc(), hand(), hub(), frame() and sleepFrame()).
 *
 * The base, in three layers cut from the mockup's own canvases (tools/export/rd_layers.json):
 *   rd_hp_plate, A8 278 x 261 at (10, 7): plateTexture()'s plate lit by the work lamp's pool (its second canvas), each
 *   byte an index into RD_HP_PLATE_RAMP (black to hullDeep to hullLit, and black to steelHi for the seam's rivets);
 *   outside the pool's ink the plate is black, so the layer stops there;
 *   rd_hp_marks, A8 436 x 435 at (15, 15): build()'s marks over that plate (the 48-rivet ring, the minute track, the
 *   numerals with the 4 knocked askew, the strike's cracks), each byte a colour and an alpha of RD_HP_MARK_RUNS laid
 *   over the plate's pixel (0: the plate alone);
 *   rd_hp_patch, RGB565 170 x 92 at (222, 238): build()'s canvas there, whole (the crater with the rock in it and the
 *   ticket), over everything under it.
 * Over the base, in the mockup's order: the hazard arc at 12 (hazardArc(): hazard()'s 45-degree stripes, pitch 20 sqrt
 * 2, on hazardDark with its three black chips, inside the annular sector r 196-207, +-0.2 rad; each pixel 4 x 4
 * supersampled, no libm), the wake's rivet glint (star0, the linear glow table r 8), the running light (lamp() at
 * 10:30, r 3.2: its glow from the linear table r 8, its lens and its steel ring), the readout above 6 (Chakra 18 paint,
 * tracked 2.5, its canvas 'middle' at 329: Chakra Petch's .263 em puts the baseline at 334) or the stale marker in its
 * place or, unset, the dashes above the bolt and AWAITING TIME SIGNAL (Chakra 13 paintDim), the hour and minute hands
 * (rd_hand_metal), the seconds hand (rd_hand_hull_sec), the hex-bolt hub.
 * Hands are raster polygons through fk_sincos (lesson 6), drawn at full alpha; the springs (the tick's and the wake's)
 * are const tables of the mockup's spring(), 1 - e^-5x cos(10 x) (1 - x). Pure: no esp_*, freertos or LVGL; no libm;
 * no allocation. */
#include <string.h>
#include "rd/faces.h"
#include "rd/release.h"
#include "rd/assets_meta.h"
#include "fk/compose.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define C 233
#define TAU 6.28318530718f
#define PX 10      /* rd_hp_plate's crop: the pool's ink */
#define PY 7
#define PW 278
#define PH 261
#define MX 15      /* rd_hp_marks' crop: the rivet ring's reach */
#define MY 15
#define MW 436
#define MH 435
#define QX 222     /* rd_hp_patch's crop: the ticket, the crater and the rock */
#define QY 238
#define QW 170
#define QH 92
#define KX 366.856288f   /* the strike: C + 152 (cos, sin)(4/12 turn - pi/2 - 0.03) */
#define KY 305.017319f
#define LAMP_X 94.407071f   /* the running light at 10:30: C + 196 (cos, sin)(-pi/2 - 1.5/12 turn) */
#define LAMP_Y 94.407071f
#define LAMP_R 3.2f
#define LAMP_GX 94       /* its glow about the nearest whole-pixel centre (the linear table's layout) */
#define LAMP_GY 94
#define GLOW_R 8         /* the light's glow (r 3.2 x 2.6 = 8.32) and the glint's (8): RD_LIN_LUT_RADII */
#define Y_READ 334       /* the readout: 329 middle + .263 x 18 */
#define Y_UNSET_LINE 332 /* AWAITING TIME SIGNAL: 329 middle + .263 x 13 */
#define READ_CX 234.f    /* fillText at C + 1, centred */
#define TRACK18 2.5f     /* util(ctx, 18): (18 x 0.14).toFixed(1) px */
#define TRACK13 1.8f
#define STALE_CX 235     /* staleMark(233, 329): the shared marker's STALE lands from (235, 333) */
#define STALE_CY 333
#define UNSET_CY 193.f   /* unsetTime(ctx, C, C - 40, 64, stencil) */
#define S64_ADV0 42.24f  /* Saira Stencil One's '0' at 64 px (660 of 1000 units) */
#define HZ_X 173         /* hazard(ctx, C - 60, C - 212, 120, 20, ...) */
#define HZ_Y 21
#define HZ_W 120
#define HZ_H 20
#define HZ_PITCH 28.284271f
#define HZ_R0 196.f
#define HZ_R1 207.f
#define HZ_TAN 0.202710036f   /* tan(0.2): the sector's half-angle */
#define TICK_MS 220
#define WAKE_SWING_MS 80      /* the swing starts here (spring((t - 80) / 1000)) */
#define WAKE_DONE_MS 950      /* from here the wake shows settled: its last frame at the 50 ms cadence */
#define GLINT_N 48
#define CHIME_GAIN 0.6f       /* the pool's swell: the plate's ramp x (1 + .6 thrum) */
#define FRAME_PAGE "FR 0412 \267 PLATE 7"
#define UNSET_LINE "AWAITING TIME SIGNAL"

const omni_rect_t RD_HP_RUNLIGHT = {85, 85, 104, 104};
const omni_rect_t RD_HP_HAZARD = {190, 25, 277, 42};
const omni_rect_t RD_HP_READOUT = {100, 314, 368, 344};
const omni_rect_t RD_HP_STALE = {196, 320, 276, 340};
const omni_rect_t RD_HP_TAP = {96, 306, 372, 352};
const omni_rect_t RD_HP_POOL = {PX, PY, PX + PW, PY + PH};
const omni_rect_t RD_HP_UNSET = {170, 176, 297, 211};

/* rd_hp_plate's ramp (tools/export/rd_layers.json's row names the same stops) */
const rd_ramp_stop_t RD_HP_PLATE_RAMP[RD_HP_PLATE_RAMP_STOPS] = {
    {0, 0x00, 0x00, 0x00}, {150, 0x3E, 0x12, 0x0B}, {191, 0xC4, 0x49, 0x2F}, {192, 0x00, 0x00, 0x00},
    {255, 0x8A, 0x90, 0x96},
};
/* rd_hp_marks' runs (the row's a8.runs): paint, paintDim, steel, black, steelHi */
const rd_hp_run_t RD_HP_MARK_RUNS[RD_HP_MARK_RUNS_N] = {
    {1, 128, 0xDC, 0xD3, 0xBE}, {129, 192, 0x8C, 0x84, 0x74}, {193, 252, 0x5A, 0x60, 0x68},
    {253, 253, 0x00, 0x00, 0x00}, {254, 254, 0x8A, 0x90, 0x96},
};

/* the mockup's spring() at k / 11 of the tick's 220 ms (every 20 ms), and at k / 40 of the wake's swing (every 25 ms):
 * const tables (no libm); each read by linear interpolation */
static const float TICK_SPRING[12] = {0.000000f, 0.645434f, 1.080718f, 1.170249f, 1.090908f, 1.009338f,
                                      0.979909f, 0.984954f, 0.996054f, 1.000979f, 1.000912f, 1.000000f};
static const float WAKE_SPRING[41] = {
    0.000000f, 0.166314f, 0.350711f, 0.534834f, 0.705061f, 0.852318f, 0.971598f, 1.061301f, 1.122473f,
    1.158052f, 1.172149f, 1.169433f, 1.154628f, 1.132135f, 1.105776f, 1.078648f, 1.053077f, 1.030635f,
    1.012220f, 0.998164f, 0.988358f, 0.982380f, 0.979613f, 0.979351f, 0.980878f, 0.983533f, 0.986747f,
    0.990069f, 0.993170f, 0.995838f, 0.997962f, 0.999515f, 1.000533f, 1.001091f, 1.001288f, 1.001229f,
    1.001012f, 1.000724f, 1.000431f, 1.000181f, 1.000000f};

/* the hands' shapes (hand(ctx, ang, len, w, tail)): the hour's and the minute's */
static const struct { float len, w, tail; } HAND[2] = {{112.f, 17.f, 22.f}, {174.f, 12.f, 26.f}};
#define GLANCE_TAIL 14.f   /* sleepFrame()'s outlines start 14 px back */

typedef struct { uint16_t rgb; uint8_t a; } mark_t;
typedef struct { float x, y, r; } chip_t;

typedef struct {
    fk_img_t plate, marks, patch, s64_img, ch18_img, ch13_img;
    fk_atlas_t s64, ch18, ch13;
    const uint8_t *lin8;
    uint16_t ramp[256];        /* rd_hp_plate's RGB565 */
    uint8_t ramp8[256][3];     /* and its 8-bit colours, for the chime's gain */
    mark_t mark[256];          /* rd_hp_marks' bytes: a colour and an alpha (0: none) */
    chip_t chip[3];            /* the hazard band's chips (hazard()'s rng(1604)) */
    omni_pt_t hub[6];          /* the hub's hexagon */
} hp_t;

/* ---- small helpers */
static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}

static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

static float table_at(const float *t, int n, float x)   /* t[0 .. n - 1] over x in [0, 1], linear between */
{
    if (!(x > 0)) return t[0];
    if (x >= 1) return t[n - 1];
    const float p = x * (float)(n - 1);
    const int i = (int)p;
    return t[i] + (t[i + 1] - t[i]) * (p - (float)i);
}

static uint32_t mulberry32(uint32_t *a)   /* the mockups' rng(), as integers */
{
    uint32_t t = (*a += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

unsigned rd_hp_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_HP_POOL;
    out[1] = RD_HP_RUNLIGHT;
    return 2;
}

/* ---- the hands */
/* (u, v) in the hand's frame (u along it from the pivot, v across, to its right) on the screen, about (cx, cy), at angle
 * a clockwise from 12: the canvas's rotate(a - pi/2) */
typedef struct { float cx, cy, s, c; } frame_t;
static frame_t frame_of(float cx, float cy, float a)
{
    frame_t f = {cx, cy, 0, 0};
    fk_sincos(a, &f.s, &f.c);
    return f;
}
static omni_pt_t at(const frame_t *f, float u, float v)
{
    return (omni_pt_t){f->cx + u * f->s + v * f->c, f->cy - u * f->c + v * f->s};
}

static void body_of(const frame_t *f, uint8_t which, float tail, omni_pt_t p[5])
{
    const float len = HAND[which].len, w = HAND[which].w;
    p[0] = at(f, -tail, -w / 2.f), p[1] = at(f, len - w, -w / 2.f), p[2] = at(f, len, 0.f);
    p[3] = at(f, len - w, w / 2.f), p[4] = at(f, -tail, w / 2.f);
}

static void quad(uint16_t *fb, int stride, const omni_rect_t *clip, const frame_t *f, float u0, float u1, float v0,
                 float v1, uint16_t rgb)
{
    const omni_pt_t q[4] = {at(f, u0, v0), at(f, u1, v0), at(f, u1, v1), at(f, u0, v1)};
    omni_fill_poly(fb, stride, clip, q, 4, rgb, 255);
}

/* the box of points p (n of them) grown by m px, cut to the screen */
static void box_of(const omni_pt_t *p, int n, float m, omni_rect_t *out)
{
    float x0 = p[0].x, y0 = p[0].y, x1 = x0, y1 = y0;
    for (int i = 1; i < n; i++) {
        x0 = p[i].x < x0 ? p[i].x : x0, x1 = p[i].x > x1 ? p[i].x : x1;
        y0 = p[i].y < y0 ? p[i].y : y0, y1 = p[i].y > y1 ? p[i].y : y1;
    }
    const int a = (int)(x0 - m) - 1, b = (int)(y0 - m) - 1, c = (int)(x1 + m) + 2, d = (int)(y1 + m) + 2;
    out->x0 = (int16_t)(a < 0 ? 0 : a), out->y0 = (int16_t)(b < 0 ? 0 : b);
    out->x1 = (int16_t)(c > W ? W : c), out->y1 = (int16_t)(d > W ? W : d);
}

void rd_hand_metal(uint16_t *fb, int stride, const omni_rect_t *clip, float a, uint8_t which, bool outline)
{
    if (which > 1) return;
    const frame_t f = frame_of((float)C, (float)C, a);
    const float len = HAND[which].len, w = HAND[which].w, tail = HAND[which].tail;
    omni_pt_t body[5];
    body_of(&f, which, tail, body);
    /* the steel body, shaded across its short axis */
    const uint16_t stops[2] = {RD_C_STEEL, RD_C_STEEL_DEEP};
    const omni_pt_t g0 = at(&f, 0.f, -w / 2.f), g1 = at(&f, 0.f, w / 2.f);
    fk_poly_grad(fb, stride, clip, body, 5, g0.x, g0.y, g1.x, g1.y, stops, 2, 255);
    /* the lit edge: fillRect(-tail, -w / 2, len - w + tail, 1) in steelHi */
    quad(fb, stride, clip, &f, -tail, len - w, -w / 2.f, -w / 2.f + 1.f, RD_C_STEEL_HI);
    /* the paint stripe, with its stencil bridge */
    const float sw = w * 0.38f > 3.f ? w * 0.38f : 3.f, s0 = len * 0.18f, s1 = len - w * 1.3f, gap = len * 0.62f;
    quad(fb, stride, clip, &f, s0, gap, -sw / 2.f, sw / 2.f, RD_C_PAINT);
    quad(fb, stride, clip, &f, gap + 4.f, s1, -sw / 2.f, sw / 2.f, RD_C_PAINT);
    if (outline) fk_poly_stroke(fb, stride, clip, body, 5, 1.5f, 0x0000, 255);
}

void rd_hand_metal_bbox(float a, uint8_t which, omni_rect_t *out)
{
    const frame_t f = frame_of((float)C, (float)C, a);
    omni_pt_t body[5];
    body_of(&f, which > 1 ? 1 : which, HAND[which > 1 ? 1 : which].tail, body);
    box_of(body, 5, 2.5f, out);   /* the 1.5 px stroke's half and its tip's mitre (1.7 px) */
}

/* the seconds hand: the nut 40 px back, the arm to 192 px */
#define SEC_TAIL 40.f
#define SEC_LEN 192.f
#define NUT_R 8.f
void rd_hand_hull_sec(uint16_t *fb, int stride, const omni_rect_t *clip, float a)
{
    const frame_t f = frame_of((float)C, (float)C, a);
    const omni_pt_t p0 = at(&f, -SEC_TAIL, 0.f), p1 = at(&f, SEC_LEN, 0.f);
    fk_line(fb, stride, clip, p0.x, p0.y, p1.x, p1.y, 4.f, 0x0000, 255);
    fk_line(fb, stride, clip, p0.x, p0.y, p1.x, p1.y, 2.f, RD_C_AMBER, 255);
    omni_pt_t nut[6];
    for (int i = 0; i < 6; i++) {   /* the hex nut: (-40 + 8 cos, 8 sin) of i / 6 turn, in the hand's frame */
        float s, c;
        fk_sincos((float)i * TAU / 6.f, &s, &c);
        nut[i] = at(&f, -SEC_TAIL + c * NUT_R, s * NUT_R);
    }
    omni_fill_poly(fb, stride, clip, nut, 6, RD_C_AMBER, 255);
    fk_poly_stroke(fb, stride, clip, nut, 6, 1.5f, 0x0000, 255);
    fk_disc(fb, stride, clip, p0.x, p0.y, 3.2f, 0x0000, 255);
}

void rd_hand_hull_sec_rects(float a, omni_rect_t out[2])
{
    const frame_t f = frame_of((float)C, (float)C, a);
    const omni_pt_t arm[2] = {at(&f, 0.f, 0.f), at(&f, SEC_LEN, 0.f)};
    box_of(arm, 2, 2.f, &out[0]);   /* the 4 px line's half width */
    const omni_pt_t tail[2] = {at(&f, -SEC_TAIL, 0.f), at(&f, 0.f, 0.f)};
    omni_rect_t t;
    box_of(tail, 2, NUT_R + 1.5f, &t);   /* the nut's corners (r 8) and its stroke's mitres */
    out[1] = t;
}

/* ---- the clock's angles */
float rd_hp_sec_angle(const fk_face_in_t *in)
{
    const float s = (float)in->clk.s;
    if (in->reduced || in->clk.ms >= TICK_MS) return s * TAU / 60.f;
    const float k = table_at(TICK_SPRING, 12, (float)in->clk.ms / (float)TICK_MS);
    return (s - 1.f + k) * TAU / 60.f;
}

void rd_hp_hand_angles(const fk_face_in_t *in, float *hour, float *minute)
{
    *hour = ((float)(in->clk.h % 12) + (float)in->clk.m / 60.f) * TAU / 12.f;
    *minute = (float)in->clk.m * TAU / 60.f;
}

void rd_hp_readout(const fk_face_in_t *in, char out[32])
{
    out[0] = 0;
    if (!in->clk.set) {
        strcpy(out, UNSET_LINE);
        return;
    }
    if (in->clk.stale) return;
    const unsigned v = in->variant % RD_HP_PAGES;
    if (v == 0) {   /* the date (12 h: its AM/PM suffix: the face's awake date line) */
        fk_date_str(&in->clk, out);
        strcat(out, fk_ampm(&in->clk, in->h24));
    } else if (v == 1) {   /* 'RESERVE ' + Math.round(battery x 100) + '%' */
        strcpy(out, "RESERVE ");
        const int b = in->battery;
        char n[5];
        if (b < 0) {
            strcpy(n, "--");
        } else {
            const int c = b > 100 ? 100 : b;
            int i = 0;
            if (c >= 100) n[i++] = '1';
            if (c >= 10) n[i++] = (char)('0' + c / 10 % 10);
            n[i++] = (char)('0' + c % 10);
            n[i] = 0;
        }
        strcat(out, n);
        strcat(out, "%");
    } else {
        strcpy(out, FRAME_PAGE);
    }
}

/* ---- bind */
static bool bind(void *ctx, fk_asset_fn get)
{
    hp_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_HP_PLATE, &n);
    bool ok = b && fk_img_from_bin(&k->plate, b, n) && k->plate.a && !k->plate.rgb && k->plate.w == PW &&
              k->plate.h == PH;
    b = get(ASSET_RD_HP_MARKS, &n);
    ok = ok && b && fk_img_from_bin(&k->marks, b, n) && k->marks.a && !k->marks.rgb && k->marks.w == MW &&
         k->marks.h == MH;
    b = get(ASSET_RD_HP_PATCH, &n);
    ok = ok && b && fk_img_from_bin(&k->patch, b, n) && k->patch.rgb && !k->patch.a && k->patch.w == QW &&
         k->patch.h == QH;
    b = get(ASSET_RD_F_S64, &n);
    ok = ok && b && fk_img_from_bin(&k->s64_img, b, n) && k->s64_img.a;
    b = get(ASSET_RD_F_CH18, &n);
    ok = ok && b && fk_img_from_bin(&k->ch18_img, b, n) && k->ch18_img.a;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    k->s64 = (fk_atlas_t){&k->s64_img, RD_CELLS_RD_F_S64, RD_GLYPHS_RD_F_S64, ASSET_RD_F_S64_LINE_H};
    k->ch18 = (fk_atlas_t){&k->ch18_img, RD_CELLS_RD_F_CH18, RD_GLYPHS_RD_F_CH18, ASSET_RD_F_CH18_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->lin8 = rd_glow_lut(GLOW_R);
    ok = ok && k->lin8;
    rd_ramp565(RD_HP_PLATE_RAMP, RD_HP_PLATE_RAMP_STOPS, k->ramp);
    for (int i = 0; i < 256; i++)
        k->ramp8[i][0] = (uint8_t)omni_ex_r(k->ramp[i]), k->ramp8[i][1] = (uint8_t)omni_ex_g(k->ramp[i]),
        k->ramp8[i][2] = (uint8_t)omni_ex_b(k->ramp[i]);
    for (int r = 0; r < RD_HP_MARK_RUNS_N; r++) {
        const rd_hp_run_t *q = &RD_HP_MARK_RUNS[r];
        const int len = q->i1 - q->i0 + 1;
        for (int i = q->i0; i <= q->i1; i++)
            k->mark[i] = (mark_t){omni_pack565(q->r, q->g, q->b), (uint8_t)(((i - q->i0 + 1) * 255 + len / 2) / len)};
    }
    /* hazard()'s chips: rng(Math.round(x * 7 + y * 13 + w)); per chip cx = x + r() w, top = r() < .5, r 1.5 + 2 r() */
    uint32_t seed = (uint32_t)(HZ_X * 7 + HZ_Y * 13 + HZ_W);
    for (int i = 0; i < 3; i++) {
        const double u0 = (double)mulberry32(&seed) / 4294967296.0, u1 = (double)mulberry32(&seed) / 4294967296.0;
        const double u2 = (double)mulberry32(&seed) / 4294967296.0;
        k->chip[i] = (chip_t){(float)(HZ_X + u0 * HZ_W), (float)(u1 < 0.5 ? HZ_Y : HZ_Y + HZ_H), (float)(1.5 + u2 * 2.0)};
    }
    for (int i = 0; i < 6; i++) {   /* the hub: r 13 at i / 6 turn + pi / 6 (from 3 o'clock) */
        float s, c;
        fk_sincos((float)i * TAU / 6.f + TAU / 12.f, &s, &c);
        k->hub[i] = (omni_pt_t){(float)C + c * 13.f, (float)C + s * 13.f};
    }
    return ok;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced;
    bool waking;           /* the wake runs (wake_ms under WAKE_DONE_MS, not reduced, awake) */
    uint32_t t;            /* its time */
    float sw;              /* the hands' swing (1 at rest) */
    float ha, ma, sa;      /* the hands' angles (the swing applied) */
    float lamp_on;         /* the running light */
    uint16_t lamp_rgb;
    float haz;             /* the hazard band's phase (px, 0 .. pitch) */
    float gain;            /* the pool's (1 at rest) */
    char read[32];
} hp_state_t;

static void state_of(const fk_face_in_t *in, hp_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    const bool live = !in->reduced && !in->glance;
    s->waking = live && in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_DONE_MS;
    s->t = s->waking ? in->wake_ms : 0;
    s->sw = 1.f;
    if (s->waking)
        s->sw = table_at(WAKE_SPRING, 41, ((float)in->wake_ms - (float)WAKE_SWING_MS) / 1000.f);
    if (s->set) {
        rd_hp_hand_angles(in, &s->ha, &s->ma);
        s->sa = rd_hp_sec_angle(in);
        s->ha *= s->sw, s->ma *= s->sw, s->sa *= s->sw;
        /* the hazard band crawls a pixel each second of the clock (the mockup's floor(t / 1000)); still when reduced */
        if (!in->reduced) {
            const long d = ((long)in->clk.h * 60 + in->clk.m) * 60 + in->clk.s;
            const long q = (long)((float)d / HZ_PITCH);
            float o = (float)d - (float)q * HZ_PITCH;
            while (o >= HZ_PITCH) o -= HZ_PITCH;
            while (o < 0) o += HZ_PITCH;
            s->haz = o;
        }
    }
    /* the running light: red, 150 ms on each second (else .06); unset, amber breathing (.35 +- .35 over 1.8 s) */
    if (!s->set) {
        s->lamp_rgb = RD_C_AMBER;
        if (in->reduced) {
            s->lamp_on = 0.35f;
        } else {
            float sn, cs;
            fk_sincos((float)(in->mono_ms % 1800u) / 1800.f * TAU, &sn, &cs);
            s->lamp_on = 0.35f + 0.35f * sn;
        }
    } else {
        s->lamp_rgb = RD_C_LAMP;
        s->lamp_on = in->reduced ? 0.6f : in->clk.ms < 150 ? 1.f : 0.06f;
    }
    s->gain = 1.f;
    if (live) {   /* the chime: the pool swells with the thrum; the light blinks on with each note */
        const float th = in->thrum > 1.f ? 1.f : in->thrum < 0.f ? 0.f : in->thrum;
        s->gain = 1.f + CHIME_GAIN * th;
        if (in->beep > 0.5f) s->lamp_on = 1.f;
    }
    rd_hp_readout(in, s->read);
}

/* ---- drawing */
static void base_draw(const hp_t *k, const hp_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    uint16_t lift[256];   /* the plate's ramp at the pool's gain (the chime's swell) */
    const uint16_t *ramp = k->ramp;
    if (s->gain != 1.f && meets(clip, RD_HP_POOL)) {
        const int g = (int)(s->gain * 256.f + 0.5f);
        for (int i = 0; i < 256; i++) {
            int r = k->ramp8[i][0] * g >> 8, gg = k->ramp8[i][1] * g >> 8, b = k->ramp8[i][2] * g >> 8;
            lift[i] = omni_pack565(r > 255 ? 255 : r, gg > 255 ? 255 : gg, b > 255 ? 255 : b);
        }
        lift[0] = 0;
        ramp = lift;
    }
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        memset(d + clip.x0, 0, (size_t)(clip.x1 - clip.x0) * sizeof d[0]);
        if (y >= PY && y < PY + PH) {
            const int a = clip.x0 > PX ? clip.x0 : PX, b = clip.x1 < PX + PW ? clip.x1 : PX + PW;
            const uint8_t *src = k->plate.a + (size_t)(y - PY) * PW - PX;
            for (int x = a; x < b; x++) d[x] = ramp[src[x]];
        }
        const bool patch_row = y >= QY && y < QY + QH;
        if (y >= MY && y < MY + MH) {
            const int a = clip.x0 > MX ? clip.x0 : MX, b = clip.x1 < MX + MW ? clip.x1 : MX + MW;
            const uint8_t *src = k->marks.a + (size_t)(y - MY) * MW - MX;
            for (int x = a; x < b; x++) {
                const uint8_t m = src[x];
                if (m && !(patch_row && x >= QX && x < QX + QW)) d[x] = omni_blend565(d[x], k->mark[m].rgb, k->mark[m].a);
            }
        }
        if (patch_row) {
            const int a = clip.x0 > QX ? clip.x0 : QX, b = clip.x1 < QX + QW ? clip.x1 : QX + QW;
            if (a < b) memcpy(d + a, k->patch.rgb + (size_t)(y - QY) * QW + (a - QX), (size_t)(b - a) * 2);
        }
    }
}

/* hazardArc(): hazard(ctx, 173, 21, 120, 20, phase) inside the annular sector r 196-207, +-0.2 rad about 12; each
 * pixel's 16 samples: in the sector and the band's rect, a stripe (rd.hazard), a chip (black) or the band (hazardDark) */
static void hazard_arc(const hp_t *k, const hp_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_HP_HAZARD, clip)) return;
    const int x0 = clip.x0 > RD_HP_HAZARD.x0 ? clip.x0 : RD_HP_HAZARD.x0;
    const int x1 = clip.x1 < RD_HP_HAZARD.x1 ? clip.x1 : RD_HP_HAZARD.x1;
    const int y0 = clip.y0 > RD_HP_HAZARD.y0 ? clip.y0 : RD_HP_HAZARD.y0;
    const int y1 = clip.y1 < RD_HP_HAZARD.y1 ? clip.y1 : RD_HP_HAZARD.y1;
    /* a stripe: X + Y - (y + h) - px in [0, pitch / 2) mod pitch, px = x - h - pitch + phase */
    const float base = (float)(HZ_Y + HZ_H) + (float)(HZ_X - HZ_H) - HZ_PITCH + s->haz;
    const int HR = omni_ex_r(RD_C_HAZARD), HG = omni_ex_g(RD_C_HAZARD), HB = omni_ex_b(RD_C_HAZARD);
    const int DR = omni_ex_r(RD_C_HAZARD_DARK), DG = omni_ex_g(RD_C_HAZARD_DARK), DB = omni_ex_b(RD_C_HAZARD_DARK);
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = x0; x < x1; x++) {
            int n = 0, r = 0, g = 0, b = 0;
            for (int j = 0; j < 4; j++) {
                const float Y = (float)y + 0.125f + 0.25f * (float)j;
                if (Y < (float)HZ_Y || Y >= (float)(HZ_Y + HZ_H)) continue;
                const float dy = Y - (float)C;
                for (int i = 0; i < 4; i++) {
                    const float X = (float)x + 0.125f + 0.25f * (float)i, dx = X - (float)C;
                    const float rr = dx * dx + dy * dy, adx = dx < 0 ? -dx : dx;
                    if (rr < HZ_R0 * HZ_R0 || rr > HZ_R1 * HZ_R1 || !(dy < 0) || adx > -dy * HZ_TAN) continue;
                    if (X < (float)HZ_X || X >= (float)(HZ_X + HZ_W)) continue;
                    n++;
                    bool chip = false;
                    for (int c = 0; c < 3 && !chip; c++) {
                        const float ex = X - k->chip[c].x, ey = Y - k->chip[c].y;
                        chip = ex * ex + ey * ey <= k->chip[c].r * k->chip[c].r;
                    }
                    if (chip) continue;   /* black adds nothing */
                    const float q = X + Y - base;
                    const float m = q - (float)(long)(q / HZ_PITCH) * HZ_PITCH;
                    if (m >= 0 && m < HZ_PITCH / 2.f) r += HR, g += HG, b += HB;
                    else r += DR, g += DG, b += DB;
                }
            }
            if (!n) continue;
            const uint16_t p = d[x];
            const int keep = 16 - n;
            d[x] = omni_pack565((omni_ex_r(p) * keep + r + 8) / 16, (omni_ex_g(p) * keep + g + 8) / 16,
                                (omni_ex_b(p) * keep + b + 8) / 16);
        }
    }
}

/* a linear radial glow (the linear table r 8: alpha 1 at the centre to 0 at r) in rgb at a8, about the whole-pixel
 * corner (gx, gy), over the pixels it reaches */
static void lin_glow(const hp_t *k, uint16_t *fb, int stride, omni_rect_t clip, int gx, int gy, uint16_t rgb, int a8)
{
    if (a8 <= 0) return;
    const omni_rect_t bx = {(int16_t)(gx - GLOW_R), (int16_t)(gy - GLOW_R), (int16_t)(gx + GLOW_R), (int16_t)(gy + GLOW_R)};
    if (!meets(bx, clip)) return;
    const int y0 = clip.y0 > bx.y0 ? clip.y0 : bx.y0, y1 = clip.y1 < bx.y1 ? clip.y1 : bx.y1;
    const int x0 = clip.x0 > bx.x0 ? clip.x0 : bx.x0, x1 = clip.x1 < bx.x1 ? clip.x1 : bx.x1;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        const int ey = 2 * (y - gy) + 1;
        for (int x = x0; x < x1; x++) {
            const int ex = 2 * (x - gx) + 1, N = ex * ex + ey * ey;
            if (N >= 4 * GLOW_R * GLOW_R) continue;
            const int a = k->lin8[(N - 2) / 8] * a8 / 255;
            if (a) d[x] = omni_blend565(d[x], rgb, a);
        }
    }
}

/* lamp(ctx, x, y, 3.2, col, on): the glow (.30 on, r 8.32), the lens (its colour at .3 + .7 on, the warm highlight up
 * and left), the ring (r 4.7, steel .9) */
static void running_light(const hp_t *k, const hp_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_HP_RUNLIGHT, clip)) return;
    const float on = s->lamp_on;
    if (on > 0.02f) {
        lin_glow(k, fb, stride, clip, LAMP_GX, LAMP_GY, s->lamp_rgb, a8_of(0.30f * on));
        fk_disc(fb, stride, &clip, LAMP_X, LAMP_Y, LAMP_R, s->lamp_rgb, a8_of(0.3f + 0.7f * on));
        fk_disc(fb, stride, &clip, LAMP_X - 0.96f, LAMP_Y - 1.12f, 1.4f, RD_RGB(0xFF, 0xF4, 0xE0),
                a8_of(0.63f * (0.2f + 0.8f * on)));
    } else {
        fk_disc(fb, stride, &clip, LAMP_X, LAMP_Y, LAMP_R, RD_RGB(0x12, 0x10, 0x10), 255);
    }
    fk_ring(fb, stride, &clip, LAMP_X, LAMP_Y, LAMP_R + 1.5f, 1.f, RD_C_STEEL, 230);
}

/* a rect [x0, x1) x [y0, y1) in rgb (canvas fillRect) */
static void rect_fill(uint16_t *fb, int stride, omni_rect_t clip, float x0, float y0, float x1, float y1, uint16_t rgb)
{
    const omni_pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, &clip, p, 4, rgb, 255);
}

/* unsetTime(ctx, cx, cy, 64, stencil): two pairs of dashes 1.25 dash advances either side (the '0' advance x .66 apart,
 * .72 of it wide, 6.4 px tall, Math.round'ed) and the colon's two 3.84 px squares. Awake, the mockup measures its '0'
 * with util(ctx, 18)'s letterSpacing still set (2.5 px: the shot's dashes span 171-294); the glance's has none */
static void unset_time(uint16_t *fb, int stride, omni_rect_t clip, float cx, float cy, uint16_t rgb, float spacing)
{
    const float adv = (S64_ADV0 + spacing) * 0.66f, bw = adv * 0.72f, bh = 64.f * 0.1f, d = 64.f * 0.06f;
    for (int side = -1; side <= 1; side += 2) {
        const float c = cx + (float)side * adv * 1.25f;
        for (int i = 0; i < 2; i++) {
            const float x = (float)(int)(c + ((float)i - 0.5f) * adv - bw / 2.f + 0.5f);
            const float y = (float)(int)(cy - bh / 2.f + 0.5f);
            rect_fill(fb, stride, clip, x, y, x + (float)(int)(bw + 0.5f), y + (float)(int)(bh + 0.5f), rgb);
        }
    }
    const float qx = (float)(int)(cx - d / 2.f + 0.5f);
    const float q0 = (float)(int)(cy - 64.f * 0.2f + 0.5f), q1 = (float)(int)(cy + 64.f * 0.2f - d + 0.5f);
    rect_fill(fb, stride, clip, qx, q0, qx + d, q0 + d, rgb);
    rect_fill(fb, stride, clip, qx, q1, qx + d, q1 + d, rgb);
}

/* the wake's glint: a 6-rivet trail running once round the ring (head = t / 1000 x 48 rivets), star0 at .6 on its
 * trail's fall, each a linear glow r 8 about its rivet's nearest whole-pixel corner */
static void glint(const hp_t *k, const hp_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    const float head = (float)s->t / 1000.f * (float)GLINT_N;
    for (int i = 0; i < GLINT_N; i++) {
        const float d = head - (float)i;
        if (d < 0 || d > 6.f) continue;
        float sn, cs;
        fk_sincos((float)i * TAU / (float)GLINT_N, &sn, &cs);
        const float x = (float)C + sn * 216.f, y = (float)C - cs * 216.f;
        lin_glow(k, fb, stride, clip, (int)(x + 0.5f), (int)(y + 0.5f), RD_C_STAR0, a8_of(0.6f * (1.f - d / 6.f)));
    }
}

static void hub(const hp_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    const omni_rect_t bx = {C - 16, C - 16, C + 17, C + 17};
    if (!meets(bx, clip)) return;
    omni_fill_poly(fb, stride, &clip, k->hub, 6, RD_C_STEEL_DEEP, 255);
    fk_poly_stroke(fb, stride, &clip, k->hub, 6, 1.2f, RD_C_STEEL, 255);
    fk_disc(fb, stride, &clip, (float)C, (float)C, 5.f, 0x0000, 255);
    rect_fill(fb, stride, clip, (float)(C - 7), (float)(C - 9), (float)(C - 6), (float)(C - 8), RD_C_STEEL_HI);
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const hp_t *k = ctx;
    hp_state_t s;
    state_of(in, &s);
    base_draw(k, &s, fb, stride, clip);
    hazard_arc(k, &s, fb, stride, clip);
    if (s.waking) glint(k, &s, fb, stride, clip);
    running_light(k, &s, fb, stride, clip);
    if (meets(RD_HP_READOUT, clip)) {
        if (s.stale) {
            rd_stale_mark(fb, stride, &clip, &k->ch13, STALE_CX, STALE_CY, false);
        } else if (!s.set) {
            fk_atlas_text_tracked(fb, stride, clip, &k->ch13, s.read, READ_CX - TRACK13 / 2.f, Y_UNSET_LINE, TRACK13,
                                  true, RD_C_PAINT_DIM, 255);
        } else {
            fk_atlas_text_tracked(fb, stride, clip, &k->ch18, s.read, READ_CX - TRACK18 / 2.f, Y_READ, TRACK18, true,
                                  RD_C_PAINT, 255);
        }
    }
    if (!s.set) {
        if (meets(RD_HP_UNSET, clip)) unset_time(fb, stride, clip, (float)C, UNSET_CY, RD_C_PAINT_DIM, TRACK18);
    } else {
        const omni_rect_t *cp = &clip;
        omni_rect_t b;
        rd_hand_metal_bbox(s.ha, 0, &b);
        if (meets(b, clip)) rd_hand_metal(fb, stride, cp, s.ha, 0, true);
        rd_hand_metal_bbox(s.ma, 1, &b);
        if (meets(b, clip)) rd_hand_metal(fb, stride, cp, s.ma, 1, true);
        omni_rect_t sr[2];
        rd_hand_hull_sec_rects(s.sa, sr);
        if (meets(sr[0], clip) || meets(sr[1], clip)) rd_hand_hull_sec(fb, stride, cp, s.sa);
    }
    hub(k, fb, stride, clip);
}

/* ---- the glance (sleepFrame()) */
/* the cardinal numerals' outlines (the stencil 64 atlas's 0x80 + digit codes) on Saira Stencil One's own advances,
 * centred as strokeText centres them, their canvas 'middle' at (x, y + 3): the alphabetic baseline .221 em below */
static void glance_numeral(const hp_t *k, uint16_t *fb, int stride, omni_rect_t clip, const char *digits, float cx,
                           float ymid)
{
    static const int16_t ADV_U[10] = {660, 402, 614, 610, 654, 628, 662, 576, 676, 662};
    float w = 0;
    for (const char *p = digits; *p; p++) w += (float)ADV_U[*p - '0'] * 0.064f;
    float pen = cx - w / 2.f;
    const int base = (int)(ymid + 3.f + 0.221f * 64.f + 0.5f);
    for (const char *p = digits; *p; p++) {
        const char g[2] = {(char)(0x80 + (*p - '0')), 0};
        fk_atlas_text_rgb(fb, stride, clip, &k->s64, g, (int)(pen + 0.5f), base, 0, RD_G_PAINT, 255, 0);
        pen += (float)ADV_U[*p - '0'] * 0.064f;
    }
}

static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const hp_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const float cx = (float)(C + in->odx), cy = (float)(C + in->ody);
    const bool set = in->clk.set, stale = set && in->clk.stale;
    for (int i = 0; i < 48; i += 4) {   /* every fourth rivet: fillRect(round(cx + 216 cos - 1), .., 3, 3) */
        float sn, cs;
        fk_sincos((float)i * TAU / 48.f, &sn, &cs);
        const float x = (float)(int)(cx + cs * 216.f - 1.f + 0.5f), y = (float)(int)(cy + sn * 216.f - 1.f + 0.5f);
        rect_fill(fb, stride, clip, x, y, x + 3.f, y + 3.f, RD_G_STEEL);
    }
    glance_numeral(k, fb, stride, clip, "12", cx, cy - 158.f);
    glance_numeral(k, fb, stride, clip, "3", cx + 158.f, cy);
    glance_numeral(k, fb, stride, clip, "6", cx, cy + 158.f);
    glance_numeral(k, fb, stride, clip, "9", cx - 158.f, cy);
    fk_disc(fb, stride, &clip, KX + (float)in->odx, KY + (float)in->ody, 4.f, RD_G_STEEL, 255);
    if (set) {
        float ha, ma;
        rd_hp_hand_angles(in, &ha, &ma);
        for (uint8_t w = 0; w < 2; w++) {
            const frame_t f = frame_of(cx, cy, w ? ma : ha);
            omni_pt_t body[5];
            body_of(&f, w, GLANCE_TAIL, body);
            fk_poly_stroke(fb, stride, &clip, body, 5, 1.5f, RD_G_PAINT, 255);
        }
    } else {
        unset_time(fb, stride, clip, cx, cy - 40.f, RD_G_STEEL, 0.f);
    }
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, STALE_CX + in->odx, STALE_CY + in->ody, true);
    fk_ring(fb, stride, &clip, cx, cy, 6.f, 1.f, RD_G_STEEL, 255);
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

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        float ha, ma, hb, mb;
        rd_hp_hand_angles(a, &ha, &ma);
        rd_hp_hand_angles(b, &hb, &mb);
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody && (!a->clk.set || (ha == hb && ma == mb));
        return same ? 0 : whole(out);
    }
    hp_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || sa.reduced != sb.reduced || sa.waking || sb.waking) return whole(out);
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.set && sa.sa != sb.sa) {   /* the seconds hand: the old and the new */
        omni_rect_t ra[2], rb[2];
        rd_hand_hull_sec_rects(sa.sa, ra);
        rd_hand_hull_sec_rects(sb.sa, rb);
        add(&L, ra[0]), add(&L, ra[1]), add(&L, rb[0]), add(&L, rb[1]);
    }
    if (sa.set && (sa.ha != sb.ha || sa.ma != sb.ma)) {
        omni_rect_t r;
        for (uint8_t w = 0; w < 2; w++) {
            if ((w ? sa.ma : sa.ha) == (w ? sb.ma : sb.ha)) continue;
            rd_hand_metal_bbox(w ? sa.ma : sa.ha, w, &r);
            add(&L, r);
            rd_hand_metal_bbox(w ? sb.ma : sb.ha, w, &r);
            add(&L, r);
        }
    }
    if (sa.lamp_on != sb.lamp_on || sa.lamp_rgb != sb.lamp_rgb) add(&L, RD_HP_RUNLIGHT);
    if (sa.haz != sb.haz) add(&L, RD_HP_HAZARD);
    if (sa.gain != sb.gain) add(&L, RD_HP_POOL);
    if (sa.stale != sb.stale || strcmp(sa.read, sb.read) != 0) add(&L, RD_HP_READOUT);
    if (L.n > 8) {
        out[0] = L.u;
        return 1;
    }
    for (unsigned i = 0; i < L.n; i++) out[i] = L.r[i];
    return (uint8_t)L.n;
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= RD_HP_TAP.x0 && x < RD_HP_TAP.x1 && y >= RD_HP_TAP.y0 && y < RD_HP_TAP.y1 ? FK_TAP_PULSE : FK_TAP_NONE;
}

/* the next page (RAM only), not while the readout is hidden (stale: the marker; unset: the unset line); FK_TAP_PULSE
 * with no pulse_len_ms: fk_face_gesture draws the new variant, nothing more. A NULL in (only from outside the runtime,
 * fk/face.h) counts as shown */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    const fk_tap_t t = tap(ctx, x, y);
    const bool shown = in == NULL || (in->clk.set && !in->clk.stale);
    if (t == FK_TAP_PULSE && shown) *variant = (uint16_t)((*variant + 1u) % RD_HP_PAGES);
    return t;
}

const fk_face_ops_t RD_FACE_HULL = {
    .key = "hull",
    .ctx_bytes = sizeof(hp_t),
    .cadence_ms = 50,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = 1000,
    .tap_var = tap_var,
    .stills_extra = 1,   /* still 11: the reserve page (RD_HP_TAPPED_VARIANT) */
};
