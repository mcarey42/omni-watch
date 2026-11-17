#include "unity.h"
#include "omni/globe.h"
#include "omni/globe_check.h"
#include "omni/globe_compact.h"
#include "omni/sun.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define PI 3.14159265358979323846
#define D2R (PI / 180.0)
#define TW 256
#define TH 128
#define S 112
#define DIAM 96
#define WHEN 1792669200LL   /* 2026-10-22 11:40 UTC */

static const omni_globe_cfg_t CFG = { .size = S, .diam = DIAM, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO, .tex_w = TW };
static const omni_globe_look_t LIVE = { .gain = 1.0f, .night = 1.25f, .atmo = 1.0f };
static const omni_globe_look_t DIM = { .gain = 0.6f, .night = 1.1f, .atmo = 0.8f };   /* gain != 1 (spin-up, mockup 562) */

static uint32_t g_px[TW * TH];
static omni_globe_tex_t g_tex = { TW, TH, g_px };
static omni_globe_shade_t g_shade, g_shade_dim;
static omni_globe_lut_t g_lut;
static void *g_lut_mem;

static uint16_t pack(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static int ex_r(uint16_t c) { int v = c >> 11; return (v << 3) | (v >> 2); }
static int ex_g(uint16_t c) { int v = (c >> 5) & 63; return (v << 2) | (v >> 4); }
static int ex_b(uint16_t c) { int v = c & 31; return (v << 3) | (v >> 2); }

/* Smooth and continuous across +-180 deg (R48/C5, D8). Texel centres. */
static void make_texture(void)
{
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            double lon = ((x + 0.5) / TW * 360 - 180) * D2R, lat = (90 - (y + 0.5) / TH * 180) * D2R;
            uint16_t day = pack((int)lround(128 + 100 * cos(lon)), (int)lround(128 + 100 * sin(lon)),
                                (int)lround(128 + 100 * sin(lat)));
            uint16_t night = pack((int)lround(40 + 30 * cos(3 * lon)), 30, 10);
            g_px[y * TW + x] = day | (uint32_t)night << 16;
        }
}

static omni_globe_clut_t g_clut;
static void *g_clut_mem;
static bool g_use_compact;   /* check_vs_oracle* render through the compact LUT (Task 11) */
static omni_globe_cspan_fn g_ckernel = omni_globe_cspan_ref;   /* ... with this kernel */
static bool g_oracle_lin;    /* the oracle samples linear in u between texel centres (R53 step 6a) */

static void setup_compact(void)
{
    const size_t cb = omni_globe_compact_bytes(&g_lut);
    g_clut_mem = malloc(cb);
    TEST_ASSERT_TRUE(omni_globe_compact(&g_clut, &g_lut, g_clut_mem, cb));
}

static void setup_all(void)
{
    make_texture();
    omni_globe_shade_build(&g_shade, &LIVE);
    omni_globe_shade_build(&g_shade_dim, &DIM);
    size_t bytes = omni_globe_lut_bytes(&CFG);
    g_lut_mem = malloc(bytes);
    TEST_ASSERT_TRUE(omni_globe_lut_build(&g_lut, &CFG, g_lut_mem, bytes));
    setup_compact();
}

static omni_globe_frame_t frame(double lon0, const uint16_t *under)
{
    omni_sun_t sun;
    omni_sun_at(WHEN, &sun);
    omni_globe_frame_t f = { .lut = &g_lut, .tex = &g_tex, .shade = &g_shade, .lon0_deg = lon0, .under = under,
                             .over = NULL, .fade = 255 };
    omni_globe_sun_now(&f.sun, &sun, CFG.lat0_deg, lon0);
    return f;
}

/* ---- Float oracle: the mockup's Globe.prototype.build pixel geometry and render() (faces-practical.js
 * 195-226), composited onto the base as the canvas does (non-premultiplied source-over, 8-bit). */

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static void oracle(const omni_globe_look_t *look, double lon0, const uint16_t *under, uint16_t *out)
{
    const double R = DIAM / 2.0, lat0 = CFG.lat0_deg * D2R, cl = cos(lat0), sl = sin(lat0);
    const double gain = look->gain, nightGain = look->night * gain, atmo = look->atmo;
    const double W = TW, uScale = W / (2 * PI), uOff = lon0 * D2R * (W / (2 * PI)) + W / 2;
    omni_sun_t sun;
    omni_sun_at(WHEN, &sun);
    const double dec = sun.decl_deg * D2R, sd = sin(dec), cd = cos(dec), dl = (lon0 - sun.subsolar_lon) * D2R;
    const double cD = cos(dl), sD = sin(dl);
    const int box = (S - DIAM) / 2;
    for (int i = 0; i < S * S; i++) out[i] = under ? under[i] : 0;
    for (int py = 0; py < DIAM; py++)
        for (int px = 0; px < DIAM; px++) {
            double xs = (px + 0.5 - R) / R, ys = -(py + 0.5 - R) / R, r = hypot(xs, ys);
            if (r > 1 + 1 / R) continue;
            const double cov = clampd((1 - r) * R + 0.5, 0, 1);
            if (r > 0.9995) { xs *= 0.9995 / r; ys *= 0.9995 / r; r = 0.9995; }
            const double xv = xs, yv = ys, z = sqrt(fmax(0, 1 - xv * xv - yv * yv));
            const double Y = yv * cl + z * sl, Zf = -yv * sl + z * cl;
            const double lat = asin(clampd(Y, -1, 1)), lonRel = atan2(xv, Zf);
            const double limb = 0.42 + 0.58 * pow(z, 0.55), rim = pow(1 - z, 2.6);
            const int row = (int)clampd(floor((0.5 - lat / PI) * TH), 0, TH - 1);
            const int alpha = (int)lround(cov * 255);

            double u = lonRel * uScale + uOff;
            u -= floor(u / W) * W;
            double dr, dg, db, nr, ngg, nb;
            if (g_oracle_lin) {   /* between the centres of texels x0 and x0 + 1 (wrapping) */
                const double p = u - 0.5, fl = floor(p), fr = p - fl;
                const int x0 = ((int)fl % TW + TW) % TW, x1 = (x0 + 1) % TW;
                const uint32_t t0 = g_px[row * TW + x0], t1 = g_px[row * TW + x1];
                const uint16_t d0 = (uint16_t)t0, d1 = (uint16_t)t1, n0 = (uint16_t)(t0 >> 16), n1 = (uint16_t)(t1 >> 16);
                dr = ex_r(d0) * (1 - fr) + ex_r(d1) * fr;
                dg = ex_g(d0) * (1 - fr) + ex_g(d1) * fr;
                db = ex_b(d0) * (1 - fr) + ex_b(d1) * fr;
                nr = ex_r(n0) * (1 - fr) + ex_r(n1) * fr;
                ngg = ex_g(n0) * (1 - fr) + ex_g(n1) * fr;
                nb = ex_b(n0) * (1 - fr) + ex_b(n1) * fr;
            } else {
                const uint32_t t = g_px[row * TW + (int)u];
                const uint16_t dy = (uint16_t)t, ng = (uint16_t)(t >> 16);
                dr = ex_r(dy); dg = ex_g(dy); db = ex_b(dy); nr = ex_r(ng); ngg = ex_g(ng); nb = ex_b(ng);
            }
            const double mu = sin(lat) * sd + cos(lat) * cd * (cos(lonRel) * cD - sin(lonRel) * sD);
            double x = (mu + 0.09) * 5.5;
            x = x < 0 ? 0 : x > 1 ? 1 : x;
            const double s = x * x * (3 - 2 * x);
            const double lit = s * (0.30 + 0.70 * sqrt(mu > 0 ? mu : 0)) * gain;
            const double nk = 1 - s, tw = s * nk * 4, L = limb;
            double cr = (dr * lit * (1 + 0.22 * tw) + nr * nk * nightGain + dr * nk * 0.05 * gain) * L;
            double cg = (dg * lit * (1 + 0.04 * tw) + ngg * nk * nightGain + dg * nk * 0.06 * gain) * L;
            double cb = (db * lit * (1 - 0.18 * tw) + nb * nk * nightGain * 0.9 + db * nk * 0.09 * gain) * L;
            const double a = rim * atmo * (0.18 + 0.82 * s) * gain;
            cr += 60 * a; cg += 135 * a; cb += 255 * a;

            const int k = (py + box) * S + px + box;
            const uint16_t base = out[k];
            const double ch[3] = { clampd(rint(cr), 0, 255), clampd(rint(cg), 0, 255), clampd(rint(cb), 0, 255) };
            const double bs[3] = { ex_r(base), ex_g(base), ex_b(base) };
            int o[3];
            for (int c = 0; c < 3; c++) o[c] = (int)clampd(rint(ch[c] * alpha / 255.0 + bs[c] * (1 - alpha / 255.0)), 0, 255);
            out[k] = pack(o[0], o[1], o[2]);
        }
}

