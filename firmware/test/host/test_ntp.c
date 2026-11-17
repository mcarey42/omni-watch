#include "unity.h"
#include <string.h>
#include "omni/ntp.h"

/* R109: SNTP best-of-N. Replies are built here byte by byte (big-endian NTP timestamps) from times chosen in unix us,
 * so the offset and delay each test expects are written out, not re-derived by the code under test. */

void setUp(void) {}
void tearDown(void) {}

#define T_2026 1793049600000000LL   /* 2026-10-26 21:20:00 UTC, unix us */

static void put_ts(uint8_t *p, int64_t unix_us)
{
    const int64_t s = unix_us / 1000000 + OMNI_NTP_UNIX_EPOCH, us = unix_us % 1000000;
    const uint32_t sec = (uint32_t)s, frac = (uint32_t)(((uint64_t)us << 32) / 1000000u);
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(sec >> (24 - 8 * i)), p[4 + i] = (uint8_t)(frac >> (24 - 8 * i));
}

/* the server's reply to a request carrying t1 (its originate), received at t2 and sent at t3 */
static void reply(uint8_t pkt[48], int64_t t1, int64_t t2, int64_t t3)
{
    memset(pkt, 0, 48);
    pkt[0] = (0 << 6) | (4 << 3) | 4;   /* LI 0, version 4, mode 4 */
    pkt[1] = 2;                          /* stratum 2 */
    uint8_t req[48];
    omni_ntp_request(req, t1);
    memcpy(pkt + 24, req + 40, 8);       /* originate = the request's transmit */
    put_ts(pkt + 32, t2);
    put_ts(pkt + 40, t3);
}

static void test_request(void)
{
    uint8_t req[48];
    memset(req, 0xa5, sizeof req);
    omni_ntp_request(req, T_2026 + 250000);
    TEST_ASSERT_EQUAL_HEX8(0x23, req[0]);   /* LI 0, version 4, mode 3 */
    for (int i = 1; i < 40; i++) TEST_ASSERT_EQUAL_HEX8(0, req[i]);
    uint8_t want[8];
    put_ts(want, T_2026 + 250000);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, req + 40, 8);
    /* 2026-10-26 21:20:00 UTC is NTP second 0xEE8A4280 (1793049600 + 2208988800 = 4002038400) */
    TEST_ASSERT_EQUAL_HEX8(0xEE, req[40]);
    TEST_ASSERT_EQUAL_HEX8(0x8A, req[41]);
    TEST_ASSERT_EQUAL_HEX8(0x42, req[42]);
    TEST_ASSERT_EQUAL_HEX8(0x80, req[43]);
    TEST_ASSERT_EQUAL_HEX8(0x40, req[44]);   /* 0.25 s */
}

static void test_ts(void)
{
    TEST_ASSERT_EQUAL_INT64(T_2026, omni_ntp_ts_unix_us(0xEE8A4280u, 0));
    TEST_ASSERT_EQUAL_INT64(T_2026 + 500000, omni_ntp_ts_unix_us(0xEE8A4280u, 0x80000000u));
    TEST_ASSERT_EQUAL_INT64(0, omni_ntp_ts_unix_us((uint32_t)OMNI_NTP_UNIX_EPOCH, 0));
    /* era 1: 2036-02-07 06:28:16 UTC is NTP second 0 again (unix 2085978496) */
    TEST_ASSERT_EQUAL_INT64(2085978496LL * 1000000, omni_ntp_ts_unix_us(0, 0));
    TEST_ASSERT_EQUAL_INT64(2085978496LL * 1000000 + 1000000, omni_ntp_ts_unix_us(1, 0));
}

/* The watch 300 ms behind; 40 ms out, 10 ms at the server, 20 ms back (asymmetric: the offset is off by half of it). */
static void test_offset_delay(void)
{
    uint8_t p[48];
    omni_ntp_sample_t s;
    const int64_t t1 = T_2026, true_off = 300000;
    const int64_t t2 = t1 + true_off + 40000, t3 = t2 + 10000, t4 = t3 - true_off + 20000;
    reply(p, t1, t2, t3);
    TEST_ASSERT_EQUAL(OMNI_NTP_OK, omni_ntp_sample(p, 48, t1, t4, true, &s));
    TEST_ASSERT_EQUAL_INT64(60000, s.delay_us);          /* 40 + 20 */
    TEST_ASSERT_EQUAL_INT64(310000, s.offset_us);   /* (340 + 280) / 2: 300 ms + half the 20 ms asymmetry */
    TEST_ASSERT_EQUAL_UINT8(2, s.stratum);
}

