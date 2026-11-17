#pragma once
/* A hold-to-act key (Settings > Faces' "Restore default", spec 8.2): pure. A press starts on G_DOWN on the key; it acts
 * once, at the first G_HOLD whose hold_ms (the input task's age of the press) reaches fire_ms; any other gesture ends it
 * (the lift: G_TAP / G_LIFT_STILL; a drag; a swipe), so a busy event loop that delivers the ticks late never cancels a
 * real hold. Times are uint32 ms compared by signed difference (no wrap at 0 or 2^32).
 * Forget's hold is deliberately not this one: it keeps its own machine, omni_forget_hold_step (join.h), as C2's gate. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/input.h"

typedef struct {
    uint32_t t_down;   /* when the press started (the screen's clock: its fill) */
    bool active;
} omni_hold_key_t;

typedef enum { OMNI_HOLD_KEY_NONE, OMNI_HOLD_KEY_FIRE, OMNI_HOLD_KEY_CANCEL } omni_hold_key_rc_t;

void omni_hold_key_down(omni_hold_key_t *h, uint32_t t);
/* Every gesture while the key may be held: FIRE once (the press ends), CANCEL when a non-hold gesture ends an active
 * press, else NONE (G_DOWN is the caller's: it starts a press or not). */
omni_hold_key_rc_t omni_hold_key_gesture(omni_hold_key_t *h, const omni_gesture_t *g, uint16_t fire_ms);
/* The fill, 0..1000 (per mille of fire_ms) at now; 0 when inactive or now is before t_down. */
uint16_t omni_hold_key_fill(const omni_hold_key_t *h, uint32_t now, uint16_t fire_ms);
