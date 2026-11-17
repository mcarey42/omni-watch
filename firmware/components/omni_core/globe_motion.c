#include "omni/globe_motion.h"

#include <math.h>
#include <stdlib.h>

static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

float omni_ease_out3(float x)
{
    x = clampf(x, 0.0f, 1.0f);
    return 1.0f - powf(1.0f - x, 3.0f);
}

float omni_ease_in_out3(float x)
{
    x = clampf(x, 0.0f, 1.0f);
    return x < 0.5f ? 4.0f * x * x * x : 1.0f - powf(-2.0f * x + 2.0f, 3.0f) / 2.0f;
}

/* ---- reveals ---- */

void omni_reveal_live(uint32_t t_ms, bool reduced, omni_reveal_t *r)
{
    float t = (float)t_ms;
    float k = reduced ? 1.0f : omni_ease_out3(t / 1400.0f);
    r->fade = clampf(t / (reduced ? 500.0f : 350.0f), 0.0f, 1.0f);
    r->lon_off_deg = -(1.0f - k) * 220.0f;
    r->gain = 0.35f + 0.65f * r->fade;
    r->settle = 1.0f;
    r->done = t_ms >= (reduced ? 500u : 1400u);
}

void omni_reveal_analog(uint32_t t_ms, bool reduced, omni_reveal_t *r)
{
    float t = (float)t_ms;
    float k = reduced ? 1.0f : omni_ease_out3(t / 1200.0f);
    r->fade = clampf(t / 300.0f, 0.0f, 1.0f);
    r->lon_off_deg = -(1.0f - k) * 160.0f;
    r->gain = 0.9f * (0.4f + 0.6f * r->fade);
    r->settle = reduced ? 1.0f : omni_ease_in_out3(t / 900.0f);
    r->done = t_ms >= (reduced ? 300u : 1200u);
}

void omni_reveal_spin(uint32_t t_ms, bool reduced, omni_reveal_t *r)
{
    float t = (float)t_ms;
    r->fade = reduced ? clampf(t / 400.0f, 0.0f, 1.0f) : omni_ease_out3(t / 900.0f);
    r->lon_off_deg = 0.0f;
    r->gain = 0.25f + 0.75f * r->fade;
    r->settle = 1.0f;
    r->done = t_ms >= (reduced ? 400u : 900u);
}

/* ---- spin ---- */

float omni_wrap180f(float lon)
{
    lon = fmodf(lon + 180.0f, 360.0f);
    if (lon < 0.0f)
        lon += 360.0f;
    lon -= 180.0f;
    if (lon >= 180.0f) /* float rounding can land exactly on 180 */
        lon -= 360.0f;
    return lon;
}

double omni_wrap180(double lon)
{
    lon = fmod(lon + 180.0, 360.0);
    if (lon < 0.0)
        lon += 360.0;
    lon -= 180.0;
    if (lon >= 180.0)
        lon -= 360.0;
    return lon;
}

void omni_spin_wake(omni_spin_t *s, uint32_t t, bool reduced)
{
    s->v = reduced ? OMNI_SPIN_IDLE : 520.0f;
    s->show_until = t + 3000u;
}

void omni_spin_down(omni_spin_t *s, uint32_t t, int16_t x)
{
    s->dragging = true;
    s->moved = false;
    s->last_x = x;
    s->down_x = x;
    s->last_t = t;
    s->down_t = t;
    s->v = 0.0f;
}

void omni_spin_move(omni_spin_t *s, uint32_t t, int16_t x)
{
    if (!s->dragging)
        return;
    int32_t dt_ms = (int32_t)(t - s->last_t);
    float dt = (float)(dt_ms < 1 ? 1 : dt_ms);
    float dx = (float)(x - s->last_x);
    if (abs(x - s->down_x) > 6)
        s->moved = true;
    s->lon = omni_wrap180f(s->lon - dx * 0.42f);
    s->v = clampf(0.5f * s->v + 0.5f * (dx * 0.42f / dt * 1000.0f), -900.0f, 900.0f);
    s->last_x = x;
    s->last_t = t;
}

