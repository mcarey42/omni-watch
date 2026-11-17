/* Drift Plate (`drift`, rd/faces.h; rd wave 2 Task 5, spec section 10.3; faces-reveal.js buildHullTile(), washMe() and
 * frame_hull() settled and mid-pass, sleep_hull() the glance).
 *
 * Bind keeps the stars (the mockup's stars(503, 230) recipe through its rng, drawn over black and kept as their lit
 * pixels, about 1 500) and reads the tile (rd_dp_tile, RGB565 466 x 186) and WASH ME (rd_dp_wash, A8 130 x 34) in place.
 * A frame lays, inside its clip: black and the stars; the tile's rows 6-185 over rows BY + 6 to BY + BH, each screen
 * column x showing tile column (x - o) mod 466 for the band's offset o (the pass's rounded offset less the drift); the
 * frame numbers riding it; the time and WASH ME at the pass's offset; the chime's glow over the time; the 60 windows and
 * the newest one's glow; the labels; the chime's flash. Every value depends only on the pixel and the inputs.
 * The pass (rd/pass.h, R166) is this face drawn at the reveal's wake_ms: the reveal only plans the interior's redraw and
 * fades it in.
 * Pure: no esp_*, freertos or LVGL; no libm (lesson 6); no allocation. */
#include <string.h>
#include "rd/faces.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "rd/pass.h"
#include "fk/compose.h"
#include "fk/ease.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define C 233
#define BY RD_DP_BY
#define BH RD_DP_BH
#define TILE_TOP 6         /* the tile's first opaque row (rows 0-5: the builder's transparent margin) */
#define PW 116.5f          /* a plate: W / 4 */
#define STAR_MAX 6000      /* the stars' lit pixels (bind refuses more) */
#define SCRATCH 8          /* the bind's scratch rows */
#define Y_LABEL 82
#define Y_DATE 116
#define Y_TIME (BY + 135)
#define Y_FR (BY + 40)
#define WIN_X 56           /* C - 177 */
#define WIN_Y (BY + BH - 12)
#define WASH_DX 80
#define WASH_Y (BY + 143)
#define TRACK13 (13.f * 0.14f)   /* lab()'s letterSpacing */
#define TRACK_FR10 18             /* the frame numbers' letterSpacing, 1.8 px, in tenths */

const omni_rect_t RD_DP_BAND = {0, BY, W, BY + BH};
const omni_rect_t RD_DP_WINDOWS = {44, 310, 426, 336};
const omni_rect_t RD_DP_TIME = {34, 185, 432, 296};
const omni_rect_t RD_DP_WASH = {C + WASH_DX, WASH_Y, C + WASH_DX + 130, WASH_Y + 34};
const omni_rect_t RD_DP_DATE = {100, 84, 366, 127};
const omni_rect_t RD_DP_CAPTION = {30, 357, 436, 380};
const omni_rect_t RD_DP_STALE = {130, 66, 336, 88};
static const omni_rect_t GLOW_BOX = {C - 180, BY + 90 - 180, C + 181, BY + 90 + 181};   /* the chime's glow r 180 */
static const omni_rect_t FLASH_BOX = {C - 200, BY + BH - 9 - 200, C + 201, W};          /* its flash r 200 */

typedef struct {
    fk_img_t s132_img, s132o_img, ch13_img, tile, wash;
    fk_atlas_t s132, s132o, ch13;
    rd_atlas2_t vt28;   /* used where filled, never copied */
    const uint8_t *g10, *g180, *g200;
    uint32_t nstar;
    uint32_t star_at[STAR_MAX];   /* the stars' pixels (y W + x, rising) and values */
    uint16_t star_px[STAR_MAX];
    uint16_t scratch[W * SCRATCH];
} dp_t;

/* ---- small helpers */
static int iround(float v) { return v >= 0 ? (int)(v + 0.5f) : -(int)(-v + 0.5f); }

static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}

static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

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

/* stars(503, 230) (faces-reveal.js:214-229): each star a w x w square at alpha .18 + .7 b, and the brightest (b > .9) a
 * faint cross at .12 */
