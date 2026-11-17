/* Drive Console (`console`, rd/faces.h; rd wave 2 Task 6, spec section 10.4; faces-bridge.js:225-398: build(), frame()
 * and sleepFrame()).
 *
 * The panel (rd_dc_panel, the mockup's build() canvas cut to its ink: A8 406 x 390 at (30, 35), each byte an index into
 * RD_DC_RAMP's five runs from black) is copied through the ramp's RGB565 table, then the tapes over it (rd_dc_cuts,
 * RGB565 124 x 40 at (171, 381), the rect the panel leaves at 0). Over them, in frame()'s order:
 *   stale: a black (150, 34) 166 x 36 and the shared marker (staleMark(233, 52): the ring and STALE from (235, 56));
 *   the hazard collar (collar(): the annulus r 24-30 about (233, 344), the canvas's evenodd clip, each pixel's exact
 *   coverage by omni_cover_ring; inside it hazard()'s 45 degree stripes, 20 sqrt 2 px a period, half rd.hazard, on
 *   rd.hazardDark, each pixel's share of 4 x 4 samples; the band's chips land outside the annulus) at the phase of the
 *   clock's second of the day (1 px a second), the minute's under reduced motion (R171), mono_ms's seconds while unset;
 *   the lamps (lamp(): the glow, rd.<col> at .30 on falling linearly to 0 at r 17 (the linear table, r 6.5 x 2.6 =
 *   16.9); the lens r 6.5, a two-point radial gradient from (x - 1.95, y - 2.275) r 0 to (x, y) r 6.5 through #fff4e0 at
 *   .2 + .8 on, col at .3 + .7 on and col at .15 + .35 on (dark: #24201c, #121010, #060505), its coverage and the
 *   gradient's place per pixel kept from bind (16 x 16 samples; the place by its quadratic, no libm); the steel ring r 8
 *   at .9);
 *   the readout: each digit's glow first (the mockup's shadowBlur 6 in amber .45: each segment's coverage blurred at bind
 *   by a Gaussian of sigma 3 (the canvas's blur / 2), radius 9, integer weights, laid segment by segment), then its lit
 *   segments (rd/seg7.h) in amber (unset: rd.paintDim), both dimmed by the chime's breath; then the colon's two squares
 *   (amber at its blink's alpha, or rd.paintDim unset) over their own blurred glow;
 *   the strip (phosText(): the glow plane then the core, each in the line's colour dimmed by .9, or .8 unset; VT323 28 on
 *   its 11.2 px advance, centred on 233, its 'middle' 293 on the baseline 301);
 *   the needles (needle(): the polygon (-9, -2.2) (r - 7, -.6) (r - 7, .6) (-9, 2.2) turned to its angle, filled, then
 *   stroked 1 px in black .8; the steelDeep hub r 5 and its steelHi pixel), SEC in amber (steel unset), RESERVE in paint;
 *   the button (r 21 - 2 dn about (233, 344 + 2 dn), a two-point radial gradient from (x - 6, y - 7 + 2 dn) r 2 through
 *   #ff6a4e (#c4492f while it sinks), hull at .45 and #2a0806 at 1); dn (state_of) is 1, sunk, at the tap, held to
 *   250 ms and back to 0 linearly by 550 ms.
 * The strip's RESERVE and last-sync lines are faces.c's rd_reserve_str and rd_sync_str (shared with Nav Scope).
 * Numerals are dimmed by colour, at alpha 255 (the numerals' rule); the colon's blink is the mockup's alpha. Pure: no
 * esp_*, freertos or LVGL; no libm (lesson 6: angles through fk_sincos, square roots by Newton's steps); no allocation;
 * every array a draw needs over 256 B is in the context, written at bind. */
#include <string.h>
#include "rd/faces.h"
#include "rd/seg7.h"
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
#define PX 30      /* rd_dc_panel's crop */
#define PY 35
#define PW 406
#define PH 390
#define CX0 171    /* rd_dc_cuts' crop */
#define CY0 381
#define CW 124
#define CH 40
#define DH 96.f    /* the digits: segPaths(54, 96, 11, .07) */
#define DY 161     /* RY + 15 */
#define GM 10      /* a glow map's margin about the digit's box (the blur's radius 9 and a pixel) */
#define GW 82      /* the digit's 61 px (54 and its slant's 6.72) and the margins */
#define GH 117
#define CGX 226    /* the colon's glow map, screen */
#define CGY 174
#define CGW 34
#define CGH 70
#define LX0 103    /* the lamps: x LX0 + 52 i, y LY */
#define LY 106
#define LR 6.5f
#define LN 16      /* the lens's tables: the pixels (x - 8 .. x + 7) */
#define LGR 17     /* the glow: the linear table r 17 */
#define G1X 134    /* the gauges */
#define G2X 332
#define GY 352
#define GR 50.f
#define BX RD_DC_BTN_X
#define BY RD_DC_BTN_Y
#define Y_STRIP 301     /* 293 middle + 8.4 */
#define VT_TRACK 0.2f   /* VT323 28's 11.2 px advance less the atlas's 11 */
#define TRACK 1.8f      /* util(ctx, 13): (13 x 0.14).toFixed(1) px */
#define STALE_CX 235    /* staleMark(233, 52): the shared marker's STALE lands there from (235, 56) */
#define STALE_CY 56
#define Y_GDATE 296     /* the glance's date: 293 middle + 3.4 */
#define WAKE_MS 900
#define TICK_MS 220
#define PI_F 3.14159265359f
#define TAU 6.28318530718f
#define HZ_PITCH 28.2842712475f   /* hazard()'s 20 sqrt 2 */

