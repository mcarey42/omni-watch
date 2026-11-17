#include "unity.h"
#include "omni/blend565.h"
#include "omni/iris.h"
#include "omni/omni_dial.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The Omni dial compositor (Task 6). Most tests run on the shipped assets (ASSETS_DIR), loaded as dial_view (Task 7)
 * will: the base, strips and dial map in place, the overlays as compact spans at their assets_meta.h origins, the
 * Omni globe from omni512.tex. test_dial_order and test_layer_odd_origin use flat synthetic resources. */

void setUp(void) {}
void tearDown(void) {}

#define W OMNI_DIAL_W
#define N 2048

static uint16_t g_fa[W * W], g_fb[W * W];

/* Everything the fixtures allocate, freed at the end (LeakSanitizer). */
static void *g_alloc[256];
static int g_nalloc;
static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p && g_nalloc < 256) g_alloc[g_nalloc++] = p;
    return p;
}

/* ---- Loading the assets */

static uint8_t *read_file(const char *name, size_t *size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", ASSETS_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = xmalloc((size_t)n);
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) p = NULL;
    fclose(f);
    *size = (size_t)n;
    return p;
}

/* An LVGL v9 image (12-byte header): its pixels, w and h. */
static const uint8_t *lv_img(const char *name, uint8_t cf, int *w, int *h)
{
    size_t size;
    uint8_t *p = read_file(name, &size);
    TEST_ASSERT_NOT_NULL_MESSAGE(p, name);
    TEST_ASSERT_EQUAL_HEX8(0x19, p[0]);
    TEST_ASSERT_EQUAL_HEX8(cf, p[1]);
    *w = p[4] | p[5] << 8;
    *h = p[6] | p[7] << 8;
    return p + 12;
}

static omni_dial_layer_t layer(const char *name, int x, int y)
{
    int w, h;
    const uint32_t *argb = (const uint32_t *)(const void *)lv_img(name, 0x10, &w, &h);
    omni_dial_layer_t l = { .x = (int16_t)x, .y = (int16_t)y };
    const size_t b = omni_overlay_bytes(argb, (uint16_t)w, (uint16_t)h, (uint16_t)w);
    TEST_ASSERT_TRUE(omni_overlay_build(&l.ov, argb, (uint16_t)w, (uint16_t)h, (uint16_t)w, xmalloc(b), b));
    return l;
}

static uint16_t rgb(int r, int g, int b) { return omni_pack565(r, g, b); }

static omni_dial_res_t g_res;
static omni_strip_t g_strip[3];
static omni_globe_lut_t g_lut;
static omni_globe_clut_t g_clut;
static omni_globe_tex_t g_tex;
static omni_globe_shade_t g_shade;
static omni_pmap_t g_loupe_map;
static void *g_loupe_mem;
static uint16_t g_loupe_base[100 * 200];
static float g_loupe_a = -1;

static void test_real_res_loads(void)
{
    omni_dial_res_t *R = &g_res;
    int w, h;
    R->base = (const uint16_t *)(const void *)lv_img("omni_base.bin", 0x12, &w, &h);
    TEST_ASSERT_EQUAL(W, w);
    for (int l = 0; l < 3; l++) {
        char name[32];
        snprintf(name, sizeof name, "omni_strip%d.bin", l);
        size_t size;
        const uint8_t *p = read_file(name, &size);
        TEST_ASSERT_TRUE(omni_strip_map(&g_strip[l], p, size));
        R->strips[l] = &g_strip[l];
    }
    size_t size;
    const uint8_t *mf = read_file("omni_dialmap.bin", &size);
    const omni_pmap_cfg_t dc = omni_dial_pmap_cfg(N);
    const size_t mb = omni_pmap_file_mem(mf, size);
    char why[64] = "";
    TEST_ASSERT_TRUE_MESSAGE(omni_pmap_load(&R->dial_map, &dc, mf, size, xmalloc(mb), mb, why, sizeof why), why);
    TEST_ASSERT_TRUE_MESSAGE(omni_rings_compatible(R->strips, &R->dial_map, why, sizeof why), why);
    const omni_pmap_cfg_t mc = omni_dial_mag_cfg(N);
    const size_t gb = omni_pmap_bytes(&mc, false, true);
    TEST_ASSERT_TRUE(omni_pmap_build(&R->mag_map, &mc, false, true, xmalloc(gb), gb));
    R->mag_base = xmalloc((size_t)(mc.rect.x1 - mc.rect.x0) * (mc.rect.y1 - mc.rect.y0) * 2);
    omni_dial_zoom_base(R->base, &mc, R->mag_base);
    R->top = layer("omni_top.bin", 101, 101);
    R->over = layer("omni_over.bin", 97, 0);
    R->dome = layer("omni_dome.bin", 121, 121);
    R->glass = layer("omni_glass.bin", 207, 18);
    R->lens[0] = layer("omni_lens_red_off.bin", 104, 28);
    R->lens[1] = layer("omni_lens_red_on.bin", 104, 28);
    R->lens[2] = layer("omni_lens_green_off.bin", 319, 28);
    R->lens[3] = layer("omni_lens_green_on.bin", 319, 28);
    /* the blooms: their tint stops (manifest.json) as colour(a) = out + (in - out) a / 255, with in the inner stop
     * carried out to a = 255 (the A8 peaks at 0.75 / 0.7) */
    R->bloom[0] = (omni_dial_a8_t){ .x = 56, .y = -23, .rgb_in = rgb(255, 113, 63), .rgb_out = rgb(200, 20, 10) };
    R->bloom[1] = (omni_dial_a8_t){ .x = 271, .y = -23, .rgb_in = rgb(149, 255, 146), .rgb_out = rgb(20, 180, 60) };
    for (int i = 0; i < 2; i++) {
        int bw, bh;
        R->bloom[i].a = lv_img(i ? "omni_bloom_green.bin" : "omni_bloom_red.bin", 0x0e, &bw, &bh);
        R->bloom[i].w = (int16_t)bw;
        R->bloom[i].h = (int16_t)bh;
    }
    /* the Omni globe (Task 5): compact LUT, look kind 1, omni512 */
    const size_t lb = omni_globe_lut_bytes(&OMNI_DIAL_GLOBE_CFG);
    TEST_ASSERT_TRUE(omni_globe_lut_build(&g_lut, &OMNI_DIAL_GLOBE_CFG, xmalloc(lb), lb));
    const size_t cb = omni_globe_compact_bytes(&g_lut);
    TEST_ASSERT_TRUE(omni_globe_compact(&g_clut, &g_lut, xmalloc(cb), cb));
    omni_globe_shade_build(&g_shade, &OMNI_DIAL_GLOBE_LOOK);
    const uint8_t *tex = read_file("omni512.tex", &size);
    TEST_ASSERT_EQUAL_size_t(512 * 256 * 4, size);
    g_tex = (omni_globe_tex_t){ 512, 256, (const uint32_t *)(const void *)tex };
    R->globe = (omni_globe_frame_t){ .lut = &g_lut, .tex = &g_tex, .shade = &g_shade, .clut = &g_clut, .fade = 255 };
    omni_globe_sun_studio(&R->globe.sun);
    R->clut = &g_clut;
    R->ckernel = omni_globe_cspan_ref;
    R->rkernel = omni_rings_span_codes;
    R->loupe_map = &g_loupe_map;
    R->loupe_base = g_loupe_base;
    int gw, gh;   /* the iris's grain (Task 10): omni_blade, A8 256 x 512 */
    R->iris_grain = lv_img("omni_blade.bin", 0x0e, &gw, &gh);
    TEST_ASSERT_EQUAL(256, gw);
    TEST_ASSERT_EQUAL(512, gh);
}

