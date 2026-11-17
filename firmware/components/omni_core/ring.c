#include "omni/ring.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "omni/blend565.h"
#include "omni/globe_check.h"   /* OMNI_KCHECK_GUARD, omni_fnv1a64 */
#include "omni/trig.h"

/* The Omni date rings: polar map, strips and span kernels (ring.h). The map build is single precision in a fixed
 * order (-ffp-contract=off, CMakeLists.txt), the radius from exact squares and theta from the table atan2 (trig.h),
 * with pixels near a theta cell boundary settled by libm (atan2f, then double), so host and device build the same
 * map. */

const omni_ring_geom_t OMNI_RING_GEOM[OMNI_RINGS] = {
    { 126, 143, 12 }, { 143, 160, 32 }, { 160, 178, 100 }, { 178, 197, 42 },
};

#define TWO_PI_F 6.28318530717958647692f
#define TWO_PI_D 6.28318530717958647692
#define MAX_SPAN 512        /* a span lies in one row of the rect: the screen is 466 wide */
#define TH_NEAR 1e-3f       /* theta * N / 2pi within this of an integer: settled with double atan2 */

static bool pow2(uint32_t n) { return n >= 4 && (n & (n - 1)) == 0; }

static int ring_at(float r)
{
    for (int i = 0; i < OMNI_RINGS; i++)
        if (r >= OMNI_RING_GEOM[i].r0 && r < OMNI_RING_GEOM[i].r1) return i;
    return -1;
}

/* The pixel's centre inside the rounded rect (radius rr; 0: the plain rect). */
static inline __attribute__((always_inline)) bool in_clip(const omni_pmap_cfg_t *c, int x, int y)
{
    if (!(c->rr > 0)) return true;
    const float px = (float)x + 0.5f, py = (float)y + 0.5f;
    const float ax = (float)c->rect.x0 + c->rr, bx = (float)c->rect.x1 - c->rr;
    const float ay = (float)c->rect.y0 + c->rr, by = (float)c->rect.y1 - c->rr;
    const float dx = px < ax ? ax - px : px > bx ? px - bx : 0.0f;
    const float dy = py < ay ? ay - py : py > by ? py - by : 0.0f;
    return dx * dx + dy * dy <= c->rr * c->rr;
}

/* Classification without per-pixel sqrtf (98 cycles on the S3), floorf (72) or a scan of the whole rect: a row meets
 * each boundary circle in at most two points, so its ring runs come from the circles' intersections (one sqrtf a
 * circle a row, each end then settled with the exact predicate). r^2 = X^2 + Y^2 is exact for the dial map (half-pixel
 * centres, |X|, |Y| <= 233), so ring membership is a compare against an exact square, and ri = floor(r) - r_min
 * exactly, tracked along a run from its first pixel (r moves at most 1 px a step). */
typedef struct {
    const omni_pmap_cfg_t *c;
    float ik, r2lo, r2hi, r02[OMNI_RINGS], r12[OMNI_RINGS];
} pclass_t;

static float sq(float v) { return v * v; }

static void pclass_init(pclass_t *p, const omni_pmap_cfg_t *c)
{
    p->c = c;
    p->ik = 1.0f / c->k;   /* 1 exactly for the dial map */
    p->r2lo = sq((float)c->r_min);
    p->r2hi = sq((float)c->r_min + (float)c->n_r);
    for (int i = 0; i < OMNI_RINGS; i++) {
        p->r02[i] = sq(OMNI_RING_GEOM[i].r0);
        p->r12[i] = sq(OMNI_RING_GEOM[i].r1);
    }
}

/* The source point of pixel x on the row whose source Y is given (X right of the dial centre). */
static inline float src_x(const pclass_t *p, int x) { return p->c->sx + ((float)x + 0.5f - p->c->dx) * p->ik - 233.0f; }
static inline float src_y(const pclass_t *p, int y) { return 233.0f - (p->c->sy + ((float)y + 0.5f - p->c->dy) * p->ik); }
static inline bool inside(const pclass_t *p, int x, float Y, float R2) { const float X = src_x(p, x); return X * X + Y * Y < R2; }

/* [*a, *b): the pixels of the row (source Y) with r^2 < R2, within [x0, x1) (r^2 is convex along the row). */
static void disc_run(const pclass_t *p, float Y, float R2, int x0, int x1, int *a, int *b)
{
    *a = *b = x0;
    if (x0 >= x1 || !(Y * Y < R2)) return;
    const omni_pmap_cfg_t *c = p->c;
    const float xc = c->dx - 0.5f + (233.0f - c->sx) * c->k, h = sqrtf(R2 - Y * Y) * c->k;   /* X = 0 at xc */
    int lo = (int)floorf(xc - h), hi = (int)floorf(xc + h);
    if (lo < x0) lo = x0;
    if (hi > x1 - 1) hi = x1 - 1;
    if (lo > hi) {   /* the estimate missed the rect: the run is empty unless an end pixel is inside */
        if (hi < x0 || lo > x1 - 1) return;
    }
    while (lo > x0 && inside(p, lo - 1, Y, R2)) lo--;
    while (lo <= hi && !inside(p, lo, Y, R2)) lo++;
    while (hi < x1 - 1 && inside(p, hi + 1, Y, R2)) hi++;
    while (hi >= lo && !inside(p, hi, Y, R2)) hi--;
    if (lo <= hi) {
        *a = lo;
        *b = hi + 1;
    }
}

/* A row's runs of one ring each (at most two a ring), in x order. Returns the count. */
typedef struct { int16_t x0, x1; uint8_t ring; } prun_t;