/* Largest per-channel difference in 565 units; *n_over2 counts pixels with a channel off by more than 2. */
static int compare(const uint16_t *a, const uint16_t *b, int *n_over2)
{
    int worst = 0;
    *n_over2 = 0;
    for (int i = 0; i < S * S; i++) {
        int d[3] = { abs((a[i] >> 11) - (b[i] >> 11)), abs(((a[i] >> 5) & 63) - ((b[i] >> 5) & 63)),
                     abs((a[i] & 31) - (b[i] & 31)) };
        int m = d[0] > d[1] ? d[0] : d[1];
        m = m > d[2] ? m : d[2];
        if (m > 2) (*n_over2)++;
        if (m > worst) worst = m;
    }
    return worst;
}

static void make_under(uint16_t *u)
{
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) u[y * S + x] = pack(x * 2, y * 2, (x + y) & 255);
}

/* ---- Shade table */

static double smooth(double mu)
{
    double x = (mu + 0.09) * 5.5;
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}

static void check_shade(const omni_globe_shade_t *sh, const omni_globe_look_t *look)
{
    const double g = look->gain, nt = look->night * g, at = look->atmo * g;
    for (int i = 0; i < OMNI_SHADE_N; i++) {
        const double mu = (i + 0.5) / 256 - 1, s = smooth(mu), nk = 1 - s, tw = 4 * s * nk;
        const double lit = s * (0.30 + 0.70 * sqrt(fmax(mu, 0))) * g;
        const double want[7] = { lit * (1 + 0.22 * tw) + 0.05 * nk * g, lit * (1 + 0.04 * tw) + 0.06 * nk * g,
                                 lit * (1 - 0.18 * tw) + 0.09 * nk * g, nk * nt, nk * nt, 0.9 * nk * nt,
                                 (0.18 + 0.82 * s) * at };
        const omni_shade_e_t *e = &sh->e[i];
        const uint16_t got[7] = { e->dr, e->dg, e->db, e->nr, e->ng, e->nb, e->at };
        for (int c = 0; c < 7; c++) TEST_ASSERT_INT_WITHIN(1, (int)lround(want[c] * 256), got[c]);
    }
}

static void test_shade_curve(void)
{
    check_shade(&g_shade, &LIVE);
    check_shade(&g_shade_dim, &DIM);
    TEST_ASSERT_INT_WITHIN(1, (int)lround(0.6 * 256), g_shade_dim.e[511].dr);   /* gain scales the lit side */
    TEST_ASSERT_INT_WITHIN(1, (int)lround(1.1 * 0.6 * 256), g_shade_dim.e[0].nr);
    TEST_ASSERT_INT_WITHIN(1, (int)lround(0.8 * 0.6 * 256), g_shade_dim.e[511].at);
    const omni_shade_e_t *e = &g_shade.e[128];
    TEST_ASSERT_INT_WITHIN(1, 13, e->dr);    /* 0.050 */
    TEST_ASSERT_INT_WITHIN(1, 15, e->dg);    /* 0.060 */
    TEST_ASSERT_INT_WITHIN(1, 23, e->db);    /* 0.090 */
    TEST_ASSERT_INT_WITHIN(1, 320, e->nr);   /* 1.25 */
    e = &g_shade.e[256];                      /* dusk: warm */
    TEST_ASSERT_INT_WITHIN(1, 59, e->dr);    /* 0.230 */
    TEST_ASSERT_INT_WITHIN(1, 52, e->dg);    /* 0.205 */
    TEST_ASSERT_INT_WITHIN(1, 47, e->db);    /* 0.182 */
    TEST_ASSERT_TRUE(e->dr > e->dg && e->dg > e->db);
    e = &g_shade.e[511];
    TEST_ASSERT_EQUAL(256, e->dr);
    TEST_ASSERT_EQUAL(256, e->dg);
    TEST_ASSERT_EQUAL(256, e->db);
    TEST_ASSERT_EQUAL(0, e->nr);
}

/* ---- Renderer */

static uint16_t g_a[S * S], g_b[S * S], g_under[S * S];


static void check_vs_oracle_look(const omni_globe_look_t *look, const omni_globe_shade_t *sh, double lon0,
                                 const uint16_t *under, int max_all, double frac_within2)
{
    omni_globe_frame_t f = frame(lon0, under);
    f.shade = sh;
    if (g_use_compact) {
        f.clut = &g_clut;
        omni_globe_render_c(&f, g_a, 0, S, g_ckernel, false, NULL);
    } else {
        omni_globe_render(&f, g_a, 0, S, omni_globe_span_ref);
    }
    oracle(look, lon0, under, g_b);
    int over2, worst = compare(g_a, g_b, &over2);
    char msg[96];
    snprintf(msg, sizeof msg, "lon0 %.1f: worst %d, %d px over 2", lon0, worst, over2);
    printf("oracle%s %s%s: %s\n", g_use_compact ? " (compact)" : "", look == &LIVE ? "live" : "dim", under ? " under" : "",
           msg);
    TEST_ASSERT_TRUE_MESSAGE(worst <= max_all, msg);
    TEST_ASSERT_TRUE_MESSAGE(over2 <= (1 - frac_within2) * S * S, msg);
}

static void check_vs_oracle(double lon0, const uint16_t *under, int max_all, double frac_within2)
{
    check_vs_oracle_look(&LIVE, &g_shade, lon0, under, max_all, frac_within2);
}

static void test_render_matches_float_oracle(void)
{
    check_vs_oracle(-83, NULL, 4, 0.99);
    make_under(g_under);
    check_vs_oracle(-83, g_under, 4, 0.99);
    check_vs_oracle_look(&DIM, &g_shade_dim, -83, g_under, 4, 0.99);   /* gain != 1 */
}

static void test_render_rotation_wraps(void)
{
    omni_globe_frame_t f = frame(-170, NULL);
    omni_globe_render(&f, g_a, 0, S, omni_globe_span_ref);
    f = frame(190, NULL);
    omni_globe_render(&f, g_b, 0, S, omni_globe_span_ref);
    TEST_ASSERT_EQUAL_MEMORY(g_a, g_b, sizeof g_a);
    check_vs_oracle(179.9, NULL, 4, 0);   /* the dateline crosses the disc */
}

static void test_render_rows_split_equal(void)
{
    make_under(g_under);
    omni_globe_frame_t f = frame(-83, g_under);
    omni_globe_render(&f, g_a, 0, S, omni_globe_span_ref);
    memset(g_b, 0xa5, sizeof g_b);
    omni_globe_render(&f, g_b, 0, 37, omni_globe_span_ref);
    omni_globe_render(&f, g_b, 37, S, omni_globe_span_ref);
    TEST_ASSERT_EQUAL_MEMORY(g_a, g_b, sizeof g_a);
}

/* Plane entry of box pixel (x, y), or -1 outside the LUT. */
static long entry_at(int x, int y)
{
    for (unsigned i = 0; i < g_lut.nspans; i++) {
        const omni_globe_span_t *s = &g_lut.span[i];
        if (s->y == y && x >= s->x0 && x < s->x0 + s->n) return (long)(s->off + (uint32_t)(x - s->x0));
    }
    return -1;
}