/* The iris (Task 10) at ap over frame f, as omni_model steps it (s = ap / 207) */
static omni_dial_frame_t iris_frame(omni_dial_frame_t f, float ap, float phi)
{
    f.st.iris = true;
    f.st.iris_ap = ap;
    f.st.iris_phi = phi;
    f.st.iris_s = ap / OMNI_IRIS_AP_OPEN;
    return f;
}

/* What dial_view's worker does before a render whose loupe size changed. */
static void loupe_for(float a)
{
    if (a == g_loupe_a || a <= 0) return;
    g_loupe_a = a;
    free(g_loupe_mem);
    const omni_pmap_cfg_t c = omni_dial_loupe_cfg(a, N);
    const size_t b = omni_pmap_bytes(&c, false, true);
    g_loupe_mem = malloc(b);
    TEST_ASSERT_TRUE(omni_pmap_build(&g_loupe_map, &c, false, true, g_loupe_mem, b));
    TEST_ASSERT_TRUE((c.rect.x1 - c.rect.x0) * (c.rect.y1 - c.rect.y0) <= (int)(sizeof g_loupe_base / 2));
    omni_dial_zoom_base(g_res.base, &c, g_loupe_base);
}

/* The dial at rest at 10:10:40 on 2026-10-22, home Columbus, 6 s after a plain wake: the reference frame's state. */
static omni_dial_frame_t rest_frame(void)
{
    omni_model_t m;
    omni_model_init(&m, 39.96, -83.0);
    omni_model_wake(&m, 0, &OMNI_ENTRY_DIAL, false);
    struct tm tm = { .tm_year = 126, .tm_mon = 9, .tm_mday = 22, .tm_hour = 10, .tm_min = 10, .tm_sec = 40 };
    omni_dial_frame_t f = { .res = &g_res };
    omni_model_step(&m, 6000, true, &tm, OMNI_LENS_OFF, &f.st);
    for (int i = 0; i < OMNI_RINGS; i++) f.pose.val[i] = f.st.val[i];
    omni_rings_fidelity(&f.pose, f.st.speed);
    return f;
}

static void render_full(const omni_dial_frame_t *f, uint16_t *fb)
{
    loupe_for(f->st.loupe);
    const omni_rect_t all = { 0, 0, W, W };
    memset(fb, 0, (size_t)W * W * 2);
    omni_dial_render(f, fb, &all);
}

/* ---- Tests */

/* f 0.92: (255, 250, 235) at r 0, (187.7, 161.9, 125.1) at r 117, (93.8, 58.9, 33.1) from r 260; f 0.2: (56.1, 55.0, 51.7)
 * at r 0. The table the frame uses gives those to the 565 step. */
