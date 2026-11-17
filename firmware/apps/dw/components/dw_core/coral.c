/* Face 11, Coral (dw/faces.h; dw wave 1 Task 14, spec sections 10, 11 and 12), from faces-modern.js cdBuild,
 * rotorColon and modern-digital (frame, sleepFrame).
 *
 * Layers (tools/export/dw_layers.json): dw_k11_base (the sixty hex cells' outlines, every fifth larger, and the two
 * coral struts each side, RGB565), dw_k11_dig (the designers' 112 px gold digits with their glow, drawn in fixed 66 px
 * cells, WD10), dw_k11_gdig (the glance's 100 px digits, alpha only), dw_k11_day and dw_k11_ap (the day line and
 * AM / PM, alpha only, tinted). Drawn here: the seconds' hex
 * cells (raster hexagons at each cell's own place and turn; the current one's teal glow from glow_lut's r 18), the
 * rotor colon (its glow from glow_lut's r 60, the glass, the ochre caps, the slug and the three teal threads: rects and
 * one vertical ramp), the circle script under-line (the shared numerals, dw/circles.h, live) and the glance.
 *
 * Motion at rest stays in regions (spec section 11): the colon's slug bobs on a 2.2 s breath (dw_coral_slug, read at
 * the time of day mod 2.2 s so every frame is a function of its inputs), its glow breathing with it, inside
 * DW_K11_COLON (120 x 120) at cadence_ms 50; each second one cell redraws: the previous fills gold, the current glows
 * teal (two 37 px boxes); at :00 they reset with the minute's full frame. In the glance nothing moves within a minute.
 * The wake (the face's own: no doors): the hex ring and struts fade in over DW_K11_WAKE_MS (easeOut3). Reduced motion
 * (faces-modern.js motion false): the slug rests in the middle.
 * The under-line is the time again in the numerals, following the shared 12/24 h like the digits beside it (spec
 * section 12 [call]); no strut or cell comes near it (test_coral_underline_no_touch). Unset: -- and --, no day, four
 * empty rings ("time not known", spec section 12), no cells, the slug at rest. Stale: the day line reads NOT SYNCED in
 * amber.
 * Pure: no libm trig (fk_sincos), no allocation. */
#include <math.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/faces.h"
#include "dw/circles.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define GOLD RGB(0xe0, 0xc6, 0x97)        /* CORAL.gold; the gold cells at .55, the day at .85, AM / PM at .6 */
#define OCHRE RGB(0xb4, 0x8d, 0x4b)
#define TEAL RGB(0x3f, 0xe0, 0xc0)        /* CORAL.teal (63, 224, 192) */
#define TEAL_HI RGB(0xb8, 0xff, 0xf0)
#define AMBER RGB(0xf2, 0xa3, 0x3a)
#define INK8 RGB(194, 170, 118)           /* G.ink #F3D594 at .8 over black: numerals dim by colour, never alpha */
#define CELL_R 214.f
#define COLON_X 233
#define COLON_Y 237                       /* rotorColon(ctx, cx, cy + 4) */
#define DIG_BASE 273                      /* cy + 40; two 66 px cells centred at cx - 82 (hours), cx + 82 (minutes) */

const omni_rect_t DW_K11_COLON = {COLON_X - 60, COLON_Y - 60, COLON_X + 60, COLON_Y + 60};
const omni_rect_t DW_K11_STALE = {100, 118, 366, 146};
/* cdBuild: moveTo(cx + s 150, 390) bezierCurveTo(cx + s 205, 300, cx + s 160, 200, cx + s 196, 90), width 7; and
 * moveTo(cx + s 172, 330) bezierCurveTo(cx + s 140, 260, cx + s 188, 210, cx + s 168, 150), width 3; s = -1, 1 */
const dw_bezier_t DW_K11_STRUTS[DW_K11_STRUT_N] = {
    {83.f, 390.f, 28.f, 300.f, 73.f, 200.f, 37.f, 90.f, 7.f},
    {383.f, 390.f, 438.f, 300.f, 393.f, 200.f, 429.f, 90.f, 7.f},
    {61.f, 330.f, 93.f, 260.f, 45.f, 210.f, 65.f, 150.f, 3.f},
    {405.f, 330.f, 373.f, 260.f, 421.f, 210.f, 401.f, 150.f, 3.f},
};

