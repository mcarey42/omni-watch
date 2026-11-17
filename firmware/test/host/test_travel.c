#include "unity.h"
#include "omni/travel.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Expected numbers come from the mockup's own makeDial model() and startTravel: node tools/omni_travel_oracle.mjs
 * (faces-omni.js 627-700), a travel from 2026-10-22 (home lon -83) to Philadelphia 1776-07-04 (lon -75.17). */

static const struct tm DAY = { .tm_year = 126, .tm_mon = 9, .tm_mday = 22, .tm_hour = 10, .tm_min = 10, .tm_sec = 40 };
#define W 1000u   /* the wake (at rest) and the travel's start */

static omni_model_t g_m;
static omni_dial_state_t g_s;

static void step(uint32_t t) { omni_model_step(&g_m, t, true, &DAY, OMNI_LENS_OFF, &g_s); }

/* at rest at W (lon -83 exactly), the red lens at W */
static void start_philadelphia(bool reduced)
{
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, W, NULL, reduced);
    step(W);
    omni_travel_start(&g_m, W, false);
    TEST_ASSERT_TRUE(omni_travel_active(&g_m));
    TEST_ASSERT_FALSE(omni_travel_away(&g_m));
}

static void check4(const float want[4], const float got[4], float tol)
{
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(tol, want[i], got[i]);
}

static void test_dest_table(void)
{
    TEST_ASSERT_EQUAL_STRING("Philadelphia", OMNI_DEST[0].name);
    TEST_ASSERT_EQUAL_INT(1776, OMNI_DEST[0].y);
    TEST_ASSERT_EQUAL_INT(-44, OMNI_DEST[2].y);
    TEST_ASSERT_EQUAL_STRING("North Atlantic", OMNI_DEST[6].name);
    TEST_ASSERT_EQUAL_INT(1912, OMNI_DEST[6].y);
    TEST_ASSERT_EQUAL_INT(4, OMNI_DEST[6].m);
    TEST_ASSERT_EQUAL_INT(14, OMNI_DEST[6].d);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -49.95f, OMNI_DEST[6].lon);
}

static void test_travel_rings_matches_mockup(void)
{
    static const uint32_t tau[7] = { 100, 300, 1000, 1500, 2100, 2230, 3000 };
    static const float want[7][4] = {
        { 8.9606f, 21.9731f, 26.0104f, 40.0381f },   { 9.0702f, 22.0702f, 26.0057f, 39.9359f },
        { 13.6505f, 9.5565f, 43.3306f, 39.1012f },   { 29.1943f, -79.5565f, 293.2404f, 14.4878f },
        { 29.9830f, -92.1131f, 375.9841f, -4.8990f }, { 29.9969f, -91.9880f, 376.1102f, -4.9972f },
        { 30.0000f, -92.0000f, 376.0001f, -4.9999f },
    };
    /* the speed for the fidelity rule: the mockup's ringBlur over the model's 32 ms step (T12 M1, R105), slots per step:
     * -clamp(total (e(tau) - e(tau - 32)), +-slots / 3) (the oracle's speed32) */
    static const float blur1000[4] = { -0.8883f, 2.6976f, -4.3422f, 0.2666f };
    static const float blur1500[4] = { -0.2904f, 3.2223f, -16.7128f, 3.2232f };
    static const float zero[4] = { 0, 0, 0, 0 };
    start_philadelphia(false);
    for (int k = 0; k < 7; k++) {
        step(W + tau[k]);
        check4(want[k], g_s.val, 1e-3f);
        if (tau[k] == 1000) check4(blur1000, g_s.speed, 2e-4f);
        if (tau[k] == 1500) check4(blur1500, g_s.speed, 2e-4f);
        if (tau[k] == 300 || tau[k] == 3000) check4(zero, g_s.speed, 1e-6f);
        /* the V marks: old at the start's date, new at the destination's */
        TEST_ASSERT_EQUAL_FLOAT(9, g_s.sel_old[0]);
        TEST_ASSERT_EQUAL_FLOAT(76, g_s.sel_new[2]);
    }
    /* reduced motion at tau 1000: no turns, no jitter or detent, done by 900 + 140 i */
    static const float rm1000[4] = { 6.0000f, 4.0033f, 73.4972f, 38.0481f };
    start_philadelphia(true);
    step(W + 1000);
    check4(rm1000, g_s.val, 1e-3f);
}

