/* Time asleep (rd/asleep.h; rd wave 1 Task 9, spec section 9.6, R154, PR17): the duration at a revealing wake by the
 * same-boot (monotonic) and soft-reset (unix) rules, the caps, the record's seal and check, the consuming wake, the
 * cold-boot clear and the readout's lines. */
#include <string.h>
#include "unity.h"
#include "rd/asleep.h"

void setUp(void) {}
void tearDown(void) {}

#define TOKEN 0x1234abcdu
#define T0 1793500000LL   /* 2026-10-31 (any set wall clock) */

static rd_asleep_rec_t rec(uint32_t token, int64_t unix_s, int64_t mono_us)
{
    rd_asleep_rec_t r;
    memset(&r, 0xA5, sizeof r);   /* the padding is RTC garbage: the seal must not depend on it */
    r.token = token, r.sleep_unix = unix_s, r.sleep_mono_us = mono_us, r.used = 0;
    rd_asleep_seal(&r);
    return r;
}

static void test_rd_asleep_same_boot(void)
{
    const rd_asleep_rec_t r = rec(TOKEN, T0, 5000000);
    rd_asleep_path_t p = RD_AS_NONE;
    TEST_ASSERT_TRUE(rd_asleep_valid(&r));
    TEST_ASSERT_EQUAL_INT32(61, rd_asleep_dur(&r, TOKEN, 5000000 + 61400000, T0 + 61, true, &p));
    TEST_ASSERT_EQUAL(RD_AS_SAME_BOOT, p);
    /* whatever the unix clocks say: stepped back an hour, unset at the sleep, unset now */
    TEST_ASSERT_EQUAL_INT32(61, rd_asleep_dur(&r, TOKEN, 5000000 + 61400000, T0 + 61 - 3600, true, &p));
    TEST_ASSERT_EQUAL(RD_AS_SAME_BOOT, p);
    const rd_asleep_rec_t u = rec(TOKEN, 0, 5000000);
    TEST_ASSERT_EQUAL_INT32(61, rd_asleep_dur(&u, TOKEN, 5000000 + 61400000, 0, false, &p));
    TEST_ASSERT_EQUAL(RD_AS_SAME_BOOT, p);
    /* a monotonic clock behind the sleep (impossible in one boot): none */
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN, 4000000, T0, true, &p));
}

static void test_rd_asleep_soft_reset(void)
{
    const rd_asleep_rec_t r = rec(TOKEN, T0, 900000000);
    rd_asleep_path_t p = RD_AS_NONE;
    /* another token (a new boot): the monotonic clock restarted, so the unix path; d = 3 h */
    TEST_ASSERT_EQUAL_INT32(10800, rd_asleep_dur(&r, TOKEN ^ 1u, 2000000, T0 + 10800, true, &p));
    TEST_ASSERT_EQUAL(RD_AS_SOFT_RESET, p);
    /* the clock unset now (C2): none */
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN ^ 1u, 2000000, T0 + 10800, false, &p));
    TEST_ASSERT_EQUAL(RD_AS_NONE, p);
}

static void test_rd_asleep_soft_reset_negative(void)
{
    const rd_asleep_rec_t r = rec(TOKEN, T0, 0);
    rd_asleep_path_t p = RD_AS_SOFT_RESET;
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN + 7u, 1000, T0 - 5, true, &p));
    TEST_ASSERT_EQUAL(RD_AS_NONE, p);
    TEST_ASSERT_EQUAL_INT32(0, rd_asleep_dur(&r, TOKEN + 7u, 1000, T0, true, &p));   /* 0 is a duration */
}

static void test_rd_asleep_soft_reset_cap(void)
{
    const rd_asleep_rec_t r = rec(TOKEN, T0, 0);
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, 1u, 0, T0 + 400LL * 86400, true, NULL));
    TEST_ASSERT_EQUAL_INT32(399 * 86400, rd_asleep_dur(&r, 1u, 0, T0 + 399LL * 86400, true, NULL));
    TEST_ASSERT_EQUAL_INT32(400 * 86400 - 1, rd_asleep_dur(&r, 1u, 0, T0 + 400LL * 86400 - 1, true, NULL));
}

