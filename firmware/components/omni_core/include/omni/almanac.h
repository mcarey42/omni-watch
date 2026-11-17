#pragma once
/* The almanac face's maths (Stage 5 Task 2): the mockup's sunAltAz / sunTimes / skyColor / buildAlmRing / buildMoon
 * (design/mockups/faces-practical.js 74-120, 985-1053) as pure C, with the moon's phase times from Meeus ch. 49 (D9)
 * instead of the mean synodic month. "Local" is the TZ as applied by omni_tz_apply. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/sun.h"

void omni_alm_altaz(int64_t unix_s, double lat, double lon, double *alt_deg, double *az_deg); /* az from N, clockwise */
void omni_alm_sky_rgb(double alt_deg, uint8_t rgb[3]);   /* the mockup's skyColor stops, linear between */

typedef struct { omni_sun_day_t kind; int64_t rise, set, noon; int32_t len_s, delta_s; } omni_alm_day_t;
/* The local calendar day of now (TZ as applied): rise/set, noon (the midpoint of rise and set; in polar day or night,
 * where rise and set are 0, the computed solar noon), length and the change since yesterday. ALWAYS_UP: len 86400,
 * ALWAYS_DOWN: 0. */
void omni_alm_day(int64_t now, double lat, double lon, omni_alm_day_t *d);

int64_t omni_alm_phase_unix(double k);                   /* Meeus 49: integer k new moon, k + 0.5 full */
typedef struct { double f, illum; uint8_t name; int64_t next_full, next_new; } omni_alm_moon_t;
/* f piecewise (B10): new -> full maps to 0..0.5, full -> next new to 0.5..1, all three events from Meeus 49, so f is
 * exactly 0.5 at full. illum = (1 - cos(2 pi f)) / 2. */
/* Callers gate every almanac call on omni_time_is_set: with an unset clock (unix near 0) this returns 1970's moon, and for
 * absurd times the search is capped, so f is then only meaningful to within a lunation. */
void omni_alm_moon(int64_t unix_s, omni_alm_moon_t *m);
const char *omni_alm_moon_name(uint8_t i);               /* the mockup's 8 names; i = floor(f*8 + 0.5) % 8 */

void omni_alm_daylen_text(const omni_alm_day_t *d, bool set, char out[12]);    /* "9h  02m"; unset "--h  --m" */
void omni_alm_delta_text(const omni_alm_day_t *d, bool set, char out[28]);     /* "+1m 58s vs yesterday"; unset "" */
void omni_alm_moon_text(const omni_alm_moon_t *m, char out[40]);               /* "62% \xc2\xb7 Full Wed 25 Nov" (local) */
void omni_alm_alt_text(double alt, char out[12]);                              /* "34\xc2\xb0", "\xe2\x88\x9212\xc2\xb0" (U+2212) */
void omni_alm_sun_lines(int64_t now, double lat, double lon, bool h24, char az[24], char noon[24], char shadow[32]);
/* "azimuth 212\xc2\xb0 SW" | "solar noon 1:23p" | "1 m stick casts 2.3 m" or "sun below horizon" (alt <= 1) */

#define OMNI_ALM_BOX 438
#define OMNI_ALM_BOX_XY 14
/* The sky ring 190..214, the engraved hour ticks, the hairlines at 189.5 and 214.5, and the rise/set ticks (2 px
 * #ecd29a, 186..218) at local clock hours rise_h/set_h (< 0: none); black elsewhere. 288 samples by local clock time
 * (A4): sample i is the instant whose local time (TZ as applied) is i x 5 min on day0's date, so a 23 h or 25 h DST day
 * keeps every hour under its numeral. Resolved without mktime: a time in a DST gap takes the offset before the jump,
 * one in an overlap the earlier instant. set false: every sample is the night colour and there are no rise/set
 * ticks (D13). hourAngle(h) = pi/2 + h/24 * 2pi, canvas radians (midnight at the bottom, clockwise). box is
 * OMNI_ALM_BOX x OMNI_ALM_BOX RGB565, stride OMNI_ALM_BOX, every pixel written. */
void omni_alm_ring_render(uint16_t *box, int64_t day0_unix, double lat, double lon, float rise_h, float set_h, bool set);
/* The mockup's buildMoon (MARIA, 70 seeded craters, seed 7, Tycho) at phase f: d x d RGB565A8, the colour plane
 * (d*d RGB565) then the alpha plane (d*d bytes). rgb565a8 must be 2-byte aligned (the colour plane is written as uint16_t) and
 * hold d*d*3 bytes. */
void omni_alm_moon_render(uint8_t *rgb565a8, int d, double f);