static void test_travel_lights_matches_mockup(void)
{
    /* (red, green, flash, markOld, markNew) */
    static const uint32_t tau[6] = { 100, 1000, 2100, 2230, 2500, 3000 };
    static const float want[6][5] = {
        { 0.150f, 0.500f, 0, 1, 0 },        { 1, 0, 0, 0, 0 },          { 1, 0, 0.0917f, 0, 0.4667f },
        { 1, 0, 0.9200f, 0, 0.640f },       { 0, 0, 0.2543f, 0, 1 },    { 0, 0.5f, 0.0235f, 0, 1 },
    };
    start_philadelphia(false);
    for (int k = 0; k < 6; k++) {
        step(W + tau[k]);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, want[k][0], g_s.red);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, want[k][1], g_s.green);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, want[k][2], g_s.flash);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, want[k][3], g_s.mark_old);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, want[k][4], g_s.mark_new);
    }
    /* the hands' swing sw = 4 hs (1 - hs): minute -1.5 turns x sw, hour -.25 turns x sw */
    float h0, m0;
    omni_hand_angles(10, 10, 40, &h0, &m0);
    static const uint32_t st[2] = { 1000, 1500 };
    static const float sw[2] = { 0.4981f, 0.7659f };
    start_philadelphia(false);
    for (int k = 0; k < 2; k++) {
        step(W + st[k]);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, sw[k], (m0 - g_s.min_rad) / (1.5f * 6.2831853f));
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, sw[k], (h0 - g_s.hour_rad) / (0.25f * 6.2831853f));
    }
    /* the marker on the destination, fading in over 2700-3000 ms */
    step(W + 2850);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, g_s.marker_a);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 39.95f, g_s.marker_lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -75.17f, g_s.marker_lon);
    /* no flash in reduced motion */
    start_philadelphia(true);
    step(W + 2230);
    TEST_ASSERT_EQUAL_FLOAT(0, g_s.flash);
}

static void test_travel_globe_matches_mockup(void)
{
    static const uint32_t tau[4] = { 100, 1000, 1450, 2650 };
    static const double want[4] = { -83.2200, -257.8254, -799.0850, -1515.1700 };
    start_philadelphia(false);
    for (int k = 0; k < 4; k++) {
        step(W + tau[k]);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)want[k], (float)omni_travel_lon(&g_m, W + tau[k]));
        double wr = fmod(want[k] + 180, 360);
        if (wr < 0) wr += 360;
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(wr - 180), g_s.lon_deg);
        TEST_ASSERT_TRUE(g_s.lon_deg >= -180 && g_s.lon_deg < 180);
    }
    /* reduced: by 1200 ms, no extra turns (and, unlike the mockup, no drift: it is reduced motion's rule) */
    start_philadelphia(true);
    step(W + 100);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -83, (float)omni_travel_lon(&g_m, W + 100));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -435.17f, (float)omni_travel_lon(&g_m, W + 1200));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -75.17f, (float)omni_travel_lon(&g_m, W + 1300));
}

