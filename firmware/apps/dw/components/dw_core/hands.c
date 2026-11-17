/* The dw hands and the faces' own shapes (dw/faces.h; dw wave 1 Task 11, spec section 11 [call]: hands are
 * raster polygons). The generic shapes (polygons, discs, glows, rings, lines, gradients) are face_kit's (fk/shapes.h,
 * rd wave 1 Task 1); the hands here build on them and on its exported helpers. Every angle through the in-tree sine table
 * (omni/trig.h), never libm's sinf/cosf, so host and device draw the same bytes. Pure, no allocation. */
#include "dw/faces.h"
#include <math.h>
#include "omni/blend565.h"
#include "omni/raster.h"
#include "omni/trig.h"

#define CX 233.f
#define CY 233.f
#define TWO_PI 6.28318530717958647692f
#define PI_F 3.14159265358979f

static const omni_rect_t SCREEN = {0, 0, 466, 466};
/* ---- the crystal shard hands (faces-reveal.js shard(), crystalHands(), crystalSleep()) */

typedef struct { float L, tail, w; uint16_t light, dark; } shard_t;
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
static const shard_t HOUR = {116.f, 18.f, 15.f, RGB(0xff, 0xe6, 0xbc), RGB(0xd9, 0x8a, 0x3c)};
static const shard_t MINUTE = {174.f, 22.f, 10.5f, RGB(0xff, 0xf1, 0xd6), RGB(0xe3, 0x9b, 0x4c)};
#define OUTLINE_RGB RGB(16, 6, 2)          /* rgba(16, 6, 2, .95), 3 px, under the fill */
#define OUTLINE_A 242
#define RIDGE_A 217                         /* rgba(255, 255, 255, .85), 0.9 px */
#define LANCE_RGB RGB(255, 190, 110)        /* rgba(255, 190, 110, .5) at globalAlpha .22: additive */
#define LANCE_A 28

/* local (lx, ly) (-y along the hand) to the screen about (cx, cy), the hand at angle (s, c) */
static omni_pt_t rot(float lx, float ly, float s, float c, float cx, float cy)
{
    return (omni_pt_t){cx + lx * c - ly * s, cy + lx * s + ly * c};
}

static void shard_pts(const shard_t *h, float L, float s, float c, omni_pt_t p[6])
{
    const float w = h->w, t = h->tail, sh = 0.8f * L;
    p[0] = rot(0, t, s, c, CX, CY);
    p[1] = rot(-0.5f * w, t - w, s, c, CX, CY);
    p[2] = rot(-0.38f * w, -sh, s, c, CX, CY);
    p[3] = rot(0, -L, s, c, CX, CY);
    p[4] = rot(0.38f * w, -sh, s, c, CX, CY);
    p[5] = rot(0.5f * w, t - w, s, c, CX, CY);
}

static void lance_pts(float L172, float s, float c, omni_pt_t q[4])
{
    q[0] = rot(-6.f, -30.f, s, c, CX, CY);
    q[1] = rot(6.f, -30.f, s, c, CX, CY);
    q[2] = rot(6.f, -L172, s, c, CX, CY);
    q[3] = rot(-6.f, -L172, s, c, CX, CY);
}

void dw_hand_shard(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, float grow, uint8_t a8)
{
    if (!(grow > 0) || a8 == 0) return;
    const shard_t *h = hour ? &HOUR : &MINUTE;
    float s, c;
    fk_sincos(a, &s, &c);
    const float L = h->L * grow;
    const int sa = (a8 * 255 + 127) / 255;
    if (!hour && 172.f * grow > 30.f) {   /* the minute hand's lance glow, under the shard */
        omni_pt_t q[4];
        lance_pts(172.f * grow, s, c, q);
        fk_poly_add(fb, stride, clip, q, 4, LANCE_RGB, (uint8_t)((LANCE_A * sa + 127) / 255));
    }
    omni_pt_t p[6], o[6];
    shard_pts(h, L, s, c, p);
    fk_offset_poly(p, 6, 1.5f, o);
    omni_fill_poly(fb, stride, clip, o, 6, OUTLINE_RGB, (uint8_t)((OUTLINE_A * sa + 127) / 255));
    omni_fill_poly(fb, stride, clip, p, 6, h->dark, a8);
    omni_fill_poly(fb, stride, clip, p, 4, h->light, a8);   /* the lit left facet: tail, shoulder, waist, tip */
    const float r0 = h->tail - 2.f, r1 = -L + 2.f;
    if (r1 < r0) {
        const omni_pt_t q[4] = {rot(-0.45f, r0, s, c, CX, CY), rot(0.45f, r0, s, c, CX, CY),
                                rot(0.45f, r1, s, c, CX, CY), rot(-0.45f, r1, s, c, CX, CY)};
        omni_fill_poly(fb, stride, clip, q, 4, RGB(255, 255, 255), (uint8_t)((RIDGE_A * sa + 127) / 255));
    }
}

static void grow_box(const omni_pt_t *p, int n, omni_rect_t *bb)
{
    for (int i = 0; i < n; i++) {
        const int16_t x0 = fk_to16(floorf(p[i].x) - 2.f), y0 = fk_to16(floorf(p[i].y) - 2.f);
        const int16_t x1 = fk_to16(ceilf(p[i].x) + 2.f), y1 = fk_to16(ceilf(p[i].y) + 2.f);
        if (x0 < bb->x0) bb->x0 = x0;
        if (y0 < bb->y0) bb->y0 = y0;
        if (x1 > bb->x1) bb->x1 = x1;
        if (y1 > bb->y1) bb->y1 = y1;
    }
}

void dw_hand_shard_bbox(float a, bool hour, omni_rect_t *out)
{
    const shard_t *h = hour ? &HOUR : &MINUTE;
    float s, c;
    fk_sincos(a, &s, &c);
    *out = (omni_rect_t){32000, 32000, -32000, -32000};
    for (int g = 0; g < 2; g++) {   /* the shape at grow 1.2 and at a small grow (its tail end stays put) */
        const float grow = g ? 1.2f : 0.05f;
        omni_pt_t p[6], o[6];
        shard_pts(h, h->L * grow, s, c, p);
        fk_offset_poly(p, 6, 1.5f, o);
        grow_box(o, 6, out);
        if (!hour) {
            omni_pt_t q[4];
            lance_pts(172.f * 1.2f, s, c, q);
            grow_box(q, 4, out);
        }
    }
}

