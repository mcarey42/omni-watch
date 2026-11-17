#include "unity.h"
#include "omni/blend565.h"
#include "omni/lid.h"
#include "omni/omni_dial.h"
#include "omni/omni_model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The lid flip (Stage 3 Task 9): the hinge engine against the mockup's drawLid (tools/omni_lid_oracle.mjs prints every
 * number pinned here, from faces-omni.js itself with its dome height Hd = 0), the timeline, the model's lid state, and
 * the compositor's lid layer on a synthetic dial with the shipped omni_lid.bin (ASSETS_DIR). Built twice: plain, and
 * as test_lid_mockup with OMNI_FLIP_MOCKUP (the mockup's timeline, the owner's A/B at Task 14). */

void setUp(void) {}
void tearDown(void) {}

#define W OMNI_DIAL_W
#define D2R 0.017453292519943295f

static float rad(float deg) { return deg * D2R; }

#if !(defined(OMNI_FLIP_MOCKUP) && OMNI_FLIP_MOCKUP)

/* ---- The hinge engine */

/* The mockup's proj() with Hd = 0, forward: source row y to the screen row it lands on, and its scale. */
static void proj(float theta, float y, double *Y, double *s)
{
    const double ct = cos(theta), st = sin(theta), d = y - (-6.0);
    const double Yw = -6.0 + d * ct, Z = d * st;
    *s = 800.0 / (800.0 - Z);
    *Y = 233.0 + (Yw - 233.0) * *s;
}

/* forward with proj(), invert with omni_hinge_map: source row 100, 233, 366 at 30 and 60 deg (the oracle's numbers) */
static void test_lid_flat_matches_mockup(void)
{
    static const struct { float deg, src; double screen, scale; } T[] = {
        { 30, 100, 75.3547, 1.0710 }, { 30, 233, 195.3572, 1.1756 }, { 30, 366, 341.3537, 1.3029 },
        { 60, 100, 22.8902, 1.1296 }, { 60, 233, 71.7913, 1.3490 },  { 60, 366, 144.2671, 1.6742 },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        const float th = rad(T[i].deg);
        double Y, s;
        proj(th, T[i].src, &Y, &s);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)T[i].screen, (float)Y);   /* the oracle's value, to its 4 places */
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)T[i].scale, (float)s);
        omni_hinge_map_t m;
        omni_hinge_map(&OMNI_LID_HINGE, th, &m);
        const float y = (float)Y, v = (m.a * y + m.b) / (m.c * y + m.d) + OMNI_LID_HINGE.pos;
        TEST_ASSERT_FLOAT_WITHIN(0.01f, T[i].src, v);
        const float sc = OMNI_LID_HINGE.focal / (OMNI_LID_HINGE.focal - (v - OMNI_LID_HINGE.pos) * sinf(th));
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)s, sc);
    }
    /* the lines: the screen row holding the forward image of source row 233 reads 233, at 1 / s */
    omni_hinge_line_t L[W];
    omni_hinge_lines(&OMNI_LID_HINGE, rad(30), L);
    TEST_ASSERT_EQUAL_INT16(233, L[195].v);   /* 195.3572: row 195's centre 195.5 maps just past 233 */
    TEST_ASSERT_INT_WITHIN(8, (int)lround(65536.0 / 1.1756), (int)L[195].inv_s_q16);
    for (int y = 1; y < W; y++)
        if (L[y].v >= 0 && L[y - 1].v >= 0) TEST_ASSERT_TRUE(L[y].v >= L[y - 1].v);   /* monotonic down the leaf */
}

/* The line whose continuous source coordinate is nearest v: its shading is v's to within half a row. */
static const omni_hinge_line_t *line_at(const omni_hinge_line_t *L, int v)
{
    for (int y = 0; y < W; y++)
        if (L[y].v == v) return &L[y];
    return NULL;
}

/* (10 deg, v 233) dark .1101 spec .0580; (30, 233) .4071 .1753; (30, 100) .2116 .0618; (60, 366) .6000 .0193;
 * x 255 +- 1 */