static void test_travel_lands_and_returns(void)
{
    float phil[4], today[4];
    omni_dial_vals(1776, 7, 4, phil);
    omni_dial_vals(2026, 10, 22, today);
    start_philadelphia(false);
    float a;
    step(W + 2999);
    TEST_ASSERT_NULL(omni_travel_caption(&g_m, W + 2999, &a));
    TEST_ASSERT_EQUAL_STRING("PHILADELPHIA", omni_travel_caption(&g_m, W + 3150, &a));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, a);
    step(W + 3300);   /* lands (the mockup at 3300 + its next frame) */
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    TEST_ASSERT_TRUE(omni_travel_away(&g_m));
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(phil[i], g_s.val[i]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -76.6f, g_s.lon_deg);   /* -75.17 less the drift since 2650 ms */
    TEST_ASSERT_EQUAL_FLOAT(1, g_s.green);
    TEST_ASSERT_EQUAL_FLOAT(0, g_s.flash);
    TEST_ASSERT_EQUAL_STRING("PHILADELPHIA", omni_travel_caption(&g_m, W + 3300, &a));
    TEST_ASSERT_EQUAL_FLOAT(1, a);
    /* away: the caption fades over 9000-9600 ms from the arrival (2650 ms: the mockup's awayT) */
    step(W + 2650 + 9300);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -95.63f, g_s.lon_deg);
    TEST_ASSERT_EQUAL_STRING("PHILADELPHIA", omni_travel_caption(&g_m, W + 2650 + 9300, &a));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, a);
    TEST_ASSERT_NULL(omni_travel_caption(&g_m, W + 2650 + 9600, &a));
    /* the trip home starts once more than 12 s have passed since the arrival */
    step(W + 2650 + 12000);
    TEST_ASSERT_TRUE(omni_travel_away(&g_m));
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    const uint32_t h0 = W + 2650 + 12001;
    step(h0);
    TEST_ASSERT_TRUE(omni_travel_active(&g_m));
    TEST_ASSERT_FALSE(omni_travel_away(&g_m));
    TEST_ASSERT_NULL(omni_travel_caption(&g_m, h0 + 3200, &a));   /* no caption on the way home */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -101.57f, (float)omni_travel_lon(&g_m, h0));   /* from the last step's globe */
    step(h0 + 1);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.995f, g_s.green);
    step(h0 + 3300);
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    TEST_ASSERT_FALSE(omni_travel_away(&g_m));
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], g_s.val[i]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * 0.65), g_s.lon_deg);
    TEST_ASSERT_EQUAL_FLOAT(1, g_s.green);
    TEST_ASSERT_EQUAL_FLOAT(0.9f, g_s.marker_a);
    TEST_ASSERT_EQUAL_FLOAT(0, g_s.marker_pulse);
    step(h0 + 3332);   /* and drifts on from there */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)(-83 - 2.2 * 0.682), g_s.lon_deg);
    /* red while away goes home too, and green; green at home does nothing; the next red goes on to Menlo Park */
    omni_travel_start(&g_m, h0 + 4000, true);
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    omni_travel_start(&g_m, h0 + 4000, false);
    step(h0 + 4000 + 3300);
    TEST_ASSERT_TRUE(omni_travel_away(&g_m));
    TEST_ASSERT_EQUAL_STRING("MENLO PARK", omni_travel_caption(&g_m, h0 + 4000 + 3300, &a));
    omni_travel_start(&g_m, h0 + 8000, true);
    TEST_ASSERT_TRUE(omni_travel_active(&g_m));
    /* a start while active is ignored */
    omni_travel_start(&g_m, h0 + 8100, false);
    step(h0 + 8000 + 3300);
    TEST_ASSERT_FALSE(omni_travel_away(&g_m));
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], g_s.val[i]);
}

static void test_travel_sleep_lands_home(void)
{
    float today[4];
    omni_dial_vals(2026, 10, 22, today);
    start_philadelphia(false);
    step(W + 1500);
    omni_travel_abort_home(&g_m);
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    TEST_ASSERT_FALSE(omni_travel_away(&g_m));
    step(W + 1532);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], g_s.val[i]);
    TEST_ASSERT_EQUAL_FLOAT(1, g_s.green);   /* the entry ended at the travel's start: green lit at rest */
    TEST_ASSERT_EQUAL_FLOAT(0, g_s.flash);
    TEST_ASSERT_EQUAL_FLOAT(0, g_s.red);
    TEST_ASSERT_EQUAL_FLOAT(0, g_s.marker_pulse);
    float a;
    TEST_ASSERT_NULL(omni_travel_caption(&g_m, W + 1532, &a));
    /* away, then the sleep: home too; the next wake is at home */
    start_philadelphia(false);
    step(W + 3300);
    TEST_ASSERT_TRUE(omni_travel_away(&g_m));
    omni_travel_abort_home(&g_m);
    omni_model_wake(&g_m, W + 9000, NULL, false);
    step(W + 9000);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(today[i], g_s.val[i]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -83, g_s.lon_deg);
    TEST_ASSERT_NULL(omni_travel_caption(&g_m, W + 9000, &a));
}