const omni_rect_t RD_DC_LAMPS = {LX0 - LGR, LY - LGR, LX0 + 52 * 5 + LGR, LY + LGR};
const omni_rect_t RD_DC_SECG = {G1X - 56, GY - 56, G1X + 56, GY + 56};
const omni_rect_t RD_DC_RESG = {G2X - 56, GY - 56, G2X + 56, GY + 56};
const omni_rect_t RD_DC_TIME = {101 - GM, DY - GM, 315 - GM + GW, DY - GM + GH};
const omni_rect_t RD_DC_COLON = {CGX, CGY, CGX + CGW, CGY + CGH};
const omni_rect_t RD_DC_STRIP = {84, 270, 382, 318};
const omni_rect_t RD_DC_COLLAR = {BX - 32, BY - 32, BX + 32, BY + 32};
const omni_rect_t RD_DC_BTN_HIT = {BX - RD_DC_BTN_R, BY - RD_DC_BTN_R, BX + RD_DC_BTN_R, BY + RD_DC_BTN_R};
const omni_rect_t RD_DC_BTN_BOX = {BX - 23, BY - 23, BX + 24, BY + 25};
const omni_rect_t RD_DC_STALE = {150, 34, 316, 70};

/* rd_dc_panel's ramp (tools/export/rd_layers.json's row names the same stops) */
const rd_ramp_stop_t RD_DC_RAMP[RD_DC_RAMP_STOPS] = {
    {0, 0x00, 0x00, 0x00},   {50, 0x8A, 0x90, 0x96},  {51, 0x00, 0x00, 0x00},  {101, 0xDC, 0xD3, 0xBE},
    {102, 0x00, 0x00, 0x00}, {152, 0xC4, 0x49, 0x2F}, {153, 0x00, 0x00, 0x00}, {203, 0xFF, 0xA8, 0x3A},
    {204, 0x00, 0x00, 0x00}, {255, 0xFF, 0x3B, 0x24},
};

/* the mockup's spring() at k / 11 of the tick's 220 ms (Hull Plate's table, the same curve) */
static const float TICK_SPRING[12] = {0.000000f, 0.645434f, 1.080718f, 1.170249f, 1.090908f, 1.009338f,
                                      0.979909f, 0.984954f, 0.996054f, 1.000979f, 1.000912f, 1.000000f};
/* exp(-i^2 / 18) (a Gaussian of sigma 3) over i = 0 .. 9, in 4 096ths of the kernel's sum */
static const uint16_t GAUSS[10] = {544, 516, 437, 331, 224, 136, 74, 36, 16, 6};

static const uint16_t LAMP_RGB[6] = {RD_C_GO, RD_C_GO, RD_C_AMBER, RD_C_GO, RD_C_AMBER, RD_C_LAMP};
#define NO_DATE "--- -- ---"
#define AWAIT "[ AWAITING TIME SIGNAL ]"

typedef struct {
    fk_img_t panel, cuts, ch13_img;
    fk_atlas_t ch13;
    rd_atlas2_t vt28;   /* used where filled, never copied */
    const uint8_t *lin17;
    uint16_t ramp[256];
    uint8_t glow[7][GH][GW];       /* each segment's blurred coverage, the digit at (GM, GM) of its map */
    uint8_t cglow[CGH][CGW];       /* the colon's two squares' blurred coverage, screen (CGX, CGY) */
    uint8_t lens_cov[LN][LN];      /* the lens r 6.5's coverage about a lamp's centre */
    uint8_t lens_t[LN][LN];        /* the gradient's place (0..255) */
} dc_t;

/* ---- small helpers */
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static uint8_t a8_of(float a) { return fk_ease_to8(a); }
static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static uint16_t dim565(uint16_t c, uint8_t k)
{
    if (k == 255) return c;
    return omni_pack565(omni_ex_r(c) * k / 255, omni_ex_g(c) * k / 255, omni_ex_b(c) * k / 255);
}
/* sqrt(v) for v >= 0 by Newton's steps from the float's halved exponent (no libm: sqrtf is a software routine) */
static float sqrt_f(float v)
{
    if (!(v > 0)) return 0;
    uint32_t u;
    memcpy(&u, &v, 4);
    u = (u >> 1) + 0x1FC00000u;
    float x;
    memcpy(&x, &u, 4);
    for (int i = 0; i < 4; i++) x = 0.5f * (x + v / x);
    return x;
}
static float table_at(const float *t, int n, float x)   /* t[0 .. n - 1] over x in [0, 1], linear between */
{
    if (!(x > 0)) return t[0];
    if (x >= 1) return t[n - 1];
    const float p = x * (float)(n - 1);
    const int i = (int)p;
    return t[i] + (t[i + 1] - t[i]) * (p - (float)i);
}
static long ms_of_day(const fk_clock_t *c) { return (((long)c->h * 60 + c->m) * 60 + c->s) * 1000L + c->ms; }
static bool waking(const fk_face_in_t *in) { return !in->reduced && in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_MS; }