static void test_lid_rows_shading(void)
{
    static const struct { float deg; int v; float dark, spec; } T[] = {
        { 10, 233, 0.1101f, 0.0580f }, { 30, 233, 0.4071f, 0.1753f },
        { 30, 100, 0.2116f, 0.0618f }, { 60, 366, 0.6000f, 0.0193f },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        omni_hinge_line_t L[W];
        omni_hinge_lines(&OMNI_LID_HINGE, rad(T[i].deg), L);
        const omni_hinge_line_t *l = line_at(L, T[i].v);
        TEST_ASSERT_NOT_NULL(l);
        TEST_ASSERT_INT_WITHIN(1, (int)lroundf(T[i].dark * 255), l->dark);
        TEST_ASSERT_INT_WITHIN(1, (int)lroundf(T[i].spec * 255), l->spec);
    }
}

/* theta 1 deg -> .92, 10 -> .6650, 45 -> .0831, 80 -> 0 */
static void test_flip_dim(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.92f, omni_flip_dim(rad(1)));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.6650f, omni_flip_dim(rad(10)));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0831f, omni_flip_dim(rad(45)));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, omni_flip_dim(rad(80)));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, omni_flip_fade(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, omni_flip_fade(300));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.125f, omni_flip_fade(150));   /* 1 - outCubic(.5) */
}

/* The lines and the map are the same for either axis (R98: the per-line maths does not depend on it; a column
 * renderer for the vertical axis is the themes stage's): {VERTICAL, ..} gives exactly OMNI_LID_HINGE's, at 30 and 60 */
static void test_hinge_lines_same_for_either_axis(void)
{
    const omni_hinge_t vh = { OMNI_HINGE_VERTICAL, -6, 800, 466, 226, 0.55f };
    for (int k = 0; k < 2; k++) {
        omni_hinge_line_t a[W], b[W];
        memset(a, 0, sizeof a);   /* padding too: compared whole */
        memset(b, 0, sizeof b);
        omni_hinge_lines(&OMNI_LID_HINGE, rad(k ? 60 : 30), a);
        omni_hinge_lines(&vh, rad(k ? 60 : 30), b);
        for (int i = 0; i < W; i++) {
            TEST_ASSERT_EQUAL_INT16(a[i].v, b[i].v);
            TEST_ASSERT_EQUAL_UINT32(a[i].inv_s_q16, b[i].inv_s_q16);
            TEST_ASSERT_EQUAL_UINT8(a[i].dark, b[i].dark);
            TEST_ASSERT_EQUAL_UINT8(a[i].spec, b[i].spec);
        }
        omni_hinge_map_t ma, mb;
        omni_hinge_map(&OMNI_LID_HINGE, rad(k ? 60 : 30), &ma);
        omni_hinge_map(&vh, rad(k ? 60 : 30), &mb);
        TEST_ASSERT_EQUAL_MEMORY(&ma, &mb, sizeof ma);
    }
}

/* dome_r 0 is a flat leaf (a door): no dome term, so dark is the same on every line, 1 - cos(theta + .5) / cos(.5);
 * the geometry is the dome's own (the same v and 1 / s) */
static void test_hinge_flat_leaf(void)
{
    const omni_hinge_t flat = { OMNI_HINGE_VERTICAL, -6, 800, 466, 0, 0.55f };
    omni_hinge_line_t a[W], b[W];
    omni_hinge_lines(&OMNI_LID_HINGE, rad(30), a);
    omni_hinge_lines(&flat, rad(30), b);
    const int want = (int)lround((1 - cos(rad(30) + 0.5) / cos(0.5)) * 255);
    int lines = 0;
    for (int i = 0; i < W; i++) {
        TEST_ASSERT_EQUAL_INT16(a[i].v, b[i].v);
        TEST_ASSERT_EQUAL_UINT32(a[i].inv_s_q16, b[i].inv_s_q16);
        if (b[i].v < 0) continue;
        TEST_ASSERT_INT_WITHIN(1, want, b[i].dark);
        lines++;
    }
    TEST_ASSERT_TRUE(lines > 300);
}

