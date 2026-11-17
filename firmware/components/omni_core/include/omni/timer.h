#pragma once
/* The timer and stopwatch (Omni Stage 5 Task 1): the state machine of the mockup's `tmr` (faces-practical.js 1145), its
 * readout texts, with R135's departure: turning the rim only SETS the duration (ARMED); only a centre tap starts the
 * countdown, a second tap pauses it. So a pocket drag can never start an alarm.
 * R144 (owner follow-up): the rim is a relative jog dial, not an absolute angle. A wind starts from the current value and
 * angular travel adds (clockwise) or subtracts (anticlockwise) time, 30 min per turn (1 min per 12 degrees), clamped at
 * 0 and 60; lifting and replacing the thumb continues from the value. omni/tmr_jog.h turns touches into omni_tmr_wind_by
 * calls. A wind is taken in IDLE, ARMED and PAUSED (it adjusts the remaining time); a running countdown ignores it.
 * Pure C, no clock of its own: every call that needs the time takes `now`, uint32 ms (esp_timer on the device), and
 * times are compared by signed difference, so the 32-bit wrap (49 days) is harmless. */
#include <stdbool.h>
#include <stdint.h>

typedef enum { OMNI_TMR_IDLE, OMNI_TMR_SET, OMNI_TMR_COUNT, OMNI_TMR_PAUSED, OMNI_TMR_DONE,
               OMNI_TMR_SW, OMNI_TMR_SWPAUSE,
               OMNI_TMR_ARMED /* R135: a duration set, waiting for a centre tap */ } omni_tmr_mode_t;
#define OMNI_TMR_MAX_MIN 60        /* owner Q3 */
#define OMNI_TMR_RIM_R 140         /* touch-down at r >= this: the rim (mockup) */
#define OMNI_TMR_CX 233
#define OMNI_TMR_CY 233

typedef struct {
    omni_tmr_mode_t mode, prev;   /* prev: the mode a wind started from (restored by wind_cancel) */
    uint8_t set_min;
    uint32_t end_ms, remain_ms, sw_start_ms, done_ms;
    uint64_t sw_acc_ms;           /* 64-bit: a stopwatch left running for days never wraps */
    /* appended in fix round 1 */
    uint8_t prev_min;             /* set_min before a wind started (restored by wind_cancel) */
    bool alarm_pending;           /* a tap took the countdown to DONE: the next tick reports the step once */
    /* appended (R144): the relative wind */
    uint32_t wind_ms;             /* SET: the value being wound */
    float wind_frac;              /* SET: travel since the last whole-minute step, minutes */
} omni_tmr_t;

void omni_tmr_init(omni_tmr_t *t);
float omni_tmr_rim_angle(int16_t x, int16_t y);          /* rad, 0 at 12 o'clock, clockwise, [0, 2pi) */
/* r >= RIM_R and the mode takes a wind: IDLE, ARMED, PAUSED (or SET). R144: not COUNT, SW, SWPAUSE or DONE, whose rim
 * drags stay the shell's swipes. */
bool omni_tmr_claims(const omni_tmr_t *t, int16_t x0, int16_t y0);
bool omni_tmr_holds(const omni_tmr_t *t, int16_t x, int16_t y);     /* r < RIM_R and mode not IDLE/DONE */
/* -> SET from IDLE, ARMED or PAUSED, starting at the current value (IDLE 0, ARMED set_min, PAUSED the remaining time).
 * Ignored in any other mode (COUNT, SW, SWPAUSE, DONE); a second start keeps the first. */
void omni_tmr_wind_start(omni_tmr_t *t);
/* SET only: add dmin minutes of travel (negative subtracts). The value moves in whole minutes, a step for each whole
 * minute of travel (12 degrees) past the last step, truncated toward zero either way, so a step back also needs a whole
 * minute (a detent: no toggling at a boundary). PAUSED keeps its seconds. It stops at 0 and 60:00; travel beyond an end
 * is not banked, so a step back from an end needs one minute of travel. */
void omni_tmr_wind_by(omni_tmr_t *t, float dmin);
uint32_t omni_tmr_set_ms(const omni_tmr_t *t);           /* SET: the value being wound, ms; else 0 */
/* SET -> from IDLE/ARMED: ARMED with the value (never COUNT, R135); from PAUSED: PAUSED with the value as its
 * remaining time (a tap resumes). A value of 0 ends IDLE (the arming or the paused countdown cleared). */
void omni_tmr_wind_end(omni_tmr_t *t);
/* A wind that never ends (touch cancelled, sleep): back to the state before wind_start, set_min included (PAUSED keeps
 * its remaining time, ARMED its duration). Ignored unless the mode is SET. */
void omni_tmr_wind_cancel(omni_tmr_t *t);
/* centre: IDLE->SW, SW<->SWPAUSE, ARMED->COUNT (end = now + set_min min), COUNT<->PAUSED; DONE->IDLE.
 * A tap at or after a countdown's end takes it to DONE instead (never PAUSED at 0:00); the alarm is still due: the
 * next omni_tmr_tick reports the step. Ignored in SET. */
void omni_tmr_tap(omni_tmr_t *t, uint32_t now);
void omni_tmr_reset(omni_tmr_t *t);                      /* -> IDLE, all zero */
/* COUNT at or past end -> DONE (done_ms = end): true once (also for a step a tap made). Also folds a long-running
 * stopwatch's span into its total, so call it regularly. */
bool omni_tmr_tick(omni_tmr_t *t, uint32_t now);
uint32_t omni_tmr_remaining_ms(const omni_tmr_t *t, uint32_t now);   /* COUNT: end - now (0 when past); else remain_ms */
uint64_t omni_tmr_elapsed_ms(const omni_tmr_t *t, uint32_t now);     /* the stopwatch's elapsed time */
bool omni_tmr_alarm_at(const omni_tmr_t *t, uint32_t *end_ms);      /* COUNT only */
void omni_tmr_start_seconds(omni_tmr_t *t, uint32_t now, uint32_t s); /* console `tmr count <s>` (D15); s capped at 1 h */

typedef struct {
    char big[12], small[4], label[28], hint[40];
    uint8_t ink;          /* 0 ink, 1 faint (idle), 2 last-minute (#ff9c8c), 3 red (done) */
    float sector_min;     /* 0..60 */
    bool sector_red;      /* the last minute, or done */
    uint8_t alpha;        /* sector alpha x255: 0.75 steady; paused 0.45+0.15 sin((now % 1885)/300); done
                           * 0.45+0.4 sin(((now-done) % 1005)/160) (the periods 2 pi x 300 and 2 pi x 160 ms, whole) */
    float bead;           /* stopwatch: (elapsed mod 60 s)/60 s, or -1 */
} omni_tmr_view_t;
void omni_tmr_view(const omni_tmr_t *t, uint32_t now, omni_tmr_view_t *v);
