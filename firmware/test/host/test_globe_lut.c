#include "unity.h"
#include "omni/globe.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define PI 3.14159265358979323846
#define D2R (PI / 180.0)

static const omni_globe_cfg_t LIVE = { .size = 286, .diam = 262, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO, .tex_w = 1024 };
static const omni_globe_cfg_t SPIN = { .size = 350, .diam = 316, .lat0_deg = 14, .roll_deg = 23.44f,
                                       .proj = OMNI_PROJ_ORTHO, .tex_w = 1024 };
static const omni_globe_cfg_t POLAR = { .size = 286, .diam = 262, .proj = OMNI_PROJ_POLAR, .max_colat_deg = 145,
                                        .tex_w = 1024 };

static void *g_mem;

static void build(omni_globe_lut_t *lut, const omni_globe_cfg_t *cfg)
{
    size_t bytes = omni_globe_lut_bytes(cfg);
    TEST_ASSERT_TRUE(bytes > 0);
    free(g_mem);
    g_mem = malloc(bytes + 1);
    TEST_ASSERT_NOT_NULL(g_mem);
    /* Deliberately misaligned: the build aligns the planes itself. */
    TEST_ASSERT_TRUE(omni_globe_lut_build(lut, cfg, (char *)g_mem + 1, bytes));
}

/* Plane index of box pixel (x, y), or -1 if it is outside the LUT. */
static long entry_at(const omni_globe_lut_t *lut, int x, int y)
{
    for (unsigned i = 0; i < lut->nspans; i++) {
        const omni_globe_span_t *s = &lut->span[i];
        if (s->y == y && x >= s->x0 && x < s->x0 + s->n) return (long)(s->off + (uint32_t)(x - s->x0));
    }
    return -1;
}

static long entry_at_project(const omni_globe_lut_t *lut, const float p[3])
{
    double c = lut->cfg.size / 2.0, R = lut->cfg.diam / 2.0;
    return entry_at(lut, (int)floor(c + p[0] * R), (int)floor(c + p[1] * R));
}

static int mu_at(const omni_globe_lut_t *lut, long k, const omni_globe_sun_t *s)
{
    return ((lut->a[k] * s->sd) >> 14) + ((lut->b[k] * s->k1) >> 14) - ((lut->c[k] * s->k2) >> 14);
}

