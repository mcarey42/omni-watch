#include "omni/almanac.h"
#include "omni/blend565.h"
#include "omni/raster.h"
#include "omni/trig.h"
#include <math.h>
#include <string.h>
#include <time.h>

/* The almanac's two bitmaps: the sky ring (the mockup's buildAlmRing plus the face's rise/set ticks) and the moon
 * (buildMoon). Single precision with a fixed evaluation order (-ffp-contract=off), the angle of each ring pixel by
 * omni_atan2_tab (an integer reciprocal and a table, no libm), so the device builds the bytes the host pins. The one
 * exception is the 288 sample colours, which come from almanac.c's double-precision sun maths and are quantised to RGB565:
 * a libm difference in the last ulp could move a channel by one level only where a value sits on a rounding edge. */

#define PI_F 3.14159265358979323846f
#define TAU 6.28318530717958647692
#define N_SAMPLES 288
#define BOX OMNI_ALM_BOX
#define CEN 219.0f   /* the ring centre in box px (233 - 14) */

static uint16_t rgb565(int r, int g, int b) { return omni_pack565(r, g, b); }

/* Days since 1970-01-01 of a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

/* The local clock reading of instant t, as if it were UTC, minus t: the UTC offset in force at t (seconds). */
static int64_t utc_offset(int64_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return 0;
    return days_from_civil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 86400 + tm.tm_hour * 3600 +
           tm.tm_min * 60 + tm.tm_sec - t;
}

/* The instant at which the local clock reads `naive` (the reading taken as UTC), resolved explicitly rather than by
 * libc's mktime(tm_isdst = -1), whose choice differs between glibc and newlib:
 *  - an ordinary time: the one instant;
 *  - the overlap hour of a fall-back (the clock reads it twice): the first, earlier one;
 *  - the gap hour of a spring-forward (the clock never reads it): the offset in force before the jump, so the sample
 *    shows the sky one hour later by the dial, as the clock itself jumps. */
static int64_t local_instant(int64_t naive)
{
    const int64_t oa = utc_offset(naive - 86400), ob = utc_offset(naive + 86400);
    const int64_t ta = naive - oa, tb = naive - ob;
    if (oa == ob) return ta;
    const bool va = utc_offset(ta) == oa, vb = utc_offset(tb) == ob;
    if (va && vb) return ta < tb ? ta : tb;
    if (va) return ta;
    if (vb) return tb;
    return ta;   /* the gap: the offset in force before the jump */
}

/* The 288 samples' RGB565: sample i is the instant whose local time is i x 5 min on day0's date. */
static void ring_colours(uint16_t col[N_SAMPLES], int64_t day0, double lat, double lon, bool set)
{
    uint8_t c[3];
    if (!set) {
        omni_alm_sky_rgb(-90.0, c);
        for (int i = 0; i < N_SAMPLES; i++) col[i] = rgb565(c[0], c[1], c[2]);
        return;
    }
    time_t t0 = (time_t)day0;
    struct tm base;
    if (!localtime_r(&t0, &base)) memset(&base, 0, sizeof base);
    const int64_t midnight = days_from_civil(base.tm_year + 1900, base.tm_mon + 1, base.tm_mday) * 86400;
    for (int i = 0; i < N_SAMPLES; i++) {
        double alt, az;
        omni_alm_altaz(local_instant(midnight + i * 300), lat, lon, &alt, &az);
        omni_alm_sky_rgb(alt, c);
        col[i] = rgb565(c[0], c[1], c[2]);
    }
}

static float hour_angle(float h) { return PI_F / 2.0f + h / 24.0f * (float)TAU; }

/* A radial line at hour h from r0 to r1, w px wide, as a quad. */
static void radial_quad(omni_pt_t p[4], float h, float r0, float r1, float w)
{
    const float a = hour_angle(h), c = cosf(a), s = sinf(a), nx = -s * w / 2.0f, ny = c * w / 2.0f;
    p[0] = (omni_pt_t){ CEN + c * r0 + nx, CEN + s * r0 + ny };
    p[1] = (omni_pt_t){ CEN + c * r1 + nx, CEN + s * r1 + ny };
    p[2] = (omni_pt_t){ CEN + c * r1 - nx, CEN + s * r1 - ny };
    p[3] = (omni_pt_t){ CEN + c * r0 - nx, CEN + s * r0 - ny };
}