static void test_render_under_over_fade(void)
{
    make_under(g_under);
    omni_globe_frame_t f = frame(-83, g_under);
    omni_globe_render(&f, g_a, 0, S, omni_globe_span_ref);

    /* Outside the disc: the base. Inside: the span kernel applied to the base, so fade 255 is a no-op. */
    memcpy(g_b, g_under, sizeof g_b);
    for (unsigned i = 0; i < g_lut.nspans; i++) {
        const omni_globe_span_t *sp = &g_lut.span[i];
        omni_globe_span_ref(&f, sp, omni_globe_uoff_q4(TW, -83), g_b + sp->y * S + sp->x0);
    }
    TEST_ASSERT_EQUAL_MEMORY(g_b, g_a, sizeof g_a);
    int outside = 0;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            long k = entry_at(x, y);
            if (k >= 0 && g_lut.alpha[k]) continue;
            TEST_ASSERT_EQUAL_HEX16(g_under[y * S + x], g_a[y * S + x]);
            outside++;
        }
    TEST_ASSERT_TRUE(outside > S * S - DIAM * DIAM);

    /* An overlay pixel with alpha 255 replaces; alpha 0 leaves the pixel alone. */
    static uint32_t img[S * S], mem[S * S + 64];
    memset(img, 0, sizeof img);
    img[56 * S + 56] = 0xff3366ccu;   /* disc centre */
    img[2 * S + 3] = 0xff00ff00u;     /* outside the disc */
    img[56 * S + 57] = 0x80ffffffu;   /* half white over the globe */
    omni_overlay_t ov;
    size_t bytes = omni_overlay_bytes(img, S, S, S);
    TEST_ASSERT_TRUE(bytes <= sizeof mem);
    TEST_ASSERT_TRUE(omni_overlay_build(&ov, img, S, S, S, mem, bytes));
    f.over = &ov;
    omni_globe_render(&f, g_b, 0, S, omni_globe_span_ref);
    TEST_ASSERT_EQUAL_HEX16(pack(0x33, 0x66, 0xcc), g_b[56 * S + 56]);
    TEST_ASSERT_EQUAL_HEX16(pack(0, 0xff, 0), g_b[2 * S + 3]);
    const uint16_t g = g_a[56 * S + 57], h = g_b[56 * S + 57];
    TEST_ASSERT_EQUAL((ex_r(g) * 127 + 255 * 129) >> 8 >> 3, h >> 11);   /* alpha' 129 */
    for (int i = 0; i < S * S; i++)
        if (i != 56 * S + 56 && i != 2 * S + 3 && i != 56 * S + 57) TEST_ASSERT_EQUAL_HEX16(g_a[i], g_b[i]);

    /* Fade: 0 is black, 128 about halves every channel. */
    f.fade = 0;
    omni_globe_render(&f, g_b, 0, S, omni_globe_span_ref);
    for (int i = 0; i < S * S; i++) TEST_ASSERT_EQUAL_HEX16(0, g_b[i]);
    f.over = NULL;
    f.fade = 128;
    omni_globe_render(&f, g_b, 0, S, omni_globe_span_ref);
    for (int i = 0; i < S * S; i++) {
        TEST_ASSERT_EQUAL(((ex_r(g_a[i]) * 129) >> 8) >> 3, g_b[i] >> 11);
        TEST_ASSERT_EQUAL(((ex_g(g_a[i]) * 129) >> 8) >> 2, (g_b[i] >> 5) & 63);
    }
}

/* ---- Pinned reference and the bit-exact kernel harness.
 * The oracle tests above only bound the error, and against a smooth texture they cannot see an off-by-one
 * shade bin or texel. These cases render a pseudo-random texture at the face sizes, so any change to
 * omni_globe_span_ref's output changes a digest; check_kernel_bitexact holds any span kernel (Task 11's SIMD)
 * to span_ref bit for bit, guard words included. Re-pin only for a deliberate change of the reference arithmetic
 * or LUT. Re-pinned for Task 11 / R58 O0 (the single-precision LUT build): spin -83, spin 100 and studio changed,
 * 18 of 2.94 M pixels over 24 lon0 differing from the double build (a texel column at a rounding boundary); the
 * float-oracle error is unchanged (worst 2, 0 px over 2). */

#define GTW 512
static uint32_t g_gpx[GTW * GTW / 2];
static const omni_globe_tex_t g_gtex = { GTW, GTW / 2, g_gpx };

typedef struct {
    const char *name;
    omni_globe_cfg_t cfg;
    double lon0;
    bool dim, under;
} kcase_t;

#define K_LIVE { .size = 286, .diam = 262, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO, .tex_w = GTW }
#define K_SPIN { .size = 350, .diam = 316, .lat0_deg = 14, .roll_deg = 23.44f, .proj = OMNI_PROJ_ORTHO, .tex_w = GTW }
static const kcase_t KCASES[] = {
    { "live -83", K_LIVE, -83, false, false },
    { "live 179.9 dim under", K_LIVE, 179.9, true, true },
    { "spin -83 under", K_SPIN, -83, false, true },
    { "spin 100 dim", K_SPIN, 100, true, false },
    { "studio spin", { .size = 350, .diam = 316, .lat0_deg = 14, .roll_deg = 23.44f, .proj = OMNI_PROJ_ORTHO,
                       .studio = true, .light = { -0.52241f, 0.48222f, 0.70324f }, .tex_w = GTW }, 30, true, true },
    { "polar -83", { .size = 286, .diam = 262, .proj = OMNI_PROJ_POLAR, .max_colat_deg = 145, .tex_w = GTW }, -83,
      false, true },
    { "S == D", { .size = 96, .diam = 96, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO, .tex_w = GTW }, 10, false, true },
};
#define NKCASES (sizeof KCASES / sizeof KCASES[0])
static const uint64_t GOLDEN[NKCASES] = {
    0xceb3e2b646251f21ull, /* live -83 */
    0xb3c051332cda0d55ull, /* live 179.9 dim under */
    0xdc0d8f60d62cda29ull, /* spin -83 under */
    0x3d793bf2b0652113ull, /* spin 100 dim */
    0xfd085b93a7e8370bull, /* studio spin */
    0x686d54f64a4aa224ull, /* polar -83 */
    0xafe9adb50c7bd506ull, /* S == D */
};

typedef struct {
    omni_globe_lut_t lut;
    void *mem;
    uint16_t *under;
    omni_globe_frame_t f;
} kenv_t;

static void make_gtex(void)
{
    uint32_t x = 2463534242u;
    for (int i = 0; i < GTW * GTW / 2; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;   /* xorshift32 */
        g_gpx[i] = x;
    }
}

static void kenv_init(kenv_t *e, const kcase_t *c)
{
    const size_t bytes = omni_globe_lut_bytes(&c->cfg);
    TEST_ASSERT_TRUE(bytes > 0);
    e->mem = malloc(bytes);
    TEST_ASSERT_TRUE(omni_globe_lut_build(&e->lut, &c->cfg, e->mem, bytes));
    const int n = c->cfg.size;
    e->under = NULL;
    if (c->under) {
        e->under = malloc((size_t)n * n * 2);
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) e->under[y * n + x] = (uint16_t)(x * 0x0841u + y * 0x1003u);
    }
    omni_globe_frame_t f = { .lut = &e->lut, .tex = &g_gtex, .shade = c->dim ? &g_shade_dim : &g_shade,
                             .lon0_deg = c->lon0, .under = e->under, .over = NULL, .fade = 255 };
    if (c->cfg.studio) {
        omni_globe_sun_studio(&f.sun);
    } else {
        omni_sun_t sun;
        omni_sun_at(WHEN, &sun);
        omni_globe_sun_now(&f.sun, &sun, c->cfg.lat0_deg, c->lon0);
    }
    e->f = f;
}

static void kenv_free(kenv_t *e)
{
    free(e->mem);
    free(e->under);
}

/* fn against span_ref on every span of every case (omni_globe_kernel_bitexact, shared with the device's
 * `globe selftest`): each call on its own row plus guard pixels. False with a reason in why. */