unsigned rd_dc_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_DC_TIME;
    out[1] = RD_DC_LAMPS;
    return 2;
}

/* ---- the face's readings */
bool rd_dc_strip(const fk_face_in_t *in, char out[32])
{
    if (in->pulsing && in->pulse_ms < 3200) {
        strcpy(out, in->pulse_ms < 1500 ? "[ REQUEST LOGGED ]" : "[ REQUEST IGNORED ]");
        return true;
    }
    if (!in->clk.set) {
        strcpy(out, in->variant % 2 ? NO_DATE : AWAIT);
        return false;
    }
    switch (in->variant % RD_DC_PAGES) {
    case 0: {
        char d[16];
        fk_date_str(&in->clk, d);
        strcpy(out, d);
        strcat(out, fk_ampm(&in->clk, in->h24));
        break;
    }
    case 1: {
        char r[16];
        rd_reserve_str(in->battery, r);
        strcpy(out, r);
        break;
    }
    default: {
        char s[24];
        rd_sync_str(in->sync_age_s, s);
        strcpy(out, s);
        break;
    }
    }
    return false;
}

float rd_dc_lamp(const fk_face_in_t *in, int i)
{
    const bool rm = in->reduced, set = in->clk.set;
    float on = 0;
    if (!set) {   /* only NAV breathes, over 1.4 s of the free-running count */
        if (i == 2) {
            if (rm) {
                on = 0.5f;
            } else {
                float s, c;
                fk_sincos((float)(in->mono_ms % 1400u) / 1400.f * TAU, &s, &c);
                on = 0.5f + 0.5f * s;
            }
        }
    } else if (i == 5) {   /* COMMS is dead */
        on = 0;
    } else if (i == 2) {   /* NAV breathes over 2.6 s of the clock */
        if (rm) {
            on = 0.55f;
        } else {
            float s, c;
            fk_sincos((float)(ms_of_day(&in->clk) % 2600L) / 2600.f * TAU, &s, &c);
            on = 0.55f + 0.45f * s;
        }
    } else if (i == 4) {   /* AFT's loose filament: the clock's 90 ms windows */
        if (rm) {
            on = 0.95f;
        } else {
            const uint32_t k = (uint32_t)(ms_of_day(&in->clk) / 90);
            on = (uint32_t)((uint64_t)k * 2654435761u) % 23u == 0 ? 0.2f : 0.95f;
        }
    } else {
        on = 0.9f;
    }
    if (waking(in) && in->wake_ms < 600 && !(in->wake_ms > (uint32_t)(60 + i * 70))) on = 0;   /* popping on */
    if (set && !rm && !in->glance && in->beep > 0) {   /* the chime: every lamp lights on the beep */
        const float b = in->beep > 1 ? 1.f : in->beep;
        on = b > on ? b : on;
    }
    return on;
}

void rd_dc_digits(const fk_face_in_t *in, char out[5])
{
    if (waking(in) && in->wake_ms <= 150) {
        memcpy(out, "    ", 5);
        return;
    }
    if (waking(in) && in->wake_ms < 450) {
        memcpy(out, "8888", 5);
        return;
    }
    rd_seg7_str(in, out);
}

/* the wake's swing over the rest place v: v easeOut3(wk) + sin(pi easeInOut(wk)) */
static float swing(const fk_face_in_t *in, float v)
{
    if (!waking(in)) return v;
    const float wk = (float)in->wake_ms / (float)WAKE_MS;
    float s, c;
    fk_sincos(fk_ease_in_out_cubic(wk) * PI_F, &s, &c);
    return v * fk_ease_out_cubic(wk) + s;
}

float rd_dc_sec(const fk_face_in_t *in)
{
    float sec = 0;
    if (in->clk.set) {
        const float s = (float)in->clk.s;
        if (in->reduced) {
            sec = s / 60.f;
        } else {
            const float tick = table_at(TICK_SPRING, 12, (float)in->clk.ms / (float)TICK_MS);
            sec = in->clk.s == 0 ? (59.f / 60.f) * (1.f - tick) : (s - 1.f + tick) / 60.f;
        }
    }
    return clampf(swing(in, sec), -0.02f, 1.02f);
}

float rd_dc_res(const fk_face_in_t *in)
{
    const float res = in->battery < 0 ? 0.f : (float)(in->battery > 100 ? 100 : in->battery) / 100.f;
    return clampf(swing(in, res), -0.02f, 1.02f);
}

/* ---- bind */
/* a separable blur of the map (w x h, row stride w) by GAUSS, in place, rounded at each pass */
static void blur(uint8_t *m, int w, int h)
{
    uint8_t line[GH > GW ? GH : GW];
    for (int y = 0; y < h; y++) {
        memcpy(line, m + y * w, (size_t)w);
        for (int x = 0; x < w; x++) {
            uint32_t acc = 0;
            for (int k = -9; k <= 9; k++) {
                const int xx = x + k;
                if (xx >= 0 && xx < w) acc += (uint32_t)line[xx] * GAUSS[k < 0 ? -k : k];
            }
            m[y * w + x] = (uint8_t)((acc + 2048) >> 12);
        }
    }
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) line[y] = m[y * w + x];
        for (int y = 0; y < h; y++) {
            uint32_t acc = 0;
            for (int k = -9; k <= 9; k++) {
                const int yy = y + k;
                if (yy >= 0 && yy < h) acc += (uint32_t)line[yy] * GAUSS[k < 0 ? -k : k];
            }
            m[y * w + x] = (uint8_t)((acc + 2048) >> 12);
        }
    }
}

