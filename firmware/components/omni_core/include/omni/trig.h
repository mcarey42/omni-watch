#pragma once
/* Table trigonometry for the map builds (Stage 3 Task 2 step 5): libm's atan2f costs 371 cycles a call on the S3
 * (measured), so a 72 000-pixel dial map spent about 110 ms in it alone. Tables are const (flash rodata, no RAM).
 *  - a 1024-entry sine table (plus a quarter turn, so cosine reads the same table);
 *  - omni_recip_q16: 1/x in Q16 by a 256-entry table and one Newton step;
 *  - omni_atan2_tab: atan2 by octant reduction to atan(min * recip(max)) on [0, 1], a 1025-entry atan table with
 *    linear interpolation. test_trig holds it within 1 strip entry of atan2f at N 4096 (measured: 0.0078 entries,
 *    1.19e-5 rad; the map build re-does pixels within 0.03 entries of a cell edge with atan2f). */
#include <stdint.h>

#define OMNI_TRIG_N 1024
extern const float OMNI_SIN_TAB[OMNI_TRIG_N + OMNI_TRIG_N / 4];   /* sin(2 pi i / 1024), i < 1280 */
static inline float omni_sin_tab(unsigned i) { return OMNI_SIN_TAB[i & (OMNI_TRIG_N - 1)]; }
static inline float omni_cos_tab(unsigned i) { return OMNI_SIN_TAB[(i & (OMNI_TRIG_N - 1)) + OMNI_TRIG_N / 4]; }

/* 1/x in Q16 for x in Q16, x >= 32768 (0.5): floor(2^32 / x) within 1. */
uint32_t omni_recip_q16(uint32_t x);
/* atan2(y, x) in radians, (-pi, pi], by the table for |x|, |y| < 256 (not both below 2^-16); outside that domain (or
 * NaN) it is libm's atan2f. (0, 0) gives 0. */
float omni_atan2_tab(float y, float x);