static void test_flash_table(void)
{
    const struct { float f, d, c[3]; } T[] = {
        { 0.92f, 0, { 255, 250, 235 } },         { 0.92f, 117, { 187.7f, 161.9f, 125.1f } },
        { 0.92f, 260, { 93.8f, 58.9f, 33.1f } }, { 0.92f, 330, { 93.8f, 58.9f, 33.1f } },
        { 0.2f, 0, { 56.1f, 55.0f, 51.7f } },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        float c[3];
        omni_flash_rgb(T[i].f, T[i].d, c);
        for (int k = 0; k < 3; k++) TEST_ASSERT_FLOAT_WITHIN(1.0f, T[i].c[k], c[k]);
    }
    memset(g_fa, 0, sizeof g_fa);
    const omni_rect_t all = { 0, 0, W, W };
    omni_flash(g_fa, W, &all, 0.92f);
    TEST_ASSERT_EQUAL_HEX16(omni_pack565(94, 59, 33), g_fa[0]);                  /* a corner: beyond r 260 */
    TEST_ASSERT_EQUAL_HEX16(omni_pack565(255, 250, 235), g_fa[232 * W + 232]);   /* r 0.7 */
    /* r 117 within a 565 step: the pixel (350, 232) is at 117.0 */
    const uint16_t p = g_fa[232 * W + 349];
    TEST_ASSERT_INT_WITHIN(8, 188, omni_ex_r(p));
    TEST_ASSERT_INT_WITHIN(4, 162, omni_ex_g(p));
    TEST_ASSERT_INT_WITHIN(8, 125, omni_ex_b(p));
    /* additive, saturating: over white it stays white; f 0 adds nothing */
    for (int i = 0; i < W * W; i++) g_fb[i] = 0xffff;
    omni_flash(g_fb, W, &all, 0.92f);
    for (int i = 0; i < W * W; i += 97) TEST_ASSERT_EQUAL_HEX16(0xffff, g_fb[i]);
    memset(g_fb, 0, sizeof g_fb);
    omni_flash(g_fb, W, &all, 0);
    for (int i = 0; i < W * W; i += 97) TEST_ASSERT_EQUAL_HEX16(0, g_fb[i]);
}

static void test_magnifier_map(void)
{
    const omni_pmap_cfg_t c = omni_dial_mag_cfg(N);
    TEST_ASSERT_EQUAL(207, c.rect.x0);
    TEST_ASSERT_EQUAL(18, c.rect.y0);
    TEST_ASSERT_EQUAL(259, c.rect.x1);
    TEST_ASSERT_EQUAL(122, c.rect.y1);
    TEST_ASSERT_EQUAL_FLOAT(1.4f, c.k);
    TEST_ASSERT_EQUAL_FLOAT(3, c.rr);
    /* pixel (233, 70), centre (233.5, 70.5) -> source (233.357, 70.5) */
    const float qx = c.sx + (233.5f - c.dx) / c.k, qy = c.sy + (70.5f - c.dy) / c.k;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 233.357f, qx);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 70.5f, qy);
    const size_t b = omni_pmap_bytes(&c, true, true);
    omni_pmap_t m;
    void *mem = malloc(b);
    TEST_ASSERT_TRUE(omni_pmap_build(&m, &c, true, true, mem, b));
    bool row18 = false, row31 = false, px = false;
    for (uint32_t i = 0; i < m.nspans; i++) {
        const omni_pspan_t *s = &m.span[i];
        row18 |= s->y == 18;   /* the top row maps to r 199.6: no ring, mag_base only (C10) */
        if (s->y == 31 && s->x0 <= 233 && s->x0 + s->n > 233) {
            row31 = true;
            TEST_ASSERT_EQUAL_UINT8(3, s->ring);   /* century */
            TEST_ASSERT_INT_WITHIN(1, 190 - 126, m.ri[s->lut + 233 - s->x0]);
        }
        if (s->y == 70 && s->x0 <= 233 && s->x0 + s->n > 233) {
            px = true;   /* (233.357, 70.5): r 162.5, theta 0.357 / 162.5 rad */
            TEST_ASSERT_EQUAL_UINT8(2, s->ring);
            TEST_ASSERT_EQUAL_UINT8(162 - 126, m.ri[s->lut + 233 - s->x0]);
            TEST_ASSERT_EQUAL_UINT16((uint16_t)floor(atan2(0.357, 162.5) / (2 * 3.14159265358979) * N), m.th[s->lut + 233 - s->x0]);
        }
    }
    TEST_ASSERT_FALSE(row18);
    TEST_ASSERT_TRUE(row31);
    TEST_ASSERT_TRUE(px);
    printf("magnifier map: %u spans, %u px (%u B with codes only)\n", (unsigned)m.nspans, (unsigned)m.npx,
           (unsigned)omni_pmap_bytes(&c, false, true));
    free(mem);
}

/* ---- Synthetic flat resources */

static uint32_t argb(int a, int r, int g, int b) { return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b; }

static omni_dial_layer_t flat_layer(int x, int y, int w, int h, uint32_t c)
{
    uint32_t *img = xmalloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) img[i] = c;
    omni_dial_layer_t l = { .x = (int16_t)x, .y = (int16_t)y };
    const size_t b = omni_overlay_bytes(img, (uint16_t)w, (uint16_t)h, (uint16_t)w);
    TEST_ASSERT_TRUE(omni_overlay_build(&l.ov, img, (uint16_t)w, (uint16_t)h, (uint16_t)w, xmalloc(b), b));
    return l;   /* img is the layer's pixels: kept */
}

#define SN 256
static omni_dial_res_t g_syn;
static omni_strip_t g_sstrip[3];
static uint16_t g_sbase[W * W], g_smag[52 * 104];
#define C_BASE omni_pack565(40, 60, 90)
#define C_RING omni_pack565(200, 120, 40)
#define A_RING 170
#define C_TOP argb(140, 30, 200, 60)
#define C_OVER argb(90, 250, 10, 120)
#define C_MAG omni_pack565(70, 20, 150)
#define C_GLASS argb(60, 255, 255, 255)