static void stars_draw(uint16_t *fb, int stride, const omni_rect_t *clip)
{
    uint32_t seed = 503;
    for (int i = 0; i < 230; i++) {
        const float x = rng_next(&seed) * (float)W, y = rng_next(&seed) * (float)W;
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

static bool bind(void *ctx, fk_asset_fn get)
{
    dp_t *k = ctx;
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
    b = get(ASSET_RD_DP_TILE, &n);
    ok = ok && b && fk_img_from_bin(&k->tile, b, n) && k->tile.rgb && !k->tile.a && k->tile.w == W && k->tile.h == BH;
    b = get(ASSET_RD_DP_WASH, &n);
    ok = ok && b && fk_img_from_bin(&k->wash, b, n) && k->wash.a && !k->wash.rgb && k->wash.w == 130 &&
         k->wash.h == 34;
    if (!ok) return false;
    k->s132 = (fk_atlas_t){&k->s132_img, RD_CELLS_RD_F_S132, RD_GLYPHS_RD_F_S132, ASSET_RD_F_S132_LINE_H};
    k->s132o = (fk_atlas_t){&k->s132o_img, RD_CELLS_RD_F_S132O, RD_GLYPHS_RD_F_S132O, ASSET_RD_F_S132O_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->g10 = rd_soft_lut(10), k->g180 = rd_soft_lut(180), k->g200 = rd_soft_lut(200);
    if (!k->g10 || !k->g180 || !k->g200) return false;
    /* the stars, SCRATCH rows at a time over black (band-exact, so the bands are the whole frame's): their lit pixels */
    k->nstar = 0;
    for (int y0 = 0; y0 < W; y0 += SCRATCH) {
        const int y1 = y0 + SCRATCH < W ? y0 + SCRATCH : W;
        const omni_rect_t clip = {0, (int16_t)y0, W, (int16_t)y1};
        memset(k->scratch, 0, sizeof k->scratch);
        stars_draw(k->scratch, W, &clip);
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < W; x++) {
                const uint16_t p = k->scratch[(y - y0) * W + x];
                if (!p) continue;
                if (k->nstar >= STAR_MAX) return false;
                k->star_at[k->nstar] = (uint32_t)(y * W + x), k->star_px[k->nstar] = p;
                k->nstar++;
            }
    }
    return true;
}

/* ---- the clock's readings */
int rd_dp_drift(const fk_clock_t *c)
{
    if (!c->set) return 0;
    const long sod = (c->h * 60L + c->m) * 60L + c->s;
    return (int)((sod / 10) % W);
}

unsigned rd_dp_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_DP_WINDOWS, out[1] = GLOW_BOX, out[2] = FLASH_BOX;
    return 3;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, settled;
    int off;          /* the pass's offset, rounded (0 settled) */
    int band;         /* the band's offset: off less the drift */
    int lit;          /* windows lit */
    uint8_t settle8;  /* the labels' settle (255 settled) */
    float thrum;      /* the chime picture's thrum (0: none) */
    uint8_t glow_a, flash_a;
} dp_state_t;

static void state_of(const fk_face_in_t *in, dp_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    const bool pic = !in->reduced && !in->glance;
    uint8_t lights8 = 255;
    s->settle8 = 255;
    if (in->wake_ms != UINT32_MAX && pic) {
        rd_pass_state_t ps;
        rd_pass_at(in->wake_ms, false, &ps);
        s->off = iround(ps.off), lights8 = ps.lights8, s->settle8 = ps.settle8;
    }
    s->settled = s->off == 0 && lights8 == 255 && s->settle8 == 255;
    s->band = s->off - rd_dp_drift(&in->clk);
    s->lit = s->set ? (int)(((unsigned)(in->clk.s % 60) + 1u) * lights8 / 255u) : 0;
    if (pic && s->settled) {   /* the chime picture: at rest only (none while the pass runs) */
        const float th = in->thrum > 1 ? 1.f : in->thrum > 0 ? in->thrum : 0.f;
        const float bp = in->beep > 1 ? 1.f : in->beep > 0 ? in->beep : 0.f;
        s->thrum = th;
        s->glow_a = s->set ? a8_of(0.1f * th) : 0;
        s->flash_a = a8_of(0.18f * bp);
    }
}

/* ---- drawing */
static void base_rows(const dp_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    const size_t w = (size_t)(clip.x1 - clip.x0);
    for (int y = clip.y0; y < clip.y1; y++) memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, w * sizeof fb[0]);
    const uint32_t lo = (uint32_t)clip.y0 * W, hi = (uint32_t)clip.y1 * W;
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

/* the tile's opaque rows inside clip: screen column x shows tile column (x - o) mod 466 */
static void band_rows(const dp_t *k, uint16_t *fb, int stride, omni_rect_t clip, int o)
{
    const int y0 = clip.y0 > BY + TILE_TOP ? clip.y0 : BY + TILE_TOP, y1 = clip.y1 < BY + BH ? clip.y1 : BY + BH;
    int c0 = (clip.x0 - o) % W;
    c0 += c0 < 0 ? W : 0;
    const int w = clip.x1 - clip.x0, first = w < W - c0 ? w : W - c0;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        const uint16_t *s = k->tile.rgb + (size_t)(y - BY) * W;
        memcpy(d, s + c0, (size_t)first * 2);
        for (int x = first; x < w; x += W) memcpy(d + x, s, (size_t)(w - x < W ? w - x : W) * 2);
    }
}

