#pragma once
/* Globe motion: reveal curves, spin flywheel, live-globe drift home, analog longitude easing and render cadence.
 * Ported from design/mockups/faces-practical.js; per-frame factors are made time-based (k per 1/60 s -> k^(60 dt)).
 * Every uint32 time comparison is a signed difference, so it survives the 32-bit wrap. */
#include <stdbool.h>
#include <stdint.h>

/* Longitude into [-180, 180) (float rounding can land a plain fmod on 180: that is -180 here). */
float omni_wrap180f(float lon);
double omni_wrap180(double lon);

float omni_ease_out3(float x);
float omni_ease_in_out3(float x);

typedef struct {
    float lon_off_deg, gain, fade, settle;
    bool done;
} omni_reveal_t;
void omni_reveal_live(uint32_t t_ms, bool reduced, omni_reveal_t *r);
void omni_reveal_analog(uint32_t t_ms, bool reduced, omni_reveal_t *r);
void omni_reveal_spin(uint32_t t_ms, bool reduced, omni_reveal_t *r);

#define OMNI_SPIN_IDLE 7.0f /* deg/s */
typedef struct {
    float lon, v;
    bool dragging, moved;
    int16_t last_x, down_x;
    uint32_t last_t, down_t, show_until;
} omni_spin_t;
void omni_spin_wake(omni_spin_t *s, uint32_t t, bool reduced);
void omni_spin_down(omni_spin_t *s, uint32_t t, int16_t x);
void omni_spin_move(omni_spin_t *s, uint32_t t, int16_t x);
void omni_spin_up(omni_spin_t *s, uint32_t t);
void omni_spin_step(omni_spin_t *s, float dt_s);
/* One tick of dt_s: omni_spin_step in pieces of at most its 50 ms clamp, so the pace is the same at 15 and 30 fps,
 * and at most OMNI_SPIN_TICK_MAX_S in all (a stalled frame must not jump the globe). */
#define OMNI_SPIN_TICK_MAX_S 0.25f
void omni_spin_advance(omni_spin_t *s, float dt_s);
float omni_spin_loud(const omni_spin_t *s, uint32_t t);

typedef struct {
    float off_lon, v;
    bool dragging;
    int16_t last_x;
    uint32_t last_t, release_t;
} omni_drift_t;
void omni_drift_down(omni_drift_t *d, uint32_t t, int16_t x);
void omni_drift_move(omni_drift_t *d, uint32_t t, int16_t x);
void omni_drift_up(omni_drift_t *d, uint32_t t);
void omni_drift_step(omni_drift_t *d, uint32_t t, float dt_s);
bool omni_drift_moving(const omni_drift_t *d);

float omni_ease_lon(float cur, float target, float dt_s);

typedef enum { OMNI_CAD_LIVE, OMNI_CAD_ANALOG, OMNI_CAD_SPIN } omni_cad_face_t;
uint32_t omni_globe_period_ms(omni_cad_face_t f, bool animating, uint32_t since_active_ms);

int8_t omni_globe_lat0_for(double home_lat);
