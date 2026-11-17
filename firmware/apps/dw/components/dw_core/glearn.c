/* Face 16, Learning (dw/faces.h; dw wave 1 Task 15, spec section 12), from faces-modern.js circles-learn: the
 * circle script numerals with their 24 h Arabic twins underneath (D5).
 *
 * Layers: dw_g16_ring (the fainter, narrower script band, held still: scriptRing's alpha, tinted (214, 170, 96)) and
 * the atlases dw_g_d26 (the twins, "?" and "-"), dw_g_label (HOURS, MINUTES) and dw_g_title (the day). Drawn here: the
 * four 68 px rings (dw/circles.h, live), the "?" button (a 48 px ring), the stale marker and the glance. The
 * mockup's divider line between the rows is left out: it crosses the minutes' top lines, and no decorative line may
 * touch a numeral (spec section 12; test_g14_layout_no_touch).
 *
 * A tap hides the twins for DW_G16_ALT_MS (FK_TAP_ALT: in->alt, which face_generic clears after alt_len_ms); the "?"
 * opens the key (FK_TAP_SCREEN). Nothing moves at rest. Unset: empty rings, "-" twins, no day. Stale: the amber lamp
 * and NOT SYNCED in the day's place. Pure: no allocation. */
#include <math.h>
#include <string.h>
#include "g_common.h"
#include "dw/assets_meta.h"
#include "dw/circles.h"

#define TWIN_TOP 222           /* the twins' baselines (y + r + 22 in the mockup) */
#define TWIN_BOT 348
#define HELP_Y 418.f
#define DAY_Y 80
#define STALE_Y 84.f

const dw_slot_t DW_G16_SLOTS[4] = {
    {184.72f, 147.72f, 34.f}, {281.28f, 147.72f, 34.f}, {184.72f, 274.28f, 34.f}, {281.28f, 274.28f, 34.f}};
const omni_rect_t DW_G16_HELP = {203, 388, 263, 448};
const omni_rect_t DW_G16_STALE = {164, 64, 300, 99};

typedef struct {
    bool ring_ok, d26_ok, label_ok, title_ok, dymo_ok;
    fk_img_t ring, d26_img, label_img, title_img, dymo_img;
    fk_atlas_t d26, label, title, dymo;
} g16_t;

static int twin_base(int i) { return i < 2 ? TWIN_TOP : TWIN_BOT; }

void dw_g16_twin_box(int i, omni_rect_t *out)
{
    const int x = (int)DW_G16_SLOTS[i & 3].cx, b = twin_base(i & 3);
    *out = (omni_rect_t){(int16_t)(x - 17), (int16_t)(b - 24), (int16_t)(x + 18), (int16_t)(b + 4)};
}

