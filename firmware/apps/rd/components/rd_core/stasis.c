/* Stasis Clock (`stasis`, rd/faces.h; rd wave 1 Task 9, spec section 10.1; faces-reveal.js stasisBase() and
 * stasisFace() settled, sleep_stasis() the glance).
 *
 * The base (RW10, 0 B of assets): the seal arcs r 211 and r 229 (1 px, steel .55), 60 ticks on r 216-224 (1.2 px, .6)
 * with a rivet at every fifth (r 220), the lamp housing at (233, 13) r 7.5, drawn procedurally at bind with face_kit's
 * shapes into the context: every pixel it lights lies in the annulus [BASE_R0, BASE_R1] about the centre, so the
 * context keeps only that annulus (each row's left and right spans, about 34 000 px), with the chime ring's two
 * coverages (its 9 px halo and 2.5 px core) beside it. A frame copies the annulus and draws the rest:
 *   the seconds slot on r 220 (C20): the head's +-0.03 rad arc, 3.5 px, and a 5-slot tail at 2.5 px, alpha
 *   .55 x .55^(k-1) x (1 - .4 frac), frac stepped at 100 ms (0 under reduced motion: the slot steps once a second), and
 *   the head's glow r 16 at .35; the go-lamp at (233, 13) r 4.5 and its glow r 14 (green live, amber stale, none unset);
 *   BOOTH 4 . DECK 14 (Chakra 13 paintDim) at y 117, or the stale marker; the date (VT323 28 amber, 12 h with its AM/PM
 *   suffix) at y 149; the hero time (stencil 132, paint; unset "--:--" paintDim at .8) at y 278; the readout
 *   (rd_asleep_lines: VT323 28 phosphor, Chakra 13 paintDim) at y 325 / 350.
 * The chime picture (not under reduced motion, an unset clock or the glance): the seal ring swells green with thrum
 * (the 2.5 px core in phosphor at .55 thrum, then the 9 px phosphorDim halo at .5 thrum over it, the mockup's order), the
 * numerals take a breath (glow r 190 at .1 thrum), and the go-lamp dims to .35 and blinks on beep.
 * The glance (sleep_stasis()): 12 dots on r 220 (r 1.6, g.steel), the lamp dot r 3.5 (g.phos, g.amber stale, none
 * unset), the stale marker in g.amber, the date (VT323 28 core, g.amber, no AM/PM), the time as the 132 stencil's
 * 1.5 px outline (g.paint) and IN STASIS / AWAITING TIME SIGNAL (VT323 20 core, g.phos) at y 322; all through the orbit.
 * Pure: no esp_*, freertos or LVGL; no libm (lesson 6: angles through fk_sincos, the base's coverage by an integer
 * square root); no allocation. */
#include <string.h>
#include "rd/faces.h"
#include "rd/asleep.h"
#include "rd/release.h"
#include "fk/ease.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "fk/compose.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define C 233
#define TAU_F 6.28318530718f
#define BASE_R0 207   /* the annulus that holds every pixel the base, the chime ring and nothing else light */
#define BASE_R1 232
#define NPX_MAX 36000 /* its pixels (34 600 at these radii; bind refuses more) */
#define BAND 8        /* the bind's scratch rows */

#define Y_LABEL 117
#define Y_DATE 149
#define Y_TIME 278
#define Y_READ1 325
#define Y_READ2 350
#define Y_GLANCE_LINE 322
#define TRACK13 (13.f * 0.14f)   /* lab()'s letterSpacing */

const omni_rect_t RD_ST_SLOT_BOX = {-18, -18, 19, 19};
const omni_rect_t RD_ST_TIME = {34, 182, 432, 288};
const omni_rect_t RD_ST_READOUT = {80, 290, 386, 358};
const omni_rect_t RD_ST_STALE = {140, 102, 326, 124};
const omni_rect_t RD_ST_DATE = {100, 122, 366, 162};
const omni_rect_t RD_ST_LAMP = {218, 0, 249, 29};
const omni_rect_t RD_ST_TAP = {0, 0, W, W};

typedef struct { int16_t ox0, ix0, ix1, ox1; uint32_t off; } span_t;   /* row y: [ox0, ix0) then [ix1, ox1) */

