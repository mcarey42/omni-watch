#include "unity.h"
#include "omni/globe_check.h"   /* omni_fnv1a64 */
#include "omni/ring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The prebuilt dial map (R80): firmware/apps/omni/assets/omni_dialmap.bin, written by tools/ringpack with omni_pmap_file_write,
 * must be the bytes omni_pmap_build gives here (the device's code path); the loader validates its header and every
 * span, and the digest the device compares is the file's checksum. */

static omni_pmap_t g_map;
static void *g_mem;
static uint8_t *g_file;
static size_t g_size;

void setUp(void) {}
void tearDown(void) {}

static bool build_b(omni_pmap_t *m, void **mem, const omni_pmap_cfg_t *c)
{
    const size_t b = omni_pmap_bytes(c, false, true);
    *mem = malloc(b);
    return *mem && omni_pmap_build(m, c, false, true, *mem, b);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)n);
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

static void test_dial_cfg(void)
{
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(2048);
    TEST_ASSERT_EQUAL_INT16(36, c.rect.x0);
    TEST_ASSERT_EQUAL_INT16(36, c.rect.y0);
    TEST_ASSERT_EQUAL_INT16(430, c.rect.x1);
    TEST_ASSERT_EQUAL_INT16(430, c.rect.y1);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.k);
    TEST_ASSERT_EQUAL_UINT16(2048, c.n_theta);
    TEST_ASSERT_EQUAL_UINT8(126, c.r_min);
    TEST_ASSERT_EQUAL_UINT8(71, c.n_r);
}

/* R80: the shipped asset is byte-identical to the map built here by the device's own code path. */
static void test_asset_is_the_built_map(void)
{
    size_t fs;
    uint8_t *f = read_file(ASSETS_DIR "/omni_dialmap.bin", &fs);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "firmware/apps/omni/assets/omni_dialmap.bin missing: run tools/assets.sh");
    TEST_ASSERT_EQUAL_size_t(g_size, fs);
    TEST_ASSERT_EQUAL_MEMORY(g_file, f, fs);
    /* and it loads back to the same map */
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(2048);
    const size_t need = omni_pmap_file_mem(f, fs);
    TEST_ASSERT_TRUE(need > 0);
    void *mem = malloc(need);
    omni_pmap_t m;
    char why[64] = "";
    TEST_ASSERT_TRUE_MESSAGE(omni_pmap_load(&m, &c, f, fs, mem, need, why, sizeof why), why);
    TEST_ASSERT_EQUAL_UINT32(g_map.nspans, m.nspans);
    TEST_ASSERT_EQUAL_UINT32(g_map.npx, m.npx);
    TEST_ASSERT_NULL(m.th);
    TEST_ASSERT_NULL(m.ri);
    TEST_ASSERT_EQUAL_MEMORY(g_map.span, m.span, g_map.nspans * sizeof *m.span);
    TEST_ASSERT_EQUAL_MEMORY(g_map.codes, m.codes, (g_map.npx - g_map.nspans + 1) / 2);
    TEST_ASSERT_EQUAL_UINT64(omni_pmap_digest(&g_map), omni_pmap_digest(&m));
    printf("dial map: %u spans, %u px, file %u B, load needs %u B, digest %016llx\n", (unsigned)m.nspans,
           (unsigned)m.npx, (unsigned)fs, (unsigned)need, (unsigned long long)omni_pmap_digest(&m));
    free(mem);
    free(f);
}

static void test_file_layout(void)
{
    TEST_ASSERT_EQUAL_size_t(OMNI_PMAP_HDR + (size_t)g_map.nspans * OMNI_PMAP_SPAN_BYTES +
                                 (g_map.npx - g_map.nspans + 1) / 2,
                             g_size);
    TEST_ASSERT_EQUAL_MEMORY("OPM1", g_file, 4);
    TEST_ASSERT_EQUAL_UINT16(OMNI_PMAP_VERSION, g_file[4] | g_file[5] << 8);
    TEST_ASSERT_EQUAL_UINT16(2048, g_file[6] | g_file[7] << 8);
    uint64_t ck = 0;
    for (int i = 0; i < 8; i++) ck |= (uint64_t)g_file[24 + i] << (8 * i);
    TEST_ASSERT_EQUAL_UINT64(omni_pmap_digest(&g_map), ck);   /* the checksum is the map's digest */
    TEST_ASSERT_EQUAL_size_t(g_size, omni_pmap_file_bytes(&g_map));
    TEST_ASSERT_EQUAL_size_t(0, omni_pmap_file_write(&g_map, g_file, g_size - 1));   /* short: nothing */
    /* a map without the codes (variant A only) has no file */
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(1024);
    const size_t b = omni_pmap_bytes(&c, true, false);
    void *mem = malloc(b);
    omni_pmap_t a;
    TEST_ASSERT_TRUE(omni_pmap_build(&a, &c, true, false, mem, b));
    TEST_ASSERT_EQUAL_size_t(0, omni_pmap_file_bytes(&a));
    free(mem);
}

