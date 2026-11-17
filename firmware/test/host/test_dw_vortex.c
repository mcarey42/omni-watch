/* The Vortex tunnel (dw/vortex.h; dw wave 2 Task 13, WD37, WD38): the DWL1 table's header and padding rule, the mask, the
 * q 1 and q 2 blocks, band exactness, the per-pixel rule against an independent reference, the palette-indexed form,
 * and the rot and rush wraps. The header cases use blobs built here; the rest read the committed generated assets
 * (tools/dw_vortex.py; DW_ASSETS_DIR). DW_VORTEX_BENCH=1 also prints the host's time per frame (not a gate). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "dw_assets_host.h"
#include "dw/vortex.h"

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define N DW_V12_N
#define LUT_ODD (8u + (unsigned)N * N * 3u)   /* 162 867 + 8 = 162 875: odd */

static omni_rect_t rect(int x0, int y0, int x1, int y1)
{
    return (omni_rect_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}

/* ---- blobs for the header cases: a fake fk_asset_fn over two buffers */
static uint16_t s_lut_mem[(DW_V12_LUT_BYTES + 16) / 2];
static size_t s_lut_size;
static uint16_t s_tex_mem[6 + 256 * 256];
static size_t s_tex_size;

static const void *fake_get(const char *name, size_t *size)
{
    if (strcmp(name, "dw_v12_lut.dat") == 0 && s_lut_size) {
        *size = s_lut_size;
        return s_lut_mem;
    }
    if ((strcmp(name, "dw_v12_tx0.bin") == 0 || strcmp(name, "dw_v12_tx1.bin") == 0 ||
         strcmp(name, "dw_v12_tx2.bin") == 0) && s_tex_size) {
        *size = s_tex_size;
        return s_tex_mem;
    }
    *size = 0;
    return NULL;
}

static void make_lut(const char tag[4], int w, int h, size_t size, uint8_t pad)
{
    uint8_t *p = (uint8_t *)s_lut_mem;
    memset(p, 0, sizeof s_lut_mem);
    memcpy(p, tag, 4);
    p[4] = (uint8_t)w;
    p[5] = (uint8_t)(w >> 8);
    p[6] = (uint8_t)h;
    p[7] = (uint8_t)(h >> 8);
    for (size_t i = 8; i < LUT_ODD && i < size; i++) p[i] = (uint8_t)(i * 13u);
    if (size > LUT_ODD) p[LUT_ODD] = pad;
    s_lut_size = size;
}

static void make_tex(uint8_t cf, int w, int h)
{
    uint8_t *p = (uint8_t *)s_tex_mem;
    memset(p, 0, sizeof s_tex_mem);
    p[0] = 0x19;
    p[1] = cf;
    p[4] = (uint8_t)w;
    p[5] = (uint8_t)(w >> 8);
    p[6] = (uint8_t)h;
    p[7] = (uint8_t)(h >> 8);
    p[8] = (uint8_t)(w * 2);
    p[9] = (uint8_t)((w * 2) >> 8);
    for (int i = 0; i < 256 * 256; i++) s_tex_mem[6 + i] = (uint16_t)(i * 2654435761u >> 16);
    s_tex_size = 12 + (size_t)w * h * 2;
}

static void test_vortex_lut_header(void)
{
    dw_vortex_t v;
    make_tex(0x12, 256, 256);
    make_lut("DWL1", N, N, DW_V12_LUT_BYTES, 0);
    TEST_ASSERT_TRUE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a DWL1 table with its 0 pad byte binds");
    TEST_ASSERT_EQUAL_PTR((const uint8_t *)s_lut_mem + 8, v.lut);
    TEST_ASSERT_EQUAL_PTR(s_tex_mem + 6, v.tex);
    TEST_ASSERT_TRUE_MESSAGE(dw_vortex_bind(&v, fake_get, 1), "palette 1 (Task 14)");
    TEST_ASSERT_TRUE_MESSAGE(dw_vortex_bind(&v, fake_get, 2), "palette 2 (Task 14)");
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, DW_V12_PALETTES), "no palette 3");
    TEST_ASSERT_NULL(v.lut);
    make_lut("DWL1", N, N, LUT_ODD, 0);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "8 + w h 3 bytes with no pad byte (odd) is refused");
    make_lut("DWL1", N, N, DW_V12_LUT_BYTES, 7);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a pad byte other than 0 is refused");
    make_lut("DWL1", N, N, DW_V12_LUT_BYTES + 2, 0);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a longer blob is refused");
    make_lut("DWL1", N, N, 6, 0);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a short blob (6 B) is refused");
    make_lut("DWL1", N, N, DW_V12_LUT_BYTES - 2, 0);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a short blob (one cell short) is refused");
    make_lut("DWL2", N, N, DW_V12_LUT_BYTES, 0);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a mis-tagged blob is refused");
    make_lut("DWL1", N - 1, N, DW_V12_LUT_BYTES, 0);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a 232 x 233 header is refused");
    make_lut("DWL1", N, N, DW_V12_LUT_BYTES, 0);
    s_lut_size = 0;
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "no table: refused");
    make_lut("DWL1", N, N, DW_V12_LUT_BYTES, 0);
    make_tex(0x14, 256, 256);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "an RGB565A8 texture is refused");
    make_tex(0x12, 256, 255);
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "a 256 x 255 texture is refused");
    s_tex_size = 0;
    TEST_ASSERT_FALSE_MESSAGE(dw_vortex_bind(&v, fake_get, 0), "no texture: refused");
    TEST_ASSERT_NULL(v.lut);
    TEST_ASSERT_NULL(v.tex);

    /* the generated file: an even length, and it binds */
    size_t n = 1;
    TEST_ASSERT_NOT_NULL(dw_assets_host_get("dw_v12_lut.dat", &n));
    TEST_ASSERT_EQUAL_UINT32(DW_V12_LUT_BYTES, (uint32_t)n);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)(n % 2));
    TEST_ASSERT_TRUE(dw_vortex_bind(&v, dw_assets_host_get, 0));
}