typedef struct {
    fk_img_t s132_img, s132o_img, ch13_img;
    fk_atlas_t s132, s132o, ch13;
    rd_atlas2_t vt28, vt20;   /* used where filled, never copied */
    const uint8_t *g16, *g14, *g190;
    span_t span[W];
    uint32_t npx;
    uint16_t base[NPX_MAX];   /* the base's pixels on the annulus, row by row */
    uint8_t halo[NPX_MAX], core[NPX_MAX];   /* the chime ring's coverage there (0..255) */
    uint16_t band[W * BAND];  /* the bind's scratch */
} st_t;

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

/* a pixel's coverage by the annulus [r0, r1) about the centre (fk_ring_fill's rule: clamp(r1 + .5 - d) - clamp(r0 + .5
 * - d)), radii and the distance in 1/256 px; 0..255 */
static uint8_t ring_cov(uint32_t d256, int32_t r0_256, int32_t r1_256)
{
    int32_t co = r1_256 + 128 - (int32_t)d256, ci = r0_256 + 128 - (int32_t)d256;
    co = co < 0 ? 0 : co > 256 ? 256 : co;
    ci = ci < 0 ? 0 : ci > 256 ? 256 : ci;
    const int32_t cov = co - ci;
    return (uint8_t)(cov <= 0 ? 0 : cov >= 256 ? 255 : (cov * 255 + 128) / 256);
}

static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

/* ---- the base, drawn once at bind */
static void base_draw(uint16_t *fb, int stride, const omni_rect_t *clip)
{
    const uint16_t ring = RD_C_STEEL;
    fk_ring(fb, stride, clip, C, C, 211.f, 1.f, ring, 140);   /* rgba(90, 96, 104, .55) */
    fk_ring(fb, stride, clip, C, C, 229.f, 1.f, ring, 140);
    for (int i = 0; i < 60; i++) {
        float s, c;
        fk_sincos((float)i / 60.f * TAU_F, &s, &c);
        if (i % 5 == 0) {   /* rivet(): a steel disc r 1.6, a steelHi pixel up-left, a black one down-right */
            const float x = C + 220.f * s, y = C - 220.f * c;
            fk_disc(fb, stride, clip, x, y, 1.6f, RD_C_STEEL, 255);
            const omni_pt_t hi[4] = {{x - 1.2f, y - 1.2f}, {x - 0.2f, y - 1.2f}, {x - 0.2f, y - 0.2f}, {x - 1.2f, y - 0.2f}};
            const omni_pt_t lo[4] = {{x + 0.4f, y + 0.6f}, {x + 1.4f, y + 0.6f}, {x + 1.4f, y + 1.6f}, {x + 0.4f, y + 1.6f}};
            omni_fill_poly(fb, stride, clip, hi, 4, RD_C_STEEL_HI, 255);
            omni_fill_poly(fb, stride, clip, lo, 4, 0x0000, 255);
        } else {
            fk_line(fb, stride, clip, C + 216.f * s, C - 216.f * c, C + 224.f * s, C - 224.f * c, 1.2f, RD_C_STEEL, 153);
        }
    }
    fk_disc(fb, stride, clip, C, 13.f, 7.5f, 0x0000, 255);   /* the lamp housing */
    fk_ring(fb, stride, clip, C, 13.f, 7.5f, 1.f, RD_C_STEEL_HI, 140);
}

