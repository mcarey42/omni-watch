/* rd_core's chime picture and plan (rd/chime_env.h; rd wave 1 Task 8, spec section 11). The scheduler itself is
 * test_fk_chime.c's (PR33): here only rd's own part, the envelope the faces breathe with and the plan the service
 * calls (one asset, pings ignored, the service's gain). */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "rd/chime_env.h"

void setUp(void) {}
void tearDown(void) {}

static rd_chime_env_t env_at(uint32_t ms)
{
    rd_chime_env_t e;
    memset(&e, 0x7f, sizeof e);   /* every field must be written */
    rd_chime_env(ms, &e);
    return e;
}

static void test_rd_chime_env_breakpoints(void)
{
    TEST_ASSERT_EQUAL_UINT32(2400, RD_CHIME_MS);
    /* the thrum: (ms/550)^2 up, held, then (1 - ph(1500, 2350))^2 down */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, env_at(0).thrum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, env_at(275).thrum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, env_at(550).thrum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, env_at(1000).thrum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, env_at(1500).thrum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, env_at(1925).thrum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, env_at(2350).thrum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, env_at(2399).thrum);
    /* the bip [750, 860), the bop [950, 1100), the click [2200, 2260) */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, env_at(750).bip);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.36788f, env_at(795).bip);   /* e^-1 one time constant on */
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, env_at(859).bip);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env_at(860).bip);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env_at(749).bip);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, env_at(950).bop);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.36788f, env_at(1010).bop);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env_at(1100).bop);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env_at(949).bop);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, env_at(2200).click);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, env_at(2230).click);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env_at(2260).click);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, env_at(2199).click);
    /* every channel 0 at the end and beyond, never wrapped */
    static const uint32_t past[] = {2400, 2401, 5000, 0x7fffffffu, 0x80000000u, UINT32_MAX};
    for (unsigned k = 0; k < sizeof past / sizeof past[0]; k++) {
        const rd_chime_env_t e = env_at(past[k]);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, e.thrum);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, e.bip);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, e.bop);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, e.click);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, e.beep);
    }
    /* every ms: each channel in 0..1, beep = max(bip, bop), the thrum never rises after its hold */
    float prev = 0.0f;
    for (uint32_t ms = 0; ms <= RD_CHIME_MS + 10; ms++) {
        const rd_chime_env_t e = env_at(ms);
        const float ch[] = {e.thrum, e.bip, e.bop, e.click, e.beep};
        for (unsigned c = 0; c < 5; c++) TEST_ASSERT_TRUE(ch[c] >= 0.0f && ch[c] <= 1.0f);
        TEST_ASSERT_EQUAL_FLOAT(e.bip > e.bop ? e.bip : e.bop, e.beep);
        if (ms <= 550) TEST_ASSERT_TRUE(e.thrum >= prev);
        if (ms >= 1500) TEST_ASSERT_TRUE(e.thrum <= prev);
        prev = e.thrum;
    }
    rd_chime_env(100, NULL);   /* no output: nothing happens */
}

static fk_chime_verdict_t verdict(fk_chime_why_t why, uint8_t pings)
{
    const fk_chime_verdict_t v = {.why = why, .pings = pings, .gain_pct = 100, .next_s = 0};
    return v;
}

static void test_rd_chime_plan(void)
{
    const char *asset = NULL;
    uint8_t gain = 0;
    fk_chime_verdict_t v = verdict(FK_CH_PLAY, 3);
    TEST_ASSERT_TRUE(rd_chime_plan(&v, 100, &asset, &gain));
    TEST_ASSERT_EQUAL_STRING("rd_chime.pcm", asset);
    TEST_ASSERT_EQUAL_UINT8(100, gain);
    TEST_ASSERT_TRUE(rd_chime_plan(&v, 40, &asset, &gain));   /* the quiet window's gain, as the service passes it */
    TEST_ASSERT_EQUAL_STRING("rd_chime.pcm", asset);
    TEST_ASSERT_EQUAL_UINT8(40, gain);
    static const uint8_t pings[] = {0, 1, 12, 40};   /* no hour pings: always the one asset */
    for (unsigned k = 0; k < sizeof pings / sizeof pings[0]; k++) {
        v = verdict(FK_CH_PLAY, pings[k]);
        asset = NULL;
        gain = 0;
        TEST_ASSERT_TRUE(rd_chime_plan(&v, 100, &asset, &gain));
        TEST_ASSERT_EQUAL_STRING("rd_chime.pcm", asset);
        TEST_ASSERT_EQUAL_UINT8(100, gain);
    }
    static const fk_chime_why_t quiet[] = {FK_CH_LATE, FK_CH_UNSET, FK_CH_PROV, FK_CH_OFF, FK_CH_WAIT, FK_CH_SILENT,
                                           FK_CH_ERROR, FK_CH_BOOT};
    for (unsigned k = 0; k < sizeof quiet / sizeof quiet[0]; k++) {
        v = verdict(quiet[k], 3);
        asset = "untouched";
        gain = 7;
        TEST_ASSERT_FALSE(rd_chime_plan(&v, 100, &asset, &gain));
    }
    TEST_ASSERT_FALSE(rd_chime_plan(NULL, 100, &asset, &gain));
    v = verdict(FK_CH_PLAY, 3);
    TEST_ASSERT_FALSE(rd_chime_plan(&v, 100, NULL, &gain));
    TEST_ASSERT_FALSE(rd_chime_plan(&v, 100, &asset, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rd_chime_env_breakpoints);
    RUN_TEST(test_rd_chime_plan);
    return UNITY_END();
}