static bool bind(void *ctx, fk_asset_fn get)
{
    dc_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_DC_PANEL, &n);
    bool ok = b && fk_img_from_bin(&k->panel, b, n) && k->panel.a && !k->panel.rgb && k->panel.w == PW &&
              k->panel.h == PH;
    b = get(ASSET_RD_DC_CUTS, &n);
    ok = ok && b && fk_img_from_bin(&k->cuts, b, n) && k->cuts.rgb && !k->cuts.a && k->cuts.w == CW && k->cuts.h == CH;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    b = get(ASSET_RD_F_VT28, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt28);
    k->lin17 = rd_glow_lut(LGR);
    ok = ok && k->lin17;
    rd_ramp565(RD_DC_RAMP, RD_DC_RAMP_STOPS, k->ramp);
    /* the segments' glow: each one's exact coverage in its map, blurred */
    for (int sg = 0; sg < 7; sg++) {
        omni_pt_t p[6];
        rd_seg7_poly(sg, (float)GM, (float)GM, DH, p);
        for (int y = 0; y < GH; y++) omni_cover_poly(p, 6, y, 0, GW, k->glow[sg][y]);
        blur(&k->glow[sg][0][0], GW, GH);
    }
    /* the colon's: fillRect(cxl - 4, yy + DH x .30 - 5, 10, 10) and (cxl - 7, yy + DH x .70 - 5), cxl = 243 */
    {
        const float qx[2] = {239.f - CGX, 236.f - CGX};
        const float qy[2] = {(float)DY + DH * 0.30f - 5.f - CGY, (float)DY + DH * 0.70f - 5.f - CGY};
        for (int i = 0; i < 2; i++) {
            const omni_pt_t p[4] = {{qx[i], qy[i]}, {qx[i] + 10, qy[i]}, {qx[i] + 10, qy[i] + 10}, {qx[i], qy[i] + 10}};
            for (int y = 0; y < CGH; y++) {
                uint8_t row[CGW];
                omni_cover_poly(p, 4, y, 0, CGW, row);
                for (int x = 0; x < CGW; x++) {
                    const int v = k->cglow[y][x] + row[x];
                    k->cglow[y][x] = (uint8_t)(v > 255 ? 255 : v);
                }
            }
        }
        blur(&k->cglow[0][0], CGW, CGH);
    }
    /* the lens: coverage by 16 x 16 samples; the gradient's place t at the pixel centre: the circles (f, 0) to (0, 6.5)
     * about f = (-1.95, -2.275), |u + f t| = 6.5 t for u = p - f, its larger root */
    const float fx = -0.3f * LR, fy = -0.35f * LR, qa = fx * fx + fy * fy - LR * LR;
    for (int j = 0; j < LN; j++)
        for (int i = 0; i < LN; i++) {
            int hit = 0;
            for (int sy = 0; sy < 16; sy++)
                for (int sx = 0; sx < 16; sx++) {
                    const float x = (float)(i - 8) + ((float)sx + 0.5f) / 16.f, y = (float)(j - 8) + ((float)sy + 0.5f) / 16.f;
                    hit += x * x + y * y <= LR * LR;
                }
            k->lens_cov[j][i] = (uint8_t)((hit * 255 + 128) / 256);
            const float ux = (float)(i - 8) + 0.5f - fx, uy = (float)(j - 8) + 0.5f - fy;
            const float qb = 2.f * (ux * fx + uy * fy), qc = ux * ux + uy * uy;
            const float t = (-qb - sqrt_f(qb * qb - 4.f * qa * qc)) / (2.f * qa);
            k->lens_t[j][i] = a8_of(t);
        }
    return ok;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced;
    float lamp[6];
    char dig[5];
    uint8_t segk;        /* the chime's breath on the digits and the colon (255: none) */
    uint8_t colon_a;     /* the colon's alpha (amber), 0 none */
    char strip[32];
    bool msg;
    float sec, res, dn;
    long collar;         /* the collar's phase, px */
} dc_state_t;

static void state_of(const fk_face_in_t *in, dc_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    for (int i = 0; i < 6; i++) s->lamp[i] = rd_dc_lamp(in, i);
    rd_dc_digits(in, s->dig);
    s->segk = 255;
    if (s->set && !in->reduced && !in->glance && in->thrum > 0) s->segk = a8_of(1.f - 0.4f * clamp01(in->thrum));
    if (!s->set) s->colon_a = 255;   /* paintDim, static */
    else if (waking(in) && in->wake_ms <= 150) s->colon_a = 0;
    else s->colon_a = in->reduced || in->clk.ms < 500 ? 255 : a8_of(0.15f);
    s->msg = rd_dc_strip(in, s->strip);
    s->sec = rd_dc_sec(in);
    s->res = rd_dc_res(in);
    if (in->pulsing && in->pulse_ms < 3200) s->dn = clamp01(1.f - ((float)in->pulse_ms - 250.f) / 300.f);
    if (!s->set) s->collar = in->reduced ? 0 : (long)(in->mono_ms / 1000u);
    else s->collar = in->reduced ? (long)in->clk.h * 60 + in->clk.m : ms_of_day(&in->clk) / 1000;
}

