#include "unity.h"
#include "omni/brassbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* R112: the three brass backgrounds ship as flash assets (firmware/apps/omni/assets/bg_*.bin), byte-identical to the Stage 3
 * device's prerender (R115: pixel CRC-32s 5cfc94d2, 9fe68698, 9ec99c26, the boot log's "background <n> rendered"
 * lines). omni_bg_render is the device's fallback renderer and tools/bgpack's source. */

#define NPX ((size_t)OMNI_BG_W * OMNI_BG_W)
#define FILE_BYTES (12 + NPX * 2)   /* 434 324 */

static const char *const NAMES[OMNI_BG__COUNT] = {"bg_bezel.bin", "bg_picker.bin", "bg_settings.bin"};
static const uint32_t CRC[OMNI_BG__COUNT] = {0x5cfc94d2u, 0x9fe68698u, 0x9ec99c26u};   /* Task 1 step 1, per enum */
static const uint8_t HDR[12] = {0x19, 0x12, 0x00, 0x00, 0xd2, 0x01, 0xd2, 0x01, 0xa4, 0x03, 0x00, 0x00};

void setUp(void) {}
void tearDown(void) {}

static uint32_t crc32(const uint8_t *p, size_t n) /* zlib's CRC-32 (= the device's esp_rom_crc32_le(0, ...)) */
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = n > 0 ? malloc((size_t)n) : NULL;
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

/* The pixels as the device holds them: little-endian RGB565 bytes. */
static uint32_t crc_px(const uint16_t *px)
{
    uint8_t *b = malloc(NPX * 2);
    TEST_ASSERT_NOT_NULL(b);
    for (size_t k = 0; k < NPX; k++) b[2 * k] = (uint8_t)px[k], b[2 * k + 1] = (uint8_t)(px[k] >> 8);
    const uint32_t c = crc32(b, NPX * 2);
    free(b);
    return c;
}

static void render(omni_bg_t which, int rows, uint16_t *out)
{
    uint8_t *strip = malloc((size_t)OMNI_BG_W * 4 * (size_t)rows);
    TEST_ASSERT_NOT_NULL(strip);
    omni_bg_render_rows(which, out, strip, rows, NULL, NULL);
    free(strip);
}

/* Each committed asset: the size, the header of an opaque 466 x 466 RGB565 image (as tools/assets.sh writes
 * omni_base.bin) and the pixel CRC the Stage 3 device logged for its enum. */
static void test_committed_bg_crcs(void)
{
    for (int i = 0; i < OMNI_BG__COUNT; i++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", ASSETS_DIR, NAMES[i]);
        size_t size;
        uint8_t *f = read_file(path, &size);
        TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(FILE_BYTES, size, NAMES[i]);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(HDR, f, 12, NAMES[i]);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(CRC[i], crc32(f + 12, NPX * 2), NAMES[i]);
        free(f);
    }
}

/* B2/R110: the strip height never changes the bytes (whole square, the device's 59 rows, an odd 7). A count above
 * the square is taken as the square (the strip is never written past OMNI_BG_W rows); rows <= 0 renders nothing. */
static void test_strip_invariance(void)
{
    uint16_t *a = malloc(NPX * 2), *b = malloc(NPX * 2), *c = malloc(NPX * 2);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    for (int i = 0; i < OMNI_BG__COUNT; i++) {
        render((omni_bg_t)i, OMNI_BG_W, a);
        render((omni_bg_t)i, OMNI_BG_STRIP, b);
        render((omni_bg_t)i, 7, c);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, b, NPX * 2, NAMES[i]);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, c, NPX * 2, NAMES[i]);
    }
    render(OMNI_BG_PICKER, OMNI_BG_W, a);
    uint8_t *strip = malloc((size_t)OMNI_BG_W * 4 * OMNI_BG_W);
    TEST_ASSERT_NOT_NULL(strip);
    omni_bg_render_rows(OMNI_BG_PICKER, b, strip, OMNI_BG_W + 100, NULL, NULL);   /* uses only OMNI_BG_W rows */
    TEST_ASSERT_EQUAL_MEMORY(a, b, NPX * 2);
    memset(c, 0xa5, NPX * 2);
    omni_bg_render_rows(OMNI_BG_PICKER, c, strip, 0, NULL, NULL);
    omni_bg_render_rows(OMNI_BG_PICKER, c, strip, -3, NULL, NULL);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xa5, c, NPX * 2);
    free(strip);
    free(a);
    free(b);
    free(c);
}

/* R121/B1/C3: a warning only. The host's bytes depend on glibc's sinf, cosf and atan2f and on the host compiler's
 * FMA contraction, neither of which is the device's (newlib, Xtensa madd.s); the committed captures are the source of
 * truth (R115), and a libm update must not fail the suite with no code change. */
static void test_host_render_crc(void)
{
    uint16_t *px = malloc(NPX * 2);
    TEST_ASSERT_NOT_NULL(px);
    for (int i = 0; i < OMNI_BG__COUNT; i++) {
        render((omni_bg_t)i, OMNI_BG_STRIP, px);
        const uint32_t c = crc_px(px);
        if (c != CRC[i]) {   /* in Unity's own output (an INFO line), never a failure */
            char msg[160];
            snprintf(msg, sizeof msg, "%s: WARN host CRC %08x != committed %08x (libm and FMA differ from the device; R115)",
                     NAMES[i], (unsigned)c, (unsigned)CRC[i]);
            TEST_MESSAGE(msg);
        }
    }
    free(px);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_committed_bg_crcs);
    RUN_TEST(test_strip_invariance);
    RUN_TEST(test_host_render_crc);
    return UNITY_END();
}
