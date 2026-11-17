/* face_kit's compositor and polar annulus (fk/compose.h, fk/annulus.h; dw wave 1 Task 6, WD9, C12). Every image here is synthetic
 * (R132): hand-made LVGL bins, a pattern atlas and ORS1 strips built in the test, never shipped or private art. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "fk/annulus.h"
#include "fk/compose.h"
#include "omni/blend565.h"
#include "omni/ring.h"

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define CANARY 0xA5C3u
#define PI_D 3.14159265358979323846

static uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
static omni_rect_t rect(int x0, int y0, int x1, int y1) { return (omni_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 }; }
static uint16_t *at(uint16_t *fb, int stride, omni_rect_t c) { return fb + c.y0 * stride + c.x0; }   /* fb: screen (0, 0) */
static void fill(uint16_t *fb, int n, uint16_t v) { for (int i = 0; i < n; i++) fb[i] = v; }

/* An LVGL v9 bin in a uint16-aligned buffer: header, then the planes as tools/assets.sh packs them. */
static size_t make_bin(uint16_t *buf, uint8_t magic, uint8_t cf, uint16_t flags, int w, int h, int stride,
                       const uint16_t *rgb, const uint8_t *a)
{
    uint8_t *p = (uint8_t *)buf;
    p[0] = magic;
    p[1] = cf;
    p[2] = (uint8_t)flags;
    p[3] = (uint8_t)(flags >> 8);
    p[4] = (uint8_t)w;
    p[5] = (uint8_t)(w >> 8);
    p[6] = (uint8_t)h;
    p[7] = (uint8_t)(h >> 8);
    p[8] = (uint8_t)stride;
    p[9] = (uint8_t)(stride >> 8);
    p[10] = p[11] = 0;
    size_t o = 12;
    if (rgb) {
        memcpy(p + o, rgb, (size_t)w * h * 2);
        o += (size_t)w * h * 2;
    }
    if (a) {
        memcpy(p + o, a, (size_t)w * h);
        o += (size_t)w * h;
    }
    return o;
}

static void test_img_from_bin(void)
{
    static uint16_t buf[64];
    const uint16_t rgb[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const uint8_t a[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    const size_t n = make_bin(buf, 0x19, FK_CF_RGB565A8, 0, 4, 2, 8, rgb, a);
    TEST_ASSERT_EQUAL(12 + 16 + 8, n);
    fk_img_t img;
    TEST_ASSERT_TRUE(fk_img_from_bin(&img, buf, n));
    TEST_ASSERT_EQUAL(4, img.w);
    TEST_ASSERT_EQUAL(2, img.h);
    TEST_ASSERT_EQUAL(FK_CF_RGB565A8, img.cf);
    TEST_ASSERT_EQUAL_PTR((const uint8_t *)buf + 12, img.rgb);        /* the planes at 12 and 12 + 16 */
    TEST_ASSERT_EQUAL_PTR((const uint8_t *)buf + 12 + 16, img.a);
    TEST_ASSERT_EQUAL(5, img.rgb[4]);
    TEST_ASSERT_EQUAL(80, img.a[7]);

    const fk_img_t keep = img;
    buf[0] = (uint16_t)((buf[0] & 0xff00) | 0x18);                    /* bad magic */
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, n));
    TEST_ASSERT_EQUAL_MEMORY(&keep, &img, sizeof img);
    make_bin(buf, 0x19, 0x13, 0, 4, 2, 8, rgb, a);                    /* a bad cf */
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, n));
    make_bin(buf, 0x19, FK_CF_RGB565A8, 0, 4, 2, 8, rgb, a);
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, n - 1));             /* short */
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, 11));
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, (const uint8_t *)buf + 1, n));   /* odd address */
    make_bin(buf, 0x19, FK_CF_RGB565A8, 0, 4, 2, 10, rgb, a);         /* stride other than w x 2 */
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, n + 8));
    make_bin(buf, 0x19, FK_CF_RGB565A8, 0x0008, 4, 2, 8, rgb, a);     /* compressed */
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, n));
    make_bin(buf, 0x19, FK_CF_RGB565A8, 0x0001, 4, 2, 8, rgb, a);     /* premultiplied */
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, n));
    make_bin(buf, 0x19, FK_CF_RGB565A8, 0, 0, 2, 0, rgb, a);          /* empty */
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, n));

    TEST_ASSERT_TRUE(fk_img_from_bin(&img, buf, make_bin(buf, 0x19, FK_CF_RGB565, 0, 4, 2, 8, rgb, NULL)));
    TEST_ASSERT_EQUAL_PTR(NULL, img.a);
    TEST_ASSERT_EQUAL(FK_CF_RGB565, img.cf);
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, 12 + 15));
    TEST_ASSERT_TRUE(fk_img_from_bin(&img, buf, make_bin(buf, 0x19, FK_CF_A8, 0, 4, 2, 4, NULL, a)));
    TEST_ASSERT_EQUAL_PTR(NULL, img.rgb);
    TEST_ASSERT_EQUAL_PTR((const uint8_t *)buf + 12, img.a);
    TEST_ASSERT_FALSE(fk_img_from_bin(&img, buf, 12 + 7));
}