static void check_project(const omni_globe_cfg_t *cfg, double lat, double lon, double lon0, float x, float y, float z)
{
    float o[3];
    omni_globe_project(cfg, lat, lon, lon0, o);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, x, o[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, y, o[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, z, o[2]);
}

static void test_project_matches_mockup(void)
{
    check_project(&LIVE, 39.96, -83, -83, 0, -0.30835f, 0.95127f);
    check_project(&LIVE, 0, 0, -83, 0.99255f, 0.04565f, 0.11300f);
    check_project(&SPIN, 90, 0, 0, -0.38597f, -0.89022f, 0.24192f);
    check_project(&SPIN, 0, 30, 0, 0.54208f, -0.00667f, 0.84030f);
    check_project(&POLAR, 45, 90, 0, -0.31034f, 0, 1);
}

static void test_lut_bytes_budget(void)
{
    omni_globe_cfg_t c = LIVE;
    TEST_ASSERT_TRUE(omni_globe_lut_bytes(&c) > 0);
    TEST_ASSERT_TRUE(omni_globe_lut_bytes(&c) <= 900000);
    c.diam = 316; c.size = 350;
    TEST_ASSERT_TRUE(omni_globe_lut_bytes(&c) > 0);
    TEST_ASSERT_TRUE(omni_globe_lut_bytes(&c) <= 1300000);
    c.tex_w = 1000;   /* not a power of two */
    TEST_ASSERT_EQUAL(0, omni_globe_lut_bytes(&c));
}

static void test_lut_span_alignment(void)
{
    const omni_globe_cfg_t *cfgs[] = { &LIVE, &SPIN, &POLAR };
    for (int ci = 0; ci < 3; ci++) {
        omni_globe_lut_t lut;
        build(&lut, cfgs[ci]);
        TEST_ASSERT_EQUAL(0, lut.n % 8);
        const void *planes[] = { lut.u, lut.row, lut.a, lut.b, lut.c, lut.limb, lut.rim, lut.alpha };
        for (int p = 0; p < 8; p++) TEST_ASSERT_EQUAL(0, (uintptr_t)planes[p] & 15);
        uint32_t covered = 0;
        for (unsigned i = 0; i < lut.nspans; i++) {
            const omni_globe_span_t *s = &lut.span[i];
            TEST_ASSERT_EQUAL(0, s->off % 8);
            TEST_ASSERT_TRUE(s->n > 0);
            uint32_t end = i + 1 < lut.nspans ? lut.span[i + 1].off : lut.n;
            TEST_ASSERT_TRUE(s->off + (uint32_t)s->n <= end);
            TEST_ASSERT_TRUE(end - s->off < (uint32_t)s->n + 8);
            for (uint32_t k = s->off + (uint32_t)s->n; k < end; k++) TEST_ASSERT_EQUAL(0, lut.alpha[k]);
            covered += (uint32_t)s->n;
        }
        TEST_ASSERT_TRUE(covered > 0);
    }
}

static void test_lut_coverage(void)
{
    const omni_globe_cfg_t *cfgs[] = { &LIVE, &SPIN, &POLAR };
    for (int ci = 0; ci < 3; ci++) {
        omni_globe_lut_t lut;
        build(&lut, cfgs[ci]);
        double c = lut.cfg.size / 2.0, R = lut.cfg.diam / 2.0;
        long k = entry_at(&lut, (int)c, (int)c);
        TEST_ASSERT_TRUE(k >= 0);
        TEST_ASSERT_EQUAL(255, lut.alpha[k]);
        int limb = 0;
        for (unsigned i = 0; i < lut.nspans; i++) {
            const omni_globe_span_t *s = &lut.span[i];
            for (int j = 0; j < s->n; j++) {
                double d = hypot(s->x0 + j + 0.5 - c, s->y + 0.5 - c);
                uint8_t a = lut.alpha[s->off + (uint32_t)j];
                TEST_ASSERT_TRUE(d <= R + 1);
                if (d > R + 0.5) TEST_ASSERT_EQUAL(0, a);
                if (d < R - 0.5) TEST_ASSERT_EQUAL(255, a);
                if (d > R - 0.49 && d < R + 0.49) {
                    TEST_ASSERT_TRUE(a > 0 && a < 255);
                    limb++;
                }
            }
        }
        TEST_ASSERT_TRUE(limb > (int)(4 * R));   /* the whole circumference */
    }
}

static double wrap_texels(double d, double w)
{
    d = fmod(d, w);
    if (d > w / 2) d -= w;
    if (d < -w / 2) d += w;
    return d;
}

/* 200 visible samples per projection, away from the limb and the poles (where a pixel spans many texels).
 * tex_w 256: about 3 px per texel at the centre, so the pixel quantisation stays under a texel. */
static void check_lut_vs_project(omni_globe_cfg_t cfg)
{
    cfg.tex_w = 256;
    omni_globe_lut_t lut;
    build(&lut, &cfg);
    const double w = cfg.tex_w, h = w / 2;
    unsigned seed = 12345;
    int got = 0;
    while (got < 200) {
        seed = seed * 1103515245u + 12345u;
        double lat = ((seed >> 8) % 12000) / 100.0 - 60.0;
        seed = seed * 1103515245u + 12345u;
        double lon = ((seed >> 8) % 36000) / 100.0 - 180.0;
        float p[3];
        omni_globe_project(&cfg, lat, lon, 0, p);
        if (cfg.proj == OMNI_PROJ_ORTHO ? p[2] < 0.5f : (90 - lat) / cfg.max_colat_deg > 0.95) continue;
        if (cfg.proj == OMNI_PROJ_POLAR && lat > 70) continue;
        long k = entry_at_project(&lut, p);
        TEST_ASSERT_TRUE(k >= 0);
        TEST_ASSERT_EQUAL(255, lut.alpha[k]);
        double u = lon * D2R * w / (2 * PI);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, 0, (float)wrap_texels(lut.u[k] / 16.0 - u, w));
        double row = floor((0.5 - lat / 180.0) * h);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, (float)row, (float)lut.row[k]);
        got++;
    }
}

