/* Face 6, the Roundel wall (dw/faces.h; dw wave 2 Task 10, spec sections 10 and 11), from faces-classic.js wallCells,
 * WALL_COLS, roundelSprite and classic-roundel-wall: a piece of the console room's wall, sixty roundels in the true
 * staggered pattern cropped by the round glass, a dark porthole at the centre for the time. Each roundel is a minute:
 * numbered clockwise from 12, the lit ones sweep round like a broad minute hand, in the hour's era colour.
 *
 * Layers (tools/export/dw_layers.json): dw_w6_unlit (roundelSprite(17.5, cream, 0), the dark well, 52 px RGB565A8) and
 * dw_w6_lit (roundelSprite(17.5, WALL_COLS[k], 1) for the six colours, a vertical stack of 52 px cells: the k-th is a
 * sub-image by pointer offset, R147 PR16), drawn by hands.c dw_wall_roundel at every cell: the well, then the lit
 * sprite over it at the cell's level (WD27: two sprites, not a 434 KB wall image). The shared A8 atlases dw_p60 (the
 * time and --:--), dw_cor16 (the day) and dw_cap11 (the date line, AWAITING SIGNAL), tinted at draw (WD25), and dw_dymo
 * (the stale strip). Drawn here: the porthole's two rings and its text, the stale mark and the glance.
 *
 * Levels (dw_w6_level, :589-595): cells below the minute m at .82; cell m at clamp(s / .6, 0, 1) (.72 + .28 sin(t / 2400
 * turns)), t the ms of the day (a 2.4 s breath, faded on over the minute's first .6 s); the colour DW_W6_COLS[h % 6]. At
 * the hour the wall goes dark and starts again from 12 in the next colour. Unset: only cell 0 breathes, cream, at
 * .25 + .25 sin on the free-running mono_ms (the clock's fields are not read), --:-- and AWAITING SIGNAL.
 * The wake (wake_len_ms 800, :587): shown = min(m, floor(easeOut3(t / 800) (m + 1))): the wall powers up from 12.
 * Reduced motion: cell m steady at .86 (the breath's mean; derived, the mockup's reduced mode only fades in), no fade-on,
 * no wake; unset, cell 0 steady at .25.
 * Motion stays in regions (the S1 lesson: full-frame canvas animation ran 5-6 fps): dirty() names exactly the cells
 * whose level changed (at rest, cell m's box each frame while it breathes; at the minute, cells m - 1 and m) and the
 * centre when its text changes; the whole frame at the hour (every cell goes dark and the colour changes), on a state,
 * mode, orbit or reduced-motion change, and when more cells change than the 8 rects hold (a skip, the wake's first
 * frames at a late minute).
 * Stale: dw_stale_mark centred (233, 305) (staleMark(cx, cy + 72)); in the glance, its lamp dim.
 * Glance (:619-632): r 3 dots at the lit cells in the colour at .32, a 1.5 px r 13 ring at cell m at .40, the time in
 * cream .30, on the glance orbit.
 * Pure: the table trig (fk_sincos) and +, -, x, / in every still; the cells are a const table; no allocation. */
#include <math.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/faces.h"
#include "omni/blend565.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
/* faces-classic.js P.cream and its alphas over the black porthole as colour (the faces dim by colour, never by alpha) */
#define CREAM RGB(0xF2, 0xEA, 0xD8)
#define CREAM_70 RGB(169, 164, 151)
#define CREAM_55 RGB(133, 129, 119)
#define CREAM_50 RGB(121, 117, 108)
#define CREAM_30 RGB(73, 70, 65)
#define STALE_DIM RGB(96, 64, 22)
#define RING_OUT_A 51   /* the porthole's rings (:598-599): r 104, 1.5 px, cream .20; r 98, 1 px, cream .08 */
#define RING_IN_A 20
#define DAY_BASE 199    /* cy - 34 */
#define TIME_BASE 253   /* cy + 20 */
#define DATE_BASE 279   /* cy + 46 */
#define AWAIT_BASE 281  /* cy + 48 */
#define CELL 35         /* the time's cells (:609 cells(.., 35, 18)) */
#define COLON 18
#define STALE_X 233.f
#define STALE_Y 305.f   /* cy + 72 */
#define SPRITE 52       /* dw_w6_unlit and each of dw_w6_lit's cells */
#define LV_SHOWN 209    /* .82 */
#define LV_REDUCED 219  /* .86 */
#define WAKE_LEN DW_W6_WAKE_MS

