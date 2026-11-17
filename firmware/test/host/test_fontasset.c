/* watch_ui/fontasset.c on the host (rim keyboard Task 8): the committed OFA1 fonts load zero copy, and a corrupt asset
 * (every truncation, every refused header field, huge or misaligned offsets, a glyph past the file, random damage)
 * is refused with the font untouched. Built against stub_lvgl/lvgl.h (LVGL 9.5's font structs). */
#include "unity.h"
#include "fontasset.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KB_ASSETS_DIR
#error "KB_ASSETS_DIR: firmware/components/watch_ui/assets (firmware/test/host/CMakeLists.txt)"
#endif

#define COUNT 97u

/* the LVGL callbacks fontasset_parse installs: never called here */
const void *lv_font_get_bitmap_fmt_txt(lv_font_glyph_dsc_t *g, lv_draw_buf_t *b)
{
    (void)g;
    (void)b;
    return NULL;
}
bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *f, lv_font_glyph_dsc_t *d, uint32_t u, uint32_t n)
{
    (void)f;
    (void)d;
    (void)u;
    (void)n;
    return false;
}

static const uint8_t *s_raw;
static size_t s_raw_size;
const void *assets_raw(const char *name, size_t *size)
{
    if (s_raw && strcmp(name, "kb_mono21.bin") == 0) {
        *size = s_raw_size;
        return s_raw;
    }
    *size = 0;
    return NULL;
}

/* the file in a 4-aligned heap block (the mapping is 64-aligned) */
static uint8_t *load(const char *name, size_t *size)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", KB_ASSETS_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n + 8);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL((size_t)n, fread(p, 1, (size_t)n, f));
    fclose(f);
    *size = (size_t)n;
    return p;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the end of the last glyph's bitmap: every shorter file must be refused */
static size_t used_end(const uint8_t *p)
{
    uint32_t dsc = rd32(p + 16), bm = rd32(p + 20), end = 0;
    for (uint32_t i = 0; i < COUNT; i++) {
        const uint8_t *d = p + dsc + 8 * i;
        uint32_t e = (rd32(d) & 0xFFFFF) + ((uint32_t)d[4] * d[5] * p[5] + 7) / 8;
        if (e > end) end = e;
    }
    return bm + end;
}

static lv_font_t s_font;
static lv_font_fmt_txt_dsc_t s_dsc;

static void poison(void)
{
    memset(&s_font, 0xA5, sizeof s_font);
    memset(&s_dsc, 0xA5, sizeof s_dsc);
}

static void assert_untouched(void)
{
    lv_font_t f;
    lv_font_fmt_txt_dsc_t d;
    memset(&f, 0xA5, sizeof f);
    memset(&d, 0xA5, sizeof d);
    TEST_ASSERT_EQUAL_MEMORY(&f, &s_font, sizeof f);
    TEST_ASSERT_EQUAL_MEMORY(&d, &s_dsc, sizeof d);
}

static bool parse(const uint8_t *p, size_t n) { return fontasset_parse(p, n, &s_font, &s_dsc); }

void setUp(void) { poison(); }
void tearDown(void) {}