/* ---- #2's hands (wave 2 Task 5; faces-reveal.js classicHands, 678-690) */
typedef struct { float L, tail, w; } room_hand_t;
static const room_hand_t ROOM_HOUR = {108.f, 16.f, 9.f}, ROOM_MINUTE = {162.f, 20.f, 6.5f};

/* baton(): (-0.55 w, tail), (-w / 2, -L + 0.6 w), (0, -L), (w / 2, -L + 0.6 w), (0.55 w, tail) */
static void room_pts(const room_hand_t *h, float L, float s, float c, omni_pt_t p[5])
{
    const float w = h->w;
    p[0] = rot(-0.55f * w, h->tail, s, c, CX, CY);
    p[1] = rot(-0.5f * w, -L + 0.6f * w, s, c, CX, CY);
    p[2] = rot(0, -L, s, c, CX, CY);
    p[3] = rot(0.5f * w, -L + 0.6f * w, s, c, CX, CY);
    p[4] = rot(0.55f * w, h->tail, s, c, CX, CY);
}

void dw_hand_room(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, float grow, uint8_t a8)
{
    if (!(grow > 0) || a8 == 0) return;
    const room_hand_t *h = hour ? &ROOM_HOUR : &ROOM_MINUTE;
    float s, c;
    fk_sincos(a, &s, &c);
    const float L = h->L * grow;
    omni_pt_t p[5], o[5];
    room_pts(h, L, s, c, p);
    fk_offset_poly(p, 5, 1.5f, o);   /* the 3 px stroke under the fill: its outer half shows */
    omni_fill_poly(fb, stride, clip, o, 5, RGB(0, 0, 0), a8);
    omni_fill_poly(fb, stride, clip, p, 5, RGB(0xf4, 0xee, 0xdf), a8);
    /* fillRect(-0.6, -0.9 L, 1.2, 0.5 L) */
    const omni_pt_t q[4] = {rot(-0.6f, -0.9f * L, s, c, CX, CY), rot(0.6f, -0.9f * L, s, c, CX, CY),
                            rot(0.6f, -0.4f * L, s, c, CX, CY), rot(-0.6f, -0.4f * L, s, c, CX, CY)};
    omni_fill_poly(fb, stride, clip, q, 4, RGB(0x1a, 0x1a, 0x1a), a8);
}

void dw_hand_room_bbox(float a, bool hour, omni_rect_t *out)
{
    const room_hand_t *h = hour ? &ROOM_HOUR : &ROOM_MINUTE;
    float s, c;
    fk_sincos(a, &s, &c);
    *out = (omni_rect_t){32000, 32000, -32000, -32000};
    for (int g = 0; g < 2; g++) {   /* the shape at grow 1.2 and at a small grow (its tail end stays put) */
        omni_pt_t p[5], o[5];
        room_pts(h, h->L * (g ? 1.2f : 0.05f), s, c, p);
        fk_offset_poly(p, 5, 1.5f, o);
        grow_box(o, 5, out);
    }
}

/* crystalSleep's outline(): tail -14, hour L 112 w 13, minute L 170 w 9, stroked 1.3 */
static void outline_pts(float a, bool hour, float cx, float cy, omni_pt_t p[6])
{
    const float L = hour ? 112.f : 170.f, w = hour ? 13.f : 9.f;
    float s, c;
    fk_sincos(a, &s, &c);
    p[0] = rot(0, -14.f, s, c, cx, cy);
    p[1] = rot(-0.5f * w, -14.f - w, s, c, cx, cy);
    p[2] = rot(-0.38f * w, -0.8f * L, s, c, cx, cy);
    p[3] = rot(0, -L, s, c, cx, cy);
    p[4] = rot(0.38f * w, -0.8f * L, s, c, cx, cy);
    p[5] = rot(0.5f * w, -14.f - w, s, c, cx, cy);
}

void dw_hand_outline(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, float cx, float cy,
                     uint16_t rgb, uint8_t a8)
{
    omni_pt_t p[6];
    outline_pts(a, hour, cx, cy, p);
    fk_poly_stroke(fb, stride, clip, p, 6, 1.3f, rgb, a8);
}

void dw_hand_outline_bbox(float a, bool hour, float cx, float cy, omni_rect_t *out)
{
    omni_pt_t p[6], o[6];
    outline_pts(a, hour, cx, cy, p);
    fk_offset_poly(p, 6, 0.65f, o);
    *out = (omni_rect_t){32000, 32000, -32000, -32000};
    grow_box(o, 6, out);
}


/* ---- Task 13: the glow tables as const data (dw/faces.h dw_glow_lut; the Task 12 review's M3) */
#include "glow_lut.h"

const uint8_t *dw_glow_lut(int r)
{
    switch (r) {
    case 120: return DW_GLOW_LUT_120;
    case 22: return DW_GLOW_LUT_22;
    case 12: return DW_GLOW_LUT_12;
    case 60: return DW_GLOW_LUT_60;
    case 18: return DW_GLOW_LUT_18;
    case 70: return DW_GLOW_LUT_70;
    case 40: return DW_GLOW_LUT_40;
    case 26: return DW_GLOW_LUT_26;
    case 233: return DW_GLOW_LUT_233;
    case 14: return DW_GLOW_LUT_14;
    default: return NULL;
    }
}

/* ---- #4's hands (faces-classic.js anaHand(), classic-analog's seconds) */
static const uint16_t BATON_STOPS[3] = {RGB(0xff, 0xfa, 0xf0), RGB(0xf2, 0xea, 0xd8), RGB(0xbd, 0xb4, 0xa2)};
#define BATON_OUTLINE_A 230   /* rgba(0, 0, 0, .9), 2.5 px about the path: 1.25 px out, under the fill */
#define BATON_LINE_A 140      /* rgba(60, 52, 40, .55), 0.8 px */
#define BATON_RING_Y (-78.f)  /* the hour hand's hollow roundel: rr 10, the ring from rr - 0.5 to rr + 3.2 */
#define BATON_RING_R0 9.5f
#define BATON_RING_R1 13.2f

