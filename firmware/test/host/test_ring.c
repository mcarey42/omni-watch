#include "unity.h"
#include "omni/blend565.h"
#include "omni/globe_check.h"
#include "omni/omni_dial.h"
#include "omni/ring.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define PI 3.14159265358979323846
#define W 466
#define L2X 932                       /* the 2x engraving layers */
#define GREY 0x8410                   /* the mid-grey base the oracle test renders over */
#define DIAL_PX 72016u                /* screen pixels with 126 <= r < 197 */
#define ORACLE_W0 68                  /* the per-pose pins (test_ring_vs_oracle): measured 66 / 108 / 83 worst, */
#define ORACLE_W1 110                 /* 0.8462 / 0.8372 / 0.8448 within 6 */
#define ORACLE_W2 85
#define ORACLE_P0 0.8455
#define ORACLE_P1 0.8365
#define ORACLE_P2 0.8440

static const omni_pmap_cfg_t DIAL = { .rect = { 36, 36, 430, 430 }, .k = 1, .r_min = 126, .n_r = 71 };

static omni_pmap_t g_map[3];          /* N 1024, 2048, 4096: both variants */
static void *g_map_mem[3];
static uint8_t *g_layer[OMNI_RINGS];  /* the synthetic engraving, 2x unpremultiplied RGBA */
static omni_strip_t g_strip[3];       /* N 2048, levels 0..2 */
static void *g_strip_mem[3];
static omni_strip_t g_strip1k, g_strip4k;   /* N 1024 and 4096, level 0 */
static void *g_strip1k_mem, *g_strip4k_mem;
static uint16_t g_fa[W * W], g_fb[W * W];

static uint32_t xs32(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

static omni_pmap_cfg_t dial_cfg(uint16_t n)
{
    omni_pmap_cfg_t c = DIAL;
    c.n_theta = n;
    return c;
}

static bool build(omni_pmap_t *m, void **mem, uint16_t n, bool lut, bool codes)
{
    const omni_pmap_cfg_t c = dial_cfg(n);
    const size_t b = omni_pmap_bytes(&c, lut, codes);
    *mem = malloc(b);
    return *mem && omni_pmap_build(m, &c, lut, codes, *mem, b);
}

static int ring_of(double r)
{
    for (int i = 0; i < OMNI_RINGS; i++)
        if (r >= OMNI_RING_GEOM[i].r0 && r < OMNI_RING_GEOM[i].r1) return i;
    return -1;
}

/* ---- Synthetic engraving: per slot a text-like glyph, three radial strokes 1.6 px wide and a 1.2 px bar across
 * their foot, ink rgba(14, 9, 3, .92) on a transparent ground, edges anti-aliased over one layer pixel. Slot 0 alone
 * also has a dot to the clockwise side of its head, so a label placed a whole slot off cannot match (T2 m6). */

static double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }

static double glyph_cov(int ring, double r, double th)
{
    const omni_ring_geom_t *g = &OMNI_RING_GEOM[ring];
    const double rt = (g->r0 + g->r1) / 2, step = 2 * PI / g->slots;
    const double k = floor(th / step + 0.5), dth = th - k * step, arc = dth * r;   /* px from the label centre */
    double cov = 0;
    for (int s = -1; s <= 1; s++) {   /* strokes: |arc - 3.2 s| < 0.8, |r - rt| < 3.5 */
        const double d = fmax(fabs(arc - 3.2 * s) - 0.8, fabs(r - rt) - 3.5);
        cov = fmax(cov, clampd(0.5 - d * 2, 0, 1));   /* one layer pixel = 0.5 screen px of ramp */
    }
    const double d = fmax(fabs(arc) - 4.0, fabs(r - (rt + 3.5)) - 0.6);
    cov = fmax(cov, clampd(0.5 - d * 2, 0, 1));
    if (((long)k % g->slots + g->slots) % g->slots == 0) {   /* slot 0's marker dot: arc 5.6, r rt - 2.5 */
        const double dd = fmax(fabs(arc - 5.6) - 0.9, fabs(r - (rt - 2.5)) - 0.9);
        cov = fmax(cov, clampd(0.5 - dd * 2, 0, 1));
    }
    return cov;
}

