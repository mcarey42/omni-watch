/* The faces' text helpers (fk/text.h). */
#include "fk/text.h"
#include <string.h>

static const char DOW[7][4] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char MON[12][4] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

static char *two(char *p, unsigned v)
{
    *p++ = (char)('0' + v / 10u % 10u);
    *p++ = (char)('0' + v % 10u);
    return p;
}

int fk_time_str(const fk_clock_t *c, bool h24, char out[6])
{
    if (!c->set) {
        memcpy(out, "--:--", 6);
        return 5;
    }
    unsigned h = c->h % 24u;
    char *p = out;
    if (h24) p = two(p, h);
    else {
        h %= 12u;
        if (h == 0) h = 12;
        if (h >= 10) *p++ = '1';
        *p++ = (char)('0' + h % 10u);
    }
    *p++ = ':';
    p = two(p, c->m % 60u);
    *p = '\0';
    return (int)(p - out);
}

void fk_date_str(const fk_clock_t *c, char out[16])
{
    if (!c->set) {
        memcpy(out, "--- -- ---", 11);
        return;
    }
    char *p = out;
    memcpy(p, DOW[c->wday % 7u], 3);
    p += 3;
    *p++ = ' ';
    p = two(p, c->mday);
    *p++ = ' ';
    memcpy(p, MON[c->mon % 12u], 3);
    p[3] = '\0';
}

const char *fk_ampm(const fk_clock_t *c, bool h24)
{
    if (!c->set || h24) return "";
    return c->h % 24u < 12 ? " \267 AM" : " \267 PM";
}
