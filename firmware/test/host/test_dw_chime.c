/* dw_core's chime plan and envelope (dw/chime.h; dw wave 1 Task 7; preflight D7). The scheduler's cases moved to
 * test_fk_chime.c with the scheduler (rd wave 1 Task 1). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "dw/assets_meta.h"
#include "dw/chime.h"
#include "dw_assets_host.h"

static uint8_t *g_pcm;   /* the synthetic PCM of the running test, freed in tearDown on every path */

void setUp(void) {}
void tearDown(void)
{
    free(g_pcm);
    g_pcm = NULL;
}

static void test_chime_plan_and_length(void)
{
    dw_chime_step_t s[13];
    TEST_ASSERT_EQUAL_UINT8(13, dw_chime_plan(12, 3440, s));
    TEST_ASSERT_EQUAL_UINT8(0, s[0].what);
    TEST_ASSERT_EQUAL_UINT16(0, s[0].at_ms);
    TEST_ASSERT_EQUAL_UINT8(1, s[1].what);
    TEST_ASSERT_EQUAL_UINT16(3440, s[1].at_ms);
    TEST_ASSERT_EQUAL_UINT16(3440 + 11 * 360, s[12].at_ms);
    TEST_ASSERT_EQUAL_UINT16(7400, s[12].at_ms);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(DW_CHIME_MAX_MS, s[12].at_ms + DW_CHIME_PING_MAX_MS);   /* the last ping's end */
    TEST_ASSERT_EQUAL_UINT32(DW_CHIME_MAX_MS, s[12].at_ms + DW_CHIME_PING_MAX_MS);
    TEST_ASSERT_EQUAL_UINT8(1, dw_chime_plan(0, 3440, s));
    TEST_ASSERT_EQUAL_UINT8(13, dw_chime_plan(40, 3440, s));   /* clamped to 12 pings */
    TEST_ASSERT_EQUAL_UINT8(0, dw_chime_plan(3, 3440, NULL));
    /* a longer chime is clamped by the code: 3 500 ms and uint16 extremes never pass the 7.6 s bound or wrap at_ms */
    static const uint16_t big[] = { 3441, 3500, 5000, 20000, 65000, 65535 };
    for (unsigned k = 0; k < sizeof big / sizeof big[0]; k++) {
        TEST_ASSERT_EQUAL_UINT8(13, dw_chime_plan(12, big[k], s));
        TEST_ASSERT_EQUAL_UINT16(DW_CHIME_CHIME_MAX_MS, s[1].at_ms);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(DW_CHIME_MAX_MS, s[12].at_ms + DW_CHIME_PING_MAX_MS);
        for (int i = 2; i <= 12; i++) TEST_ASSERT_GREATER_THAN_UINT16(s[i - 1].at_ms, s[i].at_ms);   /* no wrap */
        TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(DW_CHIME_MAX_MS + 500, 12, big[k]));
    }
}

/* the plan at the length of the shipped chime (the asset's sample count at 16 kHz), not a hard-coded one */
static void test_chime_plan_asset_length(void)
{
    const uint16_t ms = (uint16_t)((uint32_t)ASSET_DW_CHIME_SAMPLES * 1000u / 16000u);
    dw_chime_step_t s[13];
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(3000, ms);
    TEST_ASSERT_EQUAL_UINT8(13, dw_chime_plan(12, ms, s));
    TEST_ASSERT_EQUAL_UINT16(ms, s[1].at_ms);
    TEST_ASSERT_EQUAL_UINT16(ms + 11 * DW_CHIME_PING_GAP_MS, s[12].at_ms);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(DW_CHIME_MAX_MS, s[12].at_ms + DW_CHIME_PING_MAX_MS);
}