typedef struct { omni_pt_t body[5], tip[3]; int nb; float line0, line1; } baton_t;

static void baton_pts(float s, float c, bool hour, baton_t *b)
{
    if (hour) {   /* the sword to under the ring, the ring, the tip beyond it (len 104, rr 10, ry -78) */
        b->nb = 4;
        b->body[0] = rot(-6.5f, 16.f, s, c, CX, CY);
        b->body[1] = rot(-5.f, -67.f, s, c, CX, CY);
        b->body[2] = rot(5.f, -67.f, s, c, CX, CY);
        b->body[3] = rot(6.5f, 16.f, s, c, CX, CY);
        b->tip[0] = rot(-3.6f, -89.f, s, c, CX, CY);
        b->tip[1] = rot(0.f, -104.f, s, c, CX, CY);
        b->tip[2] = rot(3.6f, -89.f, s, c, CX, CY);
        b->line0 = 10.f, b->line1 = -66.f;
    } else {      /* len 194: the shoulder at 0.62 len */
        b->nb = 5;
        b->body[0] = rot(-5.5f, 22.f, s, c, CX, CY);
        b->body[1] = rot(-4.f, -120.28f, s, c, CX, CY);
        b->body[2] = rot(0.f, -194.f, s, c, CX, CY);
        b->body[3] = rot(4.f, -120.28f, s, c, CX, CY);
        b->body[4] = rot(5.5f, 22.f, s, c, CX, CY);
        b->line0 = 16.f, b->line1 = -180.f;
    }
}

void dw_hand_baton(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, uint8_t a8)
{
    if (a8 == 0) return;
    float s, c;
    fk_sincos(a, &s, &c);
    baton_t b;
    baton_pts(s, c, hour, &b);
    const uint8_t oa = (uint8_t)((BATON_OUTLINE_A * a8 + 127) / 255);
    omni_pt_t o[5];
    fk_offset_poly(b.body, b.nb, 1.25f, o);
    omni_fill_poly(fb, stride, clip, o, b.nb, RGB(0, 0, 0), oa);
    const omni_pt_t rc = rot(0.f, BATON_RING_Y, s, c, CX, CY);
    if (hour) {
        fk_offset_poly(b.tip, 3, 1.25f, o);
        omni_fill_poly(fb, stride, clip, o, 3, RGB(0, 0, 0), oa);
        const uint16_t k = RGB(0, 0, 0);
        const fk_grad_t g = {0, 0, 0, 0, 0, &k, 1, NULL};
        fk_ring_fill(fb, stride, clip, rc.x, rc.y, BATON_RING_R0 - 1.25f, BATON_RING_R1 + 1.25f, &g, oa, false);
    }
    /* the cream gradient across the hand, -6 to +6 px (padded past them, as canvas pads) */
    const omni_pt_t g0 = rot(-6.f, 0.f, s, c, CX, CY), g1 = rot(6.f, 0.f, s, c, CX, CY);
    const fk_grad_t g = fk_grad_of(g0.x, g0.y, g1.x, g1.y, BATON_STOPS, 3);
    fk_poly_grad_g(fb, stride, clip, b.body, b.nb, &g, a8);
    if (hour) {   /* the parts as one: their overlaps painted twice in the same colour (the mockup's evenodd notch
                   * where the sword and the tip meet the ring is left out) */
        fk_poly_grad_g(fb, stride, clip, b.tip, 3, &g, a8);
        fk_ring_fill(fb, stride, clip, rc.x, rc.y, BATON_RING_R0, BATON_RING_R1, &g, a8, false);
    }
    const omni_pt_t l0 = rot(0.f, b.line0, s, c, CX, CY), l1 = rot(0.f, b.line1, s, c, CX, CY);
    fk_line(fb, stride, clip, l0.x, l0.y, l1.x, l1.y, 0.8f, RGB(60, 52, 40),
            (uint8_t)((BATON_LINE_A * a8 + 127) / 255));
}

void dw_hand_baton_bbox(float a, bool hour, omni_rect_t *out)
{
    float s, c;
    fk_sincos(a, &s, &c);
    baton_t b;
    baton_pts(s, c, hour, &b);
    omni_pt_t o[5];
    *out = (omni_rect_t){32000, 32000, -32000, -32000};
    fk_offset_poly(b.body, b.nb, 1.25f, o);
    grow_box(o, b.nb, out);
    if (hour) {
        fk_offset_poly(b.tip, 3, 1.25f, o);
        grow_box(o, 3, out);
        const omni_pt_t rc = rot(0.f, BATON_RING_Y, s, c, CX, CY);
        const float R = BATON_RING_R1 + 1.25f;
        const omni_pt_t box[2] = {{rc.x - R, rc.y - R}, {rc.x + R, rc.y + R}};
        grow_box(box, 2, out);
    }
}

#define SECOND_RGB RGB(0xe0, 0x40, 0x2a)   /* P.red */
#define SECOND_TAIL 38.f
#define SECOND_LEN 206.f
#define SECOND_W 1.8f
/* the counterweight: hexPath(0, 30, 6.5, pi / 6), its corners at 30 + 60 k degrees */
static const float HEX_CS[6][2] = {{0.8660254f, 0.5f}, {0.f, 1.f}, {-0.8660254f, 0.5f}, {-0.8660254f, -0.5f},
                                   {0.f, -1.f}, {0.8660254f, -0.5f}};

void dw_hand_second(uint16_t *fb, int stride, const omni_rect_t *clip, float a)
{
    float s, c;
    fk_sincos(a, &s, &c);
    const omni_pt_t p0 = rot(0.f, SECOND_TAIL, s, c, CX, CY), p1 = rot(0.f, -SECOND_LEN, s, c, CX, CY);
    fk_line(fb, stride, clip, p0.x, p0.y, p1.x, p1.y, SECOND_W, SECOND_RGB, 255);
    fk_disc(fb, stride, clip, p0.x, p0.y, 0.5f * SECOND_W, SECOND_RGB, 255);   /* the round caps */
    fk_disc(fb, stride, clip, p1.x, p1.y, 0.5f * SECOND_W, SECOND_RGB, 255);
    omni_pt_t h[6];
    for (int i = 0; i < 6; i++) h[i] = rot(6.5f * HEX_CS[i][0], 30.f + 6.5f * HEX_CS[i][1], s, c, CX, CY);
    omni_fill_poly(fb, stride, clip, h, 6, SECOND_RGB, 255);
}

