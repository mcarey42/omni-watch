/* The numeral key, #15 (dw/faces.h; dw wave 1 Task 15, spec section 12), from faces-modern.js circles-key: a
 * pushed screen (scr_key.c over a dw_view) that teaches the numerals in three pages, always 24 h (D5).
 *   0 the ten numerals, 0-4 over 5-9, each with its Arabic twin, the rule and "Bottom row = top row + the dot";
 *   1 the four line shapes (1-4), and the sum 5 + n = 5+n cycling every 1.6 s;
 *   2 the time frozen when the page opened (the clock less wake_ms, the time since it opened), its rings read one at
 *     a time (1.7 s each, then a 2.6 s rest), the reading building below, and the hours' line "The hours count 00 to
 *     23: 21 is nine in the evening."
 * A tap turns the page (FK_TAP_NEXT; scr_key plays the tick), the back button and BOOT go back (FK_TAP_BACK; the
 * core's pop). Each page fades in over 300 ms and page 0's numerals arrive one by one; under reduced motion a page
 * shows at once and the sum holds at 5 + 1 = 6, while page 2 still reads ring by ring (discrete steps: the reading is
 * information). The mockup's dashed ring round the
 * ring being read is left out: it would cross the next ring's lines (spec section 12's hard rule); the ring being
 * read is lit instead. The glance: the 0-9 strip, dim, on the orbit.
 * Layers: the atlases dw_g_text (the sentences, IBM Plex Sans 15), dw_g_title (the titles), dw_g_d26 (the twins and
 * signs) and dw_g_d34 (page 2's reading). The numerals are drawn live. Pure: no allocation. */
#include <math.h>
#include <string.h>
#include "g_common.h"
#include "dw/assets_meta.h"
#include "dw/circles.h"

#define TEXT G_RGB(0xed, 0xe6, 0xd6)
#define WHITE G_RGB(255, 255, 255)
#define BACK_Y 434.f
#define PIPS_Y 400.f

const omni_rect_t DW_KEY_BACK = {199, 402, 267, 466};
static const omni_rect_t SUM = {80, 286, 386, 372};      /* page 1: the sum, its labels */
static const omni_rect_t WALK = {48, 140, 418, 384};     /* page 2: the rings, the reading and its line */
static const float XS0[5] = {-128.f, -64.f, 0.f, 64.f, 128.f};
static const float XS1[4] = {-111.f, -37.f, 37.f, 111.f};

typedef struct {
    bool text_ok, title_ok, d26_ok, d34_ok;
    fk_img_t text_img, title_img, d26_img, d34_img;
    fk_atlas_t text, title, d26, d34;
} gkey_t;

static bool settled(const fk_face_in_t *in) { return in->reduced || in->wake_ms == UINT32_MAX; }

void dw_key_time(const fk_face_in_t *in, int d[4])
{
    fk_clock_t c = in->clk;
    if (c.set && in->wake_ms != UINT32_MAX) {   /* the clock when the page opened */
        const uint32_t day = 86400000u;
        const uint32_t now = ((c.h % 24u * 60u + c.m % 60u) * 60u + c.s % 60u) * 1000u + c.ms % 1000u;
        const uint32_t t = (now + day - in->wake_ms % day) % day;
        c.h = (uint8_t)(t / 3600000u), c.m = (uint8_t)(t / 60000u % 60u);
    }
    dw_digits24(&c, d);
}

void dw_key_sum(const fk_face_in_t *in, int *n, uint8_t *level)
{
    if (settled(in)) {
        *n = 1, *level = 255;   /* still: 5 + 1 = 6, the shot's */
        return;
    }
    const uint32_t t = in->wake_ms;
    *n = (int)(t / DW_KEY_SUM_MS % 5u);
    const float ph = (float)(t % DW_KEY_SUM_MS) / (float)DW_KEY_SUM_MS;
    *level = (uint8_t)(g_ease_out3(ph * 3.f) * 255.f + 0.5f);
}

