#include "omni/tmr_jog.h"
#include <stdlib.h>

#define TAU 6.28318530717958647692f
#define DEG (TAU / 360.0f)

static int32_t r2(int16_t x, int16_t y)
{
    const int32_t dx = x - OMNI_TMR_CX, dy = y - OMNI_TMR_CY;
    return dx * dx + dy * dy;
}

/* Minutes of travel from this sample (0 until the rim has the drag; on the sample that gives it, the whole travel). */
static float sample(omni_tmr_drag_t *d, int16_t x, int16_t y)
{
    const float a = omni_tmr_rim_angle(x, y);
    float da = a - d->a;
    if (da > TAU / 2) da -= TAU;
    else if (da < -TAU / 2) da += TAU;
    d->a = a;
    const int32_t rr = r2(x, y);
    if (d->phase == OMNI_TMR_DRAG_PENDING) {
        if (rr < OMNI_TMR_JOG_LEAVE_R * OMNI_TMR_JOG_LEAVE_R) {
            d->phase = OMNI_TMR_DRAG_FREE;   /* left the band first: a swipe or nothing, never a jog */
            return 0;
        }
        d->travel += da;
        if (d->travel < OMNI_TMR_JOG_CLAIM_DEG * DEG && d->travel > -OMNI_TMR_JOG_CLAIM_DEG * DEG) return 0;
        d->phase = OMNI_TMR_DRAG_JOG;
        return d->travel / TAU * OMNI_TMR_JOG_MIN_PER_TURN;
    }
    if (d->phase != OMNI_TMR_DRAG_JOG || rr < OMNI_TMR_JOG_DEAD_R * OMNI_TMR_JOG_DEAD_R) return 0;
    return da / TAU * OMNI_TMR_JOG_MIN_PER_TURN;
}

static void feed(omni_tmr_drag_t *d, omni_tmr_t *t, int16_t x, int16_t y)
{
    const uint8_t was = d->phase;
    const float m = sample(d, x, y);
    if (d->phase != OMNI_TMR_DRAG_JOG) return;
    if (was != OMNI_TMR_DRAG_JOG) omni_tmr_wind_start(t);   /* refused unless the mode takes a wind */
    omni_tmr_wind_by(t, m);
}

static omni_gesture_kind_t swipe(const omni_tmr_drag_t *d, int16_t x, int16_t y)
{
    const int dx = x - d->x0, dy = y - d->y0;
    if (d->phase == OMNI_TMR_DRAG_JOG) return G_NONE;
    if (d->phase == OMNI_TMR_DRAG_PENDING && (d->travel >= OMNI_TMR_JOG_SWIPE_MAX_DEG * DEG ||
                                              d->travel <= -OMNI_TMR_JOG_SWIPE_MAX_DEG * DEG)) return G_NONE;
    if (abs(dx) >= abs(dy)) {
        if (abs(dx) < OMNI_TMR_JOG_SWIPE_PX) return G_NONE;
        return dx < 0 ? G_SWIPE_L : G_SWIPE_R;
    }
    /* vertical: down from the top edge is quick settings (gesture.c's rule); swipe up does nothing on a face (R144) */
    return dy >= OMNI_TMR_JOG_SWIPE_PX && d->y0 < OMNI_TMR_JOG_TOP_Y ? G_SWIPE_DOWN_FROM_TOP : G_NONE;
}

omni_gesture_kind_t omni_tmr_drag(omni_tmr_drag_t *d, omni_tmr_t *t, const omni_gesture_t *g)
{
    switch (g->kind) {
    case G_DRAG_START:
        d->x0 = g->x0;
        d->y0 = g->y0;
        d->a = omni_tmr_rim_angle(g->x0, g->y0);
        d->travel = 0;
        d->phase = r2(g->x0, g->y0) >= OMNI_TMR_RIM_R * OMNI_TMR_RIM_R ? OMNI_TMR_DRAG_PENDING : OMNI_TMR_DRAG_FREE;
        feed(d, t, g->x, g->y);
        return G_NONE;
    case G_DRAG_MOVE:
        if (d->phase != OMNI_TMR_DRAG_OFF) feed(d, t, g->x, g->y);
        return G_NONE;
    case G_DRAG_END: {
        if (d->phase == OMNI_TMR_DRAG_OFF) return G_NONE;
        feed(d, t, g->x, g->y);
        const omni_gesture_kind_t k = swipe(d, g->x, g->y);
        if (d->phase == OMNI_TMR_DRAG_JOG) omni_tmr_wind_end(t);
        d->phase = OMNI_TMR_DRAG_OFF;
        return k;
    }
    default: return G_NONE;
    }
}
