#pragma once
/* The procedural seven-segment digit (rd wave 2 Task 6; faces-bridge.js:213-222 segPaths() and segDigit(), the Drive
 * Console's readout; Vending Machine's too): slanted hexagonal segments, no atlas. A digit of height h is h x 54 / 96
 * wide (the console's 54 x 96), its segments h x 11 / 96 thick, slanted by .07 (a point y below the digit's top moves
 * (h - y) x .07 right), with a 1.4 x h / 96 px gap at each segment end. A digit's (x, y) is its box's top-left before the
 * slant. Every draw call takes fb as the pixel at (clip.x0, clip.y0) with row stride `stride` px (fk/compose.h); clip
 * NULL: the screen, fb at (0, 0). Pure: no esp_*, freertos or LVGL; no libm; no allocation. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/face.h"
#include "omni/geom.h"

/* the segments a..g (bit 0 = a ... bit 6 = g) a character lights: '0'-'9', '-' (g alone) and ' ' (none); any other
 * character lights none */
#define RD_SEG7_A 0x01
#define RD_SEG7_G 0x40
#define RD_SEG7_ALL 0x7F
unsigned rd_seg7_segs(int ch);
/* segment seg's (0 = a ... 6 = g) hexagon for the digit at (x, y) of height h, after the slant */
void rd_seg7_poly(int seg, float x, float y, float h, omni_pt_t out[6]);
/* the digit's width (h x 54 / 96) and its slant's reach (the top's shift right, h x .07) */
float rd_seg7_width(float h);
float rd_seg7_slant(float h);

/* One digit: each segment ch lights filled in on (outline: stroked 2 px in on, the glance's outline, and the unlit
 * ones left out); each unlit segment filled in ghost, unless ghost is 0 (none: the caller's layer holds the ghosts) or
 * the digit is outlined. */
void rd_seg7_digit(uint16_t *fb, int stride, const omni_rect_t *clip, int ch, float x, float y, float h, uint16_t on,
                   uint16_t ghost, bool outline);
/* The four digits' x (Math.round'ed, the mockup's digitXs() at height h, centred on cx: 54, 10, 22 and 10 px of 96 for
 * a digit, the gap, the colon's room, the gap) */
void rd_seg7_xs(float cx, float h, float xs[4]);
/* The four characters of the time in (the hours and the minutes, no colon): 24 h "0959"; 12 h " 959" (the leading
 * digit blank, its ghost kept) and "1000"; unset "----" */
void rd_seg7_str(const fk_face_in_t *in, char out[5]);
/* The time (rd_seg7_str) as four digits at rd_seg7_xs(cx, h) on row y; colon: the awake colon's two squares (10 x h /
 * 96, the mockup's fillRect(cxl - 4, y + .3 h - 5) and (cxl - 7, y + .7 h - 5), cxl = xs[1] + 54 + 24 at 96) in on */
void rd_seg7_time(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_face_in_t *in, float cx, float y,
                  float h, uint16_t on, uint16_t ghost, bool colon, bool outline);