typedef struct {
    bool base_ok, dig_ok, gdig_ok, day_ok, ap_ok;
    fk_img_t base, dig_img, gdig_img, day_img, ap_img;
    fk_atlas_t dig, gdig, day, ap;
} k11_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ease_out3(float x) { const float u = 1 - clampf(x, 0, 1); return 1 - u * u * u; }

float dw_coral_slug(uint32_t ms_in_2200)
{
    float s, c;
    fk_sincos((float)(ms_in_2200 % 2200u) / 2200.f * 2 * PI_F, &s, &c);
    return s;
}

/* the hex ring's level in the wake (frame's f): easeOut3 over DW_K11_WAKE_MS, 255 settled or under reduced motion */
static uint8_t fade8(const fk_face_in_t *in)
{
    if (in->reduced || in->wake_ms == UINT32_MAX) return 255;
    return (uint8_t)(ease_out3((float)in->wake_ms / (float)DW_K11_WAKE_MS) * 255.f + 0.5f);
}

/* the slug's bob for in: the time of day mod 2.2 s; at rest (0) unset or under reduced motion */
static float bob_of(const fk_face_in_t *in)
{
    if (!in->clk.set || in->reduced) return 0.f;
    const uint32_t t = ((in->clk.h * 60u + in->clk.m) * 60u + in->clk.s) * 1000u + in->clk.ms;
    return dw_coral_slug(t % 2200u);
}

void dw_k11_cell_xy(int i, float *x, float *y)
{
    float s, c;
    fk_sincos((float)i * (2 * PI_F / 60.f), &s, &c);
    *x = C + CELL_R * s, *y = C - CELL_R * c;
}

uint8_t dw_k11_cell_state(const fk_face_in_t *in, int i)
{
    if (!in->clk.set || i < 0 || i > in->clk.s) return 0;
    return i == in->clk.s ? 2 : 1;
}

void dw_k11_underline(const fk_face_in_t *in, int digits[4])
{
    if (!in->clk.set) {
        for (int i = 0; i < 4; i++) digits[i] = DW_G_EMPTY;
        return;
    }
    const int h = in->h24 ? in->clk.h % 24 : (in->clk.h % 24 + 11) % 12 + 1, m = in->clk.m % 60;
    digits[0] = h / 10, digits[1] = h % 10, digits[2] = m / 10, digits[3] = m % 10;
}

