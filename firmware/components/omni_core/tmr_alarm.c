#include "omni/tmr_alarm.h"
#include <string.h>

uint32_t omni_tmr_retry_us(bool posted, uint32_t fails)
{
    if (posted) return 0;
    return fails <= OMNI_TMR_RETRY_FAST_N ? OMNI_TMR_RETRY_FAST_US : OMNI_TMR_RETRY_SLOW_US;
}

bool omni_tmr_woke(bool valid, uint32_t last, uint32_t now)
{
    return valid && (int32_t)(now - last) >= (int32_t)OMNI_TMR_WAKE_GAP_MS;
}

bool omni_tmr_bell_silence(omni_tmr_bell_t *b)
{
    const bool was = b->ringing;
    b->ringing = false;
    return was;
}

void omni_tmr_bell_poll(omni_tmr_bell_t *b, omni_tmr_mode_t mode, uint32_t done_ms, bool stepped, bool face_awake,
                        bool woke, uint32_t now, omni_tmr_bell_out_t *o)
{
    memset(o, 0, sizeof *o);
    if (mode != OMNI_TMR_DONE) {
        o->silence = omni_tmr_bell_silence(b);
        return;
    }
    if (stepped) b->handled = false;
    if (face_awake) {
        o->handled_now = !b->handled;
        b->handled = true;
    } else if (!b->handled && woke) {
        o->post_alarm = true;
    }
    if ((int32_t)(now - done_ms) >= (int32_t)OMNI_TMR_RING_FOR_MS) {   /* the 30 s cap (a late step stays silent) */
        o->silence = omni_tmr_bell_silence(b);
        return;
    }
    if (stepped) {
        o->ring = true;
    } else if (face_awake && (int32_t)(now - b->last_ring) >= (int32_t)OMNI_TMR_RING_EVERY_MS) {
        o->ring = true;
        o->keep = true;
    }
    if (o->ring) {
        b->last_ring = now;
        b->ringing = true;
    }
}

bool omni_tmr_parse_seconds(const char *txt, uint32_t *s)
{
    if (txt == NULL || txt[0] == '\0') return false;
    uint32_t v = 0;
    size_t n = 0;
    for (const char *p = txt; *p; p++, n++) {
        if (*p < '0' || *p > '9' || n >= 9) return false;
        v = v * 10u + (uint32_t)(*p - '0');
    }
    *s = v;
    return true;
}
