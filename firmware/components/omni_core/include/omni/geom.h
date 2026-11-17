#pragma once
/* Shared screen geometry (R73): the rect and point types every raster, map and compositor uses. Nothing else. */
#include <stdint.h>

typedef struct { int16_t x0, y0, x1, y1; } omni_rect_t;   /* half-open: x0 <= x < x1, y0 <= y < y1 */
typedef struct { float x, y; } omni_pt_t;