/* ---- drawing */
static void base_draw(const dc_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        memset(d, 0, (size_t)(clip.x1 - clip.x0) * sizeof d[0]);
        if (y >= PY && y < PY + PH) {
            const int a = clip.x0 > PX ? clip.x0 : PX, b = clip.x1 < PX + PW ? clip.x1 : PX + PW;
            const uint8_t *src = k->panel.a + (size_t)(y - PY) * PW;
            for (int x = a; x < b; x++) d[x - clip.x0] = k->ramp[src[x - PX]];
        }
        if (y >= CY0 && y < CY0 + CH) {
            const int a = clip.x0 > CX0 ? clip.x0 : CX0, b = clip.x1 < CX0 + CW ? clip.x1 : CX0 + CW;
            if (a < b)
                memcpy(d + (a - clip.x0), k->cuts.rgb + (size_t)(y - CY0) * CW + (a - CX0), (size_t)(b - a) * 2);
        }
    }
}

/* a rect [x0, x1) x [y0, y1) in rgb at a */
static void rect_fill(uint16_t *fb, int stride, omni_rect_t clip, float x0, float y0, float x1, float y1, uint16_t rgb,
                      uint8_t a)
{
    const omni_pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, &clip, p, 4, rgb, a);
}

static void collar(const dc_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_DC_COLLAR, clip)) return;
    const int y0 = clip.y0 > RD_DC_COLLAR.y0 ? clip.y0 : RD_DC_COLLAR.y0;
    const int y1 = clip.y1 < RD_DC_COLLAR.y1 ? clip.y1 : RD_DC_COLLAR.y1;
    const int x0 = clip.x0 > RD_DC_COLLAR.x0 ? clip.x0 : RD_DC_COLLAR.x0;
    const int x1 = clip.x1 < RD_DC_COLLAR.x1 ? clip.x1 : RD_DC_COLLAR.x1;
    /* hazard(ctx, 193, 304, 80, 80, phase): stripe at (X, Y) where (X + Y - 497 + pitch - off0) mod pitch < pitch / 2 */
    const long q = (long)((float)s->collar / HZ_PITCH);
    float off0 = (float)s->collar - (float)q * HZ_PITCH;
    while (off0 >= HZ_PITCH) off0 -= HZ_PITCH;
    while (off0 < 0) off0 += HZ_PITCH;
    const float base = -(float)(BX - 40) - (float)(BY - 40) + HZ_PITCH - off0;
    for (int y = y0; y < y1; y++) {
        uint8_t cov[64];
        omni_cover_ring((float)BX, (float)BY, 24.f, 30.f, y, x0, x1, cov);
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = x0; x < x1; x++) {
            const int c = cov[x - x0];
            if (!c) continue;
            int hits = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float u = (float)x + ((float)sx + 0.5f) / 4.f + (float)y + ((float)sy + 0.5f) / 4.f + base;
                    const long m = (long)(u / HZ_PITCH);
                    u -= (float)m * HZ_PITCH;
                    if (u < 0) u += HZ_PITCH;
                    hits += u < HZ_PITCH / 2.f;
                }
            const uint16_t col = omni_blend565(RD_C_HAZARD_DARK, RD_C_HAZARD, hits * 255 / 16);
            d[x] = omni_blend565(d[x], col, c);
        }
    }
}