static int row_runs(const pclass_t *p, int y, prun_t run[2 * OMNI_RINGS])
{
    const omni_pmap_cfg_t *c = p->c;
    int x0 = c->rect.x0, x1 = c->rect.x1;
    if (c->rr > 0) {   /* the rounded rect's row: an interval too */
        while (x0 < x1 && !in_clip(c, x0, y)) x0++;
        while (x1 > x0 && !in_clip(c, x1 - 1, y)) x1--;
    }
    const float Y = src_y(p, y);
    int n = 0;
    for (int i = 0; i < OMNI_RINGS; i++) {
        const float in2 = p->r02[i] > p->r2lo ? p->r02[i] : p->r2lo, out2 = p->r12[i] < p->r2hi ? p->r12[i] : p->r2hi;
        if (!(in2 < out2)) continue;
        int a1, b1, a0, b0;
        disc_run(p, Y, out2, x0, x1, &a1, &b1);
        if (a1 >= b1) continue;
        disc_run(p, Y, in2, x0, x1, &a0, &b0);
        if (a0 >= b0) {
            run[n++] = (prun_t){ (int16_t)a1, (int16_t)b1, (uint8_t)i };
            continue;
        }
        if (a1 < a0) run[n++] = (prun_t){ (int16_t)a1, (int16_t)a0, (uint8_t)i };
        if (b0 < b1) run[n++] = (prun_t){ (int16_t)b0, (int16_t)b1, (uint8_t)i };
    }
    for (int i = 1; i < n; i++)   /* insertion sort by x0 */
        for (int j = i; j > 0 && run[j].x0 < run[j - 1].x0; j--) {
            const prun_t t = run[j];
            run[j] = run[j - 1];
            run[j - 1] = t;
        }
    return n;
}

/* floor(r) - r_min for the pixel at r^2 = r2, from a hint (the previous pixel's; -1: none). */
static int radius_row(const pclass_t *p, float r2, int h)
{
    const omni_pmap_cfg_t *c = p->c;
    if (h < 0) h = (int)sqrtf(r2) - c->r_min;
    if (h < 0) h = 0;
    if (h > c->n_r - 1) h = c->n_r - 1;
    const float r0 = (float)c->r_min;
    while (h + 1 < c->n_r && sq(r0 + (float)(h + 1)) <= r2) h++;
    while (h > 0 && sq(r0 + (float)h) > r2) h--;
    return h;
}

/* theta index floor(theta / 2pi * n): the table atan2 (trig.h, within 0.008 entries at N 4096); a pixel within
 * TH_TAB entries of a cell edge is redone with atan2f, and within TH_NEAR of one with double atan2, so the index is
 * exactly the double formula's (test_pmap_matches_atan2). */
#define TH_TAB 0.03f
static uint16_t theta_index(float X, float Y, uint32_t n, float k)   /* k = n / 2pi (float divide is soft) */
{
    float t = omni_atan2_tab(X, Y);
    if (t < 0) t += TWO_PI_F;
    float v = t * k;
    long i = (long)v;   /* v >= 0 */
    float fr = v - (float)i;
    if (fr < TH_TAB || fr > 1.0f - TH_TAB) {
        t = atan2f(X, Y);
        if (t < 0) t += TWO_PI_F;
        v = t * k;
        i = (long)v;
        fr = v - (float)i;
        if (fr < TH_NEAR || fr > 1.0f - TH_NEAR) {   /* on a cell edge: double settles it (about 0.5 % of pixels) */
            double td = atan2((double)X, (double)Y);
            if (td < 0) td += TWO_PI_D;
            i = (long)floor(td * n / TWO_PI_D);
        }
    }
    if (i < 0) i = 0;
    if (i >= (long)n) i = (long)n - 1;
    return (uint16_t)i;
}

/* The spans and pixels of the map (runs split at MAX_SPAN). */
typedef struct { uint32_t nspans, npx; } pcount_t;

static pcount_t count_spans(const omni_pmap_cfg_t *c)
{
    pcount_t k = { 0, 0 };
    pclass_t pc;
    pclass_init(&pc, c);
    prun_t run[2 * OMNI_RINGS];
    for (int y = c->rect.y0; y < c->rect.y1; y++) {
        const int n = row_runs(&pc, y, run);
        for (int i = 0; i < n; i++) {
            const int len = run[i].x1 - run[i].x0;
            k.npx += (uint32_t)len;
            k.nspans += (uint32_t)((len + MAX_SPAN - 1) / MAX_SPAN);
        }
    }
    return k;
}

static size_t align4(size_t n) { return (n + 3) & ~(size_t)3; }

static size_t layout(const pcount_t *k, bool lut, bool codes, size_t *o_th, size_t *o_ri, size_t *o_codes)
{
    size_t o = align4(k->nspans * sizeof(omni_pspan_t));
    *o_th = o;
    if (lut) o += (size_t)k->npx * 2;
    *o_ri = o;
    if (lut) o += k->npx;
    *o_codes = o;
    if (codes) o += (k->npx - k->nspans + 1) / 2;
    return o + 4;   /* alignment slack for mem */
}

size_t omni_pmap_bytes(const omni_pmap_cfg_t *cfg, bool lut, bool codes)
{
    const pcount_t k = count_spans(cfg);
    size_t a, b, c;
    return layout(&k, lut, codes, &a, &b, &c);
}

/* Settles a span's direction and base from its theta/radius indices; false if no code fits (or theta turns back). */
static bool span_codes_fit(const uint16_t *th, const uint8_t *ri, int n, uint32_t N, uint8_t *dir, uint8_t *base)
{
    int sgn = 0;
    uint32_t lo = UINT32_MAX, hi = 0;
    for (int i = 1; i < n; i++) {
        int32_t d = (int32_t)((th[i] - th[i - 1] + N / 2) & (N - 1)) - (int32_t)(N / 2);
        const int dr = (int)ri[i] - (int)ri[i - 1];
        if (dr < -1 || dr > 1) return false;
        if (d < 0) {
            if (sgn > 0) return false;
            sgn = -1;
            d = -d;
        } else if (d > 0) {
            if (sgn < 0) return false;
            sgn = 1;
        }
        if ((uint32_t)d < lo) lo = (uint32_t)d;
        if ((uint32_t)d > hi) hi = (uint32_t)d;
    }
    *dir = sgn < 0;
    if (n < 2) {
        *base = 0;
        return true;
    }
    const uint32_t b = hi > 3 ? hi - 3 : 0;   /* the smallest base that holds the largest step */
    if (b > lo || b > 255) return false;
    *base = (uint8_t)b;
    return true;
}

