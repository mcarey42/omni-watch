#include "omni/tmr_ring.h"
#include "omni/blend565.h"
#include "omni/raster.h"
#include <math.h>
#include <string.h>

#define TAU 6.28318530717958647692
#define CTR 219.0f                 /* the ring centre in box px */
#define R0 176.0f
#define R1 214.0f
#define ZONE_R0 160                /* the LUT zone */
#define ZONE_R1 220
#define NRQ ((ZONE_R1 - ZONE_R0) * 4 + 1)   /* radius steps of 1/4 px */
#define TRACK_R (R0 - 10.0f)       /* the stopwatch track and bead */
#define BEAD_R 5.0f

#define RGB(r, g, b) omni_pack565((r), (g), (b))
#define SCALE_BG RGB(11, 10, 8)    /* #0b0a08 */
#define BRASS_HI RGB(236, 210, 154) /* #ecd29a */
#define RED_HI RGB(255, 138, 122)  /* #ff8a7a */
#define INK RGB(236, 230, 220)
#define BRASS RGB(201, 163, 91)

static uint32_t zone_count(void)
{
    static uint32_t n;
    if (n) return n;
    uint32_t c = 0;
    for (int y = 0; y < OMNI_TMR_BOX; y++)
        for (int x = 0; x < OMNI_TMR_BOX; x++) {
            const double dx = x + 0.5 - CTR, dy = y + 0.5 - CTR;
            const double r = sqrt(dx * dx + dy * dy);
            if (r >= ZONE_R0 && r <= ZONE_R1) c++;
        }
    return n = c;
}

size_t omni_tmr_lut_bytes(void) { return (size_t)zone_count() * 7u; }

bool omni_tmr_lut_build(omni_tmr_lut_t *l, void *mem, size_t n)
{
    const uint32_t cnt = zone_count();
    if (!l || !mem || ((uintptr_t)mem & 3u) || n < (size_t)cnt * 7u) return false;
    uint32_t *idx = (uint32_t *)mem;
    uint16_t *ang = (uint16_t *)(void *)(idx + cnt);
    uint8_t *rq = (uint8_t *)(ang + cnt);
    uint32_t k = 0;
    for (int y = 0; y < OMNI_TMR_BOX; y++)
        for (int x = 0; x < OMNI_TMR_BOX; x++) {
            const double dx = x + 0.5 - CTR, dy = y + 0.5 - CTR;
            const double r = sqrt(dx * dx + dy * dy);
            if (r < ZONE_R0 || r > ZONE_R1) continue;
            double a = atan2(dx, -dy);
            if (a < 0) a += TAU;
            long q = lround(a / TAU * 65536.0);
            idx[k] = (uint32_t)(y * OMNI_TMR_BOX + x);
            ang[k] = (uint16_t)(q & 0xffff);
            rq[k] = (uint8_t)lround((r - ZONE_R0) * 4.0);
            k++;
        }
    l->n = k;
    l->idx = idx;
    l->ang = ang;
    l->rq = rq;
    return true;
}

/* ---- the LUT file (R139) */
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

size_t omni_tmr_lut_file_bytes(const omni_tmr_lut_t *l) { return l ? OMNI_TMR_LUT_HDR + (size_t)l->n * 7u : 0; }

size_t omni_tmr_lut_file_write(const omni_tmr_lut_t *l, uint8_t *out, size_t cap)
{
    const size_t fb = omni_tmr_lut_file_bytes(l);
    if (!l || !out || cap < fb) return 0;
    memcpy(out, "OTL1", 4);
    put16(out + 4, OMNI_TMR_BOX);
    out[6] = ZONE_R0;
    out[7] = ZONE_R1;
    put32(out + 8, l->n);
    put32(out + 12, 0);
    uint8_t *p = out + OMNI_TMR_LUT_HDR;
    for (uint32_t i = 0; i < l->n; i++, p += 4) put32(p, l->idx[i]);
    for (uint32_t i = 0; i < l->n; i++, p += 2) put16(p, l->ang[i]);
    memcpy(p, l->rq, l->n);
    return fb;
}

