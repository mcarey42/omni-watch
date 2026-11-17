#pragma once
/* The brass iris reveal (spec §7.5, Stage 3 Task 10): nine blades opening over the dial, drawn procedurally (the
 * measured method), as the mockup's drawIris (design/mockups/faces-omni.js 736) and the iris entry's
 * timeline (makeRevealEntry 949-956). Pure C, single precision in a fixed order (-ffp-contract=off), no allocation:
 * a frame is prepared once (omni_iris_prepare) and drawn in any rects by any number of cores at once (read-only).
 *
 * Geometry (screen pixels, centre C = (233, 233)): blade i's angle a_i = i 40 deg + phi; the aperture is the regular
 * 9-gon of inradius ap, vertex V(i) at angle a_i - 20 deg and radius ap / cos 20 deg; edge i runs V(i) -> V(i + 1),
 * outward normal n_i = (cos a_i, sin a_i), tangent t_i = (-sin a_i, cos a_i). Edge i continues past V(i + 1) as a
 * gently curving ray: V(i + 1) + t_i s + n_i s^2 / 900 at s = 0, 60 .. 480 (8 segments; it leaves the disc). Blade i's
 * region is bounded by edge i, its own ray and the ray of edge i - 1 (from V(i)): the nine regions partition the plane
 * outside the aperture (test_iris_partition). */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"

#define OMNI_IRIS_N 9
#define OMNI_IRIS_R 199                      /* the iris is clipped to the disc r < 199 about the centre */
#define OMNI_IRIS_AP_OPEN (OMNI_IRIS_R + 8)  /* ap = s x 207: fully open (the mockup's IRIS_R + 8) */
#define OMNI_IRIS_GONE (OMNI_IRIS_R + 4)     /* from ap 203 on no blade is drawn (the dim alone remains until s = 1) */
#define OMNI_IRIS_BOX ((omni_rect_t){ 34, 34, 432, 432 })   /* every pixel the iris can touch: the disc's box (C12) */

/* The timeline t_ms after the wake (mockup: anticip 260-560, open 480-1420): tt = t (t x 2.2 reduced, as the mockup's
 * tt); anticip = outCubic(ph(tt, 260, 430)) - inOutCubic(ph(tt, 430, 560)); s = inOutCubic(ph(tt, 480, 1420));
 * ap = s x 207, phi = -6 deg anticip + 62 deg s (radians here). s is ap / OMNI_IRIS_AP_OPEN. */
void omni_iris_at(uint32_t t_ms, bool reduced, float *ap, float *phi_rad);
/* blade i's region (drawIris 736): v0, v1, the 8 points of its curved ray, the 8 of its neighbour's, reversed.
 * Returns 18. */
int omni_iris_region(float ap, float phi, int i, omni_pt_t out[18]);

/* The whole procedural iris over fb inside r 199: dim .55 (1 - s) under it, shadow ring, 9 blades (gradient along the
 * blade normal, lum .3 + .17 cos(a + 135 deg), brushed grain from omni_blade in blade-local coords), overlap shadows,
 * polished edges, closed-centre glint. grain: omni_blade's A8 plane (256 x 512, row stride 256: the bytes after its
 * 12-byte LVGL header), or NULL (no grain). Its rows are identical (the mockup draws full-height stripes), so row 0
 * alone is read: the grain is a function of the blade-local x (the distance from V(i) along n_i), and every blade pixel
 * in the disc lies inside the texture's drawn rows (local y in [-40, 480), test_iris_vs_oracle checks it). The grain is
 * stored as the mockup texture's luminance over grey 128 (export.js): g > 128 lightens toward (255, 232, 180) by
 * (g - 128) / 107, g < 128 darkens toward black by (128 - g) / 128.
 * fb is the pixel at (clip->x0, clip->y0), row stride `stride` px; clip NULL is the 466 x 466 screen with fb its (0, 0).
 * Only pixels inside clip and within r 199.71 of the centre are written. Where the blades are opaque the pixel is
 * written without reading fb (omni_iris_opaque says where). Each pixel's value depends only on the frame and the
 * pixel, never on the clip.
 * Stack: omni_iris_draw prepares into a local omni_iris_t (16 KB) plus a row's scratch (2.9 KB): host and tests only
 * (the device's worker stacks are 16 KB); the device prepares once into PSRAM and calls omni_iris_draw_prepared, whose
 * stack is the row scratch (about 3.5 KB in all). */
void omni_iris_draw(uint16_t *fb, int stride, const omni_rect_t *clip, float ap, float phi, float s,
                    const uint8_t *grain);

/* The same in two steps: the frame's geometry and per-blade colour tables built once (prepare), then any number of
 * clips drawn from it, by any number of cores at once (read-only; opaque storage holding no pointers, so it may be
 * copied). prepare returns false when there is nothing to draw (s >= 1: fully open). */
#define OMNI_IRIS_BYTES 16384
typedef union { unsigned char b[OMNI_IRIS_BYTES]; double d; void *p; } omni_iris_t;
bool omni_iris_prepare(omni_iris_t *ip, float ap, float phi, float s, const uint8_t *grain);
void omni_iris_draw_prepared(const omni_iris_t *ip, uint16_t *fb, int stride, const omni_rect_t *clip);
/* Rows [y0, y1), columns [x0, x1): out = { o0, o1, h0, h1 }: the columns in [o0, o1) the iris writes from the blades
 * alone (never reading fb) in every one of those rows, but for the hole [h0, h1) (the aperture, which shows the dial),
 * with a margin. o0 >= o1: none (the frame under the iris is needed everywhere). A compositor may leave the frame
 * undrawn in [o0, h0) and [h1, o1). */
void omni_iris_opaque(const omni_iris_t *ip, int y0, int y1, int x0, int x1, int16_t out[4]);
/* How many times a row's capacity (its events, active lines or ray crossings) has overrun since boot: each overrun
 * drops an entry, so a nonzero count means some frame drew wrong. 0 in every measured frame (the margins are wide); on
 * the host an overrun also fails an assert. `dial` prints it. */
uint32_t omni_iris_overflows(void);