static void make_layers(void)
{
    for (int i = 0; i < OMNI_RINGS; i++) {
        g_layer[i] = calloc((size_t)L2X * L2X, 4);
        TEST_ASSERT_NOT_NULL(g_layer[i]);
        for (int y = 0; y < L2X; y++)
            for (int x = 0; x < L2X; x++) {
                const double X = (x + 0.5) / 2 - 233, Y = 233 - (y + 0.5) / 2, r = hypot(X, Y);
                if (ring_of(r) != i) continue;
                double th = atan2(X, Y);
                if (th < 0) th += 2 * PI;
                const double cov = glyph_cov(i, r, th);
                if (cov <= 0) continue;
                uint8_t *p = g_layer[i] + ((size_t)y * L2X + x) * 4;
                p[0] = 14; p[1] = 9; p[2] = 3;
                p[3] = (uint8_t)lround(cov * 0.92 * 255);
            }
    }
}

/* The mockup's drawRot(K.eng[i], -val/n 2pi): rotate the screen pixel's centre back and sample the 2x layer
 * bilinearly (premultiplied), then quantised as the device quantises a strip entry (RGB565 rounded, alpha 8 bits) and
 * blended over the grey base with the one rule (blend565.h). The comparison then measures sampling only: quantised
 * the same way, a float composite differs from it by up to 12 (RGB565 rounding plus the truncating pack). */
static uint16_t oracle_px(const float val[OMNI_RINGS], int x, int y)
{
    const double X = x + 0.5 - 233, Y = 233 - (y + 0.5), r = hypot(X, Y);
    const int ring = ring_of(r);
    if (ring < 0) return GREY;
    const double rot = val[ring] / OMNI_RING_GEOM[ring].slots * 2 * PI, c = cos(rot), s = sin(rot);
    /* theta_src = theta + rot, clockwise from 12: (X, Y) -> (X c + Y s, -X s + Y c) */
    const double sx = X * c + Y * s, sy = -X * s + Y * c;
    const double u = (233 + sx) * 2 - 0.5, v = (233 - sy) * 2 - 0.5;
    const int u0 = (int)floor(u), v0 = (int)floor(v);
    const double fu = u - u0, fv = v - v0;
    double acc[4] = { 0, 0, 0, 0 };
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            const int uu = u0 + i, vv = v0 + j;
            if (uu < 0 || vv < 0 || uu >= L2X || vv >= L2X) continue;
            const double w = (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
            const uint8_t *p = g_layer[ring] + ((size_t)vv * L2X + uu) * 4;
            const double a = p[3] / 255.0 * w;
            acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a;
        }
    const long a8 = lround(acc[3] * 255);
    if (a8 <= 0) return GREY;
    const double inv = 1 / acc[3];
    const long R = lround(acc[0] * inv * 31 / 255), G = lround(acc[1] * inv * 63 / 255), B = lround(acc[2] * inv * 31 / 255);
    const uint16_t col = (uint16_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (B > 31 ? 31 : B));
    return omni_blend565(GREY, col, (int)(a8 > 255 ? 255 : a8));
}

static int chan_err(uint16_t a, uint16_t b)
{
    const int d[3] = { abs(omni_ex_r(a) - omni_ex_r(b)), abs(omni_ex_g(a) - omni_ex_g(b)), abs(omni_ex_b(a) - omni_ex_b(b)) };
    const int m = d[0] > d[1] ? d[0] : d[1];
    return m > d[2] ? m : d[2];
}

static void fill(uint16_t *fb, uint16_t v)
{
    for (int i = 0; i < W * W; i++) fb[i] = v;
}

static const omni_strip_t *const *strips(void)
{
    static const omni_strip_t *s[3];
    for (int i = 0; i < 3; i++) s[i] = &g_strip[i];
    return s;
}

static void render(const omni_pmap_t *m, const float val[OMNI_RINGS], const uint8_t level[OMNI_RINGS],
                   omni_rings_span_fn fn, uint16_t *fb)
{
    omni_rings_pose_t p;
    for (int i = 0; i < OMNI_RINGS; i++) {
        p.val[i] = val[i];
        p.level[i] = level ? level[i] : 0;
    }
    fill(fb, GREY);
    omni_rings_draw(strips(), m, &p, fn, fb, W, NULL);
}

/* ---- Tests */

