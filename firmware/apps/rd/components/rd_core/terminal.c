/* Ship Terminal (`terminal`, rd/faces.h; rd wave 1 Task 11, spec section 10.7; faces-bridge.js:696-801: build(),
 * frame() and sleepFrame()).
 *
 * The housing (rd_tm_house, the mockup's build() canvas cut to its ink: A8 380 x 321 at (43, 79), each byte an index
 * into RD_TM_RAMP's four runs from black) is copied through the ramp's RGB565 table, then the PWR lamp (lamp() at
 * (388, 381), r 3.5, go at .45: its glow r 9, its lens and its steel ring). Inside the glass (64, 98, 338 x 262, r 20;
 * pixel centres inside the rounded rect, the canvas clip):
 *   the header DEEP HAUL CO. . TERM 04 (VT323 20 phosphor at .65, x the wake's brightness) from x 88, or the stale
 *   marker in its place; the spinner right-aligned at 378 (.65; '|' is two columns of light, the atlas has no 0x7C);
 *   the rule (88, 140) 290 x 1 in phosphorDim .8; the log's two lines (VT323 20 at .85) at y 160 and 182, typed; the
 *   time (VT323 132, glow plane then core) centred on 233 at y 258, the old time under it at 30 % for 120 ms after the
 *   minute; unset, rd.paintDim dashes (unsetTime()); the prompt "> " + the date (VT323 28 at .85; 12 h with its AM/PM
 *   suffix) from x 88 at y 326 and the 12 x 20 block cursor 6 px after it; the chime's band; then the scanlines, every
 *   third row from y 98 darkened to .8 (a per-row multiply).
 * The mockup's y are canvas 'middle' baselines: VT323's ascent 800 and descent 200 put the alphabetic baseline .3 em
 * below (20 px: +6, 28 px: +8, 132 px: +40); Chakra Petch's (992, 308) the middle of its em box, (992 - 308) / (992 +
 * 308) / 2 = .263 em (the stale marker's STALE: 126 middle + 0.5 + 3.4 = 129.9, its baseline 130; Task 13's check against
 * the stale shot: the .342 em of Task 11 sat it a pixel low).
 * The time and the old time are dimmed by colour, at alpha 255 (the numerals' rule).
 * Pure: no esp_*, freertos or LVGL; no libm (lesson 6); no allocation. */
#include <string.h>
#include "rd/faces.h"
#include "rd/release.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "fk/compose.h"
#include "fk/ease.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define GX 64      /* the glass (the mockup's X, Y, Wd, Ht) and its corner radius */
#define GY 98
#define GW 338
#define GH 262
#define GR 20
#define HX 43      /* rd_tm_house's crop */
#define HY 79
#define HW 380
#define HH 321
#define LX 88      /* the text's left edge (X + 24) */
#define Y_HEAD 132       /* 126 middle + 6 */
#define X_SPIN 378       /* X + Wd - 24, right-aligned */
#define Y_RULE 140
#define Y_LOG0 166       /* 160 middle + 6 */
#define Y_LOG1 188
#define Y_TIME_MID 258   /* Y + 160 */
#define Y_TIME 298       /* + 39.6 */
#define Y_PROMPT 334     /* 326 middle + 8.4 */
#define CUR_Y 316        /* the cursor: fillRect(X + 24 + pw + 6, Y + 218, 12, 20) */
#define CUR_W 12
#define CUR_H 20
#define STALE_CX 130     /* staleMark(X + 64, Y + 28): the shared marker's ring and STALE land there from (130, 130) */
#define STALE_CY 130
#define GLANCE_STALE_CX 235
#define LAMP_X 388
#define LAMP_Y 381
#define WAKE_OPEN_MS 250
#define WAKE_TYPE_MS 300   /* the log types from here in the wake */
#define WAKE_TYPED_MS 860  /* from here the wake's page shows whole: its last frame at the 40 ms cadence (PR32) */
#define CHAR_MS 25         /* 40 characters a second */
#define LINGER_MS 120

