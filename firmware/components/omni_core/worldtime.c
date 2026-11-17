#include "omni/worldtime.h"
#include "omni/cities.h"
#include "omni/timefmt.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define CENTRE 233.0f

/* ---- civil dates (Hinnant) ---- */

static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static int64_t year_of_days(int64_t z)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    return yoe + era * 400 + (mp >= 10 ? 1 : 0);
}

static int days_in_month(int64_t y, int m)
{
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return dim[m - 1] + (m == 2 && leap ? 1 : 0);
}

/* ---- POSIX TZ ---- */

typedef struct { int m, w, d; int32_t time_s; } rule_t;

static bool parse_name(const char **p)
{
    const char *s = *p;
    if (*s == '<') {
        const char *e = strchr(s, '>');
        if (!e || e == s + 1) return false;
        *p = e + 1;
        return true;
    }
    int n = 0;
    while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) { s++; n++; }
    if (n < 3) return false;
    *p = s;
    return true;
}

static bool parse_num(const char **p, int max_digits, int *out)
{
    const char *s = *p;
    int v = 0, n = 0;
    while (*s >= '0' && *s <= '9' && n < max_digits) { v = v * 10 + (*s - '0'); s++; n++; }
    if (!n) return false;
    *out = v;
    *p = s;
    return true;
}

/* [+-]hh[:mm[:ss]] in seconds, hours up to max_h. */
static bool parse_hms(const char **p, int max_h, int32_t *out)
{
    const char *s = *p;
    int sign = 1;
    if (*s == '+') s++;
    else if (*s == '-') { sign = -1; s++; }
    int h, m = 0, sec = 0;
    if (!parse_num(&s, 3, &h) || h > max_h) return false;
    if (*s == ':') {
        s++;
        if (!parse_num(&s, 2, &m) || m > 59) return false;
        if (*s == ':') {
            s++;
            if (!parse_num(&s, 2, &sec) || sec > 59) return false;
        }
    }
    *out = sign * (h * 3600 + m * 60 + sec);
    *p = s;
    return true;
}

static bool parse_rule(const char **p, rule_t *r)
{
    const char *s = *p;
    if (*s++ != 'M') return false;
    if (!parse_num(&s, 2, &r->m) || r->m < 1 || r->m > 12 || *s++ != '.') return false;
    if (!parse_num(&s, 1, &r->w) || r->w < 1 || r->w > 5 || *s++ != '.') return false;
    if (!parse_num(&s, 1, &r->d) || r->d > 6) return false;
    r->time_s = 7200;
    if (*s == '/') {
        s++;
        if (!parse_hms(&s, 167, &r->time_s)) return false;
    }
    *p = s;
    return true;
}

/* The rule's local clock reading in year y, as seconds since the epoch if that reading were UTC. */
static int64_t rule_local(const rule_t *r, int64_t y)
{
    const int64_t first = days_from_civil(y, r->m, 1);
    const int wd1 = (int)(((first + 4) % 7 + 7) % 7);   /* 1970-01-01 was a Thursday */
    int day = 1 + (r->d - wd1 + 7) % 7 + 7 * (r->w - 1);
    while (day > days_in_month(y, r->m)) day -= 7;
    return (first + day - 1) * 86400 + r->time_s;
}

bool omni_tz_offset(const char *posix, int64_t utc, int32_t *off_s, bool *dst)
{
    *off_s = 0;
    *dst = false;
    /* beyond this, utc + an offset, a year's rule instants and the cartouche's local time could overflow (Task 3 m1) */
    if (!posix || utc > OMNI_TZ_UTC_LIMIT || utc < -OMNI_TZ_UTC_LIMIT) return false;
    const char *p = posix;
    int32_t std_w, dst_w;
    if (!parse_name(&p) || !parse_hms(&p, 24, &std_w)) return false;
    const int32_t std_e = -std_w;
    if (!*p) {
        *off_s = std_e;
        return true;
    }
    if (!parse_name(&p)) return false;
    dst_w = std_w - 3600;
    if (*p && *p != ',' && !parse_hms(&p, 24, &dst_w)) return false;
    const int32_t dst_e = -dst_w;
    rule_t on = {3, 2, 0, 7200}, off = {11, 1, 0, 7200};
    if (*p == ',') {
        p++;
        if (!parse_rule(&p, &on) || *p++ != ',' || !parse_rule(&p, &off)) return false;
    }
    if (*p) return false;

    /* the rules are read in the year of the local standard time; a southern pair (start after end) is the
     * complement of the interval from end to start */
    const int64_t y = year_of_days(floor_div(utc + std_e, 86400));
    const int64_t start = rule_local(&on, y) - std_e;   /* at local standard time */
    const int64_t end = rule_local(&off, y) - dst_e;    /* at local daylight time */
    const bool is_dst = start < end ? (utc >= start && utc < end) : !(utc >= end && utc < start);
    *dst = is_dst;
    *off_s = is_dst ? dst_e : std_e;
    return true;
}

