#pragma once
/* Host-only: main 4ef03a7's gesture recogniser, renamed (see gesture_oracle.c). */
#include "omni/input.h"

void oracle_gesture_reset(omni_gesture_sm_t *g);
int oracle_gesture_feed(omni_gesture_sm_t *g, uint32_t t_ms, bool pressed, int16_t x, int16_t y,
                        bool (*claim_drag)(int16_t x0, int16_t y0, void *ctx), void *ctx, omni_gesture_t out[2]);