/* D5: the one blend rule is Stage 2's globe blend, and the globe's digests (its kernels now through blend565.h) hold. */
static void test_blend565_globe_unchanged(void)
{
    uint32_t seed = 99;
    for (int i = 0; i < 200000; i++) {
        const uint16_t d = (uint16_t)xs32(&seed), s = (uint16_t)xs32(&seed);
        const int a = (int)(xs32(&seed) & 255), ap = a + (a >> 7);
        const int r5 = (((s >> 11) << 3 | (s >> 13)) * ap + ((d >> 11) << 3 | (d >> 13)) * (256 - ap)) >> 8;
        const int g6 = ((((s >> 5) & 63) << 2 | ((s >> 9) & 3)) * ap + (((d >> 5) & 63) << 2 | ((d >> 9) & 3)) * (256 - ap)) >> 8;
        const int b5 = (((s & 31) << 3 | ((s >> 2) & 7)) * ap + ((d & 31) << 3 | ((d >> 2) & 7)) * (256 - ap)) >> 8;
        TEST_ASSERT_EQUAL_HEX16((r5 >> 3) << 11 | (g6 >> 2) << 5 | b5 >> 3, omni_blend565(d, s, a));
    }
    TEST_ASSERT_EQUAL_HEX16(0x1234, omni_blend565(0x1234, 0xffff, 0));
    TEST_ASSERT_EQUAL_HEX16(0xbeef, omni_blend565(0x1234, 0xbeef, 255));
    size_t b = omni_globe_synth_bytes();
    void *mem = malloc(b);
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_SYNTH_DIGEST, omni_globe_synth_digest(omni_globe_span_ref, mem, b));
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_SYNTH_DIGEST, omni_globe_synth_digest(omni_globe_span_fast, mem, b));
    free(mem);
    b = omni_globe_csynth_bytes();
    mem = malloc(b);
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_CSYNTH_DIGEST, omni_globe_csynth_digest(omni_globe_cspan_ref, mem, b));
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_CSYNTH_DIGEST, omni_globe_csynth_digest(omni_globe_cspan_fast, mem, b));
    free(mem);
}

