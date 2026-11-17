/* The almanac face's pure helpers (omni/alm_face.h). */
#include "omni/alm_face.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

int32_t omni_alm_date_key(int64_t t)
{
    const time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return 0;
    return (int32_t)tm.tm_year * 400 + tm.tm_yday;
}

void omni_alm_hour_label(int hh, bool h24, char out[4])
{
    hh = ((hh % 24) + 24) % 24;
    if (h24) snprintf(out, 4, "%02d", hh);
    else snprintf(out, 4, "%d%s", (hh + 11) % 12 + 1, hh == 0 ? "a" : hh == 12 ? "p" : "");
}

static int16_t tick_min(float h) { return h >= 0.0f ? (int16_t)lroundf(h * 60.0f) : (int16_t)-60; }

void omni_alm_ring_key(omni_alm_ring_key_t *k, int64_t now, const char *tz, int city, bool set, float rise_h, float set_h)
{
    *k = (omni_alm_ring_key_t){ .date = 0, .tz = 0, .rs = { -60, -60 }, .city = -1, .set = false };
    if (!set) return;
    uint32_t h = 2166136261u;   /* FNV-1a 32 */
    for (const char *p = tz ? tz : ""; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    k->date = omni_alm_date_key(now);
    k->tz = h;
    k->rs[0] = tick_min(rise_h);
    k->rs[1] = tick_min(set_h);
    k->city = (int16_t)city;
    k->set = true;
}

bool omni_alm_ring_key_eq(const omni_alm_ring_key_t *a, const omni_alm_ring_key_t *b)
{
    return a->date == b->date && a->tz == b->tz && a->rs[0] == b->rs[0] && a->rs[1] == b->rs[1] && a->city == b->city &&
           a->set == b->set;
}
