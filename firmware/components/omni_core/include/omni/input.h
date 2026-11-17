#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    G_NONE, G_TAP, G_LONG_PRESS, G_SWIPE_L, G_SWIPE_R, G_SWIPE_UP, G_SWIPE_DOWN_FROM_TOP,
    G_DRAG_START, G_DRAG_MOVE, G_DRAG_END,
    G_DOWN, G_HOLD, G_LIFT_STILL /* appended: only for a claim that asks (OMNI_GW_*) */
} omni_gesture_kind_t;

typedef struct {
    omni_gesture_kind_t kind;
    int16_t x, y, x0, y0; /* current/release point, start point */
    bool edge_start;      /* start point lies in the outer 40 px band of the round display */
    uint16_t hold_ms;     /* G_HOLD: the press's age (ms), saturating at 65 535; else 0 */
} omni_gesture_t;

/* Opaque state: callers only reset it. */
typedef struct {
    int16_t x0, y0, x, y;
    uint32_t t0;
    uint8_t phase;
    bool claimed;
    uint16_t next_hold;   /* age (ms) of the next G_HOLD tick; callers still only reset it */
} omni_gesture_sm_t;

void omni_gesture_reset(omni_gesture_sm_t *g);

/* Appended events, asked for per claim (the `want` of omni_gesture_feed_w; 0 = today's stream, byte for byte):
 *   OMNI_GW_DOWN       G_DOWN on the first pressed sample, at its point.
 *   OMNI_GW_HOLD       G_HOLD at the press's ages 450 + 85 k ms (first sample at or after each) while the finger stays
 *                      within 12 px; at most one per feed, hold_ms = the current age; none after 65 535 ms.
 *   OMNI_GW_LIFT_STILL G_LIFT_STILL on a release within 12 px at or after 300 ms (earlier is G_TAP).
 * I9: any want that includes HOLD or LIFT_STILL suppresses G_LONG_PRESS, and the gesture stays in its pressed phase
 * until the release or a move past 12 px (then the claim and G_DRAG_*, as today). DOWN alone leaves today's long press. */
#define OMNI_GW_DOWN       0x01
#define OMNI_GW_HOLD       0x02
#define OMNI_GW_LIFT_STILL 0x04
#define OMNI_HOLD_FIRST_MS 450
#define OMNI_HOLD_STEP_MS  85

/* Feed touch samples (pressed=false on release); keep feeding periodically while held so the 600 ms long
 * press fires without movement. claim_drag() (may be NULL = never claimed) is asked once, when movement
 * from the start first exceeds 12 px. Returns the number of gestures written to out (0..2). */
int omni_gesture_feed(omni_gesture_sm_t *g, uint32_t t_ms, bool pressed, int16_t x, int16_t y,
                      bool (*claim_drag)(int16_t x0, int16_t y0, void *ctx), void *ctx,
                      omni_gesture_t out[2]);
/* omni_gesture_feed(...) == omni_gesture_feed_w(..., 0, out). Returns 0..2 as above. */
int omni_gesture_feed_w(omni_gesture_sm_t *g, uint32_t t_ms, bool pressed, int16_t x, int16_t y,
                        bool (*claim_drag)(int16_t x0, int16_t y0, void *ctx), void *ctx, uint8_t want,
                        omni_gesture_t out[2]);

typedef enum { B_NONE, B_SHORT, B_DOUBLE, B_LONG } omni_press_t;

/* Opaque state: callers only zero-initialise it. */
typedef struct {
    uint32_t t_down, t_last_up;
    uint8_t clicks;
    bool down, long_sent;
} omni_button_sm_t;

/* Call on edges AND periodically (down = current state). B_SHORT is emitted by a later call once 350 ms
 * pass after a release with no new press; B_DOUBLE on the release of a press that began within 350 ms of the previous release; B_LONG while
 * still held at 800 ms (nothing on that release). */
omni_press_t omni_button_feed(omni_button_sm_t *b, uint32_t t_ms, bool down);