/* row y's annulus spans: pixel centres with BASE_R0 <= d <= BASE_R1, in doubled integer coordinates */
static void row_span(int y, span_t *sp)
{
    const int64_t dy = 2 * y + 1 - W, r0 = 2 * BASE_R0, r1 = 2 * BASE_R1;
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
    st_t *k = ctx;
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
    b = get(ASSET_RD_F_VT20, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt20);
    k->s132 = (fk_atlas_t){&k->s132_img, RD_CELLS_RD_F_S132, RD_GLYPHS_RD_F_S132, ASSET_RD_F_S132_LINE_H};
    k->s132o = (fk_atlas_t){&k->s132o_img, RD_CELLS_RD_F_S132O, RD_GLYPHS_RD_F_S132O, ASSET_RD_F_S132O_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->g16 = rd_glow_lut(16), k->g14 = rd_glow_lut(14), k->g190 = rd_glow_lut(190);
    ok = ok && k->g16 && k->g14 && k->g190;
    /* the annulus's spans */
    uint32_t off = 0;
    for (int y = 0; y < W; y++) {
        row_span(y, &k->span[y]);
        k->span[y].off = off;
        off += (uint32_t)(k->span[y].ix0 - k->span[y].ox0) + (uint32_t)(k->span[y].ox1 - k->span[y].ix1);
    }
    if (off > NPX_MAX) return false;
    k->npx = off;
    /* the base, BAND rows at a time (band-exact, so the bands are the whole frame's), and the chime ring's coverage */
    const int32_t h0 = (int32_t)(215.5f * 256.f), h1 = (int32_t)(224.5f * 256.f);   /* the 9 px halo about r 220 */
    const int32_t c0 = (int32_t)(218.75f * 256.f), c1 = (int32_t)(221.25f * 256.f); /* the 2.5 px core */
    for (int y0 = 0; y0 < W; y0 += BAND) {
        const int y1 = y0 + BAND < W ? y0 + BAND : W;
        const omni_rect_t clip = {0, (int16_t)y0, W, (int16_t)y1};
        memset(k->band, 0, sizeof k->band);
        base_draw(k->band, W, &clip);
        for (int y = y0; y < y1; y++) {
            const span_t *sp = &k->span[y];
            uint32_t o = sp->off;
            const int64_t dy = 2 * y + 1 - W;
            for (int x = sp->ox0; x < sp->ox1; x++) {
                if (x >= sp->ix0 && x < sp->ix1) continue;
                const int64_t dx = 2 * x + 1 - W;
                /* d in 1/256 px: sqrt((dx^2 + dy^2) / 4) x 256 = sqrt((dx^2 + dy^2) x 16384) */
                const uint32_t d256 = isqrt64((uint64_t)(dx * dx + dy * dy) * 16384u);
                k->base[o] = k->band[(y - y0) * W + x];
                k->halo[o] = ring_cov(d256, h0, h1);
                k->core[o] = ring_cov(d256, c0, c1);
                o++;
            }
        }
    }
    return ok;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, chime;   /* chime: the picture runs (thrum > 0, awake, set, not reduced) */
    float thrum;
    int sec;                  /* the head slot (the second) */
    int fq;                   /* frac in tenths (the 100 ms step; 0 reduced) */
    uint16_t lamp_rgb;
    uint8_t lamp_a, lamp_glow;   /* the go-lamp's disc and glow alphas (0: none) */
    /* Task 10: under the reveal the face settles (settle: the slot, the lamp, the header, the date and the readout
     * fade in) and thaws (thaw: the numerals cross-fade from frost to paint, the frost speckle dissolving) by
     * in->wake_ms; both 1 when settled (wake_ms UINT32_MAX), reduced or in the glance. */
    float settle, thaw;
} st_state_t;

/* The reveal's settle and thaw (spec section 9.1): outCubic over 1150-2000 ms and inOutSine over 1350-2100 ms of
 * the reveal's time; 1 past their ends, so the reveal's last frame is the settled one byte for byte. */
static void wake_look(const fk_face_in_t *in, float *settle, float *thaw)
{
    *settle = *thaw = 1.f;
    if (in->wake_ms == UINT32_MAX || in->reduced || in->glance) return;
    const float t = (float)in->wake_ms;
    *settle = fk_ease_out_cubic(fk_ease_ph(t, 1150.f, 2000.f));
    float sn, cs;
    fk_sincos(3.14159265358979f * fk_ease_ph(t, 1350.f, 2100.f), &sn, &cs);
    *thaw = t >= 2100.f ? 1.f : -(cs - 1.f) / 2.f;
}

static bool chime_active(const fk_face_in_t *in) { return in->thrum > 0 || in->beep > 0 || in->click > 0; }

static void state_of(const fk_face_in_t *in, st_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    const bool pic = s->set && !in->reduced && !in->glance;
    s->chime = pic && in->thrum > 0;
    s->thrum = s->chime ? (in->thrum > 1 ? 1.f : in->thrum) : 0.f;
    s->sec = in->clk.s % 60;
    s->fq = in->reduced ? 0 : in->clk.ms / 100;
    wake_look(in, &s->settle, &s->thaw);
    if (s->set) {
        const bool act = pic && chime_active(in);
        const float beep = act ? (in->beep > 1 ? 1.f : in->beep < 0 ? 0.f : in->beep) : 0.f;
        const float k = (act ? 0.35f + beep * 0.65f : 1.f) * s->settle;
        s->lamp_rgb = s->stale ? RD_C_AMBER : RD_C_GO;
        s->lamp_a = a8_of(k);
        s->lamp_glow = a8_of(0.4f * k + beep * 0.6f);
    }
}

/* slot i's look in s: -1 not drawn, else (k << 8 | alpha) */
static int slot_look(const st_state_t *s, int i)
{
    if (!s->set) return -1;
    const int k = (s->sec - i + 60) % 60;
    if (k > 5) return -1;
    static const float TAIL[6] = {1.f, 0.55f, 0.3025f, 0.166375f, 0.09150625f, 0.0503284375f};
    const float a = (k == 0 ? 1.f : TAIL[k] * (1.f - 0.4f * (float)s->fq / 10.f)) * s->settle;
    return k << 8 | a8_of(a);
}

static void slot_centre(int i, float *x, float *y)
{
    float s, c;
    fk_sincos((float)i / 60.f * TAU_F, &s, &c);
    *x = C + 220.f * s, *y = C - 220.f * c;
}

void rd_st_slot_rect(int i, omni_rect_t *out)
{
    float x, y;
    slot_centre(((i % 60) + 60) % 60, &x, &y);
    const int cx = iround(x), cy = iround(y);
    *out = (omni_rect_t){(int16_t)(cx + RD_ST_SLOT_BOX.x0), (int16_t)(cy + RD_ST_SLOT_BOX.y0),
                         (int16_t)(cx + RD_ST_SLOT_BOX.x1), (int16_t)(cy + RD_ST_SLOT_BOX.y1)};
}

unsigned rd_st_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    /* the ring's 8 sector boxes (r 214 to 226, each 45 degrees from 22.5), the breath's box and the lamp's */
    for (int i = 0; i < 8; i++) {
        float lo_x = 1e9f, lo_y = 1e9f, hi_x = -1e9f, hi_y = -1e9f;
        for (int j = 0; j <= 9; j++) {
            float s, c;
            fk_sincos(((22.5f + 45.f * (float)i) + 5.f * (float)j) / 360.f * TAU_F, &s, &c);
            for (int rr = 0; rr < 2; rr++) {
                const float r = rr ? 226.f : 214.f, x = C + r * s, y = C - r * c;
                lo_x = x < lo_x ? x : lo_x, hi_x = x > hi_x ? x : hi_x;
                lo_y = y < lo_y ? y : lo_y, hi_y = y > hi_y ? y : hi_y;
            }
        }
        out[i] = (omni_rect_t){(int16_t)((int)lo_x - 2), (int16_t)((int)lo_y - 2), (int16_t)((int)hi_x + 3),
                               (int16_t)((int)hi_y + 3)};
    }
    out[8] = (omni_rect_t){C - 191, C - 1 - 191, C + 191, C - 1 + 191};   /* the glow r 190 about (233, 232) */
    out[9] = RD_ST_LAMP;
    return 10;
}