/* wallCells (:551-560), worked out once (tools: the mockup's arithmetic in double, then sorted): columns c -7..7 at
 * 233 + 39.836 c, rows at 233 + 46 r (+ 23 in odd columns), kept for 123.5 < d < 238.25, by angle from 12 clockwise
 * and, on one ray (12, 2, 4, 6, 8 and 10 o'clock carry three each), outwards. test_wall_sixty_cells recomputes it. */
static const float CELLS[DW_W6_CELLS][2] = {
    {233.000f, 95.f}, {233.000f, 49.f}, {233.000f, 3.f}, {272.836f, 26.f}, {272.836f, 72.f},
    {312.672f, 49.f}, {312.672f, 95.f}, {352.508f, 72.f}, {352.508f, 118.f}, {392.344f, 95.f},
    {352.508f, 164.f}, {392.344f, 141.f}, {432.180f, 118.f}, {432.180f, 164.f}, {392.344f, 187.f},
    {432.180f, 210.f}, {392.344f, 233.f}, {432.180f, 256.f}, {392.344f, 279.f}, {432.180f, 302.f},
    {352.508f, 302.f}, {392.344f, 325.f}, {432.180f, 348.f}, {392.344f, 371.f}, {352.508f, 348.f},
    {352.508f, 394.f}, {312.672f, 371.f}, {312.672f, 417.f}, {272.836f, 394.f}, {272.836f, 440.f},
    {233.000f, 371.f}, {233.000f, 417.f}, {233.000f, 463.f}, {193.164f, 440.f}, {193.164f, 394.f},
    {153.328f, 417.f}, {153.328f, 371.f}, {113.492f, 394.f}, {113.492f, 348.f}, {73.656f, 371.f},
    {113.492f, 302.f}, {73.656f, 325.f}, {33.820f, 348.f}, {33.820f, 302.f}, {73.656f, 279.f},
    {33.820f, 256.f}, {73.656f, 233.f}, {33.820f, 210.f}, {73.656f, 187.f}, {33.820f, 164.f},
    {113.492f, 164.f}, {73.656f, 141.f}, {33.820f, 118.f}, {73.656f, 95.f}, {113.492f, 118.f},
    {113.492f, 72.f}, {153.328f, 95.f}, {153.328f, 49.f}, {193.164f, 72.f}, {193.164f, 26.f},
};

const uint16_t DW_W6_COLS[6] = {RGB(0xF2, 0xEA, 0xD8), RGB(0xDC, 0xCB, 0xF2), RGB(0xF4, 0xD2, 0x7E),
                                RGB(0xBF, 0xE2, 0xE6), RGB(0xF2, 0xC6, 0xBE), RGB(0xCF, 0xE6, 0xB9)};
static const uint8_t COL8[6][3] = {{0xF2, 0xEA, 0xD8}, {0xDC, 0xCB, 0xF2}, {0xF4, 0xD2, 0x7E},
                                   {0xBF, 0xE2, 0xE6}, {0xF2, 0xC6, 0xBE}, {0xCF, 0xE6, 0xB9}};
const omni_rect_t DW_W6_CENTRE = {150, 182, 316, 286};   /* the day (rows 186-200), time (209-255), date (270-284) */
const omni_rect_t DW_W6_STALE = {164, 290, 302, 320};    /* dw_stale_mark(233, 305)'s reach */

_Static_assert(ASSET_DW_W6_UNLIT_W == SPRITE && ASSET_DW_W6_UNLIT_H == SPRITE, "dw_w6_unlit is one 52 px sprite");
_Static_assert(ASSET_DW_W6_LIT_W == SPRITE && ASSET_DW_W6_LIT_H == 6 * SPRITE, "dw_w6_lit is six 52 px sprites");

typedef struct {
    bool unlit_ok, lit_ok, p60_ok, cor_ok, cap_ok, dymo_ok;
    fk_img_t unlit, lit_img, lit[6], p60_img, cor_img, cap_img, dymo_img;
    fk_atlas_t p60, cor, cap, dymo;
} w6_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