const omni_rect_t RD_TM_GLASS = {GX, GY, GX + GW, GY + GH};
const omni_rect_t RD_TM_CURSOR = {224, 312, 300, 340};
const omni_rect_t RD_TM_SPINNER = {360, 110, 390, 142};
const omni_rect_t RD_TM_LOG = {72, 142, 394, 204};
const omni_rect_t RD_TM_TIME = {84, 200, 382, 318};
const omni_rect_t RD_TM_STALE = {72, 108, 352, 140};
const omni_rect_t RD_TM_PROMPT = {72, 306, 300, 346};

/* rd_tm_house's ramp (tools/export/rd_layers.json's row names the same stops): black to phosphorDim, to steel (the
 * rim's steelDeep and the bevel's steel .8 lie on it), to steelHi, to paintDim */
const rd_ramp_stop_t RD_TM_RAMP[RD_TM_RAMP_STOPS] = {
    {0, 0x00, 0x00, 0x00},   {63, 0x2F, 0x7A, 0x48},  {64, 0x00, 0x00, 0x00},  {127, 0x5A, 0x60, 0x68},
    {128, 0x00, 0x00, 0x00}, {191, 0x8A, 0x90, 0x96}, {192, 0x00, 0x00, 0x00}, {255, 0x8C, 0x84, 0x74},
};

static const char *const PAGES[RD_TM_PAGES][2] = {
    {"CREW ON BOARD ........ 1", "CREW REQUIRED ...... 169"},
    {"LAST BACKUP ...... TUESDAY", "WHICH TUESDAY .... UNKNOWN"},
    {"DAYS WITHOUT INCIDENT .. 0", "PREVIOUS RECORD ....... 0"},
    {"DRINKS MACHINE ... JAMMED", "ENGINEER NOTIFIED .... NO"},
};
static const char *const UNSET_LINES[2] = {"NTP .......... NO CARRIER", "[ AWAITING TIME SIGNAL ]"};
#define HEADER "DEEP HAUL CO. \267 TERM 04"

typedef struct {
    fk_img_t house, ch13_img;
    fk_atlas_t ch13;
    rd_atlas2_t vt132, vt28, vt20;   /* used where filled, never copied */
    const uint8_t *g9;
    uint16_t ramp[256];
    int16_t gx0[GH], gx1[GH];        /* the glass's span on each of its rows: pixel centres inside the rounded rect */
} tm_t;

/* ---- small helpers */
static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

/* rgb565 scaled by k / 255 per 8-bit channel (a colour dimmed, drawn at alpha 255) */
static uint16_t dim565(uint16_t c, uint8_t k)
{
    return omni_pack565(omni_ex_r(c) * k / 255, omni_ex_g(c) * k / 255, omni_ex_b(c) * k / 255);
}

const char *rd_tm_page_line(unsigned page, unsigned line) { return PAGES[page % RD_TM_PAGES][line ? 1 : 0]; }

unsigned rd_tm_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_TM_GLASS;
    return 1;
}

