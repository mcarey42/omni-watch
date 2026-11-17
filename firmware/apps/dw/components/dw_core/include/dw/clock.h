#pragma once
/* The dw theme's view of the clock: local broken-down time, computed by the caller (the app owns the time zone
 * and the 12/24 h setting), so every face stays TZ-free and a face's pure code takes this struct and nothing else.
 * There is deliberately no 12 h flag: the numeral faces (14, 15, 16) are always 24 h (spec §12). Pure: no esp_*. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/clock.h"   /* fk_clock_t, fk_clock_from (rd wave 1 Task 1: face_kit's) */

#define DW_G_EMPTY 10   /* "time not known": the ring alone (also dw/circles.h) */

/* HH MM as four digits [h tens, h units, m tens, m units], always 24 h. All four DW_G_EMPTY when the clock is unset
 * (or h / m are out of range). */
void dw_digits24(const fk_clock_t *c, int out[4]);

/* (wave 2 Task 9) Today's local sunrise and sunset at (lat, lon) as wall-clock minutes of the local date of now (the
 * caller's zone, localtime_r): of the solar days omni_sun_rise_set gives for the local date's 12:00 UTC and the UTC days
 * either side, the one whose sunrise falls on today's local date (a zone more than 12 h ahead of its solar time, such as
 * Apia's or Tongatapu's +13, finds it on the next UTC date). *rise_m is that sunrise's tm_hour x 60 + tm_min; *set_m the
 * sunset's, 1439 if it falls on the next local date. False (both -1) in polar day or night, or when no solar day's
 * sunrise falls on today. */
bool dw_sun_today(int64_t now, double lat, double lon, int16_t *rise_m, int16_t *set_m);