int dw_key_slots(const fk_face_in_t *in, dw_slot_t out[12])
{
    switch (in->page) {
    case 1:
        for (int i = 0; i < 4; i++) out[i] = (dw_slot_t){G_C + XS1[i], 176.f, 26.f};
        out[4] = (dw_slot_t){G_C - 106.f, 322.f, 22.f};
        out[5] = (dw_slot_t){G_C - 10.f, 322.f, 22.f};
        out[6] = (dw_slot_t){G_C + 106.f, 322.f, 22.f};
        return 7;
    case 2: {
        const float r = in->clk.set ? 29.f : 30.f, cy = in->clk.set ? 226.f : 220.f;
        for (int i = 0; i < 4; i++) {
            float x, y;
            g_quad(G_C, cy, r, i, &x, &y);
            out[i] = (dw_slot_t){x, y, r};
        }
        return 4;
    }
    default:
        for (int i = 0; i < 10; i++) out[i] = (dw_slot_t){G_C + XS0[i % 5], i < 5 ? 196.f : 296.f, 19.f};
        return 10;
    }
}

static bool key_bind(void *ctx, fk_asset_fn get)
{
    gkey_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->text_ok = g_img(get, ASSET_DW_G_TEXT, &k->text_img, FK_CF_A8, ASSET_DW_G_TEXT_W, ASSET_DW_G_TEXT_H);
    k->title_ok = g_img(get, ASSET_DW_G_TITLE, &k->title_img, FK_CF_A8, ASSET_DW_G_TITLE_W, ASSET_DW_G_TITLE_H);
    k->d26_ok = g_img(get, ASSET_DW_G_D26, &k->d26_img, FK_CF_A8, ASSET_DW_G_D26_W, ASSET_DW_G_D26_H);
    k->d34_ok = g_img(get, ASSET_DW_G_D34, &k->d34_img, FK_CF_A8, ASSET_DW_G_D34_W, ASSET_DW_G_D34_H);
    k->text = (fk_atlas_t){&k->text_img, DW_CELLS_DW_G_TEXT, DW_GLYPHS_DW_G_TEXT, ASSET_DW_G_TEXT_LINE_H};
    k->title = (fk_atlas_t){&k->title_img, DW_CELLS_DW_G_TITLE, DW_GLYPHS_DW_G_TITLE, ASSET_DW_G_TITLE_LINE_H};
    k->d26 = (fk_atlas_t){&k->d26_img, DW_CELLS_DW_G_D26, DW_GLYPHS_DW_G_D26, ASSET_DW_G_D26_LINE_H};
    k->d34 = (fk_atlas_t){&k->d34_img, DW_CELLS_DW_G_D34, DW_GLYPHS_DW_G_D34, ASSET_DW_G_D34_LINE_H};
    return k->text_ok && k->title_ok && k->d26_ok && k->d34_ok;
}

/* ---- the state an input draws (dirty compares these) */
typedef struct {
    uint8_t fade;          /* the page's fade in */
    uint8_t build[10];     /* page 0: each numeral's arrival */
    int sum_n;             /* page 1 */
    uint8_t sum_l;
    int cur;               /* page 2: the ring being read (4: all read), and which twins show */
    uint8_t shown;
    int t[4];              /* page 2: the frozen time */
} kstate_t;

static void state_of(const fk_face_in_t *in, kstate_t *st)
{
    memset(st, 0, sizeof *st);
    const bool still = settled(in);
    const float t = still ? 1e9f : (float)in->wake_ms;
    st->fade = (uint8_t)(g_clampf(t / 300.f, 0, 1) * 255.f + 0.5f);
    if (in->page == 0)
        for (int d = 0; d < 10; d++)
            st->build[d] = (uint8_t)(g_clampf((t - 100.f - 70.f * (float)d) / 400.f, 0, 1) * 255.f + 0.5f);
    if (in->page == 1) dw_key_sum(in, &st->sum_n, &st->sum_l);
    if (in->page == 2) {
        dw_key_time(in, st->t);
        /* the reading is information, not decoration: under reduced motion it still steps ring by ring (I1); only a
         * settled input (the stills) shows it read through */
        const bool through = in->wake_ms == UINT32_MAX;
        const uint32_t tt = through ? 0 : in->wake_ms % DW_KEY_CYCLE_MS;
        st->cur = through ? 4 : (int)(tt / DW_KEY_STEP_MS);
        for (int i = 0; i < 4; i++)
            if (i < st->cur || st->cur >= 4 || (i == st->cur && tt % DW_KEY_STEP_MS > 900)) st->shown |= 1u << i;
    }
}