void dw_hand_second_rects(float a, omni_rect_t out[4])
{
    float s, c;
    fk_sincos(a, &s, &c);
    const float y0 = SECOND_TAIL + 1.f, y1 = -SECOND_LEN - 1.f, step = (y0 - y1) / 4.f;
    for (int k = 0; k < 4; k++) {
        const float ya = y0 - step * (float)k, yb = y0 - step * (float)(k + 1);
        omni_pt_t p[8] = {rot(-1.f, ya, s, c, CX, CY), rot(1.f, ya, s, c, CX, CY), rot(-1.f, yb, s, c, CX, CY),
                          rot(1.f, yb, s, c, CX, CY)};
        int n = 4;
        if (k == 0) {   /* the counterweight (30 +- 6.5 along the hand) lies in the first box */
            p[n++] = rot(-6.5f, 23.5f, s, c, CX, CY), p[n++] = rot(6.5f, 23.5f, s, c, CX, CY);
            p[n++] = rot(-6.5f, 36.5f, s, c, CX, CY), p[n++] = rot(6.5f, 36.5f, s, c, CX, CY);
        }
        out[k] = (omni_rect_t){32000, 32000, -32000, -32000};
        grow_box(p, n, &out[k]);
    }
}

float dw_second_angle(const fk_clock_t *c)
{
    if (!c->set) return 0;
    if (c->ms >= DW_SECOND_OVERSHOOT_MS) return ((float)c->s / 60.f) * 2 * PI_F;
    const float x = (float)c->ms / (float)DW_SECOND_OVERSHOOT_MS;
    float sn, cs;
    fk_sincos(x * PI_F, &sn, &cs);
    const float tick = 1.f + sn * 0.18f * (1.f - x);
    return (((float)c->s - 1.f + tick) / 60.f) * 2 * PI_F;
}

/* ---- the Dymo tape and the stale marker (faces-classic.js tape(), staleMark()) */
void dw_tape(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_atlas_t *at, const char *text, float cx,
             float cy, bool tint, uint16_t rgb)
{
    if (!at || !at->img || !text) return;
    const omni_rect_t cl = clip ? *clip : SCREEN;
    float adv = 0;
    for (const char *ch = text; *ch; ch++) {
        const char one[2] = {*ch, 0};
        adv += (float)fk_atlas_width(at, one, 0) + 1.6f;   /* tracked 1.6 px */
    }
    const int w = (int)ceilf(adv) + 14, h = 19;
    const float x0 = floorf(cx - (float)w / 2 + 0.5f), y0 = floorf(cy - (float)h / 2 + 0.5f);
    const float x1 = x0 + (float)w, y1 = y0 + (float)h;
    const omni_pt_t tp[8] = {{x0 + 1.5f, y0}, {x1 - 1.5f, y0}, {x1, y0 + 1.5f}, {x1, y1 - 1.5f},
                             {x1 - 1.5f, y1}, {x0 + 1.5f, y1}, {x0, y1 - 1.5f}, {x0, y0 + 1.5f}};
    omni_fill_poly(fb, stride, clip, tp, 8, RGB(17, 17, 17), 255);
    const omni_pt_t hl[4] = {{x0 + 2, y0 + 1}, {x1 - 2, y0 + 1}, {x1 - 2, y0 + 2}, {x0 + 2, y0 + 2}};
    omni_fill_poly(fb, stride, clip, hl, 4, RGB(255, 255, 255), 18);
    fk_poly_stroke(fb, stride, clip, tp, 8, 1.f, RGB(255, 255, 255), 26);
    const int base = (int)floorf(cy + 0.5f) + 6;
    float px = x0 + 7;
    for (const char *ch = text; *ch; ch++) {
        const char one[2] = {*ch, 0};
        if (tint) fk_atlas_text_rgb(fb, stride, cl, at, one, (int)floorf(px + 0.5f), base, 0, rgb, 255, 0);
        else fk_atlas_text(fb, stride, cl, at, one, (int)floorf(px + 0.5f), base, 0, 255);
        px += (float)fk_atlas_width(at, one, 0) + 1.6f;
    }
}

void dw_stale_mark(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_atlas_t *at, float x, float y)
{
    const uint16_t AMBER = RGB(242, 163, 58);
    const float lx = x - 52.f, ly = y;
    fk_glow_add_lut(fb, stride, clip, (int)lx, (int)ly, 12, dw_glow_lut(12), AMBER, 90);   /* the lamp's spill */
    fk_disc(fb, stride, clip, lx, ly, 4.f, RGB(200, 128, 40), 255);                        /* the dome */
    fk_disc(fb, stride, clip, lx - 0.8f, ly - 0.9f, 2.4f, AMBER, 230);
    fk_disc(fb, stride, clip, lx - 1.4f, ly - 1.6f, 1.1f, RGB(255, 236, 200), 220);         /* the highlight */
    static const float O8[8][2] = {{0, -1}, {0.70710678f, -0.70710678f}, {1, 0}, {0.70710678f, 0.70710678f},
                                   {0, 1}, {-0.70710678f, 0.70710678f}, {-1, 0}, {-0.70710678f, -0.70710678f}};
    omni_pt_t ring[8];
    for (int i = 0; i < 8; i++) ring[i] = (omni_pt_t){lx + 5.5f * O8[i][0], ly + 5.5f * O8[i][1]};
    fk_poly_stroke(fb, stride, clip, ring, 8, 1.f, RGB(184, 200, 176), 115);
    dw_tape(fb, stride, clip, at, "NOT SYNCED", x + 8.f, y, false, 0);
}

float dw_second_angle_in(const fk_face_in_t *in)
{
    if (in->reduced && in->clk.set) return ((float)in->clk.s / 60.f) * 2 * PI_F;
    return dw_second_angle(&in->clk);
}