static void test_lut_matches_project(void)
{
    check_lut_vs_project(LIVE);
    check_lut_vs_project(SPIN);
    check_lut_vs_project(POLAR);
}

static void test_mu_subsolar(void)
{
    omni_globe_cfg_t cfg = { .size = 64, .diam = 64, .lat0_deg = 0, .proj = OMNI_PROJ_ORTHO, .tex_w = 512 };
    omni_globe_lut_t lut;
    build(&lut, &cfg);
    const double lon0 = 37;
    omni_globe_sun_t s;
    omni_globe_sun(&s, 10, lon0, lon0);
    long k = entry_at(&lut, 32, 32);
    TEST_ASSERT_TRUE(k >= 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)cos(10 * D2R), mu_at(&lut, k, &s) / 16384.0f);
    float p[3];
    omni_globe_project(&cfg, 10, lon0, lon0, p);
    k = entry_at_project(&lut, p);
    TEST_ASSERT_TRUE(k >= 0);
    TEST_ASSERT_TRUE(mu_at(&lut, k, &s) / 16384.0 >= 0.995);

    /* Off-centre sun (pins the sign of the k2 term): the subsolar pixel is still the brightest. */
    omni_globe_sun(&s, 10, lon0 + 40, lon0);
    omni_globe_project(&cfg, 10, lon0 + 40, lon0, p);
    k = entry_at_project(&lut, p);
    TEST_ASSERT_TRUE(k >= 0);
    TEST_ASSERT_TRUE(mu_at(&lut, k, &s) / 16384.0 >= 0.99);
}

static void test_studio_mu(void)
{
    omni_globe_cfg_t cfg = SPIN;
    double L[3] = { -0.52, 0.48, 0.70 }, n = sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
    cfg.studio = true;
    for (int i = 0; i < 3; i++) cfg.light[i] = (float)(L[i] / n);
    omni_globe_lut_t lut;
    build(&lut, &cfg);
    omni_globe_sun_t s;
    omni_globe_sun_studio(&s);
    TEST_ASSERT_EQUAL(1 << 14, s.sd);
    TEST_ASSERT_EQUAL(0, s.k1);
    TEST_ASSERT_EQUAL(0, s.k2);
    double r = cfg.roll_deg * D2R;
    double xs = cfg.light[0] * cos(r) - cfg.light[1] * sin(r), ys = cfg.light[0] * sin(r) + cfg.light[1] * cos(r);
    double c = cfg.size / 2.0, R = cfg.diam / 2.0;
    long k = entry_at(&lut, (int)floor(c + xs * R), (int)floor(c - ys * R));
    TEST_ASSERT_TRUE(k >= 0);
    TEST_ASSERT_TRUE(mu_at(&lut, k, &s) / 16384.0 >= 0.98);
    for (uint32_t j = 0; j < lut.n; j++) {
        TEST_ASSERT_EQUAL(0, lut.b[j]);
        TEST_ASSERT_EQUAL(0, lut.c[j]);
    }
}

static void test_sun_now_unset_lights_disc(void)
{
    omni_globe_lut_t lut;
    build(&lut, &LIVE);
    omni_globe_sun_t s;
    TEST_ASSERT_FALSE(omni_globe_sun_now(&s, NULL, 22, -83));
    int full = 0;
    for (uint32_t k = 0; k < lut.n; k++) {
        if (lut.alpha[k] != 255) continue;
        TEST_ASSERT_TRUE(mu_at(&lut, k, &s) >= 0);
        full++;
    }
    TEST_ASSERT_TRUE(full > 50000);

    omni_sun_t sun;
    omni_sun_at(1792670400LL, &sun);
    omni_globe_sun_t want;
    omni_globe_sun(&want, sun.decl_deg, sun.subsolar_lon, -83);
    TEST_ASSERT_TRUE(omni_globe_sun_now(&s, &sun, 22, -83));
    TEST_ASSERT_EQUAL(want.sd, s.sd);
    TEST_ASSERT_EQUAL(want.k1, s.k1);
    TEST_ASSERT_EQUAL(want.k2, s.k2);
}

