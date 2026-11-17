#pragma once
/* The hull pass (rd wave 2 Task 5; spec section 10.3, as ruling R166 has it; faces-reveal.js hullR() 797-802 and
 * frame_hull() 803-850): Drift Plate's wake. We are already mid-pass, very close: the face's own band of rust plates
 * rushes right to left, its frame numbers flying by, slows over 1.5 s and parks with a 7 px overshoot on the plate with
 * the time painted on it; then the underside windows light one by one up to the current second while the labels settle.
 *
 * A direct-path reveal over the face's own band (W2-D1, R166, superseding spec sections 17.18 and 10.3's band canvas):
 * the face draws the pass itself (rd/faces.h RD_FACE_DRIFT: its draw() at the reveal's wake_ms renders the band at
 * rd_pass_at's offset), and RD_REVEAL_PASS (pass.c, in rd_core) only plans which part of the interior each frame
 * redraws, then copies the interior and lays the opening fade. A refused claim runs the same frames through fk_view's
 * whole-frame fallback (the whole 466 x 466 frame banded and the whole canvas invalidated): that is not spec section
 * 10.3's band-only canvas pass, and is never reported as a measurement of it (I10, R166).
 *
 * Pure: no esp_*, freertos or LVGL; no allocation; no libm (lesson 6): the overshoot's sine through fk_sincos. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/reveal.h"

#define RD_PASS_MS 2050         /* the whole reveal (HU_END) */
#define RD_PASS_FADE_MS 500     /* reduced motion and the Reveal row's Fade: a cross-fade from black */
#define RD_PASS_PARK_MS 1800    /* the band parked (done after it): not the reveal's end */
#define RD_PASS_SETTLED_MS 2000 /* the reveal's settled end: the labels' settle ends (I1) */

typedef struct {
    float off;          /* the pass's offset in px: 2700 (1 - outCubic(ph(t, 0, 1500))) - 7 sin(pi u) (1 - u), u =
                           ph(t, 1500, 1800); 0 under reduced motion */
    uint8_t lights8;    /* the windows' share lit: ph(t, 1450, 1950) x 255; 255 under reduced motion */
    uint8_t settle8;    /* the labels' settle: outCubic(ph(t, 1250, 2000)) x 255; 255 under reduced motion */
    uint8_t fade8;      /* the opening fade: ph(t, 0, 200) x 255; under reduced motion outCubic(ph(t, 0, 500)) x 255 */
    bool done;          /* the band parked: t > 1800 (reduced: t >= 500) */
    bool settled;       /* t >= 2000 (the labels' settle ends) or a skip: the reveal's end (I1); reduced: t >= 500 */
} rd_pass_state_t;
void rd_pass_at(uint32_t t_ms, bool reduced, rd_pass_state_t *st);
void rd_pass_skip(rd_pass_state_t *st);   /* a tap: the end frame (off 0, everything at 255, settled) */

/* The pass's frame timing (`rd reveal bench`): rd_rel_timing's accumulator (one static for the three reveals), by the
 * pass's windows: 0 rush [0, 1000), 1 slow [1000, 1500), 2 park [1500, 1800), 3 settle [1800, 2050] */
#define RD_PASS_WIN_RUSH 1000
#define RD_PASS_WIN_SLOW 1500

extern const fk_reveal_ops_t RD_REVEAL_PASS;   /* pass.c: len 2050, reduced 500, pop 0, pop_asset NULL; bind reads
                                                  nothing (the face reads its own tile); rctx: the last plan only (its
                                                  rd_pass_state_t, which band() reads, and three flags) */
