#include "omni/globe.h"
#include <math.h>
#include <string.h>

/* Port of Globe.prototype.build / project in design/mockups/faces-practical.js: same pixel centres,
 * the r > 1 + 1/R cut-off and the 0.9995 clamp. The per-pixel build runs in single precision on the ESP32-S3's
 * FPU (it has no double unit: the original double build took 10-15 s per face) with only + - * / and sqrtf, in a
 * fixed order (no libm; globe.c is built with -ffp-contract=off), so host and device build the same bytes.
 * The trig the planes need is gone or approximated to about 1e-7 rad:
 *  - ortho a, b, c = sin lat, cos lat cos lonRel, cos lat sin lonRel are the world-frame normal's Y, Zf, xv;
 *  - lat = atan2(Y, hypot(xv, Zf)) and lonRel = atan2(xv, Zf) through atan2_f below;
 *  - limb and rim's powers through pow_f (log2/exp2 series, relative error about 1e-7).
 * The planes then differ from the mockup's double maths only where a value lies within float rounding of a
 * quantisation step (test_lut_float_vs_double bounds how often and by how much). */

#define PI 3.14159265358979323846
#define D2R (PI / 180.0)
#define ALIGN16(x) (((x) + 15u) & ~(size_t)15u)
#define PI_F 3.14159265f

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static bool cfg_valid(const omni_globe_cfg_t *c)
{
    if (c->diam < 2 || c->size < c->diam || c->size > 32767 || (c->size & 1) || ((c->size - c->diam) & 1)) return false;
    if (c->tex_w < 2 || c->tex_w > 2048 || (c->tex_w & (c->tex_w - 1))) return false;   /* Q4 u fits int16 */
    if (c->proj != OMNI_PROJ_ORTHO && c->proj != OMNI_PROJ_POLAR) return false;
    if (c->studio && c->proj != OMNI_PROJ_ORTHO) return false;   /* the mockup lights only ortho globes by ndl */
    return true;
}

static double max_colat_rad(const omni_globe_cfg_t *c)
{
    return (c->max_colat_deg > 0 ? c->max_colat_deg : 150.0) * D2R;
}

/* ---- Float maths: IEEE single + - * / and sqrtf (correctly rounded on both: libgcc's __divsf3 and the device's
 * sqrtf use the S3 FPU's divide/sqrt steps), no other libm, a fixed evaluation order: the same bits on the host and
 * the device (the device LUT digests equal the host's). */

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static float floor_f(float v)   /* |v| < 2^31 */
{
    const float t = (float)(int32_t)v;
    return t > v ? t - 1 : t;
}

/* atan(t), |t| <= 1: Cephes atanf's reduction (tan pi/8) and polynomial, error about 1 ulp. */
static float atan_unit(float t)
{
    float base = 0;
    if (t > 0.414213562f) {
        base = PI_F / 4;
        t = (t - 1) / (t + 1);
    }
    const float z = t * t;
    return base + ((((8.05374449538e-2f * z - 1.38776856032e-1f) * z + 1.99777106478e-1f) * z - 3.33329491539e-1f) * z * t + t);
}

/* atan2(y, x) in [-pi, pi], as libm's for finite y, x (atan2(0, 0) = 0). */
static float atan2_f(float y, float x)
{
    const float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax == 0 && ay == 0) return x < 0 || (x == 0 && f2u(x) >> 31) ? (y < 0 ? -PI_F : PI_F) : y;
    float a = ay <= ax ? atan_unit(ay / ax) : PI_F / 2 - atan_unit(ax / ay);
    if (x < 0) a = PI_F - a;
    return y < 0 ? -a : a;
}

/* log2(x), x > 0 normal: x = m 2^e with m in [sqrt(1/2), sqrt(2)), ln m = 2 atanh(s), s = (m-1)/(m+1), |s| <= 0.172. */
static float log2_f(float x)
{
    uint32_t b = f2u(x);
    int e = (int)(b >> 23) - 127;
    b = (b & 0x7fffffu) | 0x3f800000u;   /* m in [1, 2) */
    float m = u2f(b);
    if (m > 1.41421356f) {
        m *= 0.5f;
        e++;
    }
    const float s = (m - 1) / (m + 1), s2 = s * s;
    const float ln = 2 * s * (1 + s2 * (1.0f / 3 + s2 * (1.0f / 5 + s2 * (1.0f / 7 + s2 * (1.0f / 9)))));
    return (float)e + ln * 1.44269504f;
}

