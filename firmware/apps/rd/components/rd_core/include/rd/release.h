#pragma once
/* Stasis Release (rd wave 1 Task 10; spec sections 9.1-9.3; faces-reveal.js stasisR(), drawLid() and frame_stasis()):
 * the theme's reveal, the stasis lid that counts up, pops its seal, lets the cold mist in and swings up on its top
 * hinge while the time thaws underneath. The timeline here is pure (rd_rel_at); RD_REVEAL_STASIS (release.c, in
 * rd_core: PR12) runs it on fk_view's direct path through face_kit's row renderer (fk/rows.h), over the Stasis Clock
 * as its interior, and ends on the Stasis Clock's settled frame.
 *
 * Pure: no esp_*, freertos or LVGL; no allocation; no libm (lesson 6): angles through fk_sincos, except the hinge
 * engine's own trig inside omni_core for a swinging lid (fk_rows_map, Task 3's ruling), so a frame mid-swing is pinned
 * per machine, never compared host to device. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/reveal.h"
#include "fk/rows.h"
#include "omni/lid.h"

extern const omni_hinge_t RD_LID_HINGE;   /* { OMNI_HINGE_HORIZONTAL, 0, 560, 466, 0, 0 }: hinged on the top edge */
#define RD_REL_MS 2150                    /* the whole reveal */
#define RD_REL_FADE_MS 500                /* reduced motion and the Reveal row's Fade: the final lid's cross-fade */
#define RD_REL_POP_MS 900                 /* the seal pops (no sound: pop_asset NULL, PR3) */
#define RD_REL_GONE_MS 1620               /* the swing's end: after it no lid is drawn (PR11) */

typedef struct {
    fk_rows_state_t rows;  /* the row renderer's state: theta, the pop's dy, the shade, the edge quad, rim and fade */
    uint32_t counter;      /* 0 .. 3 000 000, outQuart over 120-860 ms; final from 880 */
    uint8_t lamp;          /* 0 red off, 1 red on (140 ms blink), 2 green (from 880; stays red when unset) */
    uint8_t flash_a8;      /* the additive (200, 255, 215) cold flash, peak 13 % */
    uint8_t shimmer_a8;    /* .06 + .05 (.5 + .5 sin(t / 90)) while shut, x 255 */
    uint32_t wake_ms;      /* the interior's settle and thaw clock */
    bool final_lid;        /* the rd_st_lidf deltas apply (t >= 880) */
    bool lid_gone;         /* t > 1620: no lid row and no edge quad is drawn (the mockup's done: te > 1620). PR11 */
    /* appended: reduced motion (and Fade): the final lid's cross-fade over the settled face, 1 - outCubic(ph(t, 0,
     * 500)) x 255 (255 outside reduced, where the lid is drawn whole) */
    uint8_t door_a8;
    bool reduced, done;    /* the state is reduced's; done: the end frame (t >= the reveal's length, or a skip) */
} rd_rel_state_t;
void rd_rel_at(uint32_t t_ms, bool reduced, bool clock_set, rd_rel_state_t *st);
void rd_rel_skip(rd_rel_state_t *st);            /* a tap: the end frame */
/* The clock's state for the next reveal (the reveal ops see no face input): the theme sets it before the reveal binds
 * (rd_theme.c's woke hook, which fk_face calls before it starts the reveal), and bind latches it into the reveal's
 * context. Unset: the lamp stays red. true until set (the host's default). */
void rd_rel_set_clock(bool set);

/* The 14 mist puffs at t (seed 77, faces-reveal.js:485-490): centre, quantised radius, alpha; n = puffs alive */
typedef struct { int16_t x, y; uint8_t r_idx; uint8_t a8; } rd_puff_t;
int rd_rel_mist(uint32_t t_ms, rd_puff_t out[14]);
#define RD_MIST_RADII {40, 48, 56, 64, 72, 80, 88}   /* (80 + 100 p) / 2 to 8 px steps (RW9) */
#define RD_MIST_N_RADII 7
#define RD_SHIMMER_R 120                              /* the stasis field's glow behind the frost */

/* The counter's text (PR41, review I1): seven digits, zero-padded as the mockup's padStart(7, '0'), in groups of three
 * with a space, then " YRS": 0 -> "0 000 000 YRS", 678 012 -> "0 678 012 YRS"; n above 9 999 999 is clamped there.
 * Returns strlen (13); the caller right-aligns it at x 318 (RD_REL_COUNTER_X, baseline RD_REL_COUNTER_Y) */
int rd_rel_counter_str(uint32_t n, char out[16]);
#define RD_REL_COUNTER_X 318
#define RD_REL_COUNTER_Y 306

/* The lid's ramp (rd_st_lid's A8 is an index into it; tools/export/rd_layers.json's row names the same stops): the
 * steel run #141618 -> #2B2F34 holds indices 64-176, black below it, then steel, steelHi and paint for the frame, the
 * rivets and the lettering. Each channel between stops (i0, c0) and (i1, c1) is
 * (c0 (i1 - i) + c1 (i - i0) + (i1 - i0) / 2) / (i1 - i0), integer (the export's rampOf()). */
typedef struct { uint8_t i; uint8_t r, g, b; } rd_ramp_stop_t;
#define RD_LID_RAMP_STOPS 7
extern const rd_ramp_stop_t RD_LID_RAMP[RD_LID_RAMP_STOPS];
void rd_lid_ramp(uint16_t out[256]);              /* the ramp's 256 RGB565 colours */
/* any ramp's 256 RGB565 colours by the same rule, rounded (Task 11: Ship Terminal's housing, RD_TM_RAMP) */
void rd_ramp565(const rd_ramp_stop_t *stops, int ns, uint16_t out[256]);

/* The reveals' frame timing (review M5): each frame's wall time (from its reveal time to the next frame's) summed by
 * the window its time falls in: 0 the shut lid [0, 1010), 1 the swing [1010, 1620), 2 the heavy mist after the lid is
 * gone [1620, 1900), 3 the settle [1900, 2150]. Accumulated over reveals until reset (`rd lid bench`). */
typedef struct { uint32_t frames[4], ms[4], max_ms[4]; } rd_rel_timing_t;
void rd_rel_timing_reset(void);
void rd_rel_timing_get(rd_rel_timing_t *out);
/* one frame's wall time ms into window w (0-3): the frame ops' own call (wave 2 Task 4: the airlock notes its frames by
 * its own windows into the same accumulator; one reveal runs at a time) */
void rd_rel_timing_note(int w, uint32_t ms);

extern const fk_reveal_ops_t RD_REVEAL_STASIS;   /* release.c (rd_core, PR12): len 2150, reduced 500, pop 900,
                                                    pop_asset NULL (no latch sound, PR3); rctx: the composed lid
                                                    leaf (434 KB PSRAM, RW6), the counter recess's two versions and
                                                    the frame's rows, edge and state */