static void syn_res(void)
{
    omni_dial_res_t *R = &g_syn;
    memset(R, 0, sizeof *R);
    for (int i = 0; i < W * W; i++) g_sbase[i] = C_BASE;
    for (int i = 0; i < 52 * 104; i++) g_smag[i] = C_MAG;
    R->base = g_sbase;
    for (int l = 0; l < 3; l++) {
        const uint16_t n = (uint16_t)(SN >> l), S = (uint16_t)(n + OMNI_STRIP_PAD);
        uint16_t *c = xmalloc((size_t)S * 71 * 2);
        uint8_t *a = xmalloc((size_t)S * 71);
        for (int i = 0; i < S * 71; i++) {
            c[i] = C_RING;
            a[i] = A_RING;
        }
        g_sstrip[l] = (omni_strip_t){ .n_theta = n, .stride = S, .r_min = 126, .n_r = 71, .level = (uint8_t)l, .rgb = c, .a = a };
        R->strips[l] = &g_sstrip[l];
    }
    const omni_pmap_cfg_t dc = omni_dial_pmap_cfg(SN), mc = omni_dial_mag_cfg(SN);
    size_t b = omni_pmap_bytes(&dc, false, true);
    TEST_ASSERT_TRUE(omni_pmap_build(&R->dial_map, &dc, false, true, xmalloc(b), b));
    b = omni_pmap_bytes(&mc, false, true);
    TEST_ASSERT_TRUE(omni_pmap_build(&R->mag_map, &mc, false, true, xmalloc(b), b));
    R->top = flat_layer(101, 70, 264, 60, C_TOP);
    R->over = flat_layer(97, 0, 272, 131, C_OVER);
    R->glass = flat_layer(207, 18, 52, 104, C_GLASS);
    R->rkernel = omni_rings_span_codes;
    R->ckernel = omni_globe_cspan_ref;
}

static omni_dial_frame_t syn_frame(void)
{
    omni_dial_frame_t f = { .res = &g_syn };
    omni_dial_vals(2026, 10, 22, f.st.val);
    memcpy(f.st.sel_old, f.st.val, sizeof f.st.val);
    memcpy(f.st.sel_new, f.st.val, sizeof f.st.val);
    f.st.mark_old = 1;
    f.st.hour_rad = f.st.min_rad = 3.0f;   /* both hands near 6 o'clock */
    memcpy(f.pose.val, f.st.val, sizeof f.st.val);
    return f;
}

static uint16_t over8(uint16_t dst, uint32_t c)   /* an overlay pixel by the C rule (globe_render.c over_run_c) */
{
    const int ap = omni_amul((int)(c >> 24));
    return omni_pack565(omni_blend8((int)(c >> 16 & 255), omni_ex_r(dst), ap), omni_blend8((int)(c >> 8 & 255), omni_ex_g(dst), ap),
                        omni_blend8((int)(c & 255), omni_ex_b(dst), ap));
}

/* (233, 100) has the month ring (r 133), the top and the over layer: base, ring, top, over in drawDial's order. Inside
 * the magnifier, with it on: mag_base, the ring through the magnifier map (source r 141.1), the additive
 * (40, 32, 18) x .35 = (14, 11, 6), the glass. */
static void test_dial_order(void)
{
    syn_res();
    omni_dial_frame_t f = syn_frame();
    uint16_t *mag = g_syn.mag_base;
    g_syn.mag_base = NULL;   /* no magnifier: the layers under it show */
    const omni_rect_t all = { 0, 0, W, W };
    omni_dial_render(&f, g_fa, &all);
    const uint16_t want = over8(over8(omni_blend565(C_BASE, C_RING, A_RING), C_TOP), C_OVER);
    TEST_ASSERT_EQUAL_HEX16(want, g_fa[100 * W + 233]);
    TEST_ASSERT_EQUAL_HEX16(C_BASE, g_fa[300 * W + 10]);   /* outside everything */
    g_syn.mag_base = g_smag;
    omni_dial_render(&f, g_fa, &all);
    const uint16_t ring = omni_blend565(C_MAG, C_RING, A_RING);
    const uint16_t tint = omni_pack565(omni_ex_r(ring) + 14, omni_ex_g(ring) + 11, omni_ex_b(ring) + 6);
    TEST_ASSERT_EQUAL_HEX16(over8(tint, C_GLASS), g_fa[100 * W + 233]);
    (void)mag;
}

