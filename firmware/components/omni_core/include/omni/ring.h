#pragma once
/* The Omni date rings (spec §7.3, Stage 3 Task 2): four brass rings that turn independently, drawn by a polar map from
 * screen pixels into one radius-indexed strip of the four engravings. Pure C, no allocation in the draw path.
 *
 * Two map variants, the same pixels (test_kernels_bitexact, `ring selftest`):
 *  A, the plain LUT: per span pixel a uint16 theta index and a uint8 radius row (3 B/px), omni_rings_span_lut (the
 *    reference kernel);
 *  B, the delta-coded stream (the owner's technique): a span's start (th0, ri0), then one 4-bit code per further
 *    pixel, |dtheta| - base in {0..3} x dr in {-1, 0, +1} (code = 3 * (|dtheta| - base) + dr + 1; 12..15 reserved),
 *    in theta's constant direction dir along the span; omni_rings_span_codes (0.5 B/px). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/geom.h"

#define OMNI_RINGS 4
typedef struct { float r0, r1; uint16_t slots; } omni_ring_geom_t;
extern const omni_ring_geom_t OMNI_RING_GEOM[OMNI_RINGS]; /* month {126,143,12} day {143,160,32} year {160,178,100}
                                                           * century {178,197,42} */

/* Polar map: each screen pixel p of rect (inside the rounded-rect clip of radius rr, if rr > 0) maps to the source
 * point q = s + (p + 0.5 - d) / k, and q to (theta clockwise from 12 o'clock, r) about (233, 233). Pixels whose r
 * falls in a ring (and in [r_min, r_min + n_r)) get that ring's strip indices th = floor(theta / 2pi * n_theta) and
 * ri = floor(r) - r_min. The dial map is k 1, d = s = (0, 0), rect (36, 36, 430, 430); the magnifier and loupe maps
 * (Task 6) differ only in cfg. Spans are runs of one row in one ring, so each takes one ring's offset. */
typedef struct { omni_rect_t rect; float dx, dy, sx, sy, k, rr; uint16_t n_theta; uint8_t r_min, n_r; } omni_pmap_cfg_t;
typedef struct { int16_t y, x0, n; uint8_t ring, dir; uint16_t th0; uint8_t ri0, base; uint32_t lut, code; } omni_pspan_t;
/* lut: the span's first entry in th/ri (variant A); code: the nibble index of its second pixel's code (variant B; a
 * span of n pixels has n - 1 codes, low nibble first); base: B's per-span theta step; dir: 0 theta rises along +x,
 * 1 it falls (taken mod n_theta, so a span may cross 12 o'clock). */
typedef struct {
    omni_pmap_cfg_t cfg; uint32_t nspans; omni_pspan_t *span;
    uint16_t *th; uint8_t *ri;   /* plain LUT: one entry per span pixel (variant A) */
    uint8_t *codes;              /* delta stream: 4-bit codes, two per byte, from span.code (variant B) */
    uint32_t npx;                /* span pixels in all */
} omni_pmap_t;
/* The memory omni_pmap_build needs for cfg (exact: it counts the spans), any alignment. */
size_t omni_pmap_bytes(const omni_pmap_cfg_t *cfg, bool lut, bool codes);
/* Builds m in mem (single precision in a fixed order: radii from exact squares, theta from the table atan2 of trig.h,
 * with pixels within 0.03 entries of a cell edge redone by atan2f and within 1e-3 by double atan2, about 0.5 % of
 * them; host and device build the same bytes). lut and codes choose the variants kept (spans always). False if bytes is short, or with codes if a span would need a reserved code: then the smallest
 * per-span base that keeps every |dtheta| - base in 0..3 is taken, and false only if none does. */
bool omni_pmap_build(omni_pmap_t *m, const omni_pmap_cfg_t *cfg, bool lut, bool codes, void *mem, size_t bytes);
uint32_t omni_pmap_base_spans(const omni_pmap_t *m);   /* spans whose base is not 0 (C5) */
/* The dial map's cfg (rect (36, 36, 430, 430), k 1, r_min 126, n_r 71) at n_theta: the device view and tools/ringpack
 * (omni_dialmap.bin) take it from here, so the two cannot drift. */
