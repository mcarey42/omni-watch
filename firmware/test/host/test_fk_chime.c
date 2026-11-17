/* face_kit's chime scheduler (fk/chime.h; dw wave 1 Task 7; preflight B3/B4/D7; moved from test_dw_chime.c in rd wave 1
 * Task 1, a split: the scheduler's cases, unchanged but for the names). Each case sets TZ and calls tzset().
 * Times are UTC epoch seconds built by utc(); the local hour is asserted through localtime_r where it matters. */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "fk/chime.h"

#define EST "EST5EDT,M3.2.0,M11.1.0"

static void tz(const char *s) { setenv("TZ", s, 1); tzset(); }
void setUp(void) { tz(EST); }
void tearDown(void) {}

/* days from civil (Howard Hinnant) */
static int64_t utc(int y, int m, int d, int h, int mi, int s)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

static int local_hour(int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    return tm.tm_hour;
}

static fk_chime_ctx_t ctx(void)
{
    fk_chime_ctx_t c = { true, true, true, false, 30, 22, 7, false };
    return c;
}

/* 2026-07-01 is EDT (UTC-4): local 14:00 is 18:00Z */
#define H14 utc(2026, 7, 1, 18, 0, 0)

static void test_chime_on_the_hour(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    fk_chime_verdict_t v = fk_chime_due(&st, H14, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(2, v.pings);
    TEST_ASSERT_EQUAL_UINT8(100, v.gain_pct);
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);
    v = fk_chime_due(&st, H14 + 1, &c);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, v.why);
    TEST_ASSERT_EQUAL_INT64(H14 + 3600, v.next_s);
}

static void test_chime_once_per_hour_step_back(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14 + 5, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 - 2, &c).why);   /* stepped back to 13:59:58 */
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14, &c).why);       /* 14:00:00 again: no second play */
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 + 4, &c).why);
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14 + 3600, &c).why);   /* the next hour still plays */
}

static void test_chime_step_forward_late(void)
{
    fk_chime_state_t st = { H14 - 3600 };            /* 13:00 handled */
    fk_chime_ctx_t c = ctx();
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 - 10, &c).why);
    fk_chime_verdict_t v = fk_chime_due(&st, H14 + 45, &c);
    TEST_ASSERT_EQUAL(FK_CH_LATE, v.why);
    TEST_ASSERT_EQUAL_UINT8(0, v.pings);
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);                                /* the hour is marked */
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 + 46, &c).why);
}

static void test_chime_step_forward_within_grace(void)
{
    fk_chime_state_t st = { H14 - 3600 };
    fk_chime_ctx_t c = ctx();
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 - 10, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14 + 20, &c).why);
    /* the grace is [b, b + 30): 29 s plays, 30 s is late */
    fk_chime_state_t a = { 0 }, b = { 0 };
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&a, H14 + 29, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_LATE, fk_chime_due(&b, H14 + 30, &c).why);
}

static void test_chime_dst_spring(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    int64_t t = utc(2026, 3, 8, 6, 59, 59);          /* 01:59:59 EST */
    TEST_ASSERT_EQUAL_INT(1, local_hour(t));
    int64_t nb = fk_chime_next(t);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 8, 7, 0, 0), nb);   /* 03:00 EDT: there is no 02:00 */
    TEST_ASSERT_EQUAL_INT(3, local_hour(nb));
    fk_chime_verdict_t v = fk_chime_due(&st, nb, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(3, v.pings);
    TEST_ASSERT_EQUAL_INT64(nb, fk_chime_boundary(nb + 1799));
    TEST_ASSERT_EQUAL_INT64(nb + 3600, v.next_s);
}

