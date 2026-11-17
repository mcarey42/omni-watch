#include "omni/vbus.h"

/* In the fallback: OMNI_VBUS_AGREE good reads in a row that agree end it, at their value. */
static void fallback_read(omni_vbus_filter_t *f, bool v)
{
    if (f->good == 0 || f->cand != v) {
        f->cand = v;
        f->good = 0;
    }
    if (++f->good < OMNI_VBUS_AGREE) return;
    f->failsafe = false;
    f->known = true;
    f->state = f->confirmed = v;
    f->good = f->count = 0;
}

bool omni_vbus_feed(omni_vbus_filter_t *f, omni_vbus_read_t r)
{
    if (r == OMNI_VBUS_READ_FAIL) { /* no information: no change, a pending count kept */
        f->good = 0;            /* but a fallback's recovery needs its good reads in a row */
        if (f->fails < OMNI_VBUS_FAIL_MAX) f->fails++;
        if (f->fails >= OMNI_VBUS_FAIL_MAX && !f->failsafe) { /* not for ever: assume battery (light sleep only) */
            f->failsafe = true;
            f->state = false;
            f->count = f->good = 0;
        }
        return f->state;
    }
    f->fails = 0;
    const bool v = r == OMNI_VBUS_PRESENT;
    if (f->failsafe) {
        fallback_read(f, v);
    } else if (!f->known) {
        f->known = true;
        f->state = f->confirmed = v;
        f->count = 0;
    } else if (v == f->state) {
        f->count = 0;
    } else {
        if (f->count == 0 || f->cand != v) {
            f->cand = v;
            f->count = 0;
        }
        if (++f->count >= OMNI_VBUS_AGREE) {
            f->state = f->confirmed = v;
            f->count = 0;
        }
    }
    return f->state;
}

bool omni_vbus_reenumerate(bool cfg_vbus, bool cfg_assumed, bool now) { return !cfg_vbus && cfg_assumed && now; }