bool omni_tmr_lut_load(omni_tmr_lut_t *l, const void *file, size_t size)
{
    const uint8_t *f = (const uint8_t *)file;
    if (!l || !f || size < OMNI_TMR_LUT_HDR || ((uintptr_t)f & 3u) || memcmp(f, "OTL1", 4) != 0 ||
        get16(f + 4) != OMNI_TMR_BOX || f[6] != ZONE_R0 || f[7] != ZONE_R1)
        return false;
    const uint32_t n = get32(f + 8);
    if (n == 0 || n > (uint32_t)OMNI_TMR_BOX * OMNI_TMR_BOX || size != OMNI_TMR_LUT_HDR + (size_t)n * 7u) return false;
    const uint32_t *idx = (const uint32_t *)(const void *)(f + OMNI_TMR_LUT_HDR);
    const uint16_t *ang = (const uint16_t *)(const void *)(idx + n);
    const uint8_t *rq = (const uint8_t *)(ang + n);
    for (uint32_t i = 0; i < n; i++)
        if ((i && idx[i] <= idx[i - 1]) || rq[i] >= NRQ) return false;
    if (idx[n - 1] >= (uint32_t)OMNI_TMR_BOX * OMNI_TMR_BOX) return false;
    l->n = n;
    l->idx = idx;
    l->ang = ang;
    l->rq = rq;
    return true;
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float cmin(float m) { return !(m > 0) ? 0 : m > 60 ? 60 : m; }

/* The gradient's colour at each radius step (the mockup's createRadialGradient R0..R1, alpha applied at the blend). */
static void gradient(bool red, uint16_t *out)
{
    for (int i = 0; i < NRQ; i++) {
        const float r = ZONE_R0 + (float)i * 0.25f;
        const float t = clamp01((r - R0) / (R1 - R0));
        float c[3];
        static const float b0[3] = { 120, 92, 40 }, b1[3] = { 201, 163, 91 }, b2[3] = { 150, 118, 60 };
        static const float r0[3] = { 160, 40, 30 }, r1[3] = { 216, 68, 58 };
        for (int k = 0; k < 3; k++) {
            if (red) c[k] = r0[k] + (r1[k] - r0[k]) * t;
            else if (t < 0.6f) c[k] = b0[k] + (b1[k] - b0[k]) * (t / 0.6f);
            else c[k] = b1[k] + (b2[k] - b1[k]) * ((t - 0.6f) / 0.4f);
        }
        out[i] = RGB((int)(c[0] + 0.5f), (int)(c[1] + 0.5f), (int)(c[2] + 0.5f));
    }
}

/* Coverage (0..1) of the half-plane "before angle a" for a pixel at angle th, radius r: 1 px ramp across the edge. */
static float before(float th, float a, float r) { return clamp01((a - th) * r + 0.5f); }

/* Coverage of the sweep from 12 o'clock clockwise to angle a (0..2pi) for a pixel at angle th (0..2pi), radius r: the
 * end edge's ramp, cut by the start edge's ramp on the 3 o'clock side of 12. Past 6 o'clock (th > pi) the pixel is
 * wholly after the start: the start ramp is only for the pixels just clockwise of 12. (Until the 30 min fix the start
 * term was taken from th - 2pi there, which is 0, so every sweep stopped at 6 o'clock: the sector at 30 min and the
 * stopwatch track at 30 s.) */
static float sweep(float th, float a, float r)
{
    const float after_start = th > (float)(TAU / 2) ? 1.0f : clamp01(th * r + 0.5f);
    return clamp01(before(th, a, r) + after_start - 1.0f);
}

static uint8_t to255(float c) { return (uint8_t)(clamp01(c) * 255.0f + 0.5f); }

static void tick_quad(omni_pt_t q[4], float ang, float ra, float rb, float w)
{
    const float s = sinf(ang), c = cosf(ang);          /* radial (s, -c); perpendicular (c, s) */
    const float hw = w * 0.5f;
    q[0] = (omni_pt_t){ CTR + s * ra - c * hw, CTR - c * ra - s * hw };
    q[1] = (omni_pt_t){ CTR + s * rb - c * hw, CTR - c * rb - s * hw };
    q[2] = (omni_pt_t){ CTR + s * rb + c * hw, CTR - c * rb + s * hw };
    q[3] = (omni_pt_t){ CTR + s * ra + c * hw, CTR - c * ra + s * hw };
}

void omni_tmr_ring_draw(const omni_tmr_lut_t *l, const omni_tmr_ring_t *s, uint16_t *box, int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > OMNI_TMR_BOX) y1 = OMNI_TMR_BOX;
    if (!l || !s || !box || y1 <= y0) return;

    const float mins = cmin(s->min);
    const bool full = mins >= 60.0f, have_sector = mins > 0;
    const float a1 = mins / 60.0f * (float)TAU;
    const bool have_bead = s->bead >= 0;
    const float bead = have_bead ? (s->bead > 1 ? 1 : s->bead) : 0;
    const float ab = bead * (float)TAU, bx = CTR + sinf(ab) * TRACK_R, by = CTR - cosf(ab) * TRACK_R;
    uint16_t grad[NRQ];
    gradient(s->red, grad);

    /* the LUT is in box order: the band is a contiguous run, found by bisection */
    uint32_t lo = 0, hi = l->n;
    const uint32_t first = (uint32_t)y0 * OMNI_TMR_BOX, last = (uint32_t)y1 * OMNI_TMR_BOX;
    while (lo < hi) { const uint32_t m = (lo + hi) / 2; if (l->idx[m] < first) lo = m + 1; else hi = m; }
    const uint32_t i0 = lo;
    hi = l->n;
    while (lo < hi) { const uint32_t m = (lo + hi) / 2; if (l->idx[m] < last) lo = m + 1; else hi = m; }
    const uint32_t i1 = lo;

    /* pass 1: scale ring, sector, stopwatch track and bead, per LUT pixel (the ring's contours built once) */
    omni_ring_t scale;
    omni_ring_prepare(&scale, CTR, CTR, R0, R1);
    uint32_t i = i0;
    while (i < i1) {
        const int y = (int)(l->idx[i] / OMNI_TMR_BOX), xa = (int)(l->idx[i] % OMNI_TMR_BOX);
        uint32_t j = i + 1;
        while (j < i1 && l->idx[j] == l->idx[j - 1] + 1 && (int)(l->idx[j] / OMNI_TMR_BOX) == y && (int)(j - i) < OMNI_COVER_W) j++;
        uint8_t cov[OMNI_COVER_W];
        omni_cover_ring_prepared(&scale, y, xa, xa + (int)(j - i), cov);
        for (uint32_t k = i; k < j; k++) {
            const int rq = l->rq[k];
            const float r = (float)ZONE_R0 + (float)rq * 0.25f;
            const float th = (float)l->ang[k] * (float)(TAU / 65536.0);
            uint16_t px = 0;
            const uint8_t cr = cov[k - i];
            if (cr) {
                px = omni_blend565(px, SCALE_BG, cr);
                if (have_sector) {
                    const float ca = full ? 1.0f : sweep(th, a1, r);
                    const int sa = (int)((float)s->alpha * (float)cr / 255.0f * ca + 0.5f);
                    if (sa > 0) px = omni_blend565(px, grad[rq], (int)(sa > 255 ? 255 : sa));
                }
            }
            if (have_bead) {
                const float dr = r - TRACK_R, ad = dr < 0 ? -dr : dr;
                const float cr2 = clamp01(1.5f - ad);   /* 2 px wide */
                if (cr2 > 0 && bead > 0) {
                    const float c = sweep(th, ab, r) * cr2;
                    const uint8_t a = to255(c * 0.55f);
                    if (a) px = omni_blend565(px, BRASS, a);
                }
                const int idx = (int)l->idx[k], x = idx % OMNI_TMR_BOX, yy = idx / OMNI_TMR_BOX;
                const float ddx = (float)x + 0.5f - bx, ddy = (float)yy + 0.5f - by;
                const float cb = clamp01(BEAD_R + 0.5f - sqrtf(ddx * ddx + ddy * ddy));
                if (cb > 0) px = omni_blend565(px, BRASS_HI, to255(cb));
            }
            box[l->idx[k]] = px;
        }
        i = j;
    }

    /* pass 2: the shapes, over the band */
    const omni_rect_t clip = { 0, (int16_t)y0, OMNI_TMR_BOX, (int16_t)y1 };
    uint16_t *fb = box + (size_t)y0 * OMNI_TMR_BOX;
    omni_pt_t q[4];
    if (have_sector) {
        tick_quad(q, a1, R0 - 4.0f, R1 + 4.0f, 2.0f);
        omni_fill_poly(fb, OMNI_TMR_BOX, &clip, q, 4, s->red ? RED_HI : BRASS_HI, 255);
    }
    for (int m = 0; m < 60; m++) {
        const bool five = m % 5 == 0;
        tick_quad(q, (float)m / 60.0f * (float)TAU, five ? R1 - 12.0f : R1 - 7.0f, R1 - 2.0f, five ? 1.6f : 1.0f);
        float ymin = q[0].y, ymax = q[0].y;
        for (int k = 1; k < 4; k++) { if (q[k].y < ymin) ymin = q[k].y; if (q[k].y > ymax) ymax = q[k].y; }
        if (ymax + 2 < (float)y0 || ymin - 2 > (float)y1) continue;
        omni_fill_poly(fb, OMNI_TMR_BOX, &clip, q, 4, INK, five ? 191 : 77);
    }
    omni_fill_ring(fb, OMNI_TMR_BOX, &clip, CTR, CTR, R1, R1 + 1.0f, BRASS, 128);
    omni_fill_ring(fb, OMNI_TMR_BOX, &clip, CTR, CTR, R0 - 1.0f, R0, BRASS, 128);
}

