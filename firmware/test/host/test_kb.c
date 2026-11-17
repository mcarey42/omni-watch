#include "unity.h"
#include "omni/kb.h"
#include "omni/wipe.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KB_LAY_PATH
#error "KB_LAY_PATH: the committed kb_lay.bin (firmware/test/host/CMakeLists.txt)"
#endif

void setUp(void) {}
void tearDown(void) {}

#define TAU 6.28318530717958647692
#define CX 233
#define CY 233

static uint8_t g_file[256];
static size_t g_size;

static void read_asset(void)
{
    FILE *f = fopen(KB_LAY_PATH, "rb");
    TEST_ASSERT_NOT_NULL(f);
    g_size = fread(g_file, 1, sizeof g_file, f);
    fclose(f);
    TEST_ASSERT_TRUE(g_size > 10 && g_size < sizeof g_file);
}

void test_kb_layout_committed(void)
{
    read_asset();
    omni_kb_layout_t L;
    TEST_ASSERT_TRUE(omni_kb_layout_load(&L, g_file, g_size));
    TEST_ASSERT_EQUAL_UINT8(26, L.n[0]);
    TEST_ASSERT_EQUAL_UINT8(26, L.n[1]);
    TEST_ASSERT_EQUAL_UINT8(26, L.n[2]);
    TEST_ASSERT_EQUAL_UINT8(16, L.n[3]);
    TEST_ASSERT_EQUAL_MEMORY("abcdefghijklmnopqrstuvwxyz", L.keys[0], 26);
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJKLMNOPQRSTUVWXYZ", L.keys[1], 26);
    int seen[128] = {0};
    seen[' '] = 1;
    /* the case sets are the same keys: count the union once */
    for (int s = 0; s < OMNI_KB_SETS; s++)
        for (int i = 0; i < L.n[s]; i++) seen[(int)L.keys[s][i]]++;
    for (int c = 0x20; c <= 0x7E; c++) TEST_ASSERT_TRUE_MESSAGE(seen[c] >= 1, "printable char missing");
    for (int c = 0; c < 0x20; c++) TEST_ASSERT_EQUAL_INT(0, seen[c]);
    TEST_ASSERT_EQUAL_INT(0, seen[0x7F]);
    for (int c = 0x21; c <= 0x7E; c++) TEST_ASSERT_TRUE(seen[c] == 1);   /* no key twice (the cases are distinct) */
}

void test_kb_layout_refused(void)
{
    read_asset();
    uint8_t d[256];
    omni_kb_layout_t L;
    memcpy(d, g_file, g_size); d[0] = 'X';
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size));
    memcpy(d, g_file, g_size); d[4] = 2;
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size));
    memcpy(d, g_file, g_size); d[5] = 3;
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size));
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, g_file, g_size - 1));
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, g_file, 8));
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, NULL, 0));
    memcpy(d, g_file, g_size); d[9] = 0;   /* a set with no keys (the size then no longer fits either) */
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size - 16));
    memcpy(d, g_file, g_size); d[9] = 0;   /* n = 0 with the file's keys left behind: size mismatch */
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size));
    memcpy(d, g_file, g_size); d[g_size] = 'a';   /* one byte too long */
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size + 1));
    memcpy(d, g_file, g_size); d[6] = 33;
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size));
    memcpy(d, g_file, g_size); d[10] = 0x20;
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size));
    memcpy(d, g_file, g_size); d[10] = 0x7F;
    TEST_ASSERT_FALSE(omni_kb_layout_load(&L, d, g_size));
}

void test_kb_layout_freq_order(void)
{
    uint8_t d[10 + 26 * 3 + 16];
    const char *f = "etaoinshrdlcumwfgypbvkjxqz";
    memcpy(d, "OKL1", 4); d[4] = 1; d[5] = 4; d[6] = 26; d[7] = 26; d[8] = 26; d[9] = 16;
    memcpy(d + 10, f, 26);
    memcpy(d + 36, "ETAOINSHRDLCUMWFGYPBVKJXQZ", 26);
    memcpy(d + 62, "0123456789-_.@!?#$%&*+=/:;", 26);
    memcpy(d + 88, "\"'(),<>[\\]^`{|}~", 16);
    omni_kb_layout_t L;
    TEST_ASSERT_TRUE(omni_kb_layout_load(&L, d, sizeof d));
    TEST_ASSERT_EQUAL_MEMORY(f, L.keys[0], 26);
}

