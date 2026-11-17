#include "omni/travel.h"
#include "omni/globe_motion.h"
#include <math.h>
#include <string.h>

/* The mockup's travel (faces-omni.js 627-700), in double (a few dozen operations a step) with a fixed order; the tests
 * hold it to the mockup's numbers within 1e-3. */

#define TAU_D 6.28318530717958647692
#define G0_MS 250            /* the globe's spin: from 250 ms to g1 */
#define G1_MS 2650
#define G1_RM_MS 1200
#define CAP_IN0 3000         /* the caption: in over 3000-3300 ms of the travel, out over 9000-9600 ms away */
#define CAP_OUT0 9000
#define CAP_OUT1 9600
#define FLASH_MIN 0.005      /* drawDial draws the flash only above this */

const omni_dest_t OMNI_DEST[7] = {
    { "Philadelphia", 1776, 7, 4, 39.95f, -75.17f },
    { "Menlo Park", 1879, 10, 22, 40.56f, -74.34f },
    { "Rome", -44, 3, 15, 41.89f, 12.49f },
    { "Kitty Hawk", 1903, 12, 17, 36.02f, -75.67f },
    { "Hastings", 1066, 10, 14, 50.91f, 0.49f },
    { "Alexandria", -48, 8, 9, 31.2f, 29.92f },
    { "North Atlantic", 1912, 4, 14, 41.73f, -49.95f },
};
/* the upper-case names the caption shows (drawDial: name.toUpperCase()) */
static const char *const CAPTION[7] = { "PHILADELPHIA", "MENLO PARK", "ROME", "KITTY HAWK", "HASTINGS", "ALEXANDRIA",
                                        "NORTH ATLANTIC" };

static const int TURNS[OMNI_RINGS] = { 2, -3, 3, -1 };

static double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
static double ph(double t, double a, double b) { return clampd((t - a) / (b - a), 0, 1); }
static double in_out_cubic(double s) { return s < .5 ? 4 * s * s * s : 1 - pow(-2 * s + 2, 3) / 2; }
static double in_out_quart(double s) { return s < .5 ? 8 * s * s * s * s : 1 - pow(-2 * s + 2, 4) / 2; }

static uint32_t g1_ms(const omni_model_t *m) { return m->tr_reduced ? G1_RM_MS : G1_MS; }
/* reduced motion: no drift (the mockup drifts 0.55 deg over the first 250 ms and snaps back, a slip of its own) */
static double drift_dps(const omni_model_t *m) { return m->tr_reduced ? 0.0 : (double)OMNI_DRIFT_DPS; }
static double tau_of(const omni_model_t *m, uint32_t t) { return (double)(int32_t)(t - m->travel_t0); }

/* lonAt: the eastward spin (wrap180(to - from), made negative, less three turns) by inOutCubic over 250 ms-g1; before
 * 250 ms the drift */
static double lon_at(const omni_model_t *m, double tt)
{
    const double g1 = (double)g1_ms(m);
    double d = omni_wrap180(m->tr_to_lon - m->tr_from_lon);
    if (d > 0) d -= 360;   /* always eastward */
    const double total = d - (m->tr_reduced ? 0 : 3 * 360);
    if (tt < G0_MS) return m->tr_from_lon - drift_dps(m) * tt / 1000;
    return m->tr_from_lon + total * in_out_cubic(ph(tt, G0_MS, g1));
}

static double travel_lon(const omni_model_t *m, double tau)
{
    const double g1 = (double)g1_ms(m);
    return tau > g1 ? m->tr_to_lon - drift_dps(m) * (tau - g1) / 1000 : lon_at(m, tau);
}

bool omni_travel_active(const omni_model_t *m) { return m->travelling; }
bool omni_travel_away(const omni_model_t *m) { return m->away; }
bool omni_travel_to_home(const omni_model_t *m) { return m->travelling && m->to_home; }
const char *omni_travel_dest_name(const omni_model_t *m)
{
    return (m->travelling || m->away) && m->dest >= 0 && m->dest < 7 ? OMNI_DEST[m->dest].name : NULL;
}

double omni_travel_lon(const omni_model_t *m, uint32_t t)
{
    return m->travelling ? travel_lon(m, tau_of(m, t)) : NAN;
}

