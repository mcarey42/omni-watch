/* Bay Mouth (`bay`, rd/faces.h; rd wave 2 Task 3, spec section 10.2; faces-reveal.js bayBase(), hazardRing() and
 * bayFace() settled, sleep_airlock() the glance).
 *
 * Three pieces come from bind, none from a draw's own arithmetic:
 *   the upper stars (the mockup's stars(301, 260) recipe through its rng, none within SR + 6 of the giant), drawn over
 *   black above the band (rows 0-265) with face_kit's polygons and kept as their lit pixels (about 1 500);
 *   the star band, rd_by_star (RGB565 466 x 200), read in place and copied down by the clock's shift (rows it uncovers
 *   black);
 *   one annulus (pixel centres at 206-237 px from the centre) holding, per pixel, the lip's coverage (r 212, 9 px), its
 *   steel line's (r 207.5, 1 px), the hazard ring's (r 217-236: fk_ring_fill's rule, clamp(r1 + .5 - d) - clamp(r0 +
 *   .5 - d), with d by an integer square root) and the ring strip's row and column (a polar map with the annulus.c
 *   rule: the row the largest h with (217 + h)^2 <= r^2 by exact squares, the column floor(theta 1420 / 2 pi) by
 *   omni_atan2_tab, clockwise from 12; fk_annulus_draw_a8 itself needs a power-of-two n_theta, and the strip is 1 420
 *   columns, one a pixel at r 226).
 * A frame lays, inside its clip: the base rows (black, the stars, the band), the annulus (the lip in #0B0C0D, the steel
 * line in steelHi at .4, the ring's hazardDark at .72 of its coverage, the strip's yellow in hazard at its byte, which
 * carries the mockup's .72; under the chime the ring's additive hazard), the 48 rivets (rivet(): a steel disc r 1.6, a
 * steelHi pixel up-left, a black one down-right), the chime's limb glow, the pip, then the text.
 * Pure: no esp_*, freertos or LVGL; no libm (lesson 6: angles through fk_sincos and omni_atan2_tab, distances by
 * integer square roots); no allocation. */
#include <string.h>
#include "rd/faces.h"
#include "rd/airlock.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "fk/compose.h"
#include "fk/ease.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"
#include "omni/trig.h"

#define W 466
#define C 233
#define TAU_F 6.28318530718f
#define TWO_PI_F 6.28318530717958647692f   /* annulus.c's, for the column's scale */
#define BAND_Y 266         /* the star band's first row (rd_by_star) */
#define BAND_H 200
#define SY 568             /* the giant's centre (233, SY) and radius: the stars keep SR + 6 off it */
#define SR 272
#define AN_R0 206          /* the annulus every lip, steel line and ring pixel lies in (their edges included) */
#define AN_R1 237
#define NPX_MAX 45000      /* its pixels (about 43 100; bind refuses more) */
#define RING_N 1420        /* the strip's columns (one a pixel at r 226) and rows (r 217 + i) */
#define RING_R0 217
#define RING_ROWS 19
#define STAR_MAX 4096      /* the upper stars' lit pixels (bind refuses more) */
#define BAND 8             /* the bind's scratch rows */
#define N_RIVETS 48
#define Y_LABEL 98
#define Y_DATE 130
#define Y_TIME 252
#define Y_CAPTION 288
#define PIP_R 200.f
#define TRACK13 (13.f * 0.14f)   /* lab()'s letterSpacing */

const omni_rect_t RD_BY_STAR = {0, BAND_Y, W, W};
const omni_rect_t RD_BY_RING = {0, 0, W, W};
const omni_rect_t RD_BY_PIP = {-20, -20, 21, 21};
const omni_rect_t RD_BY_TIME = {34, 156, 432, 262};
const omni_rect_t RD_BY_DATE = {100, 100, 366, 143};
const omni_rect_t RD_BY_CAPTION = {60, 273, 406, 296};
const omni_rect_t RD_BY_STALE = {130, 82, 336, 104};
static const omni_rect_t GLOW_BOX = {0, 470 - 255, W, W};   /* the chime's limb glow r 255 about (233, 470) */
static const omni_rect_t PIP_TRACK = {C - 200 - 20, C - 200 - 20, C + 200 + 21, C + 200 + 21};
/* R173: the ring as the four rim bands of the screen, RIM px deep: a point of the ring (r >= 216.5 - its AA) lies within
 * 233 - 216 cos(pi / 4) = 80.3 px of an edge, so 82 holds it with a pixel to spare (test_rd_by_ring_crawl checks) */
