/* The faces' clock from the shell's time (fk/clock.h). */
#include "fk/clock.h"
#include <string.h>
#include <time.h>
#include "omni/timefmt.h"

void fk_clock_from(int64_t unix_s, uint16_t ms, bool stale, fk_clock_t *c)
{
    memset(c, 0, sizeof *c);
    if (!omni_time_is_set(unix_s)) return;
    struct tm tm;
    const time_t t = (time_t)unix_s;
    if (localtime_r(&t, &tm) == NULL) return;
    c->set = true;
    c->stale = stale;
    c->h = (uint8_t)tm.tm_hour, c->m = (uint8_t)tm.tm_min, c->s = (uint8_t)(tm.tm_sec > 59 ? 59 : tm.tm_sec);
    c->mday = (uint8_t)tm.tm_mday, c->mon = (uint8_t)tm.tm_mon, c->wday = (uint8_t)tm.tm_wday;
    c->ms = ms > 999 ? 999 : ms;
    c->yday = (uint16_t)(tm.tm_yday + 1);
}