/* An 8 x 6 opaque image, each pixel distinct. */
static uint16_t s_irgb[8 * 6];
static uint8_t s_ia[8 * 6];
static fk_img_t img8x6(bool alpha)
{
    for (int i = 0; i < 8 * 6; i++) {
        s_irgb[i] = rgb565(40 + i * 4, 255 - i * 3, (i * 37) & 255);
        s_ia[i] = (uint8_t)(i * 23 % 256);
    }
    s_ia[0] = 255;
    s_ia[1] = 0;
    return (fk_img_t){ 8, 6, s_irgb, alpha ? s_ia : NULL, alpha ? FK_CF_RGB565A8 : FK_CF_RGB565 };
}

static void test_blit_clip(void)
{
    static uint16_t fb[48 * 32];
    const fk_img_t img = img8x6(false);
    const omni_rect_t clips[3] = { rect(0, 4, 40, 7), rect(2, 3, 4, 9), rect(0, 0, 48, 32) };
    for (int k = 0; k < 3; k++) {
        const omni_rect_t c = clips[k];
        fill(fb, 48 * 32, CANARY);
        fk_blit(at(fb, 48, c), 48, c, &img, -3, 3, 255);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 48; x++) {
                const bool in_clip = x >= c.x0 && x < c.x1 && y >= c.y0 && y < c.y1;
                const bool in_img = x >= -3 && x < 5 && y >= 3 && y < 9;
                const uint16_t want = in_clip && in_img ? s_irgb[(y - 3) * 8 + (x + 3)] : CANARY;
                if (fb[y * 48 + x] != want) {
                    printf("clip %d pixel (%d, %d): %04x, want %04x\n", k, x, y, fb[y * 48 + x], want);
                    TEST_FAIL();
                }
            }
    }
    /* an image wholly outside, and an empty clip: nothing */
    fill(fb, 48 * 32, CANARY);
    fk_blit(fb, 48, rect(0, 0, 48, 32), &img, 48, 0, 255);
    fk_blit(fb, 48, rect(0, 0, 48, 32), &img, -8, 0, 255);
    fk_blit(fb, 48, rect(10, 10, 10, 20), &img, 8, 8, 255);
    for (int i = 0; i < 48 * 32; i++) TEST_ASSERT_EQUAL_HEX16(CANARY, fb[i]);
}

static void test_blit_alpha_matches_blend(void)
{
    static uint16_t fb[16 * 12], dst[16 * 12];
    const fk_img_t img = img8x6(true);
    const uint8_t opas[4] = { 255, 200, 37, 0 };
    for (int i = 0; i < 16 * 12; i++) dst[i] = (uint16_t)(i * 2654435761u >> 16);
    for (int k = 0; k < 4; k++) {
        memcpy(fb, dst, sizeof fb);
        const omni_rect_t c = rect(0, 0, 16, 12);
        fk_blit(fb, 16, c, &img, 5, 3, opas[k]);
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 16; x++) {
                uint16_t want = dst[y * 16 + x];
                if (x >= 5 && x < 13 && y >= 3 && y < 9) {
                    const int i = (y - 3) * 8 + (x - 5);
                    want = omni_blend565(want, s_irgb[i], s_ia[i] * opas[k] / 255);
                }
                TEST_ASSERT_EQUAL_HEX16(want, fb[y * 16 + x]);
            }
    }
}

/* Task 12: the 1:1 path (zero alpha passed over) and fk_add_a8_2 (two gains in one pass) against the rule pixel by
 * pixel, on a noisy frame, clipped and offset, with alphas of every kind (0, small, 255). */
static void test_add_a8_one_to_one_and_two_gains(void)
{
    enum { AW = 37, AH = 23, FW = 64, FH = 40 };
    static uint8_t a[AW * AH];
    static uint16_t fb[FW * FH], want[FW * FH], two[FW * FH];
    uint32_t seed = 12345;
    for (int i = 0; i < AW * AH; i++) {
        seed = seed * 1103515245u + 12345u;
        const unsigned k = (seed >> 16) % 5;
        a[i] = k == 0 ? 0 : k == 1 ? 255 : k == 2 ? 1 : (uint8_t)(seed >> 8);
    }
    const fk_img_t g = { AW, AH, NULL, a, FK_CF_A8 };
    const uint16_t tint = rgb565(240, 160, 90);
    static const int pos[][2] = { { 5, 4 }, { -9, -3 }, { 40, 25 } };
    static const uint8_t gains[][2] = { { 255, 255 }, { 107, 3 }, { 1, 200 }, { 0, 90 }, { 90, 0 } };
    const omni_rect_t clips[2] = { rect(0, 0, FW, FH), rect(7, 5, 50, 31) };
    for (unsigned p = 0; p < 3; p++)
        for (unsigned q = 0; q < 5; q++)
            for (unsigned ci = 0; ci < 2; ci++) {
                const omni_rect_t c = clips[ci];
                const int cw = c.x1 - c.x0;
                for (int i = 0; i < FW * FH; i++) want[i] = fb[i] = two[i] = (uint16_t)(i * 40503u + p * 7 + q);
                /* the rule, twice, by hand */
                for (int pass = 0; pass < 2; pass++)
                    for (int y = c.y0; y < c.y1; y++)
                        for (int x = c.x0; x < c.x1; x++) {
                            const int sx = x - pos[p][0], sy = y - pos[p][1];
                            if (sx < 0 || sy < 0 || sx >= AW || sy >= AH) continue;
                            const int ap = omni_amul(a[sy * AW + sx]) * omni_amul(gains[q][pass]) >> 8;
                            uint16_t *d = &want[(y - c.y0) * cw + (x - c.x0)];
                            if (ap) {
                                const int R = omni_ex_r(*d) + ((omni_ex_r(tint) * ap) >> 8),
                                          G = omni_ex_g(*d) + ((omni_ex_g(tint) * ap) >> 8),
                                          B = omni_ex_b(*d) + ((omni_ex_b(tint) * ap) >> 8);
                                *d = omni_pack565(R < 255 ? R : 255, G < 255 ? G : 255, B < 255 ? B : 255);
                            }
                        }
                fk_add_a8(fb, cw, c, &g, pos[p][0], pos[p][1], AW, AH, tint, gains[q][0]);
                fk_add_a8(fb, cw, c, &g, pos[p][0], pos[p][1], AW, AH, tint, gains[q][1]);
                fk_add_a8_2(two, cw, c, &g, pos[p][0], pos[p][1], AW, AH, tint, gains[q][0], gains[q][1]);
                TEST_ASSERT_EQUAL_HEX16_ARRAY(want, fb, FW * FH);
                TEST_ASSERT_EQUAL_HEX16_ARRAY(want, two, FW * FH);
            }
}