static void test_travel_many_bounded(void)
{
    /* 500 travels in a row (red each time it lands: away, then home, ...) in 33 ms steps: every ring within 4 turns of
     * its date's value (where it is going), the lon wrapped, the destinations cycling */
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, W, &OMNI_ENTRY_DIAL, false);
    uint32_t t = W;
    for (; t < W + 4000; t += 33) step(t);
    int dests = 0;
    for (int k = 0; k < 500; k++) {
        const bool home = omni_travel_away(&g_m);
        omni_travel_start(&g_m, t, home);
        TEST_ASSERT_TRUE(omni_travel_active(&g_m));
        float to[4];
        if (home) omni_dial_vals(2026, 10, 22, to);
        else {
            const omni_dest_t *d = &OMNI_DEST[dests++ % 7];
            omni_dial_vals(d->y, d->m, d->d, to);
        }
        const uint32_t t0 = t;
        for (; (int32_t)(t - t0) <= 3400; t += 33) {
            step(t);
            for (int i = 0; i < 4; i++) {
                const float n = (float)OMNI_RING_GEOM[i].slots;
                if (fabsf(g_s.val[i] - to[i]) > 4 * n) TEST_FAIL_MESSAGE("ring outside 4 turns of its date");
            }
            if (!(g_s.lon_deg >= -180 && g_s.lon_deg < 180)) TEST_FAIL_MESSAGE("lon out of [-180, 180)");
        }
        TEST_ASSERT_FALSE(omni_travel_active(&g_m));
        TEST_ASSERT_EQUAL(!home, omni_travel_away(&g_m));
        for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(to[i], g_s.val[i]);
    }
}

static void test_travel_unset_disabled(void)
{
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, W, NULL, false);
    omni_model_step(&g_m, W, false, NULL, OMNI_LENS_UNSET, &g_s);
    omni_travel_start(&g_m, W, false);
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    /* before any step since the wake: no clock known yet */
    omni_model_wake(&g_m, W, NULL, false);
    omni_travel_start(&g_m, W, false);
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    /* the clock going unset mid-travel goes home */
    start_philadelphia(false);
    step(W + 1000);
    omni_model_step(&g_m, W + 1033, false, NULL, OMNI_LENS_UNSET, &g_s);
    TEST_ASSERT_FALSE(omni_travel_active(&g_m));
    TEST_ASSERT_FALSE(omni_travel_away(&g_m));
    TEST_ASSERT_EQUAL_FLOAT(0, g_s.flash);
    /* T12 M3: unset while away: home, the globe drifting on from where it was shown (no jump) */
    start_philadelphia(false);
    step(W + 5000);
    TEST_ASSERT_TRUE(omni_travel_away(&g_m));
    const float lon = g_s.lon_deg;
    omni_model_step(&g_m, W + 5032, false, NULL, OMNI_LENS_UNSET, &g_s);
    TEST_ASSERT_FALSE(omni_travel_away(&g_m));
    TEST_ASSERT_NULL(omni_travel_dest_name(&g_m));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, lon, g_s.lon_deg);
    omni_model_step(&g_m, W + 6032, false, NULL, OMNI_LENS_UNSET, &g_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, lon - 2.2f, g_s.lon_deg);
}

