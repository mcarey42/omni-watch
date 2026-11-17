/* Face 8, Broadcast (dw/faces.h; dw wave 2 Task 12, spec sections 10 and 11), from faces-classic.js howlaround,
 * scanlines, bcCentre and classic-broadcast (:772-888): the watch as a 405-line television set. Everything is grey with
 * the blue-grey cast of an old tube; a howlaround ring (the camera-at-its-own-monitor feedback, evoked procedurally)
 * turns round the rim once every 90 s; inside it the time glows in bloomed white with a faint multipath ghost 5 px to
 * the right; a soft hum bar drifts down the picture every 9 s; scanlines run over everything.
 *
 * Layers (tools/export/dw_layers.json): dw_b8_ring (the howlaround as an A8 strip over r 128-233 at n_theta 2048, its
 * level the alpha, the tube's vignette baked into its rows: radial, so it turns with the ring; R147 PR8), dw_b8_dig (the
 * 88 px time, A8), dw_b8_bloom (its shadowBlur 14 alone at half resolution: 44 px, blur 7, one cell a line, each
 * cell stretched x 2 by fk_add_a8; R147 PR8, the Task 12 review's ruling), dw_cap15 (the day and PLEASE STAND BY) and the shared
 * dw_cap11 (the date line, AWAITING TIME SIGNAL) and dw_dymo (the stale strip). All tinted here, by colour.
 *
 * The frame (:845-862), in the mockup's order: black; the ring tinted (200, 204, 214) at .85 (.45 unset), turned by
 * dw_b8_ring_off; the ghost (P.grey .10, +5 px), the bloom ((232, 238, 248) at .5) and the time (#eef1f6) in 50 px cells
 * (the colon 24) about (233, 267); the day (dw_cap15, P.grey .75, tracked 3) at 183; the date line (dw_cap11, P.grey .55,
 * tracked 2.5: "1 NOVEMBER  · AM") at 297; the stale mark at (233, 333); the hum bar; the scanlines (every row with
 * y mod 3 == 2 at half value). The vignette (:861) lies only over the ring (baked) and the hum bar's outer reach: the
 * text and the stale mark lie inside its r 150, where it is clear; the bar's faint ends past r 150 are left unvignetted
 * (it peaks at .035, one 565 step).
 * The motion steps once a second (D3; cadence_ms 0: the shell's update each second): the ring k = round((t mod 90) x
 * 2048 / 90) entries clockwise, t the seconds of the day (86 400 is a multiple of 90 and of 9: no seam at midnight);
 * the hum bar at hy = (t mod 9) / 9 x 586 - 60, a 100 px linear gradient peaking at .035 in (200, 204, 212). The ring
 * reaches the rim, so each second's step is the whole frame (WD34; the harness's AREA row allows it). Unset: the ring at
 * .45 and the bar step on the free-running mono_ms (the clock's fields are not read); PLEASE STAND BY (.9), --:-- (.55)
 * and AWAITING TIME SIGNAL (.6) (:806-811).
 * The switch-on (the wake, wake_len_ms 450, :864-871): for t < 120 ms the bright line, 466 easeOut3(t / 120) wide and
 * 12 px tall (a vertical gradient to (240, 244, 250) at its middle row); then to 450 ms the settled frame scaled
 * vertically about 233 to h = max(4, 466 k), k = easeOut3((t - 120) / 330), each screen row showing the frame's row at
 * its centre (nearest), under the flash (235, 240, 248) at .5 (1 - k). face_generic gives the face a 40 ms timer while it
 * runs (R147 PR6). Reduced motion: the ring held at 0, no bar, no switch-on (the mockup's plain fade is the shell's).
 * Glance (:874-886): the time in P.grey .30, the day (PLEASE STAND BY unset) .28, a 1 px ring r 150 at .14, on the orbit,
 * under the scanlines; stale, the mark's lamp dim.
 * Pure: +, -, x, / and the annulus map (built at bind, libm-free); no allocation. */
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/faces.h"
#include "omni/blend565.h"