static const int g_ns[] = {16, 26, 32};

static int at_polar(double ang, double r, int n)
{
    int x = CX + (int)lround(r * sin(ang)), y = CY - (int)lround(r * cos(ang));
    return omni_kb_index_at((int16_t)x, (int16_t)y, n);
}

void test_kb_hit_centres(void)
{
    for (unsigned k = 0; k < 3; k++) {
        int n = g_ns[k];
        for (int i = 0; i < n; i++) {
            double a = omni_kb_key_angle(i, n);
            TEST_ASSERT_FLOAT_WITHIN(1e-5, TAU * i / n, a);
            TEST_ASSERT_EQUAL_INT(i, at_polar(a, OMNI_KB_RING_R, n));
        }
    }
    /* key 0 at 12 o'clock, key 1 clockwise of it (to the right) */
    TEST_ASSERT_EQUAL_INT(0, omni_kb_index_at(CX, CY - 196, 26));
    TEST_ASSERT_EQUAL_INT(1, omni_kb_index_at(CX + 47, CY - 190, 26));
}

void test_kb_hit_boundaries(void)
{
    for (unsigned k = 0; k < 3; k++) {
        int n = g_ns[k];
        double half = TAU / n / 2;
        for (int i = 0; i < n; i++) {
            double a = omni_kb_key_angle(i, n);
            TEST_ASSERT_EQUAL_INT(i, at_polar(a + half - 0.01, OMNI_KB_RING_R, n));
            TEST_ASSERT_EQUAL_INT(i, at_polar(a - half + 0.01, OMNI_KB_RING_R, n));
            TEST_ASSERT_EQUAL_INT((i + 1) % n, at_polar(a + half + 0.01, OMNI_KB_RING_R, n));
        }
        /* across 12 o'clock */
        TEST_ASSERT_EQUAL_INT(0, at_polar(0.01, OMNI_KB_RING_R, n));
        TEST_ASSERT_EQUAL_INT(n - 1, at_polar(-half - 0.01, OMNI_KB_RING_R, n));
        TEST_ASSERT_EQUAL_INT(0, at_polar(-0.01, OMNI_KB_RING_R, n));
    }
}

void test_kb_cancel_radius(void)
{
    TEST_ASSERT_EQUAL_INT(OMNI_KB_CANCEL, omni_kb_index_at(CX + 130, CY, 26));
    TEST_ASSERT_EQUAL_INT(OMNI_KB_CANCEL, omni_kb_index_at(CX, CY, 26));
    TEST_ASSERT_EQUAL_INT(OMNI_KB_CANCEL, omni_kb_index_at(CX, CY - 130, 26));
    TEST_ASSERT_TRUE(omni_kb_index_at(CX + 131, CY, 26) >= 0);
    TEST_ASSERT_TRUE(omni_kb_index_at(CX, CY - 131, 26) == 0);
    TEST_ASSERT_EQUAL_INT(OMNI_KB_CANCEL, omni_kb_index_at(CX, CY - 131, 0));
}

void test_kb_band_and_loupe(void)
{
    TEST_ASSERT_FALSE(omni_kb_in_band(CX + 170, CY));
    TEST_ASSERT_TRUE(omni_kb_in_band(CX + 171, CY));
    TEST_ASSERT_TRUE(omni_kb_in_band(CX, CY - 233));
    TEST_ASSERT_FALSE(omni_kb_in_band(CX, CY - 234));
    for (int i = 0; i < 26; i++) {
        int16_t x, y;
        omni_kb_loupe_at(i, 26, &x, &y);
        double dx = x - CX, dy = y - CY, r = sqrt(dx * dx + dy * dy);
        TEST_ASSERT_FLOAT_WITHIN(1.0, 111.0, r);
        TEST_ASSERT_EQUAL_INT(OMNI_KB_CANCEL, omni_kb_index_at(x, y, 26));
        /* along key i's radius: the same angle */
        TEST_ASSERT_EQUAL_INT(i, at_polar(atan2(dx, -dy), 150, 26));
    }
    int16_t x, y;
    omni_kb_loupe_at(0, 26, &x, &y);
    TEST_ASSERT_EQUAL_INT(CX, x);
    TEST_ASSERT_EQUAL_INT(CY - 111, y);
}