void omni_travel_start(omni_model_t *m, uint32_t t, bool to_home)
{
    if (!m->live || m->travelling || (to_home && !m->away)) return;
    if (m->have_prev) {   /* where the dial is (the mockup's lastVals, lastLon) */
        memcpy(m->tr_from, m->prev_val, sizeof m->tr_from);
        m->tr_from_lon = m->prev_lon;
    } else {
        memcpy(m->tr_from, m->today_v, sizeof m->tr_from);
        m->tr_from_lon = m->home_lon;
    }
    if (to_home) {
        m->dest = -1;
        memcpy(m->tr_to, m->today_v, sizeof m->tr_to);   /* and today's, step by step */
        m->tr_to_lon = m->home_lon;
    } else {
        const omni_dest_t *d = &OMNI_DEST[m->dest_ix % 7];
        m->dest = (int8_t)(m->dest_ix % 7);
        m->dest_ix = (uint8_t)((m->dest_ix + 1) % 7);
        omni_dial_vals(d->y, d->m, d->d, m->tr_to);
        m->tr_to_lon = d->lon;
    }
    m->tr_reduced = m->reduced;
    m->travelling = true;
    m->to_home = to_home;
    m->away = false;
    m->travel_t0 = t;
    m->changing = false;
    if (!m->skipped) {   /* the entry is over: no spin-up, settle or green ramp after the trip */
        m->skipped = true;
        m->skip_t = t;
    }
}

void omni_travel_abort_home(omni_model_t *m)
{
    if (m->travelling || m->away) m->have_prev = false;   /* the next wake starts at home, not where the trip was (M1) */
    m->travelling = false;
    m->away = false;
    m->dest = -1;
    m->anchored = false;   /* the globe home (less the drift), from the next step */
}

const char *omni_travel_caption(const omni_model_t *m, uint32_t t, float *alpha)
{
    double a = 0;
    if ((m->travelling && !m->to_home) || m->away) {
        const double tau = tau_of(m, t);
        a = m->travelling && tau < OMNI_TRAVEL_LAND_MS ? ph(tau, CAP_IN0, OMNI_TRAVEL_LAND_MS)
                                                       : 1 - ph(tau - (double)g1_ms(m), CAP_OUT0, CAP_OUT1);
    }
    if (alpha) *alpha = (float)a;
    return a > 0 && m->dest >= 0 && m->dest < 7 ? CAPTION[m->dest] : NULL;
}

/* the away state: the destination's date, the globe on it drifting from its arrival, green, the pip pulsing */
static void away_frame(const omni_model_t *m, uint32_t t, omni_dial_state_t *out)
{
    const omni_dest_t *d = &OMNI_DEST[m->dest];
    const double tau = tau_of(m, t);
    omni_dial_vals(d->y, d->m, d->d, out->val);
    memcpy(out->sel_old, out->val, sizeof out->sel_old);
    memcpy(out->sel_new, out->val, sizeof out->sel_new);
    out->mark_old = 1;
    out->mark_new = 0;
    out->lon_deg = omni_lon_wrap(travel_lon(m, tau));
    out->green = 1;
    out->flash = 0;
    out->marker = true;
    out->marker_lat = d->lat;
    out->marker_lon = d->lon;
    out->marker_a = 0.9f;
    out->marker_pulse = (float)(.5 + .5 * sin(tau / 300));
}

