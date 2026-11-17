#pragma once
/* The compact globe span kernel on the ESP32-S3's PIE SIMD unit (Task 11, O2): omni_globe_cspan_ref bit for bit
 * (`globe selftest` checks it). Device only: globe_pie.S. */
#include "omni/globe_compact.h"

void globe_cspan_pie(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                     uint16_t *dst);
/* globe_cspan_pie is exact while every shade factor (dr dg db nr ng nb) is below this: (d kd + n kn) >> 8 then fits
 * int16 unsaturated (255 * 16447 * 2 >> 8 = 32767). True if sh qualifies (the view falls back to cspan_fast if not). */
#define GLOBE_PIE_SHADE_MAX 16448
bool globe_pie_shade_ok(const omni_globe_shade_t *sh);
/* omni_globe_frame_t.over_run on the PIE unit: the overlay rule over n pixels, bit-exact with the C one. */
void globe_over_pie(const uint32_t *argb, uint16_t *dst, int n);
