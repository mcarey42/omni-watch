/* The hourly chime's scheduler (fk/chime.h). */
#include "fk/chime.h"
#include <time.h>
#include "omni/timefmt.h"

/* Seconds into the local hour, or -1 if the time cannot be broken down. */
static int into_hour(int64_t t, int *hour)
{
    time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return -1;
    if (hour) *hour = tm.tm_hour;
    return tm.tm_min * 60 + tm.tm_sec;
}

int64_t fk_chime_boundary(int64_t now_s)
{
    int64_t b = now_s;
    /* Normally one step. A zone whose clock moved by a part-hour inside the last hour needs another. */
    for (int i = 0; i < 4; i++) {
        int r = into_hour(b, NULL);
        if (r < 0) return 0;
        if (r == 0) return b;
        b -= r;
    }
    return into_hour(b, NULL) == 0 ? b : 0;        /* never a non-boundary */
}

int64_t fk_chime_next(int64_t now_s)
{
    int64_t c = now_s;
    for (int i = 0; i < 4; i++) {
        int r = into_hour(c, NULL);
        if (r < 0) break;
        c += 3600 - r;
        if (into_hour(c, NULL) == 0) break;
    }
    return c;
}

uint8_t fk_chime_pings(int hour24)
{
    int h = ((hour24 % 12) + 12) % 12;
    return (uint8_t)(h == 0 ? 12 : h);
}

bool fk_chime_quiet(int hour24, uint8_t from_h, uint8_t to_h)
{
    if (from_h == to_h) return false;
    if (from_h < to_h) return hour24 >= from_h && hour24 < to_h;
    return hour24 >= from_h || hour24 < to_h;
}

fk_chime_verdict_t fk_chime_due(fk_chime_state_t *st, int64_t now_s, const fk_chime_ctx_t *c)
{
    fk_chime_verdict_t v = { FK_CH_WAIT, 0, 0, 0 };
    if (!st || !c) { v.why = FK_CH_ERROR; return v; }
    if (!c->chime_on) { v.why = FK_CH_OFF; return v; }
    if (!c->time_set || !omni_time_is_set(now_s)) { v.why = FK_CH_UNSET; return v; }
    /* M2: the sound mirror is still its boot default: decide nothing (a SILENT would mark the hour), look again in 1 s */
    if (c->sound_pending) { v.why = FK_CH_BOOT; v.next_s = now_s + 1; return v; }

    int64_t b = fk_chime_boundary(now_s);
    int hour = 0;
    if (b == 0 || into_hour(b, &hour) != 0) { v.why = FK_CH_ERROR; return v; }
    v.next_s = fk_chime_next(now_s);
    if (st->last_b > b + FK_CHIME_STEP_BACK_S) st->last_b = 0;   /* the clock was corrected backward */
    if (b <= st->last_b) return v;                 /* handled, or the clock stepped back across a played hour */

    st->last_b = b;
    if (now_s - b >= FK_CHIME_GRACE_S) { v.why = FK_CH_LATE; return v; }
    if (c->provisioning) { v.why = FK_CH_PROV; return v; }
    if (!c->sound_on || c->volume == 0) { v.why = FK_CH_SILENT; return v; }

    v.why = FK_CH_PLAY;
    v.pings = fk_chime_pings(hour);
    v.gain_pct = fk_chime_quiet(hour, c->quiet_from_h, c->quiet_to_h) ? FK_CHIME_QUIET_PCT : 100;
    return v;
}

uint32_t fk_chime_arm_ms(int64_t now_s, const fk_chime_verdict_t *v, bool chime_on)
{
    if (!chime_on || !v) return 0;
    if (v->next_s == 0) return FK_CHIME_ARM_CAP_MS;
    int64_t d = v->next_s - now_s;
    if (d <= 0) return 1;
    if (d >= FK_CHIME_ARM_CAP_MS / 1000) return FK_CHIME_ARM_CAP_MS;
    return (uint32_t)(d * 1000);
}

bool fk_chime_sched_at(fk_chime_sched_t *s, int64_t real_now_s, uint32_t n_s)
{
    if (!s || n_s > FK_CHIME_AT_MAX_S) return false;
    int64_t off = 0;
    if (n_s) {
        const int64_t at = real_now_s + (int64_t)n_s;
        const int64_t b = fk_chime_next(at - 1);   /* the earliest top of hour >= at */
        if (fk_chime_boundary(b) != b || b < at) return false;
        off = b - at;
    }
    if (off != s->off_s) {
        s->off_s = off;
        s->st.last_b = 0;
    }
    return true;
}

uint32_t fk_chime_sched_tick(fk_chime_sched_t *s, int64_t real_now_s, const fk_chime_ctx_t *c)
{
    if (!s) return 0;
    const int64_t now = real_now_s + s->off_s;
    s->last = fk_chime_due(&s->st, now, c);
    if (s->last.why == FK_CH_ERROR) return FK_CHIME_ARM_CAP_MS;   /* nothing done; look again within a minute */
    return fk_chime_arm_ms(now, &s->last, c && c->chime_on);
}