#define RIM 82
static const omni_rect_t RING_BANDS[4] = {
    {0, 0, W, RIM}, {0, W - RIM, W, W}, {0, RIM, RIM, W - RIM}, {W - RIM, RIM, W, W - RIM}};

typedef struct { int16_t ox0, ix0, ix1, ox1; uint32_t off; } span_t;   /* row y: [ox0, ix0) then [ix1, ox1) */

typedef struct {
    fk_img_t s132_img, s132o_img, ch13_img, star, ring;
    fk_atlas_t s132, s132o, ch13;
    rd_atlas2_t vt28;   /* used where filled, never copied */
    const uint8_t *g18, *g255;
    float rvx[N_RIVETS], rvy[N_RIVETS];
    span_t span[W];
    uint32_t npx, nstar;
    uint16_t col[NPX_MAX];                              /* the strip column of each annulus pixel (0..1419) */
    uint8_t row[NPX_MAX];                               /* its strip row (0..18), 255 outside r 217-236 */
    uint8_t lip[NPX_MAX], steel[NPX_MAX], hz[NPX_MAX];  /* the coverages (0..255) */
    uint32_t star_at[STAR_MAX];                         /* the upper stars' pixels (y W + x, rising) and values */
    uint16_t star_px[STAR_MAX];
    uint16_t band[W * BAND];                            /* the bind's scratch */
} by_t;

/* ---- small helpers */
static int iround(float v) { return v >= 0 ? (int)(v + 0.5f) : -(int)(-v + 0.5f); }

static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}

static uint32_t isqrt64(uint64_t v)   /* floor(sqrt(v)) */
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) v -= r + bit, r = (r >> 1) + bit;
        else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

/* a pixel's coverage by the annulus [r0, r1) (fk_ring_fill's rule), radii and distance in 1/256 px; 0..255 */
static uint8_t ring_cov(uint32_t d256, int32_t r0_256, int32_t r1_256)
{
    int32_t co = r1_256 + 128 - (int32_t)d256, ci = r0_256 + 128 - (int32_t)d256;
    co = co < 0 ? 0 : co > 256 ? 256 : co;
    ci = ci < 0 ? 0 : ci > 256 ? 256 : ci;
    const int32_t cov = co - ci;
    return (uint8_t)(cov <= 0 ? 0 : cov >= 256 ? 255 : (cov * 255 + 128) / 256);
}

static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

/* rgb at a8 added to d per channel, saturating ('lighter') */
static uint16_t add565(uint16_t d, uint16_t rgb, unsigned a8)
{
    int r = omni_ex_r(d) + (omni_ex_r(rgb) * (int)a8 + 127) / 255;
    int g = omni_ex_g(d) + (omni_ex_g(rgb) * (int)a8 + 127) / 255;
    int b = omni_ex_b(d) + (omni_ex_b(rgb) * (int)a8 + 127) / 255;
    return omni_pack565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

/* the mockup's rng (mulberry32), [0, 1) in 24 bits */
static float rng_next(uint32_t *seed)
{
    *seed += 0x6D2B79F5u;
    uint32_t t = (*seed ^ (*seed >> 15)) * (1u | *seed);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) / 16777216.0f;
}

