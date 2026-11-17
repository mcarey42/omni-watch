#include "unity.h"
#include "omni/timer.h"
#include <math.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define TAU 6.28318530717958647692f

/* the state a wind must restore: everything but the scratch (prev, prev_min) */
static void same_state(const omni_tmr_t *a, const omni_tmr_t *b)
{
    TEST_ASSERT_EQUAL_INT(a->mode, b->mode);
    TEST_ASSERT_EQUAL_UINT8(a->set_min, b->set_min);
    TEST_ASSERT_EQUAL_UINT32(a->end_ms, b->end_ms);
    TEST_ASSERT_EQUAL_UINT32(a->remain_ms, b->remain_ms);
    TEST_ASSERT_EQUAL_UINT32(a->sw_start_ms, b->sw_start_ms);
    TEST_ASSERT_EQUAL_UINT32(a->done_ms, b->done_ms);
    TEST_ASSERT_EQUAL_UINT64(a->sw_acc_ms, b->sw_acc_ms);
    TEST_ASSERT_EQUAL(a->alarm_pending, b->alarm_pending);
}

static omni_tmr_view_t view(const omni_tmr_t *t, uint32_t now)
{
    omni_tmr_view_t v;
    omni_tmr_view(t, now, &v);
    return v;
}

/* R144: the rim is a relative jog. The value being wound, in whole minutes (SET), or set_min outside a wind. */
static int wound_min(const omni_tmr_t *t)
{
    return t->mode == OMNI_TMR_SET ? (int)(omni_tmr_set_ms(t) / 60000u) : t->set_min;
}

/* Wind to set_min m and end (arming), from IDLE or ARMED. */
static void wind_to(omni_tmr_t *t, int m)
{
    omni_tmr_wind_start(t);
    omni_tmr_wind_by(t, (float)(m - wound_min(t)));
    omni_tmr_wind_end(t);
}

void test_tmr_wind_relative(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    omni_tmr_wind_start(&t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SET, t.mode);
    TEST_ASSERT_EQUAL_UINT32(0u, omni_tmr_set_ms(&t));   /* from the current value, never from an angle */
    omni_tmr_wind_by(&t, 15.0f);
    TEST_ASSERT_EQUAL_UINT8(15, t.set_min);
    omni_tmr_wind_by(&t, 0.4f);                           /* a step needs a whole minute of travel (12 degrees) */
    TEST_ASSERT_EQUAL_UINT8(15, t.set_min);
    omni_tmr_wind_by(&t, 0.5f);
    TEST_ASSERT_EQUAL_UINT8(15, t.set_min);
    omni_tmr_wind_by(&t, 0.1f);
    TEST_ASSERT_EQUAL_UINT8(16, t.set_min);
    omni_tmr_wind_by(&t, -0.9f);                          /* a detent: back needs a whole minute too */
    TEST_ASSERT_EQUAL_UINT8(16, t.set_min);
    omni_tmr_wind_by(&t, -0.1f);
    TEST_ASSERT_EQUAL_UINT8(15, t.set_min);
    omni_tmr_wind_by(&t, 44.0f);
    TEST_ASSERT_EQUAL_UINT8(59, t.set_min);
    omni_tmr_wind_by(&t, 10.0f);                          /* clamped at 60 */
    TEST_ASSERT_EQUAL_UINT8(60, t.set_min);
    TEST_ASSERT_EQUAL_UINT32(3600000u, omni_tmr_set_ms(&t));
    omni_tmr_wind_by(&t, -0.6f);                          /* the overshoot is not banked: one minute back from 60 */
    TEST_ASSERT_EQUAL_UINT8(60, t.set_min);
    omni_tmr_wind_by(&t, -0.4f);
    TEST_ASSERT_EQUAL_UINT8(59, t.set_min);
    omni_tmr_wind_by(&t, -100.0f);                        /* clamped at 0 */
    TEST_ASSERT_EQUAL_UINT8(0, t.set_min);
    omni_tmr_wind_by(&t, 0.6f);
    TEST_ASSERT_EQUAL_UINT8(0, t.set_min);
    omni_tmr_wind_by(&t, 0.4f);
    TEST_ASSERT_EQUAL_UINT8(1, t.set_min);
    /* sign-symmetric: -0.99 and +0.99 both stay; a wobble round a step never toggles it */
    for (int i = 0; i < 50; i++) {
        omni_tmr_wind_by(&t, 0.05f);
        omni_tmr_wind_by(&t, -0.05f);
    }
    TEST_ASSERT_EQUAL_UINT8(1, t.set_min);
    /* outside a wind, nothing moves */
    omni_tmr_wind_end(&t);
    omni_tmr_wind_by(&t, 5.0f);
    TEST_ASSERT_EQUAL_UINT8(1, t.set_min);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, t.mode);
}