static dw_vortex_t real_pal(uint8_t pal)
{
    dw_vortex_t v;
    TEST_ASSERT_TRUE_MESSAGE(dw_vortex_bind(&v, dw_assets_host_get, pal), "the committed dw_v12_lut.dat and _tx bind");
    return v;
}

static dw_vortex_t real(void) { return real_pal(0); }

/* r^2 of cell (i, j)'s centre from the screen's centre, in screen px: x = (i + 0.5) 2 - 233 (:719) */
static int cell_r2(int i, int j)
{
    const int x = 2 * i + 1 - 233, y = 2 * j + 1 - 233;
    return x * x + y * y;
}

static void test_vortex_mask(void)
{
    const dw_vortex_t v = real();
    int inner = 0, outer = 0, full = 0, lit = 0;
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            const uint8_t m = v.lut[(j * N + i) * 3 + 2];
            const int r2 = cell_r2(i, j);
            if (r2 < 104 * 104) {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, m, "r < 104 is black (the calm centre)");
                inner++;
            } else if (r2 > 236 * 236) {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, m, "r > 236 is black");
                outer++;
            } else if (r2 >= 150 * 150 && r2 <= 200 * 200) {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(255, m, "full from r 150 to 200 (256 clamped to 255)");
                full++;
            }
            if (m) lit++;
        }
    TEST_ASSERT_TRUE(inner > 8000 && outer > 1000 && full > 10000);
    TEST_ASSERT_TRUE_MESSAGE(lit > 30000, "the ring is lit");
    /* dimmer past 200: a cell at r about 230 is lit but below full */
    const uint8_t m230 = v.lut[(116 * N + 116 + 115) * 3 + 2];   /* x = 463 - 233 = 230, y = 0 */
    TEST_ASSERT_TRUE(m230 > 100 && m230 < 255);
}

/* the rule, written again here: cell (i, j) = (x / 2, y / 2) at q 1, (2 (x / 4), 2 (y / 4)) at q 2; each channel x (m +
 * 1) >> 8 for m > 0 (Task 14's ruling: a full mask, 255, is the texel itself), m 0 black */