static bool check_kernel_bitexact(omni_globe_span_fn fn, char *why, size_t why_n)
{
    bool ok = true;
    for (unsigned ci = 0; ci < NKCASES && ok; ci++) {
        kenv_t e;
        kenv_init(&e, &KCASES[ci]);
        const int n = KCASES[ci].cfg.size;
        uint16_t *scratch = malloc((size_t)2 * (n + OMNI_KCHECK_GUARD) * 2);
        ok = omni_globe_kernel_bitexact(fn, &e.f, omni_globe_uoff_q4(GTW, KCASES[ci].lon0), scratch, KCASES[ci].name,
                                        why, why_n);
        free(scratch);
        kenv_free(&e);
    }
    return ok;
}

/* I1: the box a reveal starts from (globe_view_reset): the overlay alone over a base, by the renderer's own rule, so
 * outside the globe it is the frame's pixels exactly. */
static void test_overlay_draw_matches_render(void)
{
    make_under(g_under);
    static uint32_t img[S * S];
    for (int i = 0; i < S * S; i++) {
        const uint32_t a = (uint32_t)(i * 37) % 300;   /* about a seventh of the pixels alpha 0 */
        img[i] = (a > 255 ? 0 : a) << 24 | ((uint32_t)i * 7 & 255) << 16 | ((uint32_t)i * 13 & 255) << 8 | (i & 255);
    }
    const size_t bytes = omni_overlay_bytes(img, S, S, S);   /* dense: more than the pixels alone */
    void *mem = malloc(bytes);
    TEST_ASSERT_NOT_NULL(mem);
    omni_overlay_t ov;
    TEST_ASSERT_TRUE(omni_overlay_build(&ov, img, S, S, S, mem, bytes));
    omni_globe_frame_t f = frame(-83, g_under);
    f.over = &ov;
    omni_globe_render(&f, g_a, 0, S, omni_globe_span_ref);
    memcpy(g_b, g_under, sizeof g_b);
    omni_overlay_draw(&ov, g_b, S);
    int outside = 0;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            const int i = y * S + x;
            const long k = entry_at(x, y);
            if (k < 0 || !g_lut.alpha[k]) {
                TEST_ASSERT_EQUAL_HEX16(g_a[i], g_b[i]);
                outside++;
            }
            const int a = (int)(img[i] >> 24), ap = a + (a >> 7);
            const uint16_t u = g_under[i];
            const uint16_t want = pack(((int)(img[i] >> 16 & 255) * ap + ex_r(u) * (256 - ap)) >> 8,
                                       ((int)(img[i] >> 8 & 255) * ap + ex_g(u) * (256 - ap)) >> 8,
                                       ((int)(img[i] & 255) * ap + ex_b(u) * (256 - ap)) >> 8);
            TEST_ASSERT_EQUAL_HEX16(want, g_b[i]);
        }
    TEST_ASSERT_TRUE(outside > S * S - DIAM * DIAM);
    free(mem);
}

static void test_render_golden(void)
{
    int bad = 0;
    for (unsigned ci = 0; ci < NKCASES; ci++) {
        kenv_t e;
        kenv_init(&e, &KCASES[ci]);
        const int n = KCASES[ci].cfg.size;
        uint16_t *out = malloc((size_t)n * n * 2);
        omni_globe_render(&e.f, out, 0, (uint16_t)n, omni_globe_span_ref);
        const uint64_t h = omni_fnv1a64(OMNI_FNV1A_INIT, out, (size_t)n * n * 2);
        if (h != GOLDEN[ci]) {
            printf("golden %u (%s): 0x%016llxull\n", ci, KCASES[ci].name, (unsigned long long)h);
            bad++;
        }
        free(out);
        kenv_free(&e);
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, bad, "span_ref output differs from the pinned digests (printed above)");
}

static void broken_last_px(const omni_globe_frame_t *f, const omni_globe_span_t *sp, int32_t uoff, uint16_t *dst)
{
    omni_globe_span_ref(f, sp, uoff, dst);
    dst[sp->n / 2] ^= 1;
}

static void broken_tail_store(const omni_globe_frame_t *f, const omni_globe_span_t *sp, int32_t uoff, uint16_t *dst)
{
    omni_globe_span_ref(f, sp, uoff, dst);
    dst[sp->n] = 0;   /* an unmasked vector tail */
}

static void test_kernel_bitexact_harness(void)
{
    char why[160] = "";
    TEST_ASSERT_TRUE_MESSAGE(check_kernel_bitexact(omni_globe_span_ref, why, sizeof why), why);
    TEST_ASSERT_FALSE(check_kernel_bitexact(broken_last_px, why, sizeof why));
    TEST_ASSERT_FALSE(check_kernel_bitexact(broken_tail_store, why, sizeof why));
}

/* Task 11's fast C kernel: span_ref bit for bit on every golden case (guards included) and on the synthetic LUT. */
static void test_span_fast_bitexact(void)
{
    char why[160] = "";
    TEST_ASSERT_TRUE_MESSAGE(check_kernel_bitexact(omni_globe_span_fast, why, sizeof why), why);
    const size_t bytes = omni_globe_synth_bytes();
    void *mem = malloc(bytes);
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_SYNTH_DIGEST, omni_globe_synth_digest(omni_globe_span_fast, mem, bytes));
    free(mem);
}

/* The libm-free digest the device's `globe selftest` checks its own span_ref against. */
static void test_synth_digest(void)
{
    const size_t bytes = omni_globe_synth_bytes();
    void *mem = malloc(bytes + 1);
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_SYNTH_DIGEST, omni_globe_synth_digest(omni_globe_span_ref, mem, bytes));
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_SYNTH_DIGEST,
                            omni_globe_synth_digest(omni_globe_span_ref, (uint8_t *)mem + 1, bytes));  /* any alignment */
    TEST_ASSERT_TRUE(omni_globe_synth_digest(broken_last_px, mem, bytes) != OMNI_GLOBE_SYNTH_DIGEST);
    TEST_ASSERT_TRUE(omni_globe_synth_digest(broken_tail_store, mem, bytes) != OMNI_GLOBE_SYNTH_DIGEST);
    TEST_ASSERT_EQUAL_HEX64(0, omni_globe_synth_digest(omni_globe_span_ref, mem, bytes - 1));
    free(mem);
}

static void test_uoff_q4(void)
{
    TEST_ASSERT_EQUAL(4210, omni_globe_uoff_q4(256, -170));      /* floor(-1934.2 + 0.5) + 2048, + 4096 */
    TEST_ASSERT_EQUAL(4210, omni_globe_uoff_q4(256, 190));
    TEST_ASSERT_EQUAL(6144, omni_globe_uoff_q4(256, -0.0439453125));   /* exactly -0.5 Q4: half rounds up */
    TEST_ASSERT_EQUAL(6145, omni_globe_uoff_q4(256, 0.0439453125));    /* exactly +0.5 Q4 */
    TEST_ASSERT_EQUAL(16 * 2048 + 8 * 2048, omni_globe_uoff_q4(2048, 0));
}

/* ---- Overlay */