void test_tmr_wind_release_arms(void)
{
    omni_tmr_t t;
    uint32_t end;
    omni_tmr_init(&t);
    wind_to(&t, 25);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, t.mode);
    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
    omni_tmr_view_t v = view(&t, 500);
    TEST_ASSERT_EQUAL_STRING("25:00", v.big);
    TEST_ASSERT_EQUAL_STRING("SET \xC2\xB7 TAP TO START", v.label);
    TEST_ASSERT_EQUAL_STRING("TAP \xC2\xB7 START     HOLD \xC2\xB7 RESET", v.hint);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, v.sector_min);
    omni_tmr_tap(&t, 1000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_COUNT, t.mode);
    TEST_ASSERT_EQUAL_UINT32(1500000u, omni_tmr_remaining_ms(&t, 1000));
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(1501000u, end);
}

void test_tmr_set_view_while_winding(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, 7.0f);
    omni_tmr_view_t v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("7:00", v.big);
    TEST_ASSERT_EQUAL_STRING("SET \xC2\xB7 TAP TO START", v.label);
    TEST_ASSERT_EQUAL_STRING("", v.hint);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, v.sector_min);
    TEST_ASSERT_FALSE(v.sector_red);
    TEST_ASSERT_EQUAL_UINT8(191, v.alpha);
}

/* R135 kept: a wind never starts or stops a countdown. COUNT ignores it (R144: the jog sets, it never edits a running
 * countdown); IDLE/ARMED end ARMED, PAUSED ends PAUSED; a wind to 0 ends IDLE. */
void test_tmr_rim_drag_never_starts(void)
{
    static const float d1[] = { 0.0f, 3.0f, -3.0f, 45.0f, -45.0f };
    static const float d2[] = { 0.0f, 0.7f, -0.7f, 100.0f, -100.0f };
    const omni_tmr_mode_t from[] = { OMNI_TMR_IDLE, OMNI_TMR_ARMED, OMNI_TMR_COUNT, OMNI_TMR_PAUSED };
    for (int f = 0; f < 4; f++)
        for (int rep = 0; rep < 2; rep++)
            for (int a = 0; a < 5; a++)
                for (int b = 0; b < 5; b++) {
                    omni_tmr_t t, before;
                    uint32_t end;
                    omni_tmr_init(&t);
                    if (from[f] != OMNI_TMR_IDLE) wind_to(&t, 20);
                    if (from[f] == OMNI_TMR_COUNT || from[f] == OMNI_TMR_PAUSED) {
                        omni_tmr_tap(&t, 100);
                        if (from[f] == OMNI_TMR_PAUSED) omni_tmr_tap(&t, 200);
                    }
                    TEST_ASSERT_EQUAL_INT(from[f], t.mode);
                    before = t;
                    if (rep) omni_tmr_wind_start(&t);   /* a second start without an end */
                    omni_tmr_wind_start(&t);
                    omni_tmr_wind_by(&t, d1[a]);
                    omni_tmr_wind_by(&t, d2[b]);
                    if (from[f] == OMNI_TMR_COUNT) {
                        same_state(&before, &t);
                        omni_tmr_wind_end(&t);
                        same_state(&before, &t);
                        continue;
                    }
                    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SET, t.mode);
                    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
                    const uint32_t ms = omni_tmr_set_ms(&t);
                    omni_tmr_wind_end(&t);
                    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
                    if (ms == 0) TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, t.mode);
                    else if (from[f] == OMNI_TMR_PAUSED) {
                        TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, t.mode);
                        TEST_ASSERT_EQUAL_UINT32(ms, omni_tmr_remaining_ms(&t, 999999));
                    } else {
                        TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, t.mode);
                        TEST_ASSERT_EQUAL_UINT32(ms, t.set_min * 60000u);
                    }
                }
}