/* ---- dirty rect */
typedef struct { float x0, y0, x1, y1; } fbox_t;

static void fb_add(fbox_t *b, float x, float y)
{
    if (x < b->x0) b->x0 = x;
    if (x > b->x1) b->x1 = x;
    if (y < b->y0) b->y0 = y;
    if (y > b->y1) b->y1 = y;
}

/* grow b by the zone's wedge from angle lo to hi (rad) between radii ra and rb, widened by `pad` px */
static void wedge(fbox_t *b, float lo, float hi, float ra, float rb, float pad)
{
    const float dpa = pad / ra;
    lo -= dpa;
    hi += dpa;
    const float rr[2] = { ra, rb }, aa[2] = { lo, hi };
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) fb_add(b, CTR + rr[i] * sinf(aa[j]), CTR - rr[i] * cosf(aa[j]));
    /* the arc's extremes at the axes */
    for (int q = (int)floorf(lo / ((float)TAU / 4)); q <= (int)ceilf(hi / ((float)TAU / 4)); q++) {
        const float a = (float)q * ((float)TAU / 4);
        if (a < lo || a > hi) continue;
        fb_add(b, CTR + rb * sinf(a), CTR - rb * cosf(a));
    }
    b->x0 -= pad; b->y0 -= pad; b->x1 += pad; b->y1 += pad;
}

