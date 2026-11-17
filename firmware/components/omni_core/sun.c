#include "omni/sun.h"
#include "omni/timefmt.h"
#include <math.h>

#define D2R (3.14159265358979323846 / 180.0)
#define R2D (180.0 / 3.14159265358979323846)
#define DAY_S 86400

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* Declination (rad) and equation of time (minutes, apparent minus mean sun: positive when a sundial
 * runs fast) at a UTC instant. Meeus ch. 25 low-precision series, as in the mockup's sunPos (R47);
 * it matches the Meeus reference declination within 0.01 deg, where the NOAA fractional-year fit
 * Stage 1 used was 0.2-0.4 deg off. */
static void decl_eot(int64_t unix_s, double *decl_rad, double *eot_min)
{
    double d = (double)unix_s / 86400.0 - 10957.5;   /* days since J2000.0 */
    double g = (357.529 + 0.98560028 * d) * D2R;
    double q = 280.459 + 0.98564736 * d;
    double L = (q + 1.915 * sin(g) + 0.020 * sin(2 * g)) * D2R;
    double e = (23.439 - 0.00000036 * d) * D2R;
    double ra = atan2(cos(e) * sin(L), cos(L)) * R2D;
    double eq = fmod(fmod(q - ra, 360.0) + 540.0, 360.0);
    if (eq < 0) eq += 360.0;
    eq -= 180.0;
    *decl_rad = asin(sin(e) * sin(L));
    *eot_min = eq * 4.0;   /* 15 deg per hour */
}

void omni_sun_at(int64_t unix_s, omni_sun_t *out)
{
    double decl, eot;
    decl_eot(unix_s, &decl, &eot);
    int64_t days = floor_div(unix_s, DAY_S);
    double utc_min = (double)(unix_s - days * DAY_S) / 60.0;
    double lon = -0.25 * (utc_min - 720.0 + eot);   /* -15 deg per hour */
    lon = fmod(lon + 540.0, 360.0);
    if (lon < 0) lon += 360.0;
    lon -= 180.0;
    out->decl_deg = decl * R2D;
    out->eot_min = eot;
    out->subsolar_lat = decl * R2D;
    out->subsolar_lon = lon;
}

bool omni_sun_light(int64_t unix_s, omni_sun_t *out)
{
    if (!omni_time_is_set(unix_s)) return false;
    omni_sun_at(unix_s, out);
    return true;
}

omni_sun_day_t omni_sun_rise_set(int64_t unix_day_noon_utc, double lat, double lon,
                                 int64_t *rise_unix, int64_t *set_unix)
{
    int64_t midnight = unix_day_noon_utc - 12 * 3600;
    double decl, eot = 0.0;
    int64_t noon = unix_day_noon_utc;
    for (int i = 0; i < 3; i++) {   /* converge solar noon, evaluating decl/EoT there */
        decl_eot(noon, &decl, &eot);
        noon = midnight + (int64_t)llround((720.0 - 4.0 * lon - eot) * 60.0);
    }
    decl_eot(noon, &decl, &eot);

    double p = lat * D2R;
    double cosh = (cos(90.833 * D2R) - sin(p) * sin(decl)) / (cos(p) * cos(decl));
    if (cosh > 1.0) return SUN_ALWAYS_DOWN;
    if (cosh < -1.0) return SUN_ALWAYS_UP;
    int64_t half = (int64_t)llround(acos(cosh) * R2D * 4.0 * 60.0);   /* 4 min per degree */
    if (rise_unix) *rise_unix = noon - half;
    if (set_unix) *set_unix = noon + half;
    return SUN_NORMAL;
}

bool omni_sun_next_event(int64_t now_unix, double lat, double lon, int64_t *when_unix, bool *is_rise)
{
    int64_t today_noon = floor_div(now_unix, DAY_S) * DAY_S + 12 * 3600;
    int64_t rise, set;
    if (omni_sun_rise_set(today_noon, lat, lon, &rise, &set) != SUN_NORMAL) return false;

    bool found = false;
    int64_t best = 0;
    bool best_rise = false;
    for (int d = -1; d <= 1; d++) {
        if (omni_sun_rise_set(today_noon + d * (int64_t)DAY_S, lat, lon, &rise, &set) != SUN_NORMAL)
            continue;
        if (rise > now_unix && (!found || rise < best)) { found = true; best = rise; best_rise = true; }
        if (set > now_unix && (!found || set < best)) { found = true; best = set; best_rise = false; }
    }
    if (!found) return false;
    if (when_unix) *when_unix = best;
    if (is_rise) *is_rise = best_rise;
    return true;
}