static uint8_t mul8(int a, int b) { return (uint8_t)(a * b / 255); }

static void line(uint16_t *fb, int stride, omni_rect_t clip, const gkey_t *k, const char *s, int y, uint16_t rgb,
                 uint8_t opa)
{
    if (k->text_ok && opa) fk_atlas_text_tracked(fb, stride, clip, &k->text, s, G_C, y, 0.f, true, rgb, opa);
}

static void title(uint16_t *fb, int stride, omni_rect_t clip, const gkey_t *k, const char *s, uint8_t fade)
{
    if (k->title_ok) fk_atlas_text_tracked(fb, stride, clip, &k->title, s, G_C, 88, 3.f, true, G_INK, mul8(204, fade));
}

static void glyph(uint16_t *fb, int stride, const omni_rect_t *clip, int d, float x, float y, float r, uint16_t rgb)
{
    omni_rect_t bb;
    dw_glyph_bbox(x, y, r, &bb);
    if (bb.x0 < clip->x1 && clip->x0 < bb.x1 && bb.y0 < clip->y1 && clip->y0 < bb.y1)
        dw_glyph_draw(fb, stride, clip, d, x, y, r, rgb, 255, 1.f);
}

static void d26(uint16_t *fb, int stride, omni_rect_t clip, const gkey_t *k, char c, float x, int base, uint16_t rgb,
                uint8_t opa)
{
    const char s[2] = {c, 0};
    if (k->d26_ok && opa) fk_atlas_text_tracked(fb, stride, clip, &k->d26, s, x, base, 0.f, true, rgb, opa);
}

static void page0(const gkey_t *k, const kstate_t *st, omni_rect_t clip, uint16_t *fb, int stride)
{
    title(fb, stride, clip, k, "THE NUMERALS", st->fade);
    line(fb, stride, clip, k, "Each line is one.  The dot is five.", 116, TEXT, st->fade);
    line(fb, stride, clip, k, "A ring inside is zero.", 137, TEXT, st->fade);
    dw_slot_t sl[12];
    const fk_face_in_t p0 = {.page = 0};
    dw_key_slots(&p0, sl);
    for (int d = 0; d < 10; d++) {
        const float x = sl[d].cx, y = sl[d].cy;
        const int l = mul8(st->build[d], st->fade);
        if (!l) continue;
        glyph(fb, stride, &clip, d, x, y, sl[d].r, g_level(G_INK_R, G_INK_G, G_INK_B, l));
        d26(fb, stride, clip, k, (char)('0' + d), x, (int)y + 52, WHITE, (uint8_t)l);
    }
    line(fb, stride, clip, k, "Bottom row = top row + the dot", 374, G_INK, mul8(191, st->fade));
}