/* the frame numbers on each plate (FR 2231 + i at the plate's x + 10, y BY + 40, tracked 1.8 px), glyph by glyph: each
 * glyph at its pen rounded to the pixel (fk_atlas_text_tracked's rule), and only the glyphs that reach the round panel
 * (the numbers ride the band across its rim; a glyph wholly off the panel is never drawn) */
static void frame_numbers(const dp_t *k, uint16_t *fb, int stride, omni_rect_t clip, int o)
{
    const omni_rect_t rows = {0, Y_FR - 16, W, Y_FR + 6};
    if (!meets(rows, clip)) return;
    /* i0 = floor(-o / PW) - 1, in integers: -o / PW = -2 o / 233 */
    const int num = -2 * o;
    const int i0 = (num >= 0 ? num / 233 : -((-num + 232) / 233)) - 1;
    for (int i = i0; i < i0 + 7; i++) {
        const float x = (float)o + (float)i * PW;
        if (x < -PW || x > (float)W) continue;
        char t[16];
        int v = 2231 + i, n = 0, m = 0;
        char dg[8];
        const bool neg = v < 0;
        v = neg ? -v : v;
        do dg[n++] = (char)('0' + v % 10), v /= 10;
        while (v && n < 7);
        t[m++] = 'F', t[m++] = 'R', t[m++] = ' ';
        if (neg) t[m++] = '-';
        while (n) t[m++] = dg[--n];
        t[m] = 0;
        /* the pen in tenths of a pixel, in integers (the plate's x is a whole or half pixel, the track 1.8 px), so a
         * pixel of drift moves every glyph exactly a pixel */
        long pen10 = 10L * o + 1165L * i + 100;
        for (int j = 0; j < m; j++) {
            const char g[2] = {t[j], 0};
            const int adv = fk_atlas_width(&k->ch13, g, 0);
            const long q = pen10 + 5;
            const int x0 = (int)(q >= 0 ? q / 10 : -((-q + 9) / 10));   /* floor(pen + .5) */
            /* the glyph may reach the panel: the nearest point of [x0, x0 + adv] x [Y_FR - 12, Y_FR] to the centre (its
             * foot, the row nearer the centre) lies within 233 + 12 px (integer squares; the 12 px hold the ink a glyph
             * lays past its advance, so a glyph left out has no pixel on the panel) */
            const int nx = C < x0 ? x0 : C > x0 + adv ? x0 + adv : C, ny = Y_FR;
            const long dx = nx - C, dy = ny - C;
            if (g[0] != ' ' && dx * dx + dy * dy <= 245L * 245L)
                fk_atlas_text_rgb(fb, stride, clip, &k->ch13, g, x0, Y_FR, 0, RD_C_PAINT_DIM, 191, 0);
            pen10 += 10L * adv + TRACK_FR10;
        }
    }
}

