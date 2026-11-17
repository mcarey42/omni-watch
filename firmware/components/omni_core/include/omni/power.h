#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Board frame: +y up the screen, +z out of the screen. Values in g, specific force (what the accelerometer
 * reads): lying face up = (0,0,+1), hanging upright = (0,+1,0). */
typedef struct { float ax, ay, az; } omni_accel_t;
typedef enum { POSE_UNKNOWN, POSE_HANGING, POSE_LOOKING, POSE_FACE_DOWN } omni_pose_t;

typedef struct {
    omni_pose_t pose;
    uint32_t pose_since;     /* t_ms when the current pose was entered */
    float lp_ax, lp_ay, lp_az;
    bool primed;             /* lp has been seeded */
    bool fired;              /* wake already reported for this LOOKING episode */
    uint32_t still_since;    /* start of the current uninterrupted still LOOKING run */
    bool still_run;
    uint32_t pocket_since;   /* start of the current still, not-upright, not-LOOKING run */
    bool pocket_run;
} omni_wake_sm_t;

typedef struct {
    float look_min_z;
    float hang_max_abs_z;
    float facedown_max_z;
    float still_tol;
    uint16_t hold_ms;
    int8_t z_sign;
    float upright_min_y;     /* lp y at or above this = upright (hanging reads y ~ +0.95) */
    uint16_t pocket_ms;      /* still + not upright + not LOOKING this long = pocketed */
} omni_wake_cfg_t;

extern const omni_wake_cfg_t OMNI_WAKE_DEFAULTS;

void omni_wake_reset(omni_wake_sm_t *w);
/* Returns true once per lift: LOOKING held hold_ms with | |a|-1 | < still_tol throughout.
 * Re-arms only after the pose has left LOOKING. */
bool omni_wake_feed(omni_wake_sm_t *w, const omni_wake_cfg_t *c, uint32_t t_ms, omni_accel_t a);
/* Pose: from the low-passed z (FACE_DOWN / LOOKING / HANGING / UNKNOWN), except that a watch that has been
 * still (| |a|-1 | < still_tol), not upright (lp y < upright_min_y) and not LOOKING for pocket_ms reads
 * POSE_FACE_DOWN, meaning "pocketed or face down" (the short sleep timeout). A pocketed watch lies on its
 * edge, e.g. (-0.93,-0.24,-0.10), which z alone calls HANGING. */
omni_pose_t omni_wake_pose(const omni_wake_sm_t *w);

typedef enum { PWR_ASLEEP, PWR_AWAKE, PWR_GLANCE } omni_pwr_state_t;
typedef struct {
    omni_pwr_state_t st;     /* zero-initialised = ASLEEP, never woken */
    uint32_t t_state, t_last_touch, t_last_wake;
    bool woke;               /* t_last_wake is valid */
} omni_pwr_sm_t;
typedef enum { PA_NONE, PA_WAKE_FULL, PA_WAKE_SKIP_REVEAL, PA_TO_GLANCE, PA_TO_SLEEP } omni_pwr_action_t;

omni_pwr_action_t omni_pwr_on_lift(omni_pwr_sm_t *p, uint32_t t);
omni_pwr_action_t omni_pwr_on_button(omni_pwr_sm_t *p, uint32_t t);   /* PWR short: wakes (asleep, glance) or sleeps */
omni_pwr_action_t omni_pwr_on_touch(omni_pwr_sm_t *p, uint32_t t);
omni_pwr_action_t omni_pwr_tick(omni_pwr_sm_t *p, uint32_t t, uint8_t sleep_s, omni_pose_t pose);
/* The start of the current idle period: the later of the last touch and the last state change (wrap-safe). */
uint32_t omni_pwr_idle_ref(const omni_pwr_sm_t *p);
/* A countdown ended (UI_ALARM). ASLEEP or GLANCE -> AWAKE with PA_WAKE_SKIP_REVEAL (t_state, t_last_touch = t;
 * t_last_wake and woke untouched: not a lift for the 10 s reveal rule); AWAKE -> activity, PA_NONE. */
omni_pwr_action_t omni_pwr_on_alarm(omni_pwr_sm_t *p, uint32_t t);
/* UI_ALARM_KEEP (the ringing alarm's poll): AWAKE -> t_last_touch = t, PA_NONE; any other state -> PA_NONE. Never
 * wakes (preflight F2: a KEEP queued just before PWR silences the ring must not turn the screen back on). */
omni_pwr_action_t omni_pwr_on_alarm_keep(omni_pwr_sm_t *p, uint32_t t);

/* The console's `awake hold` lapses on its own after this (R66), so a hold whose console died (USB lost) cannot keep
 * the watch awake on battery for hours. */
#define OMNI_AWAKE_HOLD_MAX_MS (30u * 60u * 1000u)
/* True once a hold set at since_ms has lasted max_ms by now_ms (ms timestamps, wrap-safe; max_ms < 2^31; a since_ms
 * after now_ms is not expired). */
bool omni_hold_expired(uint32_t since_ms, uint32_t now_ms, uint32_t max_ms);

/* M1: power's own limit on net's Wi-Fi hold (light sleep waits while net holds Wi-Fi). Net ends every hold itself
 * within about 10 min (provisioning's cap); a net task blocked inside an esp_wifi/BLE/NVS call never would. Asleep
 * this long, a hold still set is ignored and light sleep comes on. */
#define OMNI_WIFI_HOLD_MAX_MS (12u * 60u * 1000u)
/* The hold power acts on: held, and asleep (since asleep_since_ms) for less than OMNI_WIFI_HOLD_MAX_MS (wrap-safe). */
bool omni_wifi_hold_effective(bool held, uint32_t asleep_since_ms, uint32_t now_ms);

/* 9-position ring, +-4 px, steps each minute. */
void omni_orbit_offset(int64_t unix_s, int8_t *dx, int8_t *dy);