void test_tmr_wind_cancel(void)
{
    omni_tmr_t t, before;
    uint32_t end;
    /* from each mode, a wind that never ends comes back exactly */
    omni_tmr_init(&t);
    before = t;
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, 33.0f);
    omni_tmr_wind_cancel(&t);
    same_state(&before, &t);

    wind_to(&t, 20);
    before = t;
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, 13.0f);
    omni_tmr_wind_cancel(&t);
    same_state(&before, &t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, t.mode);
    TEST_ASSERT_EQUAL_UINT8(20, t.set_min);

    omni_tmr_tap(&t, 1000);   /* COUNT: the wind is refused, so the cancel has nothing to undo */
    before = t;
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, 13.0f);
    omni_tmr_wind_cancel(&t);
    same_state(&before, &t);
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(1000u + 20u * 60000u, end);

    omni_tmr_tap(&t, 5000);
    before = t;
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, -7.0f);
    omni_tmr_wind_start(&t);   /* repeated start: still the first prev */
    omni_tmr_wind_by(&t, 2.0f);
    omni_tmr_wind_cancel(&t);
    same_state(&before, &t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, t.mode);

    /* a cancel outside a wind does nothing */
    omni_tmr_wind_cancel(&t);
    same_state(&before, &t);
}

/* PAUSED: the jog adjusts the remaining time in whole minutes, the seconds kept; it stays PAUSED (a tap resumes). */
void test_tmr_wind_paused_and_zero(void)
{
    omni_tmr_t t;
    uint32_t end;
    omni_tmr_init(&t);
    wind_to(&t, 10);
    omni_tmr_tap(&t, 1000);
    omni_tmr_tap(&t, 38000);   /* paused at 9:23 */
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, t.mode);
    TEST_ASSERT_EQUAL_UINT32(563000u, omni_tmr_remaining_ms(&t, 0));
    omni_tmr_wind_start(&t);
    TEST_ASSERT_EQUAL_UINT32(563000u, omni_tmr_set_ms(&t));
    TEST_ASSERT_EQUAL_STRING("9:23", view(&t, 0).big);
    omni_tmr_wind_by(&t, 2.0f);
    TEST_ASSERT_EQUAL_STRING("11:23", view(&t, 0).big);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 683000.0f / 60000.0f, view(&t, 0).sector_min);
    omni_tmr_wind_end(&t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, t.mode);
    TEST_ASSERT_EQUAL_UINT32(683000u, omni_tmr_remaining_ms(&t, 0));
    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
    omni_tmr_tap(&t, 50000);   /* resumes with the new time */
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(50000u + 683000u, end);

    /* paused near the top: clamped at 60:00 exactly; under a minute left, down to 0 */
    omni_tmr_tap(&t, 60000);
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, 55.0f);
    TEST_ASSERT_EQUAL_UINT32(3600000u, omni_tmr_set_ms(&t));
    omni_tmr_wind_by(&t, -59.6f);   /* 59 whole minutes down: 1:00 */
    TEST_ASSERT_EQUAL_UINT32(60000u, omni_tmr_set_ms(&t));
    omni_tmr_wind_by(&t, -1.0f);
    TEST_ASSERT_EQUAL_UINT32(0u, omni_tmr_set_ms(&t));
    omni_tmr_wind_end(&t);   /* a paused countdown wound to 0 is cleared, never left at 0:00 */
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, t.mode);
    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));

    /* a paused part-second reads the same before and during the wind (seconds rounded up, as PAUSED shows them) */
    wind_to(&t, 1);
    omni_tmr_tap(&t, 0);
    omni_tmr_tap(&t, 400);   /* 59.6 s left: "1:00" */
    TEST_ASSERT_EQUAL_STRING("1:00", view(&t, 400).big);
    omni_tmr_wind_start(&t);
    TEST_ASSERT_EQUAL_STRING("1:00", view(&t, 400).big);
    omni_tmr_wind_cancel(&t);
    omni_tmr_reset(&t);

    /* an armed timer wound to 0 is cancelled */
    wind_to(&t, 5);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, t.mode);
    wind_to(&t, 0);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, t.mode);
    TEST_ASSERT_EQUAL_UINT8(0, t.set_min);
}

