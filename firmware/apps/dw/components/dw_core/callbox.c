/* Face 5, Call box digital (dw/faces.h; dw wave 1 Task 13, spec sections 10, 11 and 14), from faces-classic.js
 * boxFrame, boxSign, frostedPane, panes and classic-digital (frame, sleepFrame).
 *
 * Layers (tools/export/dw_layers.json): dw_b5_box, the box front: the one #5 asset the art sets swap (spec section 14,
 * R132): the generic front (dw_generic.js, in this repo) or, in the private build, the overlay's; the panes and the
 * lamp sit on assets_meta.h's pane rectangles and lens in both (D6). The rest is shared: dw_b5_dig (the sign's 88 px
 * digits with their bloom, drawn in fixed cells), dw_b5_date and dw_b5_word (the stacked date and words with theirs;
 * the 15 px caps folded into 16, D14), dw_b5_note and dw_b5_serif (the plaque's caps and its serif line, alpha only,
 * tinted here), dw_b5_pane (a lit pane, cut from the designers' frame) and dw_b5_lamp (the lamp's glow, alpha only).
 * Drawn here: the sign's text, the panes, the lamp and its lens, the notice plaque, the glance. The sign never says the
 * word the real one does: it holds the time, the date and the day (spec section 14).
 *
 * Motion at rest stays in regions (spec section 11): pane i lights at i x 5 s, fading in over 0.4 s (its window's
 * rect); at :00 they go dark together with the minute's full frame; the lamp pulses on the hour, after a wake and
 * after a tap on it (FK_TAP_PULSE; its rect); the plaque's battery and sun lines (its rect). cadence_ms 40 runs frames
 * only while one of those moves. Under reduced motion (the final review, M1) a pane lights at once, with no fade, and
 * the lamp has no hourly pulse; the wake's pulses are already skipped (face_generic starts no own wake under reduced
 * motion). A tap's pulse still plays: it answers the tap. In the glance nothing moves within a minute.
 * The wake (wake_ms < UINT32_MAX, the face's own: no doors): the sign flickers on, the panes come up one after another,
 * the lamp pulses three times; by DW_B5_WAKE_MS it is the settled frame exactly. The sign's flicker is its glyphs
 * drawn at the sign's level (the mockup fades the sign's whole layer: where two glyphs' blooms meet it differs).
 * Pure: no libm trig (fk_sincos), no allocation. */
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/faces.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define CREAM RGB(0xf2, 0xea, 0xd8)
#define LAMP RGB(0xff, 0xf4, 0xd6)
#define AMBER RGB(0xf2, 0xa3, 0x3a)
#define PANE_DARK RGB(5, 8, 15)       /* frostedPane()'s unlit #05080f */
#define SIGN_Y 182                    /* the time's baseline; its cells (50 px, the colon 24) centred at 235 */
#define SIGN_CX 235

const omni_rect_t DW_B5_SIGN = {40, 90, 426, 212};
const omni_rect_t DW_B5_LAMP = {187, 11, 279, 103};
const omni_rect_t DW_B5_PLAQUE = {92, 312, 208, 388};
const omni_rect_t DW_B5_STALE = {92, 362, 208, 388};

typedef struct {
    bool box_ok, dig_ok, date_ok, word_ok, note_ok, serif_ok, pane_ok, lamp_ok;
    fk_img_t box, dig_img, date_img, word_img, note_img, serif_img, pane, lamp;
    fk_atlas_t dig, date, word, note, serif;
} b5_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

/* ---- the moving parts' levels */
uint8_t dw_b5_panes_lit(const fk_face_in_t *in)
{
    if (!in->clk.set) return 0;
    const int n = in->clk.s / 5 + 1;
    return (uint8_t)(n > DW_B5_PANE_N ? DW_B5_PANE_N : n);
}

uint8_t dw_b5_pane_level(const fk_face_in_t *in, int i)
{
    if (!in->clk.set || i < 0 || i >= DW_B5_PANE_N) return 0;
    const float sec = (float)in->clk.s + (float)in->clk.ms / 1000.f, on = 5.f * (float)i;
    float L = sec >= on ? (in->reduced ? 1.f : clampf((sec - on) / 0.4f, 0, 1)) : 0;   /* reduced: no fade (M1) */
    if (in->wake_ms != UINT32_MAX && in->wake_ms < 1200)   /* the wake: one after another */
        L *= clampf(((float)in->wake_ms - 500.f - 30.f * (float)i) / 300.f, 0, 1);
    return (uint8_t)(L * 255.f + 0.5f);
}

