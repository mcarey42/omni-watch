#pragma once
/* Time travel (spec §7.3, Stage 3 Task 12): the mockup's makeDial startTravel and model()'s travel overrides
 * (design/mockups/faces-omni.js 627-700), as pure C. The red lens sends the dial to a historic date and place: the rings
 * unlock and whirl (turns 2, -3, 3, -1), the hands swing back and return, the globe spins eastward three extra turns, a
 * warm flash, then the rings click home with a detent, the V re-forms on the new date, the globe lands on the place with
 * a pulsing pip, green lights and the caption names it. It lands at 3300 ms. Away, the caption fades over 9000-9600 ms
 * and the dial comes home by itself 12 s on (both from the globe's arrival, t0 + 2650 ms: the mockup's awayT); red or
 * green while away goes home now. Destinations cycle. Reduced motion (the wake's): no turns, jitter, detent, swing or
 * flash, a shorter whirl (rings by 900 + 140 i, the globe by 1200 ms, no extra turns and no drift).
 *
 * The model (omni_model_step) delegates to travel while one is active or the dial is away; travel reads and writes the
 * model's private fields (omni_model.h). Times are uint32 ms compared by signed difference. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/omni_model.h"

typedef struct { const char *name; int16_t y; uint8_t m, d; float lat, lon; } omni_dest_t;
/* the mockup's DEST, in order: Philadelphia 1776-07-04 ... North Atlantic 1912-04-14 (y <= 0 is BC) */
extern const omni_dest_t OMNI_DEST[7];

#define OMNI_TRAVEL_LAND_MS 3300u   /* the travel lands (away, or home) */
#define OMNI_TRAVEL_STAY_MS 12000u  /* away: the trip home starts after this, from the globe's arrival */

/* Starts a travel from the model's current state (the last step's displayed rings and globe: a travel or an arrival
 * in quick succession eases from where the dial is, T5 m6): a red tap (to the next destination; or home when away),
 * green or the 12 s timeout when away (to_home: home and today). Ignored while one is active, while the clock is unset
 * (the last step's), and to_home when not away. The wake's entry ends (as a skip would: no spin-up, settle or green
 * ramp after the trip). */
void omni_travel_start(omni_model_t *m, uint32_t t, bool to_home);
bool omni_travel_active(const omni_model_t *m);   /* a travel is in flight (taps are ignored, as the mockup's) */
bool omni_travel_away(const omni_model_t *m);     /* landed somewhere else: red or green goes home */
bool omni_travel_to_home(const omni_model_t *m);  /* the active travel is the trip home */
/* the active travel's or the away state's destination (OMNI_DEST name, mixed case); NULL at home or going home */
const char *omni_travel_dest_name(const omni_model_t *m);
/* Sleep: at home, today, no travel (the next step is the rest state; the wake starts the next entry). */
void omni_travel_abort_home(omni_model_t *m);
/* The caption at t: the destination's upper-case name and its alpha (fading in over 3000-3300 ms, out over 9000-9600
 * ms away); NULL (alpha 0) when there is none: at home, on the way home, or faded out. */
const char *omni_travel_caption(const omni_model_t *m, uint32_t t, float *alpha);
/* The travel's globe longitude at t, unwrapped (the mockup's lonAt and its drift after the arrival): the state's
 * lon_deg is this wrapped to [-180, 180). NaN when no travel is active. */
double omni_travel_lon(const omni_model_t *m, uint32_t t);

/* For omni_model_step: the travel's (or the away state's) overrides of out, given today's ring values; lands, and
 * starts the auto-return, as t passes. Returns true if it set out->speed: the mockup's ring blur taken over the model's
 * step, total (e(tau) - e(tau - 32)), slots per 32 ms step clamped to +-slots / 3 (the mockup's 16 ms and +-slots / 6,
 * scaled to the step: T12 M1, R105), the fidelity rule's input during the whirl. */
bool omni_travel_step(omni_model_t *m, uint32_t t, const float today[OMNI_RINGS], omni_dial_state_t *out);