static void rect_fill(uint16_t *fb, int stride, const omni_rect_t *clip, float x, float y, float w, float h, uint16_t rgb,
                      uint8_t a)
{
    const omni_pt_t q[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    omni_fill_poly(fb, stride, clip, q, 4, rgb, a);
}

/* stars(301, 260, the giant's avoid) (faces-reveal.js:214-229): each star a w x w square at alpha .18 + .7 b, and the
 * brightest (b > .9) a faint cross at .12 */
static void stars_draw(uint16_t *fb, int stride, const omni_rect_t *clip)
{
    uint32_t seed = 301;
    for (int i = 0; i < 260; i++) {
        const float x = rng_next(&seed) * (float)W, y = rng_next(&seed) * (float)W;
        const float dx = x - (float)C, dy = y - (float)SY;
        if (dx * dx + dy * dy < (float)(SR + 6) * (float)(SR + 6)) continue;
        float b = rng_next(&seed);
        b = b * b * b;
        const float warm = rng_next(&seed);
        const uint16_t col = warm < 0.3f    ? RD_RGB(255, 210, 170)
                             : warm < 0.85f ? RD_RGB(235, 228, 215)
                                            : RD_RGB(255, 160, 120);
        const float s = b > 0.85f ? 1.6f : b > 0.5f ? 1.1f : 0.8f;
        rect_fill(fb, stride, clip, x, y, s, s, col, a8_of(0.18f + b * 0.7f));
        if (b > 0.9f) {
            rect_fill(fb, stride, clip, x - 2.f, y + s / 2.f - 0.3f, 4.f + s, 0.6f, col, a8_of(0.12f));
            rect_fill(fb, stride, clip, x + s / 2.f - 0.3f, y - 2.f, 0.6f, 4.f + s, col, a8_of(0.12f));
        }
    }
}

/* row y's annulus spans: pixel centres with AN_R0 <= d <= AN_R1, in doubled integer coordinates */
static void row_span(int y, span_t *sp)
{
    const int64_t dy = 2 * y + 1 - W, r0 = 2 * AN_R0, r1 = 2 * AN_R1;
    sp->ox0 = sp->ix0 = sp->ix1 = sp->ox1 = 0;
    int first = -1, last = -1, h0 = -1, h1 = -1;
    for (int x = 0; x < W; x++) {
        const int64_t dx = 2 * x + 1 - W, d2 = dx * dx + dy * dy;
        if (d2 <= r1 * r1) {
            if (first < 0) first = x;
            last = x;
        }
        if (d2 < r0 * r0) {
            if (h0 < 0) h0 = x;
            h1 = x;
        }
    }
    if (first < 0) return;
    sp->ox0 = (int16_t)first, sp->ox1 = (int16_t)(last + 1);
    if (h0 < 0) sp->ix0 = sp->ix1 = sp->ox1;   /* no hole: one span */
    else sp->ix0 = (int16_t)h0, sp->ix1 = (int16_t)(h1 + 1);
}

static bool bind(void *ctx, fk_asset_fn get)
{
    by_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_F_S132, &n);
    bool ok = b && fk_img_from_bin(&k->s132_img, b, n) && k->s132_img.a;
    b = get(ASSET_RD_F_S132O, &n);
    ok = ok && b && fk_img_from_bin(&k->s132o_img, b, n) && k->s132o_img.a;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    b = get(ASSET_RD_F_VT28, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt28);
    b = get(ASSET_RD_BY_STAR, &n);
    ok = ok && b && fk_img_from_bin(&k->star, b, n) && k->star.rgb && k->star.w == W && k->star.h == BAND_H;
    b = get(ASSET_RD_BY_RING, &n);
    ok = ok && b && fk_img_from_bin(&k->ring, b, n) && k->ring.a && k->ring.w == RING_N && k->ring.h == RING_ROWS;
    if (!ok) return false;
    k->s132 = (fk_atlas_t){&k->s132_img, RD_CELLS_RD_F_S132, RD_GLYPHS_RD_F_S132, ASSET_RD_F_S132_LINE_H};
    k->s132o = (fk_atlas_t){&k->s132o_img, RD_CELLS_RD_F_S132O, RD_GLYPHS_RD_F_S132O, ASSET_RD_F_S132O_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->g18 = rd_glow_lut(18), k->g255 = rd_glow_lut(255);
    if (!k->g18 || !k->g255) return false;
    for (int i = 0; i < N_RIVETS; i++) {
        float s, c;
        fk_sincos((float)i / (float)N_RIVETS * TAU_F, &s, &c);
        k->rvx[i] = (float)C + 212.f * s, k->rvy[i] = (float)C - 212.f * c;
    }
    /* the annulus: its spans, then per pixel its coverages and its strip cell */
    uint32_t off = 0;
    for (int y = 0; y < W; y++) {
        row_span(y, &k->span[y]);
        k->span[y].off = off;
        off += (uint32_t)(k->span[y].ix0 - k->span[y].ox0) + (uint32_t)(k->span[y].ox1 - k->span[y].ix1);
    }
    if (off > NPX_MAX) return false;
    k->npx = off;
    const float kth = (float)RING_N / TWO_PI_F;
    for (int y = 0; y < W; y++) {
        const span_t *sp = &k->span[y];
        uint32_t o = sp->off;
        const int64_t dy = 2 * y + 1 - W;
        for (int x = sp->ox0; x < sp->ox1; x++) {
            if (x >= sp->ix0 && x < sp->ix1) continue;
            const int64_t dx = 2 * x + 1 - W, q = dx * dx + dy * dy;   /* (2 r)^2, exactly */
            const uint32_t d256 = isqrt64((uint64_t)q * 16384u);        /* r in 1/256 px */
            k->lip[o] = ring_cov(d256, (int32_t)(207.5f * 256.f), (int32_t)(216.5f * 256.f));
            k->steel[o] = ring_cov(d256, 207 * 256, 208 * 256);
            k->hz[o] = ring_cov(d256, RING_R0 * 256, (RING_R0 + RING_ROWS) * 256);
            uint8_t ri = 255;
            if (q >= 4LL * RING_R0 * RING_R0 && q < 4LL * (RING_R0 + RING_ROWS) * (RING_R0 + RING_ROWS)) {
                int h = 0;
                while (h + 1 < RING_ROWS && 4LL * (RING_R0 + h + 1) * (RING_R0 + h + 1) <= q) h++;
                ri = (uint8_t)h;
            }
            k->row[o] = ri;
            float t = omni_atan2_tab((float)dx * 0.5f, (float)(-dy) * 0.5f);   /* clockwise from 12 (Y up) */
            if (t < 0) t += TWO_PI_F;
            int ti = (int)(t * kth);
            k->col[o] = (uint16_t)(ti > RING_N - 1 ? RING_N - 1 : ti);
            o++;
        }
    }
    /* the upper stars, BAND rows at a time over black (band-exact, so the bands are the whole frame's): their lit
     * pixels above the star band */
    k->nstar = 0;
    for (int y0 = 0; y0 < BAND_Y; y0 += BAND) {
        const int y1 = y0 + BAND < BAND_Y ? y0 + BAND : BAND_Y;
        const omni_rect_t clip = {0, (int16_t)y0, W, (int16_t)y1};
        memset(k->band, 0, sizeof k->band);
        stars_draw(k->band, W, &clip);
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < W; x++) {
                const uint16_t p = k->band[(y - y0) * W + x];
                if (!p) continue;
                if (k->nstar >= STAR_MAX) return false;
                k->star_at[k->nstar] = (uint32_t)(y * W + x), k->star_px[k->nstar] = p;
                k->nstar++;
            }
    }
    return true;
}