static void page1(const gkey_t *k, const kstate_t *st, omni_rect_t clip, uint16_t *fb, int stride)
{
    const uint8_t f = st->fade;
    const uint16_t ink = g_level(G_INK_R, G_INK_G, G_INK_B, f);
    title(fb, stride, clip, k, "HOW IT WORKS", f);
    line(fb, stride, clip, k, "Count the lines: they make a shape.", 116, TEXT, f);
    dw_slot_t sl[12];   /* 0-3 the shapes, 4-6 the sum: 5, n, 5 + n */
    const fk_face_in_t p1 = {.page = 1};
    dw_key_slots(&p1, sl);
    for (int i = 0; i < 4; i++) {
        glyph(fb, stride, &clip, i + 1, sl[i].cx, sl[i].cy, sl[i].r, ink);
        d26(fb, stride, clip, k, (char)('1' + i), sl[i].cx, (int)sl[i].cy + 60, WHITE, f);
    }
    line(fb, stride, clip, k, "Add the big dot to add five:", 270, TEXT, f);
    if (!g_meets(clip, SUM)) return;
    const int y = (int)sl[4].cy, n = st->sum_n, e = mul8(st->sum_l, f);
    glyph(fb, stride, &clip, 5, sl[4].cx, sl[4].cy, sl[4].r, ink);
    d26(fb, stride, clip, k, '+', G_C - 63.f, y + 9, G_RGB(233, 226, 210), mul8(179, f));
    glyph(fb, stride, &clip, n, sl[5].cx, sl[5].cy, sl[5].r, ink);
    d26(fb, stride, clip, k, '=', G_C + 54.f, y + 9, G_RGB(233, 226, 210), mul8(179, f));
    if (e) glyph(fb, stride, &clip, 5 + n, sl[6].cx, sl[6].cy, sl[6].r, g_level(0xff, 0xf0, 0xc8, e));
    const char five[2] = "5", nn[2] = {(char)('0' + n), 0}, sum[2] = {(char)('5' + n), 0};
    if (k->text_ok) {   /* their Arabic twins */
        fk_atlas_text_tracked(fb, stride, clip, &k->text, five, sl[4].cx, y + 44, 0.f, true, WHITE, f);
        fk_atlas_text_tracked(fb, stride, clip, &k->text, nn, sl[5].cx, y + 44, 0.f, true, WHITE, f);
        if (e) fk_atlas_text_tracked(fb, stride, clip, &k->text, sum, sl[6].cx, y + 44, 0.f, true, WHITE, (uint8_t)e);
    }
}

static const char *const WHICH[4] = {"First hour digit: ", "Second hour digit: ", "First minute digit: ",
                                     "Last minute digit: "};

/* "Last minute digit: dot 5 + 3 lines = 8" for ring `which` showing v */
static char *put(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static void explain(int which, int v, char out[64])
{
    char *p = put(out, WHICH[which]);
    const int n = v % 5;
    if (v == 0) p = put(p, "a ring inside = 0");
    else if (v == 5) p = put(p, "the dot = 5");
    else {
        if (v > 5) p = put(p, "dot 5 + ");
        *p++ = (char)('0' + n);
        p = put(p, n > 1 ? " lines = " : " line = ");
        *p++ = (char)('0' + v);
    }
    *p = 0;
}

static void page2(const gkey_t *k, const fk_face_in_t *in, const kstate_t *st, omni_rect_t clip, uint16_t *fb,
                  int stride)
{
    const uint8_t f = st->fade;
    title(fb, stride, clip, k, "READ THE TIME", f);
    dw_slot_t sl[12];
    dw_key_slots(in, sl);
    if (!in->clk.set) {   /* empty rings: the watch has not learned the time */
        for (int i = 0; i < 4; i++)
            glyph(fb, stride, &clip, DW_G_EMPTY, sl[i].cx, sl[i].cy, sl[i].r, g_level(G_INK_R, G_INK_G, G_INK_B, f));
        line(fb, stride, clip, k, "Empty rings: the watch", 310, TEXT, f);
        line(fb, stride, clip, k, "hasn't learned the time yet.", 331, TEXT, f);
        return;
    }
    line(fb, stride, clip, k, "The hours count 00 to 23:", 114, TEXT, mul8(217, f));
    line(fb, stride, clip, k, "21 is nine in the evening.", 132, TEXT, mul8(217, f));
    for (int i = 0; i < 4; i++) {
        const bool active = i == st->cur, done = i < st->cur || st->cur >= 4;
        const int l = mul8(active || st->cur >= 4 ? 255 : done ? 191 : 102, f);
        const uint16_t c = active ? g_level(0xff, 0xf0, 0xc8, l) : g_level(G_INK_R, G_INK_G, G_INK_B, l);
        glyph(fb, stride, &clip, st->t[i], sl[i].cx, sl[i].cy, sl[i].r, c);
    }
    if (k->d34_ok) {   /* the reading so far: d d : d d */
        static const float XR[5] = {-52.f, -26.f, 0.f, 26.f, 52.f};
        for (int j = 0; j < 5; j++) {
            const int i = j < 2 ? j : j - 1;
            if (j != 2 && !(st->shown & (1u << i))) continue;
            const char s[2] = {j == 2 ? ':' : (char)('0' + st->t[i]), 0};
            fk_atlas_text_tracked(fb, stride, clip, &k->d34, s, G_C + XR[j], 346, 0.f, true, WHITE, f);
        }
    }
    char e[64];
    if (st->cur < 4) explain(st->cur, st->t[st->cur], e);
    else memcpy(e, "That's the time.", 17);
    line(fb, stride, clip, k, e, 374, G_INK, mul8(242, f));
}

static void key_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const gkey_t *k = ctx;
    kstate_t st;
    state_of(in, &st);
    g_fill(fb, stride, clip, 0);
    if (in->page == 1) page1(k, &st, clip, fb, stride);
    else if (in->page == 2) page2(k, in, &st, clip, fb, stride);
    else page0(k, &st, clip, fb, stride);
    for (int i = 0; i < DW_KEY_PAGES; i++)   /* the pips */
        fk_disc(fb, stride, &clip, G_C + (float)(i - 1) * 14.f, PIPS_Y, i == in->page ? 3.4f : 2.6f, G_INK,
                i == in->page ? 255 : 77);
    /* the back button: a 48 px ring and its chevron */
    const omni_rect_t back = {(int16_t)(G_C - 26), (int16_t)(BACK_Y - 26), (int16_t)(G_C + 26), (int16_t)(BACK_Y + 26)};
    if (g_meets(clip, back)) {
        fk_ring(fb, stride, &clip, G_C, BACK_Y, 24.f, 1.4f, G_INK, 128);
        fk_line(fb, stride, &clip, G_C + 4.f, BACK_Y - 8.f, G_C - 5.f, BACK_Y, 2.f, G_INK, 255);
        fk_line(fb, stride, &clip, G_C - 5.f, BACK_Y, G_C + 4.f, BACK_Y + 8.f, 2.f, G_INK, 255);
    }
}