void test_kb_stroke_classes(void)
{
    TEST_ASSERT_EQUAL_INT(KB_STROKE_RING, omni_kb_stroke(CX, CY - 196, CX + 20, CY - 190, false));
    TEST_ASSERT_EQUAL_INT(KB_STROKE_KEY, omni_kb_stroke(CX, CY + 20, CX + 10, CY + 20, true));
    TEST_ASSERT_EQUAL_INT(KB_STROKE_RESERVED, omni_kb_stroke(233, 233 + 100, 233, 233 + 200, false));
    TEST_ASSERT_EQUAL_INT(KB_STROKE_NONE, omni_kb_stroke(CX, CY + 20, CX + 40, CY + 60, false));
    /* starts outside the centre's r 150 but not on the ring: nothing */
    TEST_ASSERT_EQUAL_INT(KB_STROKE_NONE, omni_kb_stroke(CX, CY + 160, CX, CY + 200, false));
}

static void type(omni_kb_edit_t *e, const char *s)
{
    for (; *s; s++) TEST_ASSERT_EQUAL_INT(KB_TYPED, omni_kb_append(e, *s, 1000));
}

void test_kb_append_delete(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_PASSWORD);
    TEST_ASSERT_FALSE(omni_kb_delete(&e));
    type(&e, "hello");
    TEST_ASSERT_EQUAL_UINT8(5, e.len);
    TEST_ASSERT_EQUAL_STRING("hello", e.buf);
    TEST_ASSERT_TRUE(omni_kb_delete(&e));
    TEST_ASSERT_EQUAL_STRING("hell", e.buf);
    TEST_ASSERT_EQUAL_UINT8(0, e.buf[4]);
    omni_kb_wipe(&e);
}

void test_kb_delete_never_rereveals(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_PASSWORD);
    type(&e, "secretpw");   /* all typed at t 1000 */
    uint32_t t = 1010;
    for (int k = 0; k < 6; k++, t += 85) {
        TEST_ASSERT_TRUE(omni_kb_delete(&e));
        for (uint8_t i = 0; i < e.len; i++) TEST_ASSERT_FALSE(omni_kb_shows(&e, i, t));
    }
    omni_kb_append(&e, 'x', t);
    TEST_ASSERT_TRUE(omni_kb_shows(&e, e.len - 1, t + 899));
    TEST_ASSERT_FALSE(omni_kb_shows(&e, e.len - 1, t + 900));
    TEST_ASSERT_FALSE(omni_kb_shows(&e, 0, t));
    /* a delete at the same instant as the typing, and at t 0 */
    omni_kb_open(&e, KB_MODE_PASSWORD);
    omni_kb_append(&e, 'a', 0);
    omni_kb_append(&e, 'b', 0);
    omni_kb_delete(&e);
    TEST_ASSERT_FALSE(omni_kb_shows(&e, 0, 0));
    TEST_ASSERT_FALSE(omni_kb_shows(&e, 0, 5));
    e.reveal = true;
    TEST_ASSERT_TRUE(omni_kb_shows(&e, 0, 5));
    omni_kb_wipe(&e);
}

void test_kb_append_printable_only(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_SSID);
    const char bad[] = {0, 1, 0x1F, 0x7F, (char)0x80, (char)0xFF};
    for (unsigned i = 0; i < sizeof bad; i++) TEST_ASSERT_EQUAL_INT(KB_AT_CAP, omni_kb_append(&e, bad[i], 5));
    TEST_ASSERT_EQUAL_UINT8(0, e.len);
    TEST_ASSERT_EQUAL_UINT8(0, e.buf[0]);
    TEST_ASSERT_EQUAL_INT(KB_TYPED, omni_kb_append(&e, ' ', 5));
    TEST_ASSERT_EQUAL_INT(KB_TYPED, omni_kb_append(&e, '~', 5));
    TEST_ASSERT_EQUAL_STRING(" ~", e.buf);
}