static void test_overlay_spans(void)
{
    enum { W = 16, H = 4, STRIDE = 20 };
    static uint32_t img[STRIDE * H];
    memset(img, 0, sizeof img);
    for (int y = 0; y < H; y++) {
        for (int x = 1; x < 4; x++) img[y * STRIDE + x] = 0x10000000u | (uint32_t)(y << 8 | x);
        for (int x = 8; x < 13; x++) img[y * STRIDE + x] = 0xff000000u | (uint32_t)(y << 8 | x);
        img[y * STRIDE + 17] = 0xff000000u;   /* past w: ignored */
    }
    static uint64_t mem[64];
    omni_overlay_t ov;
    size_t bytes = omni_overlay_bytes(img, W, H, STRIDE);
    TEST_ASSERT_TRUE(bytes > 0 && bytes <= sizeof mem);
    TEST_ASSERT_FALSE(omni_overlay_build(&ov, img, W, H, STRIDE, mem, bytes - 1));
    TEST_ASSERT_TRUE(omni_overlay_build(&ov, img, W, H, STRIDE, (char *)mem + 1, bytes));   /* any alignment */
    TEST_ASSERT_EQUAL(8, ov.nspans);
    uint32_t off = 0;
    for (unsigned i = 0; i < ov.nspans; i++) {
        const omni_ospan_t *s = &ov.span[i];
        TEST_ASSERT_EQUAL(i / 2, s->y);
        TEST_ASSERT_EQUAL(i % 2 ? 8 : 1, s->x0);
        TEST_ASSERT_EQUAL(i % 2 ? 5 : 3, s->n);
        TEST_ASSERT_EQUAL(off, s->off);
        for (int j = 0; j < s->n; j++) TEST_ASSERT_EQUAL_HEX32(img[s->y * STRIDE + s->x0 + j], ov.argb[off + j]);
        off += s->n;
    }
    TEST_ASSERT_EQUAL(0, ((uintptr_t)ov.span | (uintptr_t)ov.argb) & 3);

    /* Fully transparent rows give no spans. */
    for (int x = 0; x < W; x++) img[1 * STRIDE + x] = img[3 * STRIDE + x] = 0x00ffffffu;
    TEST_ASSERT_TRUE(omni_overlay_build(&ov, img, W, H, STRIDE, mem, omni_overlay_bytes(img, W, H, STRIDE)));
    TEST_ASSERT_EQUAL(4, ov.nspans);
    for (unsigned i = 0; i < ov.nspans; i++) TEST_ASSERT_EQUAL(i < 2 ? 0 : 2, ov.span[i].y);
    memset(img, 0, sizeof img);
    TEST_ASSERT_TRUE(omni_overlay_build(&ov, img, W, H, STRIDE, mem, omni_overlay_bytes(img, W, H, STRIDE)));
    TEST_ASSERT_EQUAL(0, ov.nspans);
}

/* ---- Halo */

static void over_px(double c[3], const double col[3], double a)
{
    for (int k = 0; k < 3; k++) c[k] = col[k] * a + c[k] * (1 - a);
}

/* Expected halo pixel (well inside the antialiased outer edge) over base, in double. */
static uint16_t halo_expect(int x, int y, double cx, double cy, double R, const float *dir, double str, uint16_t base)
{
    const double d = hypot(x + 0.5 - cx, y + 0.5 - cy), r0 = 0.985 * R, r1 = 1.07 * R;
    TEST_ASSERT_TRUE(d >= r0 && d <= r1 - 0.4);
    double c[3] = { ex_r(base), ex_g(base), ex_b(base) };
    const double t = (d - r0) / (r1 - r0);
    const double C0[4] = { 110, 170, 255, fmin(1, 0.30 * str) }, C1[4] = { 70, 130, 255, fmin(1, 0.10 * str) },
                 C2[4] = { 40, 90, 255, 0 };
    const double *p = t < 0.35 ? C0 : C1, *q = t < 0.35 ? C1 : C2, u = t < 0.35 ? t / 0.35 : (t - 0.35) / 0.65;
    double col[4];
    for (int k = 0; k < 4; k++) col[k] = p[k] + (q[k] - p[k]) * u;
    over_px(c, col, col[3]);
    if (dir && d >= 0.99 * R) {
        const double lx = cx + 0.9 * R, ly = cy, dl = hypot(x + 0.5 - lx, y + 0.5 - ly) / (0.75 * R);
        const double lc[3] = { 150, 200, 255 };
        over_px(c, lc, fmin(1, 0.22 * str) * (1 - fmin(1, dl)));
    }
    return pack((int)floor(c[0] + 0.5), (int)floor(c[1] + 0.5), (int)floor(c[2] + 0.5));
}

static void test_halo_sun_side(void)
{
    static uint16_t box[S * S];
    memset(box, 0, sizeof box);
    const float cx = 56, cy = 56, R = 40, dir[2] = { 1, 0 };
    omni_globe_halo(box, S, cx, cy, R, dir, 1.0f);
    const uint16_t sun_side = box[56 * S + (int)floorf(cx + 1.03f * R)];
    const uint16_t far_side = box[56 * S + (int)floorf(cx - 1.03f * R)];
    TEST_ASSERT_TRUE((sun_side & 31) > (far_side & 31));
    TEST_ASSERT_TRUE((far_side & 31) > 0);
    int changed = 0;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            if (d < 0.98f * R || d > 1.08f * R) TEST_ASSERT_EQUAL_HEX16(0, box[y * S + x]);
            else if (box[y * S + x]) changed++;
        }
    TEST_ASSERT_TRUE(changed > 200);

    /* Colour and alpha at sample points, from the mockup's gradients: canvas interpolates the stops
     * unpremultiplied (Skia's default), composited source-over: ring, then lobe. */
    static const int xs[] = { 95, 96, 97, 14, 15, 16 };
    const float strengths[] = { 1.0f, 2.5f };
    for (int si = 0; si < 2; si++) {
        for (int i = 0; i < S * S; i++) box[i] = pack(100, 60, 20);
        omni_globe_halo(box, S, cx, cy, R, dir, strengths[si]);
        for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
            const uint16_t want = halo_expect(xs[i], 56, cx, cy, R, dir, strengths[si], pack(100, 60, 20));
            TEST_ASSERT_EQUAL_HEX16_MESSAGE(want, box[56 * S + xs[i]], "halo sample");
        }
    }

    /* No lobe: symmetric. */
    memset(box, 0, sizeof box);
    omni_globe_halo(box, S, cx, cy, R, NULL, 1.0f);
    TEST_ASSERT_EQUAL_HEX16(box[56 * S + 97], box[56 * S + 14]);
}

/* ---- Compact LUT (Task 11 O1b) */

static void test_compact_matches_planar_lut(void)
{
    TEST_ASSERT_EQUAL(0, (uintptr_t)g_clut.blk & 63);
    TEST_ASSERT_EQUAL(64, sizeof(omni_globe_blk_t));
    TEST_ASSERT_EQUAL(g_lut.nspans, g_clut.nspans);
    const double R = DIAM / 2.0;
    const int box = (S - DIAM) / 2;
    for (unsigned si = 0; si < g_lut.nspans; si++) {
        const omni_globe_span_t *p = &g_lut.span[si];
        const omni_globe_cspan_t *c = &g_clut.span[si];
        TEST_ASSERT_EQUAL(p->y, c->y);
        TEST_ASSERT_EQUAL(p->x0, c->x0);
        TEST_ASSERT_EQUAL(p->n, c->n);
        TEST_ASSERT_EQUAL(p->off / 8, c->blk);
        for (int j = 0; j < p->n; j++) {
            const uint32_t k = p->off + (uint32_t)j;
            const omni_globe_blk_t *b = &g_clut.blk[k / 8];
            const int l = (int)(k & 7);
            TEST_ASSERT_EQUAL(g_lut.u[k], b->u[l]);
            TEST_ASSERT_EQUAL(g_lut.row[k], b->row[l]);
            TEST_ASSERT_EQUAL(g_lut.limb[k], b->limb[l]);
            TEST_ASSERT_EQUAL(g_lut.rim[k], b->rim[l]);
            TEST_ASSERT_EQUAL(g_lut.alpha[k], b->alpha[l]);
            TEST_ASSERT_EQUAL(j >= c->ia && j < c->ib, g_lut.alpha[k] == 255);   /* the interior run is exact */
            double xs = (p->x0 - box + j + 0.5 - R) / R, ys = -(p->y - box + 0.5 - R) / R, r = hypot(xs, ys);
            if (r > 0.9995) r = 0.9995;
            TEST_ASSERT_INT_WITHIN(2, (int)lround(sqrt(1 - r * r) * 16384), b->z[l]);
        }
    }
}

/* The compact reference against the planar one on the golden cases: they differ only where mu's rounding moves
 * the shade index by a bin; printed, and bounded at 0.5% of the box and 6 (565 units) on the random texture. */