/* ---- the band ---- */

static const struct { const char *name, *country, *code; } k_twelve[12] = {
    {"London", "GB", "LON"}, {"New York", "US", "NYC"}, {"Los Angeles", "US", "LAX"}, {"Honolulu", "US", "HNL"},
    {"Tokyo", "JP", "TYO"},  {"Sydney", "AU", "SYD"},   {"Singapore", "SG", "SIN"},   {"Delhi", "IN", "DEL"},
    {"Dubai", "AE", "DXB"},  {"Cairo", "EG", "CAI"},    {"Sao Paulo", "BR", "SAO"},   {"Auckland", "NZ", "AKL"},
};

void omni_wt_code(const char *name, char code[4])
{
    if (name && !strcmp(name, "Columbus")) {
        memcpy(code, "CMH", 4);
        return;
    }
    for (int i = 0; name && i < 12; i++) {
        if (!strcmp(name, k_twelve[i].name)) {
            memcpy(code, k_twelve[i].code, 4);
            return;
        }
    }
    int n = 0;
    for (const char *s = name; s && *s && n < 3; s++) {
        if (*s >= 'a' && *s <= 'z') code[n++] = (char)(*s - 'a' + 'A');
        else if (*s >= 'A' && *s <= 'Z') code[n++] = *s;
    }
    code[n] = '\0';
}

int omni_wt_list(uint16_t home_city, omni_wt_city_t out[OMNI_WT_MAX])
{
    int n = 0;
    bool home_in = false;
    for (int i = 0; i < 12; i++) {
        const int c = omni_city_find_key(k_twelve[i].name, k_twelve[i].country);
        if (c < 0) continue;
        memcpy(out[n].code, k_twelve[i].code, 4);
        out[n].city = (uint16_t)c;
        out[n].home = (c == (int)home_city);
        home_in |= out[n].home;
        n++;
    }
    const omni_city_t *h = omni_city_at(home_city);
    if (!home_in && h) {
        omni_wt_code(h->name, out[n].code);
        out[n].city = home_city;
        out[n].home = true;
        n++;
    }
    return n;
}

float omni_wt_angle(float city_lon, float lon_c)
{
    return -PI_F / 2.0f - (city_lon - lon_c) * (PI_F / 180.0f);
}

bool omni_wt_flip(float a)
{
    return sinf(a) > 0.15f;
}

