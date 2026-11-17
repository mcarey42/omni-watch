#include "unity.h"
#include "omni/globe.h"
#include "omni/globe_check.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Task 11's halo rewrite (one per-pixel routine; sqrtf and reciprocals instead of hypotf and divisions) against the
 * halo as Task 3 / Task 7 wrote it (0027362), kept here verbatim as the reference, as test_globe_lut keeps the double
 * LUT build. Measured (glibc x86-64): 1 of 90 028 992 pixels differs, by 1. */

static inline int ex5(unsigned v) { return (int)((v << 3) | (v >> 2)); }
static inline int ex6(unsigned v) { return (int)((v << 2) | (v >> 4)); }
static inline int ex_r(uint16_t c) { return ex5(c >> 11); }
static inline int ex_g(uint16_t c) { return ex6((c >> 5) & 63u); }
static inline int ex_b(uint16_t c) { return ex5(c & 31u); }
static inline uint16_t pack(int r, int g, int b)
{
    return (uint16_t)(((unsigned)r >> 3) << 11 | ((unsigned)g >> 2) << 5 | ((unsigned)b >> 3));
}

/* ---- Halo: two canvas radial-gradient rings, source-over in float. Stops are interpolated unpremultiplied,
 * as the mockup's canvas (Skia) does. The rings' inner edges lie under the globe and are point-sampled at pixel
 * centres; the outer edge (1.07 R) is antialiased over +-0.4 px. */

typedef struct { float r, g, b, a; } rgba_t;   /* unpremultiplied: colour 0..255, alpha 0..1 */

static rgba_t rgba(float r, float g, float b, float a) { rgba_t c = { r, g, b, a }; return c; }
static rgba_t lerp4(rgba_t x, rgba_t y, float t)
{
    rgba_t c = { x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t };
    return c;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void over(float px[3], rgba_t c, float cov)
{
    const float a = c.a * cov;
    px[0] = c.r * a + px[0] * (1 - a);
    px[1] = c.g * a + px[1] * (1 - a);
    px[2] = c.b * a + px[2] * (1 - a);
}

static void old_halo(uint16_t *box, uint16_t size, float cx, float cy, float R, const float *dir, float strength)
{
    const float r_in = R * 0.985f, r_lobe = R * 0.99f, r_out = R * 1.07f, aa = 0.4f;
    const rgba_t s0 = rgba(110, 170, 255, clampf(0.30f * strength, 0, 1)), s1 = rgba(70, 130, 255, clampf(0.10f * strength, 0, 1));
    const rgba_t s2 = rgba(40, 90, 255, 0);
    const float la = clampf(0.22f * strength, 0, 1);
    float lx = 0, ly = 0;
    if (dir) {
        float dl = hypotf(dir[0], dir[1]);
        if (dl == 0) dl = 1;
        lx = cx + dir[0] / dl * R * 0.9f;
        ly = cy + dir[1] / dl * R * 0.9f;
    }
    const int ext = (int)ceilf(r_out + aa) + 1;
    int xa = (int)floorf(cx) - ext, xb = (int)floorf(cx) + ext, ya = (int)floorf(cy) - ext, yb = (int)floorf(cy) + ext;
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb > size - 1) xb = size - 1;
    if (yb > size - 1) yb = size - 1;
    /* Only the annulus can be written: per row, visit the columns within r_out + aa (plus a 1 px margin) and skip
     * those certainly inside r_in (less a 1 px margin). The per-pixel tests below still decide, so the output is
     * exactly that of the whole bounding square, at about a seventh of the cost. */
    const float ro = r_out + aa + 1, ri = r_in - 1;
    for (int y = ya; y <= yb; y++) {
        const float dy = y + 0.5f - cy;
        if (fabsf(dy) >= ro) continue;
        const float wo = sqrtf(ro * ro - dy * dy);
        int xl = (int)floorf(cx - wo) - 1, xr = (int)ceilf(cx + wo) + 1, sl = xr + 1, sr = xr;   /* skip [sl, sr] */
        if (xl < xa) xl = xa;
        if (xr > xb) xr = xb;
        if (ri > 0 && fabsf(dy) < ri) {
            const float wi = sqrtf(ri * ri - dy * dy);
            sl = (int)ceilf(cx - wi + 0.5f);
            sr = (int)floorf(cx + wi - 1.5f);
        }
        for (int x = xl; x <= xr; x++) {
            if (x >= sl && x <= sr) { x = sr; continue; }
            const float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            const float cov = clampf((r_out - d) / (2 * aa) + 0.5f, 0, 1);
            if (d < r_in || cov <= 0) continue;
            const float t = clampf((d - r_in) / (r_out - r_in), 0, 1);
            const rgba_t ring = t < 0.35f ? lerp4(s0, s1, t / 0.35f) : lerp4(s1, s2, (t - 0.35f) / 0.65f);
            rgba_t lobe = { 0, 0, 0, 0 };
            if (dir && d >= r_lobe) lobe = rgba(150, 200, 255, la * (1 - clampf(hypotf(x + 0.5f - lx, y + 0.5f - ly) / (R * 0.75f), 0, 1)));
            if (ring.a <= 0 && lobe.a <= 0) continue;
            uint16_t *p = &box[(size_t)y * size + x];
            float c[3] = { (float)ex_r(*p), (float)ex_g(*p), (float)ex_b(*p) };
            over(c, ring, cov);
            over(c, lobe, cov);
            *p = pack((int)clampf(c[0] + 0.5f, 0, 255), (int)clampf(c[1] + 0.5f, 0, 255), (int)clampf(c[2] + 0.5f, 0, 255));
        }
    }
}