static void test_chime_dst_fall(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    int64_t edt = utc(2026, 11, 1, 5, 0, 0);          /* 01:00 EDT */
    int64_t est = utc(2026, 11, 1, 6, 0, 0);          /* 01:00 EST: the second 01:00 */
    TEST_ASSERT_EQUAL_INT(1, local_hour(edt));
    TEST_ASSERT_EQUAL_INT(1, local_hour(est));
    TEST_ASSERT_EQUAL_INT64(est, fk_chime_next(edt + 3599));
    TEST_ASSERT_EQUAL_INT64(edt, fk_chime_boundary(edt + 3599));
    TEST_ASSERT_EQUAL_INT64(est, fk_chime_boundary(est + 3599));
    fk_chime_verdict_t v = fk_chime_due(&st, edt, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(1, v.pings);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, edt + 1800, &c).why);
    v = fk_chime_due(&st, est, &c);                  /* the second real hour */
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(1, v.pings);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, est + 1, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, est + 1800, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, est + 3600, &c).why);   /* 02:00 EST */
    TEST_ASSERT_EQUAL_INT(2, local_hour(est + 3600));
}

static void test_chime_half_hour_zone(void)
{
    tz("IST-5:30");
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    int64_t t = utc(2026, 7, 1, 8, 45, 10);          /* 14:15:10 IST */
    int64_t b = fk_chime_boundary(t);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 7, 1, 8, 30, 0), b);   /* local :00 is UTC :30 */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 7, 1, 9, 30, 0), fk_chime_next(t));
    fk_chime_verdict_t v = fk_chime_due(&st, b + 2, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(2, v.pings);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, utc(2026, 7, 1, 9, 0, 0), &c).why);   /* UTC :00 is not an hour */
}

static void test_chime_southern_zones(void)
{
    /* Sydney: DST starts 2026-10-04 02:00 AEST (16:00Z on the 3rd) -> 03:00 AEDT, ends 2026-04-05 03:00 AEDT. */
    tz("AEST-10AEDT,M10.1.0,M4.1.0/3");
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    int64_t t = utc(2026, 10, 3, 15, 59, 59);        /* 01:59:59 AEST */
    int64_t nb = fk_chime_next(t);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 3, 16, 0, 0), nb);
    TEST_ASSERT_EQUAL_INT(3, local_hour(nb));
    fk_chime_verdict_t v = fk_chime_due(&st, nb, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(3, v.pings);
    TEST_ASSERT_EQUAL_UINT8(40, v.gain_pct);          /* 03:00 is in 22-07 */
    /* fall back: 02:00 AEDT (15:00Z on the 4th of April) and 02:00 AEST (16:00Z) both chime once */
    int64_t a = utc(2026, 4, 4, 15, 0, 0), b = utc(2026, 4, 4, 16, 0, 0);
    TEST_ASSERT_EQUAL_INT(2, local_hour(a));
    TEST_ASSERT_EQUAL_INT(2, local_hour(b));
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, a, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, a + 3599, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, b, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, b + 1, &c).why);

    /* Adelaide, UTC+9:30 with DST: the half-hour zone with DST */
    tz("ACST-9:30ACDT,M10.1.0,M4.1.0/3");
    int64_t w = utc(2026, 7, 1, 4, 30, 5);           /* 14:00:05 ACST */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 7, 1, 4, 30, 0), fk_chime_boundary(w));
    v = fk_chime_due(&st, w, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(2, v.pings);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 7, 1, 5, 30, 0), v.next_s);
    /* spring forward 02:00 ACST (16:30Z on the 3rd of October) -> 03:00 ACDT */
    int64_t s = utc(2026, 10, 3, 16, 29, 59);
    TEST_ASSERT_EQUAL_INT(1, local_hour(s));
    nb = fk_chime_next(s);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 3, 16, 30, 0), nb);
    TEST_ASSERT_EQUAL_INT(3, local_hour(nb));
    TEST_ASSERT_EQUAL_UINT8(3, fk_chime_due(&st, nb, &c).pings);
    /* summer: 14:00 ACDT is 03:30Z */
    TEST_ASSERT_EQUAL_INT64(utc(2026, 12, 1, 3, 30, 0), fk_chime_boundary(utc(2026, 12, 1, 3, 59, 0)));
}