static void disc(fbox_t *b, float ang)
{
    const float x = CTR + sinf(ang) * TRACK_R, y = CTR - cosf(ang) * TRACK_R;
    fb_add(b, x - BEAD_R - 2, y - BEAD_R - 2);
    fb_add(b, x + BEAD_R + 2, y + BEAD_R + 2);
}

void omni_tmr_ring_dirty(const omni_tmr_ring_t *a, const omni_tmr_ring_t *b, int rect[4])
{
    fbox_t bb = { 1e9f, 1e9f, -1e9f, -1e9f };
    const float ma = cmin(a->min), mb = cmin(b->min);
    const float aa = ma / 60.0f * (float)TAU, ab = mb / 60.0f * (float)TAU;
    if (ma > 0 || mb > 0) {
        if (a->red != b->red || a->alpha != b->alpha) {
            /* the whole sector of whichever is larger */
            wedge(&bb, 0, aa > ab ? aa : ab, ZONE_R0, ZONE_R1, 3.0f);
        } else if (ma != mb) {
            wedge(&bb, aa < ab ? aa : ab, aa > ab ? aa : ab, ZONE_R0, ZONE_R1, 3.0f);
        }
        /* the 12 o'clock end of a sector that appears or goes is inside the wedge from 0 */
        if ((ma > 0) != (mb > 0)) wedge(&bb, 0, aa > ab ? aa : ab, ZONE_R0, ZONE_R1, 3.0f);
    }
    const bool ha = a->bead >= 0, hb = b->bead >= 0;
    if ((ha || hb) && (ha != hb || a->bead != b->bead)) {
        const float ba = ha ? (a->bead > 1 ? 1 : a->bead) * (float)TAU : 0, bbb = hb ? (b->bead > 1 ? 1 : b->bead) * (float)TAU : 0;
        const float hi = ba > bbb ? ba : bbb, lo = (ha && hb) ? (ba < bbb ? ba : bbb) : 0;
        wedge(&bb, lo, hi, TRACK_R - 2.0f, TRACK_R + 2.0f, 3.0f);
        if (ha) disc(&bb, ba);
        if (hb) disc(&bb, bbb);
    }
    if (bb.x1 < bb.x0) {
        rect[0] = 0; rect[1] = 0; rect[2] = -1; rect[3] = -1;
        return;
    }
    const int lim = OMNI_TMR_BOX - 1;
    int x0 = (int)floorf(bb.x0), y0 = (int)floorf(bb.y0), x1 = (int)ceilf(bb.x1), y1 = (int)ceilf(bb.y1);
    rect[0] = x0 < 0 ? 0 : x0;
    rect[1] = y0 < 0 ? 0 : y0;
    rect[2] = x1 > lim ? lim : x1;
    rect[3] = y1 > lim ? lim : y1;
}