/* The band 190..214 coloured by the pixel's hour, one atan2 per covered pixel. */
static void paint_band(uint16_t *box, const uint16_t col[N_SAMPLES])
{
    uint8_t cov[OMNI_COVER_W];
    omni_ring_t band;   /* built once: per row chunk it cost ~130 sinf/cosf, the render's hot spot on the device */
    omni_ring_prepare(&band, CEN, CEN, 190.0f, 214.0f);
    for (int y = 0; y < BOX; y++) {
        const double dy = y + 0.5 - (double)CEN;
        if (fabs(dy) >= 215.0) continue;
        const double ho = sqrt(215.0 * 215.0 - dy * dy);
        const int lo = (int)floor((double)CEN - ho), hi = (int)ceil((double)CEN + ho);
        int seg[4] = { lo < 0 ? 0 : lo, hi > BOX ? BOX : hi, 0, 0 }, nseg = 1;
        if (fabs(dy) < 189.0) {   /* the hole in the middle */
            const double hh = sqrt(189.0 * 189.0 - dy * dy);
            seg[3] = seg[1];
            seg[1] = (int)floor((double)CEN - hh);
            seg[2] = (int)ceil((double)CEN + hh);
            nseg = 2;
        }
        for (int s = 0; s < nseg; s++)
            for (int xa = seg[2 * s]; xa < seg[2 * s + 1]; xa += OMNI_COVER_W) {
                const int xb = xa + OMNI_COVER_W < seg[2 * s + 1] ? xa + OMNI_COVER_W : seg[2 * s + 1];
                omni_cover_ring_prepared(&band, y, xa, xb, cov);
                for (int x = xa; x < xb; x++) {
                    if (!cov[x - xa]) continue;
                    const float th = omni_atan2_tab((float)dy, (float)x + 0.5f - CEN);
                    float h = (th - PI_F / 2.0f) * (24.0f / (float)TAU);
                    if (h < 0.0f) h += 24.0f;
                    int i = (int)(h * 12.0f);
                    i = i < 0 ? 0 : i >= N_SAMPLES ? N_SAMPLES - 1 : i;
                    box[y * BOX + x] = omni_blend565(0, col[i], cov[x - xa]);
                }
            }
    }
}

void omni_alm_ring_render(uint16_t *box, int64_t day0_unix, double lat, double lon, float rise_h, float set_h, bool set)
{
    uint16_t col[N_SAMPLES];
    const omni_rect_t clip = { 0, 0, BOX, BOX };
    omni_pt_t q[4];
    memset(box, 0, (size_t)BOX * BOX * sizeof *box);
    ring_colours(col, day0_unix, lat, lon, set);
    paint_band(box, col);
    /* engraved hour ticks: black .75 w 2 across the band at 0, 6, 12, 18; black .45 w 1 to r 197 elsewhere */
    for (int h = 0; h < 24; h++) {
        const bool major = h % 6 == 0;
        radial_quad(q, (float)h, 190.0f, major ? 214.0f : 197.0f, major ? 2.0f : 1.0f);
        omni_fill_poly(box, BOX, &clip, q, 4, 0, major ? 191 : 115);
    }
    /* brass hairlines, rgba(201,163,91,.55) */
    omni_fill_ring(box, BOX, &clip, CEN, CEN, 189.0f, 190.0f, rgb565(201, 163, 91), 140);
    omni_fill_ring(box, BOX, &clip, CEN, CEN, 214.0f, 215.0f, rgb565(201, 163, 91), 140);
    if (!set) return;
    const float hs[2] = { rise_h, set_h };
    for (int i = 0; i < 2; i++) {
        if (!(hs[i] >= 0.0f)) continue;
        radial_quad(q, hs[i], 186.0f, 218.0f, 2.0f);
        omni_fill_poly(box, BOX, &clip, q, 4, rgb565(0xec, 0xd2, 0x9a), 255);
    }
}

