/* Face 14, Circle Script (dw/faces.h; dw wave 1 Task 15, spec sections 11 and 12), from faces-modern.js gfBuild and
 * gfDraw: the time in circle script numerals only, always 24 h (D5).
 *
 * Layers: dw_g14_script (the meaningless circular script, D14: scriptRing's band unwrapped to an A8 strip over r
 * 162-234, the rows its ink reaches on the panel; tinted G.deco and turned by a polar annulus map built at bind),
 * dw_g14_bead (the bead's 28 px glow, alpha only, added in (255, 224, 160)) and dw_g14_hint (TAP FOR THE KEY). Drawn
 * here: the inner circle (r 154) and its sixty fine dots (gfBuild's static layer: two shapes, so no 434 KB image), the
 * four 80 px numeral rings (dw/circles.h, live), the date's 20 px numerals on the circle at 6 o'clock with the
 * circle knocked out behind them, the bead's core, the stale marker and the glance.
 *
 * Motion at rest (spec section 11): the bead sweeps the inner circle once a minute (cadence 40 ms: its old and new
 * boxes); the band steps 0.6 degrees each second, once round in ten minutes (D3; 8 sector boxes of the annulus, which
 * also hold the bead). The wake (the face's own: no doors): the rings draw on in reading order (faces-modern.js: each
 * from 120 + 140 i ms over 650 ms, so 1.19 s), the band and the circle fade in over 700 ms, and the hint shows for
 * 2.5 s, then fades over 0.8 s. Reduced motion: the wake shows at once, the hint shows at its level for the first
 * 2.5 s after a revealing wake (in->fresh_ms) with no fade, the band holds still and the bead steps once a second
 * (dw_second_angle_in). Unset: four empty, dim rings breathing on a 4 s sine (in->mono_ms; still
 * under reduced motion), no bead, no date. Stale: the amber lamp and NOT SYNCED under the minutes, in the hint's place.
 * Pure: no libm trig (fk_sincos), no allocation. */
#include <math.h>
#include <string.h>
#include "g_common.h"
#include "dw/assets_meta.h"
#include "dw/circles.h"

#define ANN_R0 ((float)DW_G14_SCRIPT_R0)
#define ANN_R1 ((float)DW_G14_SCRIPT_R1)
#define ANN_MAX 278528         /* >= fk_annulus_bytes(162, 234, 2048), 274 479 (test_g14_annulus_fits) */
#define BOX_R0 138.f           /* the band's dirty boxes reach in to here: the bead's glow and the date's discs */
#define BEAD_A 179             /* the glow 'lighter' at .7 */
#define DATE_Y (G_C + DW_G14_CIRCLE_R)
#define HINT_Y 363             /* cy + 130 */
#define STALE_Y 356.f

const dw_slot_t DW_G14_SLOTS[4] = {
    {176.2f, 172.2f, 40.f}, {289.8f, 172.2f, 40.f}, {176.2f, 285.8f, 40.f}, {289.8f, 285.8f, 40.f}};
const omni_rect_t DW_G14_STALE = {164, 341, 300, 371};
const omni_rect_t DW_G14_HINT = {130, 346, 336, 370};

typedef struct {
    bool script_ok, bead_ok, hint_ok, dymo_ok;
    fk_img_t script, bead, hint_img, dymo_img;
    fk_atlas_t hint, dymo;
    fk_annulus_t *ann;
    omni_rect_t band[8];   /* the annulus [BOX_R0, ANN_R1) as 8 sector boxes */
    uint64_t ann_mem[ANN_MAX / 8];
} g14_t;

void dw_g14_digits(const fk_face_in_t *in, int d[4]) { dw_digits24(&in->clk, d); }

uint16_t dw_g14_script_off(const fk_face_in_t *in)
{
    if (!in->clk.set || in->reduced) return 0;
    const unsigned t = (in->clk.m % 10u) * 60u + in->clk.s % 60u;   /* seconds into the ten minutes */
    const unsigned k = (t * DW_G14_SCRIPT_NT + 300u) / 600u % DW_G14_SCRIPT_NT;
    return (uint16_t)((DW_G14_SCRIPT_NT - k) % DW_G14_SCRIPT_NT);   /* clockwise: a smaller offset */
}

void dw_g14_date_xy(int i, float *x, float *y) { *x = G_C + (i ? 15.f : -15.f), *y = DATE_Y; }

void dw_g14_bead_xy(const fk_face_in_t *in, float *x, float *y)
{
    const float a = in->reduced ? dw_second_angle_in(in)
                                : ((float)(in->clk.s % 60u) + (float)in->clk.ms / 1000.f) * (2 * G_PI / 60.f);
    float s, c;
    fk_sincos(a, &s, &c);
    *x = G_C + DW_G14_CIRCLE_R * s, *y = G_C - DW_G14_CIRCLE_R * c;
}