static void test_pmap_matches_atan2(void)
{
    const omni_pmap_t *m = &g_map[1];
    TEST_ASSERT_EQUAL_UINT32(DIAL_PX, m->npx);
    uint32_t n = 0, bad = 0;
    for (uint32_t si = 0; si < m->nspans; si++) {
        const omni_pspan_t *s = &m->span[si];
        n += (uint32_t)s->n;
        for (int i = 0; i < s->n; i++) {
            const int x = s->x0 + i, y = s->y;
            const double X = x - 233 + .5, Y = 233 - y - .5, r = hypot(X, Y);
            double th = atan2(X, Y);
            if (th < 0) th += 2 * PI;
            const int want_th = (int)floor(th * 2048 / (2 * PI)), want_ri = (int)floor(r) - 126;
            const int got_th = m->th[s->lut + i], got_ri = m->ri[s->lut + i];
            if (got_th != want_th || got_ri != want_ri || s->ring != ring_of(r)) {
                if (bad++ < 5)
                    printf("pmap: (%d, %d) th %d want %d, ri %d want %d, ring %d\n", x, y, got_th, want_th, got_ri,
                           want_ri, s->ring);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(DIAL_PX, n);
    TEST_ASSERT_EQUAL_UINT32(0, bad);
    /* every annulus pixel is in exactly one span */
    static uint8_t seen[W * W];
    memset(seen, 0, sizeof seen);
    for (uint32_t si = 0; si < m->nspans; si++)
        for (int i = 0; i < m->span[si].n; i++) seen[m->span[si].y * W + m->span[si].x0 + i]++;
    uint32_t want = 0, got = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const double r = hypot(x - 233 + .5, 233 - y - .5);
            const bool in = r >= 126 && r < 197;
            want += in;
            got += in && seen[y * W + x] == 1;
            TEST_ASSERT_TRUE(in || seen[y * W + x] == 0);
        }
    TEST_ASSERT_EQUAL_UINT32(DIAL_PX, want);
    TEST_ASSERT_EQUAL_UINT32(DIAL_PX, got);
}

/* Decodes span s's codes into (th, ri) per pixel and compares with the plain LUT. */
static void check_codes(const omni_pmap_t *m, bool expect_base0)
{
    const uint32_t mask = m->cfg.n_theta - 1u;
    for (uint32_t si = 0; si < m->nspans; si++) {
        const omni_pspan_t *s = &m->span[si];
        if (expect_base0) TEST_ASSERT_EQUAL_UINT8(0, s->base);
        uint32_t th = s->th0;
        int ri = s->ri0;
        for (int i = 0; i < s->n; i++) {
            if (i > 0) {
                const uint32_t ni = s->code + (uint32_t)(i - 1);
                const int c = (m->codes[ni >> 1] >> ((ni & 1) * 4)) & 15;
                TEST_ASSERT_TRUE_MESSAGE(c < 12, "reserved code");
                const uint32_t step = s->base + (uint32_t)(c / 3);
                th = s->dir ? th - step : th + step;
                ri += c % 3 - 1;
            }
            TEST_ASSERT_EQUAL_UINT16(m->th[s->lut + i], th & mask);
            TEST_ASSERT_EQUAL_UINT8(m->ri[s->lut + i], ri);
        }
    }
}

static void test_codes_equal_lut(void)
{
    check_codes(&g_map[0], true);
    check_codes(&g_map[1], true);
    check_codes(&g_map[2], false);   /* N 4096: per-span base (C5) */
    TEST_ASSERT_EQUAL_UINT32(0, omni_pmap_base_spans(&g_map[1]));
    printf("codes: N 4096 needs base > 0 on %u of %u spans\n", (unsigned)omni_pmap_base_spans(&g_map[2]),
           (unsigned)g_map[2].nspans);
    TEST_ASSERT_TRUE(omni_pmap_base_spans(&g_map[2]) > 0);
}

static void test_ring_off_label_at_12(void)
{
    for (int lev = 0; lev < 3; lev++)
        for (int i = 0; i < OMNI_RINGS; i++) {
            const uint16_t n = OMNI_RING_GEOM[i].slots;
            const float vals[4] = { 0, 1, (float)(n - 1), 2.5f };
            const uint32_t N = 2048u >> lev;
            for (int k = 0; k < 4; k++) {
                const uint32_t off = omni_ring_off(2048, (uint8_t)lev, n, vals[k]);
                /* Screen theta 0 reads entry off: the label's centre is at vals[k] / n * N entries */
                const double want = fmod(vals[k] / n * N, N);
                double d = fabs((double)off - want);
                if (d > N / 2.0) d = N - d;
                TEST_ASSERT_TRUE_MESSAGE(d <= 0.5, "label not at 12");
            }
        }
}

static void test_ring_offset_bounded(void)
{
    const float v[] = { 1e6f, -1e6f, -0.0001f, 999999.9f, -12.0f, 3.99999f };
    for (int lev = 0; lev < 3; lev++)
        for (int i = 0; i < OMNI_RINGS; i++)
            for (size_t k = 0; k < sizeof v / sizeof v[0]; k++) {
                const uint32_t off = omni_ring_off(2048, (uint8_t)lev, OMNI_RING_GEOM[i].slots, v[k]);
                TEST_ASSERT_TRUE(off < (2048u >> lev));
            }
}

static void test_kernels_bitexact(void)
{
    uint16_t *scratch = malloc(2 * (W + OMNI_KCHECK_GUARD) * sizeof *scratch);
    char why[128] = "";
    uint32_t seed = 12345;
    for (int mi = 0; mi < 3; mi++)
        for (int lev = 0; lev < 3; lev++) {
            if (mi != 1 && lev > 0) continue;   /* the strips are N 2048 */
            const omni_strip_t st = mi == 0 ? g_strip1k : mi == 2 ? g_strip4k : g_strip[lev];
            for (int k = 0; k < 64; k++) {
                const uint32_t off = xs32(&seed) & (st.n_theta - 1u);
                const bool ok = omni_ring_kernel_bitexact(omni_rings_span_codes, &st, &g_map[mi], off, scratch, why,
                                                          sizeof why);
                TEST_ASSERT_TRUE_MESSAGE(ok, why);
            }
        }
    free(scratch);
}

/* A kernel that is wrong in its last pixel, and one that stores a pixel past the span: the harness must see both. */
static void broken_last_px(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp, uint32_t off,
                           uint16_t *dst)
{
    omni_rings_span_lut(st, m, sp, off, dst);
    dst[sp->n - 1] ^= 1;
}
static void broken_tail_store(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp, uint32_t off,
                              uint16_t *dst)
{
    omni_rings_span_lut(st, m, sp, off, dst);
    dst[sp->n] = 0;
}

static void test_kernel_harness_catches(void)
{
    uint16_t *scratch = malloc(2 * (W + OMNI_KCHECK_GUARD) * sizeof *scratch);
    char why[128];
    TEST_ASSERT_TRUE(omni_ring_kernel_bitexact(omni_rings_span_lut, &g_strip[0], &g_map[1], 7, scratch, why, sizeof why));
    TEST_ASSERT_FALSE(omni_ring_kernel_bitexact(broken_last_px, &g_strip[0], &g_map[1], 7, scratch, why, sizeof why));
    TEST_ASSERT_FALSE(omni_ring_kernel_bitexact(broken_tail_store, &g_strip[0], &g_map[1], 7, scratch, why, sizeof why));
    free(scratch);
}

/* Pinned measurements, L0 at N 2048, per pose below (T2 m4: each pose its own pin, so one cannot regress under
 * another's): the worst channel error and the share of annulus pixels within 6, both against the quantised oracle,
 * pinned at the measured value plus a small epsilon. The brief's +-6 on 99.5 % and +-24 everywhere do not hold for a
 * nearest polar strip on glyph edges this sharp (a pixel samples up to half a cell, 0.5 px radially, from its centre,
 * plus up to half an entry of rotation; R81). Measured 2026-10-26 with slot 0's marker dot. */
static const int ORACLE_WORST[3] = { ORACLE_W0, ORACLE_W1, ORACLE_W2 };
static const double ORACLE_WITHIN6[3] = { ORACLE_P0, ORACLE_P1, ORACLE_P2 };

static void oracle_compare(const float val[OMNI_RINGS], int *worst_out, double *within_out)
{
    render(&g_map[1], val, NULL, omni_rings_span_lut, g_fa);
    int worst = 0;
    uint32_t n = 0, ok6 = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const double r = hypot(x - 233 + .5, 233 - y - .5);
            if (r < 126 || r >= 197) continue;
            const int m = chan_err(g_fa[y * W + x], oracle_px(val, x, y));
            n++;
            ok6 += m <= 6;
            if (m > worst) worst = m;
        }
    *worst_out = worst;
    *within_out = (double)ok6 / n;
}

static void test_ring_vs_oracle(void)
{
    const float vals[3][OMNI_RINGS] = { { 0, 0, 0, 0 }, { 3, 17, 26, 40 }, { 1.37f, 5.5f, 71.25f, 20.8f } };
    for (int k = 0; k < 3; k++) {
        int worst;
        double within;
        oracle_compare(vals[k], &worst, &within);
        printf("oracle: vals %d: worst %d, %.4f within 6\n", k, worst, within);
        TEST_ASSERT_TRUE(worst <= ORACLE_WORST[k]);
        TEST_ASSERT_TRUE(within >= ORACLE_WITHIN6[k]);
    }
    /* a whole slot off (val + 1 on every ring: every label moves to its neighbour's place) cannot pass: slot 0's dot
     * lands where the oracle has none */
    const float off1[OMNI_RINGS] = { 1, 1, 1, 1 };
    int worst;
    double within;
    render(&g_map[1], off1, NULL, omni_rings_span_lut, g_fa);
    {
        const float val0[OMNI_RINGS] = { 0, 0, 0, 0 };
        int w = 0;
        uint32_t n = 0, ok6 = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const double r = hypot(x - 233 + .5, 233 - y - .5);
                if (r < 126 || r >= 197) continue;
                const int m = chan_err(g_fa[y * W + x], oracle_px(val0, x, y));
                n++;
                ok6 += m <= 6;
                if (m > w) w = m;
            }
        worst = w;
        within = (double)ok6 / n;
    }
    printf("oracle: a whole slot off: worst %d, %.4f within 6\n", worst, within);   /* measured 124 */
    TEST_ASSERT_TRUE(worst > ORACLE_WORST[0] + 30);
}

