/* Face 9, the Console panel (dw/faces.h; dw wave 2 Task 9, spec sections 10 and 11), from faces-classic.js CON, PANEL,
 * meterFace, meterNeedle, conBase and classic-console: the hexagonal console seen from above, the time large in the
 * rotor collar, and six period instruments on the six sloping panels, each with its Dymo label.
 *
 * Layers (tools/export/dw_layers.json): dw_p9_base (conBase: the panels, the ridges, the collar, the glass column's
 * ring, the five meter faces, the lamps' plate and toggles and the Dymo labels; 466 RGB565 over black), the shared A8
 * text atlases dw_p60 (the time and --:--), dw_cor16 (the day) and dw_cap11 (the date line, NO TIME SIGNAL), tinted at
 * draw (WD25), and dw_dymo (the stale strip). Drawn here: the four meter needles (hands.c dw_p9_needle), the seconds
 * dial's red hand and hub, the three signal lamps (hands.c dw_dome_lamp), the centre's text, the stale mark and the
 * glance.
 *
 * Readings (dw_p9_values, by PANEL index): HOURS ((h % 12) + m / 60) / 12, a step a minute; MINUTES (m + s / 60) / 60, a
 * step a second; POWER battery / 100 in amber (0 unknown); SECONDS the red hand at (s + tick) / 60 turns, the tick a
 * 140 ms spring past each second; SIGNAL the lamps (dw_p9_lamps: green synced, amber stale, red unset; the in-flight lamp
 * on for 700 ms of each 2 s); SOLAR the day's share between today's sunrise and sunset (in->sun_rise_m, sun_set_m) in
 * whole minutes, parked at its stops before and after, 0 with no sun.
 * The wake (wake_len_ms 1570, :1012): needle i springs to its value by springTo(t - 120 - 90 i, 1000) (expf and the table
 * cosine; frames in motion only, never a still).
 * Motion stays in regions (the S1 lesson: full-frame canvas wakes ran 5-6 fps): at rest dirty() names only the meters
 * whose reading changed (the minutes meter each second, the seconds dial while it ticks), the lamps when the in-flight
 * lamp turns, and the centre at the minute; the wake redraws the five needles' windows, never the whole frame. A meter
 * is named exactly when its value differs, so skips and back-steps are covered by construction.
 * Reduced motion: the needles at their values with no spring, the seconds hand steps with no tick, the in-flight lamp
 * steady at .6; no wake.
 * Unset: --:-- (cream .5) and NO TIME SIGNAL (red .9); the hour, minute and seconds hands and solar at 0; only the red lamp.
 * Stale: the amber lamp and the stale mark centred (233, 301), inside the collar.
 * Pure: the table trig (fk_sincos) and +, -, x, /, sqrtf in every still; no allocation. */
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
/* faces-classic.js P, and its alphas over the black collar as colour (the faces dim by colour, never by alpha) */
#define CREAM RGB(0xF2, 0xEA, 0xD8)
#define CREAM_55 RGB(133, 129, 119)
#define CREAM_50 RGB(121, 117, 108)
#define CREAM_30 RGB(73, 70, 65)
#define GREEN_85 RGB(156, 170, 150)
#define GREEN_32 RGB(59, 64, 56)
#define RED_90 RGB(202, 58, 38)
#define AMBER RGB(0xF2, 0xA3, 0x3A)
#define LAMP_OK RGB(0x5F, 0xCF, 0x6A)
#define LAMP_RED RGB(0xE0, 0x40, 0x2A)
#define LAMP_FLY RGB(0xFF, 0xF4, 0xD6)
#define HAND_RED RGB(0xE0, 0x40, 0x2A)
#define HUB RGB(0xE2, 0xEC, 0xDC)
#define STALE_DIM RGB(96, 64, 22)
#define DAY_BASE 199    /* cy - 34 */
#define TIME_BASE 253   /* cy + 20 */
#define DATE_BASE 279   /* cy + 46 */
#define CELL 34         /* the time's cells (:1040 cells(.., 34, 16)) */
#define COLON 16
#define STALE_X 233.f
#define STALE_Y 301.f
#define WAKE_LEN DW_P9_WAKE_MS

/* instAt(p, 156): 156 cos 30 = 135.0999624, 156 sin 30 = 78 */
const float DW_P9_XY[6][2] = {{233.f, 77.f},  {368.0999624f, 155.f}, {368.0999624f, 311.f},
                              {233.f, 389.f}, {97.9000376f, 311.f},  {97.9000376f, 155.f}};