static void lamp_draw(const dc_t *k, int i, float on, uint16_t *fb, int stride, omni_rect_t clip)
{
    const int cx = LX0 + 52 * i, cy = LY;
    const omni_rect_t box = {(int16_t)(cx - LGR), (int16_t)(cy - LGR), (int16_t)(cx + LGR), (int16_t)(cy + LGR)};
    if (!meets(box, clip)) return;
    const uint16_t col = LAMP_RGB[i];
    if (on > 0.02f) {   /* the glow: .30 on at the centre, linearly to 0 at r 17 */
        const int A = a8_of(0.30f * on);
        const int y0 = clip.y0 > box.y0 ? clip.y0 : box.y0, y1 = clip.y1 < box.y1 ? clip.y1 : box.y1;
        const int x0 = clip.x0 > box.x0 ? clip.x0 : box.x0, x1 = clip.x1 < box.x1 ? clip.x1 : box.x1;
        for (int y = y0; y < y1; y++) {
            uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
            const int ey = 2 * (y - cy) + 1;
            for (int x = x0; x < x1; x++) {
                const int ex = 2 * (x - cx) + 1, N = ex * ex + ey * ey;
                if (N >= 4 * LGR * LGR) continue;
                const int a = k->lin17[(N - 2) / 8] * A / 255;
                if (a) d[x] = omni_blend565(d[x], col, a);
            }
        }
    }
    /* the lens: the gradient's colour and alpha at its place, at the lens's coverage */
    const bool lit = on > 0.02f;
    const int cr = omni_ex_r(col), cg = omni_ex_g(col), cb = omni_ex_b(col);
    const float a0 = 0.2f + 0.8f * on, a1 = 0.3f + 0.7f * on, a2 = 0.15f + 0.35f * on;
    for (int j = 0; j < LN; j++) {
        const int y = cy - 8 + j;
        if (y < clip.y0 || y >= clip.y1) continue;
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int i2 = 0; i2 < LN; i2++) {
            const int x = cx - 8 + i2;
            if (x < clip.x0 || x >= clip.x1) continue;
            const int cov = k->lens_cov[j][i2];
            if (!cov) continue;
            const float t = (float)k->lens_t[j][i2] / 255.f;
            float r, g, b, a;
            if (lit) {
                if (t < 0.5f) {
                    const float u = t / 0.5f;
                    r = 255.f + ((float)cr - 255.f) * u, g = 244.f + ((float)cg - 244.f) * u;
                    b = 224.f + ((float)cb - 224.f) * u, a = a0 + (a1 - a0) * u;
                } else {
                    const float u = (t - 0.5f) / 0.5f;
                    r = (float)cr, g = (float)cg, b = (float)cb, a = a1 + (a2 - a1) * u;
                }
            } else {   /* #24201c, #121010, #060505, opaque */
                const float u = t < 0.5f ? t / 0.5f : (t - 0.5f) / 0.5f;
                const float c0[3] = {36, 32, 28}, c1[3] = {18, 16, 16}, c2[3] = {6, 5, 5};
                const float *p0 = t < 0.5f ? c0 : c1, *p1 = t < 0.5f ? c1 : c2;
                r = p0[0] + (p1[0] - p0[0]) * u, g = p0[1] + (p1[1] - p0[1]) * u, b = p0[2] + (p1[2] - p0[2]) * u;
                a = 1.f;
            }
            const uint16_t c565 = omni_pack565((int)(r + 0.5f), (int)(g + 0.5f), (int)(b + 0.5f));
            d[x] = omni_blend565(d[x], c565, (int)(clamp01(a) * (float)cov + 0.5f));
        }
    }
    fk_ring(fb, stride, &clip, (float)cx, (float)cy, LR + 1.5f, 1.f, RD_C_STEEL, 230);
}

/* one digit: its lit segments' glow (amber .45 of the blurred coverage), then the segments */
static void digit_draw(const dc_t *k, int ch, int dx, uint16_t on, uint16_t glow, uint16_t *fb, int stride,
                       omni_rect_t clip)
{
    const unsigned lit = rd_seg7_segs(ch);
    if (!lit) return;
    const omni_rect_t box = {(int16_t)(dx - GM), (int16_t)(DY - GM), (int16_t)(dx - GM + GW), (int16_t)(DY - GM + GH)};
    if (!meets(box, clip)) return;
    const int y0 = clip.y0 > box.y0 ? clip.y0 : box.y0, y1 = clip.y1 < box.y1 ? clip.y1 : box.y1;
    const int x0 = clip.x0 > box.x0 ? clip.x0 : box.x0, x1 = clip.x1 < box.x1 ? clip.x1 : box.x1;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = x0; x < x1; x++)
            for (int sg = 0; sg < 7; sg++) {
                if (!((lit >> sg) & 1u)) continue;
                const int g = k->glow[sg][y - box.y0][x - box.x0];
                if (g) d[x] = omni_blend565(d[x], glow, (g * 115 + 127) / 255);
            }
    }
    rd_seg7_digit(fb, stride, &clip, ch, (float)dx, (float)DY, DH, on, 0, false);
}

static void colon_draw(const dc_t *k, const dc_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!s->colon_a || !meets(RD_DC_COLON, clip)) return;
    const uint16_t glow = dim565(RD_C_AMBER, s->segk);
    const int ga = 115 * s->colon_a / 255;
    const int y0 = clip.y0 > CGY ? clip.y0 : CGY, y1 = clip.y1 < CGY + CGH ? clip.y1 : CGY + CGH;
    const int x0 = clip.x0 > CGX ? clip.x0 : CGX, x1 = clip.x1 < CGX + CGW ? clip.x1 : CGX + CGW;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = x0; x < x1; x++) {
            const int g = k->cglow[y - CGY][x - CGX];
            if (g) d[x] = omni_blend565(d[x], glow, (g * ga + 127) / 255);
        }
    }
    const uint16_t fill = s->set ? dim565(RD_C_AMBER, s->segk) : RD_C_PAINT_DIM;
    const uint8_t a = s->set ? s->colon_a : 255;
    rect_fill(fb, stride, clip, 239.f, (float)DY + DH * 0.30f - 5.f, 249.f, (float)DY + DH * 0.30f + 5.f, fill, a);
    rect_fill(fb, stride, clip, 236.f, (float)DY + DH * 0.70f - 5.f, 246.f, (float)DY + DH * 0.70f + 5.f, fill, a);
}