/* ---- #10's hands (wave 2 Task 6; faces-modern.js shardHand, the wand hand, caHands) */
#define AMBER_DEEP RGB(0xa8, 0x59, 0x21)
#define AMBER_HI RGB(0xff, 0xb4, 0x5c)
#define AMBER_CORE RGB(0xff, 0xd3, 0x8a)
#define AMBER_EMBER RGB(0x5e, 0x2c, 0x1f)
#define AMBER_MID RGB(0xca, 0x74, 0x2b)
#define AMBER_GLOW RGB(255, 150, 60)       /* shadowColor rgba(255, 150, 60, .8), blur 10: the lit facet's glow */
#define AMBER_GLOW_IN 34                   /* the glow as two additive halos about the hand (3 and 7 px out) */
#define AMBER_GLOW_OUT 18
typedef struct { float len, w, tail; } amber_t;
static const amber_t AMBER_HOUR = {100.f, 16.f, 18.f}, AMBER_MINUTE = {150.f, 12.f, 22.f};
static const uint16_t LIT_STOPS[3] = {AMBER_DEEP, AMBER_HI, AMBER_CORE}, EMBER_STOPS[3] = {AMBER_EMBER, AMBER_DEEP,
                                                                                         AMBER_MID};
static const float LIT_POS[3] = {0.f, 0.55f, 1.f}, EMBER_POS[3] = {0.f, 0.6f, 1.f};

/* shardHand's points (P(x, y): y up the hand): tail (0, -tail), bl (-.36 w, 0), ml (-w / 2, .62 len), tip (0, len),
 * mr, br; the hand at angle (s, c) about the centre */
static void amber_pts(const amber_t *h, float s, float c, omni_pt_t p[6])
{
    const float w = h->w, L = h->len;
    p[0] = rot(0, h->tail, s, c, CX, CY);
    p[1] = rot(-0.36f * w, 0, s, c, CX, CY);
    p[2] = rot(-0.5f * w, -0.62f * L, s, c, CX, CY);
    p[3] = rot(0, -L, s, c, CX, CY);
    p[4] = rot(0.5f * w, -0.62f * L, s, c, CX, CY);
    p[5] = rot(0.36f * w, 0, s, c, CX, CY);
}

/* the halo m px about the hand (its tip and tail drawn out further, as a blur rounds them) */
static void amber_halo(const amber_t *h, float m, float s, float c, omni_pt_t p[6])
{
    const float w = h->w, L = h->len;
    p[0] = rot(0, h->tail + m, s, c, CX, CY);
    p[1] = rot(-0.36f * w - m, 0, s, c, CX, CY);
    p[2] = rot(-0.5f * w - m, -0.62f * L, s, c, CX, CY);
    p[3] = rot(0, -L - m, s, c, CX, CY);
    p[4] = rot(0.5f * w + m, -0.62f * L, s, c, CX, CY);
    p[5] = rot(0.36f * w + m, 0, s, c, CX, CY);
}

void dw_hand_amber(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, uint8_t a8)
{
    if (a8 == 0) return;
    const amber_t *h = hour ? &AMBER_HOUR : &AMBER_MINUTE;
    float s, c;
    fk_sincos(a, &s, &c);
    const int sa = (a8 * 255 + 127) / 255;
    omni_pt_t p[6], q[6];
    /* the lit facet's shadow glow: two additive halos, under the facets */
    amber_halo(h, 7.f, s, c, q);
    fk_poly_add(fb, stride, clip, q, 6, AMBER_GLOW, (uint8_t)((AMBER_GLOW_OUT * sa + 127) / 255));
    amber_halo(h, 3.f, s, c, q);
    fk_poly_add(fb, stride, clip, q, 6, AMBER_GLOW, (uint8_t)((AMBER_GLOW_IN * sa + 127) / 255));
    amber_pts(h, s, c, p);
    /* the facets' gradients run from the pivot (0) up to the tip's height (1), padded beyond */
    const omni_pt_t g0 = rot(0, 0, s, c, CX, CY), g1 = rot(0, -h->len, s, c, CX, CY);
    fk_grad_t g = fk_grad_of(g0.x, g0.y, g1.x, g1.y, LIT_STOPS, 3);
    g.pos = LIT_POS;
    const omni_pt_t lit[4] = {p[0], p[1], p[2], p[3]}, ember[4] = {p[0], p[5], p[4], p[3]};
    fk_poly_grad_g(fb, stride, clip, lit, 4, &g, a8);
    g.stops = EMBER_STOPS, g.pos = EMBER_POS;
    fk_poly_grad_g(fb, stride, clip, ember, 4, &g, a8);
    /* the hot fracture line (rgba(255, 236, 190, .9), 1.2 px, from .05 len to the tip) and the ridge (rgba(255, 220,
     * 160, .35), 0.8 px) */
    const float w = h->w, L = h->len;
    const omni_pt_t f0 = rot(0, -0.05f * L, s, c, CX, CY);
    fk_line(fb, stride, clip, f0.x, f0.y, p[3].x, p[3].y, 1.2f, RGB(255, 236, 190), (uint8_t)((230 * sa + 127) / 255));
    const omni_pt_t r0 = rot(-0.2f * w, -0.2f * L, s, c, CX, CY), r1 = rot(-0.08f * w, -0.45f * L, s, c, CX, CY),
                    r2 = rot(-0.22f * w, -0.7f * L, s, c, CX, CY);
    const uint8_t ra = (uint8_t)((89 * sa + 127) / 255);
    fk_line(fb, stride, clip, r0.x, r0.y, r1.x, r1.y, 0.8f, RGB(255, 220, 160), ra);
    fk_line(fb, stride, clip, r1.x, r1.y, r2.x, r2.y, 0.8f, RGB(255, 220, 160), ra);
    /* the outline: rgba(255, 190, 110, .6), 1 px */
    fk_poly_stroke(fb, stride, clip, p, 6, 1.f, RGB(255, 190, 110), (uint8_t)((153 * sa + 127) / 255));
}

void dw_hand_amber_bbox(float a, bool hour, omni_rect_t *out)
{
    const amber_t *h = hour ? &AMBER_HOUR : &AMBER_MINUTE;
    float s, c;
    fk_sincos(a, &s, &c);
    omni_pt_t p[6], o[6];
    *out = (omni_rect_t){32000, 32000, -32000, -32000};
    amber_halo(h, 7.f, s, c, p);
    grow_box(p, 6, out);
    amber_pts(h, s, c, p);
    fk_offset_poly(p, 6, 0.5f, o);   /* the outline's mitres */
    grow_box(o, 6, out);
}