/* ---- bind */
static bool bind(void *ctx, fk_asset_fn get)
{
    tm_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_TM_HOUSE, &n);
    bool ok = b && fk_img_from_bin(&k->house, b, n) && k->house.a && !k->house.rgb && k->house.w == HW &&
              k->house.h == HH;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    b = get(ASSET_RD_F_VT132, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt132);
    b = get(ASSET_RD_F_VT28, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt28);
    b = get(ASSET_RD_F_VT20, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt20);
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->g9 = rd_glow_lut(9);
    ok = ok && k->g9;
    rd_ramp565(RD_TM_RAMP, RD_TM_RAMP_STOPS, k->ramp);
    /* the glass's rows: in doubled integer coordinates, a pixel centre (2x + 1, 2y + 1) lies inside the rounded rect
     * when its distance from the nearest corner centre (clamped into the inner rect) is at most 2 GR */
    for (int j = 0; j < GH; j++) {
        const int y2 = 2 * (GY + j) + 1, ylo = 2 * (GY + GR), yhi = 2 * (GY + GH - GR);
        const int cy = y2 < ylo ? ylo : y2 > yhi ? yhi : y2;
        int x0 = -1, x1 = -1;
        for (int x = GX; x < GX + GW; x++) {
            const int x2 = 2 * x + 1;
            const int cx = x2 < 2 * (GX + GR) ? 2 * (GX + GR) : x2 > 2 * (GX + GW - GR) ? 2 * (GX + GW - GR) : x2;
            const int dx = x2 - cx, dy = y2 - cy;
            if (dx * dx + dy * dy <= 4 * GR * GR) {
                if (x0 < 0) x0 = x;
                x1 = x + 1;
            }
        }
        k->gx0[j] = (int16_t)(x0 < 0 ? 0 : x0), k->gx1[j] = (int16_t)(x0 < 0 ? 0 : x1);
    }
    return ok;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced;
    bool waking;        /* the wake runs (wake_ms not UINT32_MAX, not reduced) */
    uint32_t t;         /* the wake's time */
    float open;         /* the raster: 1 open */
    float bright;       /* the wake's overshoot: 1 at rest */
    float tb;           /* the time's bloom: 1 at rest */
    char spin;          /* '|', '/', '-', '\\'; '*' reduced */
    bool cursor;        /* the cursor is lit */
    bool chime;         /* the band runs */
    float thrum;
    float band_y;       /* its centre */
    char tstr[6], old[6];   /* the time, and the old time lingering ("" none) */
    char prompt[32];
} tm_state_t;

void rd_tm_shown(const fk_face_in_t *in, const char *lines[2], int n[2])
{
    const bool set = in->clk.set;
    lines[0] = set ? rd_tm_page_line(in->variant, 0) : UNSET_LINES[0];
    lines[1] = set ? rd_tm_page_line(in->variant, 1) : UNSET_LINES[1];
    const int len0 = (int)strlen(lines[0]), len1 = (int)strlen(lines[1]);
    n[0] = len0, n[1] = len1;
    if (in->reduced || in->glance) return;
    long t;
    if (in->pulsing) t = in->pulse_ms;   /* a tap's page types from the pulse's start */
    else if (in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_TYPED_MS) t = (long)in->wake_ms - WAKE_TYPE_MS;
    else return;
    const long c = t < 0 ? 0 : t / CHAR_MS;
    n[0] = c < len0 ? (int)c : len0;
    const long c1 = t < (long)len0 * CHAR_MS ? 0 : (t - (long)len0 * CHAR_MS) / CHAR_MS;
    n[1] = c1 < len1 ? (int)c1 : len1;
}

static void old_time(const fk_face_in_t *in, char out[6])
{
    fk_clock_t c = in->clk;
    int m = c.h * 60 + c.m - 1;
    if (m < 0) m += 1440;
    c.h = (uint8_t)(m / 60), c.m = (uint8_t)(m % 60);
    fk_time_str(&c, in->h24, out);
}

