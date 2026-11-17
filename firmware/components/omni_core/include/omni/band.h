#pragma once
/* Lever B's band core (Stage 4a Task 3): pure integer helpers for the direct path, which renders a dirty chunk into an
 * internal-RAM band and sends it straight to the panel. Chunking, in-place packing, the sprite overlay blend and the
 * admission rule. No esp_*, no LVGL. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"
#include "omni/rects.h"

/* The panel takes even origins and even sizes (the BSP's rounder: x1, y1 down to even, x2, y2 up to odd). */
omni_rect_t omni_band_even(omni_rect_t r, int w, int h);          /* grown outward to even edges, clipped to w x h */
/* L's rects made even into a list built by assignment (not omni_rects_add: no merge may add pixels, B10), then
 * omni_rects_disjoint, then cut into chunks of at most `rows` rows (rows even); every chunk even. Returns the count,
 * or -1 if rows is not positive and even, or if there are more than max (the caller then chunks the bounding rect of
 * L). *fallback (may be NULL) is set when omni_rects_disjoint fell back to the bounding rect (conservative: it is
 * also set when a single exact piece happens to equal the bounding rect of a multi-rect list). */
int omni_band_chunks(const omni_rects_t *L, int rows, int w, int h, omni_rect_t *out, int max, bool *fallback);
/* In place: the chunk c's rows, rendered at stride `stride` px (row 0 = c.y0, column x at offset x), packed to
 * (c.x1 - c.x0) px a row from the band's start and byte-swapped to the panel's order. */
void omni_band_pack(uint16_t *band, int stride, omni_rect_t c);
/* Straight-alpha ARGB8888 sprite (words 0xAARRGGBB, LVGL's byte order B,G,R,A) at screen rect r; stride in px
 * (an lv_draw_buf's header.stride / 4, A9). */
typedef struct { omni_rect_t r; const uint32_t *argb; int stride; } omni_band_overlay_t;
/* Blends each overlay's part inside c over the band (stride px, row 0 = c.y0) by omni_blend565(dst, rgb565, a8)
 * with rgb565 = omni_pack565(R, G, B), a8 = A. */
void omni_band_overlay(uint16_t *band, int stride, omni_rect_t c, const omni_band_overlay_t *o, int n);
/* D14: keeps a dropped LVGL flush's area a in up to max slots (*n used): merged into the first slot whose union costs
 * no more than the two areas, else a free slot, else into the slot whose union grows least. Empty a: nothing. */
void omni_band_drop_keep(omni_rect_t *slots, int *n, int max, omni_rect_t a);
typedef enum { OMNI_DD_OK, OMNI_DD_OFF, OMNI_DD_PROV, OMNI_DD_DISPLAY, OMNI_DD_HIDDEN, OMNI_DD_OVERLAY,
               OMNI_DD_RAM, OMNI_DD_BACKOFF, OMNI_DD__COUNT } omni_dd_why_t;
/* OMNI_DD_BACKOFF is a claim's result only (disp_direct_claim, D7): within a back-off after a refusal or an exit, or
 * while the last claim's bands are still draining back. omni_direct_admit never returns it. */
typedef struct { bool enabled, provisioning, display_on, view_shown, unknown_overlay;
                 uint32_t int_free, dma_largest; } omni_direct_env_t;
/* The first failing condition in the enum's order. RAM: dma_largest < band_bytes (when need > 0), or
 * (uint64_t)int_free < (uint64_t)need + reserve (A7: no uint32 wrap). need: bytes the path would allocate now (the
 * claim's; 0 at a check, the held bands being already out of int_free). */
omni_dd_why_t omni_direct_admit(const omni_direct_env_t *e, uint32_t need, uint32_t band_bytes, uint32_t reserve);