static void test_compact_vs_planar(void)
{
    for (unsigned ci = 0; ci < NKCASES; ci++) {
        const kcase_t *kc = &KCASES[ci];
        if (!omni_globe_compact_ok(&kc->cfg)) continue;
        kenv_t e;
        kenv_init(&e, kc);
        const int n = kc->cfg.size;
        omni_globe_clut_t c;
        void *cm = malloc(omni_globe_compact_bytes(&e.lut));
        TEST_ASSERT_TRUE(omni_globe_compact(&c, &e.lut, cm, omni_globe_compact_bytes(&e.lut)));
        uint16_t *a = malloc((size_t)n * n * 2), *b = malloc((size_t)n * n * 2);
        omni_globe_render(&e.f, a, 0, (uint16_t)n, omni_globe_span_ref);
        omni_globe_frame_t f = e.f;
        f.clut = &c;
        omni_globe_render_c(&f, b, 0, (uint16_t)n, NULL, false, NULL);
        int nd = 0, worst = 0;
        for (int i = 0; i < n * n; i++) {
            if (a[i] == b[i]) continue;
            nd++;
            const int d[3] = { abs((a[i] >> 11) - (b[i] >> 11)), abs(((a[i] >> 5) & 63) - ((b[i] >> 5) & 63)),
                               abs((a[i] & 31) - (b[i] & 31)) };
            for (int k = 0; k < 3; k++) worst = d[k] > worst ? d[k] : worst;
        }
        printf("compact vs planar, %s: %d of %d px differ, worst %d\n", kc->name, nd, n * n, worst);
        TEST_ASSERT_TRUE(nd * 200 < n * n);
        TEST_ASSERT_TRUE(worst <= 6);
        free(a);
        free(b);
        free(cm);
        kenv_free(&e);
    }
}

static void test_compact_float_oracle(void)
{
    g_use_compact = true;
    check_vs_oracle(-83, NULL, 4, 0.99);
    make_under(g_under);
    check_vs_oracle(-83, g_under, 4, 0.99);
    check_vs_oracle_look(&DIM, &g_shade_dim, -83, g_under, 4, 0.99);
    check_vs_oracle(179.9, NULL, 4, 0);
    g_use_compact = false;
}

static const uint64_t CGOLDEN[NKCASES] = {   /* the compact reference (cspan_ref) on KCASES; polar has none */
    0x7b34805bd0b32bc0ull, /* live -83 */
    0xc0233bd08bedc4b1ull, /* live 179.9 dim under */
    0xed30e4a5e02f3bd4ull, /* spin -83 under */
    0xb508e4dab2df10ecull, /* spin 100 dim */
    0x0547e5b4d10fb0d0ull, /* studio spin */
    0,                     /* polar -83: planar only */
    0x946ecfa66cc52e30ull, /* S == D */
};

static void test_compact_golden(void)
{
    int bad = 0;
    for (unsigned ci = 0; ci < NKCASES; ci++) {
        const kcase_t *kc = &KCASES[ci];
        if (!omni_globe_compact_ok(&kc->cfg)) continue;
        kenv_t e;
        kenv_init(&e, kc);
        const int n = kc->cfg.size;
        omni_globe_clut_t c;
        void *cm = malloc(omni_globe_compact_bytes(&e.lut));
        TEST_ASSERT_TRUE(omni_globe_compact(&c, &e.lut, cm, omni_globe_compact_bytes(&e.lut)));
        omni_globe_frame_t f = e.f;
        f.clut = &c;
        uint16_t *out = malloc((size_t)n * n * 2);
        omni_globe_render_c(&f, out, 0, (uint16_t)n, NULL, false, NULL);
        const uint64_t h = omni_fnv1a64(OMNI_FNV1A_INIT, out, (size_t)n * n * 2);
        if (h != CGOLDEN[ci]) {
            printf("compact golden %u (%s): 0x%016llxull\n", ci, kc->name, (unsigned long long)h);
            bad++;
        }
        /* keep: the same frame over a buffer last rendered (in full) for another lon0 gives the same bytes */
        uint16_t *k = malloc((size_t)n * n * 2);
        omni_globe_frame_t g = f;
        g.lon0_deg = f.lon0_deg + 37;
        omni_globe_render_c(&g, k, 0, (uint16_t)n, NULL, false, NULL);
        omni_globe_render_c(&f, k, 0, (uint16_t)(n / 3), NULL, true, NULL);
        omni_globe_render_c(&f, k, (uint16_t)(n / 3), (uint16_t)n, NULL, true, NULL);
        TEST_ASSERT_EQUAL_MEMORY(out, k, (size_t)n * n * 2);
        free(k);
        free(out);
        free(cm);
        kenv_free(&e);
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, bad, "cspan_ref output differs from the pinned compact digests (printed above)");
}

static bool check_ckernel_bitexact(omni_globe_cspan_fn fn, char *why, size_t why_n)
{
    bool ok = true;
    for (unsigned ci = 0; ci < NKCASES && ok; ci++) {
        const kcase_t *kc = &KCASES[ci];
        if (!omni_globe_compact_ok(&kc->cfg)) continue;
        kenv_t e;
        kenv_init(&e, kc);
        omni_globe_clut_t c;
        void *cm = malloc(omni_globe_compact_bytes(&e.lut));
        omni_globe_compact(&c, &e.lut, cm, omni_globe_compact_bytes(&e.lut));
        omni_globe_frame_t f = e.f;
        f.clut = &c;
        uint16_t *scratch = malloc((size_t)2 * (kc->cfg.size + OMNI_KCHECK_GUARD) * 2);
        ok = omni_globe_ckernel_bitexact(fn, &f, scratch, kc->name, why, why_n);
        free(scratch);
        free(cm);
        kenv_free(&e);
    }
    return ok;
}

static void broken_c_last(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                          uint16_t *dst)
{
    omni_globe_cspan_ref(f, sp, t, dst);
    dst[sp->n / 2] ^= 1;
}

static void broken_c_tail(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                          uint16_t *dst)
{
    omni_globe_cspan_ref(f, sp, t, dst);
    dst[sp->n] = 0;
}

static void test_cspan_fast_bitexact(void)
{
    char why[160] = "";
    TEST_ASSERT_TRUE_MESSAGE(check_ckernel_bitexact(omni_globe_cspan_ref, why, sizeof why), why);
    TEST_ASSERT_FALSE(check_ckernel_bitexact(broken_c_last, why, sizeof why));
    TEST_ASSERT_FALSE(check_ckernel_bitexact(broken_c_tail, why, sizeof why));
    TEST_ASSERT_TRUE_MESSAGE(check_ckernel_bitexact(omni_globe_cspan_fast, why, sizeof why), why);
    const size_t bytes = omni_globe_csynth_bytes();
    void *mem = malloc(bytes + 1);
    const uint64_t h = omni_globe_csynth_digest(omni_globe_cspan_ref, mem, bytes);
    printf("compact synth digest 0x%016llxull\n", (unsigned long long)h);
    TEST_ASSERT_EQUAL_HEX64(OMNI_GLOBE_CSYNTH_DIGEST, h);
    TEST_ASSERT_EQUAL_HEX64(h, omni_globe_csynth_digest(omni_globe_cspan_ref, (uint8_t *)mem + 1, bytes));
    TEST_ASSERT_EQUAL_HEX64(h, omni_globe_csynth_digest(omni_globe_cspan_fast, mem, bytes));
    TEST_ASSERT_TRUE(omni_globe_csynth_digest(broken_c_last, mem, bytes) != h);
    TEST_ASSERT_TRUE(omni_globe_csynth_digest(broken_c_tail, mem, bytes) != h);
    TEST_ASSERT_EQUAL_HEX64(0, omni_globe_csynth_digest(omni_globe_cspan_ref, mem, bytes - 1));
    free(mem);
}

/* A moving lobe (omni_globe_halo_unlobe the old, omni_globe_halo_lobe the new, over a kept ring-only base) equals the
 * whole halo for each direction, bit for bit, over a patterned base and over black, as the live face re-bases a drag;
 * and every changed pixel lies in lobe_rect. */
/* R53 step 6a: linear-in-u sampling on the compact path (omni_globe_cspan_lin_ref), held to the oracle sampling the
 * same way, as tightly as the nearest kernel is held to the nearest oracle. */
static void test_compact_lin_float_oracle(void)
{
    g_use_compact = true;
    g_oracle_lin = true;
    g_ckernel = omni_globe_cspan_lin_ref;
    check_vs_oracle(-83, NULL, 4, 0.99);
    make_under(g_under);
    check_vs_oracle(-83, g_under, 4, 0.99);
    check_vs_oracle_look(&DIM, &g_shade_dim, -83, g_under, 4, 0.99);
    check_vs_oracle(179.9, NULL, 4, 0);
    g_ckernel = omni_globe_cspan_ref;
    g_oracle_lin = false;
    g_use_compact = false;
}