/* ---- the state an input draws */
typedef struct {
    float grow[4];     /* each ring's draw-on, 1 drawn */
    uint8_t fade;      /* the band and the circle */
    uint8_t hint;      /* the hint's level */
    uint8_t breath;    /* the unset rings' level */
} wake_t;

static bool settled(const fk_face_in_t *in) { return in->reduced || in->wake_ms == UINT32_MAX; }

static void wake_of(const fk_face_in_t *in, wake_t *w)
{
    const float t = settled(in) ? 1e9f : (float)in->wake_ms;
    for (int i = 0; i < 4; i++) w->grow[i] = g_clampf((t - 120.f - 140.f * (float)i) / 650.f, 0, 1);
    const float fade = g_clampf(t / 700.f, 0, 1);
    w->fade = (uint8_t)(fade * 255.f + 0.5f);
    const bool stale = in->clk.set && in->clk.stale;   /* the marker takes the hint's place */
    float hint = settled(in) || stale ? 0 : g_clampf(1.f - (t - DW_G14_HINT_MS) / 800.f, 0, 1) * fade;
    if (in->reduced && !stale && in->fresh && in->fresh_ms < DW_G14_HINT_MS) hint = 1;   /* at once, no fade (I1) */
    w->hint = (uint8_t)(hint * 0.75f * 255.f + 0.5f);
    float level = 0.36f;   /* the unset breath: .3 + .12 sin over 4 s; held at its middle under reduced motion */
    if (!in->reduced) {
        float s, c;
        fk_sincos((float)(in->mono_ms % 4000u) * (2 * G_PI / 4000.f), &s, &c);
        level = 0.3f + 0.12f * s;
    }
    w->breath = in->clk.set ? 0 : (uint8_t)(level * 255.f + 0.5f);
}

/* the bead's sprite place (its top-left) and centre */
static void bead_at(const fk_face_in_t *in, int *ix, int *iy, float *bx, float *by)
{
    dw_g14_bead_xy(in, bx, by);
    *ix = (int)floorf(*bx - 14.f + 0.5f), *iy = (int)floorf(*by - 14.f + 0.5f);
}

/* every pixel the bead touches: its glow sprite and its core */
static void bead_box(const fk_face_in_t *in, omni_rect_t *r)
{
    int ix, iy;
    float bx, by;
    bead_at(in, &ix, &iy, &bx, &by);
    *r = (omni_rect_t){(int16_t)(ix - 1), (int16_t)(iy - 1), (int16_t)(ix + 29), (int16_t)(iy + 29)};
}

/* the box of the annulus [BOX_R0, ANN_R1] (pixel centres) between angles a0 and a1 clockwise from 12 o'clock
 * (crystal.c's sector_box over this face's radii), kept to the screen */
static void sector_box(float a0, float a1, omni_rect_t *r)
{
    /* zeroed (wave 2 Task 14): host gcc -O2 cannot see that na >= 2 fills xs[0] (-Wmaybe-uninitialized); every value
     * read is written first, so the box (and every CRC) is unchanged */
    float xs[12] = {0}, ys[12] = {0}, ang[6];
    int n = 0, na = 0;
    ang[na++] = a0;
    ang[na++] = a1;
    for (int k = -4; k <= 8; k++) {
        const float q = (float)k * (G_PI / 2);
        if (q > a0 && q < a1 && na < 6) ang[na++] = q;
    }
    for (int i = 0; i < na; i++) {
        float s, c;
        fk_sincos(ang[i], &s, &c);
        for (int j = 0; j < 2; j++) {
            const float rr = j ? ANN_R1 : BOX_R0;
            xs[n] = G_C + rr * s, ys[n] = G_C - rr * c, n++;
        }
    }
    float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < n; i++) {
        if (xs[i] < x0) x0 = xs[i];
        if (xs[i] > x1) x1 = xs[i];
        if (ys[i] < y0) y0 = ys[i];
        if (ys[i] > y1) y1 = ys[i];
    }
    r->x0 = (int16_t)(x0 < 0 ? 0 : floorf(x0)), r->y0 = (int16_t)(y0 < 0 ? 0 : floorf(y0));
    r->x1 = (int16_t)(x1 > G_W ? G_W : ceilf(x1)), r->y1 = (int16_t)(y1 > G_W ? G_W : ceilf(y1));
}