void dw_w6_cell(int i, float *x, float *y)
{
    if (i < 0 || i >= DW_W6_CELLS) i = 0;
    *x = CELLS[i][0], *y = CELLS[i][1];
}

void dw_w6_box(int i, omni_rect_t *out)
{
    float x, y;
    dw_w6_cell(i, &x, &y);
    const int x0 = (int)floorf(x - 25.5f), y0 = (int)floorf(y - 25.5f);
    *out = (omni_rect_t){(int16_t)(x0 < 0 ? 0 : x0), (int16_t)(y0 < 0 ? 0 : y0),
                         (int16_t)(x0 + SPRITE > W ? W : x0 + SPRITE), (int16_t)(y0 + SPRITE > W ? W : y0 + SPRITE)};
}

uint16_t dw_w6_colour(const fk_face_in_t *in) { return in->clk.set ? DW_W6_COLS[in->clk.h % 24u % 6u] : DW_W6_COLS[0]; }

/* a level as 0..255; .01 or less is none (the mockup draws no sprite below it) */
static uint8_t lv(float l) { return l > 0.01f ? (uint8_t)(clampf(l, 0, 1) * 255.f + 0.5f) : 0; }

/* the wall's levels at in: cells i < below at .82, cell cur (-1 none) at cur_lv */
typedef struct {
    int below, cur;
    uint8_t cur_lv;
} wl_t;

static float breath(uint32_t ms)   /* sin(ms / 2400 turns) */
{
    float s, c;
    fk_sincos((float)(ms % 2400u) / 2400.f * 2.f * PI_F, &s, &c);
    return s;
}

static wl_t wl_of(const fk_face_in_t *in)
{
    wl_t w = {0, -1, 0};
    if (!in->clk.set) {   /* only cell 0, on the free-running ms: the clock's fields are not read */
        w.cur = 0;
        w.cur_lv = lv(in->reduced ? 0.25f : 0.25f + 0.25f * breath(in->mono_ms));
        return w;
    }
    const int m = (int)(in->clk.m % 60u);
    int shown = m;
    if (!in->reduced && !in->glance && in->wake_ms < WAKE_LEN) {   /* easeOut3(t / 800) (m + 1) */
        const float u = 1.f - (float)in->wake_ms / (float)WAKE_LEN, k = (1.f - u * u * u) * (float)(m + 1);
        const int n = (int)floorf(k);
        shown = n < m ? n : m;
    }
    w.below = shown;
    if (shown >= m) {
        w.cur = m;
        if (in->reduced) {
            w.cur_lv = LV_REDUCED;
        } else {
            const uint32_t day_ms = (((uint32_t)(in->clk.h % 24u) * 60u + (uint32_t)m) * 60u + in->clk.s % 60u) * 1000u +
                                    in->clk.ms % 1000u;
            const float s = (float)(in->clk.s % 60u) + (float)(in->clk.ms % 1000u) / 1000.f;
            w.cur_lv = lv(clampf(s / 0.6f, 0, 1) * (0.72f + 0.28f * breath(day_ms)));
        }
    }
    return w;
}

static uint8_t wl_level(const wl_t *w, int i) { return i == w->cur ? w->cur_lv : i < w->below ? LV_SHOWN : 0; }

