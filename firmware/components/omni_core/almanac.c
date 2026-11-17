#include "omni/almanac.h"
#include "omni/timefmt.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846
#define D2R (PI / 180.0)
#define R2D (180.0 / PI)
#define DAY_S 86400

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------------------------------------------------------------- sun */

void omni_alm_altaz(int64_t unix_s, double lat, double lon, double *alt_deg, double *az_deg)
{
    omni_sun_t s;
    omni_sun_at(unix_s, &s);
    const double p = lat * D2R, dec = s.subsolar_lat * D2R, H = (lon - s.subsolar_lon) * D2R;
    const double mu = sin(p) * sin(dec) + cos(p) * cos(dec) * cos(H);
    if (alt_deg) *alt_deg = asin(clampd(mu, -1.0, 1.0)) * R2D;
    if (az_deg) {
        double az = atan2(sin(H), cos(H) * sin(p) - tan(dec) * cos(p)) * R2D + 180.0;
        az = fmod(az, 360.0);
        if (az < 0) az += 360.0;
        *az_deg = az;
    }
}

static const struct { double alt; uint8_t c[3]; } SKY[] = {
    { -90, { 6, 8, 16 } },    { -18, { 8, 11, 24 } },  { -12, { 16, 26, 52 } },  { -6, { 34, 56, 96 } },
    { -0.8, { 120, 92, 60 } }, { 6, { 150, 114, 58 } }, { 20, { 110, 98, 72 } }, { 90, { 118, 106, 80 } },
};
#define NSKY ((int)(sizeof SKY / sizeof SKY[0]))

void omni_alm_sky_rgb(double alt_deg, uint8_t rgb[3])
{
    for (int i = 1; i < NSKY; i++)
        if (alt_deg <= SKY[i].alt) {
            const double f = clampd((alt_deg - SKY[i - 1].alt) / (SKY[i].alt - SKY[i - 1].alt), 0.0, 1.0);
            for (int j = 0; j < 3; j++) {
                const double v = SKY[i - 1].c[j] + (SKY[i].c[j] - SKY[i - 1].c[j]) * f;
                rgb[j] = (uint8_t)lround(clampd(v, 0, 255));
            }
            return;
        }
    memcpy(rgb, SKY[NSKY - 1].c, 3);
}

/* Days since 1970-01-01 of a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* 12:00 UTC of the local calendar date of t (the input omni_sun_rise_set wants). */
static int64_t local_date_noon(int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return (t / DAY_S) * DAY_S + 12 * 3600;
    return days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * DAY_S + 12 * 3600;
}

/* Solar noon of the solar day whose noon falls on the UTC date of noon_utc (the mockup's sunTimes, two passes). */
static int64_t solar_noon(int64_t noon_utc, double lon)
{
    const int64_t midnight = noon_utc - 12 * 3600;
    omni_sun_t s;
    int64_t n = midnight + (int64_t)llround((720.0 - 4.0 * lon) * 60.0);
    omni_sun_at(n, &s);
    n = midnight + (int64_t)llround((720.0 - 4.0 * lon - s.eot_min) * 60.0);
    omni_sun_at(n, &s);
    return midnight + (int64_t)llround((720.0 - 4.0 * lon - s.eot_min) * 60.0);
}

static int32_t day_len(omni_sun_day_t k, int64_t rise, int64_t set)
{
    return k == SUN_ALWAYS_UP ? DAY_S : k == SUN_ALWAYS_DOWN ? 0 : (int32_t)(set - rise);
}

void omni_alm_day(int64_t now, double lat, double lon, omni_alm_day_t *d)
{
    const int64_t noon = local_date_noon(now);
    int64_t yr = 0, ys = 0;
    memset(d, 0, sizeof *d);
    d->kind = omni_sun_rise_set(noon, lat, lon, &d->rise, &d->set);
    if (d->kind != SUN_NORMAL) d->rise = d->set = 0;
    d->len_s = day_len(d->kind, d->rise, d->set);
    d->noon = d->kind == SUN_NORMAL ? (d->rise + d->set) / 2 : solar_noon(noon, lon);
    const omni_sun_day_t yk = omni_sun_rise_set(noon - DAY_S, lat, lon, &yr, &ys);
    d->delta_s = d->len_s - day_len(yk, yr, ys);
}