static void state_of(const fk_face_in_t *in, tm_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    s->waking = in->wake_ms != UINT32_MAX && !in->reduced && !in->glance;
    s->open = s->bright = s->tb = 1.f;
    if (s->waking) {
        const float t = (float)in->wake_ms;
        s->t = in->wake_ms;
        s->open = fk_ease_out_cubic(clamp01(t / (float)WAKE_OPEN_MS));
        s->bright = 1.f + 0.5f * clamp01(1.f - (t - 250.f) / 400.f);
        s->tb = clamp01((t - 380.f) / 200.f);
    }
    if (in->reduced) {
        s->spin = '*';
        s->cursor = true;
    } else {
        s->spin = "|/-\\"[(in->mono_ms / 180u) % 4u];
        s->cursor = (in->mono_ms / 530u) % 2u == 0;
    }
    if (!in->reduced && !in->glance) {
        const float th = in->thrum > 1 ? 1.f : in->thrum < 0 ? 0.f : in->thrum;
        const bool beep = in->beep > 0.5f;
        if (beep) s->cursor = !s->cursor;   /* the double blink: the blink inverts on the bip and on the bop */
        s->chime = th > 0;
        s->thrum = th;
        if (s->chime) {   /* the band's centre runs from the glass's top to its foot each two seconds of the clock */
            const float ph = (float)((in->clk.s % 2) * 1000 + in->clk.ms) / 2000.f;
            s->band_y = (float)GY + (float)GH * ph;
        }
    }
    fk_time_str(&in->clk, in->h24, s->tstr);
    if (s->set && !in->reduced && !in->glance && in->clk.s == 0 && in->clk.ms < LINGER_MS) old_time(in, s->old);
    char d[16];
    fk_date_str(&in->clk, d);
    strcpy(s->prompt, "> ");
    strcat(s->prompt, d);
    strcat(s->prompt, fk_ampm(&in->clk, in->h24));
}

/* the cursor's box for the prompt's width */
static omni_rect_t cursor_box(const tm_t *k, const tm_state_t *s)
{
    const int x = LX + fk_atlas_width(&k->vt28.core, s->prompt, 0) + 6;
    return (omni_rect_t){(int16_t)x, CUR_Y, (int16_t)(x + CUR_W), CUR_Y + CUR_H};
}

/* ---- drawing */
static void phos_left(uint16_t *fb, int stride, omni_rect_t clip, const rd_atlas2_t *a2, const char *s, int x, int y,
                      uint8_t a)
{
    if (!a || !*s) return;
    fk_atlas_text_rgb(fb, stride, clip, &a2->glow, s, x, y, 0, RD_C_PHOS_DIM, a, 0);
    fk_atlas_text_rgb(fb, stride, clip, &a2->core, s, x, y, 0, RD_C_PHOS, a, 0);
}

/* the numerals: glow then core, centred on cx, dimmed by colour (k / 255) at alpha 255 */
static void phos_time(uint16_t *fb, int stride, omni_rect_t clip, const rd_atlas2_t *a2, const char *s, int cx, int y,
                      uint8_t k)
{
    if (!k) return;
    const int w = fk_atlas_width(&a2->core, s, 0);
    fk_atlas_text_rgb(fb, stride, clip, &a2->glow, s, cx - w / 2, y, 0, dim565(RD_C_PHOS_DIM, k), 255, 0);
    fk_atlas_text_rgb(fb, stride, clip, &a2->core, s, cx - w / 2, y, 0, dim565(RD_C_PHOS, k), 255, 0);
}

/* a rect [x0, x1) x [y0, y1) in rgb at a8, its edges' coverage from the fractional bounds (canvas fillRect) */
static void rect_fill(uint16_t *fb, int stride, omni_rect_t clip, float x0, float y0, float x1, float y1, uint16_t rgb,
                      uint8_t a8)
{
    const omni_pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, &clip, p, 4, rgb, a8);
}

/* unsetTime(): two pairs of dashes on a 0.66 x digit advance about cx +- 1.25 of it, and the colon's two squares;
 * VT323 132's digit advance is 52.8 px (400 of 1000 units) */
static void unset_time(uint16_t *fb, int stride, omni_rect_t clip, float cx, float cy, uint16_t rgb)
{
    const float adv = 52.8f * 0.66f, bw = adv * 0.72f, bh = 132.f * 0.1f, d = 132.f * 0.06f;
    for (int side = -1; side <= 1; side += 2) {
        const float c = cx + (float)side * adv * 1.25f;
        for (int i = 0; i < 2; i++) {   /* Math.round of x, y, w and h */
            const float x = (float)(int)(c + ((float)i - 0.5f) * adv - bw / 2.f + 0.5f);
            const float y = (float)(int)(cy - bh / 2.f + 0.5f);
            rect_fill(fb, stride, clip, x, y, x + (float)(int)(bw + 0.5f), y + (float)(int)(bh + 0.5f), rgb, 255);
        }
    }
    const float qx = (float)(int)(cx - d / 2.f + 0.5f);
    const float q0 = (float)(int)(cy - 132.f * 0.2f + 0.5f), q1 = (float)(int)(cy + 132.f * 0.2f - d + 0.5f);
    rect_fill(fb, stride, clip, qx, q0, qx + d, q0 + d, rgb, 255);
    rect_fill(fb, stride, clip, qx, q1, qx + d, q1 + d, rgb, 255);
}