const omni_rect_t DW_P9_METER[6] = {{197, 41, 269, 113},  {332, 119, 405, 191}, {332, 275, 405, 347},
                                    {197, 353, 269, 425}, {61, 275, 134, 347},  {61, 119, 134, 191}};
/* the lamps at (x - 18, y - 8), (x, y - 8), (x + 18, y - 8) r 5 with their 15 px spill, about (97.9, 311) */
const omni_rect_t DW_P9_LAMPS = {63, 287, 133, 320};
const omni_rect_t DW_P9_CENTRE = {150, 182, 316, 286};   /* the day (rows 186-200), time (209-255), date (270-282) */
const omni_rect_t DW_P9_STALE = {164, 286, 302, 316};    /* dw_stale_mark(233, 301)'s reach */

_Static_assert(ASSET_DW_P9_BASE_W == W && ASSET_DW_P9_BASE_H == W, "dw_p9_base is the whole screen");

typedef struct {
    bool base_ok, p60_ok, cor_ok, cap_ok, dymo_ok;
    fk_img_t base, p60_img, cor_img, cap_img, dymo_img;
    fk_atlas_t p60, cor, cap, dymo;
} p9_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

/* springTo(t, 1000) (:31): 1 - e^(-6 x) cos(9 x) (1 - x), x = clamp(t / 1000); 0 before its start */
static float spring(float t)
{
    if (!(t > 0)) return 0;
    const float x = t / 1000.f;
    if (x >= 1) return 1;
    float s, c;
    fk_sincos(9.f * x, &s, &c);
    return 1.f - expf(-6.f * x) * c * (1.f - x);
}

/* needle i's share of its value t ms into the wake: 1 settled, under reduced motion and in the glance */
static float wake_k(const fk_face_in_t *in, int i)
{
    if (in->reduced || in->glance || in->wake_ms >= WAKE_LEN) return 1;
    return spring((float)in->wake_ms - 120.f - 90.f * (float)i);
}

static float solar(const fk_face_in_t *in)
{
    const int rise = in->sun_rise_m, set = in->sun_set_m;
    if (!in->clk.set || rise < 0 || set <= rise) return 0;
    const int now = (int)(in->clk.h % 24u) * 60 + (int)(in->clk.m % 60u);   /* whole minutes: it moves at the minute */
    return clampf((float)(now - rise) / (float)(set - rise), 0, 1);
}

void dw_p9_values(const fk_face_in_t *in, float v[6])
{
    const bool set = in->clk.set;
    const float h = (float)(in->clk.h % 12u), m = (float)(in->clk.m % 60u), s = (float)(in->clk.s % 60u);
    const unsigned ms = in->clk.ms % 1000u;
    float tick = 0;
    if (!in->reduced && ms < 140u) {
        float sn, cs;
        fk_sincos((float)ms / 140.f * PI_F, &sn, &cs);
        tick = sn * 0.25f * (1.f - (float)ms / 140.f);
    }
    v[0] = set ? (h + m / 60.f) / 12.f * wake_k(in, 0) : 0;
    v[1] = set ? (m + s / 60.f) / 60.f * wake_k(in, 1) : 0;
    v[2] = in->battery < 0 ? 0 : (float)(in->battery > 100 ? 100 : in->battery) / 100.f * wake_k(in, 2);
    v[3] = set ? (s + tick) / 60.f * wake_k(in, 3) : 0;
    v[4] = 0;
    v[5] = solar(in) * wake_k(in, 5);
}

