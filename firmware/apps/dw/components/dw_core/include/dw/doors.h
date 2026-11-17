#pragma once
/* The doors (spec §10; dw wave 1 Task 5): the column renderer, the timeline and the band compositor. Pure C, single
 * precision in a fixed order (-ffp-contract=off), no allocation: the device builds the frames the host tests pin.
 *
 * Two flat vertical-axis leaves hinged at x = 0 and x = 466, each 233 source columns of one 466 x 466 closed-doors
 * image; F 560; outward (toward the viewer) only in wave 1. The column maps are the lid's hinge engine
 * (omni_hinge_lines_at) over the vertical axis, each leaf clipped to its 233 columns. Any door art of that geometry
 * works: the repo carries the generic doors and the private build swaps in its own (R132); nothing here knows which.
 * Wave 2 (Task 3): a door kind per face (fk_cols_kind_t): wave 1's outward leaves that swing past the panel, or the
 * inward leaves that stay open (#3). The un-suffixed functions are the _k forms with &DW_DOORS_OUT. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/cols.h"   /* the column renderer, the kinds and the state (face_kit, rd wave 1 Task 1) */
#include "fk/reveal.h"
#include "omni/geom.h"
#include "omni/lid.h"

extern const fk_cols_kind_t DW_DOORS_OUT;       /* { false, false, DW_DOORS_OPEN_DEG }: #1, #2 (wave 1's) */
extern const fk_cols_kind_t DW_DOORS_IN_KEEP;   /* { true, true, 86 }: #3 (notes-reveal.md: inward, holds at 86) */
/* Every column x of 466: the left leaf's over x < 233, the right leaf mirrored about 233 over x >= 233 (an outward
 * leaf never crosses the centre line: its points scale away from the eye's line). */
void dw_doors_cols(float th_left, float th_right, fk_col_t cols[466]);


/* The timeline. The device default is paced like the lid's (spec §10 table, D13); DW_DOORS_MOCKUP (to 1) selects
 * Designer A's 2.3 s timeline (notes-reveal.md) for the owner's A/B: CONFIG_DW_DOORS_MOCKUP on the device (Kconfig,
 * dw_core's CMakeLists defines it PUBLIC), the test_dw_doors_mockup build on the host. */
#if defined(DW_DOORS_MOCKUP) && DW_DOORS_MOCKUP
/* Designer A (notes-reveal.md): fade 0-180, seam 110-300, pop 250-420, swing from 380 for 780 ms */
#define DW_DOORS_SWING_T0_MS 380
#define DW_DOORS_SWING_MS 780
#define DW_DOORS_END_MS 2300
#else
/* the device default (spec §10 table, D13) */
#define DW_DOORS_SWING_T0_MS 150
#define DW_DOORS_SWING_MS 600
#define DW_DOORS_END_MS 1800
#endif
#define DW_DOORS_LEAD_MS 80        /* the right leaf leads */
#define DW_DOORS_POP_DEG 2.0f
#define DW_DOORS_OPEN_DEG 80.0f
#define DW_DOORS_REDUCED_MS 500    /* reduced motion: a cross-fade, no swing */

/* The state t_ms after the doors start (an elapsed time, not a timestamp). Each leaf pops to DW_DOORS_POP_DEG by the
 * swing's start and holds there until its own swing, an in-out cubic (omni_flip_theta's) from the pop to
 * DW_DOORS_OPEN_DEG over DW_DOORS_SWING_MS; the right leaf from DW_DOORS_SWING_T0_MS, the left DW_DOORS_LEAD_MS later.
 * Reduced (Designer A's order): no theta leaves 0; the opening fade as usual, with the closed doors at full weight;
 * then xfade 255 -> 0 by 1 - outCubic from the fade's end to DW_DOORS_REDUCED_MS; tail 255 (nothing grows); done from
 * DW_DOORS_REDUCED_MS. At and past the end, the state is dw_doors_skip's (reduced: with
 * both thetas 0). leaves counts a leaf's free-edge thickness too, which outlives its face to about 69.6 deg; with no
 * leaf, the light out and the fade up, the band gives the interior exactly, done or not. */
void dw_doors_at(uint32_t t_ms, bool reduced, fk_cols_state_t *st);
void dw_doors_skip(fk_cols_state_t *st);   /* a tap: the end state */
uint32_t dw_doors_pop_ms(void);             /* when the latch pop starts (the latch sound plays then, WD15) */
/* The kind's timeline and skip (wave 2 Task 3): as above with k's open_deg and keep. With keep, the reduced and Fade
 * end state (both thetas 0 above) has both leaves at open_deg: the closed doors cross-fade over the face, kept leaves
 * and all (notes-reveal.md: "the kept leaves fade in with the face"), and the end is keep's done state. */
void dw_doors_at_k(const fk_cols_kind_t *k, uint32_t t_ms, bool reduced, fk_cols_state_t *st);
void dw_doors_skip_k(const fk_cols_kind_t *k, fk_cols_state_t *st);

/* The doors as face_kit's reveal (rd wave 1 Task 2): dw_doors_at_k (or dw_doors_skip_k), fk_cols_k, fk_rs_plan and
 * fk_cols_band, the sequence the view ran itself until then. len DW_DOORS_END_MS, reduced DW_DOORS_REDUCED_MS, pop
 * dw_doors_pop_ms(), pop_asset ASSET_DW_LATCH; arg: the face's door kind (NULL: DW_DOORS_OUT); asset: the face's door
 * image (NULL: ASSET_DW_DOORS_M). Every doors face sets it as its reveal_ops. */
extern const fk_reveal_ops_t DW_REVEAL_DOORS;