/* the base: black, the housing through its ramp, the PWR lamp */
static void base_draw(const tm_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    const omni_rect_t hr = {HX, HY, HX + HW, HY + HH};
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        memset(d, 0, (size_t)(clip.x1 - clip.x0) * sizeof d[0]);
        if (y < hr.y0 || y >= hr.y1) continue;
        const int a = clip.x0 > hr.x0 ? clip.x0 : hr.x0, b = clip.x1 < hr.x1 ? clip.x1 : hr.x1;
        const uint8_t *src = k->house.a + (size_t)(y - HY) * HW;
        for (int x = a; x < b; x++) d[x - clip.x0] = k->ramp[src[x - HX]];
    }
    const omni_rect_t lamp = {LAMP_X - 10, LAMP_Y - 10, LAMP_X + 11, LAMP_Y + 11};
    if (meets(lamp, clip)) {   /* lamp(ctx, x, y, 3.5, go, .45): glow r 9.1 at .3 x .45, the lens, the ring */
        fk_glow_add_lut(fb, stride, &clip, LAMP_X, LAMP_Y, 9, k->g9, RD_C_GO, 34);
        fk_disc(fb, stride, &clip, LAMP_X, LAMP_Y, 3.5f, RD_C_GO, 157);
        fk_disc(fb, stride, &clip, LAMP_X - 1.05f, LAMP_Y - 1.2f, 1.6f, RD_RGB(0xFF, 0xF4, 0xE0), 90);
        fk_ring(fb, stride, &clip, LAMP_X, LAMP_Y, 5.f, 1.f, RD_C_STEEL, 230);
    }
}