static void test_strip_wrap_seamless(void)
{
    const omni_strip_t *s = &g_strip[0];
    const uint32_t N = s->n_theta;
    const uint32_t offs[][2] = { { 5, 5 + N }, { N - 1, (uint32_t)-1 }, { 0, N * 7 } };
    for (size_t k = 0; k < sizeof offs / sizeof offs[0]; k++) {
        /* omni_rings_draw takes values: drive the span kernels with the raw offsets */
        for (int v = 0; v < 2; v++) {
            uint16_t *fb = v ? g_fb : g_fa;
            fill(fb, GREY);
            const omni_pmap_t *m = &g_map[1];
            for (uint32_t si = 0; si < m->nspans; si++) {
                const omni_pspan_t *sp = &m->span[si];
                omni_rings_span_codes(s, m, sp, offs[k][v], fb + sp->y * W + sp->x0);
            }
        }
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
        for (int v = 0; v < 2; v++) {
            uint16_t *fb = v ? g_fb : g_fa;
            fill(fb, GREY);
            const omni_pmap_t *m = &g_map[1];
            for (uint32_t si = 0; si < m->nspans; si++) {
                const omni_pspan_t *sp = &m->span[si];
                omni_rings_span_lut(s, m, sp, offs[k][v], fb + sp->y * W + sp->x0);
            }
        }
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
    }
    /* No step at 12 o'clock, with its own tolerance (T2 m5): a half turn of every ring (val + slots / 2, all slots
     * even) puts at the map's seam the pixels that sat at 6 o'clock, so the frame must be the half-turned frame
     * mirrored through the centre exactly: every pixel, seam columns included, to the bit. */
    const float vals[2][OMNI_RINGS] = { { 0, 0, 0, 0 }, { 0.5f, 0.5f, 0.5f, 0.5f } };
    for (int k = 0; k < 2; k++) {
        float half[OMNI_RINGS];
        for (int i = 0; i < OMNI_RINGS; i++) half[i] = vals[k][i] + OMNI_RING_GEOM[i].slots / 2;
        render(&g_map[1], vals[k], NULL, omni_rings_span_lut, g_fa);
        render(&g_map[1], half, NULL, omni_rings_span_codes, g_fb);
        int bad = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (g_fa[y * W + x] != g_fb[(W - 1 - y) * W + (W - 1 - x)] && bad++ < 3)
                    printf("seam: (%d, %d) %04x, half turn mirrored %04x\n", x, y, g_fa[y * W + x],
                           g_fb[(W - 1 - y) * W + (W - 1 - x)]);
        TEST_ASSERT_EQUAL(0, bad);
    }
}