void test_tmr_wind_ignored_in_sw_and_done(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    omni_tmr_tap(&t, 0);
    omni_tmr_wind_start(&t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SW, t.mode);
    omni_tmr_tap(&t, 10);
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, 5.0f);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SWPAUSE, t.mode);
    omni_tmr_wind_end(&t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SWPAUSE, t.mode);
    omni_tmr_start_seconds(&t, 0, 1);
    TEST_ASSERT_TRUE(omni_tmr_tick(&t, 1000));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
    omni_tmr_wind_start(&t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
}
void test_tmr_count_text(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    t.mode = OMNI_TMR_COUNT;
    t.end_ms = 61000;
    omni_tmr_view_t v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("1:01", v.big);
    TEST_ASSERT_EQUAL_UINT8(0, v.ink);
    TEST_ASSERT_EQUAL_STRING("COUNTDOWN", v.label);
    TEST_ASSERT_EQUAL_STRING("TAP \xC2\xB7 PAUSE     HOLD \xC2\xB7 RESET", v.hint);
    TEST_ASSERT_FALSE(v.sector_red);
    TEST_ASSERT_EQUAL_UINT8(191, v.alpha);
    t.end_ms = 59001;
    v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("1:00", v.big);
    TEST_ASSERT_EQUAL_UINT8(0, v.ink);
    TEST_ASSERT_TRUE(v.sector_red);
    t.end_ms = 59000;
    v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("0:59", v.big);
    TEST_ASSERT_EQUAL_UINT8(2, v.ink);
    TEST_ASSERT_TRUE(v.sector_red);
    TEST_ASSERT_EQUAL_FLOAT(59000.0f / 60000.0f, v.sector_min);
}

void test_tmr_done_once(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    wind_to(&t, 1);
    omni_tmr_tap(&t, 5000);
    const uint32_t end = 5000 + 60000;
    TEST_ASSERT_FALSE(omni_tmr_tick(&t, end - 1));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_COUNT, t.mode);
    TEST_ASSERT_TRUE(omni_tmr_tick(&t, end));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
    TEST_ASSERT_EQUAL_UINT32(end, t.done_ms);
    TEST_ASSERT_FALSE(omni_tmr_tick(&t, end + 5));
    omni_tmr_view_t v = view(&t, end + 5);
    TEST_ASSERT_EQUAL_STRING("0:00", v.big);
    TEST_ASSERT_EQUAL_UINT8(3, v.ink);
    TEST_ASSERT_EQUAL_STRING("TIME", v.label);
    TEST_ASSERT_EQUAL_STRING("TAP TO DISMISS", v.hint);
    TEST_ASSERT_TRUE(v.sector_red);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, v.sector_min);
    const float want = 0.45f + 0.4f * sinf((float)(5u % 1005u) / 160.0f);
    TEST_ASSERT_INT_WITHIN(1, (int)(want * 255.0f + 0.5f), v.alpha);
}

void test_tmr_pause_resume(void)
{
    omni_tmr_t t;
    uint32_t end;
    omni_tmr_init(&t);
    wind_to(&t, 10);
    omni_tmr_tap(&t, 0);
    omni_tmr_tap(&t, 60000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, t.mode);
    TEST_ASSERT_EQUAL_UINT32(540000u, omni_tmr_remaining_ms(&t, 60000));
    TEST_ASSERT_EQUAL_UINT32(540000u, omni_tmr_remaining_ms(&t, 90000));   /* frozen */
    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
    omni_tmr_view_t v = view(&t, 100000);
    TEST_ASSERT_EQUAL_STRING("PAUSED", v.label);
    TEST_ASSERT_EQUAL_STRING("9:00", v.big);
    const float want = 0.45f + 0.15f * sinf((float)(100000u % 1885u) / 300.0f);
    TEST_ASSERT_INT_WITHIN(1, (int)(want * 255.0f + 0.5f), v.alpha);
    omni_tmr_tap(&t, 100000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_COUNT, t.mode);
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(640000u, end);
}