/* Symmetric paths: the offset is exact, ahead or behind, at any size while the clock is unset. */
static void test_offset_symmetric(void)
{
    uint8_t p[48];
    omni_ntp_sample_t s;
    const int64_t offs[] = { 0, 123456, -987654, -48000, 56LL * 365 * 86400 * 1000000 };
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++) {
        const int64_t t1 = T_2026 - offs[i], t2 = T_2026 + 15000, t3 = t2 + 700, t4 = t1 + 30700;
        reply(p, t1, t2, t3);
        TEST_ASSERT_EQUAL(OMNI_NTP_OK, omni_ntp_sample(p, 48, t1, t4, i < 4, &s));
        TEST_ASSERT_INT64_WITHIN(1, offs[i], s.offset_us);
        TEST_ASSERT_INT64_WITHIN(1, 30000, s.delay_us);
    }
}

static void test_rejects(void)
{
    uint8_t p[48];
    omni_ntp_sample_t s = { 7, 7, 7 };
    const int64_t t1 = T_2026, t2 = t1 + 10000, t3 = t2 + 100, t4 = t1 + 20100;
    reply(p, t1, t2, t3);
    TEST_ASSERT_EQUAL(OMNI_NTP_SHORT, omni_ntp_sample(p, 47, t1, t4, true, &s));
    p[0] = (3 << 6) | (4 << 3) | 4;
    TEST_ASSERT_EQUAL(OMNI_NTP_LEAP, omni_ntp_sample(p, 48, t1, t4, true, &s));
    p[0] = (0 << 6) | (4 << 3) | 3;   /* a client packet */
    TEST_ASSERT_EQUAL(OMNI_NTP_MODE, omni_ntp_sample(p, 48, t1, t4, true, &s));
    p[0] = (0 << 6) | (0 << 3) | 4;   /* version 0 */
    TEST_ASSERT_EQUAL(OMNI_NTP_MODE, omni_ntp_sample(p, 48, t1, t4, true, &s));
    p[0] = (0 << 6) | (3 << 3) | 4;   /* version 3: fine */
    TEST_ASSERT_EQUAL(OMNI_NTP_OK, omni_ntp_sample(p, 48, t1, t4, true, &s));
    p[1] = 0;   /* kiss-o'-death */
    TEST_ASSERT_EQUAL(OMNI_NTP_STRATUM, omni_ntp_sample(p, 48, t1, t4, true, &s));
    p[1] = 16;
    TEST_ASSERT_EQUAL(OMNI_NTP_STRATUM, omni_ntp_sample(p, 48, t1, t4, true, &s));
    p[1] = 15;
    TEST_ASSERT_EQUAL(OMNI_NTP_OK, omni_ntp_sample(p, 48, t1, t4, true, &s));
    p[1] = 1;
    memset(p + 40, 0, 8);
    TEST_ASSERT_EQUAL(OMNI_NTP_ZERO, omni_ntp_sample(p, 48, t1, t4, true, &s));
    reply(p, t1, t2, t3);
    memset(p + 32, 0, 8);
    TEST_ASSERT_EQUAL(OMNI_NTP_ZERO, omni_ntp_sample(p, 48, t1, t4, true, &s));
    /* a late reply to the previous request (another originate) */
    reply(p, t1 - 100000, t2, t3);
    TEST_ASSERT_EQUAL(OMNI_NTP_ORIGIN, omni_ntp_sample(p, 48, t1, t4, true, &s));
    /* round trips: 1 s is too long, 999 ms is not; negative (t4 before t1) is impossible */
    reply(p, t1, t2, t3);
    TEST_ASSERT_EQUAL(OMNI_NTP_DELAY, omni_ntp_sample(p, 48, t1, t1 + 1000100, true, &s));
    TEST_ASSERT_EQUAL(OMNI_NTP_OK, omni_ntp_sample(p, 48, t1, t1 + 999100, true, &s));
    TEST_ASSERT_EQUAL(OMNI_NTP_DELAY, omni_ntp_sample(p, 48, t1, t1 - 5000, true, &s));
    reply(p, t1, t3, t2);   /* the server sent before it received */
    TEST_ASSERT_EQUAL(OMNI_NTP_DELAY, omni_ntp_sample(p, 48, t1, t4, true, &s));
    /* a set clock 25 h off: a bogus reply (truth.py's +2e9 s); within 24 h: taken; unset: taken */
    const int64_t far = 25LL * 3600 * 1000000;
    reply(p, t1, t2 + far, t3 + far);
    TEST_ASSERT_EQUAL(OMNI_NTP_INSANE, omni_ntp_sample(p, 48, t1, t4, true, &s));
    TEST_ASSERT_EQUAL(OMNI_NTP_OK, omni_ntp_sample(p, 48, t1, t4, false, &s));
    reply(p, t1, t2 - far, t3 - far);
    TEST_ASSERT_EQUAL(OMNI_NTP_INSANE, omni_ntp_sample(p, 48, t1, t4, true, &s));
    const int64_t near = 23LL * 3600 * 1000000;
    reply(p, t1, t2 + near, t3 + near);
    TEST_ASSERT_EQUAL(OMNI_NTP_OK, omni_ntp_sample(p, 48, t1, t4, true, &s));
    TEST_ASSERT_INT64_WITHIN(1, near, s.offset_us);
}