/* A 3 x 3 layer at (101, 57) lands exactly there; cut by a rect edge it composites as it does whole. */
static void test_layer_odd_origin(void)
{
    syn_res();
    uint32_t img[9];
    for (int i = 0; i < 9; i++) img[i] = argb(100 + 15 * i, 20 * i, 255 - 20 * i, 128);
    omni_dial_layer_t l = { .x = 101, .y = 57 };
    const size_t b = omni_overlay_bytes(img, 3, 3, 3);
    TEST_ASSERT_TRUE(omni_overlay_build(&l.ov, img, 3, 3, 3, xmalloc(b), b));
    g_syn.top = l;
    g_syn.over = g_syn.glass = (omni_dial_layer_t){ 0 };
    g_syn.mag_base = NULL;
    omni_dial_frame_t f = syn_frame();
    const omni_rect_t all = { 0, 0, W, W };
    omni_dial_render(&f, g_fa, &all);
    for (int y = 50; y < 66; y++)
        for (int x = 95; x < 110; x++) {
            const bool in = x >= 101 && x < 104 && y >= 57 && y < 60;
            const uint16_t want = in ? over8(C_BASE, img[(y - 57) * 3 + (x - 101)]) : C_BASE;
            TEST_ASSERT_EQUAL_HEX16(want, g_fa[y * W + x]);
        }
    const omni_rect_t cuts[3] = { { 102, 0, 466, 466 }, { 0, 58, 103, 59 }, { 102, 58, 103, 60 } };
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < W * W; i++) g_fb[i] = 0x1234;
        omni_dial_render(&f, g_fb, &cuts[k]);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const bool in = x >= cuts[k].x0 && x < cuts[k].x1 && y >= cuts[k].y0 && y < cuts[k].y1;
                TEST_ASSERT_EQUAL_HEX16(in ? g_fa[y * W + x] : 0x1234, g_fb[y * W + x]);
            }
    }
}

/* A frame with everything on: loupe opening, both lenses lit, marker pulsing, a flash, rings mid-turn. */
static omni_dial_frame_t busy_frame(void)
{
    omni_dial_frame_t f = rest_frame();
    f.st.loupe = 0.6f;
    f.st.red = 0.7f;
    f.st.green = 0.4f;
    f.st.marker_pulse = 0.5f;
    f.st.flash = 0.3f;
    f.st.min_rad = 0.05f;   /* across the magnifier and the loupe's source */
    f.st.mark_new = 0.5f;
    f.st.sel_new[1] = 23;
    f.pose.val[2] += 0.37f;
    f.pose.level[3] = 1;
    return f;
}

static void render_grid(const omni_dial_frame_t *f, const int *xs, int nx, const int *ys, int ny, uint16_t *fb)
{
    for (int i = 0; i < W * W; i++) fb[i] = 0xdead;
    for (int j = 0; j + 1 < ny; j++)
        for (int i = 0; i + 1 < nx; i++) {
            const omni_rect_t r = { (int16_t)xs[i], (int16_t)ys[j], (int16_t)xs[i + 1], (int16_t)ys[j + 1] };
            omni_dial_render(f, fb, &r);
        }
}

/* Every rect of a 3 x 3 grid (lines through the disc, the hands, both lenses, the magnifier and the loupe) gives the
 * full render's frame byte for byte; so do 16-row bands, and the fast fidelity. */
static void test_dial_rect_equals_full(void)
{
    omni_dial_frame_t fr[4] = { busy_frame(), busy_frame(), iris_frame(busy_frame(), 60, 0.4f), iris_frame(busy_frame(), 0, 0) };
    fr[1].fast = true;
    fr[1].st.flash = 0;
    fr[3].st.fade = 0.3f;
    for (int k = 0; k < 4; k++) {
        render_full(&fr[k], g_fa);
        const int xs[4] = { 0, 125, 340, W }, ys[4] = { 0, 47, 200, W };
        render_grid(&fr[k], xs, 4, ys, 4, g_fb);
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
        const int xs2[4] = { 0, 233, 236, W }, ys2[4] = { 0, 101, 233, W };   /* odd cuts: the hands' axis, the centre */
        render_grid(&fr[k], xs2, 4, ys2, 4, g_fb);
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
        int bands[32], n = 0;
        for (int y = 0; y < W; y += 16) bands[n++] = y;
        bands[n++] = W;
        const int xa[2] = { 0, W };
        render_grid(&fr[k], xa, 2, bands, n, g_fb);
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
    }
}

/* Task 7 (T6 M4): a frame prepared once (omni_dial_prepare) and rendered rect by rect from it is the full render byte
 * for byte: the busy frame, its fast fidelity and the loupe opening (its map rebuilt first), in 16-row chunks and an odd
 * grid. */
static void test_prepared_equals_render(void)
{
    static omni_dial_prep_t prep;
    omni_dial_frame_t fr[3] = { busy_frame(), busy_frame(), busy_frame() };
    fr[1].fast = true;
    fr[1].st.flash = 0;
    fr[2].st.loupe = 0.37f;
    fr[2].st.flash = 0;
    for (int k = 0; k < 3; k++) {
        render_full(&fr[k], g_fa);   /* also rebuilds the loupe map for fr[k] */
        omni_dial_prepare(&fr[k], &prep);
        for (int i = 0; i < W * W; i++) g_fb[i] = 0xdead;
        for (int y = 0; y < W; y += 16) {
            const omni_rect_t r = { 0, (int16_t)y, W, (int16_t)(y + 16 < W ? y + 16 : W) };
            omni_dial_render_prepared(&prep, g_fb, &r);
        }
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
        const int xs[4] = { 0, 233, 236, W }, ys[4] = { 0, 101, 233, W };
        for (int i = 0; i < W * W; i++) g_fb[i] = 0xdead;
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++) {
                const omni_rect_t r = { (int16_t)xs[i], (int16_t)ys[j], (int16_t)xs[i + 1], (int16_t)ys[j + 1] };
                omni_dial_render_prepared(&prep, g_fb, &r);
            }
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
    }
}

/* R88: the render in two stages. Under (base, rings, V marks, hands, top) then over (globe .. flash) over every rect is
 * the full render; and a frame's under stage cached, restored into a buffer that holds another frame, then the over
 * stage of a frame with the same under-state (here the globe and the marker moved) gives that frame exactly;
 * omni_dial_marker_box holds every pixel the marker changes outside the disc. */