static void test_chime_clock_corrected_backward(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    int64_t wrong = H14 + 3 * 86400;                 /* a wrong clock three days ahead */
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, wrong, &c).why);
    /* corrected backward by days: the mark is more than 2 h ahead, so it resets and the real hour plays */
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14, &c).why);
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 + 1, &c).why);
    /* exactly 2 h ahead is still a step back, not a reset */
    fk_chime_state_t s2 = { H14 + 7200 };
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&s2, H14, &c).why);
    TEST_ASSERT_EQUAL_INT64(H14 + 7200, s2.last_b);
    fk_chime_state_t s3 = { H14 + 7201 };
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&s3, H14, &c).why);
}

static void test_chime_step_back_under_an_hour_waits(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14 + 2, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 - 1800, &c).why);   /* 13:30: last_b is 30 min ahead */
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);
}

/* H7: `dw chime at +N` is the service shifting now_s by N minutes. Changing or clearing the offset is a clock
 * discontinuity of up to 59 min, which the scheduler cannot tell from a step back: the service zeroes st then. */
static void test_chime_offset_h7(void)
{
    fk_chime_state_t st = { H14 - 3600 };            /* 13:00 handled */
    fk_chime_ctx_t c = ctx();
    int64_t real = H14 - 600;                        /* 13:50:00 real */
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, real + 9 * 60, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, real + 10 * 60, &c).why);   /* at +10: 14:00 virtual */
    /* cleared, then a second at +10 20 s later in the same real hour: with st kept it is muted (documented) ... */
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, real + 20 + 10 * 60, &c).why);
    /* ... and with st zeroed on the offset change, as the service does, it plays again */
    memset(&st, 0, sizeof st);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, real + 20 + 10 * 60, &c).why);
}

static void test_chime_unset(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    c.time_set = false;
    fk_chime_verdict_t v = fk_chime_due(&st, H14, &c);
    TEST_ASSERT_EQUAL(FK_CH_UNSET, v.why);
    TEST_ASSERT_EQUAL_INT64(0, st.last_b);
    c.time_set = true;
    v = fk_chime_due(&st, 12345, &c);                /* 1970: not a real time */
    TEST_ASSERT_EQUAL(FK_CH_UNSET, v.why);
    TEST_ASSERT_EQUAL_INT64(0, st.last_b);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14, &c).why);   /* set at 13:59:59, plays at 14:00:00 */
}

static void test_chime_silent_marks(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    c.sound_on = false;
    TEST_ASSERT_EQUAL(FK_CH_SILENT, fk_chime_due(&st, H14, &c).why);
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);
    c.sound_on = true;
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 + 10, &c).why);   /* not a late play */
    c.volume = 0;
    TEST_ASSERT_EQUAL(FK_CH_SILENT, fk_chime_due(&st, H14 + 3600, &c).why);
}

/* the final review, M2: at boot the sound mirror is read before sfx_config has set it (off, volume 0). A tick then
 * takes no decision: FK_CH_BOOT marks nothing and looks again in a second, so the hour still plays once the mirror is
 * set, within the grace */
static void test_chime_sound_pending_marks_nothing(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    c.sound_on = false, c.volume = 0, c.sound_pending = true;   /* sfx's default mirror before sfx_config */
    fk_chime_verdict_t v = fk_chime_due(&st, H14 + 5, &c);
    TEST_ASSERT_EQUAL(FK_CH_BOOT, v.why);
    TEST_ASSERT_EQUAL_INT64(0, st.last_b);
    TEST_ASSERT_EQUAL_INT64(H14 + 6, v.next_s);
    TEST_ASSERT_EQUAL_UINT32(1000, fk_chime_arm_ms(H14 + 5, &v, true));
    c = ctx();   /* the mirror set: the hour plays */
    v = fk_chime_due(&st, H14 + 6, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);
    /* the service's path: a pending tick arms a second, then the real tick plays */
    fk_chime_sched_t s;
    memset(&s, 0, sizeof s);
    c.sound_on = false, c.volume = 0, c.sound_pending = true;
    TEST_ASSERT_EQUAL_UINT32(1000, fk_chime_sched_tick(&s, H14 + 29, &c));
    TEST_ASSERT_EQUAL(FK_CH_BOOT, s.last.why);
    c = ctx();
    fk_chime_sched_tick(&s, H14 + 29, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, s.last.why);
    /* off and unset are still decided while pending (they mark nothing either) */
    memset(&s, 0, sizeof s);
    c.sound_pending = true, c.chime_on = false;
    TEST_ASSERT_EQUAL_UINT32(0, fk_chime_sched_tick(&s, H14, &c));
    TEST_ASSERT_EQUAL(FK_CH_OFF, s.last.why);
}