static void test_add_a8_saturates(void)
{
    static uint16_t fb[32 * 16], ref[32 * 16];
    uint8_t a[4 * 2] = { 255, 128, 64, 0, 255, 255, 1, 0 };
    const fk_img_t g = { 4, 2, NULL, a, FK_CF_A8 };
    const omni_rect_t c = rect(0, 0, 32, 16);
    /* white plus glow stays white */
    fill(fb, 32 * 16, 0xFFFF);
    fk_add_a8(fb, 32, c, &g, 2, 1, 20, 10, rgb565(255, 220, 170), 255);
    for (int i = 0; i < 32 * 16; i++) TEST_ASSERT_EQUAL_HEX16(0xFFFF, fb[i]);
    /* gain 0 leaves the frame unchanged */
    for (int i = 0; i < 32 * 16; i++) ref[i] = fb[i] = (uint16_t)(i * 40503u);
    fk_add_a8(fb, 32, c, &g, 2, 1, 20, 10, 0xFFFF, 0);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(ref, fb, 32 * 16);
    /* on black: the rule, stretched 4 x 2 -> 8 x 4 at (3, 2), nearest at pixel centres (two pixels a texel) */
    fill(fb, 32 * 16, 0);
    const uint16_t tint = rgb565(255, 220, 170);
    fk_add_a8(fb, 32, c, &g, 3, 2, 8, 4, tint, 200);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++) {
            uint16_t want = 0;
            if (x >= 3 && x < 11 && y >= 2 && y < 6) {
                const int ap = omni_amul(a[((y - 2) / 2) * 4 + (x - 3) / 2]) * omni_amul(200) >> 8;
                want = omni_pack565((omni_ex_r(tint) * ap) >> 8, (omni_ex_g(tint) * ap) >> 8,
                                    (omni_ex_b(tint) * ap) >> 8);
            }
            TEST_ASSERT_EQUAL_HEX16(want, fb[y * 32 + x]);
        }
    /* non-integer stretches (4 x 2 -> 6 x 3, and down to 3 x 1), against nearest at pixel centres in double */
    const int sizes[2][2] = { { 6, 3 }, { 3, 1 } };
    for (int k = 0; k < 2; k++) {
        const int w = sizes[k][0], h = sizes[k][1];
        fill(fb, 32 * 16, 0);
        fk_add_a8(fb, 32, c, &g, 5, 4, w, h, tint, 255);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 32; x++) {
                uint16_t want = 0;
                if (x >= 5 && x < 5 + w && y >= 4 && y < 4 + h) {
                    const int sx = (int)floor((x - 5 + 0.5) * 4 / w), sy = (int)floor((y - 4 + 0.5) * 2 / h);
                    const int ap = omni_amul(a[sy * 4 + sx]) * omni_amul(255) >> 8;
                    want = omni_pack565((omni_ex_r(tint) * ap) >> 8, (omni_ex_g(tint) * ap) >> 8,
                                        (omni_ex_b(tint) * ap) >> 8);
                }
                TEST_ASSERT_EQUAL_HEX16(want, fb[y * 32 + x]);
            }
    }
    /* an RGB565A8 image's alpha plane works the same; a clip keeps the rest */
    fill(fb, 32 * 16, CANARY);
    const fk_img_t g2 = { 4, 2, s_irgb, a, FK_CF_RGB565A8 };
    fk_add_a8(at(fb, 32, rect(4, 3, 6, 4)), 32, rect(4, 3, 6, 4), &g2, 3, 2, 8, 4, tint, 255);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 32; x++)
            if (!(x >= 4 && x < 6 && y == 3)) TEST_ASSERT_EQUAL_HEX16(CANARY, fb[y * 32 + x]);
}

/* A pattern atlas: glyphs "0123456789:", cell i a solid colour COL(i), h 8, ox 0, oy -8; '1' adv 3, ':' adv 2, others
 * 5 (w = adv). Cells sit side by side in a 64 x 10 RGB565A8 image with alpha 255 inside the cells and 0 between. */