/* ---- drawing */
static void text_centred(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *s, int cx,
                         int y, uint16_t rgb, uint8_t opa)
{
    const int w = fk_atlas_width(at, s, 0);
    fk_atlas_text_rgb(fb, stride, clip, at, s, cx - w / 2, y, 0, rgb, opa, 0);
}

/* phos(): the glow plane in glow, then the core in col, at a */
static void phos(uint16_t *fb, int stride, omni_rect_t clip, const rd_atlas2_t *a2, const char *s, int cx, int y,
                 uint16_t col, uint16_t glow, uint8_t a)
{
    text_centred(fb, stride, clip, &a2->glow, s, cx, y, glow, a);
    text_centred(fb, stride, clip, &a2->core, s, cx, y, col, a);
}

static void base_copy(const st_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        memset(d, 0, (size_t)(clip.x1 - clip.x0) * sizeof d[0]);
        const span_t *sp = &k->span[y];
        /* left [ox0, ix0) at off, right [ix1, ox1) after it */
        const int seg[2][2] = {{sp->ox0, sp->ix0}, {sp->ix1, sp->ox1}};
        uint32_t o = sp->off;
        for (int g = 0; g < 2; g++) {
            const int a = seg[g][0] > clip.x0 ? seg[g][0] : clip.x0, b = seg[g][1] < clip.x1 ? seg[g][1] : clip.x1;
            if (a < b) memcpy(d + (a - clip.x0), k->base + o + (a - seg[g][0]), (size_t)(b - a) * sizeof d[0]);
            o += (uint32_t)(seg[g][1] - seg[g][0]);
        }
    }
}