/* ---- the clock's readings */
int rd_by_star_dy(const fk_clock_t *c)
{
    if (!c->set) return 0;
    const long sod = (c->h * 60L + c->m) * 60L + c->s;
    const int k = (int)((sod / 120) % 6);
    return k <= 3 ? k : 6 - k;
}

uint16_t rd_by_ring_col(const fk_clock_t *c)
{
    if (!c->set) return 0;
    return (uint16_t)(((c->h * 60L + c->m) * 60L + c->s) % RING_N);
}

float rd_by_pip_turn(const fk_clock_t *c)
{
    return ((float)(c->s % 60) + fk_ease_out_cubic(fk_ease_clampf((float)c->ms / 250.f, 0.f, 1.f))) / 60.f;
}

static void pip_centre(float turn, float *x, float *y)
{
    float s, c;
    fk_sincos(turn * TAU_F, &s, &c);
    *x = (float)C + PIP_R * s, *y = (float)C - PIP_R * c;
}

void rd_by_pip_rect(float turn, omni_rect_t *out)
{
    float x, y;
    pip_centre(turn, &x, &y);
    const int cx = iround(x), cy = iround(y);
    *out = (omni_rect_t){(int16_t)(cx + RD_BY_PIP.x0), (int16_t)(cy + RD_BY_PIP.y0), (int16_t)(cx + RD_BY_PIP.x1),
                         (int16_t)(cy + RD_BY_PIP.y1)};
}

unsigned rd_by_ring_rects(omni_rect_t out[4])
{
    for (int i = 0; i < 4; i++) out[i] = RING_BANDS[i];
    return 4;
}