/* The leaf leaves the screen at atan2(F, C - pos), 73.37 deg for the Omni lid: lines just before it, none from it on */
static void test_hinge_gone(void)
{
    const float g = omni_hinge_gone_rad(&OMNI_LID_HINGE);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 73.37f, g / D2R);
    omni_hinge_line_t L[W];
    omni_hinge_lines(&OMNI_LID_HINGE, rad(72.5f), L);
    int n = 0;
    for (int i = 0; i < W; i++) n += L[i].v >= 0;
    TEST_ASSERT_TRUE(n > 0);
    for (float d = g / D2R; d <= 110.5f; d += 0.1f) {
        omni_hinge_lines(&OMNI_LID_HINGE, rad(d), L);
        for (int i = 0; i < W; i++) TEST_ASSERT_TRUE(L[i].v < 0);
    }
}

/* a band's lines (omni_hinge_lines_at) are exactly those rows of the whole table */
static void test_lines_at_slices(void)
{
    omni_hinge_line_t all[W], part[16];
    memset(all, 0, sizeof all);
    omni_hinge_lines(&OMNI_LID_HINGE, rad(37), all);
    for (int y0 = 0; y0 < W; y0 += 16) {
        const int y1 = y0 + 16 < W ? y0 + 16 : W;
        memset(part, 0, sizeof part);
        omni_hinge_lines_at(&OMNI_LID_HINGE, rad(37), y0, y1, part);
        TEST_ASSERT_EQUAL_MEMORY(&all[y0], part, (size_t)(y1 - y0) * sizeof part[0]);
    }
}

/* theta(0) 0, (60) 4 deg, (150) 2.5, (750) 110.5, first >= 90 at 533 +- 2 ms, monotonic over [150, 750]; reduced:
 * theta(750 / 2.2) = 110.5 */