static uint16_t ref_px(const dw_vortex_t *v, const uint16_t *tex, int x, int y, int q, uint8_t rot, uint8_t rush)
{
    const int i = q == 2 ? (x / 4) * 2 : x / 2, j = q == 2 ? (y / 4) * 2 : y / 2;
    const uint8_t *c = v->lut + (j * N + i) * 3;
    if (!c[2]) return 0;
    const uint16_t t = tex[((c[1] + rush) & 255) * 256 + ((c[0] + rot) & 255)];
    const unsigned m = c[2] + 1u;
    const unsigned r = (t >> 11) * m >> 8, g = ((t >> 5) & 63u) * m >> 8, b = (t & 31u) * m >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

static uint16_t s_a[W * W], s_b[W * W];

static void whole(const dw_vortex_t *v, uint8_t rot, uint8_t rush, uint8_t q, uint16_t *fb, uint16_t canary)
{
    for (int i = 0; i < W * W; i++) fb[i] = canary;
    dw_vortex_draw(v, rot, rush, q, rect(0, 0, W, W), fb, W);
}

static void test_vortex_rule(void)
{
    const dw_vortex_t v = real();
    static const uint8_t rr[4][2] = {{0, 0}, {37, 201}, {255, 255}, {128, 1}};
    for (int q = 1; q <= 2; q++)
        for (int k = 0; k < 4; k++) {
            whole(&v, rr[k][0], rr[k][1], (uint8_t)q, s_a, 0x5A5A);
            whole(&v, rr[k][0], rr[k][1], (uint8_t)q, s_b, 0xA5A5);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_a, s_b, sizeof s_a, "every pixel written");
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++)
                    if (s_a[y * W + x] != ref_px(&v, v.tex, x, y, q, rr[k][0], rr[k][1])) {
                        char msg[96];
                        snprintf(msg, sizeof msg, "q %d rot %u rush %u: pixel (%d, %d)", q, rr[k][0], rr[k][1], x, y);
                        TEST_FAIL_MESSAGE(msg);
                    }
        }
    /* Task 14's mask rule: a full mask (255) is the texel exactly, so the brightest 565 level survives */
    {
        int full = 0;
        whole(&v, 0, 0, 1, s_a, 0);
        for (int y = 0; y < W; y += 2)
            for (int x = 0; x < W; x += 2) {
                const uint8_t *c = v.lut + ((y / 2) * N + x / 2) * 3;
                if (c[2] != 255) continue;
                TEST_ASSERT_EQUAL_HEX16(v.tex[c[1] * 256 + c[0]], s_a[y * W + x]);
                full++;
            }
        TEST_ASSERT_TRUE(full > 10000);
    }
    /* the motion moves the ring and never the centre */
    whole(&v, 0, 0, 1, s_a, 0);
    whole(&v, 1, 0, 1, s_b, 0);
    TEST_ASSERT_TRUE(memcmp(s_a, s_b, sizeof s_a) != 0);
    TEST_ASSERT_EQUAL_UINT16(0, s_a[233 * W + 233]);
    TEST_ASSERT_EQUAL_UINT16(0, s_a[0]);
}

static void test_vortex_q_blocks(void)
{
    const dw_vortex_t v = real();
    for (int q = 1; q <= 2; q++) {
        const int bs = q == 2 ? 4 : 2;
        whole(&v, 77, 190, (uint8_t)q, s_a, 0x5A5A);
        int distinct = 0;
        for (int by = 0; by < W; by += bs)
            for (int bx = 0; bx < W; bx += bs) {
                const uint16_t p = s_a[by * W + bx];
                for (int y = by; y < by + bs && y < W; y++)
                    for (int x = bx; x < bx + bs && x < W; x++)
                        if (s_a[y * W + x] != p) {
                            char msg[96];
                            snprintf(msg, sizeof msg, "q %d: block (%d, %d) not uniform at (%d, %d)", q, bx, by, x, y);
                            TEST_FAIL_MESSAGE(msg);
                        }
                if (bx >= bs && p != s_a[by * W + bx - bs]) distinct++;
            }
        TEST_ASSERT_TRUE_MESSAGE(distinct > 1000, "neighbouring blocks differ (a textured ring)");
    }
}

