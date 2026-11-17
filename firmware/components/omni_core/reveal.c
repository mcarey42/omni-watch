#include "omni/reveal.h"
#include "omni/settings.h"

omni_rv_t omni_reveal_for(omni_wake_cause_t cause, bool reveal, uint8_t reveal_style)
{
    if (!reveal) return OMNI_RV_NONE;               /* a re-lift within 10 s, a touch from the glance: at rest */
    if (cause == OMNI_WAKE_FIRST) return OMNI_RV_DIAL;   /* a swipe, the picker, boot: the plain entry (spec §4) */
    switch (reveal_style) {                         /* a wake from sleep, a BOOT double: the setting's */
    case OMNI_REVEAL_IRIS: return OMNI_RV_IRIS;
    case OMNI_REVEAL_NONE: return OMNI_RV_DIAL;
    default: return OMNI_RV_FLIP;
    }
}

void omni_wake_track_sleep(omni_wake_track_t *w) { w->slept = true; }

omni_wake_cause_t omni_wake_track_wake(omni_wake_track_t *w)
{
    const omni_wake_cause_t c = !w->woke ? OMNI_WAKE_FIRST : w->slept ? OMNI_WAKE_AFTER_SLEEP : OMNI_WAKE_REPLAY;
    w->woke = true;
    w->slept = false;
    return c;
}