bool omni_pmap_build(omni_pmap_t *m, const omni_pmap_cfg_t *cfg, bool lut, bool codes, void *mem, size_t bytes)
{
    memset(m, 0, sizeof *m);
    if (!cfg || !mem || !pow2(cfg->n_theta) || !(cfg->k > 0)) return false;
    const pcount_t k = count_spans(cfg);
    size_t o_th, o_ri, o_codes;
    if (layout(&k, lut, codes, &o_th, &o_ri, &o_codes) > bytes) return false;
    uint8_t *base = (uint8_t *)mem + ((4 - ((uintptr_t)mem & 3)) & 3);
    m->cfg = *cfg;
    m->span = (omni_pspan_t *)base;
    m->th = lut ? (uint16_t *)(base + o_th) : NULL;
    m->ri = lut ? base + o_ri : NULL;
    m->codes = codes ? base + o_codes : NULL;
    if (codes) memset(m->codes, 0, (k.npx - k.nspans + 1) / 2);
    const uint32_t N = cfg->n_theta;
    const float kth = (float)N / TWO_PI_F;
    uint16_t th[MAX_SPAN];
    uint8_t ri[MAX_SPAN];
    uint32_t si = 0, px = 0, nib = 0;
    pclass_t pc;
    pclass_init(&pc, cfg);
    prun_t run[2 * OMNI_RINGS];
    for (int y = cfg->rect.y0; y < cfg->rect.y1; y++) {
        const int nr = row_runs(&pc, y, run);
        const float Y = src_y(&pc, y);
        for (int j = 0; j < nr; j++)
            for (int x0 = run[j].x0; x0 < run[j].x1; x0 += MAX_SPAN) {
                /* one span: this ring's run from x0 */
                const int ring = run[j].ring;
                const int n = run[j].x1 - x0 < MAX_SPAN ? run[j].x1 - x0 : MAX_SPAN;
                int h = -1;
                for (int i = 0; i < n; i++) {
                    const float X = src_x(&pc, x0 + i);
                    h = radius_row(&pc, X * X + Y * Y, h);
                    th[i] = theta_index(X, Y, N, kth);
                    ri[i] = (uint8_t)h;
                }
                if (si >= k.nspans || px + (uint32_t)n > k.npx) return false;   /* cannot happen: count_spans agrees */
                omni_pspan_t *s = &m->span[si++];
                s->y = (int16_t)y;
                s->x0 = (int16_t)x0;
                s->n = (int16_t)n;
                s->ring = (uint8_t)ring;
                s->th0 = th[0];
                s->ri0 = ri[0];
                s->lut = px;
                s->code = nib;
                if (!span_codes_fit(th, ri, n, N, &s->dir, &s->base)) {
                    if (codes) return false;
                    s->dir = 0;
                    s->base = 0;
                }
                if (lut) {
                    memcpy(m->th + px, th, (size_t)n * 2);
                    memcpy(m->ri + px, ri, (size_t)n);
                }
                if (codes)
                    for (int i = 1; i < n; i++, nib++) {
                        const uint32_t d = (uint32_t)(s->dir ? th[i - 1] - th[i] : th[i] - th[i - 1]) & (N - 1);
                        const int c = (int)(d - s->base) * 3 + ((int)ri[i] - (int)ri[i - 1] + 1);
                        m->codes[nib >> 1] |= (uint8_t)(c << ((nib & 1) * 4));
                    }
                else
                    nib += (uint32_t)(n - 1);
                px += (uint32_t)n;
            }
    }
    m->nspans = si;
    m->npx = px;
    return si == k.nspans && px == k.npx;
}

uint32_t omni_pmap_base_spans(const omni_pmap_t *m)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->nspans; i++) n += m->span[i].base != 0;
    return n;
}

omni_pmap_cfg_t omni_dial_pmap_cfg(uint16_t n_theta)
{
    return (omni_pmap_cfg_t){ .rect = { 36, 36, 430, 430 }, .k = 1, .n_theta = n_theta, .r_min = 126, .n_r = 71 };
}

/* ---- The map file (R80) */