/* s16le mono 16 kHz PCM of `ms` milliseconds, sample i = fn(i / 16 samples per ms in ms) * 20000 */
static uint8_t *synth(unsigned ms, float (*fn)(float t_ms), size_t *bytes)
{
    const size_t n = (size_t)ms * 16;
    free(g_pcm);
    uint8_t *p = g_pcm = malloc(2 * n);
    for (size_t i = 0; i < n; i++) {
        const float t = (float)i / 16.0f;
        const int16_t v = (int16_t)(20000.0f * fn(t) * (i & 1 ? 1.0f : -1.0f));   /* a square wave: rms = peak */
        p[2 * i] = (uint8_t)(v & 0xFF);
        p[2 * i + 1] = (uint8_t)((uint16_t)v >> 8);
    }
    *bytes = 2 * n;
    return p;
}

static float ramp_slow(float t) { return t / 5000.0f; }              /* 0 -> 1 over 5 s */
static float ramp_up(float t) { return t / 2000.0f; }                    /* 0 -> 1 over 2 s */
static float burst(float t) { return t < 100.0f ? 1.0f : expf(-(t - 100.0f) / 300.0f); }   /* loud, then decaying */

static void test_chime_env_follows_ramp(void)
{
    size_t bytes;
    uint8_t *pcm = synth(2000, ramp_up, &bytes);
    dw_chime_env_bind(pcm, bytes);
    float prev = -1.0f;
    for (uint32_t t = 0; t < 1960; t += 20) {   /* rises monotonically */
        const float e = dw_chime_env(t, 0, 2000);
        TEST_ASSERT_TRUE(e >= 0.0f && e <= 1.0f);
        TEST_ASSERT_TRUE(e >= prev);
        prev = e;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, dw_chime_env(1000, 0, 2000));
    TEST_ASSERT_FLOAT_WITHIN(0.04f, 0.25f, dw_chime_env(500, 0, 2000));
    TEST_ASSERT_GREATER_THAN_FLOAT(0.9f, dw_chime_env(1980, 0, 2000));
}

static void test_chime_env_follows_burst(void)
{
    size_t bytes;
    uint8_t *pcm = synth(1500, burst, &bytes);
    dw_chime_env_bind(pcm, bytes);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, dw_chime_env(40, 0, 1500));
    TEST_ASSERT_FLOAT_WITHIN(0.06f, 0.37f, dw_chime_env(400, 0, 1500));   /* e^-1 at 300 ms after the burst */
    TEST_ASSERT_LESS_THAN_FLOAT(dw_chime_env(400, 0, 1500), dw_chime_env(800, 0, 1500));
    TEST_ASSERT_LESS_THAN_FLOAT(0.05f, dw_chime_env(1400, 0, 1500));
}

static void test_chime_env_pings_and_end(void)
{
    size_t bytes;
    uint8_t *pcm = synth(1500, burst, &bytes);
    dw_chime_env_bind(pcm, bytes);
    const uint8_t pings = 3;
    const uint16_t cms = 1500;
    /* a glint on each ping, a low glow between the chime and the last ping */
    for (int i = 0; i < pings; i++) {
        uint32_t p = (uint32_t)cms + (uint32_t)i * DW_CHIME_PING_GAP_MS;
        TEST_ASSERT_GREATER_THAN_FLOAT(dw_chime_env(p - 20, pings, cms) + 0.2f, dw_chime_env(p, pings, cms));
    }
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.2f, dw_chime_env(cms + DW_CHIME_PING_MAX_MS + 20, pings, cms));
    /* the fade and the end: zero after the last ping's end plus 0.5 s */
    uint32_t end = cms + 2u * DW_CHIME_PING_GAP_MS + DW_CHIME_PING_MAX_MS + 500u;
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, dw_chime_env(end - 10, pings, cms));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(end, pings, cms));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(end + 5000, pings, cms));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(UINT32_MAX, 12, cms));
    /* 12 pings still end by the bound plus the fade */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(DW_CHIME_MAX_MS + 500, 12, 3440));
}

static void test_chime_env_unbound_and_clamped(void)
{
    dw_chime_env_bind(NULL, 0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(100, 0, 1000));   /* an empty table: no chime part */
    size_t bytes;
    uint8_t *pcm = synth(5000, ramp_slow, &bytes);   /* longer than the slot: bound clamps to the first 3 440 ms */
    dw_chime_env_bind(pcm, bytes);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(DW_CHIME_MAX_MS + 500, 12, 65535));
    TEST_ASSERT_TRUE(dw_chime_env(3400, 0, 3440) > 0.0f);
}

