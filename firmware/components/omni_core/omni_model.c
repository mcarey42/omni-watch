#include "omni/omni_model.h"
#include "omni/globe_motion.h"
#include "omni/iris.h"
#include "omni/lid.h"
#include "omni/travel.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Single precision with a fixed evaluation order; the mockup's numbers are doubles and the tests hold it to 1e-3. */

#define TAU_F 6.28318530717958647692f
#define TAU_D 6.28318530717958647692

const omni_entry_t OMNI_ENTRY_DIAL = { 150, 2600, 2300, 540.0f, true };
/* the spin starts 180 ms into the swing and green comes 3420 ms into it, on either timeline (lid.h): 330 / 3570 on
 * the device's, the mockup's 760 / 4000 with OMNI_FLIP_MOCKUP */
const omni_entry_t OMNI_ENTRY_FLIP = { OMNI_FLIP_SWING_T0_MS + 180, 3600, OMNI_FLIP_SWING_T0_MS + 3420, 990.0f, false };
const omni_entry_t OMNI_ENTRY_IRIS = { 620, 3600, 3900, 990.0f, false };

static const float SETTLE_K[OMNI_RINGS] = { 1.4f, -2.2f, 3.5f, -1.2f };   /* D8 */
#define SETTLE_T0 80u
#define SETTLE_T1 1100u
#define BLINK_MS_PER_RAD 380.0   /* the shell's lens blink: sin(T / 380) */
#define STALE_RED 0.35f

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* ph(t, a, b): 0 before a, 1 from b, linear between; times by signed difference */
static float ph(uint32_t t, uint32_t a, uint32_t b)
{
    const int32_t d = (int32_t)(t - a), span = (int32_t)(b - a);
    if (d <= 0) return 0;
    if (d >= span) return 1;
    return (float)d / (float)span;
}

static float out_back(float s)
{
    const float c1 = 1.5f, c3 = c1 + 1;
    const float u = s - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}

void omni_dial_vals(int year, int month, int day, float out[OMNI_RINGS])
{
    int cent, yy;
    if (year > 0) {
        cent = 20 + year / 100;
        yy = year % 100;
    } else {
        const int b = -year;
        cent = 19 - b / 100;
        yy = b % 100;
    }
    out[0] = (float)(month - 1);
    out[1] = (float)day;
    out[2] = (float)yy;
    out[3] = (float)(cent < 0 ? 0 : cent > 40 ? 40 : cent);
}

void omni_hand_angles(int h, int m, int s, float *hour_rad, float *min_rad)
{
    const double mins = m + s / 60.0, hrs = (h % 12) + mins / 60.0;
    *hour_rad = (float)(floor(hrs / 12.0 * TAU_D * 720.0 + 0.5) / 720.0);   /* R90: the minute hand's 1/720 rad step */
    *min_rad = (float)(floor(mins / 60.0 * TAU_D * 720.0 + 0.5) / 720.0);   /* Math.round: half up */
}

float omni_settle(uint32_t t_ms, uint32_t t0, uint32_t t1) { return 1 - out_back(ph(t_ms, t0, t1)); }

void omni_ring_change(const float from[OMNI_RINGS], const float to[OMNI_RINGS], bool forward, float s_settle,
                      float out[OMNI_RINGS])
{
    for (int i = 0; i < OMNI_RINGS; i++) {
        const float n = (float)OMNI_RING_GEOM[i].slots;
        float d = to[i] - from[i];
        if (forward) {   /* from may be unreduced (a travel's turns, an overshoot): reduce, then forward */
            d = fmodf(d, n);
            if (d < 0) d += n;
        } else {
            d = fmodf(d, n);
            if (d > n / 2) d -= n;
            else if (d < -n / 2) d += n;
        }
        const float target = from[i] + d;
        out[i] = target + (from[i] - target) * s_settle;
    }
}

float omni_spin_off(uint32_t t, uint32_t t0, uint32_t dur, float k_deg)
{
    if ((int32_t)(t - t0) <= 0) return k_deg;
    const float s = clampf((float)(int32_t)(t - t0) / (float)dur, 0, 1);
    const float q = 1 - powf(s, 1.55f);
    return k_deg * q * q * q;
}

void omni_model_init(omni_model_t *m, double home_lat, double home_lon)
{
    memset(m, 0, sizeof *m);
    m->home_lat = home_lat;
    m->home_lon = home_lon;
    m->dest = -1;   /* home (T12 M5) */
}