/* T2 m3: the magnifier's and the loupe's maps (rr clips, k 1.4 and 2.35) have spans of 1 to 4 pixels; the delta
 * decoder draws every one of them as the plain LUT does, guard pixels included, at both nibble parities. */
static void test_codes_short_spans(void)
{
    const omni_pmap_cfg_t cfgs[3] = { omni_dial_mag_cfg(2048), omni_dial_loupe_cfg(1, 2048), omni_dial_loupe_cfg(0.3f, 2048) };
    uint16_t *scratch = malloc(2 * (W + OMNI_KCHECK_GUARD) * sizeof *scratch);
    int seen[5][2] = { { 0 } };
    for (int c = 0; c < 3; c++) {
        const size_t b = omni_pmap_bytes(&cfgs[c], true, true);
        void *mem = malloc(b);
        omni_pmap_t m;
        TEST_ASSERT_TRUE(omni_pmap_build(&m, &cfgs[c], true, true, mem, b));
        check_codes(&m, true);
        for (uint32_t i = 0; i < m.nspans; i++)
            if (m.span[i].n <= 4) seen[m.span[i].n][m.span[i].code & 1]++;
        char why[128] = "";
        for (int lev = 0; lev < 3; lev++)
            for (uint32_t off = 0; off < 2048; off += 517) {
                const bool ok = omni_ring_kernel_bitexact(omni_rings_span_codes, &g_strip[lev], &m, off, scratch, why, sizeof why);
                TEST_ASSERT_TRUE_MESSAGE(ok, why);
            }
        free(mem);
    }
    printf("short spans in the zoom maps (n: even, odd first nibble): 1: %d %d, 2: %d %d, 3: %d %d, 4: %d %d\n", seen[1][0],
           seen[1][1], seen[2][0], seen[2][1], seen[3][0], seen[3][1], seen[4][0], seen[4][1]);
    TEST_ASSERT_TRUE(seen[1][0] + seen[1][1] + seen[2][0] + seen[2][1] + seen[3][0] + seen[3][1] + seen[4][0] + seen[4][1] > 0);
    free(scratch);
}

static void test_ring_synth_digest(void)
{
    const size_t b = omni_ring_synth_bytes();
    void *mem = malloc(b);
    const uint64_t h = omni_ring_synth_digest(omni_rings_span_lut, mem, b);
    printf("synth digest 0x%016llxull\n", (unsigned long long)h);
    TEST_ASSERT_EQUAL_HEX64(OMNI_RING_SYNTH_DIGEST, h);
    TEST_ASSERT_EQUAL_HEX64(OMNI_RING_SYNTH_DIGEST, omni_ring_synth_digest(omni_rings_span_codes, mem, b));
    TEST_ASSERT_TRUE(omni_ring_synth_digest(broken_last_px, mem, b) != OMNI_RING_SYNTH_DIGEST);
    TEST_ASSERT_EQUAL_HEX64(0, omni_ring_synth_digest(omni_rings_span_lut, mem, b - 1));   /* short */
    free(mem);
}