/* The fast linear kernel is the linear reference bit for bit (whole frames over a patterned base, several lon0,
 * the dateline included, every Q4 phase of the texel), and a one-pixel error is caught. */
static void broken_lin(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                       uint16_t *dst)
{
    omni_globe_cspan_lin_ref(f, sp, t, dst);
    dst[sp->n / 2] ^= 0x0800;
}

static bool lin_same(omni_globe_cspan_fn fn)
{
    make_under(g_under);
    for (double lon0 = -179.95; lon0 < 180; lon0 += 13.37) {
        omni_globe_frame_t f = frame(lon0, g_under);
        f.clut = &g_clut;
        omni_globe_render_c(&f, g_a, 0, S, omni_globe_cspan_lin_ref, false, NULL);
        omni_globe_render_c(&f, g_b, 0, S, fn, false, NULL);
        if (memcmp(g_a, g_b, sizeof g_a) != 0) return false;
    }
    return true;
}

static void test_cspan_lin_fast_bitexact(void)
{
    TEST_ASSERT_TRUE(lin_same(omni_globe_cspan_lin_fast));
    TEST_ASSERT_FALSE(lin_same(broken_lin));
}

/* What the change is for: across a hard edge in the texture (an 8-texel red/black stripe pattern, a coastline), the
 * nearest kernel jumps from texel to texel while the linear one passes through the values between. */
static void test_lin_softens_edges(void)
{
    static uint32_t saved[TW * TH];
    memcpy(saved, g_px, sizeof saved);
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) g_px[y * TW + x] = (x / 8) & 1 ? pack(248, 0, 0) : 0;
    int mid[2] = { 0, 0 };
    for (int k = 0; k < 2; k++) {
        omni_globe_frame_t f = frame(5, NULL);   /* near the subsolar longitude at WHEN: the disc's middle is lit */
        f.clut = &g_clut;
        omni_globe_render_c(&f, g_a, 0, S, k ? omni_globe_cspan_lin_ref : omni_globe_cspan_ref, false, NULL);
        for (int y = S / 2 - 8; y < S / 2 + 8; y++)   /* the disc's middle rows: lit, little limb darkening */
            for (int x = S / 2 - 30; x < S / 2 + 30; x++) {
                const int r = ex_r(g_a[y * S + x]), r0 = ex_r(g_a[y * S + x - 1]), r1 = ex_r(g_a[y * S + x + 1]);
                const int lo = r0 < r1 ? r0 : r1, hi = r0 < r1 ? r1 : r0;
                if (hi - lo > 40 && r > lo + 8 && r < hi - 8) mid[k]++;   /* strictly between its neighbours */
            }
    }
    memcpy(g_px, saved, sizeof saved);
    printf("edge pixels between their neighbours: nearest %d, linear %d\n", mid[0], mid[1]);
    TEST_ASSERT_TRUE(mid[1] > 3 * mid[0] + 10);
}

static void test_halo_lobe_moves(void)
{
    enum { HS = 286 };
    static uint16_t src[HS * HS], ring[HS * HS], base[HS * HS], full[HS * HS];
    const float c = HS / 2.0f, R = 131, st = 0.9f;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < HS * HS; i++) src[i] = pass ? (uint16_t)(i * 0x9e37u) : 0;
        memcpy(ring, src, sizeof ring);
        omni_globe_halo(ring, HS, c, c, R, NULL, st);
        memcpy(base, ring, sizeof base);
        float prev[2] = { 0, 0 };
        for (int k = 0; k < 24; k++) {
            const float dir[2] = { cosf(0.9f * k), sinf(0.9f * k) * (k % 3 ? 1 : -0.3f) };
            const int split = 60 + 7 * k;   /* in two row ranges, as the two cores run it */
            omni_globe_halo_unlobe(base, ring, HS, c, c, R, k ? prev : NULL, st, 0, split);
            omni_globe_halo_unlobe(base, ring, HS, c, c, R, k ? prev : NULL, st, split, HS);
            omni_globe_halo_lobe(base, pass ? src : NULL, HS, c, c, R, dir, st, split, HS);
            omni_globe_halo_lobe(base, pass ? src : NULL, HS, c, c, R, dir, st, 0, split);
            memcpy(prev, dir, sizeof prev);
            memcpy(full, src, sizeof full);
            omni_globe_halo(full, HS, c, c, R, dir, st);
            TEST_ASSERT_EQUAL_MEMORY(full, base, sizeof full);
            int r[4];
            omni_globe_halo_lobe_rect(HS, c, c, R, dir, st, r);
            for (int y = 0; y < HS; y++)
                for (int x = 0; x < HS; x++)
                    if (full[y * HS + x] != ring[y * HS + x])
                        TEST_ASSERT_TRUE(x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]);
        }
    }
}

/* keep with a dirty rect: a buffer rendered over base U1, then the frame over base U2 (differing from U1 only inside
 * the rect) with keep and the rect, equals the full frame over U2; with an overlay that crosses the disc edge and the
 * rect, so the span and dirty intervals overlap, touch and stay apart on different rows. */
static void test_compact_keep_dirty(void)
{
    kenv_t e;
    kenv_init(&e, &KCASES[2]);   /* spin -83 under */
    const int n = KCASES[2].cfg.size;
    omni_globe_clut_t c;
    void *cm = malloc(omni_globe_compact_bytes(&e.lut));
    TEST_ASSERT_TRUE(omni_globe_compact(&c, &e.lut, cm, omni_globe_compact_bytes(&e.lut)));
    uint32_t *img = calloc((size_t)n * n, 4);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            const double d = hypot(x + 0.5 - n / 2.0, y + 0.5 - n / 2.0);
            if ((d > 140 && d < 175) || (x > 150 && x < 200)) img[y * n + x] = ((uint32_t)(40 + (x * 7) % 200) << 24 | 0x80c0e0u);
        }
    const size_t ob = omni_overlay_bytes(img, (uint16_t)n, (uint16_t)n, (uint16_t)n);
    void *om = malloc(ob);
    omni_overlay_t ov;
    TEST_ASSERT_TRUE(omni_overlay_build(&ov, img, (uint16_t)n, (uint16_t)n, (uint16_t)n, om, ob));
    uint16_t *u2 = malloc((size_t)n * n * 2), *k = malloc((size_t)n * n * 2), *x = malloc((size_t)n * n * 2);
    memcpy(u2, e.under, (size_t)n * n * 2);
    const int rects[3][4] = { { 250, 20, 340, 120 }, { 0, 150, 60, 200 }, { 100, 300, 260, 349 } };
    for (int ri = 0; ri < 3; ri++) {
        const int *r = rects[ri];
        omni_globe_frame_t f = e.f;
        f.clut = &c;
        f.over = &ov;
        f.under = e.under;
        omni_globe_render_c(&f, k, 0, (uint16_t)n, NULL, false, NULL);   /* the buffer, over U1 */
        for (int y = r[1]; y <= r[3]; y++)
            for (int xx = r[0]; xx <= r[2]; xx++) u2[y * n + xx] = (uint16_t)(e.under[y * n + xx] ^ 0x5a5au);
        f.under = u2;
        f.lon0_deg += 3;
        omni_globe_render_c(&f, x, 0, (uint16_t)n, NULL, false, NULL);
        omni_globe_render_c(&f, k, 0, (uint16_t)n, NULL, true, r);
        TEST_ASSERT_EQUAL_MEMORY(x, k, (size_t)n * n * 2);
        memcpy(u2, e.under, (size_t)n * n * 2);
    }
    free(u2);
    free(k);
    free(x);
    free(om);
    free(img);
    free(cm);
    kenv_free(&e);
}

/* ---- The Omni globe look (Task 5): limb_kind 1 and look kind 1 against the mockup's initGlobe/renderGlobe maths
 * (faces-omni.js 322-395: tilt 22, light (-.42, .52, .74) as written, shade = (.32 + .78 dif)(.62 + .38 z^.5), no
 * night, no atmosphere), point-sampled at 1x pixel centres for D 224 (the mockup's canvas is 2x). */