#define W 466
#define C 233.f
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
/* P.grey #C8CCD4 at the mockup's alphas over black, by colour (the faces dim by colour, never by alpha) */
#define GREY_90 RGB(180, 184, 191)
#define GREY_75 RGB(150, 153, 159)
#define GREY_60 RGB(120, 122, 127)
#define GREY_55 RGB(110, 112, 117)
#define GREY_30 RGB(60, 61, 64)
#define GREY_28 RGB(56, 57, 59)
#define GREY_14 RGB(28, 29, 30)
#define GREY_10 RGB(20, 20, 21)
#define TIME_RGB RGB(0xee, 0xf1, 0xf6)
#define BLOOM_RGB RGB(232, 238, 248)
#define BLOOM_A 128            /* shadowColor .5 */
#define RING_RGB RGB(200, 204, 214)
#define RING_A 217             /* globalAlpha .85 */
#define RING_A_UNSET 115       /* .45 */
#define HUM_RGB RGB(200, 204, 212)
#define LINE_RGB RGB(240, 244, 250)
#define FLASH_RGB RGB(235, 240, 248)
#define STALE_DIM RGB(96, 64, 22)
#define DAY_BASE 183           /* cy - 50 */
#define STANDBY_BASE 189       /* cy - 44 */
#define TIME_BASE 267          /* cy + 34 */
#define DATE_BASE 297          /* cy + 64 */
#define AWAIT_BASE 295         /* cy + 62 */
#define CELL 50                /* cells(.., 50, 24) */
#define COLON 24
#define GHOST_DX 5
#define STALE_X 233.f
#define STALE_Y 333.f          /* staleMark(cx, cy + 100) */
#define WAKE DW_B8_WAKE_MS
#define LINE_MS DW_B8_LINE_MS
#define ANN_MAX 364544         /* >= fk_annulus_bytes(128, 233, 2048), 363 015 (as at 1024: a uint16 entry a pixel; test_broadcast_registered) */

_Static_assert(ASSET_DW_B8_RING_NT == DW_B8_RING_NT && ASSET_DW_B8_RING_R0 == DW_B8_RING_R0 &&
                   ASSET_DW_B8_RING_NR == DW_B8_RING_R1 - DW_B8_RING_R0,
               "dw_b8_ring is the strip faces.h names");

const omni_rect_t DW_B8_STALE = {164, 318, 302, 348};    /* dw_stale_mark(233, 333)'s reach */
const omni_rect_t DW_B8_CENTRE = {80, 150, 386, 312};    /* the day, the time with its ghost and bloom, the date line */

typedef struct {
    bool ring_ok, dig_ok, bloom_ok, cap15_ok, cap11_ok, dymo_ok;
    fk_img_t ring, dig_img, bloom_img, cap15_img, cap11_img, dymo_img;
    fk_atlas_t dig, cap15, cap11, dymo;
    fk_annulus_t *ann;
    uint64_t ann_mem[ANN_MAX / 8];
} b8_t;

/* the seconds the motion steps on: of the day when set, of mono_ms unset (the clock's fields are not read) */
static uint32_t b8_sec(const fk_face_in_t *in)
{
    if (!in->clk.set) return in->mono_ms / 1000u;
    return ((uint32_t)(in->clk.h % 24u) * 60u + in->clk.m % 60u) * 60u + in->clk.s % 60u;
}