void test_tmr_stopwatch(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    omni_tmr_tap(&t, 0);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SW, t.mode);
    omni_tmr_view_t v = view(&t, 12345);
    TEST_ASSERT_EQUAL_STRING("0:12", v.big);
    TEST_ASSERT_EQUAL_STRING(".34", v.small);
    TEST_ASSERT_EQUAL_STRING("STOPWATCH", v.label);
    TEST_ASSERT_EQUAL_FLOAT(0.20575f, v.bead);
    TEST_ASSERT_EQUAL_UINT8(0, v.ink);
    omni_tmr_tap(&t, 12345);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SWPAUSE, t.mode);
    TEST_ASSERT_EQUAL_UINT32(12345u, omni_tmr_elapsed_ms(&t, 20000));
    v = view(&t, 20000);
    TEST_ASSERT_EQUAL_STRING("PAUSED", v.label);
    TEST_ASSERT_EQUAL_STRING("0:12", v.big);
    omni_tmr_tap(&t, 30000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SW, t.mode);
    TEST_ASSERT_EQUAL_UINT32(12345u + 1000u, omni_tmr_elapsed_ms(&t, 31000));

    omni_tmr_init(&t);
    t.mode = OMNI_TMR_SWPAUSE;
    t.sw_acc_ms = 3600000u;
    v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("1:00:00", v.big);
    TEST_ASSERT_EQUAL_STRING("", v.small);
    t.sw_acc_ms = 3600000u + 61000u + 999u;
    v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("1:01:01", v.big);
    t.sw_acc_ms = 3599999u;
    v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("59:59", v.big);
    TEST_ASSERT_EQUAL_STRING(".99", v.small);
}

void test_tmr_wrap(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    const uint32_t t0 = 0xFFFFF000u;
    wind_to(&t, 2);
    omni_tmr_tap(&t, t0);
    TEST_ASSERT_EQUAL_UINT32(120000u, omni_tmr_remaining_ms(&t, t0));
    TEST_ASSERT_EQUAL_UINT32(1u, omni_tmr_remaining_ms(&t, t0 + 119999u));
    TEST_ASSERT_FALSE(omni_tmr_tick(&t, t0 + 119999u));
    TEST_ASSERT_TRUE(omni_tmr_tick(&t, t0 + 120000u));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
    TEST_ASSERT_EQUAL_UINT32(t0 + 120000u, t.done_ms);

    omni_tmr_init(&t);
    omni_tmr_tap(&t, 0xFFFFFF00u);   /* stopwatch across the wrap */
    TEST_ASSERT_EQUAL_UINT32(0x200u, omni_tmr_elapsed_ms(&t, 0x100u));
}

void test_tmr_monotonic(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    wind_to(&t, 3);
    omni_tmr_tap(&t, 100);
    omni_tmr_view_t a = view(&t, 5000), b;
    omni_tmr_t copy = t;
    for (int i = 0; i < 5; i++) {
        b = view(&t, 5000);
        TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
    }
    TEST_ASSERT_EQUAL_MEMORY(&copy, &t, sizeof t);   /* viewing never changes the state */
}

void test_tmr_claims(void)
{
    /* R144: COUNT no longer claims the rim (the jog never edits a running countdown): its rim drags are swipes */
    const omni_tmr_mode_t yes[] = { OMNI_TMR_IDLE, OMNI_TMR_ARMED, OMNI_TMR_PAUSED, OMNI_TMR_SET };
    const omni_tmr_mode_t no[] = { OMNI_TMR_SW, OMNI_TMR_SWPAUSE, OMNI_TMR_DONE, OMNI_TMR_COUNT };
    omni_tmr_t t;
    omni_tmr_init(&t);
    for (int i = 0; i < 4; i++) {
        t.mode = yes[i];
        TEST_ASSERT_TRUE(omni_tmr_claims(&t, 233, 30));
    }
    for (int i = 0; i < 4; i++) {
        t.mode = no[i];
        TEST_ASSERT_FALSE(omni_tmr_claims(&t, 233, 30));
    }
    t.mode = OMNI_TMR_IDLE;
    TEST_ASSERT_FALSE(omni_tmr_claims(&t, 233, 233 - 139));
    TEST_ASSERT_TRUE(omni_tmr_claims(&t, 233, 233 - 140));
    TEST_ASSERT_FALSE(omni_tmr_claims(&t, 233, 233));
    TEST_ASSERT_TRUE(omni_tmr_claims(&t, 233 + 140, 233));
    TEST_ASSERT_TRUE(omni_tmr_claims(&t, 233 + 100, 233 + 100));   /* r 141.4 */
}