/* 2^y, y in [-126, 0]: 2^n by the exponent bits, 2^f = sqrt2 e^((f - 1/2) ln 2) by its series to the 7th power. */
static float exp2_f(float y)
{
    if (y < -126) return 0;
    int n = (int)y;   /* towards zero */
    if ((float)n > y) n--;
    const float g = (y - (float)n - 0.5f) * 0.693147181f;
    const float p = 1 + g * (1 + g * (0.5f + g * (1.0f / 6 + g * (1.0f / 24 + g * (1.0f / 120 + g * (1.0f / 720 + g * (1.0f / 5040)))))));
    return p * 1.41421356f * u2f((uint32_t)(n + 127) << 23);
}

/* x^p, x in [0, 1], p > 0 */
static float pow_f(float x, float p) { return x <= 0 ? 0 : x >= 1 ? 1 : exp2_f(p * log2_f(x)); }

/* sin x, |x| <= pi/2: its Taylor series to x^13 (error < 1e-9). */
static float sin_half(float x)
{
    const float z = x * x;
    return x * (1 - z * (1.0f / 6) * (1 - z * (1.0f / 20) * (1 - z * (1.0f / 42) * (1 - z * (1.0f / 72) *
                (1 - z * (1.0f / 110) * (1 - z * (1.0f / 156)))))));
}

/* ---- Rows */

/* Covered pixels of globe row py: first column and count (0 if none). A pixel is covered unless
 * r > 1 + 1/R, i.e. (2px+1-D)^2 + (2py+1-D)^2 > (D+2)^2 in integers (D is even: the left side is 2 mod 4, the right
 * 0 mod 4, so they are never equal and this is the double test exactly). */
static int row_extent(int D, int py, int *x0)
{
    const int32_t dy = 2 * py + 1 - D, lim = (int32_t)(D + 2) * (D + 2) - dy * dy;
    if (lim < 1) {
        *x0 = -1;
        return 0;
    }
    int32_t h = (int32_t)sqrtf((float)lim);   /* then corrected to the largest odd |dx| with dx^2 <= lim */
    while (h * h > lim) h--;
    while ((h + 1) * (h + 1) <= lim) h++;
    if (!(h & 1)) h--;
    if (h < 1) {
        *x0 = -1;
        return 0;
    }
    /* 2px+1-D = -h .. h: px = (D-1-h)/2 .. (D-1+h)/2 */
    const int first = (D - 1 - h) / 2, last = (D - 1 + h) / 2;
    *x0 = first < 0 ? 0 : first;
    return (last > D - 1 ? D - 1 : last) - *x0 + 1;
}

/* Spans and plane entries (each span padded to a multiple of 8): integer only, so microseconds. */
static void count(const omni_globe_cfg_t *c, unsigned *nspans, uint32_t *n)
{
    *nspans = 0;
    *n = 0;
    for (int py = 0; py < c->diam; py++) {
        int x0, k = row_extent(c->diam, py, &x0);
        if (!k) continue;
        (*nspans)++;
        *n += ((uint32_t)k + 7u) & ~7u;
    }
}

static size_t layout_bytes(unsigned nspans, uint32_t n)
{
    return ALIGN16(nspans * sizeof(omni_globe_span_t)) + 5 * ALIGN16((size_t)n * 2) + 3 * ALIGN16(n);
}

size_t omni_globe_lut_bytes(const omni_globe_cfg_t *cfg)
{
    if (!cfg_valid(cfg)) return 0;
    unsigned nspans;
    uint32_t n;
    count(cfg, &nspans, &n);
    return 15 + layout_bytes(nspans, n);   /* 15: aligning an arbitrary mem to 16 */
}