void dw_k11_underline_xy(int i, float *x, float *y)
{
    /* rowPos(cx, cy + 112, 10, 4, 12): step 2.75 r, the gap between the pairs 12 px */
    const float step = 2.75f * DW_K11_G_R, total = step * 3.f + 12.f;
    *x = C - total / 2 + step * (float)i + (i >= 2 ? 12.f : 0.f);
    *y = C + 112.f;
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool k11_bind(void *ctx, fk_asset_fn get)
{
    k11_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->base_ok = img(get, ASSET_DW_K11_BASE, &k->base, FK_CF_RGB565, W, W);
    k->dig_ok = img(get, ASSET_DW_K11_DIG, &k->dig_img, FK_CF_RGB565A8, ASSET_DW_K11_DIG_W, ASSET_DW_K11_DIG_H);
    k->gdig_ok = img(get, ASSET_DW_K11_GDIG, &k->gdig_img, FK_CF_A8, ASSET_DW_K11_GDIG_W, ASSET_DW_K11_GDIG_H);
    k->day_ok = img(get, ASSET_DW_K11_DAY, &k->day_img, FK_CF_A8, ASSET_DW_K11_DAY_W, ASSET_DW_K11_DAY_H);
    k->ap_ok = img(get, ASSET_DW_K11_AP, &k->ap_img, FK_CF_A8, ASSET_DW_K11_AP_W, ASSET_DW_K11_AP_H);
    k->dig = (fk_atlas_t){&k->dig_img, DW_CELLS_DW_K11_DIG, DW_GLYPHS_DW_K11_DIG, ASSET_DW_K11_DIG_LINE_H};
    k->gdig = (fk_atlas_t){&k->gdig_img, DW_CELLS_DW_K11_GDIG, DW_GLYPHS_DW_K11_GDIG, ASSET_DW_K11_GDIG_LINE_H};
    k->day = (fk_atlas_t){&k->day_img, DW_CELLS_DW_K11_DAY, DW_GLYPHS_DW_K11_DAY, ASSET_DW_K11_DAY_LINE_H};
    k->ap = (fk_atlas_t){&k->ap_img, DW_CELLS_DW_K11_AP, DW_GLYPHS_DW_K11_AP, ASSET_DW_K11_AP_LINE_H};
    return k->base_ok && k->dig_ok && k->gdig_ok && k->day_ok && k->ap_ok;
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

static void rect(uint16_t *fb, int stride, const omni_rect_t *clip, float x0, float y0, float x1, float y1,
                 uint16_t rgb, uint8_t a8)
{
    const omni_pt_t q[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, clip, q, 4, rgb, a8);
}

/* hexPath(x, y, r, rot) filled: vertices at rot + k 60 degrees, rot the cell's own angle (i / 60 of a turn from 3
 * o'clock less a quarter) */
static void hex(uint16_t *fb, int stride, const omni_rect_t *clip, int i, float r, uint16_t rgb, uint8_t a8)
{
    float x, y;
    dw_k11_cell_xy(i, &x, &y);
    omni_pt_t p[6];
    for (int k = 0; k < 6; k++) {   /* rot + k 60 deg, rot = theta - 90 deg: (sin, -cos) of theta + k 60 deg */
        float s, c;
        fk_sincos((float)i * (2 * PI_F / 60.f) + (float)k * (PI_F / 3.f), &s, &c);
        p[k] = (omni_pt_t){x + r * s, y - r * c};
    }
    omni_fill_poly(fb, stride, clip, p, 6, rgb, a8);
}

/* cell i's box: its teal glow (r 18 about the rounded centre) and every hexagon it can show */
static void cell_rect(int i, omni_rect_t *out)
{
    float x, y;
    dw_k11_cell_xy(i % 60, &x, &y);
    const int cx = (int)floorf(x + 0.5f), cy = (int)floorf(y + 0.5f);
    *out = (omni_rect_t){(int16_t)(cx - 18), (int16_t)(cy - 18), (int16_t)(cx + 19), (int16_t)(cy + 19)};
}

static void cells(const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!in->clk.set) return;
    const int s = in->clk.s % 60;
    const uint8_t *lut = dw_glow_lut(18);
    for (int i = 0; i <= s; i++) {
        omni_rect_t bb;
        cell_rect(i, &bb);
        if (!meets(clip, bb)) continue;
        if (i == s) {   /* the current second: the teal glow ('lighter'), then the cell in tealHi */
            float x, y;
            dw_k11_cell_xy(i, &x, &y);
            if (lut) fk_glow_add_lut(fb, stride, &clip, (int)floorf(x + 0.5f), (int)floorf(y + 0.5f), 18, lut, TEAL, 230);
            hex(fb, stride, &clip, i, i % 5 ? 6.5f : 8.f, TEAL_HI, 255);
        } else {
            hex(fb, stride, &clip, i, i % 5 ? 5.5f : 7.f, GOLD, 140);
        }
    }
}

/* rotorColon(): the teal glow at .55 x (.55 + .25 of the bob's level), the glass (strokeRect 12 x 70, width 1.2), the
 * ochre caps, the slug (a teal ramp 32 px tall, 18 px x bob from the middle) and the three threads */
static void colon(const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!meets(clip, DW_K11_COLON)) return;
    const float bob = bob_of(in), x = (float)COLON_X, y = (float)COLON_Y;
    const uint8_t *lut = dw_glow_lut(60);
    if (lut)
        fk_glow_add_lut(fb, stride, &clip, COLON_X, COLON_Y, 60, lut, TEAL,
                        (uint8_t)(255.f * 0.55f * (0.55f + 0.25f * (bob * 0.5f + 0.5f)) + 0.5f));
    const uint16_t GLASS = TEAL_HI;
    const uint8_t ga = 140;   /* rgba(184, 255, 240, .55) */
    rect(fb, stride, &clip, x - 6.6f, y - 35.6f, x + 6.6f, y - 34.4f, GLASS, ga);
    rect(fb, stride, &clip, x - 6.6f, y + 34.4f, x + 6.6f, y + 35.6f, GLASS, ga);
    rect(fb, stride, &clip, x - 6.6f, y - 34.4f, x - 5.4f, y + 34.4f, GLASS, ga);
    rect(fb, stride, &clip, x + 5.4f, y - 34.4f, x + 6.6f, y + 34.4f, GLASS, ga);
    rect(fb, stride, &clip, x - 10.f, y - 40.f, x + 10.f, y - 36.f, OCHRE, 255);
    rect(fb, stride, &clip, x - 10.f, y + 36.f, x + 10.f, y + 40.f, OCHRE, 255);
    const float sy = y + bob * 18.f;   /* the slug: x - 4 .. x + 4, alpha 0 at sy -+ 16, 1 at sy */
    for (int py = (int)floorf(sy - 16.f); py <= (int)ceilf(sy + 16.f); py++) {
        if (py < clip.y0 || py >= clip.y1) continue;
        const float t = ((float)py + 0.5f - (sy - 16.f)) / 32.f, a = 1.f - fabsf(2.f * t - 1.f);
        if (a <= 0) continue;
        const int a8 = (int)(a * 255.f + 0.5f);
        for (int px = COLON_X - 4; px < COLON_X + 4; px++) {
            if (px < clip.x0 || px >= clip.x1) continue;
            uint16_t *d = fb + (py - clip.y0) * stride + (px - clip.x0);
            *d = omni_blend565(*d, TEAL_HI, a8);
        }
    }
    for (int k = -1; k <= 1; k++)
        rect(fb, stride, &clip, x - 0.6f + 3.f * (float)k, y - 33.f, x + 0.6f + 3.f * (float)k, y + 33.f, TEAL, 128);
}