static void check_font(const char *name)
{
    size_t n;
    uint8_t *p = load(name, &n);
    TEST_ASSERT_TRUE(parse(p, n));
    const uint32_t dsc = rd32(p + 16), bm = rd32(p + 20);
    TEST_ASSERT_EQUAL_PTR(p + dsc, s_dsc.glyph_dsc);   /* zero copy: into the file */
    TEST_ASSERT_EQUAL_PTR(p + bm, s_dsc.glyph_bitmap);
    TEST_ASSERT_EQUAL_PTR(&s_dsc, s_font.dsc);
    TEST_ASSERT_EQUAL(4, s_dsc.bpp);
    TEST_ASSERT_EQUAL(2, s_dsc.cmap_num);
    TEST_ASSERT_EQUAL(0x20, s_dsc.cmaps[0].range_start);
    TEST_ASSERT_EQUAL(95, s_dsc.cmaps[0].range_length);
    TEST_ASSERT_EQUAL(1, s_dsc.cmaps[0].glyph_id_start);
    TEST_ASSERT_EQUAL(0x2022, s_dsc.cmaps[1].range_start);
    TEST_ASSERT_EQUAL(96, s_dsc.cmaps[1].glyph_id_start);
    TEST_ASSERT_EQUAL(LV_FONT_FMT_TXT_CMAP_SPARSE_TINY, s_dsc.cmaps[1].type);
    TEST_ASSERT_EQUAL(0, s_dsc.cmaps[1].unicode_list[0]);
    TEST_ASSERT_EQUAL(p[7], s_font.line_height);
    TEST_ASSERT_EQUAL((int8_t)p[8], s_font.base_line);
    TEST_ASSERT_NULL(s_font.fallback);
    /* LVGL's view of each descriptor is the file's bytes; glyph 0 reserved; 'M' and the bullet have ink */
    for (uint32_t i = 0; i < COUNT; i++) {
        const uint8_t *d = p + dsc + 8 * i;
        TEST_ASSERT_EQUAL(rd32(d) & 0xFFFFF, s_dsc.glyph_dsc[i].bitmap_index);
        TEST_ASSERT_EQUAL(rd32(d) >> 20, s_dsc.glyph_dsc[i].adv_w);
        TEST_ASSERT_EQUAL(d[4], s_dsc.glyph_dsc[i].box_w);
        TEST_ASSERT_EQUAL((int8_t)d[7], s_dsc.glyph_dsc[i].ofs_y);
    }
    TEST_ASSERT_EQUAL(0, s_dsc.glyph_dsc[0].box_w);
    TEST_ASSERT_TRUE(s_dsc.glyph_dsc['M' - 0x20 + 1].box_w > 0);
    TEST_ASSERT_TRUE(s_dsc.glyph_dsc[96].box_w > 0);
    unsigned g, lh;
    TEST_ASSERT_TRUE(fontasset_info(p, n, &g, &lh));
    TEST_ASSERT_EQUAL(COUNT, g);
    TEST_ASSERT_EQUAL(p[7], lh);
    free(p);
}

static void test_committed_fonts_load_zero_copy(void)
{
    check_font("kb_mono21.bin");
    check_font("kb_mono42.bin");
}

