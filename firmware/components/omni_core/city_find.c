#include "omni/city_find.h"
#include "omni/cities.h"
#include <stddef.h>

static char fold(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int starts(const char *s, const char *q)
{
    for (; *q; s++, q++) if (fold(*s) != fold(*q)) return 0;   /* *s == 0 differs from any nonzero fold(*q) */
    return 1;
}

static int has(const char *s, const char *q)
{
    for (; *s; s++) if (starts(s, q)) return 1;
    return 0;
}

int omni_city_find_all(const char *q, uint16_t out[OMNI_CITY_FIND_MAX])
{
    if (!q || !*q) return 0;
    int k = 0;
    size_t n = omni_city_count();
    for (int pass = 0; pass < 3; pass++)
        for (size_t i = 0; i < n && k < OMNI_CITY_FIND_MAX; i++) {
            int dup = 0;
            for (int j = 0; j < k; j++) if (out[j] == i) dup = 1;
            if (dup) continue;
            const omni_city_t *c = omni_city_at(i);
            int hit = pass == 0 ? starts(c->name, q) : pass == 1 ? has(c->name, q)
                                                                 : starts(omni_country_name(c->country), q);
            if (hit) out[k++] = (uint16_t)i;
        }
    return k;
}
