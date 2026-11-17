#include "unity.h"
#include "omni/tmr_ring.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* R139: the timer's ring LUT is a flash asset (firmware/apps/omni/assets/tmr_lut.bin, written by tools/ringpack with
 * omni_tmr_lut_file_write) and the face loads it in place instead of building it at create (2.97-4.09 s on the
 * device). The asset must be the LUT omni_tmr_lut_build gives here, byte for byte, so the ring the device draws from it
 * is the one the pinned digest a780930f5070a851 covers; the loader refuses anything else. */

#define NPIX (OMNI_TMR_BOX * OMNI_TMR_BOX)

static omni_tmr_lut_t g_built;
static void *g_mem;
static uint8_t *g_file;
static size_t g_size;
static uint16_t g_box[NPIX];

void setUp(void) {}
void tearDown(void) {}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n + 64);   /* + 64: room to misalign a copy */
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

static void built_once(void)
{
    if (g_mem) return;
    g_mem = malloc(omni_tmr_lut_bytes());
    TEST_ASSERT_NOT_NULL(g_mem);
    TEST_ASSERT_TRUE(omni_tmr_lut_build(&g_built, g_mem, omni_tmr_lut_bytes()));
}

static uint64_t digest(const void *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
    return h;
}

/* The file format round trip: written from the built LUT and loaded back in place, every array is the same. */
static void test_lut_file_round_trip(void)
{
    built_once();
    const size_t fb = omni_tmr_lut_file_bytes(&g_built);
    TEST_ASSERT_EQUAL_UINT32(OMNI_TMR_LUT_HDR + 7u * g_built.n, (uint32_t)fb);
    uint8_t *buf = malloc(fb);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)omni_tmr_lut_file_write(&g_built, buf, fb - 1));   /* too small: nothing */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)fb, (uint32_t)omni_tmr_lut_file_write(&g_built, buf, fb));
    TEST_ASSERT_EQUAL_MEMORY("OTL1", buf, 4);
    omni_tmr_lut_t l;
    TEST_ASSERT_TRUE(omni_tmr_lut_load(&l, buf, fb));
    TEST_ASSERT_EQUAL_UINT32(g_built.n, l.n);
    TEST_ASSERT_EQUAL_MEMORY(g_built.idx, l.idx, 4u * l.n);
    TEST_ASSERT_EQUAL_MEMORY(g_built.ang, l.ang, 2u * l.n);
    TEST_ASSERT_EQUAL_MEMORY(g_built.rq, l.rq, l.n);
    TEST_ASSERT_TRUE((const uint8_t *)l.idx == buf + OMNI_TMR_LUT_HDR);   /* in place: no copy */
    free(buf);
}

/* R139: the committed asset is the renderer's LUT, and the ring drawn from it has the pinned digest. */
static void test_asset_is_the_built_lut(void)
{
    built_once();
    g_file = read_file(ASSETS_DIR "/tmr_lut.bin", &g_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_file, "firmware/apps/omni/assets/tmr_lut.bin missing: run tools/ringpack");
    const size_t fb = omni_tmr_lut_file_bytes(&g_built);
    uint8_t *want = malloc(fb);
    TEST_ASSERT_NOT_NULL(want);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)fb, (uint32_t)omni_tmr_lut_file_write(&g_built, want, fb));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)fb, (uint32_t)g_size);
    TEST_ASSERT_EQUAL_MEMORY(want, g_file, fb);
    free(want);
    printf("tmr_lut.bin %u bytes, %u px, payload digest %016llx\n", (unsigned)g_size, (unsigned)g_built.n,
           (unsigned long long)digest(g_file + OMNI_TMR_LUT_HDR, g_size - OMNI_TMR_LUT_HDR));

    omni_tmr_lut_t l;
    TEST_ASSERT_TRUE(omni_tmr_lut_load(&l, g_file, g_size));
    const omni_tmr_ring_t s = { 25, false, 191, -1 };   /* ARMED 25:00: the pinned frame */
    memset(g_box, 0, sizeof g_box);
    omni_tmr_ring_draw(&l, &s, g_box, 0, OMNI_TMR_BOX);
    TEST_ASSERT_EQUAL_HEX64(0xa780930f5070a851ull, digest(g_box, sizeof g_box));
}

/* The loader refuses a file that is not exactly an OTL1 LUT for this box: the draw writes box[idx]. */
static void test_lut_load_refuses(void)
{
    built_once();
    const size_t fb = omni_tmr_lut_file_bytes(&g_built);
    uint8_t *buf = malloc(fb + 8);
    TEST_ASSERT_NOT_NULL(buf);
    omni_tmr_lut_t l;
    omni_tmr_lut_file_write(&g_built, buf, fb);
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, NULL, fb));
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, OMNI_TMR_LUT_HDR - 1));
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, fb - 1));                   /* truncated */
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, fb + 7));                   /* a tail */
    buf[0] = 'X';
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, fb));                       /* magic */
    buf[0] = 'O';
    buf[4] ^= 1;
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, fb));                       /* another box */
    buf[4] ^= 1;
    uint32_t *idx = (uint32_t *)(void *)(buf + OMNI_TMR_LUT_HDR);
    const uint32_t keep = idx[100];
    idx[100] = idx[99];
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, fb));                       /* not ascending */
    idx[100] = keep;
    const uint32_t last = idx[g_built.n - 1];
    idx[g_built.n - 1] = NPIX;
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, fb));                       /* past the box */
    idx[g_built.n - 1] = last;
    uint8_t *rq = buf + OMNI_TMR_LUT_HDR + 6u * g_built.n;
    rq[5] = 241;
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf, fb));                       /* a radius step past the zone */
    rq[5] = g_built.rq[5];
    TEST_ASSERT_TRUE(omni_tmr_lut_load(&l, buf, fb));
    memmove(buf + 2, buf, fb);
    TEST_ASSERT_FALSE(omni_tmr_lut_load(&l, buf + 2, fb));                   /* misaligned: idx is read as u32 */
    free(buf);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lut_file_round_trip);
    RUN_TEST(test_asset_is_the_built_lut);
    RUN_TEST(test_lut_load_refuses);
    return UNITY_END();
}