omni_pmap_cfg_t omni_dial_pmap_cfg(uint16_t n_theta);

/* The prebuilt map file (R80, omni_dialmap.bin, written by tools/ringpack): variant B (spans and codes) only. A 72-byte
 * header, all little-endian: "OPM1", u16 version, u16 n_theta, u32 nspans, u32 npx, u32 code bytes, u32 file size,
 * u64 checksum (omni_pmap_digest of the map it holds), the cfg (i16 rect x0 y0 x1 y1; f32 dx dy sx sy k rr; u8 r_min,
 * n_r), zero to 72. Then nspans records of OMNI_PMAP_SPAN_BYTES (i16 y x0 n, u8 ring dir, u16 th0, u8 ri0 base,
 * u32 lut code), then the code bytes. */
#define OMNI_PMAP_HDR 72
#define OMNI_PMAP_VERSION 1
#define OMNI_PMAP_SPAN_BYTES 20
size_t omni_pmap_file_bytes(const omni_pmap_t *m);                       /* 0 if m has no codes */
size_t omni_pmap_file_write(const omni_pmap_t *m, void *out, size_t cap);  /* bytes written, 0 if cap is short */
/* The memory omni_pmap_load needs for this file (any alignment), from its header; 0 if the header is malformed. */
size_t omni_pmap_file_mem(const void *file, size_t size);
/* Loads the file into mem (spans and codes copied; th and ri NULL). False, with the reason in why (may be NULL), on any
 * mismatch: magic, version, size (counts capped at 2^20 pixels, the sum taken in 64 bits), the cfg against the caller's
 * cfg (bit for bit), the checksum, or a span the kernels could not draw safely: theta, ring, radius, base or extent out
 * of range, lut/code offsets out of order, a reserved code (12..15), or a radius walk leaving the map's n_r rows. */
bool omni_pmap_load(omni_pmap_t *m, const omni_pmap_cfg_t *cfg, const void *file, size_t size, void *mem,
                    size_t bytes, char *why, size_t why_n);
/* FNV-1a 64 over the map's spans (their file records) and its codes: equal for any two builds of one cfg, with or
 * without variant A; the file's checksum. 0 if m has no codes. */
uint64_t omni_pmap_digest(const omni_pmap_t *m);

/* Strip: rows by radius, n_theta (a power of two) entries each, no wrap margin: kernels index (th + off) & (n_theta - 1).
 * Each row is stored `stride` = n_theta + OMNI_STRIP_PAD entries apart (the pad is never read): a power-of-two row
 * stride puts every radius row of a plane in the same one or two sets of the S3's 64 KB 8-way data cache, and a span
 * near 3 or 9 o'clock walks radially, a row a pixel (measured, Task 2: 559 ns/px cold, 496 hot). Read in place from the
 * flash mapping (R70); the header gives n_theta, stride, r_min, n_r, level. A level-L strip has n_theta = N >> L for a
 * map of N: the kernels read entry th >> L. */
#define OMNI_STRIP_PAD 32
typedef struct { uint16_t n_theta, stride; uint8_t r_min, n_r, level; const uint16_t *rgb; const uint8_t *a; } omni_strip_t;
size_t omni_strip_bytes(uint16_t n_theta, uint8_t n_r);   /* the planes: (n_theta + OMNI_STRIP_PAD) * n_r * (2 + 1) */
/* The strip file (omni_strip<L>.bin, written by tools/ringpack): a 16-byte header, then rgb (uint16 LE), then a, each
 * n_r rows of stride entries. "ORS1", u16 n_theta, u8 r_min, u8 n_r, u8 level, u8 0, u16 stride, 4 zero bytes. */
#define OMNI_STRIP_HDR 16
size_t omni_strip_file_header(const omni_strip_t *st, uint8_t hdr[OMNI_STRIP_HDR]);
bool omni_strip_map(omni_strip_t *st, const void *file, size_t size);   /* in place; false if malformed */
/* From the four 2x unpremultiplied RGBA engraving layers (932x932 each, centre 466,466; disjoint annuli, merged into
 * one radius-indexed strip: row ri takes the layer of the ring that radius r_min + ri falls in): area-averaged over
 * each entry's polar cell (4x4 subsamples, bilinear), L1/L2 then box-blurred along theta by OMNI_BLUR_L1/L2 entries at
 * n_theta >> level (host tool). On entry st->n_theta (the level-0 N), r_min and n_r are set; mem holds the planes
 * (omni_strip_bytes at the level's n_theta; the pad entries are zeroed). Allocates a row of scratch (malloc). */
