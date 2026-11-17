#pragma once
/* The airlock (rd wave 2 Task 4; spec section 10.2; faces-reveal.js airR() 597-611, drawLeaf() 575-590 and
 * frame_airlock() 689-729): Bay Mouth's wake. Two blast doors hinged at the screen's edges crack 7 deg, jam (a shudder,
 * dust, the red lamp flashing), get kicked and bang open toward you to 84 deg, while the bay settles underneath. The
 * timeline is pure (rd_air_at); RD_REVEAL_AIRLOCK (airlock.c, in rd_core) runs it on fk_view's direct path through
 * face_kit's column renderer (fk/cols.h) with the airlock's own leaf look, over Bay Mouth as its interior, and ends on
 * Bay Mouth's settled frame.
 *
 * Pure: no esp_*, freertos or LVGL; no allocation; no libm (lesson 6): every sine through fk_sincos and every
 * exponential from a const table, except the column renderer's own trig inside face_kit for a swinging leaf, so a frame
 * mid-swing is pinned per machine, never compared host to device. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/cols.h"
#include "fk/reveal.h"
#include "rd/release.h"   /* rd_ramp_stop_t, rd_ramp565, rd_rel_timing_t */

#define RD_AIR_MS 2000          /* the whole reveal */
#define RD_AIR_FADE_MS 500      /* reduced motion and the Reveal row's Fade: the shut doors' cross-fade */
#define RD_AIR_DONE_MS 1680     /* the leaves' end (cols.done after it): not the reveal's end */
#define RD_AIR_SETTLED_MS 1950  /* the reveal's settled end: Bay's settle ends (I1) */
#define RD_AIR_LAMP_Y 72        /* the lamp's row (the mockup draws it at y 72, unprojected) */
#define RD_AIR_DUST_MAX 9

/* drawLeaf's look (:575-590): .7, .08, .8; .5, 6, (255, 110, 60); 12; #121416, (255, 120, 64), .6 */
extern const fk_cols_look_t RD_AIR_LOOK;
/* outward, no keep, open_deg 84, pop_deg 7, look &RD_AIR_LOOK (each frame's copy sets its own pop: rd_air_kind_at) */
extern const fk_cols_kind_t RD_AIR_KIND;

typedef struct {
    fk_cols_state_t cols;   /* th_l = th_r = theta; fade always 255 (I7: rd fades once, in its band's step 4); xfade
                               the shut doors' weight under reduced and Fade (M5: 255 x (1 - outCubic(ph(t, 0, 500))));
                               leaves; done (t > 1680: the leaves alone, not the reveal's end, I1) */
    int8_t sx, sy;          /* the shake in whole px (rounded); 0 outside 600-900 and 1080-1400 */
    uint8_t lamp;           /* 0 off, 1 red, 2 amber (:601) */
    uint8_t beacon_a8;      /* ph(150, 300) (1 - ph(1300, 1600)), drawn only while theta < 12 deg */
    float beacon_ang;       /* t / 260 x 2 pi */
    uint8_t gap_a8;         /* the starlight through the gap (:700-708): its peak, .28 clamp(gap / 40) (1 - ph(theta,
                               15, 45 deg)) */
    uint8_t fade8;          /* the opening fade, ph(t, 0, 160) x 255, applied once by rd's band (I7) */
    bool settled;           /* t >= 1950 (Bay's settle ends) or a skip: the reveal's end (I1); reduced: t >= 500 */
    uint32_t wake_ms;       /* the interior's clock (Bay's settle and drop): t */
} rd_air_state_t;
void rd_air_at(uint32_t t_ms, bool reduced, rd_air_state_t *st);
void rd_air_skip(rd_air_state_t *st);       /* a tap: the end frame */

typedef struct { int16_t x, y; uint8_t r; uint8_t a8; } rd_dust_t;   /* r: one of RD_SOFT_LUT_RADII */
/* the 4 jam and 5 kick puffs (:722-726) alive at t (a8 > 0), sizes 26 (.4 + e) and 44 (.4 + e), r = max(16, the size
 * rounded to 8 px) (M6), centres rounded; drawn additively in (150, 140, 128) from the soft tables. Returns the count */
int rd_air_dust(uint32_t t_ms, rd_dust_t out[RD_AIR_DUST_MAX]);
/* The frame's kind (review I1): RD_AIR_KIND with pop_deg set so face_kit's leaf light (clamp(th / pop_deg, 0, 1))
 * is airR's rim light, outCubic(ph(t, 450, 700)); the kind's own pop while th is 0. Only the light changes: pop_deg is
 * read by nothing else. The reveal hands this kind to fk_cols_k and fk_cols_band. */
void rd_air_kind_at(uint32_t t_ms, float th, fk_cols_kind_t *k);
/* the lamp's x: the left leaf's projection of d 203 (leafProj(0, th)(C - 30)) */
float rd_air_lamp_x(float th);
/* The lamp alone (the band's own call; exposed for test_rd_air_lamp_on_leaf): lamp 1 red or 2 amber, a disc r 5 at
 * (rd_air_lamp_x(th) + dx, RD_AIR_LAMP_Y + dy), then its glowAt r 26 at .7 ('lighter', the soft table, at the rounded
 * centre); nothing for lamp 0 or th >= 12 deg. fb is the pixel at (clip.x0, clip.y0), stride 466. */
void rd_air_lamp_draw(uint16_t *fb, omni_rect_t clip, float th, uint8_t lamp, int dx, int dy);

/* The doors' ramp (rd_by_doors's A8 is an index into it; tools/export/rd_layers.json's row names the same stops):
 * segmented as RD_CU_LID_RAMP: 0-127 the steel (black, the leaves' two gradients #1A1D20 to #30353A, then steel and
 * steelHi), 128-191 the steel to paint, 192-255 the steel to hazard. rd_ramp565's rule, rounded to RGB565. */
#define RD_BY_DOOR_RAMP_STOPS 9
extern const rd_ramp_stop_t RD_BY_DOOR_RAMP[RD_BY_DOOR_RAMP_STOPS];
#define RD_BY_KICK_Y 312        /* rd_by_kick's first row on the leaf (466 x 50) */

/* The airlock's frame timing (`rd reveal bench`): rd_rel_timing's accumulator (one static for both reveals), by the
 * airlock's windows: 0 shut [0, 450), 1 jam [450, 1080), 2 kick [1080, 1680), 3 settle [1680, 1950] */
#define RD_AIR_WIN_SHUT 450
#define RD_AIR_WIN_JAM 1080

extern const fk_reveal_ops_t RD_REVEAL_AIRLOCK;   /* airlock.c: len 2000, reduced 500, pop 0, pop_asset NULL; rctx:
                                                     the composed leaf (466 x 466 RGB565, 434 312 B), fk_col_t[466],
                                                     rd_air_state_t and the dust list, which band() reads (M18) */