/* the wand hand (168, 34) (W 30, the pivot at x 15): the grip, the ribbed steel body, the copper collar, the claws and the
 * cyan tip, with the tip's glow and its trail (caHands) */
#define WAND_LEN 168.f
#define WAND_TAIL 34.f
#define WAND_TIP (WAND_LEN * 0.9f)       /* the emitter's centre: 151.2 px out */
#define WAND_GLOW_R 26
#define WAND_GLOW_RGB RGB(127, 232, 255)
#define WAND_TRAIL_STEP 0.022f
#define WAND_TRAIL_N 5

/* trail glow k's whole-pixel centre (R147 PR15: fk_glow_add_lut's int centres) at the hand's angle a */
static void trail_xy(float a, int k, int *x, int *y)
{
    float s, c;
    fk_sincos(a - (float)k * WAND_TRAIL_STEP, &s, &c);
    *x = (int)floorf(CX + s * WAND_TIP + 0.5f), *y = (int)floorf(CY - c * WAND_TIP + 0.5f);
}

/* a box x0..x1 across the hand, from u0 to u1 up it (fillRect in the sprite) */
static void wand_rect(float x0, float x1, float u0, float u1, float s, float c, omni_pt_t q[4])
{
    q[0] = rot(x0, -u0, s, c, CX, CY), q[1] = rot(x1, -u0, s, c, CX, CY);
    q[2] = rot(x1, -u1, s, c, CX, CY), q[3] = rot(x0, -u1, s, c, CX, CY);
}

/* the emitter's ellipse (rx 3 across, ry 5 along the hand), its coverage by the distance to its edge (the implicit
 * function over its gradient's length) */
static void wand_tip(uint16_t *fb, int stride, const omni_rect_t *clip, float s, float c, uint16_t rgb)
{
    const float ex = CX + s * WAND_TIP, ey = CY - c * WAND_TIP, rx = 3.f, ry = 5.f;
    omni_rect_t bb;
    fk_disc_box(ex, ey, ry, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fk_px_at(fb, stride, clip, x0, y);
        const float dy = (float)y + 0.5f - ey;
        for (int x = x0; x < x1; x++, d++) {
            const float dx = (float)x + 0.5f - ex;
            const float u = dx * c + dy * s, v = -dx * s + dy * c;   /* across, along (down) the hand */
            const float f = (u * u) / (rx * rx) + (v * v) / (ry * ry) - 1.f;
            const float gu = 2.f * u / (rx * rx), gv = 2.f * v / (ry * ry), gl = sqrtf(gu * gu + gv * gv);
            float cov = gl > 0 ? 0.5f - f / gl : 1.f;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            *d = omni_blend565(*d, rgb, (int)(cov * 255.f + 0.5f));
        }
    }
}

void dw_hand_wand(uint16_t *fb, int stride, const omni_rect_t *clip, float a, float pulse)
{
    const uint8_t *lut = dw_glow_lut(WAND_GLOW_R);
    /* the trail, far to near: glowSprite(.., 26, .9) at globalAlpha .22 (1 - k / 5), and .95 pulse at the tip */
    for (int k = WAND_TRAIL_N - 1; k >= 0 && lut; k--) {
        const float al = 0.9f * (k == 0 ? 0.95f * pulse : 0.22f * (1.f - (float)k / 5.f));
        const int a8 = (int)(al * 255.f + 0.5f);
        int x, y;
        trail_xy(a, k, &x, &y);
        if (a8 > 0)
            fk_glow_add_lut(fb, stride, clip, x, y, WAND_GLOW_R, lut, WAND_GLOW_RGB, (uint8_t)(a8 > 255 ? 255 : a8));
    }
    float s, c;
    fk_sincos(a, &s, &c);
    omni_pt_t q[4];
    /* the counterweight tail: a dark grip (#3a3f46, 6 px, the tail's length) and its end (#80868e, 8 x 6) */
    wand_rect(-3.f, 3.f, -WAND_TAIL, 0.f, s, c, q);
    omni_fill_poly(fb, stride, clip, q, 4, RGB(0x3a, 0x3f, 0x46), 255);
    wand_rect(-4.f, 4.f, -WAND_TAIL, -WAND_TAIL + 6.f, s, c, q);
    omni_fill_poly(fb, stride, clip, q, 4, RGB(0x80, 0x86, 0x8e), 255);
    /* the ribbed steel body to .74 len: 5 px ribs, light from 0, dark between (the dark under all of it, so the ribs
     * meet without a seam) */
    wand_rect(-2.6f, 2.6f, 0.f, 125.f, s, c, q);
    omni_fill_poly(fb, stride, clip, q, 4, RGB(0x6c, 0x73, 0x7c), 255);
    for (int y = 0; (float)y < WAND_LEN * 0.74f; y += 10) {
        wand_rect(-2.6f, 2.6f, (float)y, (float)y + 5.f, s, c, q);
        omni_fill_poly(fb, stride, clip, q, 4, RGB(0xa9, 0xb1, 0xba), 255);
    }
    /* the copper collar (9 px wide, .06 len tall, its top at .8 len) and the emitter's claws (#c9ccd2, 1.4 px) */
    wand_rect(-4.5f, 4.5f, WAND_LEN * 0.74f, WAND_LEN * 0.8f, s, c, q);
    omni_fill_poly(fb, stride, clip, q, 4, RGB(0xb0, 0x60, 0x2a), 255);
    for (int side = -1; side <= 1; side += 2) {
        const omni_pt_t c0 = rot(4.f * (float)side, -WAND_LEN * 0.8f, s, c, CX, CY);
        const omni_pt_t c1 = rot(5.f * (float)side, -WAND_TIP, s, c, CX, CY);
        fk_line(fb, stride, clip, c0.x, c0.y, c1.x, c1.y, 1.4f, RGB(0xc9, 0xcc, 0xd2), 255);
    }
    /* the tip: #d8fbff (its own 8 px cyan shadow is left to the tip glow under it) */
    wand_tip(fb, stride, clip, s, c, RGB(0xd8, 0xfb, 0xff));
}