static bool g16_bind(void *ctx, fk_asset_fn get)
{
    g16_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->ring_ok = g_img(get, ASSET_DW_G16_RING, &k->ring, FK_CF_A8, G_W, G_W);
    k->d26_ok = g_img(get, ASSET_DW_G_D26, &k->d26_img, FK_CF_A8, ASSET_DW_G_D26_W, ASSET_DW_G_D26_H);
    k->label_ok = g_img(get, ASSET_DW_G_LABEL, &k->label_img, FK_CF_A8, ASSET_DW_G_LABEL_W, ASSET_DW_G_LABEL_H);
    k->title_ok = g_img(get, ASSET_DW_G_TITLE, &k->title_img, FK_CF_A8, ASSET_DW_G_TITLE_W, ASSET_DW_G_TITLE_H);
    k->dymo_ok = g_img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->d26 = (fk_atlas_t){&k->d26_img, DW_CELLS_DW_G_D26, DW_GLYPHS_DW_G_D26, ASSET_DW_G_D26_LINE_H};
    k->label = (fk_atlas_t){&k->label_img, DW_CELLS_DW_G_LABEL, DW_GLYPHS_DW_G_LABEL, ASSET_DW_G_LABEL_LINE_H};
    k->title = (fk_atlas_t){&k->title_img, DW_CELLS_DW_G_TITLE, DW_GLYPHS_DW_G_TITLE, ASSET_DW_G_TITLE_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    return k->ring_ok && k->d26_ok && k->label_ok && k->title_ok && k->dymo_ok;
}

/* text right-aligned at x (canvas textAlign right), tracked */
static void right_text(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *s, float x,
                       int base, float track, uint16_t rgb, uint8_t opa)
{
    const float w = fk_atlas_width_tracked(at, s, track) + track;   /* canvas spacing follows the last glyph too */
    fk_atlas_text_tracked(fb, stride, clip, at, s, x - w / 2, base, track, true, rgb, opa);
}

static void g16_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const g16_t *k = ctx;
    g_fill(fb, stride, clip, 0);
    if (k->ring_ok) fk_blit_rgb(fb, stride, clip, &k->ring, 0, 0, G_DECO, 255);
    int d[4];
    dw_digits24(&in->clk, d);
    for (int i = 0; i < 4; i++) {
        const dw_slot_t *s = &DW_G16_SLOTS[i];
        omni_rect_t bb;
        dw_glyph_bbox(s->cx, s->cy, s->r, &bb);
        if (g_meets(clip, bb)) dw_glyph_draw(fb, stride, &clip, d[i], s->cx, s->cy, s->r, G_INK, 255, 1.f);
        omni_rect_t tb;
        dw_g16_twin_box(i, &tb);
        if (!k->d26_ok || !g_meets(clip, tb)) continue;
        char t[2] = {in->alt ? '?' : d[i] == DW_G_EMPTY ? '-' : (char)('0' + d[i]), 0};
        fk_atlas_text_tracked(fb, stride, clip, &k->d26, t, s->cx, twin_base(i), 0.f, true, G_RGB(255, 255, 255),
                              in->alt ? 31 : 255);   /* hidden: a faint "?" (white at .12) */
    }
    if (k->label_ok) {   /* HOURS and MINUTES, right-aligned at cx - 116 */
        right_text(fb, stride, clip, &k->label, "HOURS", G_C - 116.f, 208, 3.f, G_INK, 140);
        right_text(fb, stride, clip, &k->label, "MINUTES", G_C - 116.f, 296, 3.f, G_INK, 140);
    }
    /* the "?" button: a 48 px ring and its "?" */
    const omni_rect_t help = {(int16_t)(G_C - 26), (int16_t)(HELP_Y - 26), (int16_t)(G_C + 26), (int16_t)(HELP_Y + 26)};
    if (g_meets(clip, help)) {
        fk_ring(fb, stride, &clip, G_C, HELP_Y, 24.f, 1.4f, G_INK, 140);
        if (k->d26_ok) fk_atlas_text_tracked(fb, stride, clip, &k->d26, "?", G_C, (int)HELP_Y + 9, 0.f, true, G_INK, 255);
    }
    if (!in->clk.set || !g_meets(clip, DW_G16_STALE)) return;
    if (in->clk.stale) {
        if (k->dymo_ok) dw_stale_mark(fb, stride, &clip, &k->dymo, G_C, STALE_Y);
    } else if (k->title_ok) {   /* the day: "SAT 24" */
        static const char DOW[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        char t[8];
        const unsigned md = in->clk.mday % 100u;
        memcpy(t, in->clk.wday < 7 ? DOW[in->clk.wday] : "   ", 3);
        int n = 3;
        t[n++] = ' ';
        if (md >= 10) t[n++] = (char)('0' + md / 10);
        t[n++] = (char)('0' + md % 10);
        t[n] = 0;
        fk_atlas_text_tracked(fb, stride, clip, &k->title, t, G_C, DAY_Y, 2.f, true, G_RGB(233, 226, 210), 179);
    }
}

/* sleepFrame: the four numerals (G.glance, stroke 2.8) and their twins (#4C473C), on the glance orbit; unset, empty
 * rings and "-"; stale, a dim lamp where the awake marker is */
static void g16_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const g16_t *k = ctx;
    g_fill(fb, stride, clip, 0);
    int d[4];
    dw_digits24(&in->clk, d);
    const float ox = in->odx, oy = in->ody;
    for (int i = 0; i < 4; i++) {
        const dw_slot_t *s = &DW_G16_SLOTS[i];
        dw_glyph_draw_w(fb, stride, &clip, d[i], s->cx + ox, s->cy + oy, s->r, 2.8f, G_GLANCE, 255, 1.f);
        if (!k->d26_ok) continue;
        const char t[2] = {d[i] == DW_G_EMPTY ? '-' : (char)('0' + d[i]), 0};
        fk_atlas_text_tracked(fb, stride, clip, &k->d26, t, s->cx + ox, twin_base(i) + in->ody, 0.f, true,
                              G_RGB(0x4c, 0x47, 0x3c), 255);
    }
    if (in->clk.set && in->clk.stale)
        fk_disc(fb, stride, &clip, G_C + ox, STALE_Y + oy, 2.2f, G_RGB(96, 64, 22), 255);
}

static uint8_t g16_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody)
        return g_full(out);
    if (b->clk.set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday ||
                       a->clk.wday != b->clk.wday))
        return g_full(out);   /* the minute, the day */
    if (b->glance || a->alt == b->alt) return 0;   /* nothing moves at rest; the glance shows the twins anyway */
    for (int i = 0; i < 4; i++) dw_g16_twin_box(i, &out[i]);   /* the twins hidden or back */
    return 4;
}

static fk_tap_t g16_tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    if (x >= DW_G16_HELP.x0 && x < DW_G16_HELP.x1 && y >= DW_G16_HELP.y0 && y < DW_G16_HELP.y1) return FK_TAP_SCREEN;
    return FK_TAP_ALT;   /* anywhere else: hide the twins (or bring them back) */
}

const fk_face_ops_t DW_FACE_LEARN = {
    .key = "learn",
    .ctx_bytes = sizeof(g16_t),
    .cadence_ms = 0,
    .bind = g16_bind,
    .draw = g16_draw,
    .glance = g16_glance,
    .dirty = g16_dirty,
    .tap = g16_tap,
    .reveal = false,
    .alt_len_ms = DW_G16_ALT_MS,
};