/* The stepped build (begin, then rows in uneven batches, as the device worker runs it) gives exactly the bytes of
 * the one-call build, for ortho, rolled studio and polar; and begin refuses a short buffer. */
static void test_lut_build_in_steps(void)
{
    const omni_globe_cfg_t STUDIO = { .size = 350, .diam = 316, .lat0_deg = 14, .roll_deg = 23.44f,
                                      .proj = OMNI_PROJ_ORTHO, .studio = true,
                                      .light = { -0.52241f, 0.48222f, 0.70324f }, .tex_w = 512 };
    const omni_globe_cfg_t *CFGS[] = { &LIVE, &SPIN, &POLAR, &STUDIO };
    for (unsigned c = 0; c < sizeof CFGS / sizeof CFGS[0]; c++) {
        const size_t bytes = omni_globe_lut_bytes(CFGS[c]);
        uint8_t *a = malloc(bytes), *b = malloc(bytes);
        memset(a, 0x5a, bytes);
        memset(b, 0xa5, bytes);   /* every byte the layout covers is written, whatever was there */
        omni_globe_lut_t la, lb;
        TEST_ASSERT_TRUE(omni_globe_lut_build(&la, CFGS[c], a, bytes));
        TEST_ASSERT_FALSE(omni_globe_lut_begin(&lb, CFGS[c], b, bytes - 1));
        TEST_ASSERT_TRUE(omni_globe_lut_begin(&lb, CFGS[c], b, bytes));
        omni_globe_lut_rows(&lb, 5, 5);   /* empty */
        for (int py = 0; py < CFGS[c]->diam; py += 7) omni_globe_lut_rows(&lb, py, py + 7);
        const size_t skip = (size_t)((uint8_t *)la.span - a);   /* the alignment pad (the same for both) */
        TEST_ASSERT_EQUAL(skip, (size_t)((uint8_t *)lb.span - b));
        TEST_ASSERT_EQUAL(la.nspans, lb.nspans);
        TEST_ASSERT_EQUAL(la.n, lb.n);
        TEST_ASSERT_EQUAL_MEMORY(a + skip, b + skip, bytes - 15);
        free(a);
        free(b);
    }
}

/* ---- The original double build (Task 2, fce5428), kept as the reference the float build is held to. */

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint8_t dx255(double v) { return (uint8_t)lround(clampd(v, 0, 1) * 255.0); }
static int16_t dq14(double v) { return (int16_t)lround(clampd(v, -1, 1) * 16384.0); }