static void test_under_over_drift(void)
{
    static omni_dial_prep_t pa, pb;
    static uint16_t under[W * W];
    omni_dial_frame_t a = busy_frame(), b = a;
    a.st.flash = b.st.flash = 0;
    b.st.lon_deg += 7.5f;   /* the globe and the marker move; nothing under the globe does */
    render_full(&b, g_fa);
    omni_dial_prepare(&b, &pb);
    const omni_rect_t all = { 0, 0, W, W };
    for (int i = 0; i < W * W; i++) g_fb[i] = 0xdead;
    omni_dial_render_under(&pb, g_fb, &all);
    omni_dial_render_over(&pb, g_fb, &all);
    TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
    /* under cached from b's prep (same under-state as a), restored over a's frame in the globe box */
    omni_dial_prepare(&a, &pa);
    for (int i = 0; i < W * W; i++) under[i] = 0xdead;
    const omni_rect_t box = { 121, 121, 345, 345 };
    omni_dial_render_under(&pa, under, &box);
    render_full(&a, g_fb);
    for (int y = box.y0; y < box.y1; y++) memcpy(g_fb + y * W + box.x0, under + y * W + box.x0, (size_t)(box.x1 - box.x0) * 2);
    omni_dial_render_over(&pb, g_fb, &box);
    omni_rect_t ma, mb;
    omni_dial_marker_box(&a, &ma);
    omni_dial_marker_box(&b, &mb);
    TEST_ASSERT_TRUE(ma.x1 > ma.x0 && mb.x1 > mb.x0);
    omni_dial_render(&b, g_fb, &ma);
    omni_dial_render(&b, g_fb, &mb);
    TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
    /* the frame a holds outside the box differs from b's only inside the marker boxes */
    render_full(&a, g_fb);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const bool in_box = x >= box.x0 && x < box.x1 && y >= box.y0 && y < box.y1;
            const bool in_m = (x >= ma.x0 && x < ma.x1 && y >= ma.y0 && y < ma.y1) ||
                              (x >= mb.x0 && x < mb.x1 && y >= mb.y0 && y < mb.y1);
            if (!in_box && !in_m) TEST_ASSERT_EQUAL_HEX16(g_fa[y * W + x], g_fb[y * W + x]);
        }
}

/* Task 10: the iris layer is the dial composed whole (flash included), then omni_iris_draw over it, then the fade: the
 * dial left undrawn under the opaque blades (omni_iris_opaque) changes no pixel, and nothing reads what the buffer held
 * before (two different fills, the same frame). */
static void test_iris_layer(void)
{
    static uint16_t ref[W * W];
    static const float AP[4] = { 0, 30, 103.5f, 204 };
    for (int k = 0; k < 4; k++) {
        omni_dial_frame_t f = iris_frame(busy_frame(), AP[k], 0.3f + k);
        f.st.fade = k == 1 ? 0.4f : 0;
        omni_dial_frame_t plain = f;
        plain.st.iris = false;
        plain.st.fade = 0;
        render_full(&plain, ref);
        omni_iris_draw(ref, W, NULL, f.st.iris_ap, f.st.iris_phi, f.st.iris_s, g_res.iris_grain);
        if (f.st.fade > 0) {
            const int f8 = (int)floorf(f.st.fade * 255 + 0.5f);
            for (int i = 0; i < W * W; i++) ref[i] = omni_blend565(ref[i], 0, f8);
        }
        loupe_for(f.st.loupe);
        const omni_rect_t all = { 0, 0, W, W };
        for (int i = 0; i < W * W; i++) g_fa[i] = 0xf81f, g_fb[i] = 0x07e0;
        omni_dial_render(&f, g_fa, &all);
        omni_dial_render(&f, g_fb, &all);
        TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
        TEST_ASSERT_EQUAL_MEMORY(ref, g_fa, sizeof g_fa);
    }
}

static bool covered(const omni_rect_t *r, int n, int x, int y)
{
    for (int i = 0; i < n; i++)
        if (x >= r[i].x0 && x < r[i].x1 && y >= r[i].y0 && y < r[i].y1) return true;
    return false;
}

/* Every pixel that differs between the two full frames is inside a dirty rect. */
static int check_dirty(const omni_dial_frame_t *a, const omni_dial_frame_t *b, omni_rect_t out[OMNI_DIRTY_MAX])
{
    const int n = omni_dial_dirty(a, b, out);
    TEST_ASSERT_TRUE(n >= 0 && n <= OMNI_DIRTY_MAX);
    static uint16_t fa[W * W], fb[W * W];
    render_full(a, fa);
    render_full(b, fb);
    int miss = 0, diff = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (fa[y * W + x] != fb[y * W + x]) {
                diff++;
                if (!covered(out, n, x, y) && miss++ < 3) printf("dirty: (%d, %d) changed outside the rects\n", x, y);
            }
    TEST_ASSERT_EQUAL(0, miss);
    return n;
}

static omni_rect_t uni(omni_rect_t a, omni_rect_t b)
{
    return (omni_rect_t){ a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1 };
}

static bool contains(const omni_rect_t *r, int n, omni_rect_t b)
{
    for (int i = 0; i < n; i++)
        if (r[i].x0 <= b.x0 && r[i].y0 <= b.y0 && r[i].x1 >= b.x1 && r[i].y1 >= b.y1) return true;
    return false;
}

