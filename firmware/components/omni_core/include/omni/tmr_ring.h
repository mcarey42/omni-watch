#pragma once
/* The timer's 60-minute scale ring (Omni Stage 5 Task 1), the mockup's faces-practical.js 1211-1246 in pure C: the scale
 * ring R0 176 .. R1 214, the sector, its leading edge, 60 ticks, two hairlines and the stopwatch track and bead, into
 * an opaque RGB565 box of OMNI_TMR_BOX px square whose centre is the ring's (the screen's 233, 233 less OMNI_TMR_BOX_XY).
 *
 * Every pixel of the ring zone (160 <= r <= 220 from the centre) is recomputed from black on each draw, so a pixel
 * depends only on the state and its place, never on the box's old content or on the band it was drawn in. The pixels
 * outside the zone are never written: the owner of the box clears it once (0 is black). The numerals are LVGL labels. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OMNI_TMR_BOX 438          /* r <= 219 (the leading edge runs to R1 + 4) */
#define OMNI_TMR_BOX_XY 14        /* even: 233 - 219 */

/* The ring zone's pixels in box order: angle (0..65535 over 2pi from 12 o'clock clockwise), radius x4 from 160 (at most
 * 240) and box index. Built into one caller-owned block (4-aligned): idx, then ang, then rq. */
typedef struct { uint32_t n; const uint16_t *ang; const uint8_t *rq; const uint32_t *idx; } omni_tmr_lut_t;
/* about 0.5 MB: pi (220^2 - 160^2) = 71 628 px x 7 B (B5); built on the host only (R139: the face reads tmr_lut.bin) */
size_t omni_tmr_lut_bytes(void);
bool omni_tmr_lut_build(omni_tmr_lut_t *l, void *mem, size_t n);   /* pixels with 160 <= r <= 220 (the bead reaches 161) */
/* R139: the LUT as a flash asset (tmr_lut.bin, written on the host by tools/ringpack), so the face never builds it (the
 * build's double sqrt/atan2 over the box took 2.97-4.09 s on the device). The file: a 16-byte header ("OTL1", u16 box,
 * u8 zone r0, u8 zone r1, u32 n, u32 0), then idx[n] (u32), ang[n] (u16) and rq[n] (u8), little-endian: the layout
 * omni_tmr_lut_build makes, so the loader points into the file. */
#define OMNI_TMR_LUT_HDR 16
size_t omni_tmr_lut_file_bytes(const omni_tmr_lut_t *l);
size_t omni_tmr_lut_file_write(const omni_tmr_lut_t *l, uint8_t *out, size_t cap);   /* bytes written; 0: too small */
/* l points into file (no copy; file must outlive l and be 4-aligned). False unless file is exactly an OTL1 LUT for
 * this box and zone with idx strictly ascending inside the box and every rq in the zone: the draw writes box[idx]. */
bool omni_tmr_lut_load(omni_tmr_lut_t *l, const void *file, size_t size);

/* min: the sector's end, 0..60 min (0: none); red: the red gradient and edge; alpha: the sector's alpha x255;
 * bead: the stopwatch's place 0..1 of a minute, or < 0 for none (no track either). */
typedef struct { float min; bool red; uint8_t alpha; float bead; } omni_tmr_ring_t;
/* Draw box rows [y0, y1) (clamped to the box). Any split into bands gives the same bytes as one draw. */
void omni_tmr_ring_draw(const omni_tmr_lut_t *l, const omni_tmr_ring_t *s, uint16_t *box, int y0, int y1);
/* The box rect (x0, y0, x1, y1 inclusive) holding every pixel that can differ between drawing a and drawing b.
 * Empty (x1 < x0) when they cannot differ. */
void omni_tmr_ring_dirty(const omni_tmr_ring_t *a, const omni_tmr_ring_t *b, int rect[4]);

/* The DONE pulse (final fix, review finding 1). In DONE only the sector's alpha changes (min 60, red, no bead), and a
 * draw per frame took ~120 ms on the device (4.3 fps against the 15 fps cadence). So the face draws DONE's ring twice
 * at the start, at alpha 0 and at alpha 255, keeps the two layers of ring-zone pixels, and each frame blends them at
 * the frame's alpha (omni_blend565) into the box. Alpha 0 and 255 give the draw's bytes exactly; between them a pixel
 * is within 9 levels per 8-bit channel (one red or blue unit, two green) of a draw at that alpha, about 1.5 % of them
 * differ at all: a pixel is affine in the sector's alpha up to the blends' truncation. The block (caller-owned, 4-aligned): the zone's runs of consecutive box pixels (start, length),
 * then the two layers in LUT order. */
typedef struct { uint32_t n, runs; const uint32_t *run; const uint16_t *lo, *hi; } omni_tmr_pulse_t;
size_t omni_tmr_pulse_bytes(const omni_tmr_lut_t *l);   /* 4 n + 8 runs: about 0.29 MB */
/* Draws s at alpha 0 and 255 into box (its ring zone is left holding the 255 draw) and keeps both layers in mem. */
bool omni_tmr_pulse_build(omni_tmr_pulse_t *p, const omni_tmr_lut_t *l, const omni_tmr_ring_t *s, uint16_t *box,
                          void *mem, size_t n);
void omni_tmr_pulse_draw(const omni_tmr_pulse_t *p, uint8_t alpha, uint16_t *box);   /* the ring zone only */