/* Loading into exactly omni_pmap_file_mem bytes, at any alignment of mem. */
static void test_load_any_alignment(void)
{
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(2048);
    const size_t need = omni_pmap_file_mem(g_file, g_size);
    uint8_t *mem = malloc(need + 3);
    for (int a = 0; a < 4; a++) {
        omni_pmap_t m;
        TEST_ASSERT_TRUE(omni_pmap_load(&m, &c, g_file, g_size, mem + a, need, NULL, 0));
        TEST_ASSERT_EQUAL_UINT64(omni_pmap_digest(&g_map), omni_pmap_digest(&m));
        TEST_ASSERT_FALSE(omni_pmap_load(&m, &c, g_file, g_size, mem + a, need - 5, NULL, 0));
    }
    free(mem);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* Any header or span mismatch is refused (the device then builds the map at create). */
static void test_load_rejects(void)
{
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(2048);
    const size_t need = omni_pmap_file_mem(g_file, g_size);
    void *mem = malloc(need);
    uint8_t *f = malloc(g_size);
    omni_pmap_t m;
    char why[64];
#define REJECT(edit, expect_why)                                                                                     \
    do {                                                                                                             \
        memcpy(f, g_file, g_size);                                                                                   \
        edit;                                                                                                        \
        why[0] = 0;                                                                                                  \
        TEST_ASSERT_FALSE(omni_pmap_load(&m, &c, f, g_size, mem, need, why, sizeof why));                            \
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, expect_why), why);                                                  \
    } while (0)
    REJECT(f[0] = 'X', "magic");
    REJECT(f[4] = OMNI_PMAP_VERSION + 1, "version");
    REJECT(put32(f + 20, (uint32_t)g_size + 2), "size");
    REJECT(f[g_size - 1] ^= 0x10, "checksum");
    REJECT(f[OMNI_PMAP_HDR + 5] ^= 1, "checksum");
    memcpy(f, g_file, g_size);
    why[0] = 0;
    TEST_ASSERT_FALSE(omni_pmap_load(&m, &c, f, g_size - 1, mem, need, why, sizeof why));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, "size"), why);
    /* the cfg the caller expects */
    const omni_pmap_cfg_t c4 = omni_dial_pmap_cfg(4096);
    TEST_ASSERT_FALSE(omni_pmap_load(&m, &c4, g_file, g_size, mem, need, why, sizeof why));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, "cfg"), why);
    omni_pmap_cfg_t ck = c;
    ck.k = 1.4f;
    TEST_ASSERT_FALSE(omni_pmap_load(&m, &ck, g_file, g_size, mem, need, why, sizeof why));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, "cfg"), why);
    /* a span the kernels would read out of bounds, with a checksum that agrees: theta, ring, radius, extent, order */
    for (int t = 0; t < 5; t++) {
        omni_pmap_t bad = g_map;
        omni_pspan_t *sp = malloc(g_map.nspans * sizeof *sp);
        memcpy(sp, g_map.span, g_map.nspans * sizeof *sp);
        bad.span = sp;
        omni_pspan_t *s = &sp[100];
        if (t == 0) s->th0 = 2048;
        if (t == 1) s->ring = 4;
        if (t == 2) s->ri0 = 71;
        if (t == 3) s->n = (int16_t)(430 - s->x0 + 1);
        if (t == 4) s->code += 1;
        TEST_ASSERT_EQUAL_size_t(g_size, omni_pmap_file_write(&bad, f, g_size));
        why[0] = 0;
        TEST_ASSERT_FALSE_MESSAGE(omni_pmap_load(&m, &c, f, g_size, mem, need, why, sizeof why), "bad span loaded");
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, "span"), why);
        free(sp);
    }
    TEST_ASSERT_EQUAL_size_t(0, omni_pmap_file_mem(g_file, OMNI_PMAP_HDR - 1));
    free(f);
    free(mem);
}

