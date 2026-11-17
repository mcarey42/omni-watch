#pragma once
/* Anti-aliased raster for the Omni dial (Stage 3 Task 6): polygons, annuli and rounded rects with exact area coverage,
 * and the mockup's hand() (design/mockups/faces-omni.js 433) built from them. Pure C, no allocation.
 *
 * Coverage is each pixel's exact area inside the shape (non-zero winding, clamped at 1; a contour wound the other way
 * subtracts, which makes the hand's bevel stroke a ring), computed per pixel row by an integer cell accumulator on
 * vertices rounded to 1/256 px (the scheme of FreeType's gray rasterizer). The brief asked for 4x4 subsamples; those
 * cannot hold the 0.7 px bevel to +-10 of the oracle (a 0.7 px band takes 2 or 3 of 4 samples: 20-30 levels off,
 * test_hand_vs_oracle), so the area is exact instead. The sums are integers, so a pixel's coverage depends only on the
 * shape and the pixel, never on the clip: a frame drawn in rects is the frame drawn whole, byte for byte.
 * A fill at alpha a gives a pixel of coverage c (0..1) alpha round(a c), through omni_blend565 (full coverage: a).
 *
 * Drawing calls take fb as the pixel at (clip->x0, clip->y0) with row stride `stride` px (as omni_rings_draw); clip
 * NULL is the 466 x 466 screen with fb its (0, 0). Only pixels inside clip are written. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"   /* omni_pt_t, omni_rect_t (Task 2) */

#define OMNI_POLY_MAX 8    /* points a polygon may have; more are not drawn */
#define OMNI_COVER_W 128   /* the widest row run the coverage helpers take at once */

/* AA fill of a simple polygon (non-zero), exact area coverage per pixel (the brief: 4x4 subsamples, see above), over
 * fb (stride), clipped; colour rgb565 at alpha a (0..255) x coverage, through omni_blend565. */
void omni_fill_poly(uint16_t *fb, int stride, const omni_rect_t *clip, const omni_pt_t *p, int n, uint16_t rgb, uint8_t a);
/* Annulus r0 <= d < r1 (r0 0 = disc), as polygons of up to 64 sides whose areas are the circles'. */
void omni_fill_ring(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float r0, float r1,
                    uint16_t rgb, uint8_t a);
void omni_poly_bbox(const omni_pt_t *p, int n, omni_rect_t *out); /* + 1 px AA margin */

/* The mockup hand() (433): shadow (screen offset 1.4, 2.6, black .55), two facets lit by 0.5 + 0.5 cos(a + 45 deg),
 * the red tip, bevel strokes as 0.7/0.5 px polygons; k, cx, cy: affine (magnifier, loupe).
 * a: radians clockwise from 12 o'clock; hour: the hour hand (r 118..178, w 11) or the minute hand (118..214, 5.2).
 * A dial point q (the mockup's 1x screen coordinates) lands at (cx + k (q.x - sx), cy + k (q.y - sy)): the dial
 * itself is k 1 and cx = sx, cy = sy (all 0 will do); the magnifier and loupe pass their map's k, d and s
 * (omni_pmap_cfg_t), so the hand meets the rings their map draws. The shadow and strokes scale with k, as the
 * mockup's magnified composite does. The facets and the tip are the outline cut exactly (the mockup's clip), the
 * bevel is the outline grown and shrunk by 0.35 (mitred, as canvas strokes it). shadow false: no shadow (the fast
 * fidelity). */
void omni_draw_hand(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, bool shadow,
                    float k, float cx, float cy, float sx, float sy);
/* omni_draw_hand in two steps (Task 7, T6 M4): the hand built once (its outline, facets, tip, bevel and centre line
 * through the affine: the trig and the clipping) and drawn in any number of clips, each pixel as omni_draw_hand draws
 * it. The dial builds each hand once a frame instead of once per 16-row band. False: nothing to draw (a non-finite
 * angle or affine). The storage is opaque and self-referencing: prepare it where it will be drawn from (no copies). */
#define OMNI_HAND_BYTES 1024
typedef union { unsigned char b[OMNI_HAND_BYTES]; double d; void *p; } omni_hand_t;
bool omni_hand_prepare(omni_hand_t *h, float a, bool hour, bool shadow, float k, float cx, float cy, float sx, float sy);
void omni_hand_draw(const omni_hand_t *h, uint16_t *fb, int stride, const omni_rect_t *clip);   /* h prepared: true */
/* Every pixel omni_draw_hand(.., a, hour, true, 1, 0, 0, 0, 0) can touch (shadow and strokes, + 1 px). */
void omni_hand_bbox(float a, bool hour, omni_rect_t *out);
/* The facet colours at a: lit (34, 30, 26) + lit x (30, 26, 20), dark (14, 12, 10) + (1 - lit) x (16, 14, 10), each
 * truncated as the mockup's `| 0`, lit = 0.5 + 0.5 cos(a + 45 deg). */
void omni_hand_facets(float a, uint8_t lit[3], uint8_t dark[3]);

/* ---- Coverage rows, for compositing steps that colour per pixel (the loupe's gradient frame): each writes the
 * alpha a fill at 255 would use (round(255 c)) of pixel row y, columns [xa, xb) (xb - xa <= OMNI_COVER_W), to
 * cov[0 .. xb - xa). */
void omni_cover_poly(const omni_pt_t *p, int n, int y, int xa, int xb, uint8_t *cov);
void omni_cover_ring(float cx, float cy, float r0, float r1, int y, int xa, int xb, uint8_t *cov);
/* The rounded rect [x0, x1) x [y0, y1), corner radius r (canvas rrect: arcs as 10 segments each). */
void omni_cover_rrect(float x0, float y0, float x1, float y1, float r, int y, int xa, int xb, uint8_t *cov);
/* The same in two steps (Task 7): the outline built once (its 44 arc points: the trig), then the coverage of any rows;
 * omni_cover_rrect is the two. Opaque and self-referencing: prepare it where it is used from. False: no shape. */
#define OMNI_RRECT_BYTES 512
typedef union { unsigned char b[OMNI_RRECT_BYTES]; double d; void *p; } omni_rrect_t;
bool omni_rrect_prepare(omni_rrect_t *rr, float x0, float y0, float x1, float y1, float r);
void omni_cover_rrect_prepared(const omni_rrect_t *rr, int y, int xa, int xb, uint8_t *cov);
/* omni_cover_ring in two steps (fix wave, R140): the two contours built once (up to 2 x 64 vertices: the trig, about
 * 130 sinf/cosf), then the coverage of any rows; omni_cover_ring is the two. A caller covering a whole ring row by row
 * prepares it once instead of once per row chunk (the almanac's sky band and the timer's scale ring were rebuilding it
 * about a thousand times a render). Opaque and self-referencing: prepare it where it is used from. False: no area
 * (the coverage is then all 0). */
#define OMNI_RING_BYTES 1152
typedef union { unsigned char b[OMNI_RING_BYTES]; double d; void *p; } omni_ring_t;
bool omni_ring_prepare(omni_ring_t *rg, float cx, float cy, float r0, float r1);
void omni_cover_ring_prepared(const omni_ring_t *rg, int y, int xa, int xb, uint8_t *cov);