unsigned rd_by_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_BY_RING, out[1] = GLOW_BOX, out[2] = PIP_TRACK;
    return 3;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, chime, pip, settled;
    int dy;
    uint16_t col;
    float turn;
    uint8_t pip_a, pip_g;   /* the pip's disc and glow alphas */
    uint8_t add_a, glow_a;  /* the chime's ring add and limb glow */
    float settle, drop;     /* the airlock's interior (Task 4): 1 settled, reduced or in the glance */
} by_state_t;

static bool chime_active(const fk_face_in_t *in) { return in->thrum > 0 || in->beep > 0 || in->click > 0; }

static void state_of(const fk_face_in_t *in, by_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    const bool pic = !in->reduced && !in->glance;
    s->chime = pic && in->thrum > 0;
    const float thrum = s->chime ? (in->thrum > 1 ? 1.f : in->thrum) : 0.f;
    s->add_a = a8_of(0.35f * thrum), s->glow_a = a8_of(0.25f * thrum);
    s->settle = s->drop = 1.f;
    if (in->wake_ms != UINT32_MAX && pic) {
        const float t = (float)in->wake_ms;
        s->settle = fk_ease_out_cubic(fk_ease_ph(t, 1200.f, 1950.f));
        s->drop = fk_ease_ph(t, 1230.f, 1700.f);
    }
    s->settled = s->settle >= 1.f && s->drop >= 1.f;
    s->dy = rd_by_star_dy(&in->clk);
    /* R171: under reduced motion the crawl steps once a minute, with the time (a column a minute) */
    s->col = !s->set ? 0 : in->reduced ? (uint16_t)((in->clk.h * 60 + in->clk.m) % RING_N) : rd_by_ring_col(&in->clk);
    if (s->set) {
        s->turn = in->reduced ? (float)(in->clk.s % 60 + 1) / 60.f : rd_by_pip_turn(&in->clk);
        const bool act = pic && chime_active(in);
        const float beep = act ? (in->beep > 1 ? 1.f : in->beep < 0 ? 0.f : in->beep) : 0.f;
        float k = s->settle * (act ? 0.4f + 0.6f * beep + 0.3f : 1.f);
        k = k > 1.f ? 1.f : k;
        s->pip_a = a8_of(k), s->pip_g = a8_of(0.55f * k);
        s->pip = s->pip_a || s->pip_g;
    }
}

/* ---- drawing */
/* the base rows inside clip: black above the band and in the rows its shift uncovers, the band's rows below, and the
 * upper stars */
static void base_rows(const by_t *k, uint16_t *fb, int stride, omni_rect_t clip, int dy)
{
    const size_t w = (size_t)(clip.x1 - clip.x0);
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        const int sy = y - BAND_Y - dy;
        if (y >= BAND_Y && sy >= 0) memcpy(d, k->star.rgb + (size_t)sy * W + clip.x0, w * sizeof d[0]);
        else memset(d, 0, w * sizeof d[0]);
    }
    if (clip.y0 >= BAND_Y) return;
    const uint32_t lo = (uint32_t)clip.y0 * W, hi = (uint32_t)(clip.y1 < BAND_Y ? clip.y1 : BAND_Y) * W;
    uint32_t a = 0, b = k->nstar;   /* the first star pixel at or after lo */
    while (a < b) {
        const uint32_t m = (a + b) / 2;
        if (k->star_at[m] < lo) a = m + 1;
        else b = m;
    }
    for (uint32_t i = a; i < k->nstar && k->star_at[i] < hi; i++) {
        const int x = (int)(k->star_at[i] % W), y = (int)(k->star_at[i] / W);
        if (x >= clip.x0 && x < clip.x1) fb[(size_t)(y - clip.y0) * (size_t)stride + (size_t)(x - clip.x0)] = k->star_px[i];
    }
}

/* the annulus inside clip: with lip, the lip and its steel line first; the ring's hazardDark at .72 of its coverage
 * and the strip's yellow at column offset col; add_a, the chime's additive hazard */