/* ---- binding */
static bool g14_bind(void *ctx, fk_asset_fn get)
{
    g14_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->script_ok = g_img(get, ASSET_DW_G14_SCRIPT, &k->script, FK_CF_A8, DW_G14_SCRIPT_NT,
                         DW_G14_SCRIPT_R1 - DW_G14_SCRIPT_R0);
    k->bead_ok = g_img(get, ASSET_DW_G14_BEAD, &k->bead, FK_CF_A8, ASSET_DW_G14_BEAD_W, ASSET_DW_G14_BEAD_H);
    k->hint_ok = g_img(get, ASSET_DW_G14_HINT, &k->hint_img, FK_CF_A8, ASSET_DW_G14_HINT_W, ASSET_DW_G14_HINT_H);
    k->dymo_ok = g_img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->hint = (fk_atlas_t){&k->hint_img, DW_CELLS_DW_G14_HINT, DW_GLYPHS_DW_G14_HINT, ASSET_DW_G14_HINT_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    k->ann = fk_annulus_build(k->ann_mem, sizeof k->ann_mem, G_C, G_C, ANN_R0, ANN_R1, DW_G14_SCRIPT_NT);
    for (int i = 0; i < 8; i++)
        sector_box((21.f + 45.f * (float)i) * (G_PI / 180.f), (21.f + 45.f * (float)(i + 1)) * (G_PI / 180.f),
                   &k->band[i]);
    return k->script_ok && k->bead_ok && k->hint_ok && k->dymo_ok && k->ann;
}

/* ---- drawing */
static void g14_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const g14_t *k = ctx;
    wake_t w;
    wake_of(in, &w);
    g_fill(fb, stride, clip, 0);
    /* the band, then gfBuild's static layer (the inner circle, the sixty dots), at the wake's fade */
    if (k->script_ok && k->ann)
        fk_annulus_draw_a8(k->ann, &k->script, dw_g14_script_off(in), G_DECO, w.fade, fb, stride, clip);
    const omni_rect_t circle = {(int16_t)(G_C - DW_G14_DOTS_R - 3), (int16_t)(G_C - DW_G14_DOTS_R - 3),
                                (int16_t)(G_C + DW_G14_DOTS_R + 3), (int16_t)(G_C + DW_G14_DOTS_R + 3)};
    const uint8_t deco = (uint8_t)(G_DECO_A * w.fade / 255);
    if (g_meets(clip, circle) && deco) {
        fk_ring(fb, stride, &clip, G_C, G_C, DW_G14_CIRCLE_R, DW_G14_CIRCLE_W, G_DECO, deco);
        for (int i = 0; i < 60; i++) {
            float s, c;
            fk_sincos((float)i * (2 * G_PI / 60.f), &s, &c);
            const float x = G_C + DW_G14_DOTS_R * s, y = G_C - DW_G14_DOTS_R * c;
            const omni_rect_t b = {(int16_t)(x - 3), (int16_t)(y - 3), (int16_t)(x + 4), (int16_t)(y + 4)};
            if (g_meets(clip, b)) fk_disc(fb, stride, &clip, x, y, i % 5 ? 0.9f : 1.7f, G_DECO, deco);
        }
    }
    /* the seconds bead on the inner circle */
    if (in->clk.set) {
        int ix, iy;
        float bx, by;
        bead_at(in, &ix, &iy, &bx, &by);
        const omni_rect_t b = {(int16_t)ix, (int16_t)iy, (int16_t)(ix + 28), (int16_t)(iy + 28)};
        if (g_meets(clip, b)) {
            if (k->bead_ok)
                fk_add_a8(fb, stride, clip, &k->bead, ix, iy, 28, 28, G_RGB(255, 224, 160),
                          (uint8_t)(BEAD_A * w.fade / 255));
            fk_disc(fb, stride, &clip, bx, by, 3.2f, G_INK_HI, 255);
        }
    }
    /* the four rings: drawn on in reading order; unset, empty and dim, breathing */
    int d[4];
    dw_g14_digits(in, d);
    const uint16_t ink = in->clk.set ? G_INK : g_level(G_INK_R, G_INK_G, G_INK_B, w.breath);
    for (int i = 0; i < 4; i++) {
        omni_rect_t bb;
        dw_glyph_bbox(DW_G14_SLOTS[i].cx, DW_G14_SLOTS[i].cy, DW_G14_SLOTS[i].r, &bb);
        if (g_meets(clip, bb))
            dw_glyph_draw(fb, stride, &clip, d[i], DW_G14_SLOTS[i].cx, DW_G14_SLOTS[i].cy, DW_G14_SLOTS[i].r, ink, 255,
                          w.grow[i]);
    }
    /* the day of the month at 6 o'clock, the circle knocked out behind it (a line through a numeral at 3 and 9
     * would turn a 2 into a 4) */
    if (in->clk.set) {
        const unsigned md = in->clk.mday % 100u;
        for (int i = 0; i < 2; i++) {
            float x, y;
            dw_g14_date_xy(i, &x, &y);
            const omni_rect_t b = {(int16_t)(x - DW_G14_KNOCK_R - 2), (int16_t)(y - DW_G14_KNOCK_R - 2),
                                   (int16_t)(x + DW_G14_KNOCK_R + 2), (int16_t)(y + DW_G14_KNOCK_R + 2)};
            if (g_meets(clip, b)) fk_disc(fb, stride, &clip, x, y, DW_G14_KNOCK_R, 0, 255);
        }
        for (int i = 0; i < 2; i++) {
            float x, y;
            dw_g14_date_xy(i, &x, &y);
            dw_glyph_draw(fb, stride, &clip, (int)(i ? md % 10u : md / 10u), x, y, DW_G14_DATE_R,
                          G_RGB(128, 108, 70), 255, 1.f);   /* G.decoHi at .55, by colour */
        }
    }
    if (w.hint && k->hint_ok && g_meets(clip, DW_G14_HINT))
        fk_atlas_text_tracked(fb, stride, clip, &k->hint, "TAP FOR THE KEY", G_C, HINT_Y, 3.f, true, G_INK, w.hint);
    if (in->clk.set && in->clk.stale && k->dymo_ok && g_meets(clip, DW_G14_STALE))
        dw_stale_mark(fb, stride, &clip, &k->dymo, G_C, STALE_Y);
}