/* ---------------------------------------------------------------- moon (Meeus ch. 49) */

/* Delta T (TT - UT), seconds, Espenak and Meeus for 2005-2050; fine for a watch's decades either side. */
static double delta_t(double year)
{
    const double t = year - 2000.0;
    return 62.92 + 0.32217 * t + 0.005589 * t * t;
}

int64_t omni_alm_phase_unix(double k)
{
    const double T = k / 1236.85, T2 = T * T, T3 = T2 * T, T4 = T3 * T;
    double jde = 2451550.09766 + 29.530588861 * k + 0.00015437 * T2 - 0.000000150 * T3 + 0.00000000073 * T4;
    const double E = 1.0 - 0.002516 * T - 0.0000074 * T2;
    const double M = (2.5534 + 29.10535670 * k - 0.0000014 * T2 - 0.00000011 * T3) * D2R;
    const double Mp = (201.5643 + 385.81693528 * k + 0.0107582 * T2 + 0.00001238 * T3 - 0.000000058 * T4) * D2R;
    const double F = (160.7108 + 390.67050284 * k - 0.0016118 * T2 - 0.00000227 * T3 + 0.000000011 * T4) * D2R;
    const double Om = (124.7746 - 1.56375588 * k + 0.0020672 * T2 + 0.00000215 * T3) * D2R;
    const bool full = fabs(k - floor(k) - 0.5) < 1e-6;
    /* the periodic terms of Table 49.A: new moon, then full moon where it differs (the two leading terms and the
     * small first-order ones) */
    const double c1 = full ? -0.40614 : -0.40720, c2 = full ? 0.17302 : 0.17241, c3 = full ? 0.01614 : 0.01608,
                 c4 = full ? 0.01043 : 0.01039, c5 = full ? 0.00734 : 0.00739, c6 = full ? -0.00515 : -0.00514,
                 c7 = full ? 0.00209 : 0.00208;
    double s = c1 * sin(Mp) + c2 * E * sin(M) + c3 * sin(2 * Mp) + c4 * sin(2 * F) + c5 * E * sin(Mp - M) +
               c6 * E * sin(Mp + M) + c7 * E * E * sin(2 * M) - 0.00111 * sin(Mp - 2 * F) -
               0.00057 * sin(Mp + 2 * F) + 0.00056 * E * sin(2 * Mp + M) - 0.00042 * sin(3 * Mp) +
               0.00042 * E * sin(M + 2 * F) + 0.00038 * E * sin(M - 2 * F) - 0.00024 * E * sin(2 * Mp - M) -
               0.00017 * sin(Om) - 0.00007 * sin(Mp + 2 * M) + 0.00004 * sin(2 * Mp - 2 * F) +
               0.00004 * sin(3 * M) + 0.00003 * sin(Mp + M - 2 * F) + 0.00003 * sin(2 * Mp + 2 * F) -
               0.00003 * sin(Mp + M + 2 * F) + 0.00003 * sin(Mp - M + 2 * F) - 0.00002 * sin(Mp - M - 2 * F) -
               0.00002 * sin(3 * Mp + M) + 0.00002 * sin(4 * Mp);
    /* the planetary terms A1..A14 */
    const double A[14] = { 299.77 + 0.107408 * k - 0.009173 * T2, 251.88 + 0.016321 * k, 251.83 + 26.651886 * k,
                           349.42 + 36.412478 * k, 84.66 + 18.206239 * k, 141.74 + 53.303771 * k,
                           207.14 + 2.453732 * k, 154.84 + 7.306860 * k, 34.52 + 27.261239 * k,
                           207.19 + 0.121824 * k, 291.34 + 1.844379 * k, 161.72 + 24.198154 * k,
                           239.56 + 25.513099 * k, 331.55 + 3.592518 * k };
    static const double AC[14] = { 0.000325, 0.000165, 0.000164, 0.000126, 0.000110, 0.000062, 0.000060,
                                   0.000056, 0.000047, 0.000042, 0.000040, 0.000037, 0.000035, 0.000023 };
    for (int i = 0; i < 14; i++) s += AC[i] * sin(A[i] * D2R);
    jde += s;
    const double ut_s = (jde - 2440587.5) * 86400.0 - delta_t(2000.0 + (jde - 2451545.0) / 365.25);
    return (int64_t)llround(ut_s);
}