static const char *const DAY[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};

/* the hours ("10", " 9" in 12 h; "09" in 24 h) and the minutes as drawCellsText's 66 px cells */
static void hm(const fk_face_in_t *in, char h[3], char m[3])
{
    if (!in->clk.set) {
        memcpy(h, "--", 3), memcpy(m, "--", 3);
        return;
    }
    const unsigned hh = in->h24 ? in->clk.h % 24u : (in->clk.h % 24u + 11) % 12 + 1, mm = in->clk.m % 60u;
    h[0] = hh < 10 ? (in->h24 ? '0' : ' ') : (char)('0' + hh / 10), h[1] = (char)('0' + hh % 10), h[2] = 0;
    m[0] = (char)('0' + mm / 10), m[1] = (char)('0' + mm % 10), m[2] = 0;
}

static void k11_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const k11_t *k = ctx;
    fill(fb, stride, clip, 0);
    if (k->base_ok) fk_blit(fb, stride, clip, &k->base, 0, 0, fade8(in));
    cells(in, clip, fb, stride);
    if (in->clk.set && k->day_ok && meets(clip, DW_K11_STALE)) {   /* "SATURDAY  24", or NOT SYNCED in amber */
        char t[16];
        if (in->clk.stale) {
            memcpy(t, "NOT SYNCED", 11);
        } else {
            const char *d = in->clk.wday < 7 ? DAY[in->clk.wday] : "";
            const size_t n = strlen(d);
            const unsigned md = in->clk.mday > 99 ? 99u : in->clk.mday;
            memcpy(t, d, n);
            t[n] = ' ', t[n + 1] = ' ';
            char *p = t + n + 2;
            if (md >= 10) *p++ = (char)('0' + md / 10);
            *p++ = (char)('0' + md % 10);
            *p = 0;
        }
        fk_atlas_text_tracked(fb, stride, clip, &k->day, t, C, 140, 3.f, true, in->clk.stale ? AMBER : GOLD, 217);
    }
    if (k->dig_ok) {
        char h[3], m[3];
        hm(in, h, m);
        fk_atlas_text(fb, stride, clip, &k->dig, h, 233 - 82 - 66, DIG_BASE, 66, 255);
        fk_atlas_text(fb, stride, clip, &k->dig, m, 233 + 82 - 66, DIG_BASE, 66, 255);
    }
    if (in->clk.set && !in->h24 && k->ap_ok)
        fk_atlas_text_tracked(fb, stride, clip, &k->ap, in->clk.h < 12 ? "AM" : "PM", C, 299, 2.f, true, GOLD, 153);
    int g[4];
    dw_k11_underline(in, g);
    for (int i = 0; i < 4; i++) {
        float x, y;
        dw_k11_underline_xy(i, &x, &y);
        omni_rect_t bb;
        dw_glyph_bbox(x, y, DW_K11_G_R, &bb);
        if (meets(clip, bb)) dw_glyph_draw(fb, stride, &clip, g[i], x, y, DW_K11_G_R, INK8, 255, 1.f);
    }
    colon(in, clip, fb, stride);
}