/* round half away from zero, as lround */
static int32_t roundf_i(float v) { return v < 0 ? -(int32_t)(-v + 0.5f) : (int32_t)(v + 0.5f); }
static uint8_t x255(float v) { return (uint8_t)(clampf(v, 0, 1) * 255.0f + 0.5f); }
static int16_t q14(float v) { return (int16_t)roundf_i(clampf(v, -1, 1) * 16384.0f); }
static int16_t q14d(double v) { return (int16_t)lround((v < -1 ? -1 : v > 1 ? 1 : v) * 16384.0); }   /* the sun */

bool omni_globe_lut_begin(omni_globe_lut_t *lut, const omni_globe_cfg_t *cfg, void *mem, size_t bytes)
{
    if (!mem || !cfg_valid(cfg)) return false;
    unsigned nspans;
    uint32_t n;
    count(cfg, &nspans, &n);
    if (bytes < 15 + layout_bytes(nspans, n)) return false;   /* omni_globe_lut_bytes(cfg) */

    uint8_t *p = (uint8_t *)mem + ((16 - ((uintptr_t)mem & 15)) & 15);
    lut->cfg = *cfg;
    lut->nspans = (uint16_t)nspans;
    lut->n = n;
    lut->span = (omni_globe_span_t *)p;   p += ALIGN16(nspans * sizeof(omni_globe_span_t));
    lut->u = (int16_t *)p;                p += ALIGN16((size_t)n * 2);
    lut->row = (uint16_t *)p;             p += ALIGN16((size_t)n * 2);
    lut->a = (int16_t *)p;                p += ALIGN16((size_t)n * 2);
    lut->b = (int16_t *)p;                p += ALIGN16((size_t)n * 2);
    lut->c = (int16_t *)p;                p += ALIGN16((size_t)n * 2);
    lut->limb = p;                        p += ALIGN16(n);
    lut->rim = p;                         p += ALIGN16(n);
    lut->alpha = p;

    const int D = cfg->diam, box = (cfg->size - cfg->diam) / 2;
    memset(lut->span, 0, ALIGN16(nspans * sizeof(omni_globe_span_t)));   /* with the struct's padding bytes */
    unsigned si = 0;
    uint32_t off = 0;
    for (int py = 0; py < D; py++) {
        int x0, cnt = row_extent(D, py, &x0);
        if (!cnt) continue;
        omni_globe_span_t *s = &lut->span[si++];
        s->y = (int16_t)(py + box);
        s->x0 = (int16_t)(x0 + box);
        s->n = (int16_t)cnt;
        s->off = off;
        off += ((uint32_t)cnt + 7u) & ~7u;
    }
    /* zero only what omni_globe_lut_rows does not write (a full memset of the ~1 MB cost 60-100 ms on the device):
     * each span's padding entries (alpha 0) and each plane's alignment tail; b and c whole
     * for a studio LUT (rows write a only) */
    uint8_t *const planes[8] = { (uint8_t *)lut->u, (uint8_t *)lut->row, (uint8_t *)lut->a, (uint8_t *)lut->b,
                                 (uint8_t *)lut->c, lut->limb, lut->rim, lut->alpha };
    for (int pl = 0; pl < 8; pl++) {
        const size_t es = pl < 5 ? 2 : 1, sz = ALIGN16((size_t)n * es);
        if (cfg->studio && (pl == 3 || pl == 4)) {
            memset(planes[pl], 0, sz);
            continue;
        }
        for (unsigned i = 0; i < nspans; i++) {
            const uint32_t a = lut->span[i].off + (uint32_t)lut->span[i].n, b = i + 1 < nspans ? lut->span[i + 1].off : n;
            memset(planes[pl] + a * es, 0, (b - a) * es);
        }
        memset(planes[pl] + n * es, 0, sz - n * es);
    }
    return true;
}

