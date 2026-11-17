#include "omni/sun.h"
#include "omni/timefmt.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DAY_S 86400

/* Days since 1970-01-01 of a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* 12:00 UTC of the local calendar date of `t`, the input omni_sun_rise_set wants (the mockup's
 * sunTimes does the same with the date's UTC midnight). */
static int64_t local_date_noon(int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return (t / DAY_S) * DAY_S + 12 * 3600;
    return days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * DAY_S + 12 * 3600;
}

void omni_sun_glance_text(int64_t now, double lat, double lon, bool h24, char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    now -= (now % 60 + 60) % 60;   /* the minute's start: the text moves with the clock's digits (review finding 2) */
    int64_t noon = local_date_noon(now), rise = 0, set = 0;
    if (omni_sun_rise_set(noon, lat, lon, &rise, &set) != SUN_NORMAL) return;
    char b[12];
    if (now < rise) {
        omni_fmt_dur(rise - now, b);
        snprintf(out, n, "sunrise in %s", b);
    } else if (now < set) {
        omni_fmt_dur(set - now, b);
        snprintf(out, n, "sunset in %s", b);
    } else if (omni_sun_rise_set(noon + DAY_S, lat, lon, &rise, NULL) == SUN_NORMAL) {
        char t[8];
        omni_fmt_short_time(rise, h24, t);
        snprintf(out, n, "sunrise %s", t);
    }
}

void omni_sun_margins(int64_t now, double lat, double lon, bool h24, char rise[8], char set[8])
{
    int64_t r = 0, s = 0;
    if (omni_sun_rise_set(local_date_noon(now), lat, lon, &r, &s) != SUN_NORMAL) {
        snprintf(rise, 8, "\xe2\x80\x94");
        snprintf(set, 8, "\xe2\x80\x94");
        return;
    }
    omni_fmt_short_time(r, h24, rise);
    omni_fmt_short_time(s, h24, set);
}