uint8_t dw_w6_level(const fk_face_in_t *in, int i)
{
    if (i < 0 || i >= DW_W6_CELLS) return 0;
    const wl_t w = wl_of(in);
    return wl_level(&w, i);
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool w6_bind(void *ctx, fk_asset_fn get)
{
    w6_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->unlit_ok = img(get, ASSET_DW_W6_UNLIT, &k->unlit, FK_CF_RGB565A8, SPRITE, SPRITE);
    k->lit_ok = img(get, ASSET_DW_W6_LIT, &k->lit_img, FK_CF_RGB565A8, SPRITE, 6 * SPRITE);
    k->p60_ok = img(get, ASSET_DW_P60, &k->p60_img, FK_CF_A8, ASSET_DW_P60_W, ASSET_DW_P60_H);
    k->cor_ok = img(get, ASSET_DW_COR16, &k->cor_img, FK_CF_A8, ASSET_DW_COR16_W, ASSET_DW_COR16_H);
    k->cap_ok = img(get, ASSET_DW_CAP11, &k->cap_img, FK_CF_A8, ASSET_DW_CAP11_W, ASSET_DW_CAP11_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    if (k->lit_ok)   /* the k-th colour's sprite: its rows of the stack, by pointer offset (R147 PR16) */
        for (int c = 0; c < 6; c++) {
            const size_t off = (size_t)c * SPRITE * SPRITE;
            k->lit[c] = (fk_img_t){SPRITE, SPRITE, k->lit_img.rgb + off, k->lit_img.a + off, k->lit_img.cf};
        }
    k->p60 = (fk_atlas_t){&k->p60_img, DW_CELLS_DW_P60, DW_GLYPHS_DW_P60, ASSET_DW_P60_LINE_H};
    k->cor = (fk_atlas_t){&k->cor_img, DW_CELLS_DW_COR16, DW_GLYPHS_DW_COR16, ASSET_DW_COR16_LINE_H};
    k->cap = (fk_atlas_t){&k->cap_img, DW_CELLS_DW_CAP11, DW_GLYPHS_DW_CAP11, ASSET_DW_CAP11_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    return k->unlit_ok && k->lit_ok && k->p60_ok && k->cor_ok && k->cap_ok && k->dymo_ok;
}

/* ---- drawing */
static void fill(uint16_t *fb, int stride, omni_rect_t clip, uint16_t v)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = v;
    }
}

static bool meets(omni_rect_t clip, omni_rect_t r)
{
    return r.x0 < clip.x1 && clip.x0 < r.x1 && r.y0 < clip.y1 && clip.y0 < r.y1;
}

/* cells() (:51-57): fixed cells (CELL px, the colon COLON) centred at cx, each glyph centred in its cell, in rgb */
static void cells(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *t, int cx, int base,
                  uint16_t rgb)
{
    int w = 0;
    for (const char *p = t; *p; p++) w += *p == ':' ? COLON : CELL;
    int px = cx - w / 2;
    for (const char *p = t; *p; p++) {
        const int cw = *p == ':' ? COLON : CELL;
        const char one[2] = {*p, 0};
        fk_atlas_text_rgb(fb, stride, clip, at, one, px, base, cw, rgb, 255, 0);
        px += cw;
    }
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

/* fmt(): "8:15" (12 h, no leading zero) or "20:15" (24 h) */
static void hm_text(const fk_face_in_t *in, char out[6])
{
    const unsigned h = in->h24 ? in->clk.h % 24u : (in->clk.h % 24u + 11) % 12 + 1, m = in->clk.m % 60u;
    char *p = put_u(out, h, in->h24 ? 2 : 1);
    *p++ = ':';
    put_u(p, m, 2);
}

static const char *const DAYS[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char MON[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

/* the porthole's text (:600-611): the day (dw_cor16, cream .7, tracked 1.5), the time (dw_p60, cream, 35 px cells, the
 * colon 18) and the date line (dw_cap11, cream .55, tracked 2.5: "1 NOV  · AM", the middle dot 0xB7 in the atlas);
 * unset, --:-- (cream .5) and AWAITING SIGNAL (cream .55) */
static void centre(const w6_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!in->clk.set) {
        cells(fb, stride, clip, &k->p60, "--:--", (int)C, TIME_BASE, CREAM_50);
        fk_atlas_text_tracked(fb, stride, clip, &k->cap, "AWAITING SIGNAL", C, AWAIT_BASE, 2.5f, true, CREAM_55, 255);
        return;
    }
    fk_atlas_text_tracked(fb, stride, clip, &k->cor, DAYS[in->clk.wday % 7u], C, DAY_BASE, 1.5f, true, CREAM_70, 255);
    char hm[6];
    hm_text(in, hm);
    cells(fb, stride, clip, &k->p60, hm, (int)C, TIME_BASE, CREAM);
    char date[20];
    char *p = put_u(date, in->clk.mday % 32u, 1);
    p = put_s(p, " ");
    p = put_s(p, MON[in->clk.mon % 12u]);
    if (!in->h24) put_s(p, in->clk.h % 24u < 12 ? "  \267  AM" : "  \267  PM");
    fk_atlas_text_tracked(fb, stride, clip, &k->cap, date, C, DATE_BASE, 2.5f, true, CREAM_55, 255);
}

static void w6_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const w6_t *k = ctx;
    fill(fb, stride, clip, 0);
    const wl_t w = wl_of(in);
    const int col = in->clk.set ? (int)(in->clk.h % 24u % 6u) : 0;
    for (int i = 0; i < DW_W6_CELLS; i++) {
        omni_rect_t box;
        dw_w6_box(i, &box);
        if (!meets(clip, box)) continue;
        dw_wall_roundel(fb, stride, &clip, k->unlit_ok ? &k->unlit : NULL, k->lit_ok ? &k->lit[col] : NULL, CELLS[i][0],
                        CELLS[i][1], wl_level(&w, i));
    }
    fk_ring(fb, stride, &clip, C, C, 104.f, 1.5f, CREAM, RING_OUT_A);
    fk_ring(fb, stride, &clip, C, C, 98.f, 1.f, CREAM, RING_IN_A);
    if (meets(clip, DW_W6_CENTRE)) centre(k, in, clip, fb, stride);
    if (in->clk.set && in->clk.stale && k->dymo_ok && meets(clip, DW_W6_STALE))
        dw_stale_mark(fb, stride, &clip, &k->dymo, STALE_X, STALE_Y);
}

/* the colour k at pct % (by colour, over black) */
static uint16_t col_at(int k, int pct)
{
    return RGB((COL8[k][0] * pct + 50) / 100, (COL8[k][1] * pct + 50) / 100, (COL8[k][2] * pct + 50) / 100);
}

/* classic-roundel-wall sleepFrame (:619-632): r 3 dots at the lit cells (colour .32), a 1.5 px r 13 ring at the
 * current cell (.40), the time in cream .30, on the glance orbit; stale, the mark's lamp dim */
static void w6_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const w6_t *k = ctx;
    fill(fb, stride, clip, 0);
    const float ox = (float)in->odx, oy = (float)in->ody;
    char hm[6] = "--:--";
    if (in->clk.set) {
        const int col = (int)(in->clk.h % 24u % 6u), m = (int)(in->clk.m % 60u);
        const uint16_t dot = col_at(col, 32), ring = col_at(col, 40);
        for (int i = 0; i < m; i++) fk_disc(fb, stride, &clip, CELLS[i][0] + ox, CELLS[i][1] + oy, 3.f, dot, 255);
        fk_ring_max(fb, stride, &clip, CELLS[m][0] + ox, CELLS[m][1] + oy, 13.f, 1.5f, ring, 255);
        hm_text(in, hm);
    }
    if (k->p60_ok) cells(fb, stride, clip, &k->p60, hm, (int)C + in->odx, TIME_BASE + in->ody, CREAM_30);
    if (in->clk.set && in->clk.stale)   /* inside the stale rect on every orbit offset */
        fk_disc(fb, stride, &clip, STALE_X - 52.f + ox, STALE_Y + oy, 2.6f, STALE_DIM, 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t w6_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced)
        return full(out);   /* the mode, the clock's state, the orbit, reduced motion */
    const bool set = b->clk.set;
    const bool text = set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday ||
                              a->clk.mon != b->clk.mon || a->clk.wday != b->clk.wday || a->h24 != b->h24);
    if (b->glance) return text ? full(out) : 0;   /* the glance changes only with the time it shows */
    if (set && a->clk.h % 24u % 6u != b->clk.h % 24u % 6u) return full(out);   /* the colour: every lit cell */
    const wl_t wa = wl_of(a), wb = wl_of(b);
    const int room = text ? 7 : 8;
    uint8_t n = 0;
    for (int i = 0; i < DW_W6_CELLS; i++) {
        if (wl_level(&wa, i) == wl_level(&wb, i)) continue;
        if (n == room) return full(out);   /* more cells than the rects hold (a skip, the wake at a late minute) */
        dw_w6_box(i, &out[n++]);
    }
    if (text) out[n++] = DW_W6_CENTRE;
    return n;
}

const fk_face_ops_t DW_FACE_WALL = {
    .key = "wall",
    .ctx_bytes = sizeof(w6_t),
    .cadence_ms = 40,
    .bind = w6_bind,
    .draw = w6_draw,
    .glance = w6_glance,
    .dirty = w6_dirty,
    .tap = NULL,
    .reveal = false,
    .wake_len_ms = WAKE_LEN,
};
