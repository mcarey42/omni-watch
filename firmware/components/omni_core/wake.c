#include "omni/power.h"
#include <math.h>
#include <string.h>

const omni_wake_cfg_t OMNI_WAKE_DEFAULTS = {
    .look_min_z = 0.5f, .hang_max_abs_z = 0.35f, .facedown_max_z = -0.5f,
    .still_tol = 0.15f, .hold_ms = 250, .z_sign = 1,
    .upright_min_y = 0.5f, .pocket_ms = 2000,
};

#define LP_ALPHA 0.75f /* fast: the stillness check does the rejection */

void omni_wake_reset(omni_wake_sm_t *w) { memset(w, 0, sizeof *w); }

omni_pose_t omni_wake_pose(const omni_wake_sm_t *w) { return w->pose; }

static omni_pose_t classify(const omni_wake_cfg_t *c, float z)
{
    if (z <= c->facedown_max_z) return POSE_FACE_DOWN;
    if (z >= c->look_min_z) return POSE_LOOKING;
    if (fabsf(z) <= c->hang_max_abs_z) return POSE_HANGING;
    return POSE_UNKNOWN;
}

bool omni_wake_feed(omni_wake_sm_t *w, const omni_wake_cfg_t *c, uint32_t t_ms, omni_accel_t a)
{
    a.az *= (float)c->z_sign;
    if (!w->primed) {
        w->lp_ax = a.ax; w->lp_ay = a.ay; w->lp_az = a.az;
        w->primed = true;
    } else {
        w->lp_ax += LP_ALPHA * (a.ax - w->lp_ax);
        w->lp_ay += LP_ALPHA * (a.ay - w->lp_ay);
        w->lp_az += LP_ALPHA * (a.az - w->lp_az);
    }
    float mag = sqrtf(a.ax * a.ax + a.ay * a.ay + a.az * a.az);
    bool still = fabsf(mag - 1.0f) < c->still_tol;

    omni_pose_t p = classify(c, w->lp_az);
    if (p != POSE_LOOKING && still && w->lp_ay < c->upright_min_y) {
        if (!w->pocket_run) {
            w->pocket_run = true;
            w->pocket_since = t_ms;
        }
        if ((t_ms - w->pocket_since) >= c->pocket_ms) p = POSE_FACE_DOWN;
    } else {
        w->pocket_run = false;
    }
    if (p != w->pose) {
        w->pose = p;
        w->pose_since = t_ms;
    }
    if (p != POSE_LOOKING) {
        w->fired = false;
        w->still_run = false;
        return false;
    }

    if (!still) {
        w->still_run = false;
        return false;
    }
    if (!w->still_run) {
        w->still_run = true;
        w->still_since = t_ms;
    }
    if (!w->fired && (t_ms - w->still_since) >= c->hold_ms) {
        w->fired = true;
        return true;
    }
    return false;
}