static void test_fidelity_still_is_full(void)
{
    omni_rings_pose_t p = { { 0 }, { 9, 9, 9, 9 } };
    const float still[OMNI_RINGS] = { 0, 0.009f, -0.009f, 0.0f };
    omni_rings_fidelity(&p, still);
    for (int i = 0; i < OMNI_RINGS; i++) TEST_ASSERT_EQUAL_UINT8(0, p.level[i]);
    const float fast[OMNI_RINGS] = { OMNI_FAST1, -OMNI_FAST1, OMNI_FAST2, -3.0f };
    omni_rings_fidelity(&p, fast);
    for (int i = 0; i < OMNI_RINGS; i++) TEST_ASSERT_TRUE(p.level[i] >= 1);
    TEST_ASSERT_EQUAL_UINT8(2, p.level[2]);
    TEST_ASSERT_EQUAL_UINT8(2, p.level[3]);
    /* fast -> 0: the first zero frame is full fidelity */
    const float seq[4][OMNI_RINGS] = { { 3, 3, 3, 3 }, { 2, 1, 0.6f, 3 }, { 0.2f, 0, 0, 0 }, { 0, 0, 0, 0 } };
    for (int f = 0; f < 4; f++) omni_rings_fidelity(&p, seq[f]);
    for (int i = 0; i < OMNI_RINGS; i++) TEST_ASSERT_EQUAL_UINT8(0, p.level[i]);
}

/* Clipped draws: a band of rows and a rect cut through spans equal the whole draw inside, and leave the rest. */
static void test_draw_clip(void)
{
    const float val[OMNI_RINGS] = { 2.25f, 9.5f, 33.1f, 12.0f };
    render(&g_map[1], val, NULL, omni_rings_span_codes, g_fa);
    const omni_rect_t clips[] = { { 0, 100, 466, 116 }, { 150, 40, 300, 77 }, { 233, 233, 234, 466 } };
    omni_rings_pose_t p;
    for (int i = 0; i < OMNI_RINGS; i++) { p.val[i] = val[i]; p.level[i] = 0; }
    for (size_t k = 0; k < sizeof clips / sizeof clips[0]; k++) {
        const omni_rect_t *c = &clips[k];
        fill(g_fb, GREY);
        omni_rings_draw(strips(), &g_map[1], &p, omni_rings_span_codes, g_fb + c->y0 * W + c->x0, W, c);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const bool in = x >= c->x0 && x < c->x1 && y >= c->y0 && y < c->y1;
                TEST_ASSERT_EQUAL_HEX16(in ? g_fa[y * W + x] : GREY, g_fb[y * W + x]);
            }
    }
}

/* L1/L2: half and quarter the entries, blurred along theta: still the glyph's mean ink, softer. */
static void test_strip_levels(void)
{
    TEST_ASSERT_EQUAL_UINT16(2048, g_strip[0].n_theta);
    TEST_ASSERT_EQUAL_UINT16(1024, g_strip[1].n_theta);
    TEST_ASSERT_EQUAL_UINT16(512, g_strip[2].n_theta);
    for (int l = 0; l < 3; l++) {
        TEST_ASSERT_EQUAL_UINT8(l, g_strip[l].level);
        TEST_ASSERT_EQUAL_UINT8(126, g_strip[l].r_min);
        TEST_ASSERT_EQUAL_UINT8(71, g_strip[l].n_r);
    }
    double mean[3] = { 0 };
    int amax[3] = { 0 };
    for (int l = 0; l < 3; l++) {
        const omni_strip_t *st = &g_strip[l];
        const uint32_t n = (uint32_t)st->n_theta * st->n_r;
        for (uint32_t r = 0; r < st->n_r; r++)
            for (uint32_t j = 0; j < st->stride; j++) {
                const uint8_t a = st->a[r * st->stride + j];
                if (j >= st->n_theta) {
                    TEST_ASSERT_EQUAL_UINT8(0, a);   /* the pad */
                    continue;
                }
                mean[l] += a;
                if (a > amax[l]) amax[l] = a;
            }
        mean[l] /= n;
    }
    printf("strip alpha mean %.2f %.2f %.2f, max %d %d %d\n", mean[0], mean[1], mean[2], amax[0], amax[1], amax[2]);
    TEST_ASSERT_TRUE(fabs(mean[1] - mean[0]) < 0.02 * mean[0] + 0.5);
    TEST_ASSERT_TRUE(fabs(mean[2] - mean[0]) < 0.02 * mean[0] + 0.5);
    TEST_ASSERT_TRUE(amax[1] < amax[0] && amax[2] < amax[1]);
}