void test_kb_caps(void)
{
    omni_kb_edit_t e;
    static const struct { omni_kb_mode_t m; int cap; } c[] = {{KB_MODE_PASSWORD, 63}, {KB_MODE_SSID, 32}, {KB_MODE_CITY, 15}};
    for (unsigned k = 0; k < 3; k++) {
        omni_kb_open(&e, c[k].m);
        for (int i = 0; i < c[k].cap; i++) TEST_ASSERT_EQUAL_INT(KB_TYPED, omni_kb_append(&e, 'z', 0));
        TEST_ASSERT_EQUAL_UINT8(c[k].cap, e.len);
        char keep[65];
        memcpy(keep, e.buf, sizeof keep);
        TEST_ASSERT_EQUAL_INT(KB_AT_CAP, omni_kb_append(&e, 'q', 0));
        TEST_ASSERT_EQUAL_UINT8(c[k].cap, e.len);
        TEST_ASSERT_EQUAL_MEMORY(keep, e.buf, sizeof keep);
        TEST_ASSERT_EQUAL_UINT8(c[k].cap, omni_kb_cap(&e));
    }
}

void test_kb_cap_hex64(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_PASSWORD);
    for (int i = 0; i < 63; i++) omni_kb_append(&e, "0123456789abcdefABCDEF"[i % 22], 0);
    TEST_ASSERT_EQUAL_UINT8(64, omni_kb_cap(&e));
    TEST_ASSERT_EQUAL_INT(KB_TYPED, omni_kb_append(&e, 'a', 0));
    TEST_ASSERT_EQUAL_UINT8(64, e.len);
    TEST_ASSERT_EQUAL_UINT8(0, e.buf[64]);
    TEST_ASSERT_EQUAL_INT(KB_AT_CAP, omni_kb_append(&e, 'a', 0));

    omni_kb_open(&e, KB_MODE_PASSWORD);
    for (int i = 0; i < 63; i++) omni_kb_append(&e, 'f', 0);
    TEST_ASSERT_EQUAL_INT(KB_AT_CAP, omni_kb_append(&e, 'g', 0));
    TEST_ASSERT_EQUAL_UINT8(63, e.len);

    omni_kb_open(&e, KB_MODE_PASSWORD);
    for (int i = 0; i < 62; i++) omni_kb_append(&e, '7', 0);
    TEST_ASSERT_EQUAL_INT(KB_TYPED, omni_kb_append(&e, 'z', 0));
    TEST_ASSERT_EQUAL_UINT8(63, e.len);
    TEST_ASSERT_EQUAL_UINT8(63, omni_kb_cap(&e));
    TEST_ASSERT_EQUAL_INT(KB_AT_CAP, omni_kb_append(&e, '7', 0));
    omni_kb_wipe(&e);

    /* a non-password mode never gets the hex allowance */
    omni_kb_open(&e, KB_MODE_SSID);
    TEST_ASSERT_EQUAL_UINT8(32, omni_kb_cap(&e));
}

void test_kb_shift_once_lock_off(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_SSID);
    TEST_ASSERT_EQUAL_UINT8(0, omni_kb_set(&e));
    omni_kb_shift_tap(&e, 100);
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_ONCE, e.shift);
    TEST_ASSERT_EQUAL_UINT8(1, omni_kb_set(&e));
    omni_kb_append(&e, 'A', 200);
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_OFF, e.shift);
    TEST_ASSERT_EQUAL_UINT8(0, omni_kb_set(&e));
    omni_kb_shift_tap(&e, 1000);
    omni_kb_shift_tap(&e, 1399);
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_LOCK, e.shift);
    omni_kb_append(&e, 'B', 1500);
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_LOCK, e.shift);
    TEST_ASSERT_EQUAL_UINT8(1, omni_kb_set(&e));
    omni_kb_shift_tap(&e, 5000);
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_OFF, e.shift);
    omni_kb_shift_tap(&e, 6000);
    omni_kb_shift_tap(&e, 6400);
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_OFF, e.shift);
    TEST_ASSERT_EQUAL_UINT8(0, omni_kb_set(&e));
}