/* The file's checksum recomputed after an edit, so only the later checks can refuse it. */
static void reseal(uint8_t *f, size_t size)
{
    const uint64_t h = omni_fnv1a64(14695981039346656037ull, f + OMNI_PMAP_HDR, size - OMNI_PMAP_HDR);   /* the loader's seed */
    put32(f + 24, (uint32_t)h);
    put32(f + 28, (uint32_t)(h >> 32));
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* T4 m5 / T2 m1: an oversized file, inflated counts, a count past the cap, a reserved code, a radius walk out of the
 * strip and an impossible base, each sealed with a checksum that agrees. */
static void test_load_rejects_crafted(void)
{
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(2048);
    const size_t big = g_size + 64;
    uint8_t *f = calloc(1, big);
    void *mem = malloc(omni_pmap_file_mem(g_file, g_size) + 4096);
    omni_pmap_t m;
    char why[96];
    const uint32_t ns = get32(g_file + 8), np = get32(g_file + 12);
#define CRAFT(size, edit, expect_why)                                                                                \
    do {                                                                                                             \
        memset(f, 0, big);                                                                                           \
        memcpy(f, g_file, g_size);                                                                                   \
        edit;                                                                                                        \
        reseal(f, (size));                                                                                           \
        why[0] = 0;                                                                                                  \
        TEST_ASSERT_FALSE(omni_pmap_load(&m, &c, f, (size), mem, omni_pmap_file_mem(g_file, g_size) + 4096, why,     \
                                         sizeof why));                                                               \
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, expect_why), why);                                                  \
    } while (0)
    /* oversized: the size field and the file agree, but are 20 B more than the counts give */
    CRAFT(g_size + 20, put32(f + 20, (uint32_t)g_size + 20), "size");
    /* one more span in the header, with the size and code bytes to match and a zero record appended after the spans:
     * the counts agree, the record cannot (n 0) */
    const size_t s1 = OMNI_PMAP_HDR + (size_t)(ns + 1) * OMNI_PMAP_SPAN_BYTES + (np - ns) / 2;
    CRAFT(s1, (put32(f + 8, ns + 1), put32(f + 16, (np - ns) / 2), put32(f + 20, (uint32_t)s1),
               memmove(f + OMNI_PMAP_HDR + (size_t)(ns + 1) * OMNI_PMAP_SPAN_BYTES, g_file + OMNI_PMAP_HDR + (size_t)ns * OMNI_PMAP_SPAN_BYTES,
                       (np - ns) / 2),
               memset(f + OMNI_PMAP_HDR + (size_t)ns * OMNI_PMAP_SPAN_BYTES, 0, OMNI_PMAP_SPAN_BYTES)),
          "span");
    /* two more pixels in the header (one more code byte): the spans do not add up to it */
    CRAFT(g_size + 1, (put32(f + 12, np + 2), put32(f + 16, (np + 2 - ns + 1) / 2), put32(f + 20, (uint32_t)g_size + 1)), "pixels");
    /* counts past the cap (2^20 pixels): refused before any size is computed */
    CRAFT(g_size, put32(f + 12, 1u << 21), "size");
    TEST_ASSERT_EQUAL_size_t(0, omni_pmap_file_mem(f, g_size));
    CRAFT(g_size, put32(f + 8, 0xfffffff0u), "size");   /* nspans x 20 would wrap a 32-bit size */
    /* a reserved code (12..15) in span 100's stream */
    const uint8_t *s100 = g_file + OMNI_PMAP_HDR + 100 * OMNI_PMAP_SPAN_BYTES;
    const uint32_t code100 = get32(s100 + 16);
    const size_t cbase = OMNI_PMAP_HDR + (size_t)ns * OMNI_PMAP_SPAN_BYTES;
    CRAFT(g_size, f[cbase + (code100 >> 1)] = (uint8_t)((code100 & 1) ? (f[cbase + (code100 >> 1)] & 0x0f) | 0xd0 : (f[cbase + (code100 >> 1)] & 0xf0) | 0x0e),
          "reserved");
    /* a radius walk out of the rows: the first span that steps outward, started on the last row */
    uint32_t up = 0;
    for (uint32_t i = 0; i < ns && !up; i++) {
        const omni_pspan_t *s = &g_map.span[i];
        for (int j = 0; j < s->n - 1; j++) {
            const uint32_t ni = s->code + (uint32_t)j;
            if (((g_map.codes[ni >> 1] >> ((ni & 1) * 4)) & 15) % 3 == 2) {
                up = i;
                break;
            }
            if (((g_map.codes[ni >> 1] >> ((ni & 1) * 4)) & 15) % 3 == 0) break;   /* a step in first: not this one */
        }
    }
    TEST_ASSERT_TRUE(up > 0);
    CRAFT(g_size, f[OMNI_PMAP_HDR + (size_t)up * OMNI_PMAP_SPAN_BYTES + 10] = 70, "radius");
    /* the last span grown to the rect's edge: its codes would run past the stream (and the file) unless n is refused
     * against the header's pixels first (review I1; ASan reads the exact-size copy below) */
    {
        const size_t ls = OMNI_PMAP_HDR + (size_t)(ns - 1) * OMNI_PMAP_SPAN_BYTES;
        uint8_t *g = malloc(g_size);
        memcpy(g, g_file, g_size);
        const int x0 = g[ls + 2] | g[ls + 3] << 8;
        g[ls + 4] = (uint8_t)(430 - x0);
        g[ls + 5] = (uint8_t)((430 - x0) >> 8);
        reseal(g, g_size);
        why[0] = 0;
        TEST_ASSERT_FALSE(omni_pmap_load(&m, &c, g, g_size, mem, omni_pmap_file_mem(g_file, g_size) + 4096, why, sizeof why));
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(why, "span"), why);
        free(g);
    }
    /* a base no pixel step can need (the dial's spans are all base 0; at most 4 at N 2048, k 1, r_min 126) */
    CRAFT(g_size, f[OMNI_PMAP_HDR + 100 * OMNI_PMAP_SPAN_BYTES + 11] = 5, "span 100");
    free(f);
    free(mem);
}