static void test_out_untouched_on_error(void)
{
    uint8_t p[48];
    omni_ntp_sample_t s = { 7, 8, 9 };
    reply(p, T_2026, T_2026 + 10, T_2026 + 20);
    p[1] = 0;
    TEST_ASSERT_EQUAL(OMNI_NTP_STRATUM, omni_ntp_sample(p, 48, T_2026, T_2026 + 30, true, &s));
    TEST_ASSERT_EQUAL_INT64(7, s.offset_us);
    TEST_ASSERT_EQUAL_INT64(8, s.delay_us);
    TEST_ASSERT_EQUAL_UINT8(9, s.stratum);
}

static void test_best(void)
{
    const omni_ntp_sample_t s[] = { { -120000, 250000, 2 }, { -30000, 41000, 2 }, { -26000, 40000, 3 },
                                    { -90000, 40000, 2 }, { -500000, 900000, 1 } };
    TEST_ASSERT_EQUAL_INT(-1, omni_ntp_best(s, 0));
    TEST_ASSERT_EQUAL_INT(0, omni_ntp_best(s, 1));
    TEST_ASSERT_EQUAL_INT(1, omni_ntp_best(s, 2));
    TEST_ASSERT_EQUAL_INT(2, omni_ntp_best(s, 5));   /* 40 ms, the first of the two */
}

static void test_err_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", omni_ntp_err_name(OMNI_NTP_OK));
    TEST_ASSERT_EQUAL_STRING("origin", omni_ntp_err_name(OMNI_NTP_ORIGIN));
    TEST_ASSERT_EQUAL_STRING("insane", omni_ntp_err_name(OMNI_NTP_INSANE));
    TEST_ASSERT_EQUAL_STRING("?", omni_ntp_err_name((omni_ntp_err_t)99));
}

/* Re-review N1: lwIP turns SO_RCVTIMEO into whole ms by truncation, and 0 ms means "wait forever"; the remaining
 * window is rounded up to whole ms, at least 1. */
static void test_timeout_ms(void)
{
    TEST_ASSERT_EQUAL_INT64(1, omni_ntp_timeout_ms(0));
    TEST_ASSERT_EQUAL_INT64(1, omni_ntp_timeout_ms(1));
    TEST_ASSERT_EQUAL_INT64(1, omni_ntp_timeout_ms(999));
    TEST_ASSERT_EQUAL_INT64(1, omni_ntp_timeout_ms(1000));
    TEST_ASSERT_EQUAL_INT64(2, omni_ntp_timeout_ms(1001));
    TEST_ASSERT_EQUAL_INT64(1000, omni_ntp_timeout_ms(1000000));   /* the full 1 s window */
    TEST_ASSERT_EQUAL_INT64(1, omni_ntp_timeout_ms(-5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_request);
    RUN_TEST(test_ts);
    RUN_TEST(test_offset_delay);
    RUN_TEST(test_offset_symmetric);
    RUN_TEST(test_rejects);
    RUN_TEST(test_out_untouched_on_error);
    RUN_TEST(test_best);
    RUN_TEST(test_err_names);
    RUN_TEST(test_timeout_ms);
    return UNITY_END();
}