static void annulus(const by_t *k, uint16_t *fb, int stride, omni_rect_t clip, uint16_t col, bool lip, uint8_t add_a)
{
    const uint8_t *strip = k->ring.a;
    col %= RING_N;
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        const span_t *sp = &k->span[y];
        const int seg[2][2] = {{sp->ox0, sp->ix0}, {sp->ix1, sp->ox1}};
        uint32_t o = sp->off;
        for (int g = 0; g < 2; g++) {
            const int a = seg[g][0] > clip.x0 ? seg[g][0] : clip.x0, b = seg[g][1] < clip.x1 ? seg[g][1] : clip.x1;
            for (int x = a; x < b; x++) {
                const uint32_t e = o + (uint32_t)(x - seg[g][0]);
                uint16_t p = d[x];
                if (lip) {
                    if (k->lip[e]) p = omni_blend565(p, RD_RGB(0x0B, 0x0C, 0x0D), k->lip[e]);
                    if (k->steel[e]) p = omni_blend565(p, RD_C_STEEL_HI, (k->steel[e] * 102 + 127) / 255);
                }
                if (k->hz[e]) {
                    p = omni_blend565(p, RD_C_HAZARD_DARK, (k->hz[e] * 184 + 127) / 255);
                    if (k->row[e] != 255) {
                        int c = (int)k->col[e] - (int)col;
                        c += c < 0 ? RING_N : 0;
                        const unsigned v = strip[(size_t)k->row[e] * RING_N + (size_t)c];
                        if (v) p = omni_blend565(p, RD_C_HAZARD, (int)v);
                    }
                    if (add_a) p = add565(p, RD_C_HAZARD, (k->hz[e] * add_a + 127u) / 255u);
                }
                d[x] = p;
            }
            o += (uint32_t)(seg[g][1] - seg[g][0]);
        }
    }
}

void rd_by_ring_draw(const void *ctx, uint16_t col, omni_rect_t clip, uint16_t *fb, int stride)
{
    annulus(ctx, fb, stride, clip, col, false, 0);
}

static void rivets(const by_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int i = 0; i < N_RIVETS; i++) {
        const float x = k->rvx[i], y = k->rvy[i];
        const omni_rect_t box = {(int16_t)(x - 4.f), (int16_t)(y - 4.f), (int16_t)(x + 5.f), (int16_t)(y + 5.f)};
        if (!meets(box, clip)) continue;
        fk_disc(fb, stride, &clip, x, y, 1.6f, RD_C_STEEL, 255);
        rect_fill(fb, stride, &clip, x - 1.2f, y - 1.2f, 1.f, 1.f, RD_C_STEEL_HI, 255);
        rect_fill(fb, stride, &clip, x + 0.4f, y + 0.6f, 1.f, 1.f, 0x0000, 255);
    }
}

static void text_centred(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *s, int cx,
                         int y, uint16_t rgb, uint8_t opa)
{
    const int w = fk_atlas_width(at, s, 0);
    fk_atlas_text_rgb(fb, stride, clip, at, s, cx - w / 2, y, 0, rgb, opa, 0);
}

static uint8_t ease8(float settle, float a, float b) { return a8_of(fk_ease_out_cubic(fk_ease_ph(settle, a, b))); }