static void assert_rect(omni_rect_t want, omni_rect_t got)
{
    char m[96];
    snprintf(m, sizeof m, "want (%d %d %d %d), got (%d %d %d %d)", want.x0, want.y0, want.x1, want.y1, got.x0, got.y0,
             got.x1, got.y1);
    TEST_ASSERT_TRUE_MESSAGE(want.x0 == got.x0 && want.y0 == got.y0 && want.x1 == got.x1 && want.y1 == got.y1, m);
}

static void test_dirty_rects(void)
{
    omni_rect_t r[OMNI_DIRTY_MAX];
    const omni_dial_frame_t a = rest_frame();
    omni_dial_frame_t b = a;
    TEST_ASSERT_EQUAL(0, check_dirty(&a, &b, r));   /* equal frames */
    /* only lon: the globe box (the marker moves inside it) */
    b.st.lon_deg += 0.3f;
    TEST_ASSERT_EQUAL(1, check_dirty(&a, &b, r));
    assert_rect((omni_rect_t){ 121, 121, 345, 345 }, r[0]);
    /* only the minute hand, at 3 o'clock: old u new */
    omni_dial_frame_t c = a, d;
    c.st.min_rad = 1.5708f;
    d = c;
    d.st.min_rad += 1.0f / 720;
    TEST_ASSERT_EQUAL(1, check_dirty(&c, &d, r));
    omni_rect_t h0, h1;
    omni_hand_bbox(c.st.min_rad, false, &h0);
    omni_hand_bbox(d.st.min_rad, false, &h1);
    assert_rect(uni(h0, h1), r[0]);
    /* at 12 the hand crosses the magnifier's source: the magnifier too */
    c.st.min_rad = 0.01f;
    d = c;
    d.st.min_rad += 1.0f / 720;
    int n = check_dirty(&c, &d, r);
    omni_hand_bbox(c.st.min_rad, false, &h0);
    omni_hand_bbox(d.st.min_rad, false, &h1);
    TEST_ASSERT_TRUE(contains(r, n, uni(h0, h1)));
    TEST_ASSERT_TRUE(contains(r, n, (omni_rect_t){ OMNI_MAG_X0, OMNI_MAG_Y0, OMNI_MAG_X1, OMNI_MAG_Y1 }));
    /* the red lens: its lens and its bloom (the bloom box holds the lens) */
    c = a;
    c.st.red = 0.2f;
    d = c;
    d.st.red = 0.6f;
    TEST_ASSERT_EQUAL(1, check_dirty(&c, &d, r));
    assert_rect((omni_rect_t){ 56, 0, 196, 117 }, r[0]);
    /* a flash: the whole screen */
    d = a;
    d.st.flash = 0.1f;
    TEST_ASSERT_EQUAL(1, check_dirty(&a, &d, r));
    assert_rect((omni_rect_t){ 0, 0, W, W }, r[0]);
    /* and the rest, for coverage only: a ring turning, a V moving, the loupe opening and open, the marker, fast */
    const omni_dial_frame_t busy = busy_frame();
    omni_dial_frame_t e[8];
    for (int i = 0; i < 8; i++) e[i] = a;
    e[0].pose.val[1] += 0.25f;
    e[1].st.mark_new = 0.8f;
    e[1].st.sel_new[3] = 30;
    e[2].st.loupe = 0.3f;
    e[3].st.loupe = 1;
    e[4].st.marker_pulse = 0.7f;
    e[5].fast = true;
    e[6].pose.level[0] = 2;
    e[7].st.hour_rad += 0.2f;
    for (int i = 0; i < 8; i++) {
        n = check_dirty(&a, &e[i], r);
        TEST_ASSERT_TRUE(n > 0);
        check_dirty(&e[i], &a, r);
    }
    /* Task 10, the iris (C12): while either frame has it, exactly the disc's box (34, 34, 432, 432), plus the dirty
     * rects of anything else that changed outside it; nothing for an iris held still; the fade: the whole screen */
    {
        const omni_rect_t disc = OMNI_IRIS_BOX;
        const omni_dial_frame_t i0 = iris_frame(a, 40, 0.2f);
        omni_dial_frame_t i1 = iris_frame(a, 46, 0.25f);
        TEST_ASSERT_EQUAL(1, check_dirty(&i0, &i1, r));
        assert_rect(disc, r[0]);
        TEST_ASSERT_EQUAL(0, check_dirty(&i0, &i0, r));
        TEST_ASSERT_EQUAL(1, check_dirty(&a, &i0, r));   /* it comes */
        assert_rect(disc, r[0]);
        TEST_ASSERT_EQUAL(1, check_dirty(&i1, &a, r));   /* and goes */
        assert_rect(disc, r[0]);
        i1.st.lon_deg += 3;   /* the globe spinning up under it: inside the box */
        TEST_ASSERT_EQUAL(1, check_dirty(&i0, &i1, r));
        assert_rect(disc, r[0]);
        i1.st.red = 0.6f;     /* a lens lighting: its box too, outside */
        n = check_dirty(&i0, &i1, r);
        TEST_ASSERT_EQUAL(2, n);
        TEST_ASSERT_TRUE(contains(r, n, disc));
        TEST_ASSERT_TRUE(contains(r, n, (omni_rect_t){ 56, 0, 196, 117 }));
        omni_dial_frame_t i2 = iris_frame(a, 0, 0);
        i2.st.fade = 0.5f;    /* the reveal's first 300 ms: the fade covers the whole screen */
        TEST_ASSERT_EQUAL(1, check_dirty(&i2, &i0, r));
        assert_rect((omni_rect_t){ 0, 0, W, W }, r[0]);
    }
    omni_dial_frame_t lo = e[3], lo2 = e[3];   /* the loupe open: a ring turning shows in it */
    lo2.pose.val[3] += 0.4f;
    lo2.st.min_rad = 0.02f;
    check_dirty(&lo, &lo2, r);
    n = check_dirty(&a, &busy, r);
    TEST_ASSERT_TRUE(n >= 1);
    check_dirty(&busy, &a, r);
}