static void test_chime_provisioning(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    c.provisioning = true;
    TEST_ASSERT_EQUAL(FK_CH_PROV, fk_chime_due(&st, H14, &c).why);
    TEST_ASSERT_EQUAL_INT64(H14, st.last_b);
    c.provisioning = false;
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 + 5, &c).why);
}

static void test_chime_off(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    c.chime_on = false;
    fk_chime_verdict_t v = fk_chime_due(&st, H14, &c);
    TEST_ASSERT_EQUAL(FK_CH_OFF, v.why);
    TEST_ASSERT_EQUAL_INT64(0, v.next_s);
    TEST_ASSERT_EQUAL_INT64(0, st.last_b);
}

static void test_chime_quiet_window(void)
{
    fk_chime_ctx_t c = ctx();
    fk_chime_state_t st = { 0 };
    /* 2026-07-01 EDT: local hour h is (h + 4) Z */
    TEST_ASSERT_EQUAL_UINT8(40, fk_chime_due(&st, utc(2026, 7, 1, 2, 0, 0), &c).gain_pct);    /* 22:00 */
    st.last_b = 0;
    TEST_ASSERT_EQUAL_UINT8(40, fk_chime_due(&st, utc(2026, 7, 1, 10, 0, 0), &c).gain_pct);   /* 06:00 */
    st.last_b = 0;
    TEST_ASSERT_EQUAL_UINT8(100, fk_chime_due(&st, utc(2026, 7, 1, 11, 0, 0), &c).gain_pct);  /* 07:00 */
    st.last_b = 0;
    TEST_ASSERT_EQUAL_UINT8(100, fk_chime_due(&st, utc(2026, 7, 1, 1, 0, 0), &c).gain_pct);   /* 21:00 */
    TEST_ASSERT_EQUAL_UINT8(12, (uint8_t)(30 * FK_CHIME_QUIET_PCT / 100));
    for (int h = 0; h < 24; h++) TEST_ASSERT_FALSE(fk_chime_quiet(h, 5, 5));
    TEST_ASSERT_FALSE(fk_chime_quiet(2, 3, 5));
    TEST_ASSERT_TRUE(fk_chime_quiet(3, 3, 5));
    TEST_ASSERT_TRUE(fk_chime_quiet(4, 3, 5));
    TEST_ASSERT_FALSE(fk_chime_quiet(5, 3, 5));
    TEST_ASSERT_FALSE(fk_chime_quiet(23, 3, 5));
    TEST_ASSERT_TRUE(fk_chime_quiet(23, 22, 7));
    TEST_ASSERT_TRUE(fk_chime_quiet(0, 22, 7));
    TEST_ASSERT_FALSE(fk_chime_quiet(7, 22, 7));
    /* the owner's window is honoured: 20-21 */
    c.quiet_from_h = 20; c.quiet_to_h = 21;
    st.last_b = 0;
    TEST_ASSERT_EQUAL_UINT8(40, fk_chime_due(&st, utc(2026, 7, 2, 0, 0, 0), &c).gain_pct);    /* 20:00 */
}

