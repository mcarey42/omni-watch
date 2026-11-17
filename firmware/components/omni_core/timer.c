#include "omni/timer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TAU 6.28318530717958647692f
/* The pulses' periods (2 pi x 300 ms, 2 pi x 160 ms) in whole ms: the phase comes from the ms remainder, so a float never
 * holds the raw uptime (it would stutter after ~4.6 h); the seam moves the phase by < 0.0002 rad. */
#define PAUSE_PERIOD_MS 1885u
#define DONE_PERIOD_MS 1005u
#define MIDDOT "\xC2\xB7"

void omni_tmr_init(omni_tmr_t *t) { memset(t, 0, sizeof *t); t->mode = OMNI_TMR_IDLE; t->prev = OMNI_TMR_IDLE; }
void omni_tmr_reset(omni_tmr_t *t) { omni_tmr_init(t); }

float omni_tmr_rim_angle(int16_t x, int16_t y)
{
    float a = atan2f((float)(x - OMNI_TMR_CX), (float)(OMNI_TMR_CY - y));
    if (a < 0) a += TAU;
    if (a >= TAU) a = 0;   /* -0 or a rounding of a tiny negative */
    return a;
}

static bool on_rim(int16_t x, int16_t y)
{
    const int dx = x - OMNI_TMR_CX, dy = y - OMNI_TMR_CY;
    return dx * dx + dy * dy >= OMNI_TMR_RIM_R * OMNI_TMR_RIM_R;
}

static bool takes_wind(omni_tmr_mode_t m)
{
    return m == OMNI_TMR_IDLE || m == OMNI_TMR_ARMED || m == OMNI_TMR_PAUSED || m == OMNI_TMR_SET;
}

bool omni_tmr_claims(const omni_tmr_t *t, int16_t x0, int16_t y0)
{
    return on_rim(x0, y0) && takes_wind(t->mode);
}

bool omni_tmr_holds(const omni_tmr_t *t, int16_t x, int16_t y)
{
    return !on_rim(x, y) && t->mode != OMNI_TMR_IDLE && t->mode != OMNI_TMR_DONE;
}

#define MAX_MS ((uint32_t)OMNI_TMR_MAX_MIN * 60000u)
#define STEP_EPS 1e-3f   /* minutes: 0.012 degrees */

void omni_tmr_wind_start(omni_tmr_t *t)
{
    if (t->mode == OMNI_TMR_SET || !takes_wind(t->mode)) return;   /* a restart of a wind keeps the first prev */
    t->prev = t->mode;
    t->prev_min = t->set_min;
    t->wind_ms = t->mode == OMNI_TMR_PAUSED ? (t->remain_ms < MAX_MS ? t->remain_ms : MAX_MS)
               : t->mode == OMNI_TMR_ARMED ? (uint32_t)t->set_min * 60000u : 0u;
    t->wind_frac = 0;
    t->mode = OMNI_TMR_SET;
}

void omni_tmr_wind_by(omni_tmr_t *t, float dmin)
{
    if (t->mode != OMNI_TMR_SET || !(dmin == dmin)) return;   /* also refuses a NaN */
    float f = t->wind_frac + dmin;
    /* Whole 12 degree steps, truncated toward zero on either side (sign-symmetric): a step needs a whole minute of travel
     * past the last one, so the value has a one-minute detent and a thumb resting on a boundary never toggles it.
     * STEP_EPS only absorbs float error in a travel that is a whole number of minutes (a full turn is exactly 30). */
    while (f >= 1.0f - STEP_EPS) {
        if (t->wind_ms + 60000u > MAX_MS) {   /* the top: held, the overshoot dropped */
            t->wind_ms = MAX_MS;
            f = 0;
            break;
        }
        t->wind_ms += 60000u;
        f -= 1.0f;
    }
    while (f <= -1.0f + STEP_EPS) {
        if (t->wind_ms < 60000u) {
            t->wind_ms = 0;
            f = 0;
            break;
        }
        t->wind_ms -= 60000u;
        f += 1.0f;
    }
    t->wind_frac = f;
    t->set_min = (uint8_t)((t->wind_ms + 59999u) / 60000u);
}

uint32_t omni_tmr_set_ms(const omni_tmr_t *t) { return t->mode == OMNI_TMR_SET ? t->wind_ms : 0u; }