void omni_model_set_home(omni_model_t *m, double home_lat, double home_lon)
{
    m->home_lat = home_lat;
    m->home_lon = home_lon;
    m->have_prev = false;   /* the next wake starts at the new home, not where the old one's globe was */
}

void omni_model_wake(omni_model_t *m, uint32_t t, const omni_entry_t *entry, bool reduced)
{
    /* Final review M1: a wake without a reveal (a screen closed over the dial, a re-lift or a touch from the glance)
     * goes on from the longitude last shown, or the globe would jump back to home (the frame before is still on
     * screen, R102). A sleep that aborted a travel cleared have_prev (omni_travel_abort_home): that one is at home. */
    const bool go_on = entry == NULL && m->have_prev;
    const double shown = (double)m->prev_lon;
    m->entry = entry;
    m->reduced = reduced;
    m->skipped = false;
    m->wake_t = t;
    m->have_today = false;
    m->changing = false;
    m->loupe_on = false;
    m->have_prev = false;
    m->live = false;
    m->anchored = false;
    m->travelling = false;   /* a wake is at home (sleep aborted any travel); the destinations keep cycling */
    m->away = false;
    if (go_on) omni_model_anchor(m, t, shown);
}

float omni_lon_wrap(double lon)
{
    const float f = (float)omni_wrap180(lon);
    return f >= 180.0f ? f - 360.0f : f;   /* a double just under 180 rounds up in float */
}

static float out_cubic(float s) { const float u = 1 - s; return 1 - u * u * u; }

#define ANCHOR_EASE_MS 500u

/* The globe at home, unwrapped: the entry's spin-up offset and the drift, or (anchored) the anchor's eased offset and
 * the drift from it. */
static double home_lon(const omni_model_t *m, uint32_t t)
{
    const double drift = m->reduced ? 0.0 : (double)OMNI_DRIFT_DPS;
    if (m->anchored) {
        const int32_t da = (int32_t)(t - m->anchor_t);
        const float k = 1 - out_cubic(ph(t, m->anchor_t, m->anchor_t + ANCHOR_EASE_MS));
        return m->anchor_lon + m->anchor_off * (double)k - drift * (double)(da > 0 ? da : 0) / 1000.0;
    }
    const omni_entry_t *e = m->entry;
    const uint32_t spin_t0 = e ? m->wake_t + e->spin_t0 : m->wake_t;
    double lon = m->home_lon;
    if (e && !m->reduced && !m->skipped) lon += omni_spin_off(t, spin_t0, e->spin_dur, e->spin_k);
    const int32_t dt = (int32_t)(t - spin_t0);
    return lon - drift * (double)(dt > 0 ? dt : 0) / 1000.0;
}

void omni_model_skip(omni_model_t *m, uint32_t t)
{
    if (!m->skipped && !m->anchored) {   /* T5 m7: the spin-up's remaining offset eases out, no jump */
        const double now = home_lon(m, t);
        m->skipped = true;
        m->anchor_t = t;
        m->anchor_lon = home_lon(m, t);   /* without the offset now (skipped): home less the drift so far */
        m->anchor_off = omni_wrap180(now - m->anchor_lon);
        m->anchored = true;
    }
    m->skipped = true;
    m->skip_t = t;
}

void omni_model_anchor(omni_model_t *m, uint32_t t, double lon)
{
    m->anchored = true;
    m->anchor_t = t;
    m->anchor_lon = lon;
    m->anchor_off = 0;
}

/* ms since the wake, 0 before it */
static uint32_t since_wake(const omni_model_t *m, uint32_t t)
{
    const int32_t d = (int32_t)(t - m->wake_t);
    return d > 0 ? (uint32_t)d : 0;
}

bool omni_model_reveal_done(const omni_model_t *m, uint32_t t)
{
    const omni_entry_t *e = m->entry;
    if (m->skipped) return (int32_t)(t - m->skip_t) >= 0;
    /* before the wake counts as its start (since_wake 0): the lid closed, the iris shut */
    if (e == &OMNI_ENTRY_FLIP)
        return omni_flip_theta(since_wake(m, t), m->reduced) >= omni_hinge_gone_rad(&OMNI_LID_HINGE);
    if (e == &OMNI_ENTRY_IRIS) {
        float ap, phi;
        omni_iris_at(since_wake(m, t), m->reduced, &ap, &phi);
        return ap / (float)OMNI_IRIS_AP_OPEN >= 1;
    }
    return true;   /* DIAL or none: no lid, no iris */
}