static size_t code_bytes(uint32_t npx, uint32_t nspans) { return (npx - nspans + 1) / 2; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void span_record(const omni_pspan_t *s, uint8_t r[OMNI_PMAP_SPAN_BYTES])
{
    wr16(r, (uint16_t)s->y);
    wr16(r + 2, (uint16_t)s->x0);
    wr16(r + 4, (uint16_t)s->n);
    r[6] = s->ring;
    r[7] = s->dir;
    wr16(r + 8, s->th0);
    r[10] = s->ri0;
    r[11] = s->base;
    wr32(r + 12, s->lut);
    wr32(r + 16, s->code);
}

static void cfg_record(const omni_pmap_cfg_t *c, uint8_t r[34])
{
    wr16(r, (uint16_t)c->rect.x0);
    wr16(r + 2, (uint16_t)c->rect.y0);
    wr16(r + 4, (uint16_t)c->rect.x1);
    wr16(r + 6, (uint16_t)c->rect.y1);
    const float f[6] = { c->dx, c->dy, c->sx, c->sy, c->k, c->rr };
    for (int i = 0; i < 6; i++) wr32(r + 8 + 4 * i, fbits(f[i]));
    r[32] = c->r_min;
    r[33] = c->n_r;
}

uint64_t omni_pmap_digest(const omni_pmap_t *m)
{
    if (!m->codes) return 0;
    uint64_t h = 14695981039346656037ull;
    uint8_t r[OMNI_PMAP_SPAN_BYTES];
    for (uint32_t i = 0; i < m->nspans; i++) {
        span_record(&m->span[i], r);
        h = omni_fnv1a64(h, r, sizeof r);
    }
    return omni_fnv1a64(h, m->codes, code_bytes(m->npx, m->nspans));
}

size_t omni_pmap_file_bytes(const omni_pmap_t *m)
{
    return m->codes ? OMNI_PMAP_HDR + (size_t)m->nspans * OMNI_PMAP_SPAN_BYTES + code_bytes(m->npx, m->nspans) : 0;
}

size_t omni_pmap_file_write(const omni_pmap_t *m, void *out, size_t cap)
{
    const size_t n = omni_pmap_file_bytes(m);
    if (!n || cap < n) return 0;
    uint8_t *p = out;
    memset(p, 0, OMNI_PMAP_HDR);
    memcpy(p, "OPM1", 4);
    wr16(p + 4, OMNI_PMAP_VERSION);
    wr16(p + 6, m->cfg.n_theta);
    wr32(p + 8, m->nspans);
    wr32(p + 12, m->npx);
    wr32(p + 16, (uint32_t)code_bytes(m->npx, m->nspans));
    wr32(p + 20, (uint32_t)n);
    const uint64_t h = omni_pmap_digest(m);
    wr32(p + 24, (uint32_t)h);
    wr32(p + 28, (uint32_t)(h >> 32));
    cfg_record(&m->cfg, p + 32);
    uint8_t *q = p + OMNI_PMAP_HDR;
    for (uint32_t i = 0; i < m->nspans; i++, q += OMNI_PMAP_SPAN_BYTES) span_record(&m->span[i], q);
    memcpy(q, m->codes, code_bytes(m->npx, m->nspans));
    return n;
}

/* A map is at most a screen of pixels in spans of at least one: these caps keep every size below in 32 bits. */
#define FILE_MAX_PX (1u << 20)

/* The header's counts if the file is the size they give; false otherwise. The sum is taken in 64 bits, so a crafted
 * count cannot wrap a 32-bit size_t into a match (T4 m2). */
static bool file_counts(const uint8_t *p, size_t size, pcount_t *k)
{
    if (!p || size < OMNI_PMAP_HDR) return false;
    k->nspans = rd32(p + 8);
    k->npx = rd32(p + 12);
    if (k->nspans == 0 || k->npx < k->nspans || k->npx > FILE_MAX_PX) return false;
    const uint64_t want = (uint64_t)OMNI_PMAP_HDR + (uint64_t)k->nspans * OMNI_PMAP_SPAN_BYTES + code_bytes(k->npx, k->nspans);
    return rd32(p + 16) == code_bytes(k->npx, k->nspans) && rd32(p + 20) == want && (uint64_t)size == want;
}

size_t omni_pmap_file_mem(const void *file, size_t size)
{
    pcount_t k;
    if (!file_counts(file, size, &k)) return 0;
    size_t a, b, c;
    return layout(&k, false, true, &a, &b, &c);
}

#define WHY(...) (why && why_n ? (void)snprintf(why, why_n, __VA_ARGS__) : (void)0)

bool omni_pmap_load(omni_pmap_t *m, const omni_pmap_cfg_t *cfg, const void *file, size_t size, void *mem,
                    size_t bytes, char *why, size_t why_n)
{
    memset(m, 0, sizeof *m);
    const uint8_t *p = file;
    pcount_t k;
    if (!p || size < OMNI_PMAP_HDR || memcmp(p, "OPM1", 4) != 0) {
        WHY("magic");
        return false;
    }
    if (rd16(p + 4) != OMNI_PMAP_VERSION) {
        WHY("version %u, want %u", (unsigned)rd16(p + 4), OMNI_PMAP_VERSION);
        return false;
    }
    if (!file_counts(p, size, &k)) {
        WHY("size %u does not match the header", (unsigned)size);
        return false;
    }
    uint8_t want[34];
    cfg_record(cfg, want);
    if (rd16(p + 6) != cfg->n_theta || memcmp(p + 32, want, sizeof want) != 0) {
        WHY("cfg (file N %u, want %u)", (unsigned)rd16(p + 6), cfg->n_theta);
        return false;
    }
    size_t o_th, o_ri, o_codes;
    if (!mem || layout(&k, false, true, &o_th, &o_ri, &o_codes) > bytes) {
        WHY("memory short");
        return false;
    }
    const uint64_t ck = (uint64_t)rd32(p + 24) | (uint64_t)rd32(p + 28) << 32;
    if (omni_fnv1a64(14695981039346656037ull, p + OMNI_PMAP_HDR, size - OMNI_PMAP_HDR) != ck) {
        WHY("checksum");
        return false;
    }
    uint8_t *base = (uint8_t *)mem + ((4 - ((uintptr_t)mem & 3)) & 3);
    omni_pspan_t *sp = (omni_pspan_t *)(void *)base;
    const uint8_t *q = p + OMNI_PMAP_HDR;
    /* a pixel step moves the source 1 / k px at a radius of at least r_min: theta by at most N / (2 pi k r_min) entries,
     * so no span's base can be more (plus one for the floors) */
    const uint32_t max_base = (uint32_t)ceilf((float)cfg->n_theta / (TWO_PI_F * cfg->k * (float)(cfg->r_min ? cfg->r_min : 1))) + 1;
    uint32_t px = 0, nib = 0;
    const omni_rect_t *r = &cfg->rect;
    for (uint32_t i = 0; i < k.nspans; i++, q += OMNI_PMAP_SPAN_BYTES) {
        omni_pspan_t *s = &sp[i];
        s->y = (int16_t)rd16(q);
        s->x0 = (int16_t)rd16(q + 2);
        s->n = (int16_t)rd16(q + 4);
        s->ring = q[6];
        s->dir = q[7];
        s->th0 = (uint16_t)rd16(q + 8);
        s->ri0 = q[10];
        s->base = q[11];
        s->lut = rd32(q + 12);
        s->code = rd32(q + 16);
        if (s->n < 1 || s->n > MAX_SPAN || s->y < r->y0 || s->y >= r->y1 || s->x0 < r->x0 || s->x0 + s->n > r->x1 ||
            s->ring >= OMNI_RINGS || s->dir > 1 || s->th0 >= cfg->n_theta || s->ri0 >= cfg->n_r || s->lut != px ||
            s->code != nib || (i && s->y < sp[i - 1].y) || s->base > max_base ||
            px + (uint32_t)s->n > k.npx) {   /* before the code walk: nib + n - 1 stays in the code stream */
            WHY("span %u out of range", (unsigned)i);
            return false;
        }
        /* the code stream: no reserved code, and the radius walk stays in the rows (T2 m1 / T4 m1) */
        const uint8_t *cs = p + OMNI_PMAP_HDR + (size_t)k.nspans * OMNI_PMAP_SPAN_BYTES;
        int ri = s->ri0;
        for (uint32_t j = nib; j < nib + (uint32_t)s->n - 1; j++) {
            const unsigned c = (cs[j >> 1] >> ((j & 1) * 4)) & 15u;
            ri += (int)(c % 3) - 1;
            if (c >= 12 || ri < 0 || ri >= cfg->n_r) {
                WHY("span %u code %u: %s", (unsigned)i, (unsigned)(j - nib), c >= 12 ? "reserved" : "radius out of the strip");
                return false;
            }
        }
        px += (uint32_t)s->n;
        nib += (uint32_t)s->n - 1;
    }
    if (px != k.npx) {
        WHY("span pixels %u, header %u", (unsigned)px, (unsigned)k.npx);
        return false;
    }
    m->cfg = *cfg;
    m->span = sp;
    m->codes = base + o_codes;
    memcpy(m->codes, q, code_bytes(k.npx, k.nspans));
    m->nspans = k.nspans;
    m->npx = k.npx;
    return true;
}

/* ---- Strips */

size_t omni_strip_bytes(uint16_t n_theta, uint8_t n_r) { return ((size_t)n_theta + OMNI_STRIP_PAD) * n_r * 3; }

size_t omni_strip_file_header(const omni_strip_t *st, uint8_t hdr[OMNI_STRIP_HDR])
{
    memset(hdr, 0, OMNI_STRIP_HDR);
    memcpy(hdr, "ORS1", 4);
    hdr[4] = (uint8_t)(st->n_theta & 255);
    hdr[5] = (uint8_t)(st->n_theta >> 8);
    hdr[6] = st->r_min;
    hdr[7] = st->n_r;
    hdr[8] = st->level;
    hdr[10] = (uint8_t)(st->stride & 255);
    hdr[11] = (uint8_t)(st->stride >> 8);
    return OMNI_STRIP_HDR;
}

bool omni_strip_map(omni_strip_t *st, const void *file, size_t size)
{
    const uint8_t *p = file;
    if (!p || size < OMNI_STRIP_HDR || memcmp(p, "ORS1", 4) != 0 || ((uintptr_t)p & 1)) return false;
    const uint16_t n = (uint16_t)(p[4] | p[5] << 8), stride = (uint16_t)(p[10] | p[11] << 8);
    if (!pow2(n) || p[7] == 0 || stride != n + OMNI_STRIP_PAD || size != OMNI_STRIP_HDR + omni_strip_bytes(n, p[7]))
        return false;
    st->n_theta = n;
    st->stride = stride;
    st->r_min = p[6];
    st->n_r = p[7];
    st->level = p[8];
    st->rgb = (const uint16_t *)(const void *)(p + OMNI_STRIP_HDR);
    st->a = p + OMNI_STRIP_HDR + (size_t)stride * p[7] * 2;
    return true;
}

/* Premultiplied bilinear sample of a 2x layer at screen point (X, Y) (pixel centres at +0.5). */
static void sample2x(const uint8_t *l, double X, double Y, double acc[4], double w)
{
    const double u = X * 2 - 0.5, v = Y * 2 - 0.5;
    const int u0 = (int)floor(u), v0 = (int)floor(v);
    const double fu = u - u0, fv = v - v0;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            const int uu = u0 + i, vv = v0 + j;
            if (uu < 0 || vv < 0 || uu >= 932 || vv >= 932) continue;
            const double ww = (i ? fu : 1 - fu) * (j ? fv : 1 - fv) * w;
            const uint8_t *p = l + ((size_t)vv * 932 + uu) * 4;
            const double a = p[3] / 255.0 * ww;
            acc[0] += p[0] * a;
            acc[1] += p[1] * a;
            acc[2] += p[2] * a;
            acc[3] += a;
        }
}

