#pragma once
/* Bit-exactness support for globe span kernels, shared by the host tests (test_globe_render.c) and the device's
 * `globe selftest` (Task 7), and used by Task 11 for any optimised kernel. Pure C: no allocation, no libm. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/globe.h"
#include "omni/globe_compact.h"

/* FNV-1a 64 over n bytes, continuing from h (start with OMNI_FNV1A_INIT). */
#define OMNI_FNV1A_INIT 1469598103934665603ull
uint64_t omni_fnv1a64(uint64_t h, const void *p, size_t n);

/* Every span of f's LUT through omni_globe_span_ref and through fn, each call on its own copy of the box row
 * (f->under's row, or black) followed by OMNI_KCHECK_GUARD guard pixels of 0xa5a5: the row and the guard must come
 * back identical, which also catches a vector kernel's unmasked tail store. uoff_q4 is the column offset both get.
 * scratch holds 2 * (S + OMNI_KCHECK_GUARD) uint16 (S = f->lut->cfg.size). False with "<tag>: row y x x: ref ...,
 * kernel ..." in why. */
#define OMNI_KCHECK_GUARD 16
bool omni_globe_kernel_bitexact(omni_globe_span_fn fn, const omni_globe_frame_t *f, int32_t uoff_q4, uint16_t *scratch,
                                const char *tag, char *why, size_t why_n);

/* A digest that does not depend on libm: a synthetic all-integer LUT, shade table, texture and sun (xorshift32),
 * shaded by fn over every span at a few column offsets onto a patterned base, FNV-1a over the rows. Host and device
 * builds of omni_globe_span_ref give OMNI_GLOBE_SYNTH_DIGEST, unlike digests of real frames, whose LUT and shade
 * table come from libm (glibc on the host, newlib soft-float double on the device). mem: omni_globe_synth_bytes(),
 * any alignment. Returns 0 if mem is short. */
size_t omni_globe_synth_bytes(void);
uint64_t omni_globe_synth_digest(omni_globe_span_fn fn, void *mem, size_t bytes);
#define OMNI_GLOBE_SYNTH_DIGEST 0x24f8cf97deab3703ull   /* pinned from the host span_ref; re-pin only with GOLDEN[] */

/* FNV-1a of a built LUT's spans and planes, and of a shade table: for comparing a device-built LUT with the host's
 * (informational: they agree only where the two libms round alike). */
uint64_t omni_globe_lut_digest(const omni_globe_lut_t *lut);
uint64_t omni_globe_shade_digest(const omni_globe_shade_t *sh);
uint64_t omni_globe_clut_digest(const omni_globe_clut_t *c);   /* the compact LUT's spans and blocks */

/* The same checks for compact kernels (omni/globe_compact.h): every span of f->clut through omni_globe_cspan_ref and
 * fn with the frame's terms, guards included; and a libm-free synthetic digest (random blocks, terms and shade). */
bool omni_globe_ckernel_bitexact(omni_globe_cspan_fn fn, const omni_globe_frame_t *f, uint16_t *scratch, const char *tag,
                                 char *why, size_t why_n);
size_t omni_globe_csynth_bytes(void);
uint64_t omni_globe_csynth_digest(omni_globe_cspan_fn fn, void *mem, size_t bytes);
#define OMNI_GLOBE_CSYNTH_DIGEST 0xb1d3fb4a5b4875a6ull   /* pinned from the host cspan_ref */