#define AT_W 64
static uint16_t s_atrgb[AT_W * 10];
static uint8_t s_ata[AT_W * 10];
static fk_cell_t s_cells[11];
static fk_img_t s_atimg;
static fk_atlas_t s_at;
static uint16_t COL(int i) { return rgb565(20 + i * 20, 250 - i * 15, 60 + i * 9); }
static void make_atlas(void)
{
    static const char G[] = "0123456789:";
    memset(s_ata, 0, sizeof s_ata);
    memset(s_atrgb, 0, sizeof s_atrgb);
    int x = 0;
    for (int i = 0; i < 11; i++) {
        const int adv = G[i] == '1' ? 3 : G[i] == ':' ? 2 : 5;
        s_cells[i] = (fk_cell_t){ (int16_t)x, 1, (int16_t)adv, 8, 0, -8, (int16_t)adv };
        for (int yy = 1; yy < 9; yy++)
            for (int xx = x; xx < x + adv; xx++) {
                s_atrgb[yy * AT_W + xx] = COL(i);
                s_ata[yy * AT_W + xx] = 255;
            }
        x += adv + 1;
    }
    TEST_ASSERT_TRUE(x <= AT_W);
    s_atimg = (fk_img_t){ AT_W, 10, s_atrgb, s_ata, FK_CF_RGB565A8 };
    s_at = (fk_atlas_t){ &s_atimg, s_cells, G, 10 };
}

static void test_atlas_fixed_cells(void)
{
    static uint16_t fb[80 * 32];
    make_atlas();
    const int cell = 9;
    TEST_ASSERT_EQUAL(5 * cell, fk_atlas_width(&s_at, "12:34", cell));
    TEST_ASSERT_EQUAL(3 + 5 + 2 + 5 + 5, fk_atlas_width(&s_at, "12:34", 0));
    TEST_ASSERT_EQUAL(3 * cell, fk_atlas_width(&s_at, "1?3", cell));      /* a missing glyph takes its cell */
    TEST_ASSERT_EQUAL(3 + 5, fk_atlas_width(&s_at, "1?3", 0));            /* proportional: no room */
    TEST_ASSERT_EQUAL(0, fk_atlas_width(&s_at, "", cell));

    const char *texts[2] = { "12:34", "1?3" };
    for (int t = 0; t < 2; t++) {
        fill(fb, 80 * 32, 0);
        const omni_rect_t c = rect(0, 0, 80, 32);
        fk_atlas_text(fb, 80, c, &s_at, texts[t], 10, 20, cell, 255);
        /* the expected frame: each glyph's advance box centred in its cell, rows baseline - 8 .. baseline - 1 */
        static uint16_t want[80 * 32];
        fill(want, 80 * 32, 0);
        const char *s = texts[t];
        for (int k = 0; s[k]; k++) {
            const char *p = strchr("0123456789:", s[k]);
            if (!p) continue;
            const int i = (int)(p - "0123456789:"), adv = s_cells[i].adv;
            const int left = 10 + k * cell + (cell - adv) / 2;
            for (int y = 12; y < 20; y++)
                for (int x = left; x < left + adv; x++) want[y * 80 + x] = COL(i);
        }
        TEST_ASSERT_EQUAL_HEX16_ARRAY(want, fb, 80 * 32);
    }
    /* proportional: glyphs abut at their advances */
    fill(fb, 80 * 32, 0);
    fk_atlas_text(fb, 80, rect(0, 0, 80, 32), &s_at, "1?3", 4, 20, 0, 255);
    TEST_ASSERT_EQUAL_HEX16(COL(1), fb[15 * 80 + 4]);
    TEST_ASSERT_EQUAL_HEX16(COL(1), fb[15 * 80 + 6]);
    TEST_ASSERT_EQUAL_HEX16(COL(3), fb[15 * 80 + 7]);
    TEST_ASSERT_EQUAL_HEX16(COL(3), fb[15 * 80 + 11]);
    TEST_ASSERT_EQUAL_HEX16(0, fb[15 * 80 + 12]);

    /* a cell that overruns its atlas draws only the part inside it (never a read past the planes) */
    for (int y = 0; y < 10; y++) {
        s_atrgb[y * AT_W + AT_W - 1] = 0xFFFF;
        s_ata[y * AT_W + AT_W - 1] = 255;
    }
    s_atrgb[0] = 0x1234;
    s_ata[0] = 255;
    const fk_cell_t over[2] = { { AT_W - 1, 2, 10, 20, 0, -8, 10 }, { -3, -2, 4, 3, 0, -8, 4 } };
    const fk_atlas_t bad = { &s_atimg, over, "XY", 10 };
    fill(fb, 80 * 32, 0);
    fk_atlas_text(fb, 80, rect(0, 0, 80, 32), &bad, "XY", 4, 20, 0, 255);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 80; x++) {
            uint16_t want = 0;
            if (x == 4 && y >= 12 && y < 20) want = 0xFFFF;                      /* X: column 63, rows 2..9 */
            if (x == 17 && y == 14) want = 0x1234;                               /* Y: cut to (0, 0, 1, 1) */
            TEST_ASSERT_EQUAL_HEX16(want, fb[y * 80 + x]);
        }
}

/* ---- the annulus */