static int round_to(double v, int max)
{
    const long i = lround(v);
    return i < 0 ? 0 : i > max ? max : (int)i;
}

bool omni_strip_from_rgba(omni_strip_t *st, const uint8_t *const rgba2x[OMNI_RINGS], uint8_t level, void *mem,
                          size_t bytes)
{
    const uint32_t N0 = st->n_theta, N = N0 >> level, nr = st->n_r;
    if (!pow2(N0) || !pow2(N) || nr == 0 || !mem || ((uintptr_t)mem & 1) || bytes < omni_strip_bytes((uint16_t)N, (uint8_t)nr))
        return false;
    for (int i = 0; i < OMNI_RINGS; i++)
        if (!rgba2x[i]) return false;
    const uint32_t S = N + OMNI_STRIP_PAD;
    memset(mem, 0, omni_strip_bytes((uint16_t)N, (uint8_t)nr));
    uint16_t *rgb = mem;
    uint8_t *alpha = (uint8_t *)mem + (size_t)S * nr * 2;
    double *row = malloc((size_t)N * 4 * sizeof *row), *blur = malloc((size_t)N * 4 * sizeof *blur);
    if (!row || !blur) {
        free(row);
        free(blur);
        return false;
    }
    const int B = level == 0 ? 0 : level == 1 ? OMNI_BLUR_L1 : OMNI_BLUR_L2;
    for (uint32_t ri = 0; ri < nr; ri++) {
        const double r_lo = st->r_min + (double)ri;
        const int ring = ring_at((float)(r_lo + 0.5));
        for (uint32_t j = 0; j < N; j++) {
            double acc[4] = { 0, 0, 0, 0 };
            if (ring >= 0)
                for (int sv = 0; sv < 4; sv++)
                    for (int su = 0; su < 4; su++) {
                        const double r = r_lo + (su + 0.5) / 4, t = (j + (sv + 0.5) / 4) * TWO_PI_D / N;
                        sample2x(rgba2x[ring], 233 + r * sin(t), 233 - r * cos(t), acc, 1.0 / 16);
                    }
            memcpy(row + j * 4, acc, sizeof acc);
        }
        if (B > 0) {   /* a box B entries wide centred on each entry: taps -B/2..B/2, the two end taps at half weight */
            for (uint32_t j = 0; j < N; j++)
                for (int c = 0; c < 4; c++) {
                    double s = 0;
                    for (int t = -B / 2; t <= B / 2; t++) {
                        const double w = (t == -B / 2 || t == B / 2) ? 0.5 : 1.0;
                        s += w * row[((j + N + (uint32_t)t) & (N - 1)) * 4 + c];
                    }
                    blur[j * 4 + c] = s / B;
                }
            memcpy(row, blur, (size_t)N * 4 * sizeof *row);
        }
        for (uint32_t j = 0; j < N; j++) {
            const double *p = row + j * 4;
            const int a8 = round_to(p[3] * 255, 255);
            const size_t e = (size_t)ri * S + j;
            alpha[e] = (uint8_t)a8;
            if (a8 == 0) {
                rgb[e] = 0;
                continue;
            }
            const double inv = 1 / p[3];
            rgb[e] = (uint16_t)(round_to(p[0] * inv * 31 / 255, 31) << 11 | round_to(p[1] * inv * 63 / 255, 63) << 5 |
                                round_to(p[2] * inv * 31 / 255, 31));
        }
    }
    free(row);
    free(blur);
    st->n_theta = (uint16_t)N;
    st->stride = (uint16_t)S;
    st->level = level;
    st->rgb = rgb;
    st->a = alpha;
    return true;
}