void dw_hand_wand_rects(float a, omni_rect_t out[4])
{
    float s, c;
    fk_sincos(a, &s, &c);
    /* along the hand: the tail and the root, the body's two halves, then the collar, the claws, the tip and every
     * trail glow's box (fk_glow_add_lut's: x - r - 1 .. x + r + 2) */
    static const float SEG[4][3] = {{-WAND_TAIL - 1.f, 20.f, 5.f}, {18.f, 72.f, 3.5f}, {70.f, 127.f, 3.5f},
                                    {123.f, WAND_TIP + 6.f, 6.5f}};
    for (int k = 0; k < 4; k++) {
        omni_pt_t q[4];
        wand_rect(-SEG[k][2], SEG[k][2], SEG[k][0], SEG[k][1], s, c, q);
        out[k] = (omni_rect_t){32000, 32000, -32000, -32000};
        grow_box(q, 4, &out[k]);
    }
    for (int k = 0; k < WAND_TRAIL_N; k++) {
        int x, y;
        trail_xy(a, k, &x, &y);
        const int16_t x0 = (int16_t)(x - WAND_GLOW_R - 1), y0 = (int16_t)(y - WAND_GLOW_R - 1);
        const int16_t x1 = (int16_t)(x + WAND_GLOW_R + 2), y1 = (int16_t)(y + WAND_GLOW_R + 2);
        if (x0 < out[3].x0) out[3].x0 = x0;
        if (y0 < out[3].y0) out[3].y0 = y0;
        if (x1 > out[3].x1) out[3].x1 = x1;
        if (y1 > out[3].y1) out[3].y1 = y1;
    }
}

/* ---- #13's Breguet hands (wave 2 Task 7; faces-modern.js fwHands breguet, :853-863) */
typedef struct { float L, w; } breguet_t;
static const breguet_t BREGUET_HOUR = {112.f, 4.5f}, BREGUET_MINUTE = {170.f, 3.2f};

void dw_hand_breguet(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, uint16_t rgb, uint8_t a8)
{
    const breguet_t *h = hour ? &BREGUET_HOUR : &BREGUET_MINUTE;
    const float L = h->L, w = h->w;
    float s, c;
    fk_sincos(a, &s, &c);
    const omni_pt_t p0 = rot(0, 18.f, s, c, CX, CY), p1 = rot(0, -0.72f * L, s, c, CX, CY);
    fk_line(fb, stride, clip, p0.x, p0.y, p1.x, p1.y, w, rgb, a8);                 /* the shaft (butt ends) */
    const omni_pt_t m = rot(0, -0.78f * L, s, c, CX, CY);
    fk_ring(fb, stride, clip, m.x, m.y, 0.07f * L, 2.f, rgb, a8);                  /* the hollow moon */
    const omni_pt_t t[3] = {rot(-0.9f * w, -0.84f * L, s, c, CX, CY), rot(0, -L, s, c, CX, CY),
                            rot(0.9f * w, -0.84f * L, s, c, CX, CY)};
    omni_fill_poly(fb, stride, clip, t, 3, rgb, a8);                                /* the tip */
}

void dw_hand_breguet_bbox(float a, bool hour, omni_rect_t *out)
{
    const breguet_t *h = hour ? &BREGUET_HOUR : &BREGUET_MINUTE;
    const float L = h->L, w = h->w, mr = 0.07f * L + 1.f;
    float s, c;
    fk_sincos(a, &s, &c);
    const omni_pt_t p[7] = {rot(-0.5f * w, 18.f, s, c, CX, CY),       rot(0.5f * w, 18.f, s, c, CX, CY),
                            rot(-0.5f * w, -0.72f * L, s, c, CX, CY), rot(0.5f * w, -0.72f * L, s, c, CX, CY),
                            rot(-0.9f * w, -0.84f * L, s, c, CX, CY), rot(0, -L, s, c, CX, CY),
                            rot(0.9f * w, -0.84f * L, s, c, CX, CY)};
    *out = (omni_rect_t){32000, 32000, -32000, -32000};
    grow_box(p, 7, out);
    /* the moon (its outer edge at r .07 L + 1): its centre +- mr */
    const omni_pt_t m = rot(0, -0.78f * L, s, c, CX, CY);
    const omni_pt_t q[2] = {{m.x - mr, m.y - mr}, {m.x + mr, m.y + mr}};
    grow_box(q, 2, out);
}

