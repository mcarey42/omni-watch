#pragma once
/* The SNTP client's network half (R109): requests to one server of a pool and their replies, checked and chosen by
 * omni/ntp.h. Runs on the net task, with Wi-Fi joined. Applies nothing: net.c steps the clock. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/ntp.h"

#define NTP_CLIENT_SAMPLES 4   /* replies taken per sync; the one with the smallest round trip is the result */

typedef struct {
    int n;                                  /* replies taken (0: none, best is zeroed) */
    int tries, lost;                        /* requests sent; of them with no usable reply in time (unsent included) */
    int unsent, strays;                     /* sends that failed; replies skipped (another request's, another address) */
    char server[16];                        /* the address asked last */
    uint16_t rejected[OMNI_NTP__COUNT];     /* replies refused, by omni_ntp_sample's reason */
    omni_ntp_sample_t best;
    char delays[NTP_CLIENT_SAMPLES * 8];    /* the round trips taken, ms, "23/25/180/24" */
} ntp_client_result_t;

/* Up to 8 requests, 100 ms apart and 1 s each, to one address of servers[0], until NTP_CLIENT_SAMPLES replies pass;
 * three in a row without a usable reply move on to the next name's address (a pool member that does not answer, or
 * rate-limits, us); all within budget_ms. clock_set: the wall clock holds a synced date (omni_ntp_sample's sanity
 * bound). no_modem_sleep: Wi-Fi power save off for the queries (an AP buffering replies to a beacon makes the two
 * directions unequal), restored after. */
void ntp_client_sample(const char *const *servers, int n_servers, bool clock_set, bool no_modem_sleep,
                       uint32_t budget_ms, ntp_client_result_t *r);