static void test_chime_error_state(void)
{
    fk_chime_ctx_t c = ctx();
    fk_chime_state_t st = { 0 };
    int64_t huge = INT64_C(1) << 56;                 /* a year localtime_r cannot represent */
    TEST_ASSERT_EQUAL_INT64(0, fk_chime_boundary(huge));
    fk_chime_verdict_t v = fk_chime_due(&st, huge, &c);
    TEST_ASSERT_EQUAL(FK_CH_ERROR, v.why);
    TEST_ASSERT_EQUAL_UINT8(0, v.pings);
    TEST_ASSERT_EQUAL_INT64(0, st.last_b);           /* nothing marked */
    /* NULL guards */
    TEST_ASSERT_EQUAL(FK_CH_ERROR, fk_chime_due(NULL, H14, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_ERROR, fk_chime_due(&st, H14, NULL).why);
    TEST_ASSERT_EQUAL_INT64(0, st.last_b);
    TEST_ASSERT_EQUAL_UINT32(0, fk_chime_arm_ms(H14, NULL, true));
}

static void test_chime_quiet_through_due(void)
{
    fk_chime_ctx_t c = ctx();                        /* 22-07 */
    fk_chime_state_t st = { 0 };
    /* 2026-07-01 EDT: 23:00 local is 03:00Z on the 2nd, 00:00 is 04:00Z */
    fk_chime_verdict_t v = fk_chime_due(&st, utc(2026, 7, 2, 3, 0, 0), &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(11, v.pings);
    TEST_ASSERT_EQUAL_UINT8(40, v.gain_pct);
    v = fk_chime_due(&st, utc(2026, 7, 2, 4, 0, 0), &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(12, v.pings);
    TEST_ASSERT_EQUAL_UINT8(40, v.gain_pct);
    /* from == to: no quiet window, at any hour */
    c.quiet_from_h = 5; c.quiet_to_h = 5;
    fk_chime_state_t s2 = { 0 };
    TEST_ASSERT_EQUAL_UINT8(100, fk_chime_due(&s2, utc(2026, 7, 2, 4, 0, 0), &c).gain_pct);
    TEST_ASSERT_EQUAL_UINT8(100, fk_chime_due(&s2, utc(2026, 7, 2, 9, 0, 0), &c).gain_pct);   /* 05:00 */
}

static void test_chime_spring_after_previous_hour(void)
{
    fk_chime_state_t st = { utc(2026, 3, 8, 6, 0, 0) };   /* 01:00 EST handled */
    fk_chime_ctx_t c = ctx();
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, utc(2026, 3, 8, 6, 59, 59), &c).why);
    fk_chime_verdict_t v = fk_chime_due(&st, utc(2026, 3, 8, 7, 0, 0), &c);   /* 03:00 EDT: the gap is skipped */
    TEST_ASSERT_EQUAL(FK_CH_PLAY, v.why);
    TEST_ASSERT_EQUAL_UINT8(3, v.pings);
    TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 8, 7, 0, 0), st.last_b);
}

static void test_chime_tz_change_between_calls(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14, &c).why);   /* EDT 14:00 = 18:00Z */
    tz("IST-5:30");                                  /* the owner moves to a half-hour zone: local :00 is UTC :30 */
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14 + 1800 + 3, &c).why);   /* 23:30 IST: a new local hour */
    TEST_ASSERT_EQUAL_INT64(H14 + 1800, st.last_b);
    tz(EST);                                         /* and back: 14:30 EDT's boundary 14:00 is already marked */
    TEST_ASSERT_EQUAL(FK_CH_WAIT, fk_chime_due(&st, H14 + 1800 + 5, &c).why);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&st, H14 + 3600, &c).why);
}

static void test_chime_pings(void)
{
    TEST_ASSERT_EQUAL_UINT8(12, fk_chime_pings(0));
    TEST_ASSERT_EQUAL_UINT8(12, fk_chime_pings(12));
    TEST_ASSERT_EQUAL_UINT8(1, fk_chime_pings(13));
    TEST_ASSERT_EQUAL_UINT8(11, fk_chime_pings(23));
    TEST_ASSERT_EQUAL_UINT8(1, fk_chime_pings(1));
    TEST_ASSERT_EQUAL_UINT8(11, fk_chime_pings(11));
}