static float step_down(float t) { return t < 40.0f ? 1.0f : 0.0f; }   /* windows 0 and 1 loud, then silent */
static float loud(float t) { (void)t; return 1.0f; }

static void test_chime_env_interpolates(void)
{
    size_t bytes;
    const uint8_t *pcm = synth(1000, step_down, &bytes);
    dw_chime_env_bind(pcm, bytes);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, dw_chime_env(20, 0, 1000));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, dw_chime_env(30, 0, 1000));    /* between windows 1 (255) and 2 (0) */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, dw_chime_env(35, 0, 1000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(40, 0, 1000));
}

static void test_chime_env_falls_after_last_window(void)
{
    size_t bytes;
    const uint8_t *pcm = synth(100, loud, &bytes);                       /* five windows */
    dw_chime_env_bind(pcm, bytes);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, dw_chime_env(80, 0, 200));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, dw_chime_env(90, 0, 200));     /* the last window falls toward 0 */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(100, 0, 200));            /* past the table, inside chime_ms */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(150, 0, 200));
}

static void test_chime_env_clamp_exact(void)
{
    size_t bytes;
    const uint8_t *pcm = synth(5000, ramp_slow, &bytes);   /* normalised over the first 3 440 ms only */
    dw_chime_env_bind(pcm, bytes);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dw_chime_env(3420, 0, 3440));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.2f, dw_chime_env(3440, 0, 3440));   /* the hold after the table, not 0 or 1 */
}

static void test_chime_env_odd_and_short(void)
{
    size_t bytes;
    const uint8_t *pcm = synth(40, loud, &bytes);          /* 640 B: two windows */
    dw_chime_env_bind(pcm, 641);                           /* an odd count floors to 320 samples: one window */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dw_chime_env(0, 0, 40));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(20, 0, 40));
    dw_chime_env_bind(pcm, 200);                           /* 100 samples: less than one window, still one entry */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, dw_chime_env(0, 0, 40));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(20, 0, 40));
    dw_chime_env_bind(pcm, 1);                             /* less than one sample: empty */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dw_chime_env(0, 0, 40));
}

/* the committed chime: loud within its first 0.3 s, near zero at its end */
static void test_chime_env_committed(void)
{
    TEST_ASSERT_TRUE(dw_assets_host_open(DW_ASSETS_DIR, NULL));
    size_t size = 0;
    const uint8_t *pcm = dw_assets_host_get("dw_chime.pcm", &size);
    TEST_ASSERT_NOT_NULL(pcm);
    dw_chime_env_bind(pcm, size);
    const uint16_t ms = (uint16_t)(size / 2 / 16);
    float peak = 0.0f;
    for (uint32_t t = 0; t <= 300; t += 10) {
        const float e = dw_chime_env(t, 0, ms);
        if (e > peak) peak = e;
    }
    TEST_ASSERT_GREATER_THAN_FLOAT(0.9f, peak);
    TEST_ASSERT_LESS_THAN_FLOAT(0.05f, dw_chime_env((uint32_t)ms - 20, 0, ms));
    dw_assets_host_close();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chime_plan_and_length);
    RUN_TEST(test_chime_plan_asset_length);
    RUN_TEST(test_chime_env_follows_ramp);
    RUN_TEST(test_chime_env_follows_burst);
    RUN_TEST(test_chime_env_pings_and_end);
    RUN_TEST(test_chime_env_unbound_and_clamped);
    RUN_TEST(test_chime_env_interpolates);
    RUN_TEST(test_chime_env_falls_after_last_window);
    RUN_TEST(test_chime_env_clamp_exact);
    RUN_TEST(test_chime_env_odd_and_short);
    RUN_TEST(test_chime_env_committed);
    return UNITY_END();
}