void omni_globe_lut_rows(omni_globe_lut_t *lut, int py0, int py1)
{
    const omni_globe_cfg_t *cfg = &lut->cfg;
    const int D = cfg->diam, box = (cfg->size - cfg->diam) / 2, tex_h = cfg->tex_w / 2;
    const float R = (float)D / 2, inv_r = 1 / R, edge = 1 - 0.5f / R, edge2 = edge * edge;
    /* per-call constants in double (a few libm calls), rounded once */
    const double lat0 = cfg->lat0_deg * D2R, roll = cfg->roll_deg * D2R;
    const float cl = (float)cos(lat0), sl = (float)sin(lat0), cr = (float)cos(roll), sr = (float)sin(roll);
    const float u_scale = (float)(cfg->tex_w / (2 * PI) * 16.0);   /* Q4 texels per radian */
    const bool polar = cfg->proj == OMNI_PROJ_POLAR, studio = cfg->studio;
    const float max_colat = (float)max_colat_rad(cfg);
    const float L0 = cfg->light[0], L1 = cfg->light[1], L2 = cfg->light[2];

    /* A span is symmetric about the disc's vertical axis (x -> D-1-x negates xs exactly), so the radial planes
     * (alpha, limb, rim; z) of entry j equal those of n-1-j; and for an unrolled, unlit ortho globe the mirror's
     * xv is -xv with Y and Zf unchanged, so its u and c are exactly negated and row, a, b equal (atan2_f, roundf_i
     * and q14 are odd). Computing half and mirroring gives the bytes of the full computation. */
    const bool mirror_all = !polar && !studio && cfg->roll_deg == 0;
    for (unsigned si = 0; si < lut->nspans; si++) {
        const omni_globe_span_t *s = &lut->span[si];
        const int py = s->y - box, x0 = s->x0 - box, cnt = s->n;
        if (py < py0 || py >= py1) continue;
        const uint32_t off = s->off;
        const float ys0 = -((float)py + 0.5f - R) * inv_r;
        for (int j = 0; j < cnt; j++) {
            const uint32_t k = off + (uint32_t)j, km = off + (uint32_t)(cnt - 1 - j);
            const bool second = j > cnt - 1 - j;   /* the mirror (already done) is km */
            if (second && mirror_all) {
                lut->alpha[k] = lut->alpha[km];
                lut->limb[k] = lut->limb[km];
                lut->rim[k] = lut->rim[km];
                lut->u[k] = (int16_t)-lut->u[km];
                lut->row[k] = lut->row[km];
                lut->a[k] = lut->a[km];
                lut->b[k] = lut->b[km];
                lut->c[k] = (int16_t)-lut->c[km];
                continue;
            }
            float xs = ((float)(x0 + j) + 0.5f - R) * inv_r, ys = ys0;
            float r2 = xs * xs + ys * ys, r = 0;
            /* coverage (1 - r) R + 1/2 saturates at 1 inside r <= 1 - 1/(2R): r itself only near the edge */
            if (r2 > edge2 || polar) r = sqrtf(r2);
            if (!second) lut->alpha[k] = r2 <= edge2 ? 255 : x255((1 - r) * R + 0.5f);
            else lut->alpha[k] = lut->alpha[km];
            if (r > 0.9995f) {
                xs *= 0.9995f / r;
                ys *= 0.9995f / r;
                r = 0.9995f;
                r2 = r * r;
            }
            float lat, lon_rel;
            if (polar) {
                const float t = r * max_colat;   /* colatitude, 0 .. max_colat (< pi) */
                lat = PI_F / 2 - t;
                lon_rel = -(atan2_f(-ys, xs) + PI_F / 2);   /* screen angle -> longitude offset */
                if (lon_rel < -PI_F) lon_rel += 2 * PI_F;    /* to (-pi, pi]: same texel, and u stays in int16 */
                const float rr = r * r, r6 = rr * rr * rr;
                lut->limb[k] = x255(1 - 0.28f * r6);
                lut->rim[k] = 0;
                /* sin lat = cos t, cos lat = sin t; cos lonRel = ys / r, sin lonRel = -xs / r */
                const float st = t <= PI_F / 2 ? sin_half(t) : sin_half(PI_F - t), ct = sin_half(PI_F / 2 - t);
                lut->a[k] = q14(ct);
                lut->b[k] = q14(r > 0 ? st * ys / r : 0);
                lut->c[k] = q14(r > 0 ? -st * xs / r : -st);
            } else {
                const float xv = xs * cr + ys * sr, yv = -xs * sr + ys * cr;
                const float zz = 1 - r2, z = zz > 0 ? sqrtf(zz) : 0;   /* the roll keeps xv^2 + yv^2 = r^2 */
                const float Y = yv * cl + z * sl, Zf = -yv * sl + z * cl;
                lat = atan2_f(Y, sqrtf(xv * xv + Zf * Zf));
                lon_rel = atan2_f(xv, Zf);
                if (!second) {
                    if (cfg->limb_kind == 1) {   /* the Omni's: 0.62 + 0.38 z^0.5, no rim */
                        lut->limb[k] = x255(0.62f + 0.38f * sqrtf(z));
                        lut->rim[k] = 0;
                    } else {
                        lut->limb[k] = x255(0.42f + 0.58f * pow_f(z, 0.55f));
                        /* (1-z)^2.6 < 0.09^2.6 = 0.0019 < 0.5/255 rounds to 0 */
                        lut->rim[k] = 1 - z < 0.09f ? 0 : x255(pow_f(1 - z, 2.6f));
                    }
                } else {
                    lut->limb[k] = lut->limb[km];
                    lut->rim[k] = lut->rim[km];
                }
                if (studio) {
                    lut->a[k] = q14(xv * L0 + yv * L1 + z * L2);   /* the mockup's ndl, rolled frame */
                } else {
                    lut->a[k] = q14(Y);
                    lut->b[k] = q14(Zf);
                    lut->c[k] = q14(xv);
                }
            }
            lut->u[k] = (int16_t)roundf_i(lon_rel * u_scale);
            const float rowf = floor_f((0.5f - lat * (1 / PI_F)) * (float)tex_h);
            lut->row[k] = (uint16_t)clampf(rowf, 0, (float)(tex_h - 1));
        }
    }
}

