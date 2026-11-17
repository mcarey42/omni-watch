#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int64_t last_ok;   /* unix seconds of last success; 0 = never */
    int64_t next_try;  /* unix seconds when the next attempt is due */
    uint8_t failures;  /* consecutive failures (saturating) */
    int32_t period_s;  /* interval after a success (init: 6 h; the clock calibration sets it) */
} omni_sync_t;

void omni_sync_init(omni_sync_t *s);
/* ok: next = now+period_s. fail: next = now+1m, 5m, 30m, then max(period_s, 6 h): dense is for successes. */
void omni_sync_result(omni_sync_t *s, int64_t now, bool ok);
/* A new success interval; a shorter one also pulls in a pending post-success retry (not a failure backoff). */
void omni_sync_set_period(omni_sync_t *s, int32_t period_s);
bool omni_sync_due(const omni_sync_t *s, int64_t now);
/* Never synced, or last success more than 24 h ago. */
bool omni_sync_stale(const omni_sync_t *s, int64_t now);

/* What the 11 o'clock lens shows (R48/C16): UNSET = the clock is not set (blinks); STALE = omni_sync_stale (steady,
 * dim); OFF = synced within 24 h (hidden). */
typedef enum { OMNI_LENS_OFF, OMNI_LENS_STALE, OMNI_LENS_UNSET } omni_lens_t;
omni_lens_t omni_sync_lens(int64_t now, const omni_sync_t *s);