/* each glass pixel of the clip's rows through fn */
#define GLASS_ROWS(clip, ...)                                                                    \
    for (int y = (clip).y0 > GY ? (clip).y0 : GY; y < (clip).y1 && y < GY + GH; y++) {            \
        const int gx0 = k->gx0[y - GY] > (clip).x0 ? k->gx0[y - GY] : (clip).x0;                   \
        const int gx1 = k->gx1[y - GY] < (clip).x1 ? k->gx1[y - GY] : (clip).x1;                   \
        uint16_t *row = fb + (size_t)(y - (clip).y0) * (size_t)stride - (clip).x0;                \
        __VA_ARGS__                                                                               \
    }

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const tm_t *k = ctx;
    tm_state_t s;
    state_of(in, &s);
    base_draw(k, fb, stride, clip);
    if (!meets(RD_TM_GLASS, clip)) return;
    if (s.open < 1.f) {   /* the power-on: a bright line opens to a raster, inside the glass, nothing else */
        float hh = (float)GH * s.open;
        hh = hh < 2.f ? 2.f : hh;
        const float y0 = (float)GY + (float)GH / 2.f - hh / 2.f, y1 = y0 + hh;
        const uint8_t a = a8_of(0.75f * (1.f - s.open) + 0.05f);
        GLASS_ROWS(clip, {
            const float c0 = (float)y > y0 ? (float)y : y0, c1 = (float)(y + 1) < y1 ? (float)(y + 1) : y1;
            if (c1 <= c0) continue;
            const int av = (int)((c1 - c0) * (float)a + 0.5f);
            for (int x = gx0; x < gx1; x++) row[x] = omni_blend565(row[x], RD_C_PHOS, av);
        })
        return;
    }
    /* the header or the stale marker, the spinner, the rule */
    if (meets(RD_TM_STALE, clip)) {
        if (s.stale) rd_stale_mark(fb, stride, &clip, &k->ch13, STALE_CX, STALE_CY, false);
        else phos_left(fb, stride, clip, &k->vt20, HEADER, LX, Y_HEAD, a8_of(0.65f * s.bright));
    }
    if (meets(RD_TM_SPINNER, clip)) {
        if (s.spin == '|') {   /* VT323's bar: two columns, from 118.5 to 133 (the atlas has no 0x7C) */
            rect_fill(fb, stride, clip, X_SPIN - 5, 118.5f, X_SPIN - 3, 133.f, RD_C_PHOS, 166);
        } else {
            const char t[2] = {s.spin, 0};
            phos_left(fb, stride, clip, &k->vt20, t, X_SPIN - fk_atlas_width(&k->vt20.core, t, 0), Y_HEAD, 166);
        }
    }
    rect_fill(fb, stride, clip, LX, Y_RULE, X_SPIN, Y_RULE + 1, RD_C_PHOS_DIM, 204);
    /* the log */
    if (meets(RD_TM_LOG, clip)) {
        const char *l[2];
        int n[2];
        rd_tm_shown(in, l, n);
        for (int i = 0; i < 2; i++) {
            char t[32];
            const int m = n[i] < (int)sizeof t - 1 ? n[i] : (int)sizeof t - 1;
            memcpy(t, l[i], (size_t)m);
            t[m] = 0;
            phos_left(fb, stride, clip, &k->vt20, t, LX, i ? Y_LOG1 : Y_LOG0, 217);
        }
    }
    /* the time */
    if (meets(RD_TM_TIME, clip)) {
        if (s.old[0]) phos_time(fb, stride, clip, &k->vt132, s.old, 233, Y_TIME, 77);
        if (s.tb > 0) {
            if (!s.set) unset_time(fb, stride, clip, 233.f, (float)Y_TIME_MID, RD_C_PAINT_DIM);
            else phos_time(fb, stride, clip, &k->vt132, s.tstr, 233, Y_TIME, a8_of(s.tb * s.bright));
        }
    }
    /* the prompt and its cursor */
    if (meets(RD_TM_PROMPT, clip) || meets(RD_TM_CURSOR, clip)) {
        phos_left(fb, stride, clip, &k->vt28, s.prompt, LX, Y_PROMPT, 217);
        if (s.cursor) {
            const omni_rect_t c = cursor_box(k, &s);
            rect_fill(fb, stride, clip, c.x0, c.y0, c.x1, c.y1, RD_C_PHOS, 255);
        }
    }
    /* the chime's band: 40 px, brighter (each channel x (1 + .35 g)) with a phosphorDim lift (.18 g), g its triangle */
    if (s.chime) {
        GLASS_ROWS(clip, {
            const float dy = (float)y + 0.5f - s.band_y, ad = dy < 0 ? -dy : dy;
            if (ad >= 20.f) continue;
            const float g = s.thrum * (1.f - ad / 20.f);
            const int mul = 256 + (int)(0.35f * g * 256.f), lr = (int)(0.18f * g * 47.f), lg = (int)(0.18f * g * 122.f),
                      lb = (int)(0.18f * g * 72.f);
            for (int x = gx0; x < gx1; x++) {
                const uint16_t p = row[x];
                int r = (omni_ex_r(p) * mul >> 8) + lr, gg = (omni_ex_g(p) * mul >> 8) + lg;
                int b = (omni_ex_b(p) * mul >> 8) + lb;
                r = r > 255 ? 255 : r, gg = gg > 255 ? 255 : gg, b = b > 255 ? 255 : b;
                row[x] = omni_pack565(r, gg, b);
            }
        })
    }
    /* the scanlines: every third row from the glass's top, x .8 (the mockup's black .2 over it) */
    GLASS_ROWS(clip, {
        if ((y - GY) % 3) continue;
        for (int x = gx0; x < gx1; x++) row[x] = omni_blend565(row[x], 0x0000, 51);
    })
}