bool omni_globe_lut_build(omni_globe_lut_t *lut, const omni_globe_cfg_t *cfg, void *mem, size_t bytes)
{
    if (!omni_globe_lut_begin(lut, cfg, mem, bytes)) return false;
    omni_globe_lut_rows(lut, 0, cfg->diam);
    return true;
}

void omni_globe_project(const omni_globe_cfg_t *cfg, double lat, double lon, double lon0, float out[3])
{
    if (cfg->proj == OMNI_PROJ_POLAR) {
        const double r = (90 - lat) * D2R / max_colat_rad(cfg), lon_rel = (lon - lon0) * D2R;
        const double a = -lon_rel - PI / 2;   /* inverse of the LUT mapping */
        out[0] = (float)(cos(a) * r);
        out[1] = (float)(sin(a) * r);
        out[2] = r <= 1 ? 1.0f : -1.0f;
        return;
    }
    const double lat0 = cfg->lat0_deg * D2R, roll = cfg->roll_deg * D2R, p = lat * D2R, dl = (lon - lon0) * D2R;
    const double X = cos(p) * sin(dl), Y = sin(p), Zf = cos(p) * cos(dl);
    const double yv = Y * cos(lat0) - Zf * sin(lat0), z = Y * sin(lat0) + Zf * cos(lat0);
    const double xs = X * cos(roll) - yv * sin(roll), ys = X * sin(roll) + yv * cos(roll);
    out[0] = (float)xs;
    out[1] = (float)-ys;
    out[2] = (float)z;
}

void omni_globe_sun(omni_globe_sun_t *o, double decl_deg, double sun_lon_deg, double lon0_deg)
{
    const double d = decl_deg * D2R, dl = (lon0_deg - sun_lon_deg) * D2R;
    o->sd = q14d(sin(d));
    o->k1 = q14d(cos(d) * cos(dl));
    o->k2 = q14d(cos(d) * sin(dl));
}

void omni_globe_sun_studio(omni_globe_sun_t *o)
{
    o->sd = 1 << 14;
    o->k1 = 0;
    o->k2 = 0;
}

bool omni_globe_sun_now(omni_globe_sun_t *o, const omni_sun_t *sun, double lat0_deg, double lon0_deg)
{
    if (!sun) {
        omni_globe_sun(o, lat0_deg, lon0_deg, lon0_deg);
        return false;
    }
    omni_globe_sun(o, sun->decl_deg, sun->subsolar_lon, lon0_deg);
    return true;
}