/* a travel's frame at tau (< 3300); true: out->speed holds the ring blur */
static bool travel_frame(omni_model_t *m, uint32_t t, const float today[OMNI_RINGS], omni_dial_state_t *out)
{
    const bool rm = m->tr_reduced;
    const double tau = tau_of(m, t);
    if (m->to_home) memcpy(m->tr_to, today, sizeof m->tr_to);   /* home is today's date, even past midnight */
    for (int i = 0; i < OMNI_RINGS; i++) {
        const double n = (double)OMNI_RING_GEOM[i].slots;
        const double a0 = 380 + i * 80, a1 = (rm ? 900 : 1900) + i * 140;
        const double e = in_out_quart(ph(tau, a0, a1));
        const double total = ((double)m->tr_to[i] - (double)m->tr_from[i]) + (rm ? 0 : TURNS[i] * n);
        double v = (double)m->tr_from[i] + total * e;
        if (tau < a0 && !rm) v += sin(tau / 22 + i) * .08 * ph(tau, 0, 200);   /* unlock jitter */
        if (tau > a1 && !rm)   /* the detent click */
            v += .22 * sin((tau - a1) / 55) * exp(-(tau - a1) / 110) * (total < 0 ? -1 : 1);
        out->val[i] = (float)v;
        /* the mockup's ring blur, total (e(tau) - e(tau - 16)) clamped to +-slots / 6, over the model's step (T12 M1,
         * R105): slots per 32 ms step, as every other speed, so the fidelity rule sees the whirl's real motion */
        const double blur = total * (e - in_out_quart(ph(tau - OMNI_MODEL_STEP_MS, a0, a1)));
        const double lim = n / 6 * (OMNI_MODEL_STEP_MS / 16.0);
        out->speed[i] = (float)-clampd(blur, -lim, lim);
    }
    memcpy(out->sel_old, m->tr_from, sizeof out->sel_old);
    memcpy(out->sel_new, m->tr_to, sizeof out->sel_new);
    out->mark_old = (float)(1 - ph(tau, 300, 650));
    out->mark_new = (float)ph(tau, 1750, 2500);
    /* the hands swing back and return: 0 -> 1 -> 0 */
    const double hs = in_out_cubic(ph(tau, 380, 2250)), sw = rm ? 0 : 4 * hs * (1 - hs);
    out->min_rad = (float)((double)out->min_rad - 1.5 * TAU_D * sw);
    out->hour_rad = (float)((double)out->hour_rad - .25 * TAU_D * sw);
    out->lon_deg = omni_lon_wrap(travel_lon(m, tau));
    /* the lights: red blinks while unstable, then fades; green goes out, then lights on the arrival; the flash */
    const double blink = ((int32_t)floor(tau / 140) % 2) ? 1 : .15;
    out->red = (float)(tau < 2230 ? blink * (tau < 200 ? ph(tau, 0, 60) : 1) : fmax(0, 1 - (tau - 2230) / 150));
    out->green = (float)(tau < 2700 ? fmax(0, 1 - tau / 200) : ph(tau, 2850, 3150));
    double fl = rm ? 0 : tau < 2040 ? 0 : tau < 2230 ? pow((tau - 2040) / 190, 2) : exp(-(tau - 2230) / 210);
    fl *= .92;
    out->flash = fl > FLASH_MIN ? (float)fl : 0;
    out->marker = true;
    out->marker_lat = m->dest >= 0 ? OMNI_DEST[m->dest].lat : (float)m->home_lat;
    out->marker_lon = (float)m->tr_to_lon;
    out->marker_a = (float)ph(tau, 2700, 3000);
    out->marker_pulse = (float)(.5 + .5 * sin(tau / 300));
    return !rm;   /* reduced motion: no blur, the speeds are the steps' own */
}

bool omni_travel_step(omni_model_t *m, uint32_t t, const float today[OMNI_RINGS], omni_dial_state_t *out)
{
    if (m->travelling) {
        const double tau = tau_of(m, t);
        if (tau < OMNI_TRAVEL_LAND_MS) return travel_frame(m, t, today, out);
        m->travelling = false;   /* landed */
        if (m->to_home) {        /* the rest state the model computed (today, green, the home pip), on the anchor */
            const uint32_t arrived = m->travel_t0 + g1_ms(m);
            omni_model_anchor(m, arrived, m->tr_to_lon);
            out->lon_deg = omni_lon_wrap(travel_lon(m, tau));
            m->dest = -1;
            return false;
        }
        m->away = true;
    }
    if (!m->away || m->dest < 0) return false;
    if ((int32_t)(t - (m->travel_t0 + g1_ms(m))) > (int32_t)OMNI_TRAVEL_STAY_MS) {   /* the Omni won't let you stay */
        omni_travel_start(m, t, true);
        if (m->travelling) return travel_frame(m, t, today, out);
    }
    away_frame(m, t, out);
    return false;
}