/* A strip of n columns and nr rows, white, alpha 255 in column `lit` only (all columns if lit < 0). */
typedef struct { omni_strip_t st; uint16_t *rgb; uint8_t *a; } strip_buf_t;
static void strip_make(strip_buf_t *b, uint16_t n, uint8_t nr, int lit)
{
    const size_t S = (size_t)n + OMNI_STRIP_PAD;
    b->rgb = calloc(S * nr, 2);
    b->a = calloc(S * nr, 1);
    for (size_t r = 0; r < nr; r++)
        for (size_t c = 0; c < n; c++) {
            b->rgb[r * S + c] = 0xFFFF;
            b->a[r * S + c] = (lit < 0 || (int)c == lit) ? 255 : 0;
        }
    b->st = (omni_strip_t){ n, (uint16_t)S, 0, nr, 0, b->rgb, b->a };
}
static void strip_free(strip_buf_t *b) { free(b->rgb); free(b->a); }

static fk_annulus_t *build(float cx, float cy, float r0, float r1, uint16_t n, void **mem)
{
    const size_t bytes = fk_annulus_bytes(r0, r1, n);
    TEST_ASSERT_TRUE(bytes > 0);
    *mem = malloc(bytes);
    fk_annulus_t *an = fk_annulus_build(*mem, bytes, cx, cy, r0, r1, n);
    TEST_ASSERT_NOT_NULL(an);
    return an;
}

static void test_annulus_offset_rotates(void)
{
    enum { S = 128 };
    static uint16_t fa[S * S], fb[S * S];
    const float cx = 64, cy = 64.5f, r0 = 20, r1 = 40;
    const uint16_t n = 256;
    void *mem;
    fk_annulus_t *an = build(cx, cy, r0, r1, n, &mem);
    const omni_rect_t c = rect(0, 0, S, S);

    /* every annulus pixel, and none outside [r0, r1), is written */
    strip_buf_t all;
    strip_make(&all, n, 20, -1);
    fill(fa, S * S, CANARY);
    fk_annulus_draw(an, &all.st, 0, 255, fa, S, c);
    uint32_t inside = 0;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            const double X = x + 0.5 - cx, Y = cy - y - 0.5, r2 = X * X + Y * Y;
            const bool in = r2 >= (double)r0 * r0 && r2 < (double)r1 * r1;
            inside += in;
            TEST_ASSERT_EQUAL_HEX16(in ? 0xFFFF : CANARY, fa[y * S + x]);
        }
    TEST_ASSERT_EQUAL_UINT32(inside, fk_annulus_px(an));
    strip_free(&all);

    /* offset k lights the pixels column c - k lights at offset 0: the lit set turns by k entries */
    const int col = 10, ks[4] = { 1, 7, 64, 255 };
    for (int j = 0; j < 4; j++) {
        const int k = ks[j];
        strip_buf_t sk, s0;
        strip_make(&sk, n, 20, col);
        strip_make(&s0, n, 20, (col - k + n) % n);
        fill(fa, S * S, CANARY);
        fill(fb, S * S, CANARY);
        fk_annulus_draw(an, &sk.st, (uint16_t)k, 255, fa, S, c);
        fk_annulus_draw(an, &s0.st, 0, 255, fb, S, c);
        TEST_ASSERT_EQUAL_HEX16_ARRAY(fb, fa, S * S);
        /* and they sit at theta index col - k (clockwise from 12), within a table step at a cell edge */
        int lit = 0;
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                if (fa[y * S + x] == CANARY) continue;
                lit++;
                const double X = x + 0.5 - cx, Y = cy - y - 0.5;
                double th = atan2(X, Y);
                if (th < 0) th += 2 * PI_D;
                const double e = th * n / (2 * PI_D), want = (double)((col - k + n) % n);
                double d = e - want;
                if (d > n / 2.0) d -= n;
                if (d < -n / 2.0) d += n;
                TEST_ASSERT_TRUE(d > -0.005 && d < 1.005);   /* the table's error, no edge redo (compose.h) */
            }
        TEST_ASSERT_TRUE(lit > 0);
        strip_free(&sk);
        strip_free(&s0);
    }
    /* a strip that does not fit draws nothing */
    strip_buf_t bad;
    strip_make(&bad, 128, 20, -1);
    fill(fa, S * S, CANARY);
    fk_annulus_draw(an, &bad.st, 0, 255, fa, S, c);
    strip_free(&bad);
    strip_make(&bad, n, 19, -1);
    fk_annulus_draw(an, &bad.st, 0, 255, fa, S, c);
    strip_free(&bad);
    for (int i = 0; i < S * S; i++) TEST_ASSERT_EQUAL_HEX16(CANARY, fa[i]);
    free(mem);
}

/* The exact annulus pixel count about a centre with fractions (fx, fy), by brute force in double. */
static uint32_t count_px(double fx, double fy, double r0, double r1)
{
    uint32_t n = 0;
    const int R = (int)ceil(r1) + 2;
    for (int y = -R; y <= R; y++)
        for (int x = -R; x <= R; x++) {
            const double X = x + 0.5 - fx, Y = fy - y - 0.5, r2 = X * X + Y * Y;
            n += r2 >= r0 * r0 && r2 < r1 * r1;
        }
    return n;
}

