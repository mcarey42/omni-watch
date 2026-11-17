#pragma once
/* The host-only oracle of Curry Clock's procedural steam (rd wave 2 Task 12, R165/I5): rd_cu_steam_oracle.c. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"

/* BASE's steam_draw() at clock T (ms of the day), strength amt, cream or red, added into fb (stride, its top-left at
 * clip's), clipped to RD_CU_STEAM; the 32 x 32 A8 plane it samples is the oracle's own (the former asset's bytes) */
void rd_cu_steam_oracle(uint32_t T, float amt, bool red, uint16_t *fb, int stride, omni_rect_t clip);
/* its bilinear samples over the whole of RD_CU_STEAM at (T, amt) */
unsigned long rd_cu_steam_oracle_samples(uint32_t T, float amt);
