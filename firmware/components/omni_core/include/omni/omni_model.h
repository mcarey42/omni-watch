#pragma once
/* The Omni dial's logical model (spec §7.3, Stage 3 Task 5): the mockup's makeDial model() for the at-home state
 * (design/mockups/faces-omni.js 607-700), as pure C. Date ring values, hands, the ring settle, the globe spin-up and
 * drift, the lens levels, the loupe, hit tests. Task 6 renders what omni_model_step outputs; times are uint32 ms
 * compared by signed difference, so the model runs across the u32 wrap. */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "omni/ring.h"
#include "omni/sync.h"

/* The mockup's dateVals: [month - 1, day, year % 100, century] for a ring each. year <= 0 is BC (no year 0). */
void omni_dial_vals(int year, int month, int day, float out[OMNI_RINGS]);
/* The mockup's handAngles, radians clockwise from 12: the minute hand rounded to 1/720 rad (the mockup rounds
 * mins/60 * TAU * 720, so its step is 1/720 rad = 0.0796 deg, not the 0.5 deg its comment says); the hour hand rounded
 * to the same step (R90, amending R84: it then moves about every 10 s instead of every second, invisibly). */
void omni_hand_angles(int h, int m, int s, float *hour_rad, float *min_rad);

/* 1 - outBack(ph(t, t0, t1)) (c1 1.5): 1 before t0, 0 from t1, overshooting below 0 on the way. */
float omni_settle(uint32_t t_ms, uint32_t t0, uint32_t t1);
/* Ring values easing from `from` to `to`, out = to + (from - to) * s_settle. forward (a date rollover): always the
 * forward way round, so `to` is raised by the ring's slots when it is below `from` (31 -> 1 on the day ring ends at 33);
 * not forward (a clock step back): the shortest way. The result is not reduced mod the slots. */
void omni_ring_change(const float from[OMNI_RINGS], const float to[OMNI_RINGS], bool forward, float s_settle,
                      float out[OMNI_RINGS]);
/* The mockup's spinOff: the remaining spin-up offset, k_deg before t0 down to 0 at t0 + dur. */
float omni_spin_off(uint32_t t, uint32_t t0, uint32_t dur, float k_deg);

#define OMNI_DRIFT_DPS 2.2f   /* idle globe drift, deg/s (westward: lon falls) */
#define OMNI_MODEL_STEP_MS 32 /* the face steps the model this often (R90: 33 capped the rate at 30.3 fps, with no slot
                               * to spare); travel.c takes its ring speed per this step */

/* A reveal entry's timeline, ms from the wake. */
typedef struct { uint16_t spin_t0, spin_dur, green_at; float spin_k; bool settle; } omni_entry_t;
extern const omni_entry_t OMNI_ENTRY_DIAL, OMNI_ENTRY_FLIP, OMNI_ENTRY_IRIS;
/* DIAL {150,2600,2300,540,1}, FLIP {330,3600,3570,990,0}, IRIS {620,3600,3900,990,0}. FLIP keeps the mockup's spin start
 * 180 ms into the swing, on Task 9's default timeline (latch 150, swing from 150); with the mockup timeline
 * (OMNI_FLIP_MOCKUP, lid.h: Task 14's A/B) it is the mockup's {760, 3600, 4000, 990, 0}. DIAL and IRIS are the
 * mockup's. A wake with FLIP steps the lid into the state (omni_flip_theta, until omni_hinge_gone_rad; none once
 * skipped); one with IRIS the iris (omni_iris_at, until it is open, s = 1; none once skipped). Either sets the fade
 * while its lid or iris is on. */

typedef struct {   /* the dial's logical state for one frame (Task 6 renders it) */
    float val[OMNI_RINGS], sel_old[OMNI_RINGS], sel_new[OMNI_RINGS], mark_old, mark_new;
    float hour_rad, min_rad, lon_deg, red, green, flash, loupe;
    bool marker; float marker_lat, marker_lon, marker_a, marker_pulse;
    float speed[OMNI_RINGS], lon_speed;   /* per frame, for the fidelity rule: slots and degrees moved since the last step */
    /* the lid (Task 9): lid false = no lid; lid_theta its angle (omni_flip_theta, radians) */
    float lid_theta;
    bool lid;
    /* the iris (Task 10): iris false = none; iris_ap its aperture (px), iris_phi the blades' turn (radians), iris_s
     * its opening (ap / 207: the dim under it is .55 (1 - s)), as omni_iris_at gives them */
    float iris_ap, iris_phi, iris_s;
    bool iris;
    /* the reveal's fade from black (either entry: omni_flip_fade, the mockup's 300 ms fadeIn): the compositor's last
     * step, a true per-pixel blend of the whole composite toward black by round(fade x 255) */
    float fade;
} omni_dial_state_t;