static void test_vortex_band_exact(void)
{
    const dw_vortex_t v = real();
    for (int q = 1; q <= 2; q++) {
        whole(&v, 200, 13, (uint8_t)q, s_a, 0);
        for (int i = 0; i < W * W; i++) s_b[i] = 0x1234;
        for (int y0 = 0; y0 < W; y0 += 8)   /* the direct path's 8-row bands */
            dw_vortex_draw(&v, 200, 13, (uint8_t)q, rect(0, y0, W, y0 + 8 < W ? y0 + 8 : W), s_b + y0 * W, W);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_a, s_b, sizeof s_a, "8-row clips equal the whole frame");
        for (int i = 0; i < W * W; i++) s_b[i] = 0x4321;
        for (int y0 = 0; y0 < W; y0 += 7)   /* odd tiles: every block split somewhere */
            for (int x0 = 0; x0 < W; x0 += 13) {
                const omni_rect_t c = rect(x0, y0, x0 + 13 < W ? x0 + 13 : W, y0 + 7 < W ? y0 + 7 : W);
                dw_vortex_draw(&v, 200, 13, (uint8_t)q, c, s_b + y0 * W + x0, W);
            }
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_a, s_b, sizeof s_a, "7 x 13 tiles equal the whole frame");
        /* a clip writes nothing outside itself (a band buffer of its own rows only) */
        static uint16_t band[8 * W + 64];
        for (size_t i = 0; i < sizeof band / 2; i++) band[i] = 0xBEEF;
        dw_vortex_draw(&v, 200, 13, (uint8_t)q, rect(3, 101, 401, 109), band, W);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < W; x++) {
                const bool in = x < 398;
                if (in) TEST_ASSERT_EQUAL_UINT16(s_a[(101 + y) * W + 3 + x], band[y * W + x]);
                else TEST_ASSERT_EQUAL_UINT16(0xBEEF, band[y * W + x]);
            }
        for (int i = 8 * W; i < 8 * W + 64; i++) TEST_ASSERT_EQUAL_UINT16(0xBEEF, band[i]);
    }
}

/* the palette-indexed form equals the RGB565 one over the texture pal[idx] */
static void test_vortex_idx_equals_rgb(void)
{
    const dw_vortex_t v = real();
    static uint8_t idx[256 * 256];
    static uint16_t pal[256], tex[256 * 256];
    for (int i = 0; i < 256; i++) pal[i] = (uint16_t)(i * 40503u + 7u);
    for (int i = 0; i < 256 * 256; i++) {
        idx[i] = (uint8_t)(v.tex[i] ^ (v.tex[i] >> 8));
        tex[i] = pal[idx[i]];
    }
    const dw_vortex_t vt = {v.lut, tex};
    for (int q = 1; q <= 2; q++) {
        whole(&vt, 9, 250, (uint8_t)q, s_a, 0);
        for (int i = 0; i < W * W; i++) s_b[i] = 0x7777;
        for (int y0 = 0; y0 < W; y0 += 8)
            dw_vortex_draw_idx(&v, idx, pal, 9, 250, (uint8_t)q, rect(0, y0, W, y0 + 8 < W ? y0 + 8 : W),
                               s_b + y0 * W, W);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_a, s_b, sizeof s_a, "pal[idx] draws as that RGB565 texture");
    }
}

static void test_vortex_rot_rush_wrap(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, dw_vortex_rot(0));
    TEST_ASSERT_EQUAL_UINT8(254, dw_vortex_rot(11590));
    TEST_ASSERT_EQUAL_UINT8(255, dw_vortex_rot(11591));
    TEST_ASSERT_EQUAL_UINT8(255, dw_vortex_rot(11636));
    TEST_ASSERT_EQUAL_UINT8(0, dw_vortex_rot(11637));
    TEST_ASSERT_EQUAL_UINT8(0, dw_vortex_rush(0));
    TEST_ASSERT_EQUAL_UINT8(254, dw_vortex_rush(3642));
    TEST_ASSERT_EQUAL_UINT8(255, dw_vortex_rush(3643));
    TEST_ASSERT_EQUAL_UINT8(255, dw_vortex_rush(3657));
    TEST_ASSERT_EQUAL_UINT8(0, dw_vortex_rush(3658));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(192, dw_vortex_rot(UINT32_MAX), "no overflow in ms x 22");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(30, dw_vortex_rush(UINT32_MAX), "no overflow in ms x 70");
}


