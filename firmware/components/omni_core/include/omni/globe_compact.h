#pragma once
/* The compact ortho LUT (Task 11, R58 O1b): the planar LUT's per-pixel data in 64-byte blocks of 8 entries, one
 * cache line each, read as a single stream (8 B a pixel instead of 13 B in 8 streams).
 *
 * The a, b, c planes (6 B: the world-frame normal) are replaced by z (2 B): on an ortho globe the sun term is
 * linear in the view frame, mu = Lx xv + Ly yv + Lz z, and xv, yv are linear in the pixel's screen position. So per
 * frame the sun becomes (Ex, Ey, Lz) and per span the linear part is an exact integer ramp:
 *
 *   B0   = (int32)((((int64)(2 x - D + 1) Ex + (int64)(D - 1 - 2 y) Ey) K) >> 8)  x, y: the span's first pixel,
 *   step = (int32)(((int64)2 Ex K) >> 8)                                         globe coords; K = 2^20 / D, rounded
 *   mu_j = ((B0 + j step) >> 12) + ((z_j Lz) >> 14)
 *
 * and everything after mu (the shade index, texel, limb, rim, blend) is omni_globe_span_ref's arithmetic exactly.
 * This is its own reference (omni_globe_cspan_ref): it differs from the planar one only by the rounding of mu
 * (an occasional shade bin, see test_compact_vs_planar), and is held to the mockup's float oracle like it.
 * Polar globes and textures wider than 512 (a 9-bit row) keep the planar LUT. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/globe.h"

typedef struct {
    int16_t u[8];                  /* Q4 texels, as the planar u */
    int16_t z[8];                  /* view-frame z, Q14 (0 .. 16384), from the clamped r as the planar build */
    uint8_t row[8], limb[8], rim[8], alpha[8];
} omni_globe_blk_t;                /* 64 bytes: one cache line */

/* A disc row: box pixels x0 .. x0+n-1 on row y are entries 8 blk .. 8 blk + n-1; entries [ia, ib) have alpha 255
 * (the interior run: only the pixels outside it read the base under the globe). The packer checks every entry of the
 * run; a span where they are not one run gets none (ia = ib = 0) and is counted in bad_runs. */
typedef struct { int16_t y, x0, n, ia, ib, pad; uint32_t blk; } omni_globe_cspan_t;

typedef struct omni_globe_clut {
    omni_globe_cfg_t cfg;
    uint16_t nspans;
    omni_globe_cspan_t *span;
    uint32_t nblk;
    omni_globe_blk_t *blk;         /* 64-byte aligned */
    int16_t cl, sl, cr, sr;        /* Q14 cos/sin of lat0 and roll */
    int16_t light[3];              /* studio: the light in the rolled frame, Q14 */
    int32_t k;                     /* 2^20 / diam, rounded */
    uint32_t bad_runs;             /* spans whose alpha-255 pixels were not one run (given none; never expected) */
} omni_globe_clut_t;

/* True if cfg can use the compact LUT (ortho, tex_w <= 512). */
bool omni_globe_compact_ok(const omni_globe_cfg_t *cfg);
/* Bytes omni_globe_compact needs for a planar LUT of cfg (any alignment of mem). */
size_t omni_globe_compact_bytes(const omni_globe_lut_t *lut);
/* Packs a built planar LUT (u, row, limb, rim, alpha copied; z computed as the build does) into mem. In steps:
 * begin lays out mem, then spans [s0, s1) packs those spans (disjoint ranges may run on two cores). */
bool omni_globe_compact(omni_globe_clut_t *c, const omni_globe_lut_t *lut, void *mem, size_t bytes);
bool omni_globe_compact_begin(omni_globe_clut_t *c, const omni_globe_lut_t *lut, void *mem, size_t bytes);
void omni_globe_compact_spans(omni_globe_clut_t *c, const omni_globe_lut_t *lut, unsigned s0, unsigned s1);

/* Per-frame terms, from the frame's sun (studio LUTs use their own light) and lon0. */
typedef struct { int32_t uoff_q4; int16_t ex, ey, lz; } omni_globe_cterms_t;
void omni_globe_cterms(omni_globe_cterms_t *t, const omni_globe_clut_t *c, const omni_globe_sun_t *sun, double lon0_deg);

/* Shades one compact span onto dst (its first pixel), which holds the base outside [ia, ib). */
typedef void (*omni_globe_cspan_fn)(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp,
                                    const omni_globe_cterms_t *t, uint16_t *dst);
void omni_globe_cspan_ref(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                          uint16_t *dst);
void omni_globe_cspan_fast(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                           uint16_t *dst);
/* R53 step 6a / R76, for still frames: the same, sampling the texture linearly in u between the
 * centres of texels x0 = (u + uoff - 8) >> 4 and x0 + 1 (wrapping), weight f = (u + uoff - 8) & 15 in sixteenths:
 * each 8-bit channel (day and night) is (c0 (16 - f) + c1 f) >> 4. Rows stay nearest (the block's row has no v
 * fraction). lin_fast is lin_ref bit for bit. Measured on spin (C, no PIE): 59.6 ms a kept frame on both cores
 * against 26.9 ms for the nearest PIE kernel, so R76: frames at rest only (globe_view_frame_t.still); frames in
 * motion keep the nearest kernel. */
void omni_globe_cspan_lin_ref(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                              uint16_t *dst);
void omni_globe_cspan_lin_fast(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                               uint16_t *dst);
/* Building blocks for vector kernels: the span's ramp (B0, step of the header), and cspan_fast over entries
 * [j0, j1) only (dst is still the span's first pixel). */
void omni_globe_cramp(const omni_globe_clut_t *c, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                      int32_t *b0, int32_t *step);
void omni_globe_cspan_range(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                            uint16_t *dst, int j0, int j1);

/* omni_globe_render for f->clut (f->lut unused). keep: out already holds this frame's base and overlay outside
 * the disc spans (a buffer last rendered with the same under, over and fade 255), except inside dirty (inclusive
 * x0 y0 x1 y1, or NULL), where the base changed since: only the disc spans, their edge pixels' base, the dirty
 * rect's base and the overlay over all of them are written. fn NULL = omni_globe_cspan_ref. */
void omni_globe_render_c(const omni_globe_frame_t *f, uint16_t *out, uint16_t y0, uint16_t y1, omni_globe_cspan_fn fn,
                         bool keep, const int dirty[4]);