/* the chime ring over the annulus: cov x a8 of rgb, from cov[] */
static void ring_breath(const st_t *k, uint16_t *fb, int stride, omni_rect_t clip, const uint8_t *cov, uint16_t rgb,
                        uint8_t a8)
{
    if (!a8) return;
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        const span_t *sp = &k->span[y];
        const int seg[2][2] = {{sp->ox0, sp->ix0}, {sp->ix1, sp->ox1}};
        uint32_t o = sp->off;
        for (int g = 0; g < 2; g++) {
            const int a = seg[g][0] > clip.x0 ? seg[g][0] : clip.x0, b = seg[g][1] < clip.x1 ? seg[g][1] : clip.x1;
            for (int x = a; x < b; x++) {
                const unsigned c = cov[o + (uint32_t)(x - seg[g][0])];
                if (c) d[x - clip.x0] = omni_blend565(d[x - clip.x0], rgb, (int)((c * a8 + 127u) / 255u));
            }
            o += (uint32_t)(seg[g][1] - seg[g][0]);
        }
    }
}

static void slot_draw(const st_t *k, uint16_t *fb, int stride, omni_rect_t clip, const st_state_t *s, int i)
{
    const int look = slot_look(s, i);
    if (look < 0) return;
    omni_rect_t box;
    rd_st_slot_rect(i, &box);
    if (!meets(box, clip)) return;
    const int kk = look >> 8;
    const float w = kk == 0 ? 3.5f : 2.5f, ro = 220.f + w / 2.f, ri = 220.f - w / 2.f, a = (float)i / 60.f * TAU_F;
    omni_pt_t p[6];
    for (int j = 0; j < 3; j++) {   /* canvas arc(a - .03, a + .03), butt ends: the outer edge out, the inner back */
        float sn, cs;
        fk_sincos(a - 0.03f + 0.03f * (float)j, &sn, &cs);
        p[j] = (omni_pt_t){C + ro * sn, C - ro * cs};
        p[5 - j] = (omni_pt_t){C + ri * sn, C - ri * cs};
    }
    omni_fill_poly(fb, stride, &clip, p, 6, RD_C_PHOS, (uint8_t)(look & 0xFF));
    if (kk == 0) {   /* the head's glow r 16 at .35 (x settle under the reveal) */
        float x, y;
        slot_centre(i, &x, &y);
        fk_glow_add_lut(fb, stride, &clip, iround(x), iround(y), 16, k->g16, RD_C_PHOS,
                        s->settle >= 1.f ? 89 : a8_of(0.35f * s->settle));
    }
}

/* the mockup's rng (mulberry32), [0, 1) in 24 bits */
static float rng_next(uint32_t *seed)
{
    *seed += 0x6D2B79F5u;
    uint32_t t = (*seed ^ (*seed >> 15)) * (1u | *seed);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) / 16777216.0f;
}

/* The thaw's frost on the numerals (stasisFace(): 260 crystals of 1.5 px, seed 5, over the time's width from
 * y - 98, 104 px tall, in #E6F2EA at .7 (1 - thaw) settle; a crystal goes once its draw falls under thaw x 1.2) */
static void frost_speckle(uint16_t *fb, int stride, omni_rect_t clip, int tw, float thaw, float settle)
{
    const uint8_t a = a8_of(0.7f * (1.f - thaw) * settle);
    if (!a) return;
    uint32_t seed = 5;
    for (int i = 0; i < 260; i++) {
        const float x = (float)C - (float)tw / 2.f + rng_next(&seed) * (float)tw;
        const float y = (float)(Y_TIME - 98) + rng_next(&seed) * 104.f;
        if (rng_next(&seed) < thaw * 1.2f) continue;
        const omni_pt_t q[4] = {{x, y}, {x + 1.5f, y}, {x + 1.5f, y + 1.5f}, {x, y + 1.5f}};
        omni_fill_poly(fb, stride, &clip, q, 4, RD_RGB(0xE6, 0xF2, 0xEA), a);
    }
}

