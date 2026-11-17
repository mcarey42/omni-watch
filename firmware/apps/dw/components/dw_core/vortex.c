/* The Vortex tunnel (dw/vortex.h; dw wave 2 Task 13, WD37, the spike behind the gate WD38), from faces-modern.js
 * vxRender (:732-743): each lut cell's texel, scaled by its mask, filled over a 2 x 2 block (q 1) or, for every other
 * cell, a 4 x 4 block (q 2). Everything per row or per block is hoisted: a block's value is computed once per block row
 * of the clip and copied to the block's other rows. The inner loop is integer table lookups, three multiplies and
 * shifts: no float, no libm (sinf, cosf, floorf are software routines on the device), no allocation.
 *
 * Face 12, Vortex (wave 2 Task 14; dw/faces.h, faces-modern.js:677-772): the time on calm black at the eye of the
 * tunnel. The frame, in the mockup's order: the tunnel at DW_V12_Q (the kernel above, palette in->variant % 3, rot and
 * rush from the free-running mono_ms, 0 under reduced motion: the tunnel is still) and the eye's glow (r 120, (255, 220,
 * 170) at .08, additive, glow_lut.h's table), both at the wake's easeOut3(t / 900) (globalAlpha f over black: each
 * channel x f); then the time (dw_v12_glow in (255, 190, 110) at .6 under dw_v12_dig in #FFF4E0, 76 px, centred on 233,
 * baseline 255; 24 h as "HH:MM", 12 h as "H:MM" and its AM/PM), the date line "SUN 1 NOV" (dw_v12_txt, rgba(255, 220,
 * 170, .8) by colour, tracked 3) at 187 and AM/PM at 287; the stale mark at (233, 333); the palette's name (dw_v12_pal,
 * #fff, tracked 3) at 315 while it shows (dw_v12_name_level). Unset: --:-- from dw_p60 in rgba(255, 244, 224, .5) by
 * colour at 253, no date. The glance (:768-772): the ring r 112 (#2A2440, 1.5 px) and the time from dw_v12_dig's alpha
 * in #5A5246 (--:-- unset), on the orbit; stale, the lamp's dim disc.
 * The motion: the tunnel changes only DW_V12_TUNNEL (the calm centre is black at any rot and rush); the face's ops ask
 * for the direct path (.direct, dw_view), where every frame is drawn whole in 8-row bands; through the canvas, dirty()
 * gives the tunnel's four rects when rot or rush moved, the name's box while it fades, and the whole frame for anything
 * else that changes (the text once a minute, the palette, the wake). */
#include "dw/vortex.h"
#include <stddef.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "omni/blend565.h"

static const char *const TEX[DW_V12_PALETTES] = {ASSET_DW_V12_TX0, ASSET_DW_V12_TX1, ASSET_DW_V12_TX2};   /* by palette */

bool dw_vortex_bind(dw_vortex_t *v, fk_asset_fn get, uint8_t pal)
{
    v->lut = NULL;
    v->tex = NULL;
    if (pal >= sizeof TEX / sizeof TEX[0]) return false;
    size_t n = 0;
    const uint8_t *p = get(ASSET_DW_V12_LUT, &n);
    const size_t body = 8u + (size_t)DW_V12_N * DW_V12_N * 3u;   /* odd: one pad byte follows */
    if (!p || n != DW_V12_LUT_BYTES || n != ((body + 1u) & ~(size_t)1u) || memcmp(p, "DWL1", 4) != 0) return false;
    if ((p[4] | p[5] << 8) != DW_V12_N || (p[6] | p[7] << 8) != DW_V12_N || p[body] != 0) return false;
    fk_img_t t;
    size_t tn = 0;
    const void *tp = get(TEX[pal], &tn);
    if (!tp || !fk_img_from_bin(&t, tp, tn) || t.cf != FK_CF_RGB565 || t.w != DW_V12_TEX || t.h != DW_V12_TEX)
        return false;
    v->lut = p + 8;
    v->tex = t.rgb;
    return true;
}

uint8_t dw_vortex_rot(uint32_t ms) { return (uint8_t)(((uint64_t)ms * 22u / 1000u) & 255u); }
uint8_t dw_vortex_rush(uint32_t ms) { return (uint8_t)(((uint64_t)ms * 70u / 1000u) & 255u); }