bool omni_model_red_ready(const omni_model_t *m, uint32_t t)
{
    return (int32_t)(t - m->wake_t) >= 400 && omni_model_reveal_done(m, t - 400u);
}

bool omni_model_revealing(const omni_model_t *m, uint32_t t)
{
    const omni_entry_t *e = m->entry;
    if (e == NULL || m->skipped) return false;
    if (!omni_model_reveal_done(m, t)) return true;   /* the lid or iris is on */
    if (m->reduced) return false;                      /* no spin, no settle: nothing else moves */
    uint32_t end = (uint32_t)e->spin_t0 + e->spin_dur;
    if (e->settle && SETTLE_T1 > end) end = SETTLE_T1;
    return (int32_t)(t - m->wake_t) < (int32_t)end;   /* green's ramp alone is not worth a swallowed tap (m1) */
}

void omni_model_loupe(omni_model_t *m, uint32_t t)
{
    /* the mockup: loupeT = (t - loupeT < 3600) ? t - 3600 : t, so a tap while it is opening or open starts the close */
    if (m->loupe_on && (int32_t)(t - m->loupe_t) < 3600) m->loupe_t = t - 3600u;
    else {
        m->loupe_on = true;
        m->loupe_t = t;
    }
}

omni_hit_t omni_hit(int16_t x, int16_t y, bool loupe_open)
{
    /* faces-omni.js tap(): LENS_R 215 at -30 / +30 deg from 12 about (233, 233), 38 px; MAG +-(12, 10 / 8) */
    static const float RX = 125.5f, GX = 340.5f, LY = 46.8f;
    const float dr = hypotf((float)x - RX, (float)y - LY), dg = hypotf((float)x - GX, (float)y - LY);
    if (dr < 38) return OMNI_HIT_RED;
    if (dg < 38) return OMNI_HIT_GREEN;
    if (x > 195 && x < 271 && y > 8 && y < 130) return OMNI_HIT_MAG;
    if (loupe_open && abs(x - 233) < 50 && abs(y - 233) < 100) return OMNI_HIT_LOUPE_OFF;
    return OMNI_HIT_NONE;
}

/* a ring's cyclic distance, slots */
static float cyc(float d, float n)
{
    d = fabsf(fmodf(d, n));
    return d > n / 2 ? n - d : d;
}

static float smooth01(float x) { x = clampf(x, 0, 1); return x * x * (3 - 2 * x); }

