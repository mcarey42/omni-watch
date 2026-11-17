#include "omni/ntp.h"
#include <string.h>

/* R109: see omni/ntp.h. Integer only: offsets and delays in int64 microseconds. */

const char *omni_ntp_err_name(omni_ntp_err_t e)
{
    static const char *const N[OMNI_NTP__COUNT] = { "ok", "short", "mode", "leap", "stratum", "zero", "origin",
                                                     "delay", "insane" };
    return (unsigned)e < sizeof N / sizeof N[0] ? N[e] : "?";
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

int64_t omni_ntp_ts_unix_us(uint32_t sec, uint32_t frac)
{
    const int64_t s = (int64_t)sec + (sec < 0x80000000u ? (int64_t)1 << 32 : 0) - OMNI_NTP_UNIX_EPOCH;
    return s * 1000000 + (int64_t)(((uint64_t)frac * 1000000u + (1u << 31)) >> 32);   /* to the nearest us */
}

static void put_ts(uint8_t *p, int64_t unix_us)   /* unix_us >= 0 (a wall clock, set or not) */
{
    const int64_t s = unix_us / 1000000, us = unix_us % 1000000;
    wr32(p, (uint32_t)(s + OMNI_NTP_UNIX_EPOCH));   /* mod 2^32: the era wraps */
    wr32(p + 4, (uint32_t)(((uint64_t)us << 32) / 1000000u));
}

void omni_ntp_request(uint8_t pkt[OMNI_NTP_PACKET], int64_t t1_us)
{
    memset(pkt, 0, OMNI_NTP_PACKET);
    pkt[0] = (0 << 6) | (4 << 3) | 3;   /* LI 0, version 4, mode 3 (client) */
    put_ts(pkt + 40, t1_us);
}

static bool ts_zero(const uint8_t *p) { return rd32(p) == 0 && rd32(p + 4) == 0; }

omni_ntp_err_t omni_ntp_sample(const uint8_t *pkt, size_t n, int64_t t1_us, int64_t t4_us, bool clock_set,
                               omni_ntp_sample_t *out)
{
    if (n < OMNI_NTP_PACKET) return OMNI_NTP_SHORT;
    const unsigned li = pkt[0] >> 6, vn = (pkt[0] >> 3) & 7, mode = pkt[0] & 7;
    if (mode != 4 || vn < 1 || vn > 4) return OMNI_NTP_MODE;
    if (li == 3) return OMNI_NTP_LEAP;
    if (pkt[1] < 1 || pkt[1] > 15) return OMNI_NTP_STRATUM;
    if (ts_zero(pkt + 32) || ts_zero(pkt + 40)) return OMNI_NTP_ZERO;
    uint8_t mine[8];
    put_ts(mine, t1_us);
    if (memcmp(pkt + 24, mine, sizeof mine) != 0) return OMNI_NTP_ORIGIN;
    const int64_t t2 = omni_ntp_ts_unix_us(rd32(pkt + 32), rd32(pkt + 36));
    const int64_t t3 = omni_ntp_ts_unix_us(rd32(pkt + 40), rd32(pkt + 44));
    const int64_t held = t3 - t2, delay = (t4_us - t1_us) - held;
    /* 1 us of slack below 0: the timestamps' microsecond truncation */
    if (held < 0 || held >= OMNI_NTP_DELAY_MAX_US || delay < -1 || delay >= OMNI_NTP_DELAY_MAX_US) return OMNI_NTP_DELAY;
    const int64_t offset = ((t2 - t1_us) + (t3 - t4_us)) / 2;
    if (clock_set && (offset > OMNI_NTP_SANE_US || offset < -OMNI_NTP_SANE_US)) return OMNI_NTP_INSANE;
    out->offset_us = offset;
    out->delay_us = delay < 0 ? 0 : delay;
    out->stratum = pkt[1];
    return OMNI_NTP_OK;
}

int omni_ntp_best(const omni_ntp_sample_t *s, int n)
{
    int best = -1;
    for (int i = 0; i < n; i++)
        if (best < 0 || s[i].delay_us < s[best].delay_us) best = i;
    return best;
}

int64_t omni_ntp_timeout_ms(int64_t left_us)
{
    const int64_t ms = left_us > 0 ? (left_us + 999) / 1000 : 0;
    return ms < 1 ? 1 : ms;
}
