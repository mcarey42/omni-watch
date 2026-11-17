#pragma once
/* One polar annulus (face_kit, from the compositor's header): a per-pixel map that turns a strip about a ring, for the
 * faces' turning script rings. Pure C, no libm per pixel, no allocation. */
#include <stddef.h>
#include <stdint.h>
#include "fk/compose.h"
#include "omni/geom.h"
#include "omni/ring.h"

/* ---- One annulus turned by a strip offset: a per-pixel (theta, radius row) map built once; the strip is ORS1.
 * Convention, shared with the theme's asset packer (C12): strip column 0 is 12 o'clock, columns run clockwise, row 0
 * is r0.
 *
 * The map is ring.h's variant A (the plain LUT) for one ring of any radius about any centre: per annulus pixel a
 * uint16 theta index and a uint8 radius row (3 B), kept in row runs (at most two a row) with a 12-byte record per row.
 * A pixel is in the annulus when its centre's r^2 = X^2 + Y^2 (X = x + 0.5 - cx right, Y = cy - y - 0.5 up) satisfies
 * r0^2 <= r^2 < r1^2; its row is the largest h with (r0 + h)^2 <= r^2, and its theta index floor(theta n / 2pi) for
 * theta = atan2(X, Y) clockwise from 12 o'clock, by omni_atan2_tab (no libm: single precision in a fixed order, so the
 * host and the device build the same map). Unlike omni_pmap_build, the build does not redo pixels near a cell edge
 * with atan2f and double atan2: that redo is libm per pixel and would let the host and device bytes differ. So a pixel
 * whose exact theta lies within the table's error of a cell edge (1.19e-5 rad, trig.h: 0.004 entries at n 2048) may
 * take the neighbouring entry, at most one entry off the exact atan2 cell.
 * cx and cy lie on the half-pixel grid (a pixel corner or centre: 2 cx and 2 cy integers): every X and Y is then exact,
 * and the pixel count is one of four, so fk_annulus_bytes is exact for any such centre. 0 <= r0 < r1, at most 256
 * radius rows (ceil(r1 - r0)), r1 <= 1024; n_theta a power of two, 4..32768. */
typedef struct fk_annulus fk_annulus_t;
/* The memory fk_annulus_build needs for any centre on the grid, any alignment; 0 for a bad radius or n_theta. */
size_t fk_annulus_bytes(float r0, float r1, uint16_t n_theta);
/* The map, built in mem (opaque and self-referencing: build it where it is drawn from). NULL if bytes is short, or for
 * a centre off the grid, or a bad radius or n_theta. */
fk_annulus_t *fk_annulus_build(void *mem, size_t bytes, float cx, float cy, float r0, float r1, uint16_t n_theta);
/* Every annulus pixel inside clip reads strip entry (row ri, column (th + th_off) mod n_theta), the ring.h kernels'
 * rule, so a larger th_off turns the script counter-clockwise; its colour blended by omni_blend565 at the strip's alpha
 * x opa / 255 (integer division). The strip must have the map's n_theta, stride >= n_theta, at least its radius rows
 * and both planes; any other strip draws nothing. Pixels outside [r0, r1) are never written.
 * A turning ring is exact on one machine at any offset; the map itself is libm-free, so host and device frames agree
 * at every offset too, but only still frames are CRC-compared between them (fk/cols.h's rule). */
void fk_annulus_draw(const fk_annulus_t *an, const omni_strip_t *strip, uint16_t th_off, uint8_t opa,
                     uint16_t *fb, int stride, omni_rect_t clip);      /* blends the strip's colour by its alpha */
/* (Task 15) A one-colour strip (the asset packer's A8 strip): a8 is an A8 image n_theta wide (a8->w == the map's
 * n_theta) and at least the map's radius rows tall; row ri, column th as an ORS1 strip's. Every annulus pixel inside
 * clip is omni_blend565(dst, rgb, a x opa / 255) (integer division) for a = entry (ri, (th + th_off) mod n_theta),
 * fk_annulus_draw's rule; any other image draws nothing. */
void fk_annulus_draw_a8(const fk_annulus_t *an, const fk_img_t *a8, uint16_t th_off, uint16_t rgb, uint8_t opa,
                        uint16_t *fb, int stride, omni_rect_t clip);
/* The map's annulus pixels (for tests and the face's working-set report). */
uint32_t fk_annulus_px(const fk_annulus_t *an);
/* FNV-1a 64 of the map (its geometry, row runs and every theta and radius entry, serialised little-endian): equal on
 * the host and the device for one build's arguments, ring.h's OMNI_RING_SYNTH_DIGEST pattern; 0 for NULL. */
uint64_t fk_annulus_digest(const fk_annulus_t *an);
/* The digest of fk_annulus_build(.., 233, 233, 170, 224, 2048), the script ring's map, pinned from the host
 * (test_annulus_digest); a device's verify command can compare its own build against it. */
#define FK_ANNULUS_DIGEST_233_170_224_2048 0x8662195f2c160a1aull
