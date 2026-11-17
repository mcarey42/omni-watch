#include "omni/timefmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 2025-01-01T00:00:00Z */
#define OMNI_MIN_VALID_UNIX 1735689600LL

bool omni_time_is_set(int64_t unix_s)
{
    return unix_s >= OMNI_MIN_VALID_UNIX;
}

void omni_tz_apply(const char *posix_tz)
{
    setenv("TZ", posix_tz, 1);
    tzset();
}

static void local_tm(int64_t unix_s, struct tm *tm)
{
    time_t t = (time_t)unix_s;
    if (!localtime_r(&t, tm)) {
        *tm = (struct tm){0};
    }
}

static unsigned hour12(int h)
{
    unsigned h12 = (unsigned)h % 12u;
    return h12 ? h12 : 12u;
}

void omni_fmt_clock(int64_t unix_s, bool h24, char hhmm[6], char ampm[3])
{
    struct tm tm;
    local_tm(unix_s, &tm);
    if (h24) {
        snprintf(hhmm, 6, "%02u:%02u", (unsigned)tm.tm_hour % 24u, (unsigned)tm.tm_min % 60u);
        ampm[0] = '\0';
    } else {
        snprintf(hhmm, 6, "%u:%02u", hour12(tm.tm_hour), (unsigned)tm.tm_min % 60u);
        ampm[0] = tm.tm_hour >= 12 ? 'P' : 'A';
        ampm[1] = 'M';
        ampm[2] = '\0';
    }
}

void omni_fmt_short_time(int64_t unix_s, bool h24, char out[8])
{
    struct tm tm;
    local_tm(unix_s, &tm);
    if (h24) {
        snprintf(out, 8, "%02u:%02u", (unsigned)tm.tm_hour % 24u, (unsigned)tm.tm_min % 60u);
    } else {
        snprintf(out, 8, "%u:%02u%c", hour12(tm.tm_hour), (unsigned)tm.tm_min % 60u,
                 tm.tm_hour >= 12 ? 'p' : 'a');
    }
}

void omni_fmt_age(int64_t seconds, char out[8])
{
    if (seconds < 60) {
        snprintf(out, 8, "now");
    } else if (seconds < 3600) {
        snprintf(out, 8, "%dm", (int)(seconds / 60));
    } else if (seconds < 86400) {
        snprintf(out, 8, "%dh", (int)(seconds / 3600));
    } else {
        int64_t d = seconds / 86400;
        snprintf(out, 8, "%dd", (int)(d > 99999 ? 99999 : d));
    }
}

void omni_fmt_dur(int64_t seconds, char out[12])
{
    int64_t m = ((seconds < 0 ? -seconds : seconds) + 30) / 60;
    if (m > 99999) m = 99999;
    snprintf(out, 12, "%dh %02dm", (int)(m / 60), (int)(m % 60));
}

static const char *const DAYS[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const MONTHS[12] = {"January", "February", "March",     "April",   "May",      "June",
                                       "July",    "August",   "September", "October", "November", "December"};

const char *omni_day_name(int wday) { return DAYS[((wday % 7) + 7) % 7]; }
const char *omni_month_name(int mon) { return MONTHS[((mon % 12) + 12) % 12]; }

void omni_ascii_upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
}