void test_kb_shift_symbol_sets(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_PASSWORD);
    omni_kb_123_tap(&e);
    TEST_ASSERT_EQUAL_UINT8(2, omni_kb_set(&e));
    omni_kb_shift_tap(&e, 10);
    TEST_ASSERT_EQUAL_UINT8(3, omni_kb_set(&e));
    omni_kb_shift_tap(&e, 20);
    TEST_ASSERT_EQUAL_UINT8(2, omni_kb_set(&e));
    omni_kb_shift_tap(&e, 30);
    TEST_ASSERT_EQUAL_UINT8(3, omni_kb_set(&e));
    omni_kb_append(&e, '~', 40);
    TEST_ASSERT_EQUAL_UINT8(3, omni_kb_set(&e));
    omni_kb_123_tap(&e);
    TEST_ASSERT_EQUAL_UINT8(0, omni_kb_set(&e));
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_OFF, e.shift);
    /* caps lock is left behind by 123 */
    omni_kb_shift_tap(&e, 100);
    omni_kb_shift_tap(&e, 200);
    TEST_ASSERT_EQUAL_UINT8(KB_SHIFT_LOCK, e.shift);
    omni_kb_123_tap(&e);
    omni_kb_123_tap(&e);
    TEST_ASSERT_EQUAL_UINT8(0, omni_kb_set(&e));
    omni_kb_wipe(&e);
}

void test_kb_pass_rules(void)
{
    char h[65];
    TEST_ASSERT_FALSE(omni_kb_pass_ok("1234567", 7, OMNI_AUTH_PSK));
    TEST_ASSERT_FALSE(omni_kb_pass_ok("1234567", 7, OMNI_AUTH_SAE));
    TEST_ASSERT_TRUE(omni_kb_pass_ok("12345678", 8, OMNI_AUTH_PSK));
    TEST_ASSERT_TRUE(omni_kb_pass_ok("12345678", 8, OMNI_AUTH_SAE));
    TEST_ASSERT_TRUE(omni_kb_pass_ok("", 0, OMNI_AUTH_OPEN));
    TEST_ASSERT_FALSE(omni_kb_pass_ok("12345678", 8, OMNI_AUTH_UNSUPPORTED));
    memset(h, 'a', 63); h[63] = 0;
    TEST_ASSERT_TRUE(omni_kb_pass_ok(h, 63, OMNI_AUTH_PSK));
    memset(h, 'a', 64); h[64] = 0;
    TEST_ASSERT_TRUE(omni_kb_pass_ok(h, 64, OMNI_AUTH_PSK));
    h[10] = 'g';
    TEST_ASSERT_FALSE(omni_kb_pass_ok(h, 64, OMNI_AUTH_PSK));
    TEST_ASSERT_FALSE(omni_kb_pass_ok(h, 65, OMNI_AUTH_PSK));
}

void test_kb_mask(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_PASSWORD);
    omni_kb_append(&e, 'a', 1000);
    omni_kb_append(&e, 'b', 2000);
    TEST_ASSERT_TRUE(omni_kb_shows(&e, 1, 2899));
    TEST_ASSERT_FALSE(omni_kb_shows(&e, 1, 2900));
    TEST_ASSERT_FALSE(omni_kb_shows(&e, 0, 2000));
    e.reveal = true;
    TEST_ASSERT_TRUE(omni_kb_shows(&e, 0, 99999));
    TEST_ASSERT_TRUE(omni_kb_shows(&e, 1, 99999));
    omni_kb_open(&e, KB_MODE_PASSWORD);
    TEST_ASSERT_FALSE(e.reveal);
}