static void ref_rows_double(omni_globe_lut_t *lut)
{
    const omni_globe_cfg_t *cfg = &lut->cfg;
    const int D = cfg->diam, box = (cfg->size - cfg->diam) / 2, tex_h = cfg->tex_w / 2;
    const double R = D / 2.0;
    const double lat0 = cfg->lat0_deg * D2R, roll = cfg->roll_deg * D2R;
    const double cl = cos(lat0), sl = sin(lat0), cr = cos(roll), sr = sin(roll);
    const double u_scale = cfg->tex_w / (2 * PI) * 16.0;
    const bool polar = cfg->proj == OMNI_PROJ_POLAR;
    const double max_colat = (cfg->max_colat_deg > 0 ? cfg->max_colat_deg : 150.0) * D2R;
    const double Ld[3] = { cfg->light[0], cfg->light[1], cfg->light[2] };
    for (unsigned si = 0; si < lut->nspans; si++) {
        const omni_globe_span_t *s = &lut->span[si];
        const int py = s->y - box, x0 = s->x0 - box;
        for (int j = 0; j < s->n; j++) {
            const uint32_t k = s->off + (uint32_t)j;
            double xs = (x0 + j + 0.5 - R) / R, ys = -(py + 0.5 - R) / R, r = hypot(xs, ys);
            const double cov = clampd((1 - r) * R + 0.5, 0, 1);
            if (r > 0.9995) { xs *= 0.9995 / r; ys *= 0.9995 / r; r = 0.9995; }
            double lat, lon_rel;
            if (polar) {
                lat = PI / 2 - r * max_colat;
                lon_rel = -(atan2(-ys, xs) + PI / 2);
                if (lon_rel < -PI) lon_rel += 2 * PI;
                lut->limb[k] = dx255(1 - 0.28 * pow(r, 6));
                lut->rim[k] = 0;
            } else {
                const double xv = xs * cr + ys * sr, yv = -xs * sr + ys * cr;
                const double z = sqrt(fmax(0, 1 - xv * xv - yv * yv));
                const double Y = yv * cl + z * sl, Zf = -yv * sl + z * cl;
                lat = asin(clampd(Y, -1, 1));
                lon_rel = atan2(xv, Zf);
                lut->limb[k] = dx255(0.42 + 0.58 * pow(z, 0.55));
                lut->rim[k] = dx255(pow(1 - z, 2.6));
                if (cfg->studio) lut->a[k] = dq14(xv * Ld[0] + yv * Ld[1] + z * Ld[2]);
            }
            lut->u[k] = (int16_t)lround(lon_rel * u_scale);
            lut->row[k] = (uint16_t)clampd(floor((0.5 - lat / PI) * tex_h), 0, tex_h - 1);
            if (!cfg->studio) {
                lut->a[k] = dq14(sin(lat));
                lut->b[k] = dq14(cos(lat) * cos(lon_rel));
                lut->c[k] = dq14(cos(lat) * sin(lon_rel));
            }
            lut->alpha[k] = dx255(cov);
        }
    }
}

/* The float build (R58/O0) against the double one, entry by entry, on every face configuration and both texture
 * widths: no plane is off by more than 1 step (u by 1 Q4 = 1/16 texel, row by 1 texel row at a boundary, a/b/c by
 * 1/16384, limb/rim/alpha by 1/255), and fewer than 1 entry in 200 differs at all (float rounding near a step);
 * the spans are identical. */
static void test_lut_float_vs_double(void)
{
    const omni_globe_cfg_t ANALOG = { .size = 316, .diam = 300, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO, .tex_w = 512 };
    const omni_globe_cfg_t STUDIO = { .size = 350, .diam = 316, .lat0_deg = 14, .roll_deg = 23.44f,
                                      .proj = OMNI_PROJ_ORTHO, .studio = true,
                                      .light = { -0.52241f, 0.48222f, 0.70324f }, .tex_w = 512 };
    const omni_globe_cfg_t *CFGS[] = { &LIVE, &SPIN, &POLAR, &ANALOG, &STUDIO };
    for (unsigned c = 0; c < sizeof CFGS / sizeof CFGS[0]; c++)
        for (int tw = 512; tw <= 1024; tw *= 2) {
            omni_globe_cfg_t cfg = *CFGS[c];
            cfg.tex_w = (uint16_t)tw;
            const size_t bytes = omni_globe_lut_bytes(&cfg);
            uint8_t *mf = malloc(bytes), *md = malloc(bytes);
            omni_globe_lut_t lf, ld;
            TEST_ASSERT_TRUE(omni_globe_lut_build(&lf, &cfg, mf, bytes));
            TEST_ASSERT_TRUE(omni_globe_lut_begin(&ld, &cfg, md, bytes));
            ref_rows_double(&ld);
            TEST_ASSERT_EQUAL(ld.nspans, lf.nspans);
            TEST_ASSERT_EQUAL_MEMORY(ld.span, lf.span, ld.nspans * sizeof *ld.span);
            const int32_t umod = 16 * tw;
            uint32_t ndiff[8] = { 0 }, total = 0;
            int32_t worst[8] = { 0 };
            for (unsigned si = 0; si < lf.nspans; si++)
                for (uint32_t k = lf.span[si].off; k < lf.span[si].off + (uint32_t)lf.span[si].n; k++) {
                    int32_t du = (lf.u[k] - ld.u[k]) % umod;   /* the +-pi seam: the same column */
                    if (du > umod / 2) du -= umod;
                    if (du < -umod / 2) du += umod;
                    const int32_t d[8] = { du, lf.row[k] - ld.row[k], lf.a[k] - ld.a[k], lf.b[k] - ld.b[k],
                                           lf.c[k] - ld.c[k], lf.limb[k] - ld.limb[k], lf.rim[k] - ld.rim[k],
                                           lf.alpha[k] - ld.alpha[k] };
                    for (int p = 0; p < 8; p++) {
                        const int32_t a = d[p] < 0 ? -d[p] : d[p];
                        if (a) ndiff[p]++;
                        if (a > worst[p]) worst[p] = a;
                    }
                    total++;
                }
            printf("float vs double, cfg %u tex %d (%u px): differing u %u row %u a %u b %u c %u limb %u rim %u alpha %u;"
                   " worst %d %d %d %d %d %d %d %d\n", c, tw, (unsigned)total, (unsigned)ndiff[0], (unsigned)ndiff[1],
                   (unsigned)ndiff[2], (unsigned)ndiff[3], (unsigned)ndiff[4], (unsigned)ndiff[5], (unsigned)ndiff[6],
                   (unsigned)ndiff[7], (int)worst[0], (int)worst[1], (int)worst[2], (int)worst[3], (int)worst[4],
                   (int)worst[5], (int)worst[6], (int)worst[7]);
            for (int p = 0; p < 8; p++) {
                TEST_ASSERT_TRUE(worst[p] <= 1);
                TEST_ASSERT_TRUE(ndiff[p] * 200 < total);
            }
            free(mf);
            free(md);
        }
}

