#pragma once
/* SNTP best-of-N (R109): the request and reply packets, each reply's offset and round-trip delay, its sanity checks,
 * and the choice between replies. Pure: net.c sends and receives, and applies the chosen offset through its clock step.
 *
 * Times are unix microseconds on the watch's wall clock: t1 when the request left, t4 when the reply came; the server's
 * t2 (received) and t3 (sent) from the reply. offset = ((t2 - t1) + (t3 - t4)) / 2 is what the watch's clock is behind
 * the server's (+ = behind), and delay = (t4 - t1) - (t3 - t2) is the network's round trip. The offset is exact when the
 * two directions take the same time; its error is at most delay / 2, so the reply with the smallest delay is kept.
 * lwIP's own SNTP (SNTP_COMP_ROUNDTRIP 0 in ESP-IDF) set the clock to t3, always behind by the reply's flight time. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OMNI_NTP_PACKET 48
#define OMNI_NTP_PORT 123
#define OMNI_NTP_UNIX_EPOCH 2208988800LL   /* 1900-01-01 to 1970-01-01, s */
#define OMNI_NTP_DELAY_MAX_US 1000000      /* a round trip this long or longer is not a usable sample */
#define OMNI_NTP_SANE_US (24LL * 3600 * 1000000)   /* a set clock this far off is a bogus reply, not drift */

typedef struct {
    int64_t offset_us;   /* + = the watch is behind the server */
    int64_t delay_us;
    uint8_t stratum;
} omni_ntp_sample_t;

typedef enum {
    OMNI_NTP_OK,
    OMNI_NTP_SHORT,      /* fewer than 48 bytes */
    OMNI_NTP_MODE,       /* not a server reply (mode 4) of version 1-4 */
    OMNI_NTP_LEAP,       /* leap indicator 3: the server is not synchronised */
    OMNI_NTP_STRATUM,    /* stratum 0 (a kiss-o'-death) or above 15 */
    OMNI_NTP_ZERO,       /* receive or transmit timestamp 0 */
    OMNI_NTP_ORIGIN,     /* not the reply to this request (its originate is not our transmit) */
    OMNI_NTP_DELAY,      /* round trip negative or >= OMNI_NTP_DELAY_MAX_US, or the server's own time negative */
    OMNI_NTP_INSANE,     /* the clock is set and the offset exceeds OMNI_NTP_SANE_US */
    OMNI_NTP__COUNT
} omni_ntp_err_t;

const char *omni_ntp_err_name(omni_ntp_err_t e);

/* An NTP timestamp (seconds since 1900, era by RFC 4330's rule: below 2^31 is era 1, from 2036) as unix us. */
int64_t omni_ntp_ts_unix_us(uint32_t sec, uint32_t frac);
/* The client request (version 4, mode 3) carrying t1_us as its transmit timestamp, which the server echoes back as
 * the reply's originate timestamp. */
void omni_ntp_request(uint8_t pkt[OMNI_NTP_PACKET], int64_t t1_us);
/* One reply to the request sent at t1_us, received at t4_us; clock_set: the watch's clock holds a synced date, so a
 * wild offset is rejected (an unset clock takes any). out is written only when OMNI_NTP_OK. */
omni_ntp_err_t omni_ntp_sample(const uint8_t *pkt, size_t n, int64_t t1_us, int64_t t4_us, bool clock_set,
                               omni_ntp_sample_t *out);
/* A receive timeout in whole ms for left_us of the window: rounded up, at least 1 (lwIP truncates SO_RCVTIMEO to ms,
 * and 0 ms means wait forever: re-review N1). */
int64_t omni_ntp_timeout_ms(int64_t left_us);
/* The index of the sample with the smallest delay (the first of equals), -1 when n is 0. */
int omni_ntp_best(const omni_ntp_sample_t *s, int n);