static void test_chime_arm(void)
{
    fk_chime_state_t st = { 0 };
    fk_chime_ctx_t c = ctx();
    int64_t t = H14 + 3600 - 60;                      /* 14:59:00 */
    fk_chime_verdict_t v = fk_chime_due(&st, t, &c);
    TEST_ASSERT_EQUAL_UINT32(60000, fk_chime_arm_ms(t, &v, true));
    t = H14 + 3600 - 30;
    v = fk_chime_due(&st, t, &c);
    TEST_ASSERT_EQUAL_UINT32(30000, fk_chime_arm_ms(t, &v, true));
    t = H14 + 3600 - 61;
    v = fk_chime_due(&st, t, &c);
    TEST_ASSERT_EQUAL_UINT32(60000, fk_chime_arm_ms(t, &v, true));
    TEST_ASSERT_EQUAL_UINT32(1, fk_chime_arm_ms(v.next_s, &v, true));

    c.time_set = false;
    v = fk_chime_due(&st, 100, &c);
    TEST_ASSERT_EQUAL_UINT32(60000, fk_chime_arm_ms(100, &v, true));
    c.chime_on = false;
    v = fk_chime_due(&st, 100, &c);
    TEST_ASSERT_EQUAL_UINT32(0, fk_chime_arm_ms(100, &v, false));

    /* a clock stepped from 1970 to 13:59:59 is seen at the next arm, and 14:00 plays */
    fk_chime_state_t s2 = { 0 };
    c = ctx();
    c.time_set = false;
    v = fk_chime_due(&s2, 5000, &c);
    uint32_t ms = fk_chime_arm_ms(5000, &v, true);
    TEST_ASSERT_EQUAL_UINT32(60000, ms);
    c.time_set = true;                                /* the clock was set meanwhile */
    v = fk_chime_due(&s2, H14 - 1, &c);               /* the arm fires: 13:59:59 */
    TEST_ASSERT_EQUAL(FK_CH_LATE, v.why);             /* 13:00's grace is long past: marked, no play */
    TEST_ASSERT_EQUAL_UINT32(1000, fk_chime_arm_ms(H14 - 1, &v, true));
    TEST_ASSERT_EQUAL(FK_CH_PLAY, fk_chime_due(&s2, H14, &c).why);
}


/* ---- Task 16: the service's pure part (fk_chime_sched_t): the `dw chime at +N` offset (H7), the state zeroed on an
 * offset change, ERROR's re-arm. */
static void test_sched_at_plays_n_seconds_ahead(void)
{
    fk_chime_sched_t s;
    memset(&s, 0, sizeof s);
    s.st.last_b = H14 - 3600;                         /* 13:00 handled */
    fk_chime_ctx_t c = ctx();
    const int64_t real = H14 - 600;                   /* 13:50:00 */
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, real, 10));
    TEST_ASSERT_EQUAL_INT64(590, s.off_s);            /* the next top of hour (14:00) is 10 s ahead */
    TEST_ASSERT_EQUAL_INT64(0, s.st.last_b);          /* the offset changed: st zeroed */
    /* the command's own wake: 13:00 is long past (LATE, marked); the arm is the 10 s to 14:00 */
    TEST_ASSERT_EQUAL_UINT32(10000, fk_chime_sched_tick(&s, real, &c));
    TEST_ASSERT_EQUAL(FK_CH_LATE, s.last.why);
    TEST_ASSERT_EQUAL_UINT32(1000, fk_chime_sched_tick(&s, real + 9, &c));
    TEST_ASSERT_EQUAL(FK_CH_WAIT, s.last.why);
    /* the fire: the real path plays 14:00's two pings */
    TEST_ASSERT_EQUAL_UINT32(FK_CHIME_ARM_CAP_MS, fk_chime_sched_tick(&s, real + 10, &c));
    TEST_ASSERT_EQUAL(FK_CH_PLAY, s.last.why);
    TEST_ASSERT_EQUAL_UINT8(2, s.last.pings);
    TEST_ASSERT_EQUAL_UINT8(100, s.last.gain_pct);
    TEST_ASSERT_EQUAL_INT64(H14, s.st.last_b);
    TEST_ASSERT_EQUAL_INT64(H14 + 3600, s.last.next_s);   /* shifted time */
    fk_chime_sched_tick(&s, real + 11, &c);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, s.last.why);        /* once */
}