static void text_centred(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *s, int cx,
                         int y, uint16_t rgb, uint8_t opa)
{
    const int w = fk_atlas_width(at, s, 0);
    fk_atlas_text_rgb(fb, stride, clip, at, s, cx - w / 2, y, 0, rgb, opa, 0);
}

static uint8_t ease8(float settle, float a, float b) { return a8_of(fk_ease_out_cubic(fk_ease_ph(settle, a, b))); }

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const dp_t *k = ctx;
    dp_state_t s;
    state_of(in, &s);
    base_rows(k, fb, stride, clip);
    band_rows(k, fb, stride, clip, s.band);
    frame_numbers(k, fb, stride, clip, s.band);
    /* the painted time travels with its plate (not the drift), then parks dead centre; WASH ME beside it */
    const int tx = C + s.off;
    if (tx > -200 && tx < W + 200) {
        const omni_rect_t tbox = {(int16_t)(RD_DP_TIME.x0 + s.off), RD_DP_TIME.y0, (int16_t)(RD_DP_TIME.x1 + s.off),
                                  RD_DP_TIME.y1};
        if (meets(tbox, clip)) {
            char t[6];
            fk_time_str(&in->clk, in->h24, t);
            text_centred(fb, stride, clip, &k->s132, t, tx, Y_TIME, s.set ? RD_C_PAINT : RD_C_PAINT_DIM,
                         s.set ? 242 : 191);
        }
        const omni_rect_t wbox = {(int16_t)(tx + WASH_DX), WASH_Y, (int16_t)(tx + WASH_DX + 130), WASH_Y + 34};
        if (meets(wbox, clip)) fk_blit_rgb(fb, stride, clip, &k->wash, tx + WASH_DX, WASH_Y, RD_RGB(214, 150, 120), 255);
        if (s.glow_a && meets(GLOW_BOX, clip))
            fk_glow_add_lut(fb, stride, &clip, tx, BY + 90, 180, k->g180, RD_RGB(255, 200, 140), s.glow_a);
    }
    /* the seconds: 60 windows along the underside, lit up to the current second */
    if (meets(RD_DP_WINDOWS, clip)) {
        for (int i = 0; i < 60; i++) {
            const float x = (float)(WIN_X + 6 * i);
            const int d = i - 30;
            float chk = s.thrum > 0 ? 1.4f * s.thrum - (float)(d < 0 ? -d : d) / 40.f : 0.f;
            chk = chk < 0 ? 0 : chk > 1 ? 1 : chk;
            if (i < s.lit) {
                if (i == s.lit - 1) rect_fill(fb, stride, &clip, x, WIN_Y, 3.4f, 5.f, RD_RGB(0xFF, 0xE2, 0xA8), 255);
                else rect_fill(fb, stride, &clip, x, WIN_Y, 3.4f, 5.f, RD_RGB(255, 190, 110), a8_of(0.85f));
            } else if (chk > 0) {
                rect_fill(fb, stride, &clip, x, WIN_Y, 3.4f, 5.f, RD_RGB(255, 190, 110), a8_of(0.7f * chk));
            } else {
                rect_fill(fb, stride, &clip, x, WIN_Y, 3.4f, 5.f, RD_RGB(0x2A, 0x0E, 0x08), 255);
            }
        }
        if (s.lit > 0)
            fk_glow_add_lut(fb, stride, &clip, iround((float)(WIN_X + (s.lit - 1) * 6) + 1.7f), iround(WIN_Y + 2.5f), 10,
                            k->g10, RD_RGB(255, 200, 120), a8_of(0.6f));
    }
    /* the space-side labels */
    const float settle = (float)s.settle8 / 255.f;
    const uint8_t ha = s.settle8 == 255 ? 255 : ease8(settle, 0.2f, 1.f);
    if (meets(RD_DP_STALE, clip)) {
        if (s.stale) rd_stale_mark(fb, stride, &clip, &k->ch13, C, Y_LABEL, false);
        else if (ha) fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "HULL \267 SECTION 7 \267 PORT", C, Y_LABEL, TRACK13,
                                           true, RD_C_PAINT_DIM, ha);
    }
    if (meets(RD_DP_DATE, clip)) {
        char d[24];
        fk_date_str(&in->clk, d);
        strcat(d, fk_ampm(&in->clk, in->h24));
        const uint8_t da = s.settle8 == 255 ? (s.set ? 255 : 140) : a8_of((float)ha / 255.f * (s.set ? 1.f : 0.55f));
        if (da) {
            text_centred(fb, stride, clip, &k->vt28.glow, d, C, Y_DATE, RD_C_AMBER, da);
            text_centred(fb, stride, clip, &k->vt28.core, d, C, Y_DATE, RD_C_AMBER, da);
        }
    }
    if (meets(RD_DP_CAPTION, clip)) {
        const uint8_t ca = s.settle8 == 255 ? 217 : a8_of(fk_ease_out_cubic(fk_ease_ph(settle, 0.5f, 1.f)) * 0.85f);
        if (ca) fk_atlas_text_tracked(fb, stride, clip, &k->ch13,
                                      s.set ? "DRIFT 0.1 M/S \267 NO ONE IS STEERING" : "DRIFTING \267 CLOCK NOT SET", C,
                                      372, TRACK13, true, RD_C_PAINT_DIM, ca);
    }
    if (s.flash_a && meets(FLASH_BOX, clip))
        fk_glow_add_lut(fb, stride, &clip, C, BY + BH - 9, 200, k->g200, RD_RGB(255, 200, 120), s.flash_a);
}