static void test_rd_asleep_unset_at_sleep(void)
{
    const rd_asleep_rec_t r = rec(TOKEN, 0, 0);   /* the clock was unset when it slept */
    rd_asleep_path_t p = RD_AS_SOFT_RESET;
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN + 1u, 0, T0, true, &p));
    TEST_ASSERT_EQUAL(RD_AS_NONE, p);
}

static void test_rd_asleep_invalid(void)
{
    rd_asleep_rec_t r = rec(TOKEN, T0, 0);
    r.crc ^= 1u;
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN, 1000000, T0 + 1, true, NULL));
    r = rec(TOKEN, T0, 0);
    r.magic ^= 0x100u;
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN, 1000000, T0 + 1, true, NULL));
    /* every covered field: a changed one is caught */
    r = rec(TOKEN, T0, 0);
    r.sleep_unix += 1;
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    r = rec(TOKEN, T0, 0);
    r.sleep_mono_us += 1;
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    r = rec(TOKEN, T0, 0);
    r.token += 1;
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    r = rec(TOKEN, T0, 0);
    r.used = 1;   /* a forged consume */
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    /* RTC garbage (the cold boot that was not cleared) and an all-zero record */
    memset(&r, 0x5A, sizeof r);
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    memset(&r, 0, sizeof r);
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    TEST_ASSERT_FALSE(rd_asleep_valid(NULL));
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(NULL, TOKEN, 0, T0, true, NULL));
}

static void test_rd_asleep_consumed(void)
{
    rd_asleep_rec_t r = rec(TOKEN, T0, 1000000);
    TEST_ASSERT_EQUAL_INT32(60, rd_asleep_dur(&r, TOKEN, 61000000, T0 + 60, true, NULL));
    rd_asleep_consume(&r);
    TEST_ASSERT_TRUE(rd_asleep_valid(&r));   /* still a sound record */
    TEST_ASSERT_EQUAL_UINT8(1, r.used);
    rd_asleep_path_t p = RD_AS_SAME_BOOT;
    /* a wake with no new rd sleep (as after sleeping on the shared Digital face): none, not the stale span */
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN, 99000000, T0 + 98, true, &p));
    TEST_ASSERT_EQUAL(RD_AS_NONE, p);
    TEST_ASSERT_EQUAL_INT32(-1, rd_asleep_dur(&r, TOKEN + 1u, 0, T0 + 98, true, NULL));   /* nor after a reset */
    /* the next rd sleep writes a fresh record */
    r = rec(TOKEN, T0 + 100, 100000000);
    TEST_ASSERT_EQUAL_INT32(5, rd_asleep_dur(&r, TOKEN, 105000000, T0 + 105, true, NULL));
    rd_asleep_consume(NULL);   /* harmless */
}

static void test_rd_asleep_keep_at_boot(void)
{
    TEST_ASSERT_FALSE(rd_asleep_keep_at_boot(RD_RST_POWERON));
    TEST_ASSERT_FALSE(rd_asleep_keep_at_boot(RD_RST_BROWNOUT));
    TEST_ASSERT_TRUE(rd_asleep_keep_at_boot(RD_RST_SOFT));
    TEST_ASSERT_TRUE(rd_asleep_keep_at_boot(RD_RST_PANIC));
    TEST_ASSERT_TRUE(rd_asleep_keep_at_boot(RD_RST_WDT));
    TEST_ASSERT_TRUE(rd_asleep_keep_at_boot(RD_RST_OTHER));
}