/* ---- Offsets and fidelity */

uint32_t omni_ring_off(uint16_t n_theta, uint8_t level, uint16_t slots, float val)
{
    const uint32_t n = (uint32_t)n_theta >> level;
    if (n == 0 || slots == 0) return 0;
    float f = fmodf(val, (float)slots);   /* exact */
    if (!(f == f)) f = 0;                 /* NaN */
    if (f < 0) f += (float)slots;
    const float e = floorf(f / (float)slots * (float)n + 0.5f);
    uint32_t o = e > 0 ? (uint32_t)e : 0;
    return o % n;
}

void omni_rings_fidelity(omni_rings_pose_t *p, const float speed[OMNI_RINGS])
{
    for (int i = 0; i < OMNI_RINGS; i++) {
        const float s = fabsf(speed[i]);
        p->level[i] = s >= OMNI_FAST2 ? 2 : s >= OMNI_FAST1 ? 1 : 0;
    }
}

/* ---- Span kernels */

static unsigned level_shift(const omni_strip_t *st, const omni_pmap_t *m)
{
    unsigned sh = 0;
    while (((uint32_t)st->n_theta << sh) < m->cfg.n_theta) sh++;
    return sh;
}

/* One strip entry over a pixel: transparent keeps dst and opaque gives the colour exactly (blend565.h), so both skip
 * the blend and stay bit-exact. */
static inline void put(uint16_t *d, const uint16_t *rgb, const uint8_t *alpha, uint32_t e)
{
    const int a = alpha[e];
    if (a == 255) *d = rgb[e];
    else if (a) *d = omni_blend565(*d, rgb[e], a);
}

/* The reference, variant A: every pixel's (th, ri) from the plain LUT. sh (the strip's level against the map's N) is
 * a constant in each instance, so the loop has no variable shift. */
static inline __attribute__((always_inline)) void lut_sh(const omni_strip_t *st, const omni_pmap_t *m,
                                                         const omni_pspan_t *sp, uint32_t off, uint16_t *dst,
                                                         const unsigned sh)
{
    const uint32_t S = st->stride, mask = st->n_theta - 1u;
    const uint16_t *th = m->th + sp->lut, *rgb = st->rgb;
    const uint8_t *ri = m->ri + sp->lut, *alpha = st->a;
    off &= mask;
    for (int i = 0; i < sp->n; i++) put(dst + i, rgb, alpha, (uint32_t)ri[i] * S + ((((uint32_t)th[i] >> sh) + off) & mask));
}

void omni_rings_span_lut(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp, uint32_t off, uint16_t *dst)
{
    switch (level_shift(st, m)) {
    case 0: lut_sh(st, m, sp, off, dst, 0); break;
    case 1: lut_sh(st, m, sp, off, dst, 1); break;
    case 2: lut_sh(st, m, sp, off, dst, 2); break;
    default: lut_sh(st, m, sp, off, dst, level_shift(st, m)); break;
    }
}

/* Variant B, the delta stream: theta and radius accumulators from (th0, ri0), one 4-bit code per further pixel, read
 * a byte (two codes) at a time. t is full-resolution theta, reduced mod N << sh once per span (off may be any value);
 * the strip index masks it every pixel, so a span may cross 12 o'clock. */
static inline __attribute__((always_inline)) void codes_sh(const omni_strip_t *st, const omni_pmap_t *m,
                                                           const omni_pspan_t *sp, uint32_t off, uint16_t *dst,
                                                           const unsigned sh)
{
    const uint32_t N = st->n_theta, S = st->stride, mask = N - 1u;
    uint32_t dt[16], dr[16];
    for (int c = 0; c < 12; c++) {
        const uint32_t step = sp->base + (uint32_t)(c / 3);
        dt[c] = sp->dir ? 0u - step : step;
        dr[c] = (uint32_t)(c % 3 - 1) * S;
    }
    for (int c = 12; c < 16; c++) dt[c] = dr[c] = 0;   /* reserved: never built */
    uint32_t t = (sp->th0 + ((off & mask) << sh)) & ((N << sh) - 1u);
    uint32_t ro = (uint32_t)sp->ri0 * S;
    const uint16_t *rgb = st->rgb;
    const uint8_t *alpha = st->a;
    const uint8_t *cp = m->codes + (sp->code >> 1);
    uint16_t *d = dst, *const end = dst + sp->n;
    put(d++, rgb, alpha, ro + ((t >> sh) & mask));
    if (d == end) return;
    if (sp->code & 1) {   /* the first code is a high nibble */
        const unsigned c = *cp++ >> 4;
        t += dt[c];
        ro += dr[c];
        put(d++, rgb, alpha, ro + ((t >> sh) & mask));
    }
    while (end - d >= 2) {   /* a byte: two codes, two pixels */
        const unsigned b = *cp++, c0 = b & 15, c1 = b >> 4;
        t += dt[c0];
        ro += dr[c0];
        put(d, rgb, alpha, ro + ((t >> sh) & mask));
        t += dt[c1];
        ro += dr[c1];
        put(d + 1, rgb, alpha, ro + ((t >> sh) & mask));
        d += 2;
    }
    if (d < end) {
        const unsigned c = *cp & 15;
        t += dt[c];
        ro += dr[c];
        put(d, rgb, alpha, ro + ((t >> sh) & mask));
    }
}