/* needle(ctx, G, frac, col) */
static void needle(int gx, float frac, uint16_t col, uint16_t *fb, int stride, omni_rect_t clip)
{
    const omni_rect_t box = {(int16_t)(gx - 56), GY - 56, (int16_t)(gx + 56), GY + 56};
    if (!meets(box, clip)) return;
    const float a = 0.75f * PI_F + 1.5f * PI_F * frac;
    float sn, cs;
    fk_sincos(a, &sn, &cs);
    static const float L[4][2] = {{-9.f, -2.2f}, {GR - 7.f, -0.6f}, {GR - 7.f, 0.6f}, {-9.f, 2.2f}};
    omni_pt_t p[4];
    for (int i = 0; i < 4; i++)
        p[i] = (omni_pt_t){(float)gx + L[i][0] * cs - L[i][1] * sn, (float)GY + L[i][0] * sn + L[i][1] * cs};
    omni_fill_poly(fb, stride, &clip, p, 4, col, 255);
    fk_poly_stroke(fb, stride, &clip, p, 4, 1.f, 0x0000, 204);
    fk_disc(fb, stride, &clip, (float)gx, (float)GY, 5.f, RD_C_STEEL_DEEP, 255);
    uint16_t *h = (gx - 2 >= clip.x0 && gx - 2 < clip.x1 && GY - 2 >= clip.y0 && GY - 2 < clip.y1)
                      ? fk_px_at(fb, stride, &clip, gx - 2, GY - 2)
                      : NULL;
    if (h) *h = RD_C_STEEL_HI;
}

/* the mushroom: r 21 - 2 dn about (233, 344 + 2 dn); its two-point radial gradient from (x - 6, y - 7 + 2 dn) r 2 */
static void button(const dc_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_DC_BTN_BOX, clip)) return;
    const float dn = s->dn, br = 21.f - 2.f * dn, cy = (float)BY + 2.f * dn, cx = (float)BX;
    const float fx = -6.f, fy = -7.f, r0 = 2.f, dr = br - r0, qa = fx * fx + fy * fy - dr * dr;
    const uint16_t c0 = dn > 0 ? RD_RGB(0xC4, 0x49, 0x2F) : RD_RGB(0xFF, 0x6A, 0x4E);
    const uint16_t c1 = RD_C_HULL, c2 = RD_RGB(0x2A, 0x08, 0x06);
    const int y0 = clip.y0 > RD_DC_BTN_BOX.y0 ? clip.y0 : RD_DC_BTN_BOX.y0;
    const int y1 = clip.y1 < RD_DC_BTN_BOX.y1 ? clip.y1 : RD_DC_BTN_BOX.y1;
    const int x0 = clip.x0 > RD_DC_BTN_BOX.x0 ? clip.x0 : RD_DC_BTN_BOX.x0;
    const int x1 = clip.x1 < RD_DC_BTN_BOX.x1 ? clip.x1 : RD_DC_BTN_BOX.x1;
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = x0; x < x1; x++) {
            const float qx = (float)x + 0.5f - cx, qy = (float)y + 0.5f - cy, rr = qx * qx + qy * qy;
            if (rr > (br + 1.f) * (br + 1.f)) continue;
            const float cov = clamp01(br + 0.5f - sqrt_f(rr));
            if (!(cov > 0)) continue;
            const float ux = qx - fx, uy = qy - fy;
            const float qb = 2.f * (ux * fx + uy * fy - r0 * dr), qc = ux * ux + uy * uy - r0 * r0;
            const float t = clamp01((-qb - sqrt_f(qb * qb - 4.f * qa * qc)) / (2.f * qa));
            const uint16_t col = t < 0.45f ? omni_blend565(c0, c1, (int)(t / 0.45f * 255.f + 0.5f))
                                           : omni_blend565(c1, c2, (int)((t - 0.45f) / 0.55f * 255.f + 0.5f));
            d[x] = omni_blend565(d[x], col, (int)(cov * 255.f + 0.5f));
        }
    }
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const dc_t *k = ctx;
    dc_state_t s;
    state_of(in, &s);
    base_draw(k, fb, stride, clip);
    if (s.stale && meets(RD_DC_STALE, clip)) {
        rect_fill(fb, stride, clip, 150.f, 34.f, 316.f, 70.f, 0x0000, 255);
        rd_stale_mark(fb, stride, &clip, &k->ch13, STALE_CX, STALE_CY, false);
    }
    collar(&s, fb, stride, clip);
    if (meets(RD_DC_LAMPS, clip))
        for (int i = 0; i < 6; i++) lamp_draw(k, i, s.lamp[i], fb, stride, clip);
    if (meets(RD_DC_TIME, clip)) {
        float xs[4];
        rd_seg7_xs(233.f, DH, xs);
        const bool dashes = !s.set && strcmp(s.dig, "----") == 0;
        const uint16_t on = dashes ? RD_C_PAINT_DIM : dim565(RD_C_AMBER, s.segk), glow = dim565(RD_C_AMBER, s.segk);
        for (int i = 0; i < 4; i++) digit_draw(k, s.dig[i], (int)xs[i], on, glow, fb, stride, clip);
        colon_draw(k, &s, fb, stride, clip);
    }
    if (meets(RD_DC_STRIP, clip)) {
        const uint16_t c = dim565(s.msg ? RD_C_LAMP : RD_C_AMBER, s.set ? 230 : 204);
        fk_atlas_text_tracked(fb, stride, clip, &k->vt28.glow, s.strip, 233.f, Y_STRIP, VT_TRACK, true, c, 255);
        fk_atlas_text_tracked(fb, stride, clip, &k->vt28.core, s.strip, 233.f, Y_STRIP, VT_TRACK, true, c, 255);
    }
    needle(G1X, s.sec, s.set ? RD_C_AMBER : RD_C_STEEL, fb, stride, clip);
    needle(G2X, s.res, RD_C_PAINT, fb, stride, clip);
    button(&s, fb, stride, clip);
}