/* M3 (the Task 13 review): the face feeds dirty rects, so a clip is cut to the screen first: one reaching past any edge
 * writes only its on-screen part, at the right place, and that part equals the whole frame's */
static void test_vortex_clip_clamped(void)
{
    const dw_vortex_t v = real();
    whole(&v, 33, 66, 1, s_a, 0);
    static uint16_t big[(W + 40) * (W + 40)];
    const int S = W + 40;   /* a buffer with 20 px of margin all round: fb is the pixel at (-20, -20) */
    static const int cl[4][4] = {{-20, -20, W + 20, W + 20}, {-7, 100, 50, 120}, {440, -3, W + 9, 30}, {200, 460, 300, W + 20}};
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < S * S; i++) big[i] = 0xBEEF;
        const omni_rect_t c = rect(cl[k][0], cl[k][1], cl[k][2], cl[k][3]);
        dw_vortex_draw(&v, 33, 66, 1, c, big + (size_t)(c.y0 + 20) * S + (c.x0 + 20), S);
        for (int y = -20; y < W + 20; y++)
            for (int x = -20; x < W + 20; x++) {
                const uint16_t got = big[(size_t)(y + 20) * S + (x + 20)];
                const bool on = x >= 0 && y >= 0 && x < W && y < W && x >= c.x0 && x < c.x1 && y >= c.y0 && y < c.y1;
                if (on) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], got);
                else TEST_ASSERT_EQUAL_HEX16_MESSAGE(0xBEEF, got, "a write off the screen or outside the clip");
            }
    }
    for (int i = 0; i < S * S; i++) big[i] = 0xBEEF;   /* a clip wholly off the screen writes nothing */
    dw_vortex_draw(&v, 33, 66, 1, rect(W + 2, 10, W + 18, 20), big, S);
    for (int i = 0; i < S * S; i++) TEST_ASSERT_EQUAL_HEX16(0xBEEF, big[i]);
}

/* the three palettes bind over the same table, each its own texture */
static void test_vortex_palettes(void)
{
    const dw_vortex_t a = real_pal(0), b = real_pal(1), c = real_pal(2);
    TEST_ASSERT_EQUAL_PTR(a.lut, b.lut);
    TEST_ASSERT_EQUAL_PTR(a.lut, c.lut);
    TEST_ASSERT_TRUE(memcmp(a.tex, b.tex, 256 * 256 * 2) != 0);
    TEST_ASSERT_TRUE(memcmp(b.tex, c.tex, 256 * 256 * 2) != 0);
    TEST_ASSERT_TRUE(memcmp(a.tex, c.tex, 256 * 256 * 2) != 0);
}

/* R150 (the controller's ruling): ship one 8-bit 256 x 256 index and three 256-entry RGB565 palettes only if that is
 * lossless, the indexed render byte-equal to the generator's RGB565 texture for each palette. An 8-bit index names at
 * most 256 colours, so it is lossless only if each palette's texture uses at most 256 RGB565 values. The generated
 * textures use more (the generator's colour is a function of two noise fields, f and n3, not one ramp, and PRISM's also
 * of the column), so the textures ship as RGB565 (the ruling's fallback). This test pins that: if the generator ever
 * makes textures of at most 256 values, it fails, and the indexed form is worth its 318 KB again. It also shows the loss:
 * the best 8-bit index (the 256 most used values, the rest to their nearest) does not draw the RGB565 frame. */