/* the Task 9 review's I1: the readout refreshes on the wake's cause, never on in->fresh */
static void test_rd_asleep_wake_counts(void)
{
    /* PA_WAKE_FULL: a wake after a sleep with its reveal (whatever the reveal style: Full, Fade and Instant all pass
     * reveal true, and reduced motion changes only the picture), a lift or PWR */
    TEST_ASSERT_TRUE(rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false));
    TEST_ASSERT_TRUE(rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, true));
    /* the Stasis lift: a lift on the face shown, and the jump (a FIRST wake of the face the lift brings up) */
    TEST_ASSERT_TRUE(rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, true, false));
    TEST_ASSERT_TRUE(rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, true, false));
    /* the boot's first wake (a soft reset's span) */
    TEST_ASSERT_TRUE(rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, true));
    /* a FIRST from a swipe, the picker or a screen closing over a new face: nothing slept */
    TEST_ASSERT_FALSE(rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, false));
    TEST_ASSERT_FALSE(rd_asleep_wake_counts(OMNI_WAKE_FIRST, false, false, false));
    /* PA_WAKE_SKIP_REVEAL (a re-lift within 10 s) and a screen closing (reveal false): the value shown stays */
    TEST_ASSERT_FALSE(rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, false, false, false));
    TEST_ASSERT_FALSE(rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, false, false, true));
    /* REPLAY: awake with no sleep */
    TEST_ASSERT_FALSE(rd_asleep_wake_counts(OMNI_WAKE_REPLAY, true, false, false));
}

/* the device's sequence on the pure rules (main/rd_asleep.c is this glue): a swipe away writes a record (the shell runs
 * the sleep hook at a swipe), a swipe back 5 minutes later is a FIRST wake that does not count, so the readout keeps its
 * value and the record its span; a real sleep and a full wake then show that sleep, once */
static void test_rd_asleep_wake_sequence(void)
{
    int32_t shown = -1;   /* a cold boot */
    rd_asleep_rec_t r = rec(TOKEN, T0, 10000000);   /* the swipe away from Stasis at 10 s */
    shown = rd_asleep_wake(&r, shown, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, false), TOKEN,
                           310000000, T0 + 300, true);   /* the swipe back at 310 s */
    TEST_ASSERT_EQUAL_INT32(-1, shown);   /* not "STASIS 05M 00S" */
    TEST_ASSERT_EQUAL_UINT8(0, r.used);
    r = rec(TOKEN, T0 + 400, 410000000);   /* a real sleep at 410 s */
    shown = rd_asleep_wake(&r, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), TOKEN,
                           470500000, T0 + 460, true);   /* a lift 60.5 s later */
    TEST_ASSERT_EQUAL_INT32(60, shown);
    TEST_ASSERT_EQUAL_UINT8(1, r.used);
    /* a re-lift within 10 s (PA_WAKE_SKIP_REVEAL) and a screen closing keep 60 */
    shown = rd_asleep_wake(&r, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, false, false, false), TOKEN,
                           480000000, T0 + 470, true);
    TEST_ASSERT_EQUAL_INT32(60, shown);
    /* a full wake with no new rd sleep (slept on Digital): the consumed record gives none */
    shown = rd_asleep_wake(&r, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), TOKEN,
                           900000000, T0 + 890, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
}

/* the controller's ruling: the boot's first wake on any face. A soft reset onto Stasis shows its span (the unix path); a
 * boot onto Digital (or another face, the picker) discards the record, so a swipe to Stasis later shows "--", never a
 * span with the awake time since the boot in it */
