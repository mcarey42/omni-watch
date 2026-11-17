#pragma once
/* R144: the timer face's drags. A drag that starts on the rim (omni_tmr_claims) is classified as it moves:
 *  - the rim takes it (a jog, omni/timer.h's relative wind) only once its travel round the centre passes
 *    OMNI_TMR_JOG_CLAIM_DEG while every sample so far stayed in the rim band (it starts at r >= OMNI_TMR_RIM_R and
 *    leaves only below OMNI_TMR_JOG_LEAVE_R, so a wobble just inside 140 is not a departure); from then on the whole
 *    travel counts, 30 min per turn in whole 12 degree steps (so the claim itself is exactly one minute), until the lift;
 *  - before that nothing changes, and at the lift a mostly horizontal flick (|dx| >= |dy|, |dx| >= 60 px, as the shell's
 *    own swipes) is handed back as G_SWIPE_L / G_SWIPE_R for the face to navigate with. A drag that stayed in the band
 *    must also have swept less than OMNI_TMR_JOG_SWIPE_MAX_DEG, so a short turn of the rim is never read as a swipe.
 *    A mostly vertical drag down from the top edge (y0 < 60, dy >= 60, gesture.c's rule) comes back the same way as
 *    G_SWIPE_DOWN_FROM_TOP, so quick settings opens on the timer face in every mode.
 * The finger's absolute angle never matters. Pure C (omni_core). */
#include <stdint.h>
#include "omni/input.h"
#include "omni/timer.h"

#define OMNI_TMR_JOG_CLAIM_DEG 20.0f      /* travel in the band before the rim takes the drag */
#define OMNI_TMR_JOG_SWIPE_MAX_DEG 15.0f  /* a swipe that never left the band swept less than this */
#define OMNI_TMR_JOG_MIN_PER_TURN 30.0f   /* two turns span 0..60 min: 1 min per 12 degrees */
#define OMNI_TMR_JOG_SWIPE_PX 60          /* gesture.c's SWIPE_PX */
#define OMNI_TMR_JOG_DEAD_R 70            /* once jogging, samples nearer the centre than this add no travel */
#define OMNI_TMR_JOG_LEAVE_R 128          /* before the claim, a sample below this leaves the band (hysteresis) */
#define OMNI_TMR_JOG_TOP_Y 60             /* gesture.c's TOP_START_Y: a swipe down starts above this */

enum { OMNI_TMR_DRAG_OFF, OMNI_TMR_DRAG_PENDING, OMNI_TMR_DRAG_JOG, OMNI_TMR_DRAG_FREE };

typedef struct {
    int16_t x0, y0;     /* where the touch began */
    float a;            /* the last sample's angle (omni_tmr_rim_angle) */
    float travel;       /* signed travel round the centre while PENDING, rad (clockwise +) */
    uint8_t phase;      /* OMNI_TMR_DRAG_*: FREE = left the band first, never a jog */
} omni_tmr_drag_t;

/* The face's handling of one gesture: G_DRAG_START begins a classification at (x0, y0) and takes (x, y) as its first
 * sample, G_DRAG_MOVE adds a sample, G_DRAG_END adds the lift and ends it. Once the rim has the drag, the timer winds
 * (omni_tmr_wind_start / _by / _end; refused in modes that take no wind). Returns G_SWIPE_L, G_SWIPE_R or
 * G_SWIPE_DOWN_FROM_TOP when the drag ended as the shell's swipe (the face posts it), else G_NONE. Every other gesture kind returns G_NONE and changes nothing. */
omni_gesture_kind_t omni_tmr_drag(omni_tmr_drag_t *d, omni_tmr_t *t, const omni_gesture_t *g);