void omni_tmr_wind_end(omni_tmr_t *t)
{
    if (t->mode != OMNI_TMR_SET) return;
    if (t->wind_ms == 0) {
        omni_tmr_reset(t);   /* the arming, or the paused countdown, wound away */
        return;
    }
    if (t->prev == OMNI_TMR_PAUSED) {
        t->mode = OMNI_TMR_PAUSED;
        t->remain_ms = t->wind_ms;
    } else {
        t->mode = OMNI_TMR_ARMED;   /* never COUNT (R135) */
    }
    t->set_min = (uint8_t)((t->wind_ms + 59999u) / 60000u);
    t->wind_ms = 0;
    t->wind_frac = 0;
}

void omni_tmr_wind_cancel(omni_tmr_t *t)
{
    if (t->mode != OMNI_TMR_SET) return;
    t->mode = t->prev;
    t->set_min = t->prev_min;
    t->wind_ms = 0;
    t->wind_frac = 0;
}

uint32_t omni_tmr_remaining_ms(const omni_tmr_t *t, uint32_t now)
{
    if (t->mode != OMNI_TMR_COUNT) return t->remain_ms;
    const int32_t d = (int32_t)(t->end_ms - now);
    return d > 0 ? (uint32_t)d : 0;
}

uint64_t omni_tmr_elapsed_ms(const omni_tmr_t *t, uint32_t now)
{
    return t->sw_acc_ms + (t->mode == OMNI_TMR_SW ? (uint32_t)(now - t->sw_start_ms) : 0u);
}

void omni_tmr_tap(omni_tmr_t *t, uint32_t now)
{
    switch (t->mode) {
    case OMNI_TMR_IDLE: t->mode = OMNI_TMR_SW; t->sw_start_ms = now; t->sw_acc_ms = 0; break;
    case OMNI_TMR_SW: t->sw_acc_ms += (uint32_t)(now - t->sw_start_ms); t->mode = OMNI_TMR_SWPAUSE; break;
    case OMNI_TMR_SWPAUSE: t->mode = OMNI_TMR_SW; t->sw_start_ms = now; break;
    case OMNI_TMR_ARMED: t->mode = OMNI_TMR_COUNT; t->end_ms = now + (uint32_t)t->set_min * 60000u; break;
    case OMNI_TMR_COUNT:
        if ((int32_t)(now - t->end_ms) >= 0) {   /* the alarm is due: never pause at 0:00 */
            t->mode = OMNI_TMR_DONE;
            t->done_ms = t->end_ms;
            t->remain_ms = 0;
            t->alarm_pending = true;
            break;
        }
        t->remain_ms = omni_tmr_remaining_ms(t, now);
        t->mode = OMNI_TMR_PAUSED;
        break;
    case OMNI_TMR_PAUSED: t->mode = OMNI_TMR_COUNT; t->end_ms = now + t->remain_ms; break;
    case OMNI_TMR_DONE: omni_tmr_reset(t); break;
    case OMNI_TMR_SET: break;   /* a finger is on the rim */
    }
}

bool omni_tmr_tick(omni_tmr_t *t, uint32_t now)
{
    if (t->mode == OMNI_TMR_SW && (uint32_t)(now - t->sw_start_ms) >= 0x80000000u) {   /* keep the span under 24 days */
        t->sw_acc_ms += (uint32_t)(now - t->sw_start_ms);
        t->sw_start_ms = now;
    }
    if (t->mode == OMNI_TMR_COUNT && (int32_t)(now - t->end_ms) >= 0) {
        t->mode = OMNI_TMR_DONE;
        t->done_ms = t->end_ms;
        t->remain_ms = 0;
        t->alarm_pending = true;
    }
    const bool step = t->alarm_pending;
    t->alarm_pending = false;
    return step && t->mode == OMNI_TMR_DONE;
}

bool omni_tmr_alarm_at(const omni_tmr_t *t, uint32_t *end_ms)
{
    if (t->mode != OMNI_TMR_COUNT) return false;
    if (end_ms) *end_ms = t->end_ms;
    return true;
}