static void test_rd_asleep_boot_face(void)
{
    rd_asleep_rec_t r = rec(TOKEN, T0 + 1000, 999000000);   /* slept, then a soft reset (a new token) */
    rd_asleep_boot_face(&r, true);                          /* the boot shows Stasis */
    TEST_ASSERT_EQUAL_UINT8(0, r.used);
    int32_t shown = rd_asleep_wake(&r, -1, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, true), TOKEN + 1u,
                                   4000000, T0 + 1000 + 3 * 3600, true);
    TEST_ASSERT_EQUAL_INT32(3 * 3600, shown);
    TEST_ASSERT_EQUAL_UINT8(1, r.used);

    r = rec(TOKEN, T0 + 1000, 999000000);
    rd_asleep_boot_face(&r, false);   /* the boot shows Digital: its wake never reaches rd, the record goes now */
    TEST_ASSERT_TRUE(rd_asleep_valid(&r));
    TEST_ASSERT_EQUAL_UINT8(1, r.used);
    /* a swipe to Stasis 10 min later: a FIRST wake, not the boot's first */
    shown = rd_asleep_wake(&r, -1, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, false), TOKEN + 1u, 600000000,
                           T0 + 1000 + 3 * 3600 + 600, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    /* even a wake that counts (the next full wake with no rd sleep yet) finds nothing */
    shown = rd_asleep_wake(&r, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), TOKEN + 1u,
                           700000000, T0 + 1000 + 3 * 3600 + 700, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    char l1[RD_ASLEEP_LINE];
    rd_asleep_lines(shown, true, l1, NULL);
    TEST_ASSERT_EQUAL_STRING("STASIS --H --M", l1);
}

/* R162: the soft reset's record held while the clock is unset (no RTC: every boot's first wake is before SNTP) */
#define NEW (TOKEN + 1u)   /* the boot after the soft reset */
static const int32_t HELD_GAP = 3 * 3600 + 125;   /* the pre-reset sleep to the Stasis wake after SNTP */

static void boot_soft(rd_asleep_rec_t *r, rd_asleep_rec_t *h)
{
    *r = rec(TOKEN, T0, 999000000);   /* slept with the clock set, then a soft reset */
    memset(h, 0xA5, sizeof *h);       /* RTC garbage in the held slot */
    rd_asleep_boot_recs(r, h, NEW, true);
}

static void test_rd_asleep_held_while_unset(void)
{
    rd_asleep_rec_t r, h;
    boot_soft(&r, &h);
    TEST_ASSERT_TRUE(rd_asleep_held_live(&h));
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));   /* the live slot is this boot's */
    TEST_ASSERT_EQUAL_INT64(T0, h.sleep_unix);
    /* the boot's first wake on Stasis, the clock unset: held, not consumed (the readout: AWAITING TIME SIGNAL) */
    int32_t shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, true), true,
                                        NEW, 3000000, 0, false);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    TEST_ASSERT_TRUE(rd_asleep_held_live(&h));
    /* a swipe, a re-lift within 10 s and a screen closing never touch it, the clock set or not */
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, false), false, NEW,
                                6000000, T0 + 100, true);
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, false, false, false), true,
                                NEW, 7000000, T0 + 101, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    TEST_ASSERT_TRUE(rd_asleep_held_live(&h));
    /* a second soft reset with nothing usable (no sleep this boot) keeps the held record */
    rd_asleep_boot_recs(&r, &h, NEW + 1u, true);
    TEST_ASSERT_TRUE(rd_asleep_held_live(&h));
    TEST_ASSERT_EQUAL_INT64(T0, h.sleep_unix);
    /* any new sleep supersedes it (the clock still unset): the next full wake shows that sleep, the held span never */
    const rd_asleep_rec_t s1 = rec(NEW + 1u, 0, 20000000);
    rd_asleep_new_sleep(&r, &h, &s1);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), true,
                                NEW + 1u, 50000000, 0, false);
    TEST_ASSERT_EQUAL_INT32(30, shown);
}