/* T12 M5, M6: a new model is home (dest -1: no destination name); the accessors */
static void test_travel_accessors(void)
{
    omni_model_init(&g_m, 40, -83);
    TEST_ASSERT_EQUAL_INT(-1, g_m.dest);
    TEST_ASSERT_NULL(omni_travel_dest_name(&g_m));
    TEST_ASSERT_FALSE(omni_travel_to_home(&g_m));
    start_philadelphia(false);
    TEST_ASSERT_EQUAL_STRING("Philadelphia", omni_travel_dest_name(&g_m));
    TEST_ASSERT_FALSE(omni_travel_to_home(&g_m));
    step(W + 3300);
    TEST_ASSERT_EQUAL_STRING("Philadelphia", omni_travel_dest_name(&g_m));   /* away */
    omni_travel_start(&g_m, W + 3400, true);
    TEST_ASSERT_TRUE(omni_travel_to_home(&g_m));
    TEST_ASSERT_NULL(omni_travel_dest_name(&g_m));
}

/* T5 m6: a travel started mid-settle (the DIAL entry, 300 ms in) starts from the displayed rings and globe, and the
 * trip home right after an arrival starts from the arrival's: no jump in either */
static void test_travel_from_displayed(void)
{
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, W, &OMNI_ENTRY_DIAL, false);
    step(W + 300);
    float shown[4];
    memcpy(shown, g_s.val, sizeof shown);
    const float lon0 = g_s.lon_deg;
    float today[4];
    omni_dial_vals(2026, 10, 22, today);
    TEST_ASSERT_TRUE(fabsf(shown[2] - today[2]) > 0.1f);   /* settling */
    omni_travel_start(&g_m, W + 300, false);
    step(W + 316);
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(0.01f, shown[i], g_s.val[i]);   /* the jitter's first 16 ms */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, lon0, g_s.lon_deg);
    for (uint32_t t = W + 348; t < W + 300 + 3300; t += 32) step(t);
    step(W + 300 + 3300);
    TEST_ASSERT_TRUE(omni_travel_away(&g_m));
    memcpy(shown, g_s.val, sizeof shown);
    const float lon1 = g_s.lon_deg;
    const uint32_t h = W + 300 + 3300;
    omni_travel_start(&g_m, h, true);
    step(h + 16);
    for (int i = 0; i < 4; i++) TEST_ASSERT_FLOAT_WITHIN(0.01f, shown[i], g_s.val[i]);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, lon1, g_s.lon_deg);
}

/* T5 m6: a second date change inside the first's settle eases from the displayed rings, not the last target */
static void test_change_in_settle_from_displayed(void)
{
    struct tm a = DAY, b = DAY, c = DAY;
    a.tm_mday = 30;
    b.tm_mday = 31;
    c.tm_mon = 10;
    c.tm_mday = 1;
    omni_model_init(&g_m, 40, -83);
    omni_model_wake(&g_m, 0, NULL, false);
    omni_model_step(&g_m, 0, true, &a, OMNI_LENS_OFF, &g_s);
    omni_model_step(&g_m, 1000, true, &b, OMNI_LENS_OFF, &g_s);   /* 30 -> 31 */
    omni_model_step(&g_m, 1300, true, &b, OMNI_LENS_OFF, &g_s);   /* mid-settle */
    const float shown = g_s.val[1];
    TEST_ASSERT_TRUE(shown > 30.05f && shown < 30.95f);
    omni_model_step(&g_m, 1332, true, &c, OMNI_LENS_OFF, &g_s);   /* 31 -> 1 starts from here */
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, shown, g_s.val[1]);
    omni_model_step(&g_m, 1332 + 1100, true, &c, OMNI_LENS_OFF, &g_s);
    TEST_ASSERT_EQUAL_FLOAT(1, g_s.val[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dest_table);
    RUN_TEST(test_travel_rings_matches_mockup);
    RUN_TEST(test_travel_lights_matches_mockup);
    RUN_TEST(test_travel_globe_matches_mockup);
    RUN_TEST(test_travel_lands_and_returns);
    RUN_TEST(test_travel_sleep_lands_home);
    RUN_TEST(test_travel_many_bounded);
    RUN_TEST(test_travel_unset_disabled);
    RUN_TEST(test_travel_accessors);
    RUN_TEST(test_travel_from_displayed);
    RUN_TEST(test_change_in_settle_from_displayed);
    return UNITY_END();
}