/* Review M2: a loupe map built for another st.loupe is not drawn (the frame and shadow are, from st.loupe); a fresh
 * one is. */
static void test_loupe_stale_map(void)
{
    omni_dial_frame_t f = rest_frame();
    f.st.loupe = 0.6f;
    loupe_for(0.6f);
    TEST_ASSERT_TRUE(omni_dial_loupe_ready(&g_res, 0.6f));
    TEST_ASSERT_TRUE(omni_dial_loupe_ready(&g_res, 0));
    TEST_ASSERT_FALSE(omni_dial_loupe_ready(&g_res, 0.7f));
    const omni_rect_t all = { 0, 0, W, W };
    f.st.loupe = 0.7f;   /* the map is still 0.6's */
    omni_dial_render(&f, g_fa, &all);
    omni_pmap_t *keep = g_res.loupe_map;
    g_res.loupe_map = NULL;   /* no map at all: the same frame */
    TEST_ASSERT_FALSE(omni_dial_loupe_ready(&g_res, 0.7f));
    omni_dial_render(&f, g_fb, &all);
    g_res.loupe_map = keep;
    TEST_ASSERT_EQUAL_MEMORY(g_fa, g_fb, sizeof g_fa);
    loupe_for(0.7f);   /* rebuilt: the content differs from the frame-only render */
    omni_dial_render(&f, g_fb, &all);
    TEST_ASSERT_TRUE(memcmp(g_fa, g_fb, sizeof g_fa) != 0);
}

/* Not a gate (Task 6's extra measurement): the full dial at the reference state, written as raw RGB565 for
 * tools/omni_host_shot.mjs to compare with design/shots/omni-ref-dial-rest.png. */
static void test_host_shot_dial_rest(void)
{
    omni_dial_frame_t f = rest_frame();
    printf("dial rest: vals %.2f %.2f %.2f %.2f, hands %.4f %.4f rad, lon %.3f, green %.2f, red %.2f\n", f.st.val[0],
           f.st.val[1], f.st.val[2], f.st.val[3], f.st.hour_rad, f.st.min_rad, f.st.lon_deg, f.st.green, f.st.red);
    omni_dial_res_t keep = g_res;
    g_res.ckernel = omni_globe_cspan_lin_ref;   /* a frame at rest: linear texture sampling (R76) */
    render_full(&f, g_fa);
    g_res = keep;
    const char *out = getenv("OMNI_SHOT_OUT");
    if (!out) return;
    FILE *fp = fopen(out, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_size_t(1, fwrite(g_fa, sizeof g_fa, 1, fp));
    fclose(fp);
    printf("dial rest: wrote %s (466 x 466 RGB565 LE)\n", out);
}

/* Not a gate (Task 10's shot check): the iris entry 950 ms after its wake at the reference state (half open), written
 * as raw RGB565 for tools/omni_host_shot.mjs against design/shots/omni-ref-iris-half.png. */
static void test_host_shot_iris_half(void)
{
    omni_model_t m;
    omni_model_init(&m, 39.96, -83.0);
    omni_model_wake(&m, 0, &OMNI_ENTRY_IRIS, false);
    struct tm tm = { .tm_year = 126, .tm_mon = 9, .tm_mday = 22, .tm_hour = 10, .tm_min = 10, .tm_sec = 40 };
    omni_dial_frame_t f = { .res = &g_res };
    omni_model_step(&m, 950, true, &tm, OMNI_LENS_OFF, &f.st);
    for (int i = 0; i < OMNI_RINGS; i++) f.pose.val[i] = f.st.val[i];
    omni_rings_fidelity(&f.pose, f.st.speed);
    TEST_ASSERT_TRUE(f.st.iris);
    render_full(&f, g_fa);
    const char *out = getenv("OMNI_SHOT_IRIS_OUT");
    if (!out) return;
    FILE *fp = fopen(out, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_size_t(1, fwrite(g_fa, sizeof g_fa, 1, fp));
    fclose(fp);
    printf("iris half: wrote %s (466 x 466 RGB565 LE)\n", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_flash_table);
    RUN_TEST(test_magnifier_map);
    RUN_TEST(test_dial_order);
    RUN_TEST(test_layer_odd_origin);
    RUN_TEST(test_real_res_loads);
    RUN_TEST(test_dial_rect_equals_full);
    RUN_TEST(test_prepared_equals_render);
    RUN_TEST(test_under_over_drift);
    RUN_TEST(test_dirty_rects);
    RUN_TEST(test_iris_layer);
    RUN_TEST(test_loupe_stale_map);
    RUN_TEST(test_host_shot_dial_rest);
    RUN_TEST(test_host_shot_iris_half);
    free(g_loupe_mem);
    for (int i = 0; i < g_nalloc; i++) free(g_alloc[i]);
    return UNITY_END();
}