static void test_annulus_bytes_bound(void)
{
    const size_t bytes = fk_annulus_bytes(170, 224, 2048);
    uint32_t npx = 0;
    for (int f = 0; f < 4; f++) {
        const uint32_t k = count_px((f & 1) * 0.5, (f >> 1) * 0.5, 170, 224);
        if (k > npx) npx = k;
    }
    /* 3 B per annulus pixel, plus a header: the map's struct (<= 64 B), alignment, and a 12 B record per row */
    const size_t hdr = 64 + 8 + 12 * (2 * 224 + 2);
    printf("annulus r 170-224 n 2048: %zu B for %u px (%.3f B/px, header allowance %zu B)\n", bytes, npx,
           (double)bytes / npx, hdr);
    TEST_ASSERT_TRUE(bytes <= 3 * (size_t)npx + hdr);
    TEST_ASSERT_TRUE(bytes >= 3 * (size_t)npx);

    /* the build: exactly the pixels, short memory and bad arguments refused */
    void *mem = malloc(bytes);
    const float cs[4][2] = { { 233, 233 }, { 233.5f, 233 }, { 233, 232.5f }, { 100.5f, 300.5f } };
    for (int i = 0; i < 4; i++) {
        fk_annulus_t *an = fk_annulus_build(mem, bytes, cs[i][0], cs[i][1], 170, 224, 2048);
        TEST_ASSERT_NOT_NULL(an);
        TEST_ASSERT_EQUAL_UINT32(count_px(cs[i][0] - floorf(cs[i][0]), cs[i][1] - floorf(cs[i][1]), 170, 224),
                                 fk_annulus_px(an));
    }
    TEST_ASSERT_NULL(fk_annulus_build(mem, bytes - 1, 233, 233, 170, 224, 2048));
    TEST_ASSERT_NULL(fk_annulus_build(mem, bytes, 233.25f, 233, 170, 224, 2048));   /* off the half-pixel grid */
    TEST_ASSERT_NULL(fk_annulus_build(mem, bytes, 233, 233, 170, 224, 2000));       /* not a power of two */
    TEST_ASSERT_NULL(fk_annulus_build(NULL, bytes, 233, 233, 170, 224, 2048));
    TEST_ASSERT_EQUAL(0, fk_annulus_bytes(224, 170, 2048));
    TEST_ASSERT_EQUAL(0, fk_annulus_bytes(0, 300, 2048));                            /* more than 256 rows */
    TEST_ASSERT_EQUAL(0, fk_annulus_bytes(170, 224, 3));
    free(mem);
}

/* An ORS1 strip exactly as dw_pack.py writes one: "ORS1", u16 n_theta, u8 r_min, u8 n_r, u8 level, u8 0, u16 stride
 * (n + 32), 4 zero bytes; then the RGB565 plane (LE), then the A8 plane, n_r rows of stride entries each. */
static void test_annulus_ors1_convention(void)
{
    const uint16_t n = 2048, stride = n + 32;
    const uint8_t nr = 54;
    const size_t size = 16 + (size_t)stride * nr * 3;
    uint16_t *file = calloc(size / 2 + 1, 2);
    uint8_t *p = (uint8_t *)file;
    memcpy(p, "ORS1", 4);
    p[4] = n & 255;
    p[5] = n >> 8;
    p[6] = 170;
    p[7] = nr;
    p[8] = 0;
    p[10] = stride & 255;
    p[11] = stride >> 8;
    uint8_t *a = p + 16 + (size_t)stride * nr * 2;
    for (int r = 0; r < nr; r++) {
        uint8_t *px = p + 16 + ((size_t)r * stride + n / 4) * 2;   /* column n/4: 3 o'clock */
        px[0] = 0xFF;
        px[1] = 0xFF;
        a[(size_t)r * stride + n / 4] = 255;
    }
    omni_strip_t st;
    TEST_ASSERT_TRUE(omni_strip_map(&st, file, size));

    static uint16_t fb[W * W];
    fill(fb, W * W, 0);
    void *mem;
    fk_annulus_t *an = build(233, 233, 170, 224, n, &mem);
    fk_annulus_draw(an, &st, 0, 255, fb, W, rect(0, 0, W, W));
    int lit = 0, rmin = 999, rmax = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (!fb[y * W + x]) continue;
            lit++;
            const double X = x + 0.5 - 233, Y = 233 - y - 0.5;
            const double th = atan2(X, Y);   /* clockwise from 12: 3 o'clock is pi/2, the cell [pi/2, pi/2 + 2pi/n) */
            TEST_ASSERT_TRUE(X > 0);
            TEST_ASSERT_TRUE(th > PI_D / 2 - 0.01 * 2 * PI_D / n && th < PI_D / 2 + 1.01 * 2 * PI_D / n);
            const int r = (int)sqrt(X * X + Y * Y);
            if (r < rmin) rmin = r;
            if (r > rmax) rmax = r;
        }
    printf("3 o'clock: %d px lit, r %d..%d\n", lit, rmin, rmax);
    TEST_ASSERT_TRUE(lit >= 54);
    TEST_ASSERT_EQUAL(170, rmin);
    TEST_ASSERT_EQUAL(223, rmax);
    free(mem);
    free(file);
}

/* Every lit (non-canary) pixel of fb (S x S, centre cx, cy) lies in theta entry `want` of n, by double atan2, within
 * the table's error at a cell edge (compose.h); returns the count. */
