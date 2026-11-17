#include "omni/power.h"

#define REVEAL_SKIP_MS 10000u
#define GLANCE_MS 10000u
#define FACE_DOWN_MAX_S 5u

static omni_pwr_action_t wake(omni_pwr_sm_t *p, uint32_t t)
{
    omni_pwr_action_t act = (p->woke && (t - p->t_last_wake) < REVEAL_SKIP_MS)
                                ? PA_WAKE_SKIP_REVEAL : PA_WAKE_FULL;
    p->st = PWR_AWAKE;
    p->t_state = t;
    p->t_last_touch = t;
    p->t_last_wake = t;
    p->woke = true;
    return act;
}

omni_pwr_action_t omni_pwr_on_lift(omni_pwr_sm_t *p, uint32_t t)
{
    if (p->st == PWR_AWAKE) {
        p->t_last_touch = t;
        return PA_NONE;
    }
    return wake(p, t);
}

/* PWR wakes from ASLEEP and from the glance (it looks like a dimmed screen), and sleeps an awake watch. */
omni_pwr_action_t omni_pwr_on_button(omni_pwr_sm_t *p, uint32_t t)
{
    if (p->st != PWR_AWAKE) return wake(p, t);
    p->st = PWR_ASLEEP;
    p->t_state = t;
    return PA_TO_SLEEP;
}

omni_pwr_action_t omni_pwr_on_touch(omni_pwr_sm_t *p, uint32_t t)
{
    switch (p->st) {
    case PWR_GLANCE:
        p->st = PWR_AWAKE;
        p->t_state = t;
        p->t_last_touch = t;
        return PA_WAKE_SKIP_REVEAL;
    case PWR_AWAKE:
        p->t_last_touch = t;
        return PA_NONE;
    default:
        return PA_NONE;
    }
}

/* Not wake(): the alarm is no lift, so t_last_wake and woke stay as they were (the 10 s reveal rule sees only lifts
 * and PWR), and it never plays the reveal. */
omni_pwr_action_t omni_pwr_on_alarm(omni_pwr_sm_t *p, uint32_t t)
{
    p->t_last_touch = t;
    if (p->st == PWR_AWAKE) return PA_NONE;
    p->st = PWR_AWAKE;
    p->t_state = t;
    return PA_WAKE_SKIP_REVEAL;
}

omni_pwr_action_t omni_pwr_on_alarm_keep(omni_pwr_sm_t *p, uint32_t t)
{
    if (p->st == PWR_AWAKE) p->t_last_touch = t;
    return PA_NONE;
}

uint32_t omni_pwr_idle_ref(const omni_pwr_sm_t *p)
{
    return (int32_t)(p->t_last_touch - p->t_state) > 0 ? p->t_last_touch : p->t_state;
}

/* Signed: a hold set just after the caller sampled now_ms (since a little ahead of now) has not expired. */
bool omni_hold_expired(uint32_t since_ms, uint32_t now_ms, uint32_t max_ms)
{
    return (int32_t)(now_ms - since_ms) >= (int32_t)max_ms;
}

bool omni_wifi_hold_effective(bool held, uint32_t asleep_since_ms, uint32_t now_ms)
{
    return held && !omni_hold_expired(asleep_since_ms, now_ms, OMNI_WIFI_HOLD_MAX_MS);
}

omni_pwr_action_t omni_pwr_tick(omni_pwr_sm_t *p, uint32_t t, uint8_t sleep_s, omni_pose_t pose)
{
    if (p->st == PWR_AWAKE) {
        uint32_t ref = omni_pwr_idle_ref(p);
        uint32_t s = sleep_s;
        if (pose == POSE_FACE_DOWN && s > FACE_DOWN_MAX_S) s = FACE_DOWN_MAX_S;
        if ((t - ref) >= s * 1000u) {
            p->st = PWR_GLANCE;
            p->t_state = t;
            return PA_TO_GLANCE;
        }
    } else if (p->st == PWR_GLANCE) {
        if ((t - p->t_state) >= GLANCE_MS) {
            p->st = PWR_ASLEEP;
            p->t_state = t;
            return PA_TO_SLEEP;
        }
    }
    return PA_NONE;
}
