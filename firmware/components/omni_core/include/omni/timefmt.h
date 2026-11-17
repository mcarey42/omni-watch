#pragma once
#include <stdbool.h>
#include <stdint.h>

/* False if the clock is unset (year < 2025). */
bool omni_time_is_set(int64_t unix_s);

/* Apply a POSIX TZ string (setenv("TZ") + tzset()). */
void omni_tz_apply(const char *posix_tz);

/* Local time. 12 h: "2:58","PM"; 24 h: "14:58","" (zero-padded HH:MM). */
void omni_fmt_clock(int64_t unix_s, bool h24, char hhmm[6], char ampm[3]);

/* Local time, compact. 12 h: "6:42p"; 24 h: "18:42". */
void omni_fmt_short_time(int64_t unix_s, bool h24, char out[8]);

/* Age: "now" (<60 s or negative), "14m", "3h", "2d". */
void omni_fmt_age(int64_t seconds, char out[8]);

/* Duration, minutes rounded: "9h 02m", "0h 01m". */
void omni_fmt_dur(int64_t seconds, char out[12]);

/* "Sunday" .. "Saturday" (tm_wday, any int: taken mod 7) and "January" .. "December" (tm_mon, mod 12). The short
 * forms are the first three letters ("%.3s"). */
const char *omni_day_name(int wday);
const char *omni_month_name(int mon);
/* a-z to A-Z in place (ASCII only: the names above and the city table). */
void omni_ascii_upper(char *s);