/* the glance (sleep_hull()): the band's two hairlines, the 12 window ticks, the stale marker, the date and the time */
static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const dp_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int ox = in->odx, oy = in->ody;
    rect_fill(fb, stride, &clip, (float)(14 + ox), (float)(BY + 17 + oy), (float)(W - 28), 1.2f, RD_G_HULL, 255);
    rect_fill(fb, stride, &clip, (float)(14 + ox), (float)(BY + BH - 16 + oy), (float)(W - 28), 1.2f, RD_G_HULL, 255);
    if (in->clk.set)
        for (int i = 0; i < 12; i++)
            rect_fill(fb, stride, &clip, (float)(WIN_X + 30 * i + ox), (float)(WIN_Y + oy), 3.f, 4.f, RD_G_AMBER, 255);
    if (in->clk.set && in->clk.stale) rd_stale_mark(fb, stride, &clip, &k->ch13, C + ox, Y_LABEL + oy, true);
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
    dp_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || a->h24 != b->h24 || a->reduced != b->reduced) return whole(out);
    if (!sa.settled || !sb.settled) return whole(out);   /* the pass (either side): the band, the time, the labels */
    if (sa.thrum != sb.thrum || sa.glow_a != sb.glow_a || sa.flash_a != sb.flash_a) return whole(out);   /* the chime */
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.band != sb.band) add(&L, RD_DP_BAND);
    if (sa.lit != sb.lit) add(&L, RD_DP_WINDOWS);
    if (sa.stale != sb.stale) add(&L, RD_DP_STALE);
    if (!same_date(a, b, true)) add(&L, RD_DP_DATE);
    if (!same_time(a, b)) add(&L, RD_DP_TIME);
    return emit(&L, out);
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= 0 && x < W && y >= 0 && y < W ? FK_TAP_CHIME : FK_TAP_NONE;
}

const fk_face_ops_t RD_FACE_DRIFT = {
    .key = "drift",
    .ctx_bytes = sizeof(dp_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .reveal = true,    /* the hull pass (rd/pass.h): this face drawn at the reveal's wake_ms */
    .reveal_ops = &RD_REVEAL_PASS,
};