static int lit_in_entry(const uint16_t *fb, int S, double cx, double cy, int n, int want)
{
    int lit = 0;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            if (fb[y * S + x] == CANARY) continue;
            lit++;
            double th = atan2(x + 0.5 - cx, cy - y - 0.5);
            if (th < 0) th += 2 * PI_D;
            double d = th * n / (2 * PI_D) - want;
            if (d > n / 2.0) d -= n;
            if (d < -n / 2.0) d += n;
            if (!(d > -0.005 && d < 1.005)) {
                printf("pixel (%d, %d): entry %.4f, want %d\n", x, y, th * n / (2 * PI_D), want);
                TEST_FAIL();
            }
        }
    return lit;
}

/* m6: wrap at n 2048 against the oracle: a column next to 12 o'clock on either side, and th_off >= n (masked). */
static void test_annulus_wrap_2048(void)
{
    static uint16_t fa[W * W], fb[W * W];
    const uint16_t n = 2048;
    void *mem;
    fk_annulus_t *an = build(233, 233, 170, 224, n, &mem);
    const omni_rect_t c = rect(0, 0, W, W);
    const struct { int col; uint32_t off; int want; } cases[5] = {
        { 2047, 0, 2047 },            /* just counter-clockwise of 12 */
        { 0, 0, 0 },                  /* just clockwise of 12 */
        { 3, n + 10, 2041 },          /* th_off past n: the offset mod n, wrapping through 12 */
        { 2040, 65516, 12 },          /* 65516 = -20 mod n: column 2040 shows at entry 2060 mod n = 12 */
        { 5, 65535, 6 },              /* the largest th_off: -1 mod n */
    };
    for (int k = 0; k < 5; k++) {
        const uint16_t off = (uint16_t)cases[k].off;
        strip_buf_t sk, s0;
        strip_make(&sk, n, 54, cases[k].col);
        strip_make(&s0, n, 54, cases[k].want);
        fill(fa, W * W, CANARY);
        fill(fb, W * W, CANARY);
        fk_annulus_draw(an, &sk.st, off, 255, fa, W, c);
        fk_annulus_draw(an, &s0.st, 0, 255, fb, W, c);
        TEST_ASSERT_EQUAL_HEX16_ARRAY(fb, fa, W * W);
        TEST_ASSERT_TRUE(lit_in_entry(fa, W, 233, 233, n, cases[k].want) > 0);   /* a cell near 12 is < 1 px wide */
        strip_free(&sk);
        strip_free(&s0);
    }
    free(mem);
}

/* m1: partial clips against a canary frame, guarded on every side (above, below, left and right of the clip). The
 * annulus: clips cutting the ring's top rows, a run's ends on the right, and the hole; atlas text across a clip. */
static void test_annulus_partial_clip(void)
{
    static uint16_t fb[W * W];
    void *mem;
    fk_annulus_t *an = build(233, 233, 170, 224, 2048, &mem);
    strip_buf_t all;
    strip_make(&all, 2048, 54, -1);
    const omni_rect_t clips[4] = { rect(200, 20, 300, 31), rect(400, 180, 452, 191), rect(150, 150, 320, 320),
                                   rect(5, 228, 61, 236) };
    for (int k = 0; k < 4; k++) {
        const omni_rect_t c = clips[k];
        fill(fb, W * W, CANARY);
        fk_annulus_draw(an, &all.st, 0, 255, at(fb, W, c), W, c);
        int n = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const double X = x + 0.5 - 233, Y = 233 - y - 0.5, r2 = X * X + Y * Y;
                const bool in = x >= c.x0 && x < c.x1 && y >= c.y0 && y < c.y1 && r2 >= 170.0 * 170 && r2 < 224.0 * 224;
                n += in;
                if (fb[y * W + x] != (in ? 0xFFFF : CANARY)) {
                    printf("clip %d pixel (%d, %d): %04x\n", k, x, y, fb[y * W + x]);
                    TEST_FAIL();
                }
            }
        TEST_ASSERT_TRUE(k == 2 ? n == 0 : n > 50);   /* clip 2 lies inside the hole: nothing drawn */
    }
    strip_free(&all);
    free(mem);
}

static void test_atlas_partial_clip(void)
{
    static uint16_t full[80 * 32], fb[80 * 32];
    make_atlas();
    fill(full, 80 * 32, CANARY);
    fk_atlas_text(full, 80, rect(0, 0, 80, 32), &s_at, "12:34", 10, 20, 9, 255);
    const omni_rect_t clips[3] = { rect(25, 14, 40, 17), rect(12, 10, 30, 22), rect(44, 18, 56, 25) };
    for (int k = 0; k < 3; k++) {
        const omni_rect_t c = clips[k];
        fill(fb, 80 * 32, CANARY);
        fk_atlas_text(at(fb, 80, c), 80, c, &s_at, "12:34", 10, 20, 9, 255);
        int drawn = 0;
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 80; x++) {
                const bool in = x >= c.x0 && x < c.x1 && y >= c.y0 && y < c.y1;
                TEST_ASSERT_EQUAL_HEX16(in ? full[y * 80 + x] : CANARY, fb[y * 80 + x]);
                drawn += in && full[y * 80 + x] != CANARY;
            }
        TEST_ASSERT_TRUE(drawn > 0);
    }
}