/* ---- #9's shapes (wave 2 Task 9; faces-classic.js domeLamp :149-162, meterNeedle :922-932) */
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* domeLamp: the spill (a radial gradient rgb at .35 on to 0 at 3 r, when on > 0), the dome (a two-point radial
 * gradient from (x - .35 r, y - .4 r) r 0 to (x, y) r: on, #fff, rgb at .4 + .6 on, rgb at .25 + .3 on; off, #3a3a36,
 * #1c1c1a, #0b0b0a; stops at 0, .45, 1) and its ring (r + 1.5, 1 px, rgba(184, 200, 176, .45)), each pixel by its
 * centre's distance (only +, -, x, / and sqrtf: host and device draw the same bytes) */
void dw_dome_lamp(uint16_t *fb, int stride, const omni_rect_t *clip, float x, float y, float r, uint16_t rgb, float on)
{
    if (!(r > 0)) return;
    const bool lit = on > 0;
    const float reach = lit ? 3.f * r : r + 2.5f;
    omni_rect_t bb;
    fk_disc_box(x, y, reach, &bb);
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    const float cr = (float)omni_ex_r(rgb), cg = (float)omni_ex_g(rgb), cb = (float)omni_ex_b(rgb);
    /* the dome's stops: r, g, b (0..255) and alpha */
    float st[3][4];
    if (lit) {
        const float a1 = clamp01(0.4f + 0.6f * on), a2 = clamp01(0.25f + 0.3f * on);
        const float s0[4] = {255.f, 255.f, 255.f, 1.f}, s1[4] = {cr, cg, cb, a1}, s2[4] = {cr, cg, cb, a2};
        for (int k = 0; k < 4; k++) st[0][k] = s0[k], st[1][k] = s1[k], st[2][k] = s2[k];
    } else {
        const float s0[4] = {58.f, 58.f, 54.f, 1.f}, s1[4] = {28.f, 28.f, 26.f, 1.f}, s2[4] = {11.f, 11.f, 10.f, 1.f};
        for (int k = 0; k < 4; k++) st[0][k] = s0[k], st[1][k] = s1[k], st[2][k] = s2[k];
    }
    const float spill = lit ? 0.35f * clamp01(on) : 0.f, R3 = 3.f * r;
    const float fx = x - 0.35f * r, fy = y - 0.4f * r, ddx = x - fx, ddy = y - fy;
    const float A = ddx * ddx + ddy * ddy - r * r;   /* < 0: the focus lies inside the end circle */
    const float rr = r + 1.5f;
    const uint16_t RING = omni_pack565(184, 200, 176);
    for (int py = y0; py < y1; py++) {
        uint16_t *d = fk_px_at(fb, stride, clip, x0, py);
        const float Y = (float)py + 0.5f;
        for (int px = x0; px < x1; px++, d++) {
            const float X = (float)px + 0.5f, dx = X - x, dy = Y - y;
            const float dist = sqrtf(dx * dx + dy * dy);
            if (spill > 0 && dist < R3) {
                const int a8 = (int)(spill * (1.f - dist / R3) * 255.f + 0.5f);
                if (a8 > 0) *d = omni_blend565(*d, rgb, a8);
            }
            const float cov = clamp01(r + 0.5f - dist);
            if (cov > 0) {
                const float qx = X - fx, qy = Y - fy, qd = qx * ddx + qy * ddy, qq = qx * qx + qy * qy;
                const float t = clamp01((qd - sqrtf(qd * qd - A * qq)) / A);
                const float *p0 = t < 0.45f ? st[0] : st[1], *p1 = t < 0.45f ? st[1] : st[2];
                const float u = t < 0.45f ? t / 0.45f : (t - 0.45f) / 0.55f;
                float c[4];
                for (int k = 0; k < 4; k++) c[k] = p0[k] + (p1[k] - p0[k]) * u;
                const int a8 = (int)(c[3] * cov * 255.f + 0.5f);
                if (a8 > 0)
                    *d = omni_blend565(*d, omni_pack565((int)(c[0] + 0.5f), (int)(c[1] + 0.5f), (int)(c[2] + 0.5f)), a8);
            }
            const float rc = clamp01(1.f - fabsf(dist - rr));
            if (rc > 0) *d = omni_blend565(*d, RING, (int)(0.45f * rc * 255.f + 0.5f));
        }
    }
}

/* meterNeedle(x, y, 36, v, rgb) for meter i (DW_P9_XY): the pivot at y + 18, the needle 32 px long at
 * -145 + 110 clamp(v, -.03, 1.03) degrees (canvas angles, clockwise from 3 o'clock), its 3 px shadow
 * (rgba(0, 0, 0, .7)) under the 1.5 px needle (butt ends), then the pivot cover: the face's disc (r 32) below
 * y + 15 filled #070706 and its 1 px edge line (rgba(184, 200, 176, .45)) at y + 15 */
void dw_p9_needle(uint16_t *fb, int stride, const omni_rect_t *clip, int i, float v, uint16_t rgb)
{
    if (i < 0 || i >= 6) return;
    const float x = DW_P9_XY[i][0], y = DW_P9_XY[i][1], r = 36.f, py = y + r * 0.5f, len = r - 4.f;
    v = v < -0.03f ? -0.03f : v > 1.03f ? 1.03f : v;
    float s, c;
    fk_sincos((-145.f + 110.f * v) * (TWO_PI / 360.f), &s, &c);
    const float ex = x + c * len, ey = py + s * len;
    fk_line(fb, stride, clip, x, py, ex, ey, 3.f, 0, 179);
    fk_line(fb, stride, clip, x, py, ex, ey, 1.5f, rgb, 255);
    const float cr = r - 4.f, yt = py - 3.f;
    const omni_rect_t bb = {fk_to16(floorf(x - cr) - 1.f), fk_to16(floorf(yt - 0.5f) - 1.f), fk_to16(ceilf(x + cr) + 2.f),
                            fk_to16(ceilf(y + cr) + 2.f)};
    int x0, y0, x1, y1;
    if (!fk_span(clip, &bb, &x0, &y0, &x1, &y1)) return;
    const uint16_t COVER = omni_pack565(7, 7, 6), EDGE = omni_pack565(184, 200, 176);
    for (int row = y0; row < y1; row++) {
        const float top = (float)row, Y = top + 0.5f;
        const float fill = clamp01(top + 1.f - yt);   /* the rect from yt down */
        const float lo = top > yt - 0.5f ? top : yt - 0.5f, hi = top + 1.f < yt + 0.5f ? top + 1.f : yt + 0.5f;
        const float line = clamp01(hi - lo);          /* the 1 px line about yt */
        if (fill <= 0 && line <= 0) continue;
        uint16_t *d = fk_px_at(fb, stride, clip, x0, row);
        for (int col = x0; col < x1; col++, d++) {
            const float dx = (float)col + 0.5f - x, dy = Y - y;
            const float dc = clamp01(cr + 0.5f - sqrtf(dx * dx + dy * dy));
            if (dc <= 0) continue;
            if (fill > 0) *d = omni_blend565(*d, COVER, (int)(fill * dc * 255.f + 0.5f));
            if (line > 0) *d = omni_blend565(*d, EDGE, (int)(0.45f * line * dc * 255.f + 0.5f));
        }
    }
}

/* ---- #6's roundel (wave 2 Task 10, WD27; faces-classic.js roundelSprite as two sprites): the unlit well, then the lit
 * sprite over it at the level. clip NULL: the whole of fb (stride W wide, as the other shapes take it) */
void dw_wall_roundel(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_img_t *unlit, const fk_img_t *lit,
                     float x, float y, uint8_t level)
{
    const omni_rect_t c = clip ? *clip : (omni_rect_t){0, 0, 466, 466};
    const int x0 = (int)floorf(x - 25.5f), y0 = (int)floorf(y - 25.5f);
    if (unlit) fk_blit(fb, stride, c, unlit, x0, y0, 255);
    if (lit && level) fk_blit(fb, stride, c, lit, x0, y0, level);
}