void test_kb_repeats(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, omni_kb_repeats(0));
    TEST_ASSERT_EQUAL_UINT16(0, omni_kb_repeats(449));
    TEST_ASSERT_EQUAL_UINT16(1, omni_kb_repeats(450));
    TEST_ASSERT_EQUAL_UINT16(1, omni_kb_repeats(534));
    TEST_ASSERT_EQUAL_UINT16(2, omni_kb_repeats(535));
    TEST_ASSERT_EQUAL_UINT16(1 + (65535 - 450) / 85, omni_kb_repeats(65535));
}

void test_kb_idle(void)
{
    TEST_ASSERT_EQUAL_INT(KB_IDLE_AWAKE, omni_kb_idle(0));
    TEST_ASSERT_EQUAL_INT(KB_IDLE_AWAKE, omni_kb_idle(44999));
    TEST_ASSERT_EQUAL_INT(KB_IDLE_DIM, omni_kb_idle(45000));
    TEST_ASSERT_EQUAL_INT(KB_IDLE_DIM, omni_kb_idle(59999));
    TEST_ASSERT_EQUAL_INT(KB_IDLE_CLOSE, omni_kb_idle(60000));
}

void test_kb_wipe_zeroes(void)
{
    omni_kb_edit_t e;
    omni_kb_open(&e, KB_MODE_PASSWORD);
    for (int i = 0; i < 20; i++) omni_kb_append(&e, (char)('a' + i), 100 + i);
    e.reveal = true;
    e.set = 2;
    e.shift = KB_SHIFT_LOCK;
    omni_kb_wipe(&e);
    {   /* the whole struct (padding too) is zero except the mode */
        omni_kb_edit_t z;
        memcpy(&z, &e, sizeof z);
        z.mode = 0;
        const uint8_t *q = (const uint8_t *)&z;
        for (unsigned i = 0; i < sizeof z; i++) TEST_ASSERT_EQUAL_UINT8(0, q[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(0, e.set);
    TEST_ASSERT_EQUAL_UINT8(0, e.shift);
    for (unsigned i = 0; i < sizeof e.buf; i++) TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)e.buf[i]);
    TEST_ASSERT_EQUAL_UINT8(0, e.len);
    TEST_ASSERT_EQUAL_UINT8(KB_MODE_PASSWORD, e.mode);
    TEST_ASSERT_FALSE(e.reveal);
    TEST_ASSERT_EQUAL_UINT32(0, e.t_type);
    TEST_ASSERT_EQUAL_UINT32(0, e.t_shift);
    omni_kb_open(&e, KB_MODE_CITY);
    TEST_ASSERT_EQUAL_UINT8(KB_MODE_CITY, e.mode);
}

void test_omni_wipe(void)
{
    uint8_t b[40];
    memset(b, 0xA5, sizeof b);
    omni_wipe(b + 4, 30);
    TEST_ASSERT_EQUAL_UINT8(0xA5, b[3]);
    TEST_ASSERT_EQUAL_UINT8(0, b[4]);
    TEST_ASSERT_EQUAL_UINT8(0, b[33]);
    TEST_ASSERT_EQUAL_UINT8(0xA5, b[34]);
    omni_wipe(NULL, 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_kb_layout_committed);
    RUN_TEST(test_kb_layout_refused);
    RUN_TEST(test_kb_layout_freq_order);
    RUN_TEST(test_kb_hit_centres);
    RUN_TEST(test_kb_hit_boundaries);
    RUN_TEST(test_kb_cancel_radius);
    RUN_TEST(test_kb_band_and_loupe);
    RUN_TEST(test_kb_stroke_classes);
    RUN_TEST(test_kb_append_delete);
    RUN_TEST(test_kb_delete_never_rereveals);
    RUN_TEST(test_kb_append_printable_only);
    RUN_TEST(test_kb_caps);
    RUN_TEST(test_kb_cap_hex64);
    RUN_TEST(test_kb_shift_once_lock_off);
    RUN_TEST(test_kb_shift_symbol_sets);
    RUN_TEST(test_kb_pass_rules);
    RUN_TEST(test_kb_mask);
    RUN_TEST(test_kb_repeats);
    RUN_TEST(test_kb_idle);
    RUN_TEST(test_kb_wipe_zeroes);
    RUN_TEST(test_omni_wipe);
    return UNITY_END();
}