void dw_p9_lamps(const fk_face_in_t *in, uint16_t rgb[3], float on[3])
{
    const bool set = in->clk.set, stale = set && in->clk.stale;
    rgb[0] = LAMP_OK, on[0] = set && !stale ? 1.f : 0.f;
    rgb[1] = stale ? AMBER : LAMP_RED, on[1] = set && !stale ? 0.f : 1.f;
    rgb[2] = LAMP_FLY;
    const uint32_t day_ms = (((uint32_t)(in->clk.h % 24u) * 60u + in->clk.m % 60u) * 60u + in->clk.s % 60u) * 1000u +
                            in->clk.ms % 1000u;
    on[2] = !set ? 0.f : in->reduced ? 0.6f : day_ms % 2000u < 700u ? 1.f : 0.f;
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool p9_bind(void *ctx, fk_asset_fn get)
{
    p9_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->base_ok = img(get, ASSET_DW_P9_BASE, &k->base, FK_CF_RGB565, W, W);
    k->p60_ok = img(get, ASSET_DW_P60, &k->p60_img, FK_CF_A8, ASSET_DW_P60_W, ASSET_DW_P60_H);
    k->cor_ok = img(get, ASSET_DW_COR16, &k->cor_img, FK_CF_A8, ASSET_DW_COR16_W, ASSET_DW_COR16_H);
    k->cap_ok = img(get, ASSET_DW_CAP11, &k->cap_img, FK_CF_A8, ASSET_DW_CAP11_W, ASSET_DW_CAP11_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->p60 = (fk_atlas_t){&k->p60_img, DW_CELLS_DW_P60, DW_GLYPHS_DW_P60, ASSET_DW_P60_LINE_H};
    k->cor = (fk_atlas_t){&k->cor_img, DW_CELLS_DW_COR16, DW_GLYPHS_DW_COR16, ASSET_DW_COR16_LINE_H};
    k->cap = (fk_atlas_t){&k->cap_img, DW_CELLS_DW_CAP11, DW_GLYPHS_DW_CAP11, ASSET_DW_CAP11_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    return k->base_ok && k->p60_ok && k->cor_ok && k->cap_ok && k->dymo_ok;
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

/* the centre (:1033-1042): the day (dw_cor16, green .85, tracked 1.5), the time (dw_p60, cream, 34 px cells, the colon
 * 16) and the date line (dw_cap11, cream .55, tracked 2.5: "22 OCT  · PM", the middle dot 0xB7 in the atlas); unset,
 * --:-- (cream .5) and NO TIME SIGNAL (red .9) */
static void centre(const p9_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!in->clk.set) {
        cells(fb, stride, clip, &k->p60, "--:--", (int)C, TIME_BASE, CREAM_50);
        fk_atlas_text_tracked(fb, stride, clip, &k->cap, "NO TIME SIGNAL", C, DATE_BASE, 2.5f, true, RED_90, 255);
        return;
    }
    fk_atlas_text_tracked(fb, stride, clip, &k->cor, DAYS[in->clk.wday % 7u], C, DAY_BASE, 1.5f, true, GREEN_85, 255);
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

/* a line w px wide with round caps (canvas lineCap 'round'): each pixel by its centre's distance to the segment */
static void capsule(uint16_t *fb, int stride, omni_rect_t clip, float x0, float y0, float x1, float y1, float w,
                    uint16_t rgb)
{
    const float h = 0.5f * w, ex = x1 - x0, ey = y1 - y0, ll = ex * ex + ey * ey;
    const int bx0 = (int)floorf((x0 < x1 ? x0 : x1) - h) - 1, bx1 = (int)ceilf((x0 > x1 ? x0 : x1) + h) + 2;
    const int by0 = (int)floorf((y0 < y1 ? y0 : y1) - h) - 1, by1 = (int)ceilf((y0 > y1 ? y0 : y1) + h) + 2;
    const int xa = bx0 > clip.x0 ? bx0 : clip.x0, xb = bx1 < clip.x1 ? bx1 : clip.x1;
    const int ya = by0 > clip.y0 ? by0 : clip.y0, yb = by1 < clip.y1 ? by1 : clip.y1;
    for (int y = ya; y < yb; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        for (int x = xa; x < xb; x++) {
            const float px = (float)x + 0.5f - x0, py = (float)y + 0.5f - y0;
            const float t = ll > 0 ? clampf((px * ex + py * ey) / ll, 0, 1) : 0;
            const float qx = px - t * ex, qy = py - t * ey;
            const float cov = clampf(h + 0.5f - sqrtf(qx * qx + qy * qy), 0, 1);
            if (cov > 0) d[x - clip.x0] = omni_blend565(d[x - clip.x0], rgb, (int)(cov * 255.f + 0.5f));
        }
    }
}

/* the seconds dial's red hand (:1006-1009): 1.6 px, round caps, from 6 px behind its centre to mr - 7 = 29, at v turns;
 * its hub r 2.6 #e2ecdc */
static void seconds_hand(omni_rect_t clip, uint16_t *fb, int stride, float turns)
{
    const float x = DW_P9_XY[3][0], y = DW_P9_XY[3][1];
    float s, c;
    fk_sincos(turns * 2.f * PI_F, &s, &c);
    capsule(fb, stride, clip, x - 6.f * s, y + 6.f * c, x + 29.f * s, y - 29.f * c, 1.6f, HAND_RED);
    fk_disc(fb, stride, &clip, x, y, 2.6f, HUB, 255);
}

static void con_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const p9_t *k = ctx;
    if (k->base_ok) fk_blit(fb, stride, clip, &k->base, 0, 0, 255);
    else fill(fb, stride, clip, 0);
    if (in->clk.set && in->clk.stale && k->dymo_ok && meets(clip, DW_P9_STALE))
        dw_stale_mark(fb, stride, &clip, &k->dymo, STALE_X, STALE_Y);
    float v[6];
    dw_p9_values(in, v);
    static const int NEEDLE[4] = {0, 1, 2, 5};
    for (int n = 0; n < 4; n++) {
        const int i = NEEDLE[n];
        if (meets(clip, DW_P9_METER[i])) dw_p9_needle(fb, stride, &clip, i, v[i], i == 2 ? AMBER : CREAM);
    }
    if (meets(clip, DW_P9_METER[3])) seconds_hand(clip, fb, stride, v[3]);
    if (meets(clip, DW_P9_LAMPS)) {
        uint16_t rgb[3];
        float on[3];
        dw_p9_lamps(in, rgb, on);
        const float x = DW_P9_XY[4][0], y = DW_P9_XY[4][1] - 8.f;
        for (int l = 0; l < 3; l++) dw_dome_lamp(fb, stride, &clip, x + 18.f * (float)(l - 1), y, 5.f, rgb[l], on[l]);
    }
    if (meets(clip, DW_P9_CENTRE)) centre(k, in, clip, fb, stride);
}

/* classic-console sleepFrame (:1050-1058): the collar's hexagon (r 100) and six short ridge strokes (r 106-134) in green
 * .32 (1.2 px), and the time in cream .30, on the glance orbit; stale, the mark's lamp dim */
static void con_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const p9_t *k = ctx;
    fill(fb, stride, clip, 0);
    const float ox = C + (float)in->odx, oy = C + (float)in->ody;
    static const float U[6][2] = {{1.f, 0.f},  {0.5f, 0.8660254f},   {-0.5f, 0.8660254f},
                                  {-1.f, 0.f}, {-0.5f, -0.8660254f}, {0.5f, -0.8660254f}};
    omni_pt_t hex[6];
    for (int i = 0; i < 6; i++) {
        hex[i] = (omni_pt_t){ox + 100.f * U[i][0], oy + 100.f * U[i][1]};
        fk_line_max(fb, stride, &clip, ox + 106.f * U[i][0], oy + 106.f * U[i][1], ox + 134.f * U[i][0],
                    oy + 134.f * U[i][1], 1.2f, GREEN_32, 255);
    }
    fk_poly_stroke_max(fb, stride, &clip, hex, 6, 1.2f, GREEN_32, 255);
    char hm[6] = "--:--";
    if (in->clk.set) hm_text(in, hm);
    if (k->p60_ok) cells(fb, stride, clip, &k->p60, hm, (int)C + in->odx, TIME_BASE + in->ody, CREAM_30);
    if (in->clk.set && in->clk.stale)   /* inside the stale rect on every orbit offset */
        fk_disc(fb, stride, &clip, STALE_X - 52.f + (float)in->odx, STALE_Y + (float)in->ody, 2.6f, STALE_DIM, 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static bool lamps_differ(const fk_face_in_t *a, const fk_face_in_t *b)
{
    uint16_t ra[3], rb[3];
    float oa[3], ob[3];
    dw_p9_lamps(a, ra, oa);
    dw_p9_lamps(b, rb, ob);
    for (int l = 0; l < 3; l++)
        if (ra[l] != rb[l] || oa[l] != ob[l]) return true;
    return false;
}

static uint8_t con_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced)
        return full(out);   /* the mode, the clock's state, the orbit, reduced motion */
    const bool text = b->clk.set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday ||
                                     a->clk.mon != b->clk.mon || a->clk.wday != b->clk.wday || a->h24 != b->h24);
    if (b->glance) return text ? full(out) : 0;   /* the glance changes only with the time it shows */
    float va[6], vb[6];
    dw_p9_values(a, va);
    dw_p9_values(b, vb);
    uint8_t n = 0;
    static const int MOVES[5] = {0, 1, 2, 3, 5};
    for (int j = 0; j < 5; j++)
        if (va[MOVES[j]] != vb[MOVES[j]]) out[n++] = DW_P9_METER[MOVES[j]];   /* a needle drawn at another value */
    if (lamps_differ(a, b)) out[n++] = DW_P9_LAMPS;
    if (text) out[n++] = DW_P9_CENTRE;
    return n;
}

const fk_face_ops_t DW_FACE_CONSOLE = {
    .key = "console",
    .ctx_bytes = sizeof(p9_t),
    .cadence_ms = 40,
    .bind = p9_bind,
    .draw = con_draw,
    .glance = con_glance,
    .dirty = con_dirty,
    .tap = NULL,
    .reveal = false,
    .wake_len_ms = WAKE_LEN,
    .uses_sun = true,   /* SOLAR reads today's sunrise and sunset (the final fix wave) */
};