int omni_wt_hit(const omni_wt_city_t *l, int n, int16_t x, int16_t y, float lon_c)
{
    const float dx = (float)x - CENTRE, dy = (float)y - CENTRE;
    const float r = sqrtf(dx * dx + dy * dy);
    if (r < 60.0f) return -2;
    if (r < 120.0f) return -1;
    const float a = atan2f(dy, dx);
    int best = -1;
    float best_d = 0.25f;
    for (int i = 0; i < n; i++) {
        const omni_city_t *c = omni_city_at(l[i].city);
        if (!c) continue;
        const float d = fabsf(remainderf(a - omni_wt_angle(c->lon, lon_c), 2.0f * PI_F));
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* ---- the cartouche ---- */

typedef struct { int hour, min, wday; int32_t off; } civil_t;

static bool civil_of(const omni_wt_city_t *c, int64_t now, civil_t *out)
{
    const omni_city_t *city = c ? omni_city_at(c->city) : NULL;
    bool dst;
    if (!city || !omni_tz_offset(city->posix_tz, now, &out->off, &dst)) return false;
    const int64_t local = now + out->off;
    const int64_t days = floor_div(local, 86400);
    const int64_t sod = local - days * 86400;
    out->hour = (int)(sod / 3600);
    out->min = (int)(sod % 3600 / 60);
    out->wday = (int)(((days + 4) % 7 + 7) % 7);
    return true;
}

static int hour12(int h)
{
    return h % 12 == 0 ? 12 : h % 12;
}

static void day3(int wday, char out[4])
{
    memcpy(out, omni_day_name(wday), 3);
    out[3] = '\0';
    omni_ascii_upper(out);
}

void omni_wt_cartouche(const omni_wt_city_t *sel, const omni_wt_city_t *home, int64_t now, bool h24,
                       char big[8], char top[4], char sub[24])
{
    const bool is_home = !sel || sel->home;
    const omni_wt_city_t *c = is_home ? home : sel;
    snprintf(top, 4, "%s", c ? c->code : "");
    big[0] = sub[0] = '\0';
    civil_t cv, hv;
    if (!omni_time_is_set(now) || !civil_of(c, now, &cv)) {
        snprintf(big, 8, "--:--");
        return;
    }
    if (h24) snprintf(big, 8, "%02d:%02d", cv.hour, cv.min);
    else snprintf(big, 8, "%d:%02d", hour12(cv.hour), cv.min);
    char d[4];
    day3(cv.wday, d);
    char ap[4] = "";
    if (!h24) snprintf(ap, sizeof ap, "%s", cv.hour >= 12 ? "PM" : "AM");
    if (is_home || !civil_of(home, now, &hv)) {
        snprintf(sub, 24, "%s%s%s", d, h24 ? "" : " ", ap);
        return;
    }
    const int32_t delta = cv.off - hv.off;
    const int32_t a = (delta < 0 ? -delta : delta) % (100 * 3600);   /* under 100 h: real offsets are within 26 h */
    char off[16];
    const char *sign = delta < 0 ? "\xe2\x80\x93" : "+";
    if (a % 3600) snprintf(off, sizeof off, "%s%d:%02dh", sign, (int)(a / 3600), (int)(a % 3600 / 60));
    else snprintf(off, sizeof off, "%s%dh", sign, (int)(a / 3600));
    char full[48];   /* at most "WED PM +14:45h" (14 chars); the wide buffer keeps the compiler's bound honest */
    snprintf(full, sizeof full, "%s%s%s %s", d, h24 ? "" : " ", ap, off);
    snprintf(sub, 24, "%.23s", full);
}

void omni_wt_glance_text(const omni_wt_city_t *c, int64_t now, bool h24, char out[24])
{
    civil_t cv;
    if (!c || !omni_time_is_set(now) || !civil_of(c, now, &cv)) {
        snprintf(out, 24, "%s  --:--", c ? c->code : "");
        return;
    }
    if (h24) snprintf(out, 24, "%s  %02d:%02d", c->code, cv.hour, cv.min);
    else snprintf(out, 24, "%s  %d:%02d %s", c->code, hour12(cv.hour), cv.min, cv.hour >= 12 ? "pm" : "am");
}

void omni_wt_reveal(uint32_t t_ms, bool reduced, omni_reveal_t *r)
{
    (void)reduced;   /* nothing moves: the same curve */
    const float f = (float)t_ms / 300.0f;
    r->fade = f > 1.0f ? 1.0f : f;
    r->lon_off_deg = 0.0f;   /* R138: no spin (the mockup's 120 deg turn cost a globe frame per step: 10.6 fps) */
    r->gain = 1.0f;
    r->settle = 1.0f;
    r->done = t_ms >= 300u;
}

/* ---- the band's glyphs (R141) ---- */

int omni_wt_glyphs(const float *adv, int n, float spacing, float r, float mid, float a, bool flip, int32_t cx,
                   int32_t cy, omni_wt_glyph_t *out)
{
    if (n < 1 || n > OMNI_WT_GLYPHS || !(r > 0.0f)) return 0;
    float len = spacing * (float)(n - 1);
    for (int i = 0; i < n; i++) len += adv[i];
    const float rb = flip ? r + mid : r - mid, dir = flip ? -1.0f : 1.0f;
    float s = -len / 2.0f;   /* the run's start, px of arc from a */
    for (int i = 0; i < n; i++) {
        const float c = s + adv[i] / 2.0f, th = a + dir * c / r;
        out[i].x = cosf(th) * rb;
        out[i].y = sinf(th) * rb;
        out[i].rot_deg = th * (180.0f / PI_F) + (flip ? -90.0f : 90.0f);
        out[i].px = cx + (int32_t)lroundf(out[i].x);   /* no pivot here: lv_draw_letter applies it */
        out[i].py = cy + (int32_t)lroundf(out[i].y);
        s += adv[i] + spacing;
    }
    return n;
}
