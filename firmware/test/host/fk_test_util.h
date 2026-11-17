#pragma once
/* The face harness's shared helpers (dw wave 2 Task 2, R147 PR10): host only, linked into test_dw_faces and
 * test_dw_faces_overlay. They were test_dw_faces.c's own (whole, clock_in, the glance's lit scan, ORB9) and live here
 * alone now, so every face task's tests draw, time, measure and tap a face the same way. */
#include <stdbool.h>
#include <stdint.h>
#include "dw/faces.h"
#include "omni/geom.h"

#define DW_TEST_W 466   /* the frame: DW_TEST_W x DW_TEST_W, stride DW_TEST_W */

/* the 9 glance orbit positions the sweeps visit (omni_orbit_offset's reach: the centre, 6 around, the two extremes) */
extern const int8_t ORB9[9][2];

/* the frame of ops at in (whole, 466 x 466): glance when in->glance, else draw, on in->page; fb is first filled with
 * 0x5A bytes, so a pixel the face leaves undrawn shows */
void fk_test_frame(const fk_face_ops_t *ops, const void *ctx, const fk_face_in_t *in, uint16_t *fb);
/* in at a wall-clock instant: h, m, s, ms on a fixed date (Sun 1 Nov); settled; reduced and glance as given */
void fk_test_in_at(fk_face_in_t *in, int h, int m, int s, int ms, bool reduced, bool glance);
/* lit % (any channel > th, inside the disc) and the maximum channel (rd wave 1 Task 1 review: rd's harness counts at
 * th 8, tools/shot.py --lit's rule) */
void fk_test_lit_th(const uint16_t *fb, int th, float *pct, uint8_t *max_ch);
/* fk_test_lit_th at th 12 (the first theme's rule) */
void fk_test_lit(const uint16_t *fb, float *pct, uint8_t *max_ch);
/* every corner and edge midpoint of r (inclusive) taps `expect`; one px outside each edge midpoint does not.
 * r is half-open (x0 <= x < x1), so the inclusive probes are x0 / y0 and x1 - 1 / y1 - 1, and the outside probes
 * x0 - 1 / y0 - 1 and x1 / y1. */
void fk_test_tap_edges(const fk_face_ops_t *ops, const void *ctx, omni_rect_t r, fk_tap_t expect);