static float out_back(float s) { const float u = s - 1.f; return 1.f + 2.5f * u * u * u + 1.5f * u * u; }

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const by_t *k = ctx;
    by_state_t s;
    state_of(in, &s);
    base_rows(k, fb, stride, clip, s.dy);
    annulus(k, fb, stride, clip, s.col, true, s.add_a);
    rivets(k, fb, stride, clip);
    if (s.glow_a && meets(GLOW_BOX, clip))   /* the star's upper limb warms (glowAt (255, 120, 60), r 255: W2-D8) */
        fk_glow_add_lut(fb, stride, &clip, C, 470, 255, k->g255, RD_RGB(255, 120, 60), s.glow_a);
    if (s.pip) {   /* the beacon pip at r 200: its disc r 3.6, then its glow r 18 at .55 */
        omni_rect_t box;
        rd_by_pip_rect(s.turn, &box);
        if (meets(box, clip)) {
            float x, y;
            pip_centre(s.turn, &x, &y);
            if (s.pip_a) fk_disc(fb, stride, &clip, x, y, 3.6f, RD_C_AMBER, s.pip_a);
            if (s.pip_g) fk_glow_add_lut(fb, stride, &clip, iround(x), iround(y), 18, k->g18, RD_C_AMBER, s.pip_g);
        }
    }
    const uint8_t ha = s.settled ? 255 : ease8(s.settle, 0.15f, 1.f);
    if (meets(RD_BY_STALE, clip)) {
        if (s.stale) rd_stale_mark(fb, stride, &clip, &k->ch13, C, Y_LABEL, false);
        else if (ha) fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "BAY 03 \267 DOORS OPEN", C, Y_LABEL, TRACK13, true,
                                           RD_C_PAINT_DIM, ha);
    }
    if (meets(RD_BY_DATE, clip)) {
        char d[24];
        fk_date_str(&in->clk, d);
        strcat(d, fk_ampm(&in->clk, in->h24));
        const uint8_t da = s.settled ? (s.set ? 255 : 140) : a8_of((float)ha / 255.f * (s.set ? 1.f : 0.55f));
        if (da) {
            text_centred(fb, stride, clip, &k->vt28.glow, d, C, Y_DATE, RD_C_AMBER, da);
            text_centred(fb, stride, clip, &k->vt28.core, d, C, Y_DATE, RD_C_AMBER, da);
        }
    }
    /* the time: settled at y 252; under the airlock it drops in, (1 - outBack(drop)) x -16 px, at drop x 1.6 */
    const int ty = s.settled ? Y_TIME : Y_TIME + iround((1.f - out_back(s.drop)) * -16.f);
    const omni_rect_t tbox = {RD_BY_TIME.x0, (int16_t)(RD_BY_TIME.y0 - 17), RD_BY_TIME.x1, (int16_t)(RD_BY_TIME.y1 + 2)};
    if (meets(s.settled ? RD_BY_TIME : tbox, clip)) {
        char t[6];
        fk_time_str(&in->clk, in->h24, t);
        const uint8_t ta = s.settled ? (s.set ? 255 : 204)
                                     : a8_of(fk_ease_clampf(s.drop * 1.6f, 0.f, 1.f) * (s.set ? 1.f : 0.8f));
        if (ta) text_centred(fb, stride, clip, &k->s132, t, C, ty, s.set ? RD_C_PAINT : RD_C_PAINT_DIM, ta);
    }
    if (meets(RD_BY_CAPTION, clip)) {
        const uint8_t ca = s.settled ? 230 : a8_of(fk_ease_out_cubic(fk_ease_ph(s.settle, 0.5f, 1.f)) * 0.9f);
        if (ca) fk_atlas_text_tracked(fb, stride, clip, &k->ch13,
                                      s.set ? "DOORS: OPEN (EVENTUALLY)" : "DOORS: OPEN \267 CLOCK: NOT YET", C, Y_CAPTION,
                                      TRACK13, true, RD_C_HAZARD, ca);
    }
}

/* the glance (sleep_airlock()): the dashed hazard arc, the star's limb, the stale marker, the date and the time */
static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const by_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int ox = in->odx, oy = in->ody;
    const float cx = (float)(C + ox), cy = (float)(C + oy);
    /* arc(C, C, 226, .28 pi, .72 pi) with setLineDash([8, 8]), 3 px: canvas angles from 3 o'clock are ours (from 12)
     * less pi / 2, so it runs .78 pi to 1.22 pi of ours; a dash every 16 px of its 312 px, butt ends */
    const float a0 = 0.78f * 3.14159265358979f, len = 0.44f * 3.14159265358979f * 226.f;
    for (int d = 0; 16.f * (float)d < len; d++) {
        const float s0 = 16.f * (float)d, s1 = s0 + 8.f < len ? s0 + 8.f : len;
        omni_pt_t p[6];
        for (int j = 0; j < 3; j++) {
            float sn, cs;
            fk_sincos(a0 + (s0 + (s1 - s0) * (float)j / 2.f) / 226.f, &sn, &cs);
            p[j] = (omni_pt_t){cx + 227.5f * sn, cy - 227.5f * cs};
            p[5 - j] = (omni_pt_t){cx + 224.5f * sn, cy - 224.5f * cs};
        }
        omni_fill_poly(fb, stride, &clip, p, 6, RD_G_HAZARD, 255);
    }
    /* arc(C, 568, 272, 1.2 pi, 1.8 pi), 1.2 px: the circle's part above its ends' row, y 568 - 272 cos(.3 pi) = 408.1 */
    omni_rect_t up = clip;
    up.y1 = (int16_t)(clip.y1 < 408 + oy ? clip.y1 : 408 + oy);
    if (up.y1 > up.y0) fk_ring(fb, stride, &up, cx, (float)(568 + oy), 272.f, 1.2f, RD_G_HULL, 255);
    const bool stale = in->clk.set && in->clk.stale;
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, C + ox, Y_LABEL + oy, true);
    char d[16], t[6];
    fk_date_str(&in->clk, d);
    text_centred(fb, stride, clip, &k->vt28.core, d, C + ox, Y_DATE + oy, RD_G_AMBER, 255);
    fk_time_str(&in->clk, in->h24, t);
    text_centred(fb, stride, clip, &k->s132o, t, C + ox, Y_TIME + oy, RD_G_PAINT, 255);
}