static float pulse3(uint32_t t)   /* three slow pulses: 0.28 + 0.72 sin^2 over 900 ms each */
{
    float s, c;
    fk_sincos((float)t / 900.f * PI_F, &s, &c);
    return 0.28f + 0.72f * s * s;
}

uint8_t dw_b5_lamp_level(const fk_face_in_t *in)
{
    if (!in->clk.set) return (uint8_t)(0.18f * 255.f + 0.5f);
    float L = 0.28f;
    if (in->wake_ms != UINT32_MAX && in->wake_ms < 3 * 900) {
        const float p = pulse3(in->wake_ms);
        if (p > L) L = p;
    }
    if (in->pulsing && in->pulse_ms < 3 * 900) {
        const float p = pulse3(in->pulse_ms);
        if (p > L) L = p;
    }
    if (!in->reduced && in->clk.m == 0 && in->clk.s < 2) {   /* once on the hour; not under reduced motion (M1) */
        float s, c;
        fk_sincos((float)(in->clk.s * 1000 + in->clk.ms) / 2000.f * PI_F, &s, &c);
        const float p = 0.28f + 0.72f * s;
        if (p > L) L = p;
    }
    return (uint8_t)(clampf(L, 0, 1) * 255.f + 0.5f);
}

/* the sign's level in the wake: off, then a flicker on (dark at 380-450 ms) */
static uint8_t sign_level(const fk_face_in_t *in)
{
    if (in->wake_ms == UINT32_MAX || in->wake_ms >= 900) return 255;
    const float t = (float)in->wake_ms;
    float a = t < 250 ? 0 : clampf((t - 250) / 500.f, 0, 1);
    if (t > 380 && t < 450) a *= 0.35f;
    return (uint8_t)(a * 255.f + 0.5f);
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool b5_bind(void *ctx, fk_asset_fn get)
{
    b5_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->box_ok = img(get, ASSET_DW_B5_BOX, &k->box, FK_CF_RGB565, ASSET_DW_B5_BOX_W, ASSET_DW_B5_BOX_H);
    k->dig_ok = img(get, ASSET_DW_B5_DIG, &k->dig_img, FK_CF_RGB565A8, ASSET_DW_B5_DIG_W, ASSET_DW_B5_DIG_H);
    k->date_ok = img(get, ASSET_DW_B5_DATE, &k->date_img, FK_CF_RGB565A8, ASSET_DW_B5_DATE_W, ASSET_DW_B5_DATE_H);
    k->word_ok = img(get, ASSET_DW_B5_WORD, &k->word_img, FK_CF_RGB565A8, ASSET_DW_B5_WORD_W, ASSET_DW_B5_WORD_H);
    k->note_ok = img(get, ASSET_DW_B5_NOTE, &k->note_img, FK_CF_A8, ASSET_DW_B5_NOTE_W, ASSET_DW_B5_NOTE_H);
    k->serif_ok = img(get, ASSET_DW_B5_SERIF, &k->serif_img, FK_CF_A8, ASSET_DW_B5_SERIF_W, ASSET_DW_B5_SERIF_H);
    k->pane_ok = img(get, ASSET_DW_B5_PANE, &k->pane, FK_CF_RGB565, DW_B5_PANES[0][2], DW_B5_PANES[0][3]);
    k->lamp_ok = img(get, ASSET_DW_B5_LAMP, &k->lamp, FK_CF_A8, ASSET_DW_B5_LAMP_W, ASSET_DW_B5_LAMP_H);
    k->dig = (fk_atlas_t){&k->dig_img, DW_CELLS_DW_B5_DIG, DW_GLYPHS_DW_B5_DIG, ASSET_DW_B5_DIG_LINE_H};
    k->date = (fk_atlas_t){&k->date_img, DW_CELLS_DW_B5_DATE, DW_GLYPHS_DW_B5_DATE, ASSET_DW_B5_DATE_LINE_H};
    k->word = (fk_atlas_t){&k->word_img, DW_CELLS_DW_B5_WORD, DW_GLYPHS_DW_B5_WORD, ASSET_DW_B5_WORD_LINE_H};
    k->note = (fk_atlas_t){&k->note_img, DW_CELLS_DW_B5_NOTE, DW_GLYPHS_DW_B5_NOTE, ASSET_DW_B5_NOTE_LINE_H};
    k->serif = (fk_atlas_t){&k->serif_img, DW_CELLS_DW_B5_SERIF, DW_GLYPHS_DW_B5_SERIF, ASSET_DW_B5_SERIF_LINE_H};
    return k->box_ok && k->dig_ok && k->date_ok && k->word_ok && k->note_ok && k->serif_ok && k->pane_ok && k->lamp_ok;
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

/* canvas strokeRect(x, y, w, h) at lineWidth 1: four 1 px bands centred on the edges */
static void stroke_rect(uint16_t *fb, int stride, const omni_rect_t *clip, float x, float y, float w, float h,
                        uint16_t rgb, uint8_t a8)
{
    rect(fb, stride, clip, x - 0.5f, y - 0.5f, x + w + 0.5f, y + 0.5f, rgb, a8);
    rect(fb, stride, clip, x - 0.5f, y + h - 0.5f, x + w + 0.5f, y + h + 0.5f, rgb, a8);
    rect(fb, stride, clip, x - 0.5f, y + 0.5f, x + 0.5f, y + h - 0.5f, rgb, a8);
    rect(fb, stride, clip, x + w - 0.5f, y + 0.5f, x + w + 0.5f, y + h - 0.5f, rgb, a8);
}

/* v in decimal, at least min digits (zero-padded), at p; returns the end (NUL written) */
static char *put_u(char *p, unsigned v, int min)
{
    char t[10];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while ((v || n < min) && n < 10);
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

static char *put_s(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

/* the sign's time: "10:09" (12 h without a leading zero) or "03:47" (24 h) */
static void hm_text(const fk_face_in_t *in, char out[6])
{
    const unsigned h = in->h24 ? in->clk.h % 24u : (in->clk.h % 24u + 11) % 12 + 1, m = in->clk.m % 60u;
    char *p = put_u(out, h, in->h24 ? 2 : 1);
    *p++ = ':';
    put_u(p, m, 2);
}

/* cells(): fixed cells (50 px, the colon 24) centred at cx, each glyph centred in its cell; tinted (a glance, the
 * unset sign: the glyphs' bloom cut below lo) or in the atlas's colour */
static void cells(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *t, int cx, int base,
                  bool tint, uint16_t rgb, uint8_t opa)
{
    int w = 0;
    for (const char *p = t; *p; p++) w += *p == ':' ? 24 : 50;
    int px = cx - w / 2;
    for (const char *p = t; *p; p++) {
        const int cw = *p == ':' ? 24 : 50;
        const char one[2] = {*p, 0};
        if (tint) fk_atlas_text_rgb(fb, stride, clip, at, one, px, base, cw, rgb, opa, 96);
        else fk_atlas_text(fb, stride, clip, at, one, px, base, cw, opa);
        px += cw;
    }
}

static const char MON[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char DOW[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

/* boxSign(): the time in the lit sign, the date and month stacked on the left, the day and AM/PM (HRS in 24 h) on the
 * right; unset, --:-- and NO / TIME */
static void sign(const b5_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const uint8_t a = sign_level(in);
    if (!a || !meets(clip, DW_B5_SIGN)) return;
    if (!in->clk.set) {
        cells(fb, stride, clip, &k->dig, "--:--", SIGN_CX - 2, SIGN_Y, true, LAMP, (uint8_t)(107 * a / 255));
        fk_atlas_text_tracked(fb, stride, clip, &k->word, "NO", 82.f, 150, 2.f, false, 0, (uint8_t)(128 * a / 255));
        fk_atlas_text_tracked(fb, stride, clip, &k->word, "TIME", 82.f, 172, 2.f, false, 0, (uint8_t)(128 * a / 255));
        return;
    }
    char t[6];
    hm_text(in, t);
    cells(fb, stride, clip, &k->dig, t, SIGN_CX, SIGN_Y, false, 0, a);
    char dl[4];
    put_u(dl, in->clk.mday > 99 ? 99u : in->clk.mday, 1);
    fk_atlas_text_tracked(fb, stride, clip, &k->date, dl, 80.f, 151, 1.f, false, 0, a);
    fk_atlas_text_tracked(fb, stride, clip, &k->word, in->clk.mon < 12 ? MON[in->clk.mon] : "", 80.f, 174, 2.5f, false,
                          0, a);
    fk_atlas_text_tracked(fb, stride, clip, &k->word, in->clk.wday < 7 ? DOW[in->clk.wday] : "", 386.f, 151, 2.5f,
                          false, 0, a);
    fk_atlas_text_tracked(fb, stride, clip, &k->word, in->h24 ? "HRS" : in->clk.h < 12 ? "AM" : "PM", 386.f, 174, 2.5f,
                          false, 0, (uint8_t)(204 * a / 255));
}

/* the lamp: its glow (P.lamp by dw_b5_lamp's alpha at the level), the frosted lens (a horizontal gradient: P.lamp at
 * .25 + .5 L, white at .35 + .65 L in the middle) and its dark middle bar */
static void lamp(const b5_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!meets(clip, DW_B5_LAMP)) return;
    const uint8_t l8 = dw_b5_lamp_level(in);
    const float L = (float)l8 / 255.f;
    fk_blit_rgb(fb, stride, clip, &k->lamp, 233 - k->lamp.w / 2, 57 - k->lamp.h / 2, LAMP, l8);
    const float ae = 0.25f + 0.5f * L, am = 0.35f + 0.65f * L;
    for (int x = DW_B5_LENS_X; x < DW_B5_LENS_X + DW_B5_LENS_W; x++) {
        if (x < clip.x0 || x >= clip.x1) continue;
        const float u = ((float)x + 0.5f - 226.f) / 14.f, t = 1.f - (u > 0.5f ? 2.f * u - 1.f : 1.f - 2.f * u);
        const uint16_t col = omni_pack565(255, (int)(244.f + 11.f * t + 0.5f), (int)(214.f + 41.f * t + 0.5f));
        const int al = (int)((ae + (am - ae) * t) * 255.f + 0.5f);
        for (int y = DW_B5_LENS_Y; y < DW_B5_LENS_Y + DW_B5_LENS_H; y++) {
            if (y < clip.y0 || y >= clip.y1) continue;
            uint16_t *d = fb + (y - clip.y0) * stride + (x - clip.x0);
            *d = omni_blend565(*d, col, al);
        }
    }
    rect(fb, stride, &clip, 232.5f, (float)DW_B5_LENS_Y, 233.5f, (float)(DW_B5_LENS_Y + DW_B5_LENS_H), RGB(14, 26, 56),
         230);
}

/* fmtShort(): "7:51 AM" or "19:51" */
static void short_time(unsigned h, unsigned m, bool h24, char out[10])
{
    char *p = put_u(out, h24 ? h % 24 : (h % 24 + 11) % 12 + 1, h24 ? 2 : 1);
    *p++ = ':';
    p = put_u(p, m % 60, 2);
    if (!h24) put_s(p, h % 24 < 12 ? " AM" : " PM");
}

/* the notice plaque on the left door: the next sun event and the battery; unset, how to set the time */
static void plaque(const b5_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!meets(clip, DW_B5_PLAQUE)) return;
    rect(fb, stride, &clip, 92.f, 312.f, 208.f, 388.f, RGB(0x0b, 0x0a, 0x08), 255);
    stroke_rect(fb, stride, &clip, 92.5f, 312.5f, 115.f, 75.f, CREAM, 115);
    stroke_rect(fb, stride, &clip, 95.5f, 315.5f, 109.f, 69.f, CREAM, 46);
    const float pcx = 150.f;
    if (!in->clk.set) {
        fk_atlas_text_tracked(fb, stride, clip, &k->note, "TIME NOT SET", pcx, 336, 1.5f, true, CREAM, 204);
        fk_atlas_text_tracked(fb, stride, clip, &k->serif, "Join Wi-Fi", pcx, 358, 0.f, true, CREAM, 204);
        fk_atlas_text_tracked(fb, stride, clip, &k->note, "HOLD TO SET UP", pcx, 376, 1.5f, true, CREAM, 140);
        return;
    }
    const char *ev = in->sun_kind == 1 ? "SUNRISE" : in->sun_kind == 2 ? "SUNSET" : "SUN";
    fk_atlas_text_tracked(fb, stride, clip, &k->note, ev, pcx, 334, 2.f, true, CREAM, 191);
    char at[10];
    if (in->sun_kind) short_time(in->sun_h, in->sun_m, in->h24, at);
    else memcpy(at, "--:--", 6);
    fk_atlas_text_tracked(fb, stride, clip, &k->serif, at, pcx, 354, 1.f, true, CREAM, 242);
    if (in->clk.stale) {
        fk_atlas_text_tracked(fb, stride, clip, &k->note, "NOT SYNCED", pcx, 376, 1.5f, true, AMBER, 242);
    } else {
        char b[16];
        char *p = put_s(b, "BATTERY ");
        if (in->battery < 0) put_s(p, "--");
        else put_s(put_u(p, in->battery > 100 ? 100u : (unsigned)in->battery, 1), "%");
        fk_atlas_text_tracked(fb, stride, clip, &k->note, b, pcx, 376, 1.5f, true, CREAM, 140);
    }
}

static void b5_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const b5_t *k = ctx;
    if (k->box_ok) fk_blit(fb, stride, clip, &k->box, 0, 0, 255);
    else fill(fb, stride, clip, 0);
    sign(k, in, clip, fb, stride);
    if (k->lamp_ok) lamp(k, in, clip, fb, stride);
    for (int i = 0; i < DW_B5_PANE_N; i++) {   /* the panes: dark, or the lit pane at its level (frostedPane's L) */
        const int x = DW_B5_PANES[i][0], y = DW_B5_PANES[i][1], w = DW_B5_PANES[i][2], h = DW_B5_PANES[i][3];
        if (!meets(clip, (omni_rect_t){(int16_t)x, (int16_t)y, (int16_t)(x + w), (int16_t)(y + h)})) continue;
        rect(fb, stride, &clip, (float)x, (float)y, (float)(x + w), (float)(y + h), PANE_DARK, 255);
        const uint8_t L = dw_b5_pane_level(in, i);
        if (L && k->pane_ok) fk_blit(fb, stride, clip, &k->pane, x, y, L);
    }
    plaque(k, in, clip, fb, stride);
}

/* sleepFrame(): the sign's frame, the window outlines and the lamp as dim lines, the time at 30 %, the day, date and
 * AM/PM at 26 %, on the glance orbit (the glyphs' bloom cut). Colours at most 110 per channel. */
static void b5_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const b5_t *k = ctx;
    fill(fb, stride, clip, 0);
    const float dx = (float)in->odx, dy = (float)in->ody;
    const uint16_t EDGE = RGB(0x3c, 0x5d, 0xa3);
    stroke_rect(fb, stride, &clip, 44.f + 6.5f + dx, 106.f + 6.5f + dy, 378.f - 13.f, 96.f - 13.f, EDGE, 115);
    for (int w = 0; w < 2; w++) {
        const int16_t *a = DW_B5_PANES[6 * w], *b = DW_B5_PANES[6 * w + 5];
        stroke_rect(fb, stride, &clip, (float)a[0] - 0.5f + dx, (float)a[1] - 0.5f + dy,
                    (float)(b[0] + b[2] - a[0]) + 1.f, (float)(b[1] + b[3] - a[1]) + 1.f, EDGE, 115);
    }
    rect(fb, stride, &clip, 229.f + dx, 50.f + dy, 237.f + dx, 65.f + dy, LAMP, 89);
    const int ox = in->odx, oy = in->ody;
    if (!in->clk.set) {
        cells(fb, stride, clip, &k->dig, "--:--", SIGN_CX + ox, SIGN_Y + oy, true, LAMP, 77);
        return;
    }
    char t[6];
    hm_text(in, t);
    cells(fb, stride, clip, &k->dig, t, SIGN_CX + ox, SIGN_Y + oy, true, LAMP, 77);
    char dl[4];
    put_u(dl, in->clk.mday > 99 ? 99u : in->clk.mday, 1);
    fk_atlas_text_rgb(fb, stride, clip, &k->date, dl, 80 + ox - fk_atlas_width(&k->date, dl, 0) / 2, 151 + oy, 0, LAMP,
                      66, 96);
    const char *day = in->clk.wday < 7 ? DOW[in->clk.wday] : "";
    float pen = 386.f + dx - fk_atlas_width_tracked(&k->word, day, 2.5f) / 2;
    for (const char *p = day; *p; p++) {   /* tracked, the bloom cut */
        const char one[2] = {*p, 0};
        fk_atlas_text_rgb(fb, stride, clip, &k->word, one, (int)(pen + 0.5f), 151 + oy, 0, LAMP, 66, 96);
        pen += (float)fk_atlas_width(&k->word, one, 0) + 2.5f;
    }
    if (!in->h24) {
        const char *ap = in->clk.h < 12 ? "AM" : "PM";
        pen = 386.f + dx - fk_atlas_width_tracked(&k->word, ap, 2.5f) / 2;
        for (const char *p = ap; *p; p++) {
            const char one[2] = {*p, 0};
            fk_atlas_text_rgb(fb, stride, clip, &k->word, one, (int)(pen + 0.5f), 174 + oy, 0, LAMP, 66, 96);
            pen += (float)fk_atlas_width(&k->word, one, 0) + 2.5f;
        }
    }
    if (in->clk.stale)   /* the stale lamp, dim, where the plaque's NOT SYNCED is awake: inside the marker's rect */
        fk_disc(fb, stride, &clip, 150.f + dx, 376.f + dy, 2.2f, RGB(96, 64, 22), 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t b5_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->wake_ms != b->wake_ms || a->h24 != b->h24 || a->reduced != b->reduced)
        return full(out);   /* the mode, the state, the orbit, the wake, the hours, reduced motion */
    if (!b->clk.set) return 0;   /* unset: nothing moves (the lamp rests at 0.18, the panes are dark) */
    if (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday || a->clk.mon != b->clk.mon ||
        a->clk.wday != b->clk.wday)
        return full(out);   /* the minute: the sign, the panes going dark together */
    if (b->glance) return 0;   /* the glance shows no panes, lamp level or plaque */
    uint8_t n = 0;
    if (dw_b5_lamp_level(a) != dw_b5_lamp_level(b)) out[n++] = DW_B5_LAMP;
    for (int w = 0; w < 2; w++) {
        bool moved = false;
        for (int i = 6 * w; i < 6 * w + 6 && !moved; i++) moved = dw_b5_pane_level(a, i) != dw_b5_pane_level(b, i);
        if (!moved) continue;
        const int16_t *p = DW_B5_PANES[6 * w], *q = DW_B5_PANES[6 * w + 5];
        out[n++] = (omni_rect_t){p[0], p[1], (int16_t)(q[0] + q[2]), (int16_t)(q[1] + q[3])};
    }
    if (a->battery != b->battery || a->sun_kind != b->sun_kind || a->sun_h != b->sun_h || a->sun_m != b->sun_m)
        out[n++] = DW_B5_PLAQUE;
    return n;
}

/* the lamp: a 60 px target about it, 30 px each way of (233, 55) included: the 61 px square, so its corners tap too
 * (dw wave 2 Task 2: targets are inclusive at the edge and probed at it; pointer()'s circle missed the corners) */
static fk_tap_t b5_tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= 233 - 30 && x <= 233 + 30 && y >= 55 - 30 && y <= 55 + 30 ? FK_TAP_PULSE : FK_TAP_NONE;
}

const fk_face_ops_t DW_FACE_CALLBOX = {
    .key = "callbox",
    .ctx_bytes = sizeof(b5_t),
    .cadence_ms = 40,
    .bind = b5_bind,
    .draw = b5_draw,
    .glance = b5_glance,
    .dirty = b5_dirty,
    .tap = b5_tap,
    .reveal = false,
    .wake_len_ms = DW_B5_WAKE_MS,
    .pulse_len_ms = DW_B5_PULSE_MS,
};
