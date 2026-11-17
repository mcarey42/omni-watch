#pragma once
/* Five one-line math helpers the reveal code shares (PR1c): one definition, static inline, so every caller inlines the
 * same single-precision steps (-ffp-contract=off) and its frames keep their bytes. No libm. */
#include <stdint.h>

static inline float fk_ease_clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float fk_ease_out_cubic(float s) { const float u = 1 - s; return 1 - u * u * u; }
static inline float fk_ease_in_out_cubic(float s)   /* omni_flip_theta's */
{
    if (s < 0.5f) return 4 * s * s * s;
    const float u = -2 * s + 2;
    return 1 - u * u * u / 2;
}
/* the mockup's ph: t's place in [a, b], clamped to [0, 1] */
static inline float fk_ease_ph(float t, float a, float b) { return fk_ease_clampf((t - a) / (b - a), 0, 1); }
/* x in [0, 1] to 0..255, rounded. floorf of a value >= 0.5 is its truncation: no libm call (floorf is a software
 * routine on the device) */
static inline uint8_t fk_ease_to8(float x) { return (uint8_t)(int)(fk_ease_clampf(x, 0, 1) * 255.0f + 0.5f); }