/* ---- dirty(): what changed between a and b */
typedef struct { omni_rect_t r[8], u; unsigned n; } rects_t;   /* the first 8 rects, the count, their bounding box */

static void add(rects_t *L, omni_rect_t r)
{
    if (L->n < 8) L->r[L->n] = r;
    if (L->n == 0) L->u = r;
    L->u.x0 = r.x0 < L->u.x0 ? r.x0 : L->u.x0, L->u.y0 = r.y0 < L->u.y0 ? r.y0 : L->u.y0;
    L->u.x1 = r.x1 > L->u.x1 ? r.x1 : L->u.x1, L->u.y1 = r.y1 > L->u.y1 ? r.y1 : L->u.y1;
    L->n++;
}

static uint8_t emit(const rects_t *L, omni_rect_t out[8])
{
    if (L->n > 8) {
        out[0] = L->u;
        return 1;
    }
    for (unsigned i = 0; i < L->n; i++) out[i] = L->r[i];
    return (uint8_t)L->n;
}

static uint8_t whole(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static bool same_date(const fk_face_in_t *a, const fk_face_in_t *b, bool ampm)
{
    char x[24], y[24];
    fk_date_str(&a->clk, x);
    fk_date_str(&b->clk, y);
    if (ampm) strcat(x, fk_ampm(&a->clk, a->h24)), strcat(y, fk_ampm(&b->clk, b->h24));
    return strcmp(x, y) == 0;
}

static bool same_time(const fk_face_in_t *a, const fk_face_in_t *b)
{
    char x[6], y[6];
    fk_time_str(&a->clk, a->h24, x);
    fk_time_str(&b->clk, b->h24, y);
    return strcmp(x, y) == 0;
}

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody && same_time(a, b) && same_date(a, b, false);
        return same ? 0 : whole(out);
    }
    by_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || a->h24 != b->h24 || a->reduced != b->reduced) return whole(out);
    /* under the airlock (either side) the whole frame: the labels, the time's drop and the caption move with wake_ms */
    if (!sa.settled || !sb.settled) return whole(out);
    if (sa.add_a != sb.add_a || sa.glow_a != sb.glow_a) return whole(out);   /* the ring's add, the limb's glow */
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.col != sb.col)
        for (int i = 0; i < 4; i++) add(&L, RING_BANDS[i]);   /* R173: the rim bands, not the whole box */
    if (sa.dy != sb.dy) add(&L, RD_BY_STAR);
    if (sa.stale != sb.stale) add(&L, RD_BY_STALE);
    if (!same_date(a, b, true)) add(&L, RD_BY_DATE);
    if (!same_time(a, b)) add(&L, RD_BY_TIME);
    if (sa.pip != sb.pip || sa.turn != sb.turn || sa.pip_a != sb.pip_a || sa.pip_g != sb.pip_g) {
        omni_rect_t r;
        if (sa.pip) rd_by_pip_rect(sa.turn, &r), add(&L, r);
        if (sb.pip) rd_by_pip_rect(sb.turn, &r), add(&L, r);
    }
    return emit(&L, out);
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= 0 && x < W && y >= 0 && y < W ? FK_TAP_CHIME : FK_TAP_NONE;
}

const fk_face_ops_t RD_FACE_BAY = {
    .key = "bay",
    .ctx_bytes = sizeof(by_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .reveal = true,    /* the airlock (Task 4; rd/airlock.h): its interior is this face at the reveal's wake_ms */
    .reveal_ops = &RD_REVEAL_AIRLOCK,
};