static void test_vortex_palette_index_lossless(void)
{
    static uint8_t seen[65536];
    static uint32_t count[65536];
    unsigned distinct[3];
    for (int p = 0; p < 3; p++) {
        const dw_vortex_t v = real_pal((uint8_t)p);
        memset(seen, 0, sizeof seen);
        distinct[p] = 0;
        for (int i = 0; i < 256 * 256; i++)
            if (!seen[v.tex[i]]) seen[v.tex[i]] = 1, distinct[p]++;
        printf("R150: dw_v12_tx%d uses %u distinct RGB565 values (an 8-bit index holds 256)\n", p, distinct[p]);
    }
    TEST_ASSERT_TRUE_MESSAGE(distinct[0] > 256 || distinct[1] > 256 || distinct[2] > 256,
                             "every palette fits 256 colours: R150's indexed form would be lossless (revisit)");
    /* the loss, shown for palette 0: its 256 most used values as the palette, every texel to the nearest */
    const dw_vortex_t v = real();
    memset(count, 0, sizeof count);
    for (int i = 0; i < 256 * 256; i++) count[v.tex[i]]++;
    static uint16_t pal[256];
    for (int k = 0; k < 256; k++) {
        uint32_t best = 0;
        int bi = 0;
        for (int c = 0; c < 65536; c++)
            if (count[c] > best) best = count[c], bi = c;
        pal[k] = (uint16_t)bi;
        count[bi] = 0;
    }
    static uint8_t idx[256 * 256];
    for (int i = 0; i < 256 * 256; i++) {
        const uint16_t t = v.tex[i];
        int bk = 0;
        long bd = 1L << 30;
        for (int k = 0; k < 256; k++) {
            const long dr = (long)(t >> 11) - (pal[k] >> 11), dg = (long)((t >> 5) & 63) - ((pal[k] >> 5) & 63),
                       db = (long)(t & 31) - (pal[k] & 31), d = dr * dr + dg * dg + db * db;
            if (d < bd) bd = d, bk = k;
        }
        idx[i] = (uint8_t)bk;
    }
    whole(&v, 0, 0, 1, s_a, 0);
    for (int i = 0; i < W * W; i++) s_b[i] = 0;
    dw_vortex_draw_idx(&v, idx, pal, 0, 0, 1, rect(0, 0, W, W), s_b, W);
    long diff = 0;
    for (int i = 0; i < W * W; i++) diff += s_a[i] != s_b[i];
    printf("R150: palette 0 through its best 8-bit index differs from the RGB565 frame in %ld px\n", diff);
    TEST_ASSERT_TRUE_MESSAGE(diff > 0, "an 8-bit index of palette 0 drew the RGB565 frame exactly");
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

/* Not a gate (it asserts nothing): DW_VORTEX_BENCH=1. The host's time per whole frame in 8-row bands. */
static void test_vortex_frame_time(void)
{
    const dw_vortex_t v = real();
    static uint8_t idx[256 * 256];
    static uint16_t pal[256];
    for (int i = 0; i < 256; i++) pal[i] = (uint16_t)(i * 257u);
    for (int i = 0; i < 256 * 256; i++) idx[i] = (uint8_t)(v.tex[i] >> 8);
    const int F = 200;
    for (int form = 0; form < 2; form++)
        for (int q = 1; q <= 2; q++) {
            const double t0 = now_ms();
            for (int f = 0; f < F; f++)
                for (int y0 = 0; y0 < W; y0 += 8) {
                    const omni_rect_t c = rect(0, y0, W, y0 + 8 < W ? y0 + 8 : W);
                    if (form) dw_vortex_draw_idx(&v, idx, pal, (uint8_t)f, (uint8_t)(3 * f), (uint8_t)q, c, s_a + y0 * W, W);
                    else dw_vortex_draw(&v, (uint8_t)f, (uint8_t)(3 * f), (uint8_t)q, c, s_a + y0 * W, W);
                }
            printf("host dw_vortex_draw%s q %d: %.3f ms a frame (466 x 466 in 8-row bands, %d frames)\n",
                   form ? "_idx" : "", q, (now_ms() - t0) / F, F);
        }
}

int main(void)
{
    if (!dw_assets_host_open(DW_ASSETS_DIR, NULL)) {
        printf("no assets at %s\n", DW_ASSETS_DIR);
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_vortex_lut_header);
    RUN_TEST(test_vortex_mask);
    RUN_TEST(test_vortex_rule);
    RUN_TEST(test_vortex_q_blocks);
    RUN_TEST(test_vortex_band_exact);
    RUN_TEST(test_vortex_idx_equals_rgb);
    RUN_TEST(test_vortex_rot_rush_wrap);
    RUN_TEST(test_vortex_clip_clamped);
    RUN_TEST(test_vortex_palettes);
    RUN_TEST(test_vortex_palette_index_lossless);
    if (getenv("DW_VORTEX_BENCH")) RUN_TEST(test_vortex_frame_time);
    const int r = UNITY_END();
    dw_assets_host_close();
    return r;
}