static uint8_t ease8(float settle, float a, float b) { return a8_of(fk_ease_out_cubic(fk_ease_ph(settle, a, b))); }

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const st_t *k = ctx;
    st_state_t s;
    state_of(in, &s);
    base_copy(k, fb, stride, clip);
    for (int j = 0; j < 6 && s.set; j++) slot_draw(k, fb, stride, clip, &s, (s.sec - j + 60) % 60);
    if (s.chime) {   /* the seal ring breathes: the core, then the halo over it (stasisFace()'s order) */
        ring_breath(k, fb, stride, clip, k->core, RD_C_PHOS, a8_of(0.55f * s.thrum));
        ring_breath(k, fb, stride, clip, k->halo, RD_C_PHOS_DIM, a8_of(0.5f * s.thrum));
    }
    if (s.lamp_a && meets(RD_ST_LAMP, clip)) {
        fk_disc(fb, stride, &clip, C, 13.f, 4.5f, s.lamp_rgb, s.lamp_a);
        if (s.lamp_glow) fk_glow_add_lut(fb, stride, &clip, C, 13, 14, k->g14, s.lamp_rgb, s.lamp_glow);
    }
    const bool settled = s.settle >= 1.f && s.thaw >= 1.f;
    const uint8_t ha = ease8(s.settle, 0.2f, 1.f);   /* the header's fade-in under the reveal (255 settled) */
    if (meets(RD_ST_STALE, clip)) {
        if (s.stale) rd_stale_mark(fb, stride, &clip, &k->ch13, C, Y_LABEL, false);
        else if (ha) fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "BOOTH 4 \267 DECK 14", C, Y_LABEL, TRACK13, true,
                                           RD_C_PAINT_DIM, ha);
    }
    if (meets(RD_ST_DATE, clip)) {
        char d[24];
        fk_date_str(&in->clk, d);
        strcat(d, fk_ampm(&in->clk, in->h24));
        const uint8_t da = settled ? (s.set ? 255 : 140) : a8_of((float)ha / 255.f * (s.set ? 1.f : 0.55f));
        if (da) phos(fb, stride, clip, &k->vt28, d, C, Y_DATE, RD_C_AMBER, RD_C_AMBER, da);
    }
    const omni_rect_t frost_box = {RD_ST_TIME.x0, Y_TIME - 98, RD_ST_TIME.x1, RD_ST_TIME.y1};
    if (meets(RD_ST_TIME, clip) || (!settled && meets(frost_box, clip))) {
        char t[6];
        fk_time_str(&in->clk, in->h24, t);
        if (settled) {
            text_centred(fb, stride, clip, &k->s132, t, C, Y_TIME, s.set ? RD_C_PAINT : RD_C_PAINT_DIM,
                         s.set ? 255 : 204);
        } else if (!s.set) {   /* "--:--" in paintDim at .8 settle */
            const uint8_t a = a8_of(0.8f * s.settle);
            if (a) text_centred(fb, stride, clip, &k->s132, t, C, Y_TIME, RD_C_PAINT_DIM, a);
        } else {   /* the thaw: frost at (1 - thaw) settle, then paint at thaw over it, then the crystals */
            const uint8_t fa = a8_of((1.f - s.thaw) * s.settle), pa = a8_of(s.thaw);
            if (fa) text_centred(fb, stride, clip, &k->s132, t, C, Y_TIME, RD_C_FROST, fa);
            if (pa) text_centred(fb, stride, clip, &k->s132, t, C, Y_TIME, RD_C_PAINT, pa);
            frost_speckle(fb, stride, clip, fk_atlas_width(&k->s132, t, 0), s.thaw, s.settle);
        }
    }
    if (s.chime) fk_glow_add_lut(fb, stride, &clip, C, Y_TIME - 46, 190, k->g190, RD_C_PHOS, a8_of(0.1f * s.thrum));
    if (meets(RD_ST_READOUT, clip)) {
        char l1[RD_ASLEEP_LINE];
        const char *l2 = NULL;
        rd_asleep_lines(in->slept_s, s.set, l1, &l2);
        const uint8_t a1 = ease8(s.settle, 0.45f, 1.f), a2 = ease8(s.settle, 0.6f, 1.f);
        if (a1) phos(fb, stride, clip, &k->vt28, l1, C, Y_READ1, RD_C_PHOS, RD_C_PHOS_DIM, a1);
        if (a2) fk_atlas_text_tracked(fb, stride, clip, &k->ch13, l2, C, Y_READ2, TRACK13, true, RD_C_PAINT_DIM, a2);
    }
}