#define OTW 512
#define OTH 256
#define OD 224

static const omni_globe_cfg_t OCFG = { .size = OD, .diam = OD, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO, .studio = true,
                                       .light = { -0.42f, 0.52f, 0.74f }, .limb_kind = 1, .tex_w = OTW };
static const omni_globe_look_t OLOOK = { .gain = 1, .kind = 1 };
static uint32_t g_opx[OTW * OTH];

static void make_omni_texture(void)   /* smooth and continuous across +-180; day only */
{
    for (int y = 0; y < OTH; y++)
        for (int x = 0; x < OTW; x++) {
            const double lon = ((x + 0.5) / OTW * 360 - 180) * D2R, lat = (90 - (y + 0.5) / OTH * 180) * D2R;
            g_opx[y * OTW + x] = pack((int)lround(140 + 100 * cos(lon)), (int)lround(120 + 90 * sin(lon + 1)),
                                      (int)lround(110 + 100 * sin(lat)));
        }
}

static void omni_oracle(double lon_c, uint16_t *out)
{
    const double rad = OD / 2.0, ct = cos(22 * D2R), st = sin(22 * D2R);
    double shift = ((lon_c + 180) / 360) * OTW;
    shift = fmod(fmod(shift, OTW) + OTW, OTW);
    for (int j = 0; j < OD; j++)
        for (int i = 0; i < OD; i++) {
            const double x = (i + 0.5 - rad) / rad, y = (j + 0.5 - rad) / rad, rr = x * x + y * y, dist = sqrt(rr) * rad;
            out[j * OD + i] = 0;
            if (dist > rad + 0.5) continue;
            const double z = sqrt(1 - fmin(rr, 0.9999)), yu = -y;
            const double lat = asin(clampd(yu * ct + z * st, -1, 1)), zz = z * ct - yu * st, lon = atan2(x, zz);
            const int row = (int)clampd(floor((0.5 - lat / PI) * OTH), 0, OTH - 1);
            double u = lon / (2 * PI) * OTW + shift;
            if (u < 0) u += OTW; else if (u >= OTW) u -= OTW;
            const double dif = fmax(0, x * -0.42 + yu * 0.52 + z * 0.74);
            const double shade = (0.32 + 0.78 * dif) * (0.62 + 0.38 * pow(z, 0.5));
            const int sh = (int)lround(clampd(shade, 0, 1.15) * 256);
            const uint16_t t = (uint16_t)g_opx[row * OTW + (int)u];
            int c[3] = { (ex_r(t) * sh) >> 8, (ex_g(t) * sh) >> 8, (ex_b(t) * sh) >> 8 };
            const double a = lround(clampd(rad - dist + 0.5, 0, 1) * 255) / 255.0;
            for (int k = 0; k < 3; k++) c[k] = (int)clampd(rint((c[k] > 255 ? 255 : c[k]) * a), 0, 255);
            out[j * OD + i] = pack(c[0], c[1], c[2]);
        }
}

static void test_omni_globe_vs_mockup(void)
{
    make_omni_texture();
    omni_globe_tex_t tex = { OTW, OTH, g_opx };
    size_t bytes = omni_globe_lut_bytes(&OCFG);
    void *mem = malloc(bytes);
    omni_globe_lut_t lut;
    TEST_ASSERT_TRUE(omni_globe_lut_build(&lut, &OCFG, mem, bytes));
    TEST_ASSERT_TRUE(omni_globe_compact_ok(&OCFG));
    const size_t cb = omni_globe_compact_bytes(&lut);
    void *cm = malloc(cb);
    omni_globe_clut_t clut;
    TEST_ASSERT_TRUE(omni_globe_compact(&clut, &lut, cm, cb));
    static omni_globe_shade_t sh;
    omni_globe_shade_build(&sh, &OLOOK);
    TEST_ASSERT_EQUAL(0, sh.e[100].nr + sh.e[511].at + sh.e[0].db * 0);   /* no night, no atmosphere */
    static uint16_t a[OD * OD], b[OD * OD];
    for (int pass = 0; pass < 2; pass++) {   /* lon 0, and a turned view */
        const double lon0 = pass ? -83 : 0;
        omni_globe_frame_t f = { .lut = &lut, .tex = &tex, .shade = &sh, .clut = &clut, .lon0_deg = lon0, .fade = 255 };
        omni_globe_sun_studio(&f.sun);
        omni_globe_render_c(&f, a, 0, OD, NULL, false, NULL);
        omni_oracle(lon0, b);
        int disc = 0, within = 0, worst = 0;
        for (int i = 0; i < OD * OD; i++) {
            if (!b[i] && !a[i]) continue;
            disc++;
            const int d[3] = { abs((a[i] >> 11) - (b[i] >> 11)), abs(((a[i] >> 5) & 63) - ((b[i] >> 5) & 63)),
                               abs((a[i] & 31) - (b[i] & 31)) };
            int m = d[0] > d[1] ? d[0] : d[1];
            m = m > d[2] ? m : d[2];
            if (m <= 2) within++;
            if (m > worst) worst = m;
        }
        printf("omni globe lon %.0f: %d disc px, %d within 2 (565 units), worst %d\n", lon0, disc, within, worst);
        TEST_ASSERT_TRUE(disc > 38000);
        TEST_ASSERT_TRUE(within >= disc * 0.995);
    }
    free(mem);
    free(cm);
}

static void test_omni_shade_table(void)
{
    omni_globe_shade_t sh;
    omni_globe_shade_build(&sh, &OLOOK);
    for (int i = 0; i < OMNI_SHADE_N; i++) {
        const double mu = (i + 0.5) / 256 - 1, want = 0.32 + 0.78 * fmax(mu, 0);
        TEST_ASSERT_INT_WITHIN(1, (int)lround(want * 256), sh.e[i].dr);
        TEST_ASSERT_EQUAL(sh.e[i].dr, sh.e[i].dg);
        TEST_ASSERT_EQUAL(sh.e[i].dr, sh.e[i].db);
        TEST_ASSERT_EQUAL(0, sh.e[i].nr | sh.e[i].ng | sh.e[i].nb | sh.e[i].at);
    }
    TEST_ASSERT_TRUE(sh.e[511].dr <= 1.1 * 256 + 1);
    const omni_globe_look_t g = { .gain = 0.5f, .kind = 1 };   /* gain scales it */
    omni_globe_shade_build(&sh, &g);
    TEST_ASSERT_INT_WITHIN(1, (int)lround(0.5 * 1.1 * 256), sh.e[511].dr);
}

int main(void)
{
    setup_all();
    make_gtex();
    UNITY_BEGIN();
    RUN_TEST(test_shade_curve);
    RUN_TEST(test_render_matches_float_oracle);
    RUN_TEST(test_render_rotation_wraps);
    RUN_TEST(test_render_rows_split_equal);
    RUN_TEST(test_render_under_over_fade);
    RUN_TEST(test_uoff_q4);
    RUN_TEST(test_render_golden);
    RUN_TEST(test_kernel_bitexact_harness);
    RUN_TEST(test_span_fast_bitexact);
    RUN_TEST(test_synth_digest);
    RUN_TEST(test_overlay_spans);
    RUN_TEST(test_overlay_draw_matches_render);
    RUN_TEST(test_halo_sun_side);
    RUN_TEST(test_halo_lobe_moves);
    RUN_TEST(test_compact_matches_planar_lut);
    RUN_TEST(test_compact_vs_planar);
    RUN_TEST(test_compact_float_oracle);
    RUN_TEST(test_compact_golden);
    RUN_TEST(test_compact_keep_dirty);
    RUN_TEST(test_cspan_fast_bitexact);
    RUN_TEST(test_compact_lin_float_oracle);
    RUN_TEST(test_cspan_lin_fast_bitexact);
    RUN_TEST(test_lin_softens_edges);
    RUN_TEST(test_omni_globe_vs_mockup);
    RUN_TEST(test_omni_shade_table);
    free(g_lut_mem);
    free(g_clut_mem);
    return UNITY_END();
}