/* The digest covers the spans and the codes, so it tells a map apart from its neighbour at another N, and is the
 * same for a map built with or without the plain LUT. */
static void test_digest(void)
{
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(2048);
    const size_t b = omni_pmap_bytes(&c, true, true);
    void *mem = malloc(b);
    omni_pmap_t ab;
    TEST_ASSERT_TRUE(omni_pmap_build(&ab, &c, true, true, mem, b));
    TEST_ASSERT_EQUAL_UINT64(omni_pmap_digest(&g_map), omni_pmap_digest(&ab));
    free(mem);
    const omni_pmap_cfg_t c1 = omni_dial_pmap_cfg(1024);
    omni_pmap_t m1;
    void *mem1;
    TEST_ASSERT_TRUE(build_b(&m1, &mem1, &c1));
    TEST_ASSERT_TRUE(omni_pmap_digest(&m1) != omni_pmap_digest(&g_map));
    free(mem1);
}

int main(void)
{
    const omni_pmap_cfg_t c = omni_dial_pmap_cfg(2048);
    if (!build_b(&g_map, &g_mem, &c)) {
        printf("dial map build failed\n");
        return 1;
    }
    g_size = omni_pmap_file_bytes(&g_map);
    g_file = malloc(g_size);
    if (!g_file || omni_pmap_file_write(&g_map, g_file, g_size) != g_size) {
        printf("dial map file write failed\n");
        return 1;
    }
    UNITY_BEGIN();
    RUN_TEST(test_dial_cfg);
    RUN_TEST(test_file_layout);
    RUN_TEST(test_asset_is_the_built_map);
    RUN_TEST(test_load_any_alignment);
    RUN_TEST(test_load_rejects);
    RUN_TEST(test_load_rejects_crafted);
    RUN_TEST(test_digest);
    free(g_file);
    free(g_mem);
    return UNITY_END();
}