static void test_rd_asleep_held_consumed_on_stasis(void)
{
    rd_asleep_rec_t r, h;
    /* the normal path (the 15 s timeout): SNTP, then a sleep, then the Stasis wake: the LAST sleep, not now - S0 */
    boot_soft(&r, &h);
    int32_t shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, true), true,
                                        NEW, 3000000, 0, false);
    const rd_asleep_rec_t s1 = rec(NEW, T0 + 40, 40000000);   /* SNTP set the clock; a sleep on Stasis */
    rd_asleep_new_sleep(&r, &h, &s1);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), true,
                                NEW, 90000000, T0 + HELD_GAP, true);
    TEST_ASSERT_EQUAL_INT32(50, shown);
    TEST_ASSERT_EQUAL_UINT8(1, r.used);
    /* no sleep since the boot: a counting Stasis wake once the clock is set (a Stasis lift) shows the held span */
    boot_soft(&r, &h);
    shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, true), true, NEW,
                                3000000, 0, false);
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, true, false), true, NEW,
                                90000000, T0 + HELD_GAP, true);
    TEST_ASSERT_EQUAL_INT32(HELD_GAP, shown);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    /* a re-lift keeps it; the next full wake with no new sleep finds nothing */
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, false, false, false), true,
                                NEW, 95000000, T0 + HELD_GAP + 5, true);
    TEST_ASSERT_EQUAL_INT32(HELD_GAP, shown);
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), true,
                                NEW, 200000000, T0 + HELD_GAP + 110, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    /* the boot's own wake with the clock already set (no sleep since the boot) */
    boot_soft(&r, &h);
    shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, true), true, NEW,
                                3000000, T0 + HELD_GAP, true);
    TEST_ASSERT_EQUAL_INT32(HELD_GAP, shown);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
}

static void test_rd_asleep_held_discarded_elsewhere(void)
{
    rd_asleep_rec_t r, h;
    boot_soft(&r, &h);
    int32_t shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, false, true), true,
                                        NEW, 3000000, 0, false);
    /* SNTP; a counting wake on another face before any sleep: discarded */
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), false,
                                NEW, 72000000, T0 + HELD_GAP, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);   /* no sleep this boot yet: none */
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    /* a later Stasis lift never shows the span */
    shown = rd_asleep_wake_held(&r, &h, shown, rd_asleep_wake_counts(OMNI_WAKE_FIRST, true, true, false), true, NEW,
                                85000000, T0 + HELD_GAP + 30, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    /* the clock still unset at the wake elsewhere: discarded all the same */
    boot_soft(&r, &h);
    (void)rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), false, NEW,
                              9000000, 0, false);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    /* the boot onto another face (rd_asleep_boot_face on the held slot) */
    boot_soft(&r, &h);
    rd_asleep_boot_face(&h, false);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    /* rd_asleep_new_sleep with no held slot writes the live record alone */
    const rd_asleep_rec_t s1 = rec(NEW, T0, 1000000);
    rd_asleep_new_sleep(&r, NULL, &s1);
    TEST_ASSERT_EQUAL_INT64(1000000, r.sleep_mono_us);
}

static void test_rd_asleep_held_cap_and_clear(void)
{
    rd_asleep_rec_t r, h;
    boot_soft(&r, &h);
    /* past the 400-day cap: none, and discarded */
    int32_t shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false),
                                        true, NEW, 9000000, T0 + RD_ASLEEP_MAX_S, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    boot_soft(&r, &h);   /* just inside the cap */
    shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), true,
                                NEW, 9000000, T0 + RD_ASLEEP_MAX_S - 1, true);
    TEST_ASSERT_EQUAL_INT32(RD_ASLEEP_MAX_S - 1, shown);
    /* behind the clock: none, discarded */
    boot_soft(&r, &h);
    shown = rd_asleep_wake_held(&r, &h, -1, rd_asleep_wake_counts(OMNI_WAKE_AFTER_SLEEP, true, false, false), true,
                                NEW, 9000000, T0 - 10, true);
    TEST_ASSERT_EQUAL_INT32(-1, shown);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    /* a power-on or brownout clears both slots */
    boot_soft(&r, &h);
    rd_asleep_boot_recs(&r, &h, NEW + 1u, rd_asleep_keep_at_boot(RD_RST_POWERON));
    TEST_ASSERT_FALSE(rd_asleep_valid(&r));
    TEST_ASSERT_FALSE(rd_asleep_valid(&h));
    boot_soft(&r, &h);
    rd_asleep_boot_recs(&r, &h, NEW + 1u, rd_asleep_keep_at_boot(RD_RST_BROWNOUT));
    TEST_ASSERT_FALSE(rd_asleep_valid(&h));
    /* nothing to hold: a record slept with the clock unset, a consumed one, this boot's own */
    r = rec(TOKEN, 0, 5000000);
    memset(&h, 0, sizeof h);
    rd_asleep_boot_recs(&r, &h, NEW, true);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    TEST_ASSERT_TRUE(rd_asleep_valid(&r));   /* left as it was: rd_asleep_wake gives none for it */
    r = rec(TOKEN, T0, 5000000);
    rd_asleep_consume(&r);
    rd_asleep_boot_recs(&r, &h, NEW, true);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    r = rec(NEW, T0, 5000000);
    rd_asleep_boot_recs(&r, &h, NEW, true);
    TEST_ASSERT_FALSE(rd_asleep_held_live(&h));
    /* with no held record, rd_asleep_wake_held is rd_asleep_wake */
    r = rec(NEW, T0, 5000000);
    memset(&h, 0, sizeof h);
    TEST_ASSERT_EQUAL_INT32(7, rd_asleep_wake_held(&r, &h, -1, true, true, NEW, 12000000, T0 + 7, true));
    /* a newer usable record replaces a held one at a second soft reset */
    boot_soft(&r, &h);
    r = rec(NEW, T0 + 500, 600000000);
    rd_asleep_boot_recs(&r, &h, NEW + 1u, true);
    TEST_ASSERT_EQUAL_INT64(T0 + 500, h.sleep_unix);
}

