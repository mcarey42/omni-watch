/* Today's sunrise and sunset in local minutes (dw/clock.h, wave 2 Task 9), beside face_kit's clock (fk/clock.h). */
#include "dw/clock.h"
#include <time.h>
#include "omni/sun.h"

/* Days since 1970-01-01 of a civil date (proleptic Gregorian; omni_core's suntext.c keeps its own copy static) */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool dw_sun_today(int64_t now, double lat, double lon, int16_t *rise_m, int16_t *set_m)
{
    *rise_m = *set_m = -1;
    struct tm tm;
    const time_t t = (time_t)now;
    if (localtime_r(&t, &tm) == NULL) return false;
    const int64_t noon = days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400 + 12 * 3600;
    for (int d = -1; d <= 1; d++) {
        int64_t rise, set;
        if (omni_sun_rise_set(noon + d * (int64_t)86400, lat, lon, &rise, &set) != SUN_NORMAL) continue;
        struct tm tr, ts;
        const time_t trt = (time_t)rise, tst = (time_t)set;
        if (localtime_r(&trt, &tr) == NULL || localtime_r(&tst, &ts) == NULL) continue;
        if (tr.tm_year != tm.tm_year || tr.tm_yday != tm.tm_yday) continue;   /* this solar day rises on another date */
        const int r = tr.tm_hour * 60 + tr.tm_min;
        const bool same = ts.tm_year == tm.tm_year && ts.tm_yday == tm.tm_yday;
        const int s = same ? ts.tm_hour * 60 + ts.tm_min : 1439;   /* the sunset on the next local date */
        if (s <= r) continue;
        *rise_m = (int16_t)r, *set_m = (int16_t)s;
        return true;
    }
    return false;
}