static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const dc_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int ox = in->odx, oy = in->ody;
    const bool set = in->clk.set, stale = set && in->clk.stale;
    const uint16_t ink = set ? RD_G_AMBER : RD_G_STEEL;
    float xs[4];
    char t[5];
    rd_seg7_xs(233.f, DH, xs);
    rd_seg7_str(in, t);
    for (int i = 0; i < 4; i++)
        rd_seg7_digit(fb, stride, &clip, t[i], xs[i] + (float)ox, (float)(DY + oy), DH, ink, 0, true);
    const float cxl = xs[1] + 54.f + 24.f + (float)ox;
    rect_fill(fb, stride, clip, cxl - 2.f, (float)(DY + oy) + DH * 0.30f - 3.f, cxl + 4.f,
              (float)(DY + oy) + DH * 0.30f + 3.f, ink, 255);
    rect_fill(fb, stride, clip, cxl - 5.f, (float)(DY + oy) + DH * 0.70f - 3.f, cxl + 1.f,
              (float)(DY + oy) + DH * 0.70f + 3.f, ink, 255);
    char d[16];
    fk_date_str(&in->clk, d);
    fk_atlas_text_tracked(fb, stride, clip, &k->ch13, set ? d : NO_DATE, (float)(233 + ox), Y_GDATE + oy, TRACK, true,
                          RD_G_PAINT, 255);
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, STALE_CX + ox, STALE_CY + oy, true);
}

/* ---- dirty(): what changed between a and b */
typedef struct { omni_rect_t r[8]; unsigned n; } rects_t;

static void add(rects_t *L, omni_rect_t r)
{
    if (L->n < 8) L->r[L->n++] = r;
}

static uint8_t whole(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        char ta[5], tb[5], da[16], db[16];
        rd_seg7_str(a, ta);
        rd_seg7_str(b, tb);
        fk_date_str(&a->clk, da);
        fk_date_str(&b->clk, db);
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody && strcmp(ta, tb) == 0 && strcmp(da, db) == 0;
        return same ? 0 : whole(out);
    }
    dc_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || a->h24 != b->h24 || sa.reduced != sb.reduced) return whole(out);
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.stale != sb.stale) add(&L, RD_DC_STALE);
    if (memcmp(sa.lamp, sb.lamp, sizeof sa.lamp) != 0) add(&L, RD_DC_LAMPS);
    if (strcmp(sa.dig, sb.dig) != 0 || sa.segk != sb.segk) add(&L, RD_DC_TIME);
    else if (sa.colon_a != sb.colon_a) add(&L, RD_DC_COLON);
    if (strcmp(sa.strip, sb.strip) != 0 || sa.msg != sb.msg) add(&L, RD_DC_STRIP);
    if (sa.sec != sb.sec) add(&L, RD_DC_SECG);
    if (sa.res != sb.res) add(&L, RD_DC_RESG);
    if (sa.collar != sb.collar) add(&L, RD_DC_COLLAR);
    else if (sa.dn != sb.dn) add(&L, RD_DC_BTN_BOX);   /* inside the collar's square */
    for (unsigned i = 0; i < L.n; i++) out[i] = L.r[i];
    return (uint8_t)L.n;
}

/* ---- taps: the button's circle r 34 (inclusive), else anywhere on the panel */
static bool in_button(int x, int y)
{
    const int dx = x - BX, dy = y - BY;
    return dx * dx + dy * dy <= RD_DC_BTN_R * RD_DC_BTN_R;
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    if (x < 0 || x >= W || y < 0 || y >= W) return FK_TAP_NONE;
    return in_button(x, y) ? FK_TAP_PULSE : FK_TAP_NEXT;
}

/* the button: the pulse (its two messages), the page left as it is; elsewhere the strip's next page (RAM only;
 * FK_TAP_NEXT: fk_face_gesture draws the new variant and nothing more), and while the clock is unset the other of its
 * two lines. A NULL in (only from outside the runtime, fk/face.h) counts as a set clock */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    const fk_tap_t t = tap(ctx, x, y);
    if (t != FK_TAP_NEXT) return t;
    if (in != NULL && !in->clk.set) *variant = *variant % 2 ? 0 : 1;
    else *variant = (uint16_t)((*variant + 1u) % RD_DC_PAGES);
    return t;
}

const fk_face_ops_t RD_FACE_CONSOLE = {
    .key = "console",
    .ctx_bytes = sizeof(dc_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = WAKE_MS,
    .pulse_len_ms = 3200,   /* [ REQUEST LOGGED ] to 1 500 ms, [ REQUEST IGNORED ] to 3 200 */
    .tap_var = tap_var,
    .stills_extra = 1,      /* still 11: the strip's RESERVE page (RD_DC_TAPPED_VARIANT) */
};