void test_tmr_holds(void)
{
    const omni_tmr_mode_t yes[] = { OMNI_TMR_ARMED, OMNI_TMR_COUNT, OMNI_TMR_PAUSED, OMNI_TMR_SW, OMNI_TMR_SWPAUSE };
    omni_tmr_t t;
    omni_tmr_init(&t);
    for (int i = 0; i < 5; i++) {
        t.mode = yes[i];
        TEST_ASSERT_TRUE(omni_tmr_holds(&t, 233, 233));
        TEST_ASSERT_FALSE(omni_tmr_holds(&t, 233, 233 - 150));
    }
    t.mode = OMNI_TMR_IDLE;
    TEST_ASSERT_FALSE(omni_tmr_holds(&t, 233, 233));
    t.mode = OMNI_TMR_DONE;
    TEST_ASSERT_FALSE(omni_tmr_holds(&t, 233, 233));
}

void test_tmr_rim_angle(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, omni_tmr_rim_angle(233, 30));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, TAU / 4, omni_tmr_rim_angle(433, 233));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, TAU / 2, omni_tmr_rim_angle(233, 433));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3 * TAU / 4, omni_tmr_rim_angle(33, 233));
    const float a = omni_tmr_rim_angle(232, 30);
    TEST_ASSERT_TRUE(a > 6.0f && a < TAU);
}

void test_tmr_tap_dismiss(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    wind_to(&t, 1);
    omni_tmr_tap(&t, 0);
    omni_tmr_tick(&t, 60000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
    omni_tmr_tap(&t, 70000);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, t.mode);
    TEST_ASSERT_EQUAL_UINT8(0, t.set_min);
    omni_tmr_view_t v = view(&t, 70000);
    TEST_ASSERT_EQUAL_STRING("0:00", v.big);
    TEST_ASSERT_EQUAL_UINT8(1, v.ink);
    TEST_ASSERT_EQUAL_STRING("TIMER", v.label);
    TEST_ASSERT_EQUAL_STRING("TAP \xC2\xB7 STOPWATCH     WIND RIM \xC2\xB7 TIMER", v.hint);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, v.bead);
}

void test_tmr_alarm_at(void)
{
    omni_tmr_t t;
    uint32_t end = 0;
    omni_tmr_init(&t);
    wind_to(&t, 5);
    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
    omni_tmr_tap(&t, 1000);
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(301000u, end);
    omni_tmr_tap(&t, 2000);
    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
    omni_tmr_reset(&t);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_IDLE, t.mode);
    omni_tmr_tap(&t, 0);
    TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
}

void test_tmr_reset_and_console(void)
{
    omni_tmr_t t;
    uint32_t end;
    omni_tmr_init(&t);
    omni_tmr_start_seconds(&t, 1000, 90);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_COUNT, t.mode);
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(91000u, end);
    omni_tmr_view_t v = view(&t, 1000);
    TEST_ASSERT_EQUAL_STRING("1:30", v.big);
    omni_tmr_reset(&t);
    omni_tmr_t z;
    memset(&z, 0, sizeof z);
    TEST_ASSERT_EQUAL_MEMORY(&z, &t, sizeof t);
}

