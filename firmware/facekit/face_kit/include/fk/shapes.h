#pragma once
/* The faces' small shapes (shapes.c): raster polygons (omni/raster.h) and analytic discs and glows, every angle through
 * the in-tree sine table (omni/trig.h), so host and device draw the same bytes; each shape is a pure function of the
 * pixel (band-exact). Angles are radians clockwise from 12 o'clock; the screen centre is 233. Every draw call takes fb
 * as the pixel at (clip.x0, clip.y0) with row stride `stride` px (fk/compose.h); clip NULL: the screen, fb at (0, 0). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/geom.h"

void fk_sincos(float a, float *s, float *c);   /* the table and a second-order step: |error| < 2e-6 */
/* A closed polygon's outline, stroked width w (mitred, limit 10 as canvas strokes it), at alpha a8; n <= OMNI_POLY_MAX. */
void fk_poly_stroke(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float w,
                    uint16_t rgb, uint8_t a8);
/* An additive fill ('lighter'): coverage x a8 of rgb added per channel, saturating (fk_add_a8's rule). */
void fk_poly_add(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, uint16_t rgb, uint8_t a8);
/* A disc (anti-aliased by its edge distance), at alpha a8. */
void fk_disc(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, uint16_t rgb, uint8_t a8);
/* The mockups' glowSprite() as an additive radial glow of radius r (alpha 1, .45, .12, 0 at 0, .25, .6, 1 of r) at
 * a8; ring_r0 < ring_r1 confines it to the screen annulus [ring_r0, ring_r1) about 233 (0, 0: unconfined). */
void fk_glow_add(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, uint16_t rgb,
                 uint8_t a8, float ring_r0, float ring_r1);
/* fk_glow_add's ramp as a table, for an unconfined glow about a whole-pixel centre (Task 12: its sqrtf and divide are
 * software routines on the device). A pixel there lies at N / 4 squared px from the centre, N = (2 dx)^2 + (2 dy)^2 an
 * integer = 2 mod 8, so entry (N - 2) / 8 holds the alpha fk_glow_add works out, by the same single-precision steps;
 * N >= 4 r^2 is past the rim (alpha 0). FK_GLOW_LUT_N(r) entries; r <= 255.
 * A large radius (r >= FK_GLOW_COARSE_R; wave 2 Task 7 fix round, R148 (c)) has a coarse table instead: entry k holds
 * the alpha at N = 64 k (the same steps), and a pixel interpolates linearly between entries N >> 6 and N >> 6 + 1, in
 * integers (about 1/8 the bytes: 3 395 B for r 233, not 27 145). Its alpha is within 4 levels of fk_glow_add's (the square
 * root's bend near the centre), its pixels within one 565 step. */
#define FK_GLOW_COARSE_R 128
#define FK_GLOW_LUT_N(r) \
    ((r) >= FK_GLOW_COARSE_R ? (size_t)(((r) * (r) * 4 - 1) / 64 + 2) : (size_t)(((r) * (r) * 4 - 2 + 7) / 8))
#define FK_GLOW_LUT_MAX FK_GLOW_LUT_N(FK_GLOW_COARSE_R - 1)   /* the largest table of any radius <= 255 */
void fk_glow_lut_build(uint8_t *lut, int r);
/* fk_glow_add(fb, stride, clip, cx, cy, r, rgb, a8, 0, 0), byte for byte, from lut (fk_glow_lut_build(lut, r)), for
 * r < FK_GLOW_COARSE_R; for a coarse table, its interpolation. */
void fk_glow_add_lut(uint16_t *fb, int stride, const omni_rect_t *clip, int cx, int cy, int r, const uint8_t *lut,
                     uint16_t rgb, uint8_t a8);

/* ---- Task 13's shapes (hands.c) */
/* A circle stroked width w at radius r (pixel coverage by the edge distances), at alpha a8. */
void fk_ring(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, float w, uint16_t rgb,
             uint8_t a8);
/* A segment (x0, y0)-(x1, y1) stroked width w, butt caps (a raster quad), at alpha a8. */
void fk_line(uint16_t *fb, int stride, const omni_rect_t *clip, float x0, float y0, float x1, float y1, float w,
             uint16_t rgb, uint8_t a8);
/* The glances' ink ('lighten'): fk_ring, fk_line and fk_poly_stroke, each pixel the larger per channel of itself and
 * the stroke's colour at its coverage over black, so strokes that cross never add up past their own level. */
void fk_ring_max(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r, float w, uint16_t rgb,
                 uint8_t a8);
void fk_line_max(uint16_t *fb, int stride, const omni_rect_t *clip, float x0, float y0, float x1, float y1, float w,
                 uint16_t rgb, uint8_t a8);
void fk_poly_stroke_max(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float w,
                        uint16_t rgb, uint8_t a8);
/* A polygon filled by a linear gradient from (gx0, gy0) to (gx1, gy1) through ns evenly spaced stops (2..4), padded
 * past its ends as canvas pads, at alpha a8. */
void fk_poly_grad(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, float gx0, float gy0,
                  float gx1, float gy1, const uint16_t *stops, int ns, uint8_t a8);

/* ---- The helpers a theme's own hands build on (PR1d): shapes.c's internals, exported as they are */
/* v truncated to int16, clamped to +-32000 (a NaN gives -32000) */
int16_t fk_to16(float v);
/* clip (NULL: the screen with fb at its (0, 0)) cut by the box r: the rows and columns to visit; false if empty */
bool fk_span(const omni_rect_t *clip, const omni_rect_t *r, int *x0, int *y0, int *x1, int *y1);
/* the address of screen pixel (x, y) in fb (clip's origin, row stride `stride`; NULL clip: the screen) */
uint16_t *fk_px_at(uint16_t *fb, int stride, const omni_rect_t *clip, int x, int y);
/* p offset by d along its outward normals, mitred (each miter at most 10 x |d| long, as canvas's limit) */
void fk_offset_poly(const omni_pt_t *p, int n, float d, omni_pt_t *out);
/* every pixel a disc of radius r about (cx, cy) can touch */
void fk_disc_box(float cx, float cy, float r, omni_rect_t *bb);
/* A linear gradient: pos NULL, the ns stops evenly spaced; else their positions (0 = pos[0] < .. < pos[ns - 1] = 1) */
typedef struct { float x0, y0, vx, vy, inv; const uint16_t *stops; int ns; const float *pos; } fk_grad_t;
/* the gradient from (gx0, gy0) to (gx1, gy1) through stops (evenly spaced: pos NULL; set .pos for placed stops) */
fk_grad_t fk_grad_of(float gx0, float gy0, float gx1, float gy1, const uint16_t *stops, int ns);
/* the annulus [r0, r1) about (cx, cy), each pixel's coverage by its centre's distance (box filtered), in g's colours at
 * alpha a8; max: each pixel the larger per channel of itself and the colour at its coverage (fk_ring_max's ink) */
void fk_ring_fill(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r0, float r1,
                  const fk_grad_t *g, uint8_t a8, bool max);
/* a polygon (n <= OMNI_POLY_MAX) filled by g at alpha a8 (fk_poly_grad's fill, any gradient) */
void fk_poly_grad_g(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, const fk_grad_t *g,
                    uint8_t a8);
