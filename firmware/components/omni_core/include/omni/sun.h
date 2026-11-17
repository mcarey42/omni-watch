#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    double decl_deg;
    double eot_min;
    double subsolar_lat, subsolar_lon;
} omni_sun_t;

/* Solar declination, equation of time and subsolar point at a UTC instant (Meeus ch. 25 series, validated in R47). */
void omni_sun_at(int64_t unix_s, omni_sun_t *out);

/* omni_sun_at, but false (out untouched) when !omni_time_is_set: the caller then lights the globe
 * with a sun at the view centre instead (omni_globe_sun_now with NULL, R48/C3): mu >= 0 on the whole disc,
 * so no terminator to speak of, and no halo lobe. */
bool omni_sun_light(int64_t unix_s, omni_sun_t *out);

typedef enum { SUN_NORMAL, SUN_ALWAYS_UP, SUN_ALWAYS_DOWN } omni_sun_day_t;

/* Sunrise/sunset (zenith 90.833 deg) of the solar day whose local solar noon falls on the UTC date of
 * unix_day_noon_utc (12:00 UTC of a calendar date). rise/set are only written for SUN_NORMAL; the set
 * may fall on the following UTC date. */
omni_sun_day_t omni_sun_rise_set(int64_t unix_day_noon_utc, double lat, double lon,
                                 int64_t *rise_unix, int64_t *set_unix);

/* Next sunrise or sunset after now_unix; returns false in polar day/night. */
bool omni_sun_next_event(int64_t now_unix, double lat, double lon, int64_t *when_unix, bool *is_rise);

/* Glance sub-line: "sunrise in 1h 50m" | "sunset in 7h 43m" | "sunrise 7:51a" (tomorrow's) | "" (polar
 * day/night). Uses the local calendar day of `now` (TZ as applied by omni_tz_apply). Computed at the start of now's
 * minute, so it changes only when the clock's minute does (a glance redraws on a change: N6 0 frames). */
void omni_sun_glance_text(int64_t now, double lat, double lon, bool h24, char *out, size_t n);

/* Today's local rise and set as omni_fmt_short_time ("7:50a", "18:42"), or "\xe2\x80\x94" (em dash) in polar day/night. */
void omni_sun_margins(int64_t now, double lat, double lon, bool h24, char rise[8], char set[8]);