uint16_t dw_b8_ring_off(const fk_face_in_t *in)
{
    if (in->reduced) return 0;
    const uint32_t j = b8_sec(in) % 90u, k = (DW_B8_RING_NT * j + 45u) / 90u;   /* round(j 2048 / 90): never a tie (2048 j is even) */
    return (uint16_t)((DW_B8_RING_NT - k) % DW_B8_RING_NT);
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool b8_bind(void *ctx, fk_asset_fn get)
{
    b8_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->ring_ok = img(get, ASSET_DW_B8_RING, &k->ring, FK_CF_A8, DW_B8_RING_NT, DW_B8_RING_R1 - DW_B8_RING_R0);
    k->dig_ok = img(get, ASSET_DW_B8_DIG, &k->dig_img, FK_CF_A8, ASSET_DW_B8_DIG_W, ASSET_DW_B8_DIG_H);
    k->bloom_ok = img(get, ASSET_DW_B8_BLOOM, &k->bloom_img, FK_CF_A8, ASSET_DW_B8_BLOOM_W, ASSET_DW_B8_BLOOM_H);
    k->cap15_ok = img(get, ASSET_DW_CAP15, &k->cap15_img, FK_CF_A8, ASSET_DW_CAP15_W, ASSET_DW_CAP15_H);
    k->cap11_ok = img(get, ASSET_DW_CAP11, &k->cap11_img, FK_CF_A8, ASSET_DW_CAP11_W, ASSET_DW_CAP11_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->dig = (fk_atlas_t){&k->dig_img, DW_CELLS_DW_B8_DIG, DW_GLYPHS_DW_B8_DIG, ASSET_DW_B8_DIG_LINE_H};
    k->cap15 = (fk_atlas_t){&k->cap15_img, DW_CELLS_DW_CAP15, DW_GLYPHS_DW_CAP15, ASSET_DW_CAP15_LINE_H};
    k->cap11 = (fk_atlas_t){&k->cap11_img, DW_CELLS_DW_CAP11, DW_GLYPHS_DW_CAP11, ASSET_DW_CAP11_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    for (unsigned i = 0; k->bloom_ok && DW_GLYPHS_DW_B8_BLOOM[i]; i++)   /* one cell a line: cell i is a sub-image */
        k->bloom_ok = DW_CELLS_DW_B8_BLOOM[i].x == 0 && DW_CELLS_DW_B8_BLOOM[i].h == ASSET_DW_B8_BLOOM_LINE_H &&
                      DW_CELLS_DW_B8_BLOOM[i].y + ASSET_DW_B8_BLOOM_LINE_H <= ASSET_DW_B8_BLOOM_H;
    k->ann = fk_annulus_build(k->ann_mem, sizeof k->ann_mem, C, C, (float)DW_B8_RING_R0, (float)DW_B8_RING_R1,
                              DW_B8_RING_NT);
    return k->ring_ok && k->dig_ok && k->bloom_ok && k->cap15_ok && k->cap11_ok && k->dymo_ok && k->ann;
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

/* cells() (:54-60): fixed cells (CELL px, the colon COLON) centred at cx, each glyph centred in its cell, in rgb at opa */
static void cells(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *t, int cx, int base,
                  uint16_t rgb, uint8_t opa)
{
    int w = 0;
    for (const char *p = t; *p; p++) w += *p == ':' ? COLON : CELL;
    int px = cx - w / 2;
    for (const char *p = t; *p; p++) {
        const int cw = *p == ':' ? COLON : CELL;
        const char one[2] = {*p, 0};
        fk_atlas_text_rgb(fb, stride, clip, at, one, px, base, cw, rgb, opa, 0);
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

static const char *const DAYS[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char *const MONTHS[12] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                       "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

/* the bloom (the fill's shadow, :814-816) under the time t in cells() about (cx, base): each glyph's half-resolution cell
 * stretched x 2 by fk_add_a8 (additive; over the black centre and the dim ghost it is the mockup's source-over within a
 * level or two), placed by the fill's own advance (the dig atlas), as fk_atlas_text_rgb centres it in its cell */
static void bloom(const b8_t *k, uint16_t *fb, int stride, omni_rect_t clip, const char *t, int cx, int base)
{
    int w = 0;
    for (const char *p = t; *p; p++) w += *p == ':' ? COLON : CELL;
    int px = cx - w / 2;
    for (const char *p = t; *p; p++) {
        const int cw = *p == ':' ? COLON : CELL;
        int gi = -1, di = -1;
        for (int i = 0; DW_GLYPHS_DW_B8_BLOOM[i]; i++)
            if (DW_GLYPHS_DW_B8_BLOOM[i] == *p) gi = i;
        for (int i = 0; DW_GLYPHS_DW_B8_DIG[i]; i++)
            if (DW_GLYPHS_DW_B8_DIG[i] == *p) di = i;
        if (gi >= 0 && di >= 0) {
            const fk_cell_t *c = &DW_CELLS_DW_B8_BLOOM[gi];
            const int left = px + (cw - DW_CELLS_DW_B8_DIG[di].adv) / 2;   /* floor: both are positive */
            const fk_img_t sub = {k->bloom_img.w, (uint16_t)c->h, NULL,
                                  k->bloom_img.a + (size_t)c->y * k->bloom_img.w, FK_CF_A8};
            fk_add_a8(fb, stride, clip, &sub, left + 2 * c->ox, base + 2 * c->oy, 2 * sub.w, 2 * sub.h, BLOOM_RGB,
                      BLOOM_A);
        }
        px += cw;
    }
}

/* bcCentre() (:803-820) */
static void centre(const b8_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!in->clk.set) {
        fk_atlas_text_tracked(fb, stride, clip, &k->cap15, "PLEASE STAND BY", C, STANDBY_BASE, 3.f, true, GREY_90, 255);
        cells(fb, stride, clip, &k->dig, "--:--", (int)C, TIME_BASE, GREY_55, 255);
        fk_atlas_text_tracked(fb, stride, clip, &k->cap11, "AWAITING TIME SIGNAL", C, AWAIT_BASE, 2.5f, true, GREY_60,
                              255);
        return;
    }
    char hm[6];
    hm_text(in, hm);
    cells(fb, stride, clip, &k->dig, hm, (int)C + GHOST_DX, TIME_BASE, GREY_10, 255);   /* the multipath ghost */
    bloom(k, fb, stride, clip, hm, (int)C, TIME_BASE);
    cells(fb, stride, clip, &k->dig, hm, (int)C, TIME_BASE, TIME_RGB, 255);
    fk_atlas_text_tracked(fb, stride, clip, &k->cap15, DAYS[in->clk.wday % 7u], C, DAY_BASE, 3.f, true, GREY_75, 255);
    char date[24];
    char *p = put_u(date, in->clk.mday % 32u, 1);
    p = put_s(p, " ");
    p = put_s(p, MONTHS[in->clk.mon % 12u]);
    if (!in->h24) put_s(p, in->clk.h % 24u < 12 ? "  \267  AM" : "  \267  PM");
    fk_atlas_text_tracked(fb, stride, clip, &k->cap11, date, C, DATE_BASE, 2.5f, true, GREY_55, 255);
}

/* the hum bar (:857-859): rows [hy - 50, hy + 50), a linear gradient 0 -> .035 -> 0 down them, stepped each second */
static void hum(const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const float hy = (float)(b8_sec(in) % 9u) * (586.f / 9.f) - 60.f;
    int y0 = (int)(hy - 50.f) - 1, y1 = (int)(hy + 50.f) + 2;
    if (y0 < clip.y0) y0 = clip.y0;
    if (y1 > clip.y1) y1 = clip.y1;
    for (int y = y0; y < y1; y++) {
        const float t = ((float)y + 0.5f - (hy - 50.f)) / 100.f;
        if (t < 0.f || t >= 1.f) continue;
        const float tri = t < 0.5f ? 2.f * t : 2.f - 2.f * t;
        const int a8 = (int)(0.035f * tri * 255.f + 0.5f);
        if (!a8) continue;
        uint16_t *d = fb + (y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = omni_blend565(d[x], HUM_RGB, a8);
    }
}

/* scanlines() (:823-828): every row with y mod 3 == 2 at half value (each channel halved, floor) */
static void scanlines(omni_rect_t clip, uint16_t *fb, int stride)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        if (y % 3 != 2) continue;
        uint16_t *d = fb + (y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = (uint16_t)((d[x] >> 1) & 0x7BEFu);
    }
}

/* the settled picture (bc.tmp, :845-862) */
static void compose(const b8_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride, bool scan)
{
    fill(fb, stride, clip, 0);
    fk_annulus_draw_a8(k->ann, &k->ring, dw_b8_ring_off(in), RING_RGB, in->clk.set ? RING_A : RING_A_UNSET, fb, stride,
                       clip);
    if (meets(clip, DW_B8_CENTRE)) centre(k, in, clip, fb, stride);
    if (in->clk.set && in->clk.stale && meets(clip, DW_B8_STALE))
        dw_stale_mark(fb, stride, &clip, &k->dymo, STALE_X, STALE_Y);
    if (!in->reduced) hum(in, clip, fb, stride);
    if (scan) scanlines(clip, fb, stride);
}

static float ease_out3(float x)
{
    const float u = 1.f - (x < 0.f ? 0.f : x > 1.f ? 1.f : x);
    return 1.f - u * u * u;
}

/* the switch-on (:864-871) at in->wake_ms < WAKE */
static void switch_on(const b8_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride, bool scan)
{
    const float t = (float)in->wake_ms;
    if (in->wake_ms < LINE_MS) {   /* the line: 12 px about 233, its alpha 0 -> 1 -> 0 down its rows */
        fill(fb, stride, clip, 0);
        const float w = (float)W * ease_out3(t / (float)LINE_MS), x0 = C - w / 2.f, x1 = C + w / 2.f;
        for (int y = clip.y0 < 227 ? 227 : clip.y0; y < clip.y1 && y < 239; y++) {
            const float g = ((float)y + 0.5f - 227.f) / 12.f, ay = g < 0.5f ? 2.f * g : 2.f - 2.f * g;
            uint16_t *d = fb + (y - clip.y0) * stride;
            for (int x = clip.x0; x < clip.x1; x++) {
                float cov = ((float)x + 1.f < x1 ? (float)x + 1.f : x1) - ((float)x > x0 ? (float)x : x0);
                if (cov <= 0.f) continue;
                if (cov > 1.f) cov = 1.f;
                const int a8 = (int)(ay * cov * 255.f + 0.5f);
                if (a8) d[x - clip.x0] = omni_blend565(0, LINE_RGB, a8);
            }
        }
        return;
    }
    /* the picture opening: screen row y shows the settled frame's row at its centre's place in [top, top + h) */
    const float kk = ease_out3((t - (float)LINE_MS) / (float)(WAKE - LINE_MS));
    const float h = (float)W * kk < 4.f ? 4.f : (float)W * kk, top = C - h / 2.f;
    const int flash = (int)(0.5f * (1.f - kk) * 255.f + 0.5f);
    fk_face_in_t s = *in;
    s.wake_ms = UINT32_MAX;
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (y - clip.y0) * stride;
        const float yc = (float)y + 0.5f;
        const omni_rect_t row = {clip.x0, (int16_t)y, clip.x1, (int16_t)(y + 1)};
        if (yc < top || yc >= top + h) {
            fill(d, stride, row, 0);
            continue;
        }
        int sy = (int)((yc - top) * (float)W / h);
        if (sy > W - 1) sy = W - 1;
        compose(k, &s, (omni_rect_t){clip.x0, (int16_t)sy, clip.x1, (int16_t)(sy + 1)}, d, stride, scan);
        if (flash)
            for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = omni_blend565(d[x], FLASH_RGB, flash);
    }
}

/* classic-broadcast sleepFrame (:874-886) */
static void glance(const b8_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride, bool scan)
{
    fill(fb, stride, clip, 0);
    const float ox = (float)in->odx, oy = (float)in->ody;
    char hm[6] = "--:--";
    if (in->clk.set) hm_text(in, hm);
    cells(fb, stride, clip, &k->dig, hm, (int)C + in->odx, TIME_BASE + in->ody, GREY_30, 255);
    fk_atlas_text_tracked(fb, stride, clip, &k->cap15, in->clk.set ? DAYS[in->clk.wday % 7u] : "PLEASE STAND BY",
                          C + ox, DAY_BASE + in->ody, 3.f, true, GREY_28, 255);
    fk_ring_max(fb, stride, &clip, C + ox, C + oy, 150.f, 1.f, GREY_14, 255);
    if (in->clk.set && in->clk.stale)   /* inside the stale rect on every orbit offset */
        fk_disc(fb, stride, &clip, STALE_X - 52.f + ox, STALE_Y + oy, 2.6f, STALE_DIM, 255);
    if (scan) scanlines(clip, fb, stride);
}

void dw_b8_frame(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride, bool scan)
{
    const b8_t *k = ctx;
    if (in->glance) glance(k, in, clip, fb, stride, scan);
    else if (!in->reduced && in->wake_ms < WAKE) switch_on(k, in, clip, fb, stride, scan);
    else compose(k, in, clip, fb, stride, scan);
}

static void b8_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    fk_face_in_t a = *in;
    a.glance = false;
    dw_b8_frame(ctx, &a, clip, fb, stride, true);
}

static void b8_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    fk_face_in_t a = *in;
    a.glance = true;
    dw_b8_frame(ctx, &a, clip, fb, stride, true);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

/* the whole frame whenever anything it shows changes (the ring and the bar each second, the text, the switch-on's
 * steps), else nothing */
static uint8_t b8_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced || a->h24 != b->h24)
        return full(out);   /* the mode, the clock's state, the orbit, reduced motion, 12/24 h */
    const bool set = b->clk.set;
    const bool text = set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.wday != b->clk.wday ||
                              (!b->glance && (a->clk.mday != b->clk.mday || a->clk.mon != b->clk.mon)));
    if (text) return full(out);
    if (b->glance) return 0;   /* the glance changes only with the text it shows */
    const bool wa = !a->reduced && a->wake_ms < WAKE, wb = !b->reduced && b->wake_ms < WAKE;
    if ((wa || wb) && a->wake_ms != b->wake_ms) return full(out);   /* the switch-on's steps and its end */
    if (!b->reduced && b8_sec(a) != b8_sec(b)) return full(out);   /* the ring's and the bar's step (WD34) */
    return 0;
}

const fk_face_ops_t DW_FACE_BROADCAST = {
    .key = "broadcast",
    .ctx_bytes = sizeof(b8_t),
    .cadence_ms = 0,
    .bind = b8_bind,
    .draw = b8_draw,
    .glance = b8_glance,
    .dirty = b8_dirty,
    .tap = NULL,
    .reveal = false,
    .wake_len_ms = WAKE,
};