/* The faces' halos: live (286 px box, R 131, its lobe anywhere around), spin (350, R 158) and analog's size (316,
 * R 150), at strengths 0.75 / 0.9 / 1 / 2.5, every 10 deg of lobe direction plus none, over black and over a
 * patterned base. */
static const struct { uint16_t size; float R; } HCFG[3] = { { 286, 131 }, { 316, 150 }, { 350, 158 } };
static const float STR[4] = { 0.75f, 0.9f, 1.0f, 2.5f };

static void test_halo_fast_math(void)
{
    long px = 0, differ = 0;
    int worst = 0;
    for (int c = 0; c < 3; c++) {
        const int S = HCFG[c].size;
        uint16_t *a = malloc((size_t)S * S * 2), *b = malloc((size_t)S * S * 2);
        for (int si = 0; si < 4; si++)
            for (int base = 0; base < 2; base++)
                for (int k = -1; k < 36; k++) {
                    const float dir[2] = { cosf(k * 0.17453293f), sinf(k * 0.17453293f) };
                    for (int i = 0; i < S * S; i++) a[i] = b[i] = base ? (uint16_t)(i * 0x9e37u ^ (i >> 7)) : 0;
                    old_halo(a, (uint16_t)S, S / 2.0f, S / 2.0f, HCFG[c].R, k < 0 ? NULL : dir, STR[si]);
                    omni_globe_halo(b, (uint16_t)S, S / 2.0f, S / 2.0f, HCFG[c].R, k < 0 ? NULL : dir, STR[si]);
                    for (int i = 0; i < S * S; i++) {
                        if (a[i] == b[i]) continue;
                        differ++;
                        const int d[3] = { abs((a[i] >> 11) - (b[i] >> 11)), abs(((a[i] >> 5) & 63) - ((b[i] >> 5) & 63)),
                                           abs((a[i] & 31) - (b[i] & 31)) };
                        for (int q = 0; q < 3; q++) worst = d[q] > worst ? d[q] : worst;
                        printf("  differs: size %d str %.2f base %d dir %d px (%d, %d): %04x vs %04x\n", S, STR[si], base,
                               k, i % S, i / S, a[i], b[i]);
                    }
                    px += S * S;
                }
        free(a);
        free(b);
    }
    printf("halo fast math vs Task 7's: %ld of %ld pixels differ, worst %d (565 units)\n", differ, px, worst);
    /* a reciprocal or sqrtf instead of a division or hypotf moves a float by an ulp, which changes the packed pixel
     * only when the colour sits on a rounding boundary: at most 1 pixel in a million, by 1 */
    TEST_ASSERT_TRUE(differ * 1000000 <= px);
    TEST_ASSERT_TRUE(worst <= 1);
}

/* The halo pinned: FNV-1a of the three faces' halos (black base, strength 0.9, lobe at 30 deg; and none). */
#define HALO_DIGEST 0xcfc0b70e256ff8b6ull
static void test_halo_digest(void)
{
    uint64_t h = OMNI_FNV1A_INIT;
    const float dir[2] = { 0.8660254f, -0.5f };
    for (int c = 0; c < 3; c++)
        for (int lobe = 0; lobe < 2; lobe++) {
            const int S = HCFG[c].size;
            uint16_t *b = calloc((size_t)S * S, 2);
            omni_globe_halo(b, (uint16_t)S, S / 2.0f, S / 2.0f, HCFG[c].R, lobe ? dir : NULL, 0.9f);
            h = omni_fnv1a64(h, b, (size_t)S * S * 2);
            free(b);
        }
    printf("halo digest 0x%016llxull\n", (unsigned long long)h);
    TEST_ASSERT_EQUAL_HEX64(HALO_DIGEST, h);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_halo_fast_math);
    RUN_TEST(test_halo_digest);
    return UNITY_END();
}