/* each RGB565 channel x (m + 1) >> 8, m > 0 (the Task 14 ruling: a full mask, 255, gives the texel itself) */
static inline uint16_t shade(uint16_t t, unsigned m)
{
    m++;
    const unsigned r = (t >> 11) * m >> 8, g = ((t >> 5) & 63u) * m >> 8, b = (t & 31u) * m >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

/* The texel index of cell c: row (v + rush) & 255, column (u + rot) & 255. */
#define TI(c) (((unsigned)(uint8_t)((c)[1] + rush) << 8) | (uint8_t)((c)[0] + rot))

/* One block row's first clip row: [x, xe) px, every block's value. FETCH(ti) is the texel. */
#define ROW(FETCH)                                                                                                    \
    while (x < clip.x1) {                                                                                             \
        const int bx = x / bs, xe = (bx + 1) * bs < clip.x1 ? (bx + 1) * bs : clip.x1;                              \
        const uint8_t *c = row + (size_t)(bx * cs) * 3u;                                                              \
        const unsigned m = c[2];                                                                                      \
        const uint16_t p = m ? shade(FETCH(TI(c)), m) : 0;                                                            \
        for (; x < xe; x++) *d++ = p;                                                                                 \
    }
#define FETCH_RGB(ti) tex[ti]
#define FETCH_IDX(ti) pal[idx[ti]]

/* One clip, either texture form (idx NULL: tex). The block rows (bs px tall) the clip meets; per block row, the cells'
 * values across the clip into its first row, then that row copied down the block's other rows inside the clip. */
static void draw(const dw_vortex_t *v, const uint8_t *idx, const uint16_t *pal, uint8_t rot, uint8_t rush, uint8_t q,
                 omni_rect_t clip, uint16_t *fb, int stride)
{
    /* M3 (the Task 13 review): the clip cut to the screen, fb moved to the cut clip's first pixel */
    const int cx0 = clip.x0 < 0 ? 0 : clip.x0, cy0 = clip.y0 < 0 ? 0 : clip.y0;
    const int cx1 = clip.x1 > 466 ? 466 : clip.x1, cy1 = clip.y1 > 466 ? 466 : clip.y1;
    if (cx0 >= cx1 || cy0 >= cy1 || !v->lut || (!idx && !v->tex)) return;
    fb += (ptrdiff_t)(cy0 - clip.y0) * stride + (cx0 - clip.x0);
    clip = (omni_rect_t){(int16_t)cx0, (int16_t)cy0, (int16_t)cx1, (int16_t)cy1};
    const int bs = q == 2 ? 4 : 2, cs = q == 2 ? 2 : 1;   /* block size (px), cell step */
    const int w = clip.x1 - clip.x0;
    const uint16_t *tex = v->tex;
    for (int by = clip.y0 - clip.y0 % bs; by < clip.y1; by += bs) {
        const int y0 = by > clip.y0 ? by : clip.y0, y1 = by + bs < clip.y1 ? by + bs : clip.y1;
        const uint8_t *row = v->lut + (size_t)(by / bs * cs) * DW_V12_N * 3u;
        uint16_t *o = fb + (size_t)(y0 - clip.y0) * stride, *d = o;
        int x = clip.x0;
        if (idx) {
            ROW(FETCH_IDX)
        } else {
            ROW(FETCH_RGB)
        }
        for (int y = y0 + 1; y < y1; y++) memcpy(fb + (size_t)(y - clip.y0) * stride, o, (size_t)w * 2u);
    }
}

void dw_vortex_draw(const dw_vortex_t *v, uint8_t rot, uint8_t rush, uint8_t q, omni_rect_t clip, uint16_t *fb,
                    int stride)
{
    draw(v, NULL, NULL, rot, rush, q, clip, fb, stride);
}

void dw_vortex_draw_idx(const dw_vortex_t *v, const uint8_t *idx, const uint16_t pal[256], uint8_t rot, uint8_t rush,
                        uint8_t q, omni_rect_t clip, uint16_t *fb, int stride)
{
    draw(v, idx, pal, rot, rush, q, clip, fb, stride);
}

/* ================================================================ face 12, Vortex (wave 2 Task 14) */
#define W 466
#define C 233
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define TIME_RGB RGB(0xFF, 0xF4, 0xE0)        /* #FFF4E0 */
#define GLOW_RGB RGB(255, 190, 110)           /* shadowColor rgba(255, 190, 110, .6) */
#define GLOW_A 153                            /* .6 */
#define TXT_RGB RGB(204, 176, 136)            /* rgba(255, 220, 170, .8) over black, by colour */
#define UNSET_RGB RGB(128, 122, 112)          /* rgba(255, 244, 224, .5) over black, by colour */
#define NAME_RGB RGB(255, 255, 255)
#define EYE_RGB RGB(255, 220, 170)
#define EYE_A 20                              /* glowSprite(.., 120, .08): .08 x 255 */
#define EYE_R 120
#define RING_RGB RGB(0x2A, 0x24, 0x40)        /* the glance's ring, #2A2440 */
#define DIM_RGB RGB(0x5A, 0x52, 0x46)         /* the glance's time, #5A5246 */
#define STALE_DIM RGB(96, 64, 22)
#define TIME_BASE 255                         /* cy + 22 */
#define UNSET_BASE 253                        /* cy + 20 */
#define DATE_BASE 187                         /* cy - 46 */
#define AMPM_BASE 287                         /* cy + 54 */
#define NAME_BASE 315                         /* cy + 82 */
#define STALE_Y 333.f
#define TRACK 3.f
#define WAKE 900
#define PULSE 1900
#define NAME_FULL 1400
#define ANY_TEXT_TOP 140                      /* every text pixel, glow included, lies in [ANY_TEXT_TOP, W - ...) */

const omni_rect_t DW_V12_CENTRE = {161, 161, 305, 305};
const omni_rect_t DW_V12_TUNNEL[4] = {{0, 0, W, 161}, {0, 305, W, W}, {0, 161, 161, 305}, {305, 161, W, 305}};
const omni_rect_t DW_V12_NAME = {133, 300, 333, 318};
const omni_rect_t DW_V12_STALE = {164, 318, 302, 348};   /* dw_stale_mark(233, 333)'s reach, as #8's */

static const char *const NAMES[DW_V12_PALETTES] = {"GOLD & BLUE", "STORM", "PRISM"};   /* VX_PALS (:682-686) */
static const char *const DAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

typedef struct {
    bool vx_ok, dig_ok, glow_ok, txt_ok, pal_ok, p60_ok, dymo_ok;
    dw_vortex_t vx[DW_V12_PALETTES];
    fk_img_t dig_img, glow_img, txt_img, pal_img, p60_img, dymo_img;
    fk_atlas_t dig, glow, txt, pal, p60, dymo;
    const uint8_t *eye;   /* dw_glow_lut(120) */
} v12_t;

uint8_t dw_v12_palette(const fk_face_in_t *in) { return (uint8_t)(in->variant % DW_V12_PALETTES); }

uint8_t dw_v12_name_level(const fk_face_in_t *in)
{
    if (!in->pulsing || in->pulse_ms >= PULSE) return 0;
    if (in->pulse_ms <= NAME_FULL) return 255;
    return (uint8_t)(((unsigned)(PULSE - in->pulse_ms) * 510u + 500u) / 1000u);   /* 255 (1900 - t) / 500, rounded */
}

/* the tunnel's rot and rush: from mono_ms (the clock is not read), 0 under reduced motion */
static void motion(const fk_face_in_t *in, uint8_t *rot, uint8_t *rush)
{
    *rot = in->reduced ? 0 : dw_vortex_rot(in->mono_ms);
    *rush = in->reduced ? 0 : dw_vortex_rush(in->mono_ms);
}

/* the wake's level, 0..256: easeOut3(t / 900) x 256 rounded; 256 settled or under reduced motion */
static unsigned wake_level(const fk_face_in_t *in)
{
    if (in->reduced || in->wake_ms >= WAKE) return 256;
    const float u = 1.f - (float)in->wake_ms / (float)WAKE;
    return (unsigned)((1.f - u * u * u) * 256.f + 0.5f);
}

static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool v12_bind(void *ctx, fk_asset_fn get)
{
    v12_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->vx_ok = true;
    for (uint8_t p = 0; p < DW_V12_PALETTES; p++) k->vx_ok = dw_vortex_bind(&k->vx[p], get, p) && k->vx_ok;
    k->dig_ok = img(get, ASSET_DW_V12_DIG, &k->dig_img, FK_CF_A8, ASSET_DW_V12_DIG_W, ASSET_DW_V12_DIG_H);
    k->glow_ok = img(get, ASSET_DW_V12_GLOW, &k->glow_img, FK_CF_A8, ASSET_DW_V12_GLOW_W, ASSET_DW_V12_GLOW_H);
    k->txt_ok = img(get, ASSET_DW_V12_TXT, &k->txt_img, FK_CF_A8, ASSET_DW_V12_TXT_W, ASSET_DW_V12_TXT_H);
    k->pal_ok = img(get, ASSET_DW_V12_PAL, &k->pal_img, FK_CF_A8, ASSET_DW_V12_PAL_W, ASSET_DW_V12_PAL_H);
    k->p60_ok = img(get, ASSET_DW_P60, &k->p60_img, FK_CF_A8, ASSET_DW_P60_W, ASSET_DW_P60_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->dig = (fk_atlas_t){&k->dig_img, DW_CELLS_DW_V12_DIG, DW_GLYPHS_DW_V12_DIG, ASSET_DW_V12_DIG_LINE_H};
    k->glow = (fk_atlas_t){&k->glow_img, DW_CELLS_DW_V12_GLOW, DW_GLYPHS_DW_V12_GLOW, ASSET_DW_V12_GLOW_LINE_H};
    k->txt = (fk_atlas_t){&k->txt_img, DW_CELLS_DW_V12_TXT, DW_GLYPHS_DW_V12_TXT, ASSET_DW_V12_TXT_LINE_H};
    k->pal = (fk_atlas_t){&k->pal_img, DW_CELLS_DW_V12_PAL, DW_GLYPHS_DW_V12_PAL, ASSET_DW_V12_PAL_LINE_H};
    k->p60 = (fk_atlas_t){&k->p60_img, DW_CELLS_DW_P60, DW_GLYPHS_DW_P60, ASSET_DW_P60_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    for (unsigned i = 0; k->glow_ok && DW_GLYPHS_DW_V12_DIG[i]; i++)   /* the glow sits on the fill's own pen */
        k->glow_ok = DW_GLYPHS_DW_V12_GLOW[i] == DW_GLYPHS_DW_V12_DIG[i] &&
                     DW_CELLS_DW_V12_GLOW[i].adv == DW_CELLS_DW_V12_DIG[i].adv;
    k->eye = dw_glow_lut(EYE_R);
    return k->vx_ok && k->dig_ok && k->glow_ok && k->txt_ok && k->pal_ok && k->p60_ok && k->dymo_ok && k->eye;
}

static char *put_u(char *p, unsigned v, int digits)
{
    char b[4];
    int n = 0;
    do b[n++] = (char)('0' + v % 10u), v /= 10u;
    while (v && n < 3);
    while (n < digits && n < 3) b[n++] = '0';
    while (n) *p++ = b[--n];
    *p = 0;
    return p;
}

static char *put_s(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

/* "8:15" (12 h, no leading zero: (h + 11) % 12 + 1, :755) or "20:15" (24 h) */
static void hm_text(const fk_face_in_t *in, char out[6])
{
    const unsigned h = in->h24 ? in->clk.h % 24u : (in->clk.h % 24u + 11) % 12 + 1, m = in->clk.m % 60u;
    char *p = put_u(out, h, in->h24 ? 2 : 1);
    *p++ = ':';
    put_u(p, m, 2);
}

static bool meets(omni_rect_t clip, omni_rect_t r)
{
    return r.x0 < clip.x1 && clip.x0 < r.x1 && r.y0 < clip.y1 && clip.y0 < r.y1;
}

/* text centred on cx (canvas textAlign center: the advances' width), its pen at cx - width / 2 */
static void centred(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *t, int cx, int base,
                    uint16_t rgb, uint8_t opa)
{
    fk_atlas_text_rgb(fb, stride, clip, at, t, cx - fk_atlas_width(at, t, 0) / 2, base, 0, rgb, opa, 0);
}

/* the glow under the fill: the same pen (the advances are the fill's, checked at bind) */
static void time_glow(const v12_t *k, uint16_t *fb, int stride, omni_rect_t clip, const char *t)
{
    fk_atlas_text_rgb(fb, stride, clip, &k->glow, t, C - fk_atlas_width(&k->dig, t, 0) / 2, TIME_BASE, 0, GLOW_RGB,
                      GLOW_A, 0);
}

/* every channel of the clip x f >> 8 (globalAlpha f over black; f 256 leaves it) */
static void scale(uint16_t *fb, int stride, omni_rect_t clip, unsigned f)
{
    if (f >= 256) return;
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (ptrdiff_t)(y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) {
            const unsigned p = d[x];
            d[x] = (uint16_t)((((p >> 11) * f >> 8) << 11) | ((((p >> 5) & 63u) * f >> 8) << 5) | ((p & 31u) * f >> 8));
        }
    }
}

static void awake(const v12_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    uint8_t rot, rush;
    motion(in, &rot, &rush);
    const unsigned f = wake_level(in);
    dw_vortex_draw(&k->vx[dw_v12_palette(in)], rot, rush, DW_V12_Q, clip, fb, stride);   /* every pixel of the clip */
    scale(fb, stride, clip, f);
    const uint8_t eye = (uint8_t)((EYE_A * f + 128u) >> 8);
    if (eye) fk_glow_add_lut(fb, stride, &clip, C, C, EYE_R, k->eye, EYE_RGB, eye);
    if (clip.y1 <= ANY_TEXT_TOP || clip.y0 >= DW_V12_STALE.y1) return;   /* no text in these rows */
    if (in->clk.set) {
        char hm[6];
        hm_text(in, hm);
        time_glow(k, fb, stride, clip, hm);
        centred(fb, stride, clip, &k->dig, hm, C, TIME_BASE, TIME_RGB, 255);
        char date[16];
        char *p = put_s(date, DAYS[in->clk.wday % 7u]);
        p = put_s(p, " ");
        p = put_u(p, in->clk.mday % 32u, 1);
        p = put_s(p, " ");
        put_s(p, MONTHS[in->clk.mon % 12u]);
        fk_atlas_text_tracked(fb, stride, clip, &k->txt, date, (float)C, DATE_BASE, TRACK, true, TXT_RGB, 255);
        if (!in->h24)
            fk_atlas_text_tracked(fb, stride, clip, &k->txt, in->clk.h % 24u < 12 ? "AM" : "PM", (float)C, AMPM_BASE,
                                  TRACK, true, TXT_RGB, 255);
        if (in->clk.stale && meets(clip, DW_V12_STALE)) dw_stale_mark(fb, stride, &clip, &k->dymo, (float)C, STALE_Y);
    } else {
        centred(fb, stride, clip, &k->p60, "--:--", C, UNSET_BASE, UNSET_RGB, 255);
    }
    const uint8_t name = dw_v12_name_level(in);
    if (name && meets(clip, DW_V12_NAME))
        fk_atlas_text_tracked(fb, stride, clip, &k->pal, NAMES[dw_v12_palette(in)], (float)C, NAME_BASE, TRACK, true,
                              NAME_RGB, name);
}

static void glance(const v12_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    for (int y = clip.y0; y < clip.y1; y++) memset(fb + (ptrdiff_t)(y - clip.y0) * stride, 0, (size_t)(clip.x1 - clip.x0) * 2u);
    const int ox = in->odx, oy = in->ody;
    fk_ring_max(fb, stride, &clip, (float)(C + ox), (float)(C + oy), 112.f, 1.5f, RING_RGB, 255);
    char hm[6] = "--:--";
    if (in->clk.set) hm_text(in, hm);
    centred(fb, stride, clip, &k->dig, hm, C + ox, TIME_BASE + oy, DIM_RGB, 255);
    if (in->clk.set && in->clk.stale)   /* the lamp's dim disc, inside DW_V12_STALE on every orbit offset */
        fk_disc(fb, stride, &clip, (float)C - 52.f + (float)ox, STALE_Y + (float)oy, 2.6f, STALE_DIM, 255);
}

static void v12_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    awake(ctx, in, clip, fb, stride);
}

static void v12_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    glance(ctx, in, clip, fb, stride);
}

static fk_tap_t v12_tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= 0 && y >= 0 && x < W && y < W ? FK_TAP_CYCLE : FK_TAP_NONE;   /* a tap anywhere: the next palette */
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

/* the whole frame for a change of mode, clock state, orbit, motion setting, 12/24 h, palette, the text and every wake
 * step; else the tunnel's four rects when rot or rush moved and the name's box when its level moved */
static uint8_t v12_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced || a->h24 != b->h24)
        return full(out);
    const bool set = b->clk.set;
    const bool text = set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m ||
                              (!b->glance && (a->clk.wday != b->clk.wday || a->clk.mday != b->clk.mday ||
                                              a->clk.mon != b->clk.mon)));
    if (text) return full(out);
    if (b->glance) return 0;   /* the glance changes only with its text */
    if (dw_v12_palette(a) != dw_v12_palette(b) || wake_level(a) != wake_level(b)) return full(out);
    uint8_t n = 0;
    uint8_t ra, sa, rb, sb;
    motion(a, &ra, &sa);
    motion(b, &rb, &sb);
    if (ra != rb || sa != sb)
        for (int i = 0; i < 4; i++) out[n++] = DW_V12_TUNNEL[i];
    if (dw_v12_name_level(a) != dw_v12_name_level(b)) out[n++] = DW_V12_NAME;
    return n;
}

const fk_face_ops_t DW_FACE_VORTEX = {
    .key = "vortex",
    .ctx_bytes = sizeof(v12_t),
    .cadence_ms = 50,
    .bind = v12_bind,
    .draw = v12_draw,
    .glance = v12_glance,
    .dirty = v12_dirty,
    .tap = v12_tap,
    .reveal = false,
    .wake_len_ms = WAKE,
    .pulse_len_ms = PULSE,
    .pulse_guard_ms = 0,
    .direct = true,
};