static void test_every_truncation_refused(void)
{
    size_t n;
    uint8_t *p = load("kb_mono21.bin", &n);
    const size_t end = used_end(p);
    TEST_ASSERT_TRUE(end <= n && n - end < 4);   /* zero padding only past the last glyph */
    for (size_t k = 0; k < end; k++) {
        uint8_t *c = malloc(k ? k : 1);           /* exactly k bytes, so ASan sees any read past them */
        memcpy(c, p, k);
        poison();
        TEST_ASSERT_FALSE(parse(c, k));
        assert_untouched();
        free(c);
    }
    for (size_t k = end; k <= n; k++) TEST_ASSERT_TRUE(parse(p, k));
    free(p);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void test_header_fields_refused(void)
{
    size_t n;
    uint8_t *p = load("kb_mono21.bin", &n);
    uint8_t *c = malloc(n);
    const uint32_t dsc = rd32(p + 16), bm = rd32(p + 20);
    struct { size_t at; uint8_t v; } bytes[] = {
        {0, 'X'}, {3, '2'}, {4, 2}, {4, 0}, {5, 3}, {5, 0}, {5, 16}, {6, 1}, {6, 2}, {9, 1},
        {10, 0x21}, {11, 1}, {12, 96}, {12, 98}, {13, 1},
    };
    for (size_t i = 0; i < sizeof bytes / sizeof bytes[0]; i++) {
        memcpy(c, p, n);
        c[bytes[i].at] = bytes[i].v;
        poison();
        TEST_ASSERT_FALSE_MESSAGE(parse(c, n), "a refused header byte passed");
        assert_untouched();
    }
    struct { uint32_t dsc, bm; } offs[] = {
        {dsc + 2, bm}, {dsc, bm + 2}, {dsc + 1, bm}, {0, bm}, {20, bm}, {dsc, dsc},
        {dsc, dsc + 8 * COUNT - 4}, {0xFFFFFFFCu, bm}, {dsc, 0xFFFFFFFCu}, {(uint32_t)n, (uint32_t)n},
        {(uint32_t)n - 8 * COUNT + 4, (uint32_t)n}, {dsc, (uint32_t)n + 4}, {0x80000000u, 0x80000000u},
    };
    for (size_t i = 0; i < sizeof offs / sizeof offs[0]; i++) {
        memcpy(c, p, n);
        put32(c + 16, offs[i].dsc);
        put32(c + 20, offs[i].bm);
        poison();
        TEST_ASSERT_FALSE_MESSAGE(parse(c, n), "a bad offset passed");
        assert_untouched();
    }
    /* metrics are data, not structure: a changed line height still loads */
    memcpy(c, p, n);
    c[7] = 99;
    TEST_ASSERT_TRUE(parse(c, n));
    TEST_ASSERT_EQUAL(99, s_font.line_height);
    free(c);
    free(p);
}

static void test_glyph_past_the_file_refused(void)
{
    size_t n;
    uint8_t *p = load("kb_mono42.bin", &n);
    uint8_t *c = malloc(n);
    const uint32_t dsc = rd32(p + 16);
    const uint32_t cases[][2] = {   /* glyph, the new u32 (bitmap index | adv_w << 20) or box (w | h << 8) */
        {5, 0xFFFFFu | 100u << 20}, {50, (uint32_t)n | 100u << 20}, {96, 0xFFFF0u | 300u << 20}, {0, 1}, {0, 1u << 20},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memcpy(c, p, n);
        put32(c + dsc + 8 * cases[i][0], cases[i][1]);
        poison();
        TEST_ASSERT_FALSE(parse(c, n));
        assert_untouched();
    }
    memcpy(c, p, n);   /* a 255 x 255 box on the last glyph */
    c[dsc + 8 * 96 + 4] = 255;
    c[dsc + 8 * 96 + 5] = 255;
    TEST_ASSERT_FALSE(parse(c, n));
    memcpy(c, p, n);   /* glyph 0 with a box is not reserved */
    c[dsc + 4] = 1;
    TEST_ASSERT_FALSE(parse(c, n));
    free(c);
    free(p);
}

static void test_misaligned_mapping_refused(void)
{
    size_t n;
    uint8_t *p = load("kb_mono21.bin", &n);
    uint8_t *c = malloc(n + 4);
    for (unsigned s = 1; s < 4; s++) {
        memcpy(c + s, p, n);
        poison();
        TEST_ASSERT_FALSE(parse(c + s, n));
        assert_untouched();
    }
    memcpy(c + 4, p, n);
    TEST_ASSERT_TRUE(parse(c + 4, n));
    free(c);
    free(p);
}

static void test_random_damage_is_bounded(void)
{
    size_t n;
    uint8_t *p = load("kb_mono21.bin", &n);
    uint8_t *c = malloc(n);
    uint32_t x = 12345;
    int accepted = 0;
    for (int k = 0; k < 4000; k++) {
        memcpy(c, p, n);
        for (int j = 0; j < 3; j++) {
            x = x * 1103515245u + 12345u;
            const size_t at = (x >> 8) % (k & 1 ? 24u + 8u * COUNT : n);   /* the header and table, or anywhere */
            x = x * 1103515245u + 12345u;
            c[at] = (uint8_t)(x >> 16);
        }
        poison();
        if (!parse(c, n)) {
            assert_untouched();
            continue;
        }
        accepted++;
        /* anything accepted points only inside the file */
        for (uint32_t i = 0; i < COUNT; i++) {
            const lv_font_fmt_txt_glyph_dsc_t *g = &s_dsc.glyph_dsc[i];
            const size_t end = (size_t)(s_dsc.glyph_bitmap - c) + g->bitmap_index + ((size_t)g->box_w * g->box_h * s_dsc.bpp + 7) / 8;
            TEST_ASSERT_TRUE(end <= n);
        }
    }
    TEST_ASSERT_TRUE(accepted > 0);   /* damage in the bitmap's pixels alone is still a font */
    free(c);
    free(p);
}

static void test_load_by_name(void)
{
    size_t n;
    uint8_t *p = load("kb_mono21.bin", &n);
    s_raw = NULL;
    TEST_ASSERT_FALSE(fontasset_load(&s_font, &s_dsc, "kb_mono21.bin"));   /* not mapped */
    assert_untouched();
    s_raw = p;
    s_raw_size = n;
    TEST_ASSERT_FALSE(fontasset_load(&s_font, &s_dsc, "kb_mono42.bin"));   /* missing */
    assert_untouched();
    TEST_ASSERT_FALSE(fontasset_load(&s_font, &s_dsc, NULL));
    TEST_ASSERT_TRUE(fontasset_load(&s_font, &s_dsc, "kb_mono21.bin"));
    TEST_ASSERT_EQUAL_PTR(p + rd32(p + 20), s_dsc.glyph_bitmap);
    s_raw = NULL;
    free(p);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_committed_fonts_load_zero_copy);
    RUN_TEST(test_every_truncation_refused);
    RUN_TEST(test_header_fields_refused);
    RUN_TEST(test_glyph_past_the_file_refused);
    RUN_TEST(test_misaligned_mapping_refused);
    RUN_TEST(test_random_damage_is_bounded);
    RUN_TEST(test_load_by_name);
    return UNITY_END();
}