static const char *const MOON_NAMES[8] = { "New moon", "Waxing crescent", "First quarter", "Waxing gibbous",
                                           "Full moon", "Waning gibbous", "Last quarter", "Waning crescent" };

const char *omni_alm_moon_name(uint8_t i) { return MOON_NAMES[i & 7u]; }

void omni_alm_moon(int64_t unix_s, omni_alm_moon_t *m)
{
    const double year = 1970.0 + (double)unix_s / (365.2425 * 86400.0);
    double k = floor((year - 2000.0) * 12.3685);
    /* the estimate is within one lunation; the caps only bound the work for a nonsense unix_s */
    for (int n = 0; n < 4 && omni_alm_phase_unix(k + 1) <= unix_s; n++) k += 1;
    for (int n = 0; n < 4 && omni_alm_phase_unix(k) > unix_s; n++) k -= 1;
    const int64_t n0 = omni_alm_phase_unix(k), fu = omni_alm_phase_unix(k + 0.5), n1 = omni_alm_phase_unix(k + 1);
    double f;
    if (unix_s < fu) {
        f = 0.5 * (double)(unix_s - n0) / (double)(fu - n0);
        m->next_full = fu;
    } else {
        f = 0.5 + 0.5 * (double)(unix_s - fu) / (double)(n1 - fu);
        m->next_full = omni_alm_phase_unix(k + 1.5);
    }
    m->next_new = n1;
    m->f = f;
    m->illum = (1.0 - cos(f * 2.0 * PI)) / 2.0;
    m->name = (uint8_t)((int)floor(f * 8.0 + 0.5) % 8);
}

/* ---------------------------------------------------------------- texts */

void omni_alm_daylen_text(const omni_alm_day_t *d, bool set, char out[12])
{
    if (!set) {
        snprintf(out, 12, "--h  --m");
        return;
    }
    int64_t m = ((int64_t)d->len_s + 30) / 60;
    if (m < 0) m = 0;
    if (m > 99 * 60 + 59) m = 99 * 60 + 59;
    snprintf(out, 12, "%dh  %02dm", (int)(m / 60), (int)(m % 60));
}

void omni_alm_delta_text(const omni_alm_day_t *d, bool set, char out[28])
{
    if (!set) {
        out[0] = '\0';
        return;
    }
    int32_t a = d->delta_s < 0 ? -d->delta_s : d->delta_s;
    if (a > 5999) a = 5999;
    snprintf(out, 28, "%c%dm %02ds vs yesterday", d->delta_s < 0 ? '-' : '+', (int)(a / 60), (int)(a % 60));
}

void omni_alm_moon_text(const omni_alm_moon_t *m, char out[40])
{
    const bool full = m->f < 0.5;
    const int64_t t = full ? m->next_full : m->next_new;
    time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) tm = (struct tm){ 0 };
    int pct = (int)lround(m->illum * 100.0);
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    snprintf(out, 40, "%d%% \xc2\xb7 %s %.3s %d %.3s", pct, full ? "Full" : "New", omni_day_name(tm.tm_wday),
             (int)(tm.tm_mday % 100), omni_month_name(tm.tm_mon));
}

void omni_alm_alt_text(double alt, char out[12])
{
    long v = lround(alt);
    if (v > 99) v = 99;
    if (v < -99) v = -99;
    if (v < 0)
        snprintf(out, 12, "\xe2\x88\x92%ld\xc2\xb0", -v);
    else
        snprintf(out, 12, "%ld\xc2\xb0", v);
}

void omni_alm_sun_lines(int64_t now, double lat, double lon, bool h24, char az[24], char noon[24], char shadow[32])
{
    static const char *const DIRS[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    double alt, a;
    omni_alm_altaz(now, lat, lon, &alt, &a);
    int deg = (int)lround(a);
    if (deg >= 360) deg -= 360;
    snprintf(az, 24, "azimuth %d\xc2\xb0 %s", deg, DIRS[(int)lround(a / 45.0) % 8]);
    char hm[8];
    omni_fmt_short_time(solar_noon(local_date_noon(now), lon), h24, hm);
    snprintf(noon, 24, "solar noon %s", hm);
    if (alt > 1.0)
        snprintf(shadow, 32, "1 m stick casts %.1f m", 1.0 / tan(alt * D2R));
    else
        snprintf(shadow, 32, "sun below horizon");
}
