/* The two-plane atlases (rd wave 1 Task 7, PR29): rd_atlas2_from_bin over the committed rd_f_vt28.bin gives a glow
 * and a core atlas sharing one cell table and one glyph string, each an ordinary fk_atlas_t that fk_atlas_text_rgb
 * draws (glow first, then core); a truncated, misaligned or mislabelled bin is refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "fk/compose.h"
#include "rd/atlas2.h"
#include "rd/assets_meta.h"
#include "rd_assets_host.h"

static const uint8_t *s_bin;
static size_t s_n;

void setUp(void) {}
void tearDown(void) {}

static void test_rd_atlas2_committed_vt28(void)
{
    rd_atlas2_t a;
    TEST_ASSERT_TRUE(rd_atlas2_from_bin(s_bin, s_n, &a));
    TEST_ASSERT_EQUAL_PTR(&a.glow_img, a.glow.img);
    TEST_ASSERT_EQUAL_PTR(&a.core_img, a.core.img);
    TEST_ASSERT_EQUAL(ASSET_RD_F_VT28_W, a.glow_img.w);
    TEST_ASSERT_EQUAL(ASSET_RD_F_VT28_H, a.glow_img.h);
    TEST_ASSERT_EQUAL(a.glow_img.w, a.core_img.w);
    TEST_ASSERT_EQUAL(a.glow_img.h, a.core_img.h);
    TEST_ASSERT_EQUAL(FK_CF_A8, a.glow_img.cf);
    TEST_ASSERT_EQUAL(FK_CF_A8, a.core_img.cf);
    TEST_ASSERT_NULL(a.glow_img.rgb);
    TEST_ASSERT_TRUE(a.glow_img.a != a.core_img.a);
    /* one cell table and one glyph string for both planes */
    TEST_ASSERT_EQUAL_PTR(a.glow.cells, a.core.cells);
    TEST_ASSERT_EQUAL_PTR(a.glow.glyphs, a.core.glyphs);
    TEST_ASSERT_EQUAL(a.glow.line_h, a.core.line_h);
    TEST_ASSERT_EQUAL(65, (int)strlen(a.glow.glyphs));   /* ASCII 0x20-0x5F and the middle dot */
    TEST_ASSERT_EQUAL_CHAR(' ', a.glow.glyphs[0]);
    TEST_ASSERT_EQUAL_CHAR('_', a.glow.glyphs[63]);
    TEST_ASSERT_EQUAL_HEX8(0xB7, (uint8_t)a.glow.glyphs[64]);
    /* the glow spreads wider than the core, inside the same cells */
    unsigned glow = 0, core = 0;
    for (size_t i = 0; i < (size_t)a.glow_img.w * a.glow_img.h; i++) {
        glow += a.glow_img.a[i] != 0;
        core += a.core_img.a[i] != 0;
    }
    TEST_ASSERT_TRUE(core > 0);
    TEST_ASSERT_TRUE(glow > core);
}

static void test_rd_atlas2_draws_both_planes(void)
{
    rd_atlas2_t a;
    TEST_ASSERT_TRUE(rd_atlas2_from_bin(s_bin, s_n, &a));
    enum { W = 120, H = 60 };
    static uint16_t fb[W * H];
    const omni_rect_t clip = {0, 0, W, H};
    memset(fb, 0, sizeof fb);
    fk_atlas_text_rgb(fb, W, clip, &a.glow, "OK", 10, 40, 0, 0x07E0, 255, 0);
    unsigned lit_glow = 0;
    for (int i = 0; i < W * H; i++) lit_glow += fb[i] != 0;
    memset(fb, 0, sizeof fb);
    fk_atlas_text_rgb(fb, W, clip, &a.core, "OK", 10, 40, 0, 0x07E0, 255, 0);
    unsigned lit_core = 0;
    for (int i = 0; i < W * H; i++) lit_core += fb[i] != 0;
    TEST_ASSERT_TRUE(lit_core > 20);
    TEST_ASSERT_TRUE(lit_glow > lit_core);
    TEST_ASSERT_EQUAL(fk_atlas_width(&a.glow, "OK", 0), fk_atlas_width(&a.core, "OK", 0));
}

static void test_rd_atlas2_refuses_bad_bins(void)
{
    rd_atlas2_t a;
    memset(&a, 0x5A, sizeof a);
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(s_bin, s_n - 1, &a));       /* truncated */
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(s_bin, s_n + 2, &a));       /* longer than its planes */
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(s_bin, 15, &a));            /* shorter than the header */
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(NULL, s_n, &a));
    uint8_t *copy = malloc(s_n + 2);
    TEST_ASSERT_NOT_NULL(copy);
    memcpy(copy, s_bin, s_n);
    TEST_ASSERT_TRUE(rd_atlas2_from_bin(copy, s_n, &a));
    copy[0] ^= 1;                                                    /* the magic */
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(copy, s_n, &a));
    copy[0] ^= 1;
    copy[14] = 1;                                                    /* the reserved half-word */
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(copy, s_n, &a));
    copy[14] = 0;
    const uint16_t n = (uint16_t)(copy[8] | copy[9] << 8);
    copy[16 + 14 * n] = 0;                                           /* the glyph string shorter than its cells */
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(copy, s_n, &a));
    memcpy(copy, s_bin, s_n);
    copy[16] = 0xFF;                                                 /* cell 0's x far outside the plane */
    copy[17] = 0x7F;
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(copy, s_n, &a));
    memcpy(copy + 1, s_bin, s_n);                                    /* an odd address (the cells are int16) */
    TEST_ASSERT_FALSE(rd_atlas2_from_bin(copy + 1, s_n, &a));
    free(copy);
}

int main(void)
{
    if (!rd_assets_host_open(RD_ASSETS_DIR)) return 1;
    s_bin = rd_assets_host_get(ASSET_RD_F_VT28, &s_n);
    if (!s_bin) {
        fprintf(stderr, "no %s/%s (tools/rd_assets.sh writes it)\n", RD_ASSETS_DIR, ASSET_RD_F_VT28);
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_rd_atlas2_committed_vt28);
    RUN_TEST(test_rd_atlas2_draws_both_planes);
    RUN_TEST(test_rd_atlas2_refuses_bad_bins);
    const int rc = UNITY_END();
    rd_assets_host_close();
    return rc;
}