/* A complete type, so the face can hold one statically; its fields are private to omni_model.c and travel.c (Task 12),
 * which share them through this header (reveal.c, Task 11, is theme-neutral and does not touch them). */
typedef struct omni_model {
    double home_lat, home_lon;
    const omni_entry_t *entry;     /* NULL = at rest */
    bool reduced, skipped;
    uint32_t wake_t;               /* the entry's times are from here */
    uint32_t skip_t;               /* omni_model_skip's time (skipped) */
    bool have_today;               /* today_v / today_key below are this wake's baseline */
    float today_v[OMNI_RINGS];
    int32_t today_key;             /* y * 512 + m * 32 + d, for the direction of a change */
    bool changing, change_fwd;
    uint32_t change_t;
    float change_from[OMNI_RINGS];
    bool loupe_on;
    uint32_t loupe_t;
    bool have_prev;                /* prev_* are the last step's, for the per-frame speeds */
    float prev_val[OMNI_RINGS], prev_lon;
    bool live;                     /* the last step had a set clock (a travel needs one) */
    /* the globe's rest anchor (once the entry is skipped, or a trip home has landed): lon = anchor_lon + anchor_off
     * eased out over 500 ms from anchor_t, less the drift since anchor_t (T5 m7: a skip lands lon continuously) */
    bool anchored;
    uint32_t anchor_t;
    double anchor_lon, anchor_off;
    /* time travel (travel.c, Task 12) */
    bool travelling, away, to_home, tr_reduced;
    uint32_t travel_t0;            /* the travel's start; away: its arrival is travel_t0 + the globe's end (g1) */
    uint8_t dest_ix;               /* the next destination (OMNI_DEST, cycling) */
    int8_t dest;                   /* the travel's or the away state's destination; -1 home (omni_model_init) */
    float tr_from[OMNI_RINGS], tr_to[OMNI_RINGS];
    double tr_from_lon, tr_to_lon;
} omni_model_t;

void omni_model_init(omni_model_t *m, double home_lat, double home_lon);
/* A new home (the city setting). The next wake without a reveal starts at it instead of going on from the longitude last
 * shown (re-review N2: M1's continuation must not keep the old city's globe). */
void omni_model_set_home(omni_model_t *m, double home_lat, double home_lon);
void omni_model_wake(omni_model_t *m, uint32_t t, const omni_entry_t *entry, bool reduced); /* entry NULL = no reveal: at rest */
/* now: wall clock (local broken-down time, or set = false); lens: omni_sync_lens */
void omni_model_step(omni_model_t *m, uint32_t t, bool set, const struct tm *local, omni_lens_t lens,
                     omni_dial_state_t *out);
/* A tap during the reveal: settle and green to their end, the lid or iris gone; the spin-up's remaining offset (the
 * short way round, at most 180 deg) eases out over 500 ms, so the globe lands home without a jump (T5 m7). */
void omni_model_skip(omni_model_t *m, uint32_t t);
/* The wake's lid is gone (omni_hinge_gone_rad) or its iris open (s = 1), on the entry's own timeline (t x 2.2 reduced);
 * at once for an entry with neither (DIAL, none), and from the skip once skipped. */
bool omni_model_reveal_done(const omni_model_t *m, uint32_t t);
/* The red lens may start a travel (Task 12): 400 ms after the reveal is done (the mockup's REVEAL_END + 400). */
bool omni_model_red_ready(const omni_model_t *m, uint32_t t);
/* A tap now skips the wake's entry (omni_model_skip): not skipped and while something visibly moves: its lid or iris,
 * spin-up or settle (reduced motion: only the lid or iris). Green's ramp alone does not count (T11 m1): a tap after the
 * motion has stopped gets its normal handling (the magnifier). False at rest. */
bool omni_model_revealing(const omni_model_t *m, uint32_t t);
/* Shared with travel.c: a longitude wrapped to [-180, 180) as a float (never 180); the globe's rest anchor at lon from
 * t (a trip home's arrival: drifting from there, no eased offset). */
float omni_lon_wrap(double lon);
void omni_model_anchor(omni_model_t *m, uint32_t t, double lon);
typedef enum { OMNI_HIT_NONE, OMNI_HIT_RED, OMNI_HIT_GREEN, OMNI_HIT_MAG, OMNI_HIT_LOUPE_OFF } omni_hit_t;
omni_hit_t omni_hit(int16_t x, int16_t y, bool loupe_open);
void omni_model_loupe(omni_model_t *m, uint32_t t);  /* toggles as the mockup's tap() */