/* ---- the DONE pulse */
static uint32_t count_runs(const omni_tmr_lut_t *l)
{
    uint32_t r = 0;
    for (uint32_t k = 0; k < l->n; k++)
        if (k == 0 || l->idx[k] != l->idx[k - 1] + 1) r++;
    return r;
}

size_t omni_tmr_pulse_bytes(const omni_tmr_lut_t *l)
{
    return l && l->n ? (size_t)count_runs(l) * 8u + (size_t)l->n * 4u : 0;
}

bool omni_tmr_pulse_build(omni_tmr_pulse_t *p, const omni_tmr_lut_t *l, const omni_tmr_ring_t *s, uint16_t *box,
                          void *mem, size_t n)
{
    if (!p || !l || !l->n || !s || !box || !mem || ((uintptr_t)mem & 3u) || n < omni_tmr_pulse_bytes(l)) return false;
    uint32_t *run = mem, r = 0;
    for (uint32_t k = 0; k < l->n; k++) {
        if (k == 0 || l->idx[k] != l->idx[k - 1] + 1) {
            run[2 * r] = l->idx[k];
            run[2 * r + 1] = 0;
            r++;
        }
        run[2 * r - 1]++;
    }
    uint16_t *lo = (uint16_t *)(run + 2 * r), *hi = lo + l->n;
    omni_tmr_ring_t t = *s;
    for (int layer = 0; layer < 2; layer++) {
        t.alpha = layer ? 255 : 0;
        omni_tmr_ring_draw(l, &t, box, 0, OMNI_TMR_BOX);
        uint16_t *out = layer ? hi : lo;
        for (uint32_t k = 0; k < l->n; k++) out[k] = box[l->idx[k]];
    }
    *p = (omni_tmr_pulse_t){ l->n, r, run, lo, hi };
    return true;
}

void omni_tmr_pulse_draw(const omni_tmr_pulse_t *p, uint8_t alpha, uint16_t *box)
{
    if (!p || !p->n || !box) return;
    uint32_t k = 0;
    for (uint32_t r = 0; r < p->runs; r++) {
        uint16_t *d = box + p->run[2 * r];
        for (uint32_t i = 0, len = p->run[2 * r + 1]; i < len; i++, k++) d[i] = omni_blend565(p->lo[k], p->hi[k], alpha);
    }
}