void omni_model_step(omni_model_t *m, uint32_t t, bool set, const struct tm *local, omni_lens_t lens,
                     omni_dial_state_t *out)
{
    const omni_entry_t *e = m->entry;
    const bool rm = m->reduced;
    const uint32_t w = m->wake_t;
    memset(out, 0, sizeof *out);

    /* today's values and the hands */
    float today[OMNI_RINGS];
    if (set && local) {
        omni_dial_vals(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, today);
        omni_hand_angles(local->tm_hour, local->tm_min, local->tm_sec, &out->hour_rad, &out->min_rad);
    } else {
        omni_dial_vals(2000, 1, 1, today);
    }
    const bool live = set && local;

    /* the rings: today, a rollover in flight, or the wake settle */
    if (live) {
        const int32_t key = (local->tm_year + 1900) * 512 + (local->tm_mon + 1) * 32 + local->tm_mday;
        if (!m->have_today) {
            m->have_today = true;
        } else if (key != m->today_key) {
            /* T5 m6: from the displayed rings (a change inside another's settle, or just after a travel), not the
             * last target */
            m->changing = true;
            m->change_fwd = key > m->today_key;
            m->change_t = t;
            memcpy(m->change_from, m->have_prev ? m->prev_val : m->today_v, sizeof m->change_from);
        }
        if (m->travelling || m->away) m->changing = false;   /* travel shows its own rings; home lands on today */
        m->today_key = key;
        memcpy(m->today_v, today, sizeof today);
    } else {
        m->have_today = false;
        m->changing = false;
        if (m->travelling || m->away) {   /* the clock went unset: no travel without one */
            const bool shown = m->have_prev;   /* abort_home clears it (M1: a sleep's abort comes home) */
            omni_travel_abort_home(m);
            /* T12 M3: the globe drifts on from where it is shown, not from the wake's home formula (no jump) */
            if (shown) omni_model_anchor(m, t, (double)m->prev_lon);
        }
    }
    m->live = live;
    memcpy(out->val, today, sizeof today);
    memcpy(out->sel_old, today, sizeof today);
    memcpy(out->sel_new, today, sizeof today);
    if (live) {
        if (m->changing) {
            if ((int32_t)(t - m->change_t) >= (int32_t)SETTLE_T1) m->changing = false;
            else if (rm) m->changing = false;   /* reduced motion: the rings snap */
            else omni_ring_change(m->change_from, today, m->change_fwd, omni_settle(t, m->change_t + SETTLE_T0, m->change_t + SETTLE_T1), out->val);
        }
        if (!m->changing && e && e->settle && !rm && !m->skipped) {
            const float s = omni_settle(t, w + SETTLE_T0, w + SETTLE_T1);
            for (int i = 0; i < OMNI_RINGS; i++) out->val[i] = today[i] - SETTLE_K[i] * s;
        }
        out->mark_old = 1;
        out->mark_new = 0;
    }

    /* the globe: home, spin-up and drift (or the anchor's) */
    out->lon_deg = omni_lon_wrap(home_lon(m, t));

    /* the lights */
    float green = 1;
    if (e && !m->skipped) green = ph(t, w + e->green_at, w + e->green_at + 350u);
    if (!live) green = 0;
    if (!live || lens == OMNI_LENS_UNSET)   /* the shell's blink: lit 0.25 + 0.75 smooth(sin(T / 380) / 2 + 0.5) */
        out->red = 0.25f + 0.75f * smooth01((float)(sin((double)(int32_t)(t - w) / BLINK_MS_PER_RAD) * 0.5 + 0.5));
    else if (lens == OMNI_LENS_STALE) out->red = STALE_RED;
    out->green = green;
    out->flash = 0;

    out->marker = true;
    out->marker_lat = (float)m->home_lat;
    out->marker_lon = (float)m->home_lon;
    out->marker_a = live ? 0.9f : 0;
    out->marker_pulse = 0;

    /* the loupe: 0 -> 1 over 300 ms, held to 3600, then 1 -> 0 over 300 */
    if (m->loupe_on) {
        const int32_t lt = (int32_t)(t - m->loupe_t);
        if (lt >= 3900) m->loupe_on = false;
        else out->loupe = lt < 0 ? 0 : lt < 300 ? (float)lt / 300.0f : lt < 3600 ? 1.0f : 1.0f - (float)(lt - 3600) / 300.0f;
    }

    /* the lid (Task 9): the flip entry's, until it leaves the screen (omni_hinge_gone_rad) or a tap skips the reveal */
    if (e == &OMNI_ENTRY_FLIP && !m->skipped) {
        const int32_t lt = (int32_t)(t - w);
        const uint32_t tl = lt > 0 ? (uint32_t)lt : 0;
        const float th = omni_flip_theta(tl, rm);
        if (th < omni_hinge_gone_rad(&OMNI_LID_HINGE)) {
            out->lid = true;
            out->lid_theta = th;
            out->fade = omni_flip_fade(tl);
        }
    }
    /* the iris (Task 10): the iris entry's, until it is open (s = 1) or a tap skips the reveal */
    if (e == &OMNI_ENTRY_IRIS && !m->skipped) {
        const int32_t it = (int32_t)(t - w);
        const uint32_t tl = it > 0 ? (uint32_t)it : 0;
        float ap, phi;
        omni_iris_at(tl, rm, &ap, &phi);
        const float s = ap / (float)OMNI_IRIS_AP_OPEN;
        if (s < 1) {
            out->iris = true;
            out->iris_ap = ap;
            out->iris_phi = phi;
            out->iris_s = s;
            out->fade = omni_flip_fade(tl);
        }
    }

    /* time travel (travel.c): its rings, hands, globe, lights, marks and marker over all of the above */
    bool tr_speed = false;
    if (live && (m->travelling || m->away)) tr_speed = omni_travel_step(m, t, today, out);

    /* per-frame speeds, for the fidelity rule (a travel's rings: its blur) */
    if (m->have_prev) {
        if (!tr_speed)
            for (int i = 0; i < OMNI_RINGS; i++) out->speed[i] = cyc(out->val[i] - m->prev_val[i], (float)OMNI_RING_GEOM[i].slots);
        out->lon_speed = (float)fabs(omni_wrap180((double)out->lon_deg - (double)m->prev_lon));
    }
    memcpy(m->prev_val, out->val, sizeof m->prev_val);
    m->prev_lon = out->lon_deg;
    m->have_prev = true;
}