void test_tmr_tap_at_end_is_done(void)
{
    omni_tmr_t t;
    uint32_t end;
    omni_tmr_init(&t);
    wind_to(&t, 1);
    omni_tmr_tap(&t, 0);
    omni_tmr_tap(&t, 59999);   /* one ms early: a pause */
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_PAUSED, t.mode);
    omni_tmr_tap(&t, 60000);   /* resume: end = 120 s */
    for (int d = 0; d < 3; d++) {
        omni_tmr_init(&t);
        wind_to(&t, 1);
        omni_tmr_tap(&t, 0);
        omni_tmr_tap(&t, 60000u + (uint32_t)d * 700u);   /* at the end, and after it, before any tick */
        TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
        TEST_ASSERT_EQUAL_UINT32(60000u, t.done_ms);
        TEST_ASSERT_FALSE(omni_tmr_alarm_at(&t, &end));
        TEST_ASSERT_TRUE(omni_tmr_tick(&t, 61000));    /* the alarm is still reported, once */
        TEST_ASSERT_FALSE(omni_tmr_tick(&t, 62000));
        TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
    }
    /* across the 32-bit wrap */
    omni_tmr_init(&t);
    wind_to(&t, 1);
    omni_tmr_tap(&t, 0xFFFFFFF0u);
    omni_tmr_tap(&t, 0xFFFFFFF0u + 60000u);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
    TEST_ASSERT_TRUE(omni_tmr_tick(&t, 0xFFFFFFF0u + 60000u));
}

void test_tmr_pulse_phase_late_uptime(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    t.mode = OMNI_TMR_PAUSED;
    t.remain_ms = 5000;
    /* the same phase 100 periods and 40 days apart (whole 1885 ms multiples): the same alpha, none off by float */
    const uint32_t base = 12345u;
    omni_tmr_view_t a = view(&t, base), b = view(&t, base + 1885u * 1000u), c = view(&t, base + 1885u * 1832000u);
    TEST_ASSERT_EQUAL_UINT8(a.alpha, b.alpha);
    TEST_ASSERT_EQUAL_UINT8(a.alpha, c.alpha);
    /* it still moves ms by ms at 40 days of uptime */
    uint8_t lo = 255, hi = 0;
    for (uint32_t k = 0; k < 1885; k += 5) {
        const uint8_t al = view(&t, 3456000000u + k).alpha;
        if (al < lo) lo = al;
        if (al > hi) hi = al;
    }
    TEST_ASSERT_TRUE(hi - lo > 60);
    omni_tmr_init(&t);
    t.mode = OMNI_TMR_DONE;
    t.done_ms = 100;
    TEST_ASSERT_EQUAL_UINT8(view(&t, 100 + 1005u * 7).alpha, view(&t, 100).alpha);
}

void test_tmr_stopwatch_100h(void)
{
    omni_tmr_t t;
    omni_tmr_init(&t);
    omni_tmr_tap(&t, 0);
    /* running 100 h and a bit, ticked every 10 days (the 32-bit span folds in) */
    uint64_t total = 0;
    uint32_t now = 0;
    for (int i = 0; i < 4; i++) {
        now += 864000000u;
        total += 864000000u;
        omni_tmr_tick(&t, now);
    }
    TEST_ASSERT_EQUAL_UINT64(total, omni_tmr_elapsed_ms(&t, now));   /* 40 days: a 32-bit ms count would have wrapped at 49.7 */
    omni_tmr_init(&t);
    t.mode = OMNI_TMR_SWPAUSE;
    t.sw_acc_ms = 100ull * 3600000ull + 61000ull + 450ull;
    omni_tmr_view_t v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("100:01:01", v.big);
    TEST_ASSERT_EQUAL_STRING("", v.small);
    TEST_ASSERT_EQUAL_FLOAT(1450.0f / 60000.0f, v.bead);
    /* beyond the 32-bit ms range */
    t.sw_acc_ms = 6000000000ull;
    v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("1666:40:00", v.big);
    omni_tmr_tap(&t, 10);
    omni_tmr_tap(&t, 20);
    TEST_ASSERT_EQUAL_UINT64(6000000010ull, omni_tmr_elapsed_ms(&t, 20));   /* resumed at 10, paused at 20 */
}