/* gfDraw(sleepMode): only the four numerals, dim gold (G.glance, stroke 3.2), on the glance orbit; unset, four empty
 * rings; stale, a dim lamp where the awake marker is */
static void g14_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    (void)ctx;
    g_fill(fb, stride, clip, 0);
    int d[4];
    dw_g14_digits(in, d);
    const float ox = in->odx, oy = in->ody;
    for (int i = 0; i < 4; i++)
        dw_glyph_draw_w(fb, stride, &clip, d[i], DW_G14_SLOTS[i].cx + ox, DW_G14_SLOTS[i].cy + oy, DW_G14_SLOTS[i].r,
                        3.2f, G_GLANCE, 255, 1.f);
    if (in->clk.set && in->clk.stale)
        fk_disc(fb, stride, &clip, G_C + ox, STALE_Y + oy, 2.2f, G_RGB(96, 64, 22), 255);
}

static uint8_t g14_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    const g14_t *k = ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced)
        return g_full(out);   /* the mode, the clock's state, the orbit, reduced motion */
    if (b->clk.set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday))
        return g_full(out);   /* the minute: the rings, the date, the band's ten minutes */
    if (b->glance) return 0;   /* nothing moves in the glance within a minute */
    wake_t wa, wb;
    wake_of(a, &wa);
    wake_of(b, &wb);
    if (memcmp(wa.grow, wb.grow, sizeof wa.grow) != 0 || wa.fade != wb.fade) return g_full(out);   /* the draw-on */
    uint8_t n = 0;
    if (wa.hint != wb.hint) out[n++] = DW_G14_HINT;
    if (!b->clk.set) {
        if (g_level(G_INK_R, G_INK_G, G_INK_B, wa.breath) != g_level(G_INK_R, G_INK_G, G_INK_B, wb.breath))
            for (int i = 0; i < 4; i++)
                dw_glyph_bbox(DW_G14_SLOTS[i].cx, DW_G14_SLOTS[i].cy, DW_G14_SLOTS[i].r, &out[n++]);
        return n;
    }
    if (dw_g14_script_off(a) != dw_g14_script_off(b)) {   /* the band's step (any jump): its boxes hold the bead */
        if (n) return g_full(out);
        memcpy(out, k->band, sizeof k->band);
        return 8;
    }
    float ax, ay, bx, by;
    dw_g14_bead_xy(a, &ax, &ay);
    dw_g14_bead_xy(b, &bx, &by);
    if (ax != bx || ay != by) {   /* the bead's old and new places */
        omni_rect_t ra, rb;
        bead_box(a, &ra);
        bead_box(b, &rb);
        out[n++] = ra;
        if (memcmp(&ra, &rb, sizeof ra) != 0) out[n++] = rb;
    }
    return n;
}

static fk_tap_t g14_tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx, (void)x, (void)y;
    return FK_TAP_SCREEN;   /* anywhere: the key */
}

const fk_face_ops_t DW_FACE_CIRCLES = {
    .key = "circles",
    .ctx_bytes = sizeof(g14_t),
    .cadence_ms = 40,
    .bind = g14_bind,
    .draw = g14_draw,
    .glance = g14_glance,
    .dirty = g14_dirty,
    .tap = g14_tap,
    .reveal = false,
    .wake_len_ms = DW_G14_WAKE_MS,
};