static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const st_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int ox = in->odx, oy = in->ody;
    const bool set = in->clk.set, stale = set && in->clk.stale;
    for (int i = 0; i < 12; i++) {
        float sn, cs;
        fk_sincos((float)i / 12.f * TAU_F, &sn, &cs);
        fk_disc(fb, stride, &clip, C + ox + 220.f * sn, C + oy - 220.f * cs, 1.6f, RD_G_STEEL, 255);
    }
    if (set) fk_disc(fb, stride, &clip, (float)(C + ox), (float)(13 + oy), 3.5f, stale ? RD_G_AMBER : RD_G_PHOS, 255);
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, C + ox, Y_LABEL + oy, true);
    char d[16], t[6];
    fk_date_str(&in->clk, d);
    text_centred(fb, stride, clip, &k->vt28.core, d, C + ox, Y_DATE + oy, RD_G_AMBER, 255);
    fk_time_str(&in->clk, in->h24, t);
    text_centred(fb, stride, clip, &k->s132o, t, C + ox, Y_TIME + oy, RD_G_PAINT, 255);
    text_centred(fb, stride, clip, &k->vt20.core, set ? "IN STASIS" : "AWAITING TIME SIGNAL", C + ox,
                 Y_GLANCE_LINE + oy, RD_G_PHOS, 255);
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

/* the rects, or their bounding box when there are more than 8 */
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
    st_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || a->h24 != b->h24 || a->reduced != b->reduced) return whole(out);
    /* under the reveal (settling or thawing, either side) the whole frame: the readout, the header and the numerals'
     * frost all move with wake_ms, and the frost reaches above RD_ST_TIME */
    if (sa.settle < 1.f || sa.thaw < 1.f || sb.settle < 1.f || sb.thaw < 1.f) return whole(out);
    if (sa.chime != sb.chime || sa.thrum != sb.thrum) return whole(out);   /* the ring and the breath: everywhere */
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.lamp_rgb != sb.lamp_rgb || sa.lamp_a != sb.lamp_a || sa.lamp_glow != sb.lamp_glow) add(&L, RD_ST_LAMP);
    if (sa.stale != sb.stale) add(&L, RD_ST_STALE);
    if (!same_date(a, b, true)) add(&L, RD_ST_DATE);
    if (!same_time(a, b)) add(&L, RD_ST_TIME);
    if (sa.set && a->slept_s != b->slept_s) {
        char x[RD_ASLEEP_LINE], y[RD_ASLEEP_LINE];
        rd_asleep_lines(a->slept_s, true, x, NULL);
        rd_asleep_lines(b->slept_s, true, y, NULL);
        if (strcmp(x, y) != 0) add(&L, RD_ST_READOUT);
    }
    for (int i = 0; i < 60 && sa.set; i++)
        if (slot_look(&sa, i) != slot_look(&sb, i)) {
            omni_rect_t r;
            rd_st_slot_rect(i, &r);
            add(&L, r);
        }
    return emit(&L, out);
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= RD_ST_TAP.x0 && x < RD_ST_TAP.x1 && y >= RD_ST_TAP.y0 && y < RD_ST_TAP.y1 ? FK_TAP_CHIME : FK_TAP_NONE;
}

bool rd_chime_preview_allowed(bool chime_on, bool sound_on, bool provisioning)
{
    return chime_on && sound_on && !provisioning;
}

const fk_face_ops_t RD_FACE_STASIS = {
    .key = "stasis",
    .ctx_bytes = sizeof(st_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .reveal = true,    /* Stasis Release (rd/release.h, Task 10): its interior is this face at the reveal's time */
    .reveal_ops = &RD_REVEAL_STASIS,
};