void test_tmr_start_seconds_edges(void)
{
    omni_tmr_t t;
    uint32_t end;
    omni_tmr_init(&t);
    omni_tmr_start_seconds(&t, 5000, 0);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_COUNT, t.mode);
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(5000u, end);
    TEST_ASSERT_TRUE(omni_tmr_tick(&t, 5000));
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_DONE, t.mode);
    omni_tmr_start_seconds(&t, 5000, 100000);   /* capped at 1 h */
    TEST_ASSERT_TRUE(omni_tmr_alarm_at(&t, &end));
    TEST_ASSERT_EQUAL_UINT32(5000u + 3600000u, end);
    TEST_ASSERT_EQUAL_UINT8(60, t.set_min);
    omni_tmr_start_seconds(&t, 0, 3600);
    TEST_ASSERT_EQUAL_UINT8(60, t.set_min);
    omni_tmr_start_seconds(&t, 0, 61);
    TEST_ASSERT_EQUAL_UINT8(2, t.set_min);
    omni_tmr_view_t v = view(&t, 0);
    TEST_ASSERT_EQUAL_STRING("1:01", v.big);
}

void test_tmr_set_mode_edges(void)
{
    omni_tmr_t t, before;
    omni_tmr_init(&t);
    omni_tmr_wind_start(&t);
    omni_tmr_wind_by(&t, 12.0f);
    before = t;
    omni_tmr_tap(&t, 777);   /* a finger is on the rim: the centre tap is ignored */
    TEST_ASSERT_EQUAL_MEMORY(&before, &t, sizeof t);
    TEST_ASSERT_TRUE(omni_tmr_holds(&t, 233, 233));
    TEST_ASSERT_FALSE(omni_tmr_holds(&t, 233, 233 - 150));
    TEST_ASSERT_TRUE(omni_tmr_claims(&t, 233, 30));
}

void test_tmr_tick_now_behind(void)
{
    omni_tmr_t t, before;
    omni_tmr_init(&t);
    wind_to(&t, 1);
    omni_tmr_tap(&t, 100000);   /* end 160000 */
    before = t;
    TEST_ASSERT_FALSE(omni_tmr_tick(&t, 99999));        /* the clock behind the start */
    TEST_ASSERT_FALSE(omni_tmr_tick(&t, 0));
    TEST_ASSERT_FALSE(omni_tmr_tick(&t, 0xFFFFFF00u));   /* a "negative" now by signed difference */
    TEST_ASSERT_EQUAL_MEMORY(&before, &t, sizeof t);
    TEST_ASSERT_EQUAL_UINT32(60000u, omni_tmr_remaining_ms(&t, 100000));
    TEST_ASSERT_EQUAL_UINT32(60000u + 1u, omni_tmr_remaining_ms(&t, 99999));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tmr_wind_relative);
    RUN_TEST(test_tmr_wind_release_arms);
    RUN_TEST(test_tmr_set_view_while_winding);
    RUN_TEST(test_tmr_rim_drag_never_starts);
    RUN_TEST(test_tmr_wind_cancel);
    RUN_TEST(test_tmr_wind_paused_and_zero);
    RUN_TEST(test_tmr_wind_ignored_in_sw_and_done);
    RUN_TEST(test_tmr_count_text);
    RUN_TEST(test_tmr_done_once);
    RUN_TEST(test_tmr_pause_resume);
    RUN_TEST(test_tmr_stopwatch);
    RUN_TEST(test_tmr_wrap);
    RUN_TEST(test_tmr_monotonic);
    RUN_TEST(test_tmr_claims);
    RUN_TEST(test_tmr_holds);
    RUN_TEST(test_tmr_rim_angle);
    RUN_TEST(test_tmr_tap_dismiss);
    RUN_TEST(test_tmr_alarm_at);
    RUN_TEST(test_tmr_reset_and_console);
    RUN_TEST(test_tmr_tap_at_end_is_done);
    RUN_TEST(test_tmr_pulse_phase_late_uptime);
    RUN_TEST(test_tmr_stopwatch_100h);
    RUN_TEST(test_tmr_start_seconds_edges);
    RUN_TEST(test_tmr_set_mode_edges);
    RUN_TEST(test_tmr_tick_now_behind);
    return UNITY_END();
}
