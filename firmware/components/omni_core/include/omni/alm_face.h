#pragma once
/* The almanac face's pure helpers (Stage 5 Task 7): its ring cache's date key and its hour numerals. */
#include <stdbool.h>
#include <stdint.h>

/* The local calendar date of t (TZ as applied) as a key: tm_year * 400 + tm_yday. One value for every instant of a
 * local day, however long (23 h and 25 h DST days included), and one more for the next day within a year. */
int32_t omni_alm_date_key(int64_t t);

/* The sky ring's numeral for hour hh (0, 3 .. 21): 12 h "12a" "3" .. "12p" .., 24 h "00" "03" .. "21". */
void omni_alm_hour_label(int hh, bool h24, char out[4]);

/* R140: the sky ring's cache key, what omni_alm_ring_render bakes: the local date (omni_alm_date_key, the TZ applied),
 * the zone (a hash of its TZ string; NULL is ""), the city (its lat/lon), and the rise and set ticks rounded to the
 * minute (-60: none, any h < 0). With the clock unset (set false) every key is the one night-ring key. The face's
 * worker renders a ring only for a key the cache does not hold: once a day, a city or zone change, or a tick that
 * moved a minute; a DST change in the day is the same key (one render). */
typedef struct { int32_t date; uint32_t tz; int16_t rs[2]; int16_t city; bool set; } omni_alm_ring_key_t;
void omni_alm_ring_key(omni_alm_ring_key_t *k, int64_t now, const char *tz, int city, bool set, float rise_h, float set_h);
bool omni_alm_ring_key_eq(const omni_alm_ring_key_t *a, const omni_alm_ring_key_t *b);