void omni_spin_up(omni_spin_t *s, uint32_t t)
{
    if (!s->dragging)
        return;
    s->dragging = false;
    if (!s->moved && (int32_t)(t - s->down_t) < 350) {
        s->show_until = t + 4000u;
        s->v = OMNI_SPIN_IDLE;
    }
    if ((int32_t)(t - s->last_t) > 80)
        s->v = OMNI_SPIN_IDLE;
}

void omni_spin_step(omni_spin_t *s, float dt_s)
{
    if (s->dragging)
        return;
    float dt = clampf(dt_s, 0.0f, 0.05f);
    s->v += (OMNI_SPIN_IDLE - s->v) * (1.0f - expf(-dt / 0.55f));
    s->lon = omni_wrap180f(s->lon - s->v * dt);
}

void omni_spin_advance(omni_spin_t *s, float dt_s)
{
    float dt = dt_s > OMNI_SPIN_TICK_MAX_S ? OMNI_SPIN_TICK_MAX_S : dt_s;
    while (dt > 0.0f) {
        const float step = dt > 0.05f ? 0.05f : dt;
        omni_spin_step(s, step);
        dt -= step;
    }
}

float omni_spin_loud(const omni_spin_t *s, uint32_t t)
{
    return clampf((float)(int32_t)(s->show_until - t) / 600.0f, 0.0f, 1.0f);
}

/* ---- live-globe drift ---- */

void omni_drift_down(omni_drift_t *d, uint32_t t, int16_t x)
{
    d->dragging = true;
    d->last_x = x;
    d->last_t = t;
    d->v = 0.0f;
}

void omni_drift_move(omni_drift_t *d, uint32_t t, int16_t x)
{
    if (!d->dragging)
        return;
    int32_t dt_ms = (int32_t)(t - d->last_t);
    float dt = (float)(dt_ms < 1 ? 1 : dt_ms);
    float dx = (float)(x - d->last_x);
    d->off_lon -= dx * 0.45f;
    d->v = -dx * 0.45f / dt * 1000.0f * 0.6f + 0.4f * d->v;
    d->last_x = x;
    d->last_t = t;
}

void omni_drift_up(omni_drift_t *d, uint32_t t)
{
    d->dragging = false;
    d->release_t = t;
}

void omni_drift_step(omni_drift_t *d, uint32_t t, float dt_s)
{
    if (d->dragging || dt_s <= 0.0f)
        return;
    if (dt_s > 0.05f) /* a long gap (sleep) must not fling the globe: same clamp as the spin */
        dt_s = 0.05f;
    d->off_lon += d->v * dt_s; /* add first, then decay: the mockup's order */
    d->v *= powf(0.93f, 60.0f * dt_s);
    if ((int32_t)(t - d->release_t) > 4000)
        d->off_lon *= powf(0.94f, 60.0f * dt_s);
}

bool omni_drift_moving(const omni_drift_t *d) { return fabsf(d->off_lon) >= 0.05f || fabsf(d->v) >= 0.05f; }

float omni_ease_lon(float cur, float target, float dt_s)
{
    float diff = fmodf(target - cur, 360.0f);
    if (diff > 180.0f)
        diff -= 360.0f;
    else if (diff < -180.0f)
        diff += 360.0f;
    if (dt_s < 0.0f)
        dt_s = 0.0f;
    return omni_wrap180f(cur + diff * (1.0f - powf(0.92f, 60.0f * dt_s)));
}

/* ---- cadence and lat0 ---- */

uint32_t omni_globe_period_ms(omni_cad_face_t f, bool animating, uint32_t since_active_ms)
{
    if (f == OMNI_CAD_SPIN) /* spin always turns: its rate depends on idleness alone (spec 7.2) */
        return since_active_ms >= 20000u ? 66 : 33;
    return animating ? 33 : 30000;
}

int8_t omni_globe_lat0_for(double home_lat)
{
    double v = home_lat * 0.55;
    if (v > 45.0)
        v = 45.0;
    if (v < -45.0)
        v = -45.0;
    return (int8_t)lround(v);
}