/* m2: the script ring's map digest, pinned (compose.h), and independent of where the map's memory starts. */
static void test_annulus_digest(void)
{
    const size_t bytes = fk_annulus_bytes(170, 224, 2048);
    uint8_t *mem = malloc(bytes + 8);
    uint64_t d[3];
    for (int i = 0; i < 3; i++) {
        fk_annulus_t *an = fk_annulus_build(mem + i * 3, bytes, 233, 233, 170, 224, 2048);
        TEST_ASSERT_NOT_NULL(an);
        d[i] = fk_annulus_digest(an);
    }
    printf("annulus 233,233 r 170-224 n 2048 digest: 0x%016llxull\n", (unsigned long long)d[0]);
    TEST_ASSERT_TRUE(d[0] == d[1] && d[1] == d[2]);
    TEST_ASSERT_TRUE(d[0] == FK_ANNULUS_DIGEST_233_170_224_2048);
    fk_annulus_t *other = fk_annulus_build(mem, bytes, 233.5f, 233, 170, 224, 2048);
    TEST_ASSERT_TRUE(fk_annulus_digest(other) != d[0]);
    TEST_ASSERT_TRUE(fk_annulus_digest(NULL) == 0);
    free(mem);
}

/* A frame of every step: a backdrop, an image, a glow, text and the annulus, in clips of `rows` rows. */
static void compose(uint16_t *out, int rows, int cols, const fk_annulus_t *an, const omni_strip_t *st)
{
    const fk_img_t img = img8x6(true);
    static const uint8_t glow[4 * 4] = { 0, 80, 80, 0, 80, 255, 255, 80, 80, 255, 255, 80, 0, 80, 80, 0 };
    const fk_img_t g = { 4, 4, NULL, glow, FK_CF_A8 };
    for (int y0 = 0; y0 < W; y0 += rows)
        for (int x0 = 0; x0 < W; x0 += cols) {
            const omni_rect_t c = rect(x0, y0, x0 + cols < W ? x0 + cols : W, y0 + rows < W ? y0 + rows : W);
            uint16_t *f = at(out, W, c);
            for (int y = c.y0; y < c.y1; y++)
                for (int x = c.x0; x < c.x1; x++) out[y * W + x] = rgb565(x / 2, y / 2, (x ^ y) & 255);
            fk_annulus_draw(an, st, 333, 220, f, W, c);
            fk_blit(f, W, c, &img, 229, 101, 255);
            fk_blit(f, W, c, &img, 3, 460, 180);
            fk_add_a8(f, W, c, &g, 120, 140, 226, 190, rgb565(255, 220, 170), 230);
            fk_atlas_text(f, W, c, &s_at, "12:34", 150, 240, 21, 255);
            fk_atlas_text(f, W, c, &s_at, "10:57", 160, 300, 0, 140);
        }
}

static void test_compose_band_exact(void)
{
    static uint16_t whole[W * W], bands[W * W];
    make_atlas();
    strip_buf_t sb;
    strip_make(&sb, 2048, 54, -1);
    for (size_t i = 0; i < (size_t)(2048 + 32) * 54; i++) {   /* a varied strip: every alpha and colour */
        sb.rgb[i] = (uint16_t)(i * 2654435761u >> 13);
        sb.a[i] = (uint8_t)(i * 7);
    }
    void *mem;
    fk_annulus_t *an = build(233, 233, 170, 224, 2048, &mem);
    compose(whole, W, W, an, &sb.st);
    compose(bands, 8, W, an, &sb.st);
    TEST_ASSERT_EQUAL_HEX16_ARRAY(whole, bands, W * W);
    compose(bands, 8, 37, an, &sb.st);   /* and in odd columns */
    TEST_ASSERT_EQUAL_HEX16_ARRAY(whole, bands, W * W);
    strip_free(&sb);
    free(mem);
}

/* Not a gate, and not in the default run (it asserts nothing): DW_COMPOSE_BENCH=1 runs it. The host's time per
 * composited frame in 8-row bands: backdrop copy, the r 170-224 annulus, an image, a glow and two lines of text. */
static void test_compose_frame_time(void)
{
    static uint16_t out[W * W];
    make_atlas();
    strip_buf_t sb;
    strip_make(&sb, 2048, 54, -1);
    for (size_t i = 0; i < (size_t)(2048 + 32) * 54; i++) sb.a[i] = (uint8_t)(i * 7);
    void *mem;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    fk_annulus_t *an = build(233, 233, 170, 224, 2048, &mem);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double build_ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    const int N = 50;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++) compose(out, 8, W, an, &sb.st);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double ms = ((double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6) / N;
    printf("host time per composited frame (8-row bands, %d frames): %.3f ms; annulus map build %.3f ms\n", N, ms,
           build_ms);
    strip_free(&sb);
    free(mem);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_img_from_bin);
    RUN_TEST(test_blit_clip);
    RUN_TEST(test_blit_alpha_matches_blend);
    RUN_TEST(test_add_a8_saturates);
    RUN_TEST(test_add_a8_one_to_one_and_two_gains);
    RUN_TEST(test_atlas_fixed_cells);
    RUN_TEST(test_annulus_offset_rotates);
    RUN_TEST(test_annulus_bytes_bound);
    RUN_TEST(test_annulus_ors1_convention);
    RUN_TEST(test_compose_band_exact);
    RUN_TEST(test_annulus_wrap_2048);
    RUN_TEST(test_annulus_partial_clip);
    RUN_TEST(test_atlas_partial_clip);
    RUN_TEST(test_annulus_digest);
    if (getenv("DW_COMPOSE_BENCH")) RUN_TEST(test_compose_frame_time);
    return UNITY_END();
}