static void test_sched_offset_change_zeroes_state(void)
{
    fk_chime_sched_t s;
    memset(&s, 0, sizeof s);
    fk_chime_ctx_t c = ctx();
    const int64_t real = H14 - 600;
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, real, 10));
    fk_chime_sched_tick(&s, real + 10, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, s.last.why);
    /* the same offset again (the same command a second later lands on the same boundary 10 s on: another offset) */
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, real + 20, 10));
    TEST_ASSERT_EQUAL_INT64(570, s.off_s);
    TEST_ASSERT_EQUAL_INT64(0, s.st.last_b);          /* changed: zeroed, so the virtual 14:00 plays again */
    fk_chime_sched_tick(&s, real + 20, &c);
    fk_chime_sched_tick(&s, real + 30, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, s.last.why);
    /* an unchanged offset keeps st */
    const int64_t kept = s.st.last_b;
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, real + 21, 9));   /* 14:00 - (real + 30) - 0: the same 570 */
    TEST_ASSERT_EQUAL_INT64(570, s.off_s);
    TEST_ASSERT_EQUAL_INT64(kept, s.st.last_b);
    /* clearing is a change: zeroed; the real hour then decides (13:50: 13:00 is LATE) */
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, real + 40, 0));
    TEST_ASSERT_EQUAL_INT64(0, s.off_s);
    TEST_ASSERT_EQUAL_INT64(0, s.st.last_b);
    TEST_ASSERT_EQUAL_UINT32(FK_CHIME_ARM_CAP_MS, fk_chime_sched_tick(&s, real + 40, &c));
    TEST_ASSERT_EQUAL(FK_CH_LATE, s.last.why);
    /* clearing with no offset set changes nothing: st kept */
    const int64_t b = s.st.last_b;
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, real + 41, 0));
    TEST_ASSERT_EQUAL_INT64(b, s.st.last_b);
}

static void test_sched_at_bounds_and_quiet_hour(void)
{
    fk_chime_sched_t s;
    memset(&s, 0, sizeof s);
    s.st.last_b = 77;
    fk_chime_ctx_t c = ctx();
    TEST_ASSERT_FALSE(fk_chime_sched_at(&s, H14, FK_CHIME_AT_MAX_S + 1));   /* refused: nothing changed */
    TEST_ASSERT_EQUAL_INT64(0, s.off_s);
    TEST_ASSERT_EQUAL_INT64(77, s.st.last_b);
    TEST_ASSERT_FALSE(fk_chime_sched_at(NULL, H14, 10));
    TEST_ASSERT_FALSE(fk_chime_sched_at(&s, INT64_C(1) << 56, 10));          /* no boundary */
    TEST_ASSERT_EQUAL_INT64(77, s.st.last_b);
    /* the longest offset: the hour before is 60 s back, LATE, never a play on the command itself */
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, H14 - 3600 + 1, FK_CHIME_AT_MAX_S));
    fk_chime_sched_tick(&s, H14 - 3600 + 1, &c);
    TEST_ASSERT_EQUAL(FK_CH_LATE, s.last.why);
    fk_chime_sched_tick(&s, H14 - 3600 + 1 + FK_CHIME_AT_MAX_S, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, s.last.why);
    /* H7: the quiet decision uses the virtual boundary's hour: 21:59:00 + 10 s -> 22:00, quiet (40 %) */
    const int64_t h22 = utc(2026, 7, 2, 2, 0, 0);     /* 22:00 EDT */
    TEST_ASSERT_EQUAL_INT(22, local_hour(h22));
    memset(&s, 0, sizeof s);
    TEST_ASSERT_TRUE(fk_chime_sched_at(&s, h22 - 3600 + 1800, 10));          /* 21:30 real, at +10 */
    fk_chime_sched_tick(&s, h22 - 3600 + 1810, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, s.last.why);
    TEST_ASSERT_EQUAL_UINT8(FK_CHIME_QUIET_PCT, s.last.gain_pct);
    TEST_ASSERT_EQUAL_UINT8(10, s.last.pings);
}