static void test_flip_timeline(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0, omni_flip_theta(0, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(4), omni_flip_theta(60, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(2.5f), omni_flip_theta(150, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(9.25f), omni_flip_theta(300, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(110.5f), omni_flip_theta(750, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(110.5f), omni_flip_theta(5000, false));
    uint32_t t90 = 0;
    while (omni_flip_theta(t90, false) < rad(90)) t90++;
    TEST_ASSERT_UINT32_WITHIN(2, 533, t90);
    for (uint32_t t = 151; t <= 750; t++) TEST_ASSERT_TRUE(omni_flip_theta(t, false) >= omni_flip_theta(t - 1, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(110.5f), omni_flip_theta((uint32_t)lroundf(750 / 2.2f), true));
    for (uint32_t t = 0; t <= 400; t += 5)   /* t x 2.2 a whole ms */
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, omni_flip_theta(t * 11 / 5, false), omni_flip_theta(t, true));
}

/* ---- The model's lid state */

static void test_model_flip_state(void)
{
    static const uint32_t W0[2] = { 1000, 0xFFFFFF00u };   /* across the u32 wrap too */
    struct tm tm = { .tm_year = 126, .tm_mon = 9, .tm_mday = 22, .tm_hour = 10, .tm_min = 10, .tm_sec = 40 };
    for (int k = 0; k < 2; k++) {
        const uint32_t w = W0[k];
        omni_model_t m;
        omni_dial_state_t st;
        omni_model_init(&m, 39.96, -83.0);
        omni_model_wake(&m, w, &OMNI_ENTRY_FLIP, false);
        omni_model_step(&m, w + 100, true, &tm, OMNI_LENS_OFF, &st);
        TEST_ASSERT_TRUE(st.lid);
        TEST_ASSERT_EQUAL_FLOAT(omni_flip_theta(100, false), st.lid_theta);
        TEST_ASSERT_EQUAL_FLOAT(omni_flip_fade(100), st.fade);
        omni_model_step(&m, w + 480, true, &tm, OMNI_LENS_OFF, &st);   /* 71.1 deg: still on the screen */
        TEST_ASSERT_TRUE(st.lid);
        omni_model_step(&m, w + 490, true, &tm, OMNI_LENS_OFF, &st);   /* 75.3 deg: past 73.37, gone */
        TEST_ASSERT_FALSE(st.lid);
        TEST_ASSERT_EQUAL_FLOAT(0, st.lid_theta);
        omni_model_wake(&m, w, &OMNI_ENTRY_FLIP, true);   /* reduced: t x 2.2 */
        omni_model_step(&m, w + 100, true, &tm, OMNI_LENS_OFF, &st);
        TEST_ASSERT_TRUE(st.lid);
        TEST_ASSERT_EQUAL_FLOAT(omni_flip_theta(100, true), st.lid_theta);
        omni_model_step(&m, w + 300, true, &tm, OMNI_LENS_OFF, &st);
        TEST_ASSERT_FALSE(st.lid);
        omni_model_wake(&m, w, &OMNI_ENTRY_FLIP, false);   /* a tap skips the reveal: the lid goes */
        omni_model_step(&m, w + 100, true, &tm, OMNI_LENS_OFF, &st);
        TEST_ASSERT_TRUE(st.lid);
        omni_model_skip(&m, w + 120);
        omni_model_step(&m, w + 130, true, &tm, OMNI_LENS_OFF, &st);
        TEST_ASSERT_FALSE(st.lid);
        omni_model_wake(&m, w, &OMNI_ENTRY_DIAL, false);   /* the other entries have no lid */
        omni_model_step(&m, w + 100, true, &tm, OMNI_LENS_OFF, &st);
        TEST_ASSERT_FALSE(st.lid);
        omni_model_wake(&m, w, NULL, false);
        omni_model_step(&m, w + 100, false, NULL, OMNI_LENS_UNSET, &st);
        TEST_ASSERT_FALSE(st.lid);
    }
    TEST_ASSERT_EQUAL_UINT16(330, OMNI_ENTRY_FLIP.spin_t0);   /* 180 ms into the swing */
}

/* ---- The compositor's lid layer */

static void *g_alloc[64];
static int g_nalloc;
static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    TEST_ASSERT_NOT_NULL(p);
    if (g_nalloc < 64) g_alloc[g_nalloc++] = p;
    return p;
}

static const uint8_t *g_lid;   /* omni_lid.bin's pixels: the RGB565 plane, then the A8 plane */
static int16_t g_runs[4 * W];
static uint8_t g_spec[3 * W];
static uint16_t g_fa[W * W], g_fb[W * W], g_ref[W * W];

static void load_lid(void)
{
    if (g_lid) return;
    char path[512];
    snprintf(path, sizeof path, "%s/omni_lid.bin", ASSETS_DIR);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    uint8_t *p = xmalloc(12 + (size_t)W * W * 3);
    TEST_ASSERT_EQUAL_size_t(12 + (size_t)W * W * 3, fread(p, 1, 12 + (size_t)W * W * 3, f));
    fclose(f);
    TEST_ASSERT_EQUAL_HEX8(0x19, p[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, p[1]);   /* LV_COLOR_FORMAT_RGB565A8 */
    TEST_ASSERT_EQUAL(W, p[4] | p[5] << 8);
    TEST_ASSERT_EQUAL(W, p[6] | p[7] << 8);
    g_lid = p + 12;
    omni_dial_lid_runs(g_lid, g_runs);
}

static uint16_t lid_rgb(int u, int v)
{
    const uint8_t *p = g_lid + ((size_t)v * W + u) * 2;
    return (uint16_t)(p[0] | p[1] << 8);
}
static uint8_t lid_a(int u, int v) { return g_lid[(size_t)W * W * 2 + (size_t)v * W + u]; }

/* the runs: [any0, any1) holds every nonzero alpha of the row, [op0, op1) only 255s */
static void test_lid_runs(void)
{
    load_lid();
    int opaque_rows = 0;
    for (int v = 0; v < W; v++) {
        const int16_t *r = &g_runs[4 * v];
        for (int u = 0; u < W; u++) {
            if (u < r[0] || u >= r[1]) TEST_ASSERT_EQUAL_UINT8(0, lid_a(u, v));
            if (u >= r[2] && u < r[3]) TEST_ASSERT_EQUAL_UINT8(255, lid_a(u, v));
        }
        opaque_rows += r[3] > r[2];
    }
    TEST_ASSERT_TRUE(opaque_rows > 440);
    TEST_ASSERT_TRUE(g_runs[4 * 233 + 3] - g_runs[4 * 233 + 2] > 440);   /* the lid's widest row is almost all opaque */
}

/* a flat synthetic dial (as test_omni_dial's syn_res), its base a pattern so a misplaced pixel shows */
static omni_dial_res_t g_syn;
static omni_strip_t g_sstrip[3];
static uint16_t g_sbase[W * W];
#define SN 256

static void syn_res(void)
{
    omni_dial_res_t *R = &g_syn;
    if (R->base) return;
    for (int i = 0; i < W * W; i++) g_sbase[i] = (uint16_t)(i * 2654435761u >> 16);
    R->base = g_sbase;
    for (int l = 0; l < 3; l++) {
        const uint16_t n = (uint16_t)(SN >> l), S = (uint16_t)(n + OMNI_STRIP_PAD);
        uint16_t *c = xmalloc((size_t)S * 71 * 2);
        uint8_t *a = xmalloc((size_t)S * 71);
        for (int i = 0; i < S * 71; i++) {
            c[i] = omni_pack565(200, 120, 40);
            a[i] = 170;
        }
        g_sstrip[l] = (omni_strip_t){ .n_theta = n, .stride = S, .r_min = 126, .n_r = 71, .level = (uint8_t)l, .rgb = c,
                                      .a = a };
        R->strips[l] = &g_sstrip[l];
    }
    const omni_pmap_cfg_t dc = omni_dial_pmap_cfg(SN);
    const size_t b = omni_pmap_bytes(&dc, false, true);
    TEST_ASSERT_TRUE(omni_pmap_build(&R->dial_map, &dc, false, true, xmalloc(b), b));
    R->rkernel = omni_rings_span_codes;
    load_lid();
    R->lid = g_lid;
    R->lid_run = g_runs;
    omni_dial_lid_spec(g_spec);
    R->lid_spec = g_spec;
}

static omni_dial_frame_t syn_frame(float theta_deg, float fade)
{
    omni_dial_frame_t f = { .res = &g_syn };
    omni_dial_vals(2026, 10, 22, f.st.val);
    memcpy(f.st.sel_old, f.st.val, sizeof f.st.val);
    memcpy(f.st.sel_new, f.st.val, sizeof f.st.val);
    f.st.mark_old = 1;
    f.st.hour_rad = 1.0f;
    f.st.min_rad = 4.0f;
    memcpy(f.pose.val, f.st.val, sizeof f.st.val);
    f.st.lid = theta_deg >= 0;
    f.st.lid_theta = theta_deg >= 0 ? rad(theta_deg) : 0;
    f.st.fade = fade;
    return f;
}

static void render(const omni_dial_frame_t *f, uint16_t *fb)
{
    const omni_rect_t all = { 0, 0, W, W };
    memset(fb, 0x5a, (size_t)W * W * 2);   /* a pixel the render leaves alone shows as garbage */
    omni_dial_render(f, fb, &all);
}

/* the round panel: pixel centres within 233 of the centre (off it, the lid frame is black) */
static bool on_panel(int x, int y)
{
    const int dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
    return dx * dx + dy * dy <= W * W;
}

/* The lid layer by omni_dial.h's rule, pixel by pixel, over dial (the frame without its lid): written apart from the
 * compositor's row loops, from the lines and the spec columns. */
static void reference(const omni_dial_frame_t *f, const uint16_t *dial, uint16_t *out)
{
    omni_hinge_line_t L[W];
    omni_hinge_lines(&OMNI_LID_HINGE, f->st.lid_theta, L);
    uint8_t cols[W];
    omni_lid_spec_cols(cols);
    static const int COL[3] = { 255, 240, 205 };
    const int dim8 = (int)floorf(omni_flip_dim(f->st.lid_theta) * 255 + 0.5f);
    const int fade8 = (int)floorf(f->st.fade * 255 + 0.5f);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            uint16_t d = dial[y * W + x];
            if (dim8) d = omni_blend565(d, 0, dim8);
            const omni_hinge_line_t *l = &L[y];
            if (l->v >= 0) {
                const int64_t inv = l->inv_s_q16;
                const int64_t U0 = (int64_t)233 * 65536 - (465 * inv) / 2;
                const int64_t uq = U0 + x * inv;
                const int u = (int)(uq >> 16);
                const int a = u >= 0 && u < W ? lid_a(u, l->v) : 0;
                if (a) {
                    const uint16_t c = lid_rgb(u, l->v);
                    const int ka = omni_amul((l->dark * a + 127) / 255), sa = omni_amul(l->spec), aa = omni_amul(a);
                    int p[3] = { omni_ex_r(c), omni_ex_g(c), omni_ex_b(c) };
                    const int q[3] = { omni_ex_r(d), omni_ex_g(d), omni_ex_b(d) };
                    for (int k = 0; k < 3; k++) {
                        p[k] = omni_blend8(0, p[k], ka);
                        const int sc = (cols[u] * COL[k] + 127) / 255;
                        p[k] += (sc * sa * aa) >> 16;
                        if (p[k] > 255) p[k] = 255;
                        p[k] = omni_blend8(p[k], q[k], aa);
                    }
                    d = omni_pack565(p[0], p[1], p[2]);
                }
            }
            if (fade8) d = omni_blend565(d, 0, fade8);
            out[y * W + x] = on_panel(x, y) ? d : 0;
        }
}

/* theta 0: v = y and 1 / s = 1.0 on every row, and the rendered lid is the bitmap composited over the dial (as dimmed
 * by omni_flip_dim(0)), byte for byte, by the one blend rule, on the panel (black off it) */
static void test_lid_identity_at_zero(void)
{
    omni_hinge_line_t L[W];
    omni_hinge_lines(&OMNI_LID_HINGE, 0, L);
    for (int y = 0; y < W; y++) {
        TEST_ASSERT_EQUAL_INT16(y, L[y].v);
        TEST_ASSERT_EQUAL_UINT32(65536, L[y].inv_s_q16);
        TEST_ASSERT_EQUAL_UINT8(0, L[y].dark);
        TEST_ASSERT_EQUAL_UINT8(0, L[y].spec);
    }
    syn_res();
    const omni_dial_frame_t nolid = syn_frame(-1, 0), f = syn_frame(0, 0);
    render(&nolid, g_fa);
    render(&f, g_fb);
    const int dim8 = (int)floorf(omni_flip_dim(0) * 255 + 0.5f);
    TEST_ASSERT_EQUAL(235, dim8);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            g_ref[y * W + x] =
                on_panel(x, y) ? omni_blend565(omni_blend565(g_fa[y * W + x], 0, dim8), lid_rgb(x, y), lid_a(x, y)) : 0;
    TEST_ASSERT_EQUAL_HEX16_ARRAY(g_ref, g_fb, W * W);
}

/* At 10, 30, 60 and 85 deg (and with the fade on): the compositor equals the per-pixel reference; with the runs table
 * gone (no dial skipped under the lid) it gives the same bytes; and cut into odd rects, the same again. */
static void test_lid_layer_matches_reference(void)
{
    syn_res();
    static const struct { float deg, fade; } T[] = {
        { 10, 0 }, { 30, 0 }, { 60, 0 }, { 85, 0 }, { 4, 0.6f }, { 30, 0.2f },
    };
    const omni_dial_frame_t nolid = syn_frame(-1, 0);
    render(&nolid, g_fa);
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        const omni_dial_frame_t f = syn_frame(T[i].deg, T[i].fade);
        reference(&f, g_fa, g_ref);
        render(&f, g_fb);
        char msg[48];
        snprintf(msg, sizeof msg, "theta %.0f fade %.1f", (double)T[i].deg, (double)T[i].fade);
        TEST_ASSERT_EQUAL_HEX16_ARRAY_MESSAGE(g_ref, g_fb, W * W, msg);
        g_syn.lid_run = NULL;
        render(&f, g_fb);
        g_syn.lid_run = g_runs;
        TEST_ASSERT_EQUAL_HEX16_ARRAY_MESSAGE(g_ref, g_fb, W * W, msg);
        static const int xs[] = { 0, 37, 150, 233, 301, 466 }, ys[] = { 0, 9, 100, 177, 290, 466 };
        memset(g_fb, 0x5a, sizeof g_fb);
        omni_dial_prep_t p;
        omni_dial_prepare(&f, &p);
        for (int a = 0; a < 5; a++)
            for (int b = 0; b < 5; b++) {
                const omni_rect_t r = { (int16_t)xs[a], (int16_t)ys[b], (int16_t)xs[a + 1], (int16_t)ys[b + 1] };
                omni_dial_render_under(&p, g_fb, &r);
                omni_dial_render_over(&p, g_fb, &r);
            }
        TEST_ASSERT_EQUAL_HEX16_ARRAY_MESSAGE(g_ref, g_fb, W * W, msg);
    }
    /* no lid bitmap: the dial alone, undimmed */
    const omni_dial_frame_t f = syn_frame(30, 0);
    g_syn.lid = NULL;
    render(&f, g_fb);
    g_syn.lid = g_lid;
    TEST_ASSERT_EQUAL_HEX16_ARRAY(g_fa, g_fb, W * W);
}

/* The fade is over the whole composite (spec band and anti-aliased edge included), as the mockup's fadeIn: the first
 * frames of the flip against the same frame rendered with no fade and then blended to black by the fade (the
 * reviewer's harness, scratchpad fade.c, which asked for no pixel more than 8 levels off): byte for byte equal, and
 * the first frame (fade 1) all black. */
static void test_lid_fade_is_true_fade(void)
{
    syn_res();
    static const uint32_t T[] = { 0, 16, 33, 48, 66, 100, 150, 200, 299 };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        omni_dial_frame_t f = syn_frame(0, 0);
        f.st.lid_theta = omni_flip_theta(T[i], false);
        f.st.fade = omni_flip_fade(T[i]);
        render(&f, g_fb);
        omni_dial_frame_t f0 = f;
        f0.st.fade = 0;
        render(&f0, g_fa);
        const int f8 = (int)floorf(f.st.fade * 255 + 0.5f);
        int worst = 0;
        for (int p = 0; p < W * W; p++) {
            const uint16_t ideal = omni_blend565(g_fa[p], 0, f8), d = g_fb[p];
            const int e[3] = { abs(omni_ex_r(d) - omni_ex_r(ideal)), abs(omni_ex_g(d) - omni_ex_g(ideal)),
                               abs(omni_ex_b(d) - omni_ex_b(ideal)) };
            for (int k = 0; k < 3; k++) worst = e[k] > worst ? e[k] : worst;
            if (T[i] == 0) TEST_ASSERT_EQUAL_HEX16(0, d);
        }
        char msg[32];
        snprintf(msg, sizeof msg, "t %u ms: worst %d", (unsigned)T[i], worst);
        TEST_ASSERT_TRUE_MESSAGE(worst == 0, msg);
    }
}

/* the whole screen while the lid is set in either frame, unless the two are identical */
static void test_lid_dirty_full(void)
{
    syn_res();
    omni_rect_t r[OMNI_DIRTY_MAX];
    const omni_dial_frame_t a = syn_frame(30, 0), b = syn_frame(31, 0), off = syn_frame(-1, 0);
    TEST_ASSERT_EQUAL(1, omni_dial_dirty(&a, &b, r));
    TEST_ASSERT_TRUE(r[0].x0 == 0 && r[0].y0 == 0 && r[0].x1 == W && r[0].y1 == W);
    TEST_ASSERT_EQUAL(0, omni_dial_dirty(&a, &a, r));   /* a held lid over a still dial: nothing */
    omni_dial_frame_t a2 = a;
    a2.st.lon_deg += 1;   /* the globe turning under a held lid: all of it */
    TEST_ASSERT_EQUAL(1, omni_dial_dirty(&a, &a2, r));
    TEST_ASSERT_EQUAL(1, omni_dial_dirty(&b, &off, r));   /* the frame the lid leaves: all of it */
    TEST_ASSERT_TRUE(r[0].x0 == 0 && r[0].y0 == 0 && r[0].x1 == W && r[0].y1 == W);
    TEST_ASSERT_EQUAL(0, omni_dial_dirty(&off, &off, r));
}

#else /* OMNI_FLIP_MOCKUP: the mockup's flipTheta, and its entry (this build has its own lid.c and omni_model.c) */

/* theta(380) 4 deg, (540) 2.5, (1110) 56.5, (1640) 110.5 */
static void test_flip_timeline_mockup(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0, omni_flip_theta(280, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(4), omni_flip_theta(380, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(2.5f), omni_flip_theta(540, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(2.5f), omni_flip_theta(580, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(56.5f), omni_flip_theta(1110, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(110.5f), omni_flip_theta(1640, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(110.5f), omni_flip_theta((uint32_t)lroundf(1640 / 2.2f), true));
}

/* The mockup's entry {760, 3600, 4000, 990, 0}, derived from the timeline, run through the model: the lid at 4 deg at
 * 380 ms and 56.5 at 1110, the globe held at its full spin offset until 760 ms and turning after it, green from 4000 */
static void test_mockup_entry(void)
{
    TEST_ASSERT_EQUAL_UINT16(760, OMNI_ENTRY_FLIP.spin_t0);
    TEST_ASSERT_EQUAL_UINT16(4000, OMNI_ENTRY_FLIP.green_at);
    TEST_ASSERT_EQUAL_UINT16(3600, OMNI_ENTRY_FLIP.spin_dur);
    struct tm tm = { .tm_year = 126, .tm_mon = 9, .tm_mday = 22, .tm_hour = 10, .tm_min = 10, .tm_sec = 40 };
    omni_model_t m;
    omni_dial_state_t st;
    omni_model_init(&m, 39.96, -83.0);
    omni_model_wake(&m, 1000, &OMNI_ENTRY_FLIP, false);
    omni_model_step(&m, 1000 + 380, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_TRUE(st.lid);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(4), st.lid_theta);
    const float held = st.lon_deg;
    omni_model_step(&m, 1000 + 760, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_EQUAL_FLOAT(held, st.lon_deg);
    omni_model_step(&m, 1000 + 1110, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_TRUE(st.lid);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, rad(56.5f), st.lid_theta);
    TEST_ASSERT_TRUE(st.lon_deg != held);
    TEST_ASSERT_EQUAL_FLOAT(0, st.green);
    omni_model_step(&m, 1000 + 4350, true, &tm, OMNI_LENS_OFF, &st);
    TEST_ASSERT_FALSE(st.lid);
    TEST_ASSERT_EQUAL_FLOAT(1, st.green);
}

#endif

int main(void)
{
    UNITY_BEGIN();
#if !(defined(OMNI_FLIP_MOCKUP) && OMNI_FLIP_MOCKUP)
    RUN_TEST(test_lid_flat_matches_mockup);
    RUN_TEST(test_lid_rows_shading);
    RUN_TEST(test_flip_dim);
    RUN_TEST(test_hinge_lines_same_for_either_axis);
    RUN_TEST(test_hinge_flat_leaf);
    RUN_TEST(test_hinge_gone);
    RUN_TEST(test_lines_at_slices);
    RUN_TEST(test_flip_timeline);
    RUN_TEST(test_model_flip_state);
    RUN_TEST(test_lid_runs);
    RUN_TEST(test_lid_identity_at_zero);
    RUN_TEST(test_lid_layer_matches_reference);
    RUN_TEST(test_lid_fade_is_true_fade);
    RUN_TEST(test_lid_dirty_full);
    for (int i = 0; i < g_nalloc; i++) free(g_alloc[i]);
#else
    RUN_TEST(test_flip_timeline_mockup);
    RUN_TEST(test_mockup_entry);
#endif
    return UNITY_END();
}