void omni_tmr_start_seconds(omni_tmr_t *t, uint32_t now, uint32_t s)
{
    if (s > OMNI_TMR_MAX_MIN * 60u) s = OMNI_TMR_MAX_MIN * 60u;
    omni_tmr_reset(t);
    t->mode = OMNI_TMR_COUNT;
    t->set_min = (uint8_t)((s + 59u) / 60u);
    t->end_ms = now + s * 1000u;
}

static uint8_t to_alpha(float a)
{
    const int v = (int)(a * 255.0f + 0.5f);
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void put(char *dst, size_t n, const char *src)
{
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

void omni_tmr_view(const omni_tmr_t *t, uint32_t now, omni_tmr_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->bead = -1;
    v->alpha = to_alpha(0.75f);
    const char *hint_run = "TAP " MIDDOT " PAUSE     HOLD " MIDDOT " RESET";
    switch (t->mode) {
    case OMNI_TMR_IDLE:
        put(v->big, sizeof v->big, "0:00");
        v->ink = 1;
        put(v->label, sizeof v->label, "TIMER");
        put(v->hint, sizeof v->hint, "TAP " MIDDOT " STOPWATCH     WIND RIM " MIDDOT " TIMER");
        break;
    case OMNI_TMR_SET:
    case OMNI_TMR_ARMED: {
        /* SET shows the value being wound: whole minutes, or a paused countdown's minutes and seconds (R144) */
        const uint32_t ms = t->mode == OMNI_TMR_SET ? t->wind_ms : (uint32_t)t->set_min * 60000u;
        const uint32_t s = (ms + 999u) / 1000u;   /* whole seconds up, as PAUSED shows them: no jump at the grab */
        snprintf(v->big, sizeof v->big, "%u:%02u", (unsigned)(s / 60u) % 100u, (unsigned)(s % 60u));
        put(v->label, sizeof v->label, "SET " MIDDOT " TAP TO START");
        if (t->mode == OMNI_TMR_ARMED) put(v->hint, sizeof v->hint, "TAP " MIDDOT " START     HOLD " MIDDOT " RESET");
        v->sector_min = (float)ms / 60000.0f;
        break;
    }
    case OMNI_TMR_COUNT:
    case OMNI_TMR_PAUSED: {
        const uint32_t rem = omni_tmr_remaining_ms(t, now);
        const uint32_t s = (rem + 999u) / 1000u;
        snprintf(v->big, sizeof v->big, "%u:%02u", (unsigned)(s / 60u) % 1000u, (unsigned)(s % 60u));
        put(v->label, sizeof v->label, t->mode == OMNI_TMR_PAUSED ? "PAUSED" : "COUNTDOWN");
        put(v->hint, sizeof v->hint, hint_run);
        if (s < 60) v->ink = 2;
        v->sector_min = (float)rem / 60000.0f;
        v->sector_red = rem < 60000u;
        if (t->mode == OMNI_TMR_PAUSED) v->alpha = to_alpha(0.45f + 0.15f * sinf((float)(now % PAUSE_PERIOD_MS) / 300.0f));
        break;
    }
    case OMNI_TMR_DONE:
        put(v->big, sizeof v->big, "0:00");
        v->ink = 3;
        put(v->label, sizeof v->label, "TIME");
        put(v->hint, sizeof v->hint, "TAP TO DISMISS");
        v->sector_min = 60;
        v->sector_red = true;
        v->alpha = to_alpha(0.45f + 0.4f * sinf((float)((uint32_t)(now - t->done_ms) % DONE_PERIOD_MS) / 160.0f));
        break;
    case OMNI_TMR_SW:
    case OMNI_TMR_SWPAUSE: {
        const uint64_t e = omni_tmr_elapsed_ms(t, now), s = e / 1000u;
        if (s >= 3600u) {
            snprintf(v->big, sizeof v->big, "%u:%02u:%02u", (unsigned)(s / 3600u % 10000u), (unsigned)(s / 60u % 60u),
                     (unsigned)(s % 60u));
        } else {
            snprintf(v->big, sizeof v->big, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
            snprintf(v->small, sizeof v->small, ".%02u", (unsigned)(e / 10u % 100u));
        }
        put(v->label, sizeof v->label, t->mode == OMNI_TMR_SWPAUSE ? "PAUSED" : "STOPWATCH");
        put(v->hint, sizeof v->hint, hint_run);
        v->bead = (float)(uint32_t)(e % 60000u) / 60000.0f;
        break;
    }
    }
}
