#include "omni/sync.h"
#include "omni/timefmt.h"

#define SYNC_PERIOD_S   21600
#define SYNC_STALE_S    86400

static const int32_t backoff_s[] = {60, 300, 1800};

void omni_sync_init(omni_sync_t *s)
{
    s->last_ok = 0;
    s->next_try = 0;
    s->failures = 0;
    s->period_s = SYNC_PERIOD_S;
}

void omni_sync_result(omni_sync_t *s, int64_t now, bool ok)
{
    if (ok) {
        s->failures = 0;
        s->last_ok = now;
        s->next_try = now + s->period_s;
        return;
    }
    if (s->failures < UINT8_MAX) {
        s->failures++;
    }
    unsigned idx = s->failures - 1u;
    s->next_try = now + (idx < sizeof backoff_s / sizeof backoff_s[0] ? backoff_s[idx] : (s->period_s > SYNC_PERIOD_S ? s->period_s : SYNC_PERIOD_S));
}

void omni_sync_set_period(omni_sync_t *s, int32_t period_s)
{
    s->period_s = period_s;
    if (s->last_ok != 0 && s->failures == 0 && s->next_try > s->last_ok + period_s) s->next_try = s->last_ok + period_s;
}

bool omni_sync_due(const omni_sync_t *s, int64_t now)
{
    return now >= s->next_try;
}

bool omni_sync_stale(const omni_sync_t *s, int64_t now)
{
    return s->last_ok == 0 || now - s->last_ok > SYNC_STALE_S;
}

omni_lens_t omni_sync_lens(int64_t now, const omni_sync_t *s)
{
    if (!omni_time_is_set(now)) return OMNI_LENS_UNSET;
    return omni_sync_stale(s, now) ? OMNI_LENS_STALE : OMNI_LENS_OFF;
}