void omni_rings_span_codes(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp, uint32_t off, uint16_t *dst)
{
    switch (level_shift(st, m)) {
    case 0: codes_sh(st, m, sp, off, dst, 0); break;
    case 1: codes_sh(st, m, sp, off, dst, 1); break;
    case 2: codes_sh(st, m, sp, off, dst, 2); break;
    default: codes_sh(st, m, sp, off, dst, level_shift(st, m)); break;
    }
}

/* ---- Drawing */

static uint32_t first_span_at(const omni_pmap_t *m, int y)   /* spans are in row order */
{
    uint32_t lo = 0, hi = m->nspans;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (m->span[mid].y < y) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void draw_impl(const omni_strip_t *const strips[3], const omni_pmap_t *m, const omni_rings_pose_t *p, int only,
                      omni_rings_span_fn fn, uint16_t *fb, int stride, const omni_rect_t *clip)
{
    const omni_strip_t *st[OMNI_RINGS];
    uint32_t off[OMNI_RINGS];
    for (int i = 0; i < OMNI_RINGS; i++) {
        st[i] = strips[p->level[i] < 3 ? p->level[i] : 2];
        off[i] = omni_ring_off(st[i]->n_theta, 0, OMNI_RING_GEOM[i].slots, p->val[i]);
    }
    const int cx0 = clip ? clip->x0 : 0, cy0 = clip ? clip->y0 : 0;
    const int cx1 = clip ? clip->x1 : 32767, cy1 = clip ? clip->y1 : 32767;
    for (uint32_t si = clip ? first_span_at(m, cy0) : 0; si < m->nspans; si++) {
        const omni_pspan_t *sp = &m->span[si];
        if (sp->y >= cy1) break;
        if (only >= 0 && sp->ring != only) continue;
        const int x0 = sp->x0, x1 = sp->x0 + sp->n;
        if (x1 <= cx0 || x0 >= cx1) continue;
        uint16_t *row = fb + (size_t)(sp->y - cy0) * stride;   /* row[x - cx0]: screen column x */
        if (x0 >= cx0 && x1 <= cx1) {
            fn(st[sp->ring], m, sp, off[sp->ring], row + (x0 - cx0));
            continue;
        }
        uint16_t tmp[MAX_SPAN];   /* cut by the clip: the whole span on a copy, its inside written back */
        const int a = x0 > cx0 ? x0 : cx0, b = x1 < cx1 ? x1 : cx1;
        memset(tmp, 0, (size_t)sp->n * sizeof *tmp);
        memcpy(tmp + (a - x0), row + (a - cx0), (size_t)(b - a) * sizeof *tmp);
        fn(st[sp->ring], m, sp, off[sp->ring], tmp);
        memcpy(row + (a - cx0), tmp + (a - x0), (size_t)(b - a) * sizeof *tmp);
    }
}

void omni_rings_draw(const omni_strip_t *const strips[3], const omni_pmap_t *m, const omni_rings_pose_t *p,
                     omni_rings_span_fn fn, uint16_t *fb, int stride, const omni_rect_t *clip)
{
    draw_impl(strips, m, p, -1, fn, fb, stride, clip);
}

void omni_rings_draw_ring(const omni_strip_t *const strips[3], const omni_pmap_t *m, const omni_rings_pose_t *p,
                          int ring, omni_rings_span_fn fn, uint16_t *fb, int stride, const omni_rect_t *clip)
{
    draw_impl(strips, m, p, ring, fn, fb, stride, clip);
}

bool omni_rings_compatible(const omni_strip_t *const strips[3], const omni_pmap_t *m, char *why, size_t why_n)
{
    for (int l = 0; l < 3; l++) {
        const omni_strip_t *st = strips[l];
        if (!st || !st->rgb || !st->a) {
            WHY("strip %d missing", l);
            return false;
        }
        if (st->level != l || st->n_theta != (m->cfg.n_theta >> l) || st->stride < st->n_theta) {
            WHY("strip %d: N %u level %u stride %u, map N %u", l, st->n_theta, st->level, st->stride, m->cfg.n_theta);
            return false;
        }
        if (st->r_min != m->cfg.r_min || st->n_r < m->cfg.n_r) {
            WHY("strip %d: r %u + %u, map r %u + %u", l, st->r_min, st->n_r, m->cfg.r_min, m->cfg.n_r);
            return false;
        }
    }
    return true;
}

/* ---- Bit-exactness */

static uint16_t pattern(int x, int y) { return (uint16_t)((x * 37 + y * 101) ^ 0x8410); }

bool omni_ring_kernel_bitexact(omni_rings_span_fn fn, const omni_strip_t *st, const omni_pmap_t *m, uint32_t off,
                               uint16_t *scratch, char *why, size_t why_n)
{
    const int x0 = m->cfg.rect.x0, w = m->cfg.rect.x1 - x0, n = w + OMNI_KCHECK_GUARD;
    uint16_t *ra = scratch, *rb = scratch + n;
    for (uint32_t si = 0; si < m->nspans; si++) {
        const omni_pspan_t *sp = &m->span[si];
        for (int x = 0; x < n; x++) ra[x] = x >= w ? 0xa5a5 : pattern(x + x0, sp->y);
        memcpy(rb, ra, (size_t)n * sizeof *ra);
        omni_rings_span_lut(st, m, sp, off, ra + (sp->x0 - x0));
        fn(st, m, sp, off, rb + (sp->x0 - x0));
        for (int x = 0; x < n; x++)
            if (ra[x] != rb[x]) {
                if (why)
                    snprintf(why, why_n, "ring %u row %d x %d off %u: lut %04x, kernel %04x", sp->ring, sp->y, x + x0,
                             (unsigned)off, ra[x], rb[x]);
                return false;
            }
    }
    return true;
}

/* Synthetic: a 256 x 24 strip (and a 128-wide one, read at shift 1), alpha a third 0, a third 255; 48 spans of ragged
 * starts and lengths in a 64 px rect, the last eight short (n 2, 2, 3, 4, 3, 4, 1, 1: n 2, 3 and 4 each starting on
 * both nibbles, the decoder's short paths, T2 m3), each with a random th0/ri0, direction, base (0..2) and codes that
 * keep ri in the strip; the plain LUT decoded from the codes. Every span drawn at several offsets onto a patterned row with guard
 * pixels, FNV-1a over the rows. All integer (xorshift32): no libm. */

#define SY_N 256
#define SY_NR 24
#define SY_SPANS 48
#define SY_W 64
#define SY_ROW (SY_W + OMNI_KCHECK_GUARD)

static uint32_t sy_xs(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

size_t omni_ring_synth_bytes(void)
{
    return 16 + omni_strip_bytes(SY_N, SY_NR) + omni_strip_bytes(SY_N / 2, SY_NR) + SY_SPANS * sizeof(omni_pspan_t) +
           (size_t)SY_SPANS * SY_W * 3 + SY_SPANS * SY_W / 2 + 8 + SY_ROW * 2;
}

static void sy_strip(omni_strip_t *s, uint16_t n, uint8_t **p, uint32_t *seed)
{
    const uint32_t S = n + OMNI_STRIP_PAD;
    uint16_t *rgb = (uint16_t *)(void *)*p;
    uint8_t *a = *p + (size_t)S * SY_NR * 2;
    for (uint32_t i = 0; i < S * SY_NR; i++) {
        rgb[i] = (uint16_t)sy_xs(seed);
        const uint32_t k = sy_xs(seed) % 3;
        a[i] = k == 0 ? 0 : k == 1 ? 255 : (uint8_t)(1 + sy_xs(seed) % 254);
    }
    *s = (omni_strip_t){ .n_theta = n, .stride = (uint16_t)S, .r_min = 100, .n_r = SY_NR, .level = n == SY_N ? 0 : 1, .rgb = rgb, .a = a };
    *p += omni_strip_bytes(n, SY_NR);
}

uint64_t omni_ring_synth_digest(omni_rings_span_fn fn, void *mem, size_t bytes)
{
    if (!mem || bytes < omni_ring_synth_bytes()) return 0;
    uint8_t *p = (uint8_t *)mem + ((16 - ((uintptr_t)mem & 15)) & 15);
    uint32_t seed = 0x2468ace1u;
    omni_strip_t st[2];
    sy_strip(&st[0], SY_N, &p, &seed);
    sy_strip(&st[1], SY_N / 2, &p, &seed);
    p += (4 - ((uintptr_t)p & 3)) & 3;
    omni_pmap_t m = { .cfg = { .rect = { 0, 0, SY_W, SY_SPANS }, .n_theta = SY_N, .r_min = 100, .n_r = SY_NR, .k = 1 } };
    m.span = (omni_pspan_t *)(void *)p;
    p += SY_SPANS * sizeof(omni_pspan_t);
    m.th = (uint16_t *)(void *)p;
    p += (size_t)SY_SPANS * SY_W * 2;
    m.ri = p;
    p += (size_t)SY_SPANS * SY_W;
    m.codes = p;
    p += SY_SPANS * SY_W / 2;
    memset(m.codes, 0, SY_SPANS * SY_W / 2);
    uint16_t *row = (uint16_t *)(void *)(p + ((uintptr_t)p & 1));
    uint32_t px = 0, nib = 0;
    for (int si = 0; si < SY_SPANS; si++) {
        omni_pspan_t *s = &m.span[si];
        s->y = (int16_t)si;
        s->x0 = (int16_t)((si * 5) % 9);
        static const int8_t SHORT[8] = { 2, 2, 3, 4, 3, 4, 1, 1 };
        s->n = (int16_t)(si >= SY_SPANS - 8 ? SHORT[si - (SY_SPANS - 8)] : SY_W - s->x0 - (si * 3) % 7);
        s->ring = (uint8_t)(si % OMNI_RINGS);
        s->dir = (uint8_t)(sy_xs(&seed) & 1);
        s->base = (uint8_t)(sy_xs(&seed) % 3);
        s->th0 = (uint16_t)(sy_xs(&seed) % SY_N);
        s->ri0 = (uint8_t)(sy_xs(&seed) % SY_NR);
        s->lut = px;
        s->code = nib;
        uint32_t th = s->th0;
        int ri = s->ri0;
        for (int i = 0; i < s->n; i++) {
            if (i > 0) {
                int dr = (int)(sy_xs(&seed) % 3) - 1;
                if (ri + dr < 0 || ri + dr >= SY_NR) dr = -dr;
                const int c = (int)(sy_xs(&seed) % 4) * 3 + dr + 1;
                m.codes[nib >> 1] |= (uint8_t)(c << ((nib & 1) * 4));
                nib++;
                const uint32_t step = s->base + (uint32_t)(c / 3);
                th = s->dir ? th - step : th + step;
                ri += dr;
            }
            m.th[px + (uint32_t)i] = (uint16_t)(th & (SY_N - 1));
            m.ri[px + (uint32_t)i] = (uint8_t)ri;
        }
        px += (uint32_t)s->n;
    }
    m.nspans = SY_SPANS;
    m.npx = px;
    static const uint32_t OFFS[] = { 0, 1, 77, 255, 256 + 3, 0xfffffff0u };
    uint64_t h = OMNI_FNV1A_INIT;
    for (int k = 0; k < 2; k++)
        for (size_t oi = 0; oi < sizeof OFFS / sizeof OFFS[0]; oi++)
            for (int si = 0; si < SY_SPANS; si++) {
                const omni_pspan_t *s = &m.span[si];
                for (int x = 0; x < SY_ROW; x++) row[x] = x >= SY_W ? 0xa5a5 : pattern(x, si + 7 * (int)oi);
                fn(&st[k], &m, s, OFFS[oi], row + s->x0);
                h = omni_fnv1a64(h, row, SY_ROW * sizeof *row);
            }
    return h;
}
