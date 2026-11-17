#pragma once
/* The dw theme's circle script numerals, drawn live (spec §12): designer C's "ring-tally, evenly spread".
 *   Each line is one. The dot is five. A ring inside is zero.
 * So 1-4 are a ring with that many lines, 5 is a ring with a dot, 6-9 are the lines plus the dot, 0 is a ring with
 * a ring inside it, and DW_G_EMPTY ("time not known") is the ring alone. The lines run from 0.56 r (inside the ring)
 * to 1.28 r (outside it), spread evenly from 12 o'clock; no glyph is a rotation or mirror of another (spec §12).
 *
 * The glyphs are our own: a counting tally.
 *
 * r is the ring's radius: an "80 px ring" is r 40. Angles are clockwise from 12 o'clock. Pure C, no allocation.
 * Everything is drawn through omni/raster.h (exact area coverage), so a frame drawn in clips is the frame drawn
 * whole, byte for byte. The angles come from the in-tree sine table (omni/trig.h), not libm, so the host's bytes
 * are the device's. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"
#include "dw/clock.h"   /* DW_G_EMPTY */

#define DW_G_INNER 0.56f        /* zero's inner ring; also where every line starts */
#define DW_G_LINE_OUT 1.28f
#define DW_G_DOT 0.36f

typedef struct { uint8_t lines; bool dot, zero; } dw_glyph_t;

dw_glyph_t dw_glyph(int digit);                    /* 0..9, DW_G_EMPTY; anything else is the ring alone */
float dw_glyph_stroke(float r);                    /* max(1.6, 0.105 r) */
float dw_glyph_line_deg(int lines, int k);         /* 1: 0; 2: 0, 180; 3: 0, 120, 240; 4: 0, 90, 180, 270 */
/* Line k of `lines` as its quad: q[0], q[1] the start edge (at 0.56 r), q[3], q[2] the end edge (at 1.28 r). */
void dw_glyph_line_quad(float cx, float cy, float r, int lines, int k, omni_pt_t q[4]);
void dw_glyph_bbox(float cx, float cy, float r, omni_rect_t *out);   /* every pixel a glyph can touch, + 1 px */
/* Draw at (cx, cy), ring radius r, colour rgb565 at alpha a over fb (the raster's fb / stride / clip convention).
 * progress 0..1: the wake's draw-on (the ring sweeps clockwise over the first half, then the marks grow over the
 * second); 1 = the exact still glyph. Pieces are drawn one after another at alpha a, so below 255 the crossings of a
 * line and the ring are a little brighter than their surroundings: faces dim numerals by colour, never by alpha
 * below 255 (owner ruling). */
void dw_glyph_draw(uint16_t *fb, int stride, const omni_rect_t *clip, int digit, float cx, float cy, float r,
                   uint16_t rgb, uint8_t a, float progress);
/* (Task 15) dw_glyph_draw with stroke s in place of dw_glyph_stroke(r): the glances' thinner ink (faces-modern.js
 * drawDigit's lw). s is at most dw_glyph_stroke(r) (a wider or non-positive s draws the standard stroke), so the glyph
 * never reaches past dw_glyph_bbox and dw_glyph_clearance stays a bound. */
void dw_glyph_draw_w(uint16_t *fb, int stride, const omni_rect_t *clip, int digit, float cx, float cy, float r,
                     float s, uint16_t rgb, uint8_t a, float progress);
#if defined(DW_TEST_HOOKS)
/* (dw wave 2 Task 2) The host tests' hook, never in the firmware: every dw_glyph_draw and dw_glyph_draw_w call counts
 * here, so the harness can check that a face draws no numeral (test_faces_no_numerals_wave2). */
extern unsigned long dw_test_glyph_calls;
#endif
/* Clearance (px) between digit's outer extent (1.28 r + stroke / 2 with lines; r + stroke / 2 for the ring-only 0, 5
 * and DW_G_EMPTY) and a decorative circle's stroke (centre, radius, width); <= 0 means they touch (the hard rule: no
 * decorative line touches a numeral). */
float dw_glyph_clearance(int digit, float gx, float gy, float r, float dcx, float dcy, float dr, float dw);