static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const tm_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int ox = in->odx, oy = in->ody;
    const bool set = in->clk.set, stale = set && in->clk.stale;
    if (!set) {
        unset_time(fb, stride, clip, (float)(233 + ox), (float)(Y_TIME_MID + oy), RD_G_STEEL);
    } else {
        char t[6];
        fk_time_str(&in->clk, in->h24, t);
        const int w = fk_atlas_width(&k->vt132.core, t, 0);
        fk_atlas_text_rgb(fb, stride, clip, &k->vt132.core, t, 233 + ox - w / 2, Y_TIME + oy, 0, RD_G_PHOS, 255, 0);
    }
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, GLANCE_STALE_CX + ox, STALE_CY + oy, true);
}

/* ---- dirty(): what changed between a and b */
typedef struct { omni_rect_t r[8], u; unsigned n; } rects_t;

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

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    const tm_t *k = ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        char x[6], y[6];
        fk_time_str(&a->clk, a->h24, x);
        fk_time_str(&b->clk, b->h24, y);
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody && strcmp(x, y) == 0;
        return same ? 0 : whole(out);
    }
    tm_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || a->h24 != b->h24 || sa.reduced != sb.reduced) return whole(out);
    /* the wake (either side) and the chime's band: the glass */
    if (sa.waking || sb.waking || sa.chime || sb.chime) {
        out[0] = RD_TM_GLASS;
        return 1;
    }
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.stale != sb.stale) add(&L, RD_TM_STALE);
    if (sa.spin != sb.spin) add(&L, RD_TM_SPINNER);
    const omni_rect_t ca = cursor_box(k, &sa), cb = cursor_box(k, &sb);
    if (strcmp(sa.prompt, sb.prompt) != 0) {
        add(&L, RD_TM_PROMPT);
        add(&L, RD_TM_CURSOR);
    } else if (sa.cursor != sb.cursor) {
        add(&L, sa.cursor ? ca : cb);
    }
    if (strcmp(sa.tstr, sb.tstr) != 0 || strcmp(sa.old, sb.old) != 0) add(&L, RD_TM_TIME);
    const char *la[2], *lb[2];
    int na[2], nb[2];
    rd_tm_shown(a, la, na);
    rd_tm_shown(b, lb, nb);
    if (la[0] != lb[0] || la[1] != lb[1] || na[0] != nb[0] || na[1] != nb[1]) add(&L, RD_TM_LOG);
    return emit(&L, out);
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= RD_TM_GLASS.x0 && x < RD_TM_GLASS.x1 && y >= RD_TM_GLASS.y0 && y < RD_TM_GLASS.y1 ? FK_TAP_PULSE
                                                                                                   : FK_TAP_NONE;
}

/* the next page (0 -> 1 -> 2 -> 3 -> 0, RAM only) and the pulse that types it; not while the clock is unset (the log
 * shows its NTP lines whatever the page: the Task 13 review's rule, a tap never turns a page nobody sees). Stale, the
 * log still shows the page. A NULL in (only from outside the runtime, fk/face.h) counts as set */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    const fk_tap_t t = tap(ctx, x, y);
    if (t == FK_TAP_PULSE && (in == NULL || in->clk.set)) *variant = (uint16_t)((*variant + 1u) % RD_TM_PAGES);
    return t;
}

const fk_face_ops_t RD_FACE_TERMINAL = {
    .key = "terminal",
    .ctx_bytes = sizeof(tm_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = 900,
    .pulse_len_ms = 1350,   /* the longest page, 52 characters at 25 ms (PR32) */
    .tap_var = tap_var,
    .stills_extra = 1,      /* still 11: page 1 (RD_TM_TAPPED_VARIANT) */
};