/* sleepFrame: the 0-9 strip only, dim (17 px rings in G.glance, their twins in #4A4436), on the orbit */
static void key_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const gkey_t *k = ctx;
    g_fill(fb, stride, clip, 0);
    for (int d = 0; d < 10; d++) {
        const float x = G_C + XS0[d % 5] + (float)in->odx, y = (d < 5 ? 186.f : 280.f) + (float)in->ody;
        dw_glyph_draw_w(fb, stride, &clip, d, x, y, 17.f, 2.f, G_GLANCE, 255, 1.f);
        const char s[2] = {(char)('0' + d), 0};
        if (k->text_ok)
            fk_atlas_text_tracked(fb, stride, clip, &k->text, s, x, (int)y + 40, 0.f, true, G_RGB(0x4a, 0x44, 0x36), 255);
    }
}

static uint8_t key_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->odx != b->odx || a->ody != b->ody ||
        a->reduced != b->reduced || a->page != b->page)
        return g_full(out);
    if (b->glance) return 0;   /* the strip: nothing but the orbit */
    kstate_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.fade != sb.fade || memcmp(sa.build, sb.build, sizeof sa.build) != 0 || memcmp(sa.t, sb.t, sizeof sa.t) != 0)
        return g_full(out);   /* the page's fade, page 0's arrivals, page 2's frozen time */
    if (sa.sum_n != sb.sum_n || sa.sum_l != sb.sum_l) {
        out[0] = SUM;
        return 1;
    }
    if (sa.cur != sb.cur || sa.shown != sb.shown) {
        out[0] = WALK;
        return 1;
    }
    return 0;
}

static fk_tap_t gkey_tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    if (x >= DW_KEY_BACK.x0 && x < DW_KEY_BACK.x1 && y >= DW_KEY_BACK.y0 && y < DW_KEY_BACK.y1) return FK_TAP_BACK;
    return FK_TAP_NEXT;
}

const fk_face_ops_t DW_KEY = {
    .key = "key",
    .ctx_bytes = sizeof(gkey_t),
    .cadence_ms = 40,
    .bind = key_bind,
    .draw = key_draw,
    .glance = key_glance,
    .dirty = key_dirty,
    .tap = gkey_tap,
    .reveal = false,
    .pages = DW_KEY_PAGES,
};