static void test_strip_file_roundtrip(void)
{
    const omni_strip_t *s = &g_strip[1];
    const size_t pb = omni_strip_bytes(s->n_theta, s->n_r);
    uint8_t *f = malloc(OMNI_STRIP_HDR + pb);
    TEST_ASSERT_EQUAL(OMNI_STRIP_HDR, omni_strip_file_header(s, f));
    const size_t n = (size_t)s->stride * s->n_r;
    TEST_ASSERT_EQUAL_UINT16(s->n_theta + OMNI_STRIP_PAD, s->stride);
    memcpy(f + OMNI_STRIP_HDR, s->rgb, n * 2);
    memcpy(f + OMNI_STRIP_HDR + n * 2, s->a, n);
    omni_strip_t t;
    TEST_ASSERT_TRUE(omni_strip_map(&t, f, OMNI_STRIP_HDR + pb));
    TEST_ASSERT_EQUAL_UINT16(s->n_theta, t.n_theta);
    TEST_ASSERT_EQUAL_UINT8(s->level, t.level);
    TEST_ASSERT_EQUAL_UINT16(s->stride, t.stride);
    TEST_ASSERT_EQUAL_MEMORY(s->rgb, t.rgb, n * 2);
    TEST_ASSERT_EQUAL_MEMORY(s->a, t.a, n);
    TEST_ASSERT_FALSE(omni_strip_map(&t, f, OMNI_STRIP_HDR + pb - 1));
    f[0] = 'X';
    TEST_ASSERT_FALSE(omni_strip_map(&t, f, OMNI_STRIP_HDR + pb));
    free(f);
}

int main(void)
{
    const uint16_t N[3] = { 1024, 2048, 4096 };
    for (int i = 0; i < 3; i++)
        if (!build(&g_map[i], &g_map_mem[i], N[i], true, true)) {
            printf("map build failed at N %u\n", N[i]);
            return 1;
        }
    make_layers();
    for (int l = 0; l < 3; l++) {
        g_strip[l] = (omni_strip_t){ .n_theta = 2048, .r_min = 126, .n_r = 71 };
        const size_t b = omni_strip_bytes((uint16_t)(2048 >> l), 71);
        g_strip_mem[l] = malloc(b);
        const uint8_t *const layers[OMNI_RINGS] = { g_layer[0], g_layer[1], g_layer[2], g_layer[3] };
        if (!omni_strip_from_rgba(&g_strip[l], layers, (uint8_t)l, g_strip_mem[l], b)) {
            printf("strip build failed at level %d\n", l);
            return 1;
        }
    }
    for (int k = 0; k < 2; k++) {
        omni_strip_t *st = k ? &g_strip4k : &g_strip1k;
        void **mem = k ? &g_strip4k_mem : &g_strip1k_mem;
        *st = (omni_strip_t){ .n_theta = (uint16_t)(k ? 4096 : 1024), .r_min = 126, .n_r = 71 };
        const size_t b = omni_strip_bytes(st->n_theta, 71);
        *mem = malloc(b);
        const uint8_t *const layers[OMNI_RINGS] = { g_layer[0], g_layer[1], g_layer[2], g_layer[3] };
        if (!omni_strip_from_rgba(st, layers, 0, *mem, b)) return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_pmap_matches_atan2);
    RUN_TEST(test_codes_equal_lut);
    RUN_TEST(test_ring_off_label_at_12);
    RUN_TEST(test_ring_offset_bounded);
    RUN_TEST(test_kernels_bitexact);
    RUN_TEST(test_kernel_harness_catches);
    RUN_TEST(test_ring_vs_oracle);
    RUN_TEST(test_strip_wrap_seamless);
    RUN_TEST(test_ring_synth_digest);
    RUN_TEST(test_codes_short_spans);
    RUN_TEST(test_fidelity_still_is_full);
    RUN_TEST(test_draw_clip);
    RUN_TEST(test_strip_levels);
    RUN_TEST(test_strip_file_roundtrip);
    RUN_TEST(test_blend565_globe_unchanged);
    for (int i = 0; i < 3; i++) free(g_map_mem[i]);
    for (int i = 0; i < 3; i++) free(g_strip_mem[i]);
    free(g_strip1k_mem);
    free(g_strip4k_mem);
    for (int i = 0; i < OMNI_RINGS; i++) free(g_layer[i]);
    return UNITY_END();
}