/* sleepFrame(): the time in dim gold (#4E4129; the designers' 100 px in 60 px cells, dw_k11_gdig) and the colon as a
 * dark teal bar (#1F5248), on the glance orbit; unset --:--. The mockup's teal cell for the five-second sector is left
 * out: the glance draws once a minute (W5). */
static void k11_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const k11_t *k = ctx;
    fill(fb, stride, clip, 0);
    const int ox = in->odx, oy = in->ody, base = 233 + 36 + oy;
    const uint16_t ink = RGB(0x4e, 0x41, 0x29);
    if (!k->gdig_ok) return;
    if (!in->clk.set) {
        fk_atlas_text_rgb(fb, stride, clip, &k->gdig, "--:--", 233 + ox - 150, base, 60, ink, 255, 0);
        return;
    }
    char h[3], m[3];
    hm(in, h, m);
    fk_atlas_text_rgb(fb, stride, clip, &k->gdig, h, 233 - 78 - 60 + ox, base, 60, ink, 255, 0);
    fk_atlas_text_rgb(fb, stride, clip, &k->gdig, m, 233 + 78 - 60 + ox, base, 60, ink, 255, 0);
    rect(fb, stride, &clip, 231.f + (float)ox, 215.f + (float)oy, 235.f + (float)ox, 251.f + (float)oy,
         RGB(0x1f, 0x52, 0x48), 255);
    if (in->clk.stale)   /* the stale lamp, dim, where the day line's NOT SYNCED is awake: inside its rect */
        fk_disc(fb, stride, &clip, 233.f + (float)ox, 132.f + (float)oy, 2.2f, RGB(96, 64, 22), 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t k11_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced || a->h24 != b->h24)
        return full(out);   /* the mode, the state, the orbit, reduced motion, the hours */
    if (b->clk.set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday ||
                       a->clk.wday != b->clk.wday))
        return full(out);   /* the minute: the digits, the under-line, the day, the cells' reset */
    if (b->glance) return 0;   /* nothing moves in the glance */
    if (fade8(a) != fade8(b)) return full(out);   /* the wake's fade */
    uint8_t n = 0;
    if (bob_of(a) != bob_of(b)) out[n++] = DW_K11_COLON;
    if (b->clk.set && a->clk.s != b->clk.s) {
        if (b->clk.s != a->clk.s + 1) return full(out);   /* a skipped or stepped-back second: every cell between */
        cell_rect(a->clk.s, &out[n++]);
        cell_rect(b->clk.s, &out[n++]);
    }
    return n;
}

const fk_face_ops_t DW_FACE_CORAL = {
    .key = "coral",
    .ctx_bytes = sizeof(k11_t),
    .cadence_ms = 50,
    .bind = k11_bind,
    .draw = k11_draw,
    .glance = k11_glance,
    .dirty = k11_dirty,
    .tap = NULL,
    .reveal = false,
    .wake_len_ms = DW_K11_WAKE_MS,
};