/* ---------------------------------------------------------------- the moon */

static const float MARIA[10][3] = { { -0.3f, -0.35f, 0.26f }, { 0.18f, -0.32f, 0.16f }, { 0.33f, -0.06f, 0.19f },
                                    { 0.64f, -0.24f, 0.1f },  { 0.55f, 0.14f, 0.13f },  { -0.15f, 0.36f, 0.15f },
                                    { -0.62f, -0.02f, 0.3f }, { 0.36f, 0.27f, 0.08f },  { -0.42f, 0.18f, 0.12f },
                                    { -0.05f, -0.62f, 0.1f } };
#define N_CRATERS 70

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int to8(float v) { const long r = lroundf(v); return r < 0 ? 0 : r > 255 ? 255 : (int)r; }

void omni_alm_moon_render(uint8_t *rgb565a8, int d, double f)
{
    float cr[N_CRATERS][3];
    uint16_t *col = (uint16_t *)(void *)rgb565a8;
    uint8_t *alpha = rgb565a8 + (size_t)d * d * 2;
    /* the mockup's seeded generator (7, x 16807 mod 2^31 - 1), exact in 64-bit integers */
    uint64_t seed = 7;
    for (int i = 0; i < N_CRATERS; i++) {
        double v[3];
        for (int j = 0; j < 3; j++) {
            seed = seed * 16807u % 2147483647u;
            v[j] = (double)seed / 2147483647.0;
        }
        cr[i][0] = (float)(v[0] * 2 - 1);
        cr[i][1] = (float)(v[1] * 2 - 1);
        cr[i][2] = (float)(0.015 + v[2] * 0.045);
    }
    const float R = (float)d / 2.0f, ph = (float)(f * TAU), Lx = sinf(ph), Lz = -cosf(ph);
    for (int y = 0; y < d; y++)
        for (int x = 0; x < d; x++) {
            const int o = y * d + x;
            const float nx = ((float)x + 0.5f - R) / R, ny = ((float)y + 0.5f - R) / R, r2 = nx * nx + ny * ny;
            if (r2 > 1.02f) {
                col[o] = 0;
                alpha[o] = 0;
                continue;
            }
            const float nz = sqrtf(fmaxf(0.0f, 1.0f - r2));
            float alb = 0.82f;
            for (int i = 0; i < 10; i++) {
                const float ex = nx - MARIA[i][0], ey = ny - MARIA[i][1], lim = 1.4f * MARIA[i][2];
                if (ex * ex + ey * ey >= lim * lim) continue;   /* the cheap reject: the term is 0 out here */
                const float dd = hypotf(ex, ey) / MARIA[i][2];
                if (dd < 1.4f) alb -= 0.16f * (1.0f - clampf((dd - 0.4f) / 1.0f, 0.0f, 1.0f));
            }
            for (int i = 0; i < N_CRATERS; i++) {
                const float ex = nx - cr[i][0], ey = ny - cr[i][1];
                if (ex * ex + ey * ey >= cr[i][2] * cr[i][2]) continue;
                const float dd = hypotf(ex, ey) / cr[i][2];
                if (dd < 1.0f) alb += dd > 0.7f ? 0.035f * (1.0f - dd) / 0.3f : -0.018f * (1.0f - dd);
            }
            alb += 0.25f * fmaxf(0.0f, 1.0f - hypotf(nx + 0.1f, ny - 0.7f) / 0.07f);   /* Tycho */
            const float mu = nx * Lx + nz * Lz;
            float s = clampf((mu + 0.04f) / 0.1f, 0.0f, 1.0f);
            s = s * s * (3.0f - 2.0f * s);
            const float lum = (s * (0.35f + 0.65f * powf(fmaxf(mu, 0.0f), 0.35f)) * alb + 0.035f * (1.0f - s)) *
                              (0.75f + 0.25f * nz);
            col[o] = omni_pack565(to8(255.0f * lum), to8(255.0f * lum * 0.97f), to8(255.0f * lum * 0.9f));
            alpha[o] = (uint8_t)to8(255.0f * clampf((1.0f - sqrtf(r2)) * R + 0.5f, 0.0f, 1.0f));
        }
}