/* limb_kind 1 (the Omni): limb 0.62 + 0.38 z^0.5, rim 0; kind 0 untouched */
static void test_limb_kind_omni(void)
{
    omni_globe_cfg_t c = { .size = 224, .diam = 224, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO, .studio = true,
                           .light = { -0.42f, 0.52f, 0.74f }, .limb_kind = 1, .tex_w = 512 };
    omni_globe_cfg_t c0 = c;
    c0.limb_kind = 0;
    size_t b = omni_globe_lut_bytes(&c);
    void *m1 = malloc(b), *m0 = malloc(b);
    omni_globe_lut_t l1, l0;
    TEST_ASSERT_EQUAL(b, omni_globe_lut_bytes(&c0));
    TEST_ASSERT_TRUE(omni_globe_lut_build(&l1, &c, m1, b));
    TEST_ASSERT_TRUE(omni_globe_lut_build(&l0, &c0, m0, b));
    const double rad = 112;
    for (unsigned s = 0; s < l1.nspans; s++)
        for (int j = 0; j < l1.span[s].n; j++) {
            const uint32_t k = l1.span[s].off + (uint32_t)j;
            const double x = (l1.span[s].x0 + j + 0.5 - rad) / rad, y = (l1.span[s].y + 0.5 - rad) / rad;
            const double r2 = fmin(x * x + y * y, 0.9995 * 0.9995), z = sqrt(1 - r2);
            TEST_ASSERT_INT_WITHIN(1, (int)lround((0.62 + 0.38 * sqrt(z)) * 255), l1.limb[k]);
            TEST_ASSERT_EQUAL(0, l1.rim[k]);
            /* only limb and rim differ from kind 0 */
            TEST_ASSERT_EQUAL(l0.u[k], l1.u[k]);
            TEST_ASSERT_EQUAL(l0.row[k], l1.row[k]);
            TEST_ASSERT_EQUAL(l0.a[k], l1.a[k]);
            TEST_ASSERT_EQUAL(l0.alpha[k], l1.alpha[k]);
        }
    free(m1);
    free(m0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_project_matches_mockup);
    RUN_TEST(test_lut_bytes_budget);
    RUN_TEST(test_lut_span_alignment);
    RUN_TEST(test_lut_coverage);
    RUN_TEST(test_lut_matches_project);
    RUN_TEST(test_mu_subsolar);
    RUN_TEST(test_studio_mu);
    RUN_TEST(test_sun_now_unset_lights_disc);
    RUN_TEST(test_lut_build_in_steps);
    RUN_TEST(test_lut_float_vs_double);
    RUN_TEST(test_limb_kind_omni);
    free(g_mem);
    return UNITY_END();
}