bool omni_strip_from_rgba(omni_strip_t *st, const uint8_t *const rgba2x[OMNI_RINGS], uint8_t level, void *mem,
                          size_t bytes);
#define OMNI_BLUR_L1 4                   /* entries at its level; Task 2 step 6 sets the final values */
#define OMNI_BLUR_L2 8
#define OMNI_STILL 0.01f                 /* slots/frame: below this a ring is still */
#define OMNI_FAST1 0.5f                  /* level 1 from here */
#define OMNI_FAST2 1.5f                  /* level 2 from here */

typedef struct { float val[OMNI_RINGS]; uint8_t level[OMNI_RINGS]; } omni_rings_pose_t;  /* val in slots */
/* Ring i's strip offset in entries of its level (n_theta >> level, n_theta the level-0 N) for val:
 * theta_src = theta + val / slots * 2pi (the label `val` sits at 12). Always in [0, n_theta >> level). */
uint32_t omni_ring_off(uint16_t n_theta, uint8_t level, uint16_t slots, float val);
typedef void (*omni_rings_span_fn)(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp,
                                   uint32_t off, uint16_t *dst);       /* dst = the span's first pixel */
void omni_rings_span_lut(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp, uint32_t off, uint16_t *dst);
void omni_rings_span_codes(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp, uint32_t off, uint16_t *dst);
/* All spans of m inside clip, over fb (stride px); the strip per ring from strips[level]. fb is the pixel at
 * (clip->x0, clip->y0) (clip NULL: the screen's (0, 0), nothing clipped). A span cut by clip's x bounds is drawn
 * whole on a row copy (the codes decode from the span start) and only its part inside is written. */
void omni_rings_draw(const omni_strip_t *const strips[3], const omni_pmap_t *m, const omni_rings_pose_t *p,
                     omni_rings_span_fn fn, uint16_t *fb, int stride, const omni_rect_t *clip);
/* The same for ring `ring` only (a ring that moved alone). */
void omni_rings_draw_ring(const omni_strip_t *const strips[3], const omni_pmap_t *m, const omni_rings_pose_t *p,
                          int ring, omni_rings_span_fn fn, uint16_t *fb, int stride, const omni_rect_t *clip);
/* True if the kernels can draw strips[0..2] through m without reading outside a plane: every strip has planes,
 * strips[l].n_theta == m's n_theta >> l and level l, stride >= n_theta, r_min == m's and n_r >= m's (the map's ri stay
 * below its n_r: omni_pmap_build and omni_pmap_load see to that). Checked once, where a view pairs them. */
bool omni_rings_compatible(const omni_strip_t *const strips[3], const omni_pmap_t *m, char *why, size_t why_n);
/* Fidelity: level per ring from its speed (slots per frame); all 0 when every |speed| < OMNI_STILL. */
void omni_rings_fidelity(omni_rings_pose_t *p, const float speed[OMNI_RINGS]);
/* Bit-exactness, the globe_check.h pattern: every span of m through omni_rings_span_lut and fn, each on its own copy
 * of the row (a fixed pattern) plus OMNI_KCHECK_GUARD guard pixels; and a libm-free synthetic digest (random strip,
 * map and offsets). scratch: 2 * (rect width + OMNI_KCHECK_GUARD) uint16. m needs both variants. */
bool omni_ring_kernel_bitexact(omni_rings_span_fn fn, const omni_strip_t *st, const omni_pmap_t *m, uint32_t off,
                               uint16_t *scratch, char *why, size_t why_n);
size_t omni_ring_synth_bytes(void);
uint64_t omni_ring_synth_digest(omni_rings_span_fn fn, void *mem, size_t bytes);
#define OMNI_RING_SYNTH_DIGEST 0x0cdc88be23b5e5eeull   /* pinned from the host omni_rings_span_lut (Task 6: + short spans) */