static void test_sched_error_off_and_unset(void)
{
    fk_chime_sched_t s;
    memset(&s, 0, sizeof s);
    s.st.last_b = H14;
    fk_chime_ctx_t c = ctx();
    /* ERROR: nothing done, nothing marked, re-armed at the cap */
    TEST_ASSERT_EQUAL_UINT32(FK_CHIME_ARM_CAP_MS, fk_chime_sched_tick(&s, INT64_C(1) << 56, &c));
    TEST_ASSERT_EQUAL(FK_CH_ERROR, s.last.why);
    TEST_ASSERT_EQUAL_INT64(H14, s.st.last_b);
    TEST_ASSERT_EQUAL_UINT32(0, fk_chime_sched_tick(NULL, H14, &c));
    TEST_ASSERT_EQUAL_UINT32(FK_CHIME_ARM_CAP_MS, fk_chime_sched_tick(&s, H14, NULL));   /* ERROR: re-armed */
    TEST_ASSERT_EQUAL(FK_CH_ERROR, s.last.why);
    /* off: no timer */
    c.chime_on = false;
    TEST_ASSERT_EQUAL_UINT32(0, fk_chime_sched_tick(&s, H14 + 3600, &c));
    TEST_ASSERT_EQUAL(FK_CH_OFF, s.last.why);
    TEST_ASSERT_EQUAL_INT64(H14, s.st.last_b);        /* OFF marks nothing: st kept */
    /* unset: polled at the cap */
    c = ctx();
    c.time_set = false;
    TEST_ASSERT_EQUAL_UINT32(FK_CHIME_ARM_CAP_MS, fk_chime_sched_tick(&s, H14 + 3600, &c));
    TEST_ASSERT_EQUAL(FK_CH_UNSET, s.last.why);
    /* the state is kept across wakes (and the service never resets it at a sleep): 15:00 plays once */
    c = ctx();
    fk_chime_sched_tick(&s, H14 + 3600, &c);
    TEST_ASSERT_EQUAL(FK_CH_PLAY, s.last.why);
    fk_chime_sched_tick(&s, H14 + 3601, &c);
    TEST_ASSERT_EQUAL(FK_CH_WAIT, s.last.why);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chime_on_the_hour);
    RUN_TEST(test_chime_once_per_hour_step_back);
    RUN_TEST(test_chime_step_forward_late);
    RUN_TEST(test_chime_step_forward_within_grace);
    RUN_TEST(test_chime_dst_spring);
    RUN_TEST(test_chime_dst_fall);
    RUN_TEST(test_chime_half_hour_zone);
    RUN_TEST(test_chime_southern_zones);
    RUN_TEST(test_chime_clock_corrected_backward);
    RUN_TEST(test_chime_step_back_under_an_hour_waits);
    RUN_TEST(test_chime_offset_h7);
    RUN_TEST(test_chime_unset);
    RUN_TEST(test_chime_silent_marks);
    RUN_TEST(test_chime_sound_pending_marks_nothing);
    RUN_TEST(test_chime_provisioning);
    RUN_TEST(test_chime_off);
    RUN_TEST(test_chime_quiet_window);
    RUN_TEST(test_chime_error_state);
    RUN_TEST(test_chime_quiet_through_due);
    RUN_TEST(test_chime_spring_after_previous_hour);
    RUN_TEST(test_chime_tz_change_between_calls);
    RUN_TEST(test_chime_pings);
    RUN_TEST(test_chime_arm);
    RUN_TEST(test_sched_at_plays_n_seconds_ahead);
    RUN_TEST(test_sched_offset_change_zeroes_state);
    RUN_TEST(test_sched_at_bounds_and_quiet_hour);
    RUN_TEST(test_sched_error_off_and_unset);
    return UNITY_END();
}