static void lines_are(int32_t s, bool set, const char *want1, const char *want2)
{
    char l1[RD_ASLEEP_LINE];
    const char *l2 = NULL;
    memset(l1, 'x', sizeof l1);
    rd_asleep_lines(s, set, l1, &l2);
    TEST_ASSERT_EQUAL_STRING(want1, l1);
    TEST_ASSERT_EQUAL_STRING(want2, l2);
}

static void test_rd_asleep_lines(void)
{
    static const char *const UP = "ROUNDED UP: 3 000 000 YRS";
    lines_are(12307, true, "STASIS 3H 25M 07S", UP);
    lines_are(245, true, "STASIS 04M 05S", UP);
    lines_are(0, true, "STASIS 00M 00S", UP);
    lines_are(3599, true, "STASIS 59M 59S", UP);
    lines_are(3600, true, "STASIS 1H 00M 00S", UP);
    lines_are(999 * 3600 + 59 * 60 + 59, true, "STASIS 999H 59M 59S", UP);
    lines_are(1000 * 3600, true, "STASIS 999H+", UP);
    lines_are(INT32_MAX, true, "STASIS 999H+", UP);
    lines_are(-1, true, "STASIS --H --M", UP);
    lines_are(-7, true, "STASIS --H --M", UP);
    lines_are(12307, false, "AWAITING TIME SIGNAL", "NOTHING TO THAW YET");
    lines_are(-1, false, "AWAITING TIME SIGNAL", "NOTHING TO THAW YET");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rd_asleep_same_boot);
    RUN_TEST(test_rd_asleep_soft_reset);
    RUN_TEST(test_rd_asleep_soft_reset_negative);
    RUN_TEST(test_rd_asleep_soft_reset_cap);
    RUN_TEST(test_rd_asleep_unset_at_sleep);
    RUN_TEST(test_rd_asleep_invalid);
    RUN_TEST(test_rd_asleep_consumed);
    RUN_TEST(test_rd_asleep_keep_at_boot);
    RUN_TEST(test_rd_asleep_lines);
    RUN_TEST(test_rd_asleep_wake_counts);
    RUN_TEST(test_rd_asleep_wake_sequence);
    RUN_TEST(test_rd_asleep_boot_face);
    RUN_TEST(test_rd_asleep_held_while_unset);
    RUN_TEST(test_rd_asleep_held_consumed_on_stasis);
    RUN_TEST(test_rd_asleep_held_discarded_elsewhere);
    RUN_TEST(test_rd_asleep_held_cap_and_clear);
    return UNITY_END();
}
