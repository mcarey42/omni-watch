#pragma once
/* The analog face's seconds hand geometry (design/mockups/faces-practical.js globe-analog, frame lines 712-719):
 * along its length in canvas y (12 o'clock up, negative toward the tip), about the dial centre. */
#include <stdbool.h>
#include <stdint.h>

#define OMNI_DIAL_SEC_TAIL 42
#define OMNI_DIAL_SEC_TIP (-206)
#define OMNI_DIAL_SEC_TIP_FROM (-192)    /* the red tip */
#define OMNI_DIAL_SEC_TIP_TO (-210)
#define OMNI_DIAL_SEC_DOT 34             /* the counterweight, r 5 */
#define OMNI_DIAL_SEC_SH_DX 2            /* the shadow's offset */
#define OMNI_DIAL_SEC_SH_DY 3
#define OMNI_DIAL_SEC_PAD 7              /* round caps, the dot and anti-aliasing beyond the end points */

/* Canvas (lx, ly) rotated by a (radians, clockwise from 12) about (cx, cy), rounded to whole pixels. */
void omni_dial_rot(float a, float lx, float ly, int32_t cx, int32_t cy, int32_t *x, int32_t *y);
/* The box (x1, y1, x2, y2 inclusive) the seconds hand and its shadow cover at angle a about (cx, cy). */
void omni_dial_sec_box(float a, int32_t cx, int32_t cy, int32_t box[4]);
/* The most pixels a seconds box holds at any angle: the largest (w + 1)(h + 1) over 0.01 deg steps (a row and a
 * column to spare for the rounding between steps). Pinned by test_dial; the face sizes its sprite with it rather
 * than search at create (126 ms on the device). */
#define OMNI_DIAL_SEC_BOX_PX 38809u

/* An hour or minute hand image (and its shadow, the same size at (sdx, sdy)): its size and the pivot pixel that sits
 * on the dial centre. */
typedef struct { int32_t w, h, px, py, sdx, sdy; } omni_dial_hand_t;
/* The box (x1, y1, x2, y2 inclusive) hand h covers at rot (0.1 deg, clockwise from 12) about (cx, cy), with its
 * shadow if shadow, 2 px out for rounding and the edge's anti-aliasing; returns its pixels. */
uint32_t omni_dial_hand_box(const omni_dial_hand_t *h, int32_t rot, bool shadow, int32_t cx, int32_t cy, int32_t box[4]);
/* The most pixels the hour and minute boxes hold at any whole 0.1 deg rotation (shadow included; a row and a column
 * to spare, as the seconds bound): the face sizes its sprites with them rather than search at create (about 100 ms
 * on the LVGL context). Pinned by test_dial for assets_meta.h's ana_hour and ana_min; a re-export that changes their
 * size or pivot fails that test until these are re-pinned (on the device a box over its sprite is logged and not
 * drawn). */
#define OMNI_DIAL_HOUR_BOX_PX 23562u
#define OMNI_DIAL_MIN_BOX_PX 46225u
