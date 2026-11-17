/* Face 4, Roundel analog (dw/faces.h; dw wave 1 Task 13, spec sections 10 and 11), from faces-classic.js anaDial,
 * anaHand and classic-analog (frame, sleepFrame).
 *
 * Layers (tools/export/dw_layers.json): dw_r4_base (the dial: the minute track, the twelve dark roundel wells with
 * their engraved numerals, the stagger rings and the chapter hairline, RGB565), dw_r4_lit (each hour's lit roundel, its
 * numeral in dark ink, cut from the designers' settled frame: twelve 74 px cells at geometry.r4's boxes, RGB565) and
 * the shared Dymo atlas dw_dymo (the date tape, TIME NOT SET, the stale strip). Drawn here: the hands (hands.c), the
 * hexagonal hub, the unset clock's --:--, and the glance.
 *
 * Motion at rest stays in regions (spec section 11): the seconds hand ticks once a second with its 160 ms damped
 * overshoot (dw_second_angle; none under reduced motion), redrawing only the old and the new hand's boxes (4 each
 * along the hand, 8 rects); the hour and minute hands jump each minute (the mockup's minute hand creeps with the
 * seconds: left out for the dirty budget, as on #1). cadence_ms 40 runs frames only in the overshoot: from 160 ms on
 * the angle is exact and dirty() reports nothing. In the glance nothing moves within a minute. The chime's breath (Task
 * 16, spec section 13) lays a lamp glow over the hour roundel at the envelope's level, redrawing only its cell (merged
 * into the box it grows least when the seconds tick in the same frame); not under reduced motion. The hands' drop
 * shadows are left out.
 * The wake (wake_ms < UINT32_MAX, the face's own: no doors): a wave of light runs one lap of the roundels from the hour
 * after the current one and comes to rest on it (75 ms a roundel, the mockup's Gaussian), while the hands sweep in
 * from 12 (easeInOut over 900 ms); by DW_R4_WAKE_MS it is the settled frame exactly. A roundel part lit in the wave is
 * its lit cell blended over the dial at its level (the mockup redraws the sprite at the level: close, not the same).
 * Pure: no libm trig (fk_sincos), no allocation. */
#include <math.h>
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
#define GREEN RGB(0xb8, 0xc8, 0xb0)
#define TAPE_INK RGB(220, 218, 213)   /* tape()'s rgba(238, 236, 230, .92) over its #111 strip */

const omni_rect_t DW_R4_STALE = {164, 126, 300, 156};

typedef struct {
    bool base_ok, lit_ok, dymo_ok;
    fk_img_t base, lit_img, dymo_img;
    fk_atlas_t lit, dymo;
    fk_cell_t lit_cells[12];   /* the lit strip as an atlas: cell h - 1 is hour h's, drawn at its box's top-left */
} r4_t;

static const char LIT_GLYPHS[] = "abcdefghijkl";

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float in_out_cubic(float s) { const float u = -2 * s + 2; return s < 0.5f ? 4 * s * s * s : 1 - u * u * u / 2; }

static int hour12(const fk_clock_t *c) { return c->h % 12 ? c->h % 12 : 12; }

uint8_t dw_r4_lit(const fk_face_in_t *in, int h)
{
    if (!in->clk.set || h < 1 || h > 12) return 0;
    const int hr = hour12(&in->clk);
    if (in->wake_ms == UINT32_MAX) return h == hr ? 255 : 0;
    /* frame(): the wave passes roundel index k at wake_ms / 75; a full lap plus the hours, then the hour stays lit */
    const float pos = (float)in->wake_ms / 75.f, target = (float)(hr + 12), k = (float)(h <= hr ? h + 12 : h);
    float L = 0;
    if (pos < target + 1) L = expf(-(pos - k) * (pos - k) / 1.2f) * 0.75f;
    if (h == hr) {
        const float rest = clampf((pos - target) / 3.f, 0, 1);
        if (rest > L) L = rest;
        if (pos > target + 3) L = 1;
    }
    return L > 0.02f ? (uint8_t)(L * 255.f + 0.5f) : 0;
}

/* ---- the face's state for an input */
typedef struct {
    bool set, stale;
    float ha, ma, sa;   /* the hands: hour and minute jump each minute; the seconds tick */
    uint8_t breath;     /* the chime's glow over the hour roundel, 0..BREATH_A8 (0: none) */
} r4_state_t;

#define BREATH_A8 150   /* the glow's alpha at the envelope's peak */
#define BREATH_R 36.f   /* about the cell's centre: inside its DW_R4_LIT_CELL box */

static void state_of(const fk_face_in_t *in, r4_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->set = in->clk.set;
    st->stale = in->clk.set && in->clk.stale;
    if (!st->set) return;
    const float settle = in->wake_ms == UINT32_MAX ? 1.f : in_out_cubic(clampf((float)in->wake_ms / 900.f, 0, 1));
    st->ma = ((float)in->clk.m / 60.f) * 2 * PI_F * settle;
    st->ha = (((float)(in->clk.h % 12) + (float)in->clk.m / 60.f) / 12.f) * 2 * PI_F * settle;
    st->sa = dw_second_angle_in(in) * settle;
    if (!in->reduced && in->chime > 0)   /* the chime's breath (spec section 13), not under reduced motion */
        st->breath = (uint8_t)(clampf(in->chime, 0, 1) * (float)BREATH_A8 + 0.5f);
}

/* the hour roundel's cell (every pixel its breath can touch) */
static omni_rect_t hour_cell(const fk_clock_t *c)
{
    const int h = hour12(c), x = DW_R4_LIT_AT[h - 1][0], y = DW_R4_LIT_AT[h - 1][1];
    return (omni_rect_t){(int16_t)x, (int16_t)y, (int16_t)(x + DW_R4_LIT_CELL), (int16_t)(y + DW_R4_LIT_CELL)};
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool r4_bind(void *ctx, fk_asset_fn get)
{
    r4_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->base_ok = img(get, ASSET_DW_R4_BASE, &k->base, FK_CF_RGB565, W, W);
    k->lit_ok = img(get, ASSET_DW_R4_LIT, &k->lit_img, FK_CF_RGB565, 12 * DW_R4_LIT_CELL, DW_R4_LIT_CELL);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    for (int i = 0; i < 12; i++)
        k->lit_cells[i] = (fk_cell_t){(int16_t)(i * DW_R4_LIT_CELL), 0, DW_R4_LIT_CELL, DW_R4_LIT_CELL, 0, 0,
                                      DW_R4_LIT_CELL};
    k->lit = (fk_atlas_t){&k->lit_img, k->lit_cells, LIT_GLYPHS, DW_R4_LIT_CELL};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    return k->base_ok && k->lit_ok && k->dymo_ok;
}

/* ---- drawing */
static void fill(uint16_t *fb, int stride, omni_rect_t clip, uint16_t v)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = v;
    }
}

static void rect(uint16_t *fb, int stride, const omni_rect_t *clip, float x0, float y0, float x1, float y1,
                 uint16_t rgb, uint8_t a8)
{
    const omni_pt_t q[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, clip, q, 4, rgb, a8);
}

/* "--:--" in IBM Plex Sans 500 at px 60 or 24 (the ink boxes measured from the font: the hyphen 16 x 7 px at 60, the
 * colon's two dots 9 x 10 px), its cells centred at cx[0..4] on the baseline */
static void dashes(uint16_t *fb, int stride, const omni_rect_t *clip, const float cx[5], float base, bool big,
                   uint16_t rgb, uint8_t a8)
{
    for (int i = 0; i < 5; i++) {
        const float c = cx[i];
        if (i == 2) {
            if (big) {
                rect(fb, stride, clip, c - 4.5f, base - 32.f, c + 4.5f, base - 22.f, rgb, a8);
                rect(fb, stride, clip, c - 4.5f, base - 9.f, c + 4.5f, base + 1.f, rgb, a8);
            } else {
                rect(fb, stride, clip, c - 1.7f, base - 13.f, c + 1.3f, base - 10.f, rgb, a8);
                rect(fb, stride, clip, c - 1.7f, base - 4.f, c + 1.3f, base, rgb, a8);
            }
        } else if (big) {
            rect(fb, stride, clip, c - 8.f, base - 22.f, c + 8.f, base - 15.f, rgb, a8);
        } else {
            rect(fb, stride, clip, c - 3.8f, base - 9.f, c + 3.2f, base - 6.f, rgb, a8);
        }
    }
}

static void hex(float cx, float cy, float r, omni_pt_t p[6])
{
    static const float CS[6][2] = {{1.f, 0.f}, {0.5f, 0.8660254f}, {-0.5f, 0.8660254f}, {-1.f, 0.f},
                                   {-0.5f, -0.8660254f}, {0.5f, -0.8660254f}};
    for (int i = 0; i < 6; i++) p[i] = (omni_pt_t){cx + r * CS[i][0], cy + r * CS[i][1]};
}

static void r4_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const r4_t *k = ctx;
    r4_state_t st;
    state_of(in, &st);
    if (k->base_ok) fk_blit(fb, stride, clip, &k->base, 0, 0, 255);
    else fill(fb, stride, clip, 0);
    /* the lit roundels: the hour's, and the wake's wave */
    for (int h = 1; h <= 12 && k->lit_ok; h++) {
        const uint8_t L = dw_r4_lit(in, h);
        const int x = DW_R4_LIT_AT[h - 1][0], y = DW_R4_LIT_AT[h - 1][1];
        if (!L || x >= clip.x1 || y >= clip.y1 || x + DW_R4_LIT_CELL <= clip.x0 || y + DW_R4_LIT_CELL <= clip.y0)
            continue;
        const char g[2] = {LIT_GLYPHS[h - 1], 0};
        fk_atlas_text(fb, stride, clip, &k->lit, g, x, y, 0, L);
    }
    if (st.set && st.breath) {   /* the chime's breath: a lamp glow over the hour roundel, under the hands */
        const omni_rect_t b = hour_cell(&in->clk);
        fk_glow_add(fb, stride, &clip, (float)b.x0 + DW_R4_LIT_CELL / 2.f, (float)b.y0 + DW_R4_LIT_CELL / 2.f, BREATH_R,
                    LAMP, st.breath, 0, 0);
    }
    if (!st.set) {   /* no hands: TIME NOT SET over --:-- (60 px cells of 35, the colon 18), no roundel lit */
        if (k->dymo_ok) dw_tape(fb, stride, &clip, &k->dymo, "TIME NOT SET", C, C - 26.f, true, TAPE_INK);
        static const float CX[5] = {171.5f, 206.5f, 233.f, 259.5f, 294.5f};
        dashes(fb, stride, &clip, CX, C + 46.f, true, CREAM, 140);
        return;
    }
    if (k->dymo_ok) {   /* the date tape at 6, the stale marker above the hub (under the hands: never hiding them) */
        static const char DOW[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
        char t[8];
        memcpy(t, in->clk.wday < 7 ? DOW[in->clk.wday] : "---", 3);
        int n = 3;
        t[n++] = ' ';
        const unsigned md = in->clk.mday > 99 ? 99 : in->clk.mday;
        if (md >= 10) t[n++] = (char)('0' + md / 10);
        t[n++] = (char)('0' + md % 10);
        t[n] = 0;
        dw_tape(fb, stride, &clip, &k->dymo, t, C, C + 92.f, true, TAPE_INK);
        if (st.stale && DW_R4_STALE.x0 < clip.x1 && clip.x0 < DW_R4_STALE.x1 && DW_R4_STALE.y0 < clip.y1 &&
            clip.y0 < DW_R4_STALE.y1)
            dw_stale_mark(fb, stride, &clip, &k->dymo, C, C - 92.f);
    }
    dw_hand_baton(fb, stride, &clip, st.ha, true, 255);
    dw_hand_baton(fb, stride, &clip, st.ma, false, 255);
    dw_hand_second(fb, stride, &clip, st.sa);
    /* the hub: a pale console-green hexagon, a red pin */
    omni_pt_t hx[6];
    hex(C, C, 9.5f, hx);
    static const uint16_t HUB[2] = {RGB(0xe2, 0xec, 0xdc), RGB(0x6f, 0x7d, 0x68)};
    fk_poly_grad(fb, stride, &clip, hx, 6, C - 9.f, C - 9.f, C + 9.f, C + 9.f, HUB, 2, 255);
    fk_poly_stroke(fb, stride, &clip, hx, 6, 1.f, RGB(0, 0, 0), 179);
    fk_disc(fb, stride, &clip, C, C, 2.6f, RGB(0xe0, 0x40, 0x2a), 255);
}

/* sleepFrame(): the twelve lip rings (the hour's a little brighter), the outline hands and the hub, on the glance
 * orbit; --:-- unset. Colours at most 110 per channel (cream .38, the lamp .42), in the glances' lighten ink
 * (fk_line_max: crossing strokes never add up). */
static omni_pt_t at(float lx, float ly, float s, float c, float ox, float oy)
{
    return (omni_pt_t){ox + lx * c - ly * s, oy + lx * s + ly * c};
}

static void r4_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    (void)ctx;
    fill(fb, stride, clip, 0);
    const float ox = C + (float)in->odx, oy = C + (float)in->ody;
    const int hr = in->clk.set ? hour12(&in->clk) : 0;
    for (int h = 1; h <= 12; h++) {
        float s, c;
        fk_sincos((float)h * (PI_F / 6.f), &s, &c);
        const bool cur = h == hr;
        fk_ring_max(fb, stride, &clip, ox + (float)DW_R4_R * s, oy - (float)DW_R4_R * c, 21.6f, cur ? 2.f : 1.2f,
                cur ? LAMP : CREAM, cur ? 107 : 51);
    }
    if (!in->clk.set) {   /* "--:--" 24 px, centred (textBaseline middle: the baseline 9 px below) */
        const float x0 = ox - 22.95f;
        const float CX[5] = {x0 + 4.81f, x0 + 14.43f, x0 + 22.95f, x0 + 31.47f, x0 + 41.09f};
        dashes(fb, stride, &clip, CX, oy + 9.f, false, CREAM, 71);
        return;
    }
    r4_state_t st;
    state_of(in, &st);
    const uint16_t ink = CREAM;
    const uint8_t a = 97;
    float s, c;
    fk_sincos(st.ha, &s, &c);
    omni_pt_t p0 = at(-5.f, 14.f, s, c, ox, oy), p1 = at(-4.f, -67.f, s, c, ox, oy);
    fk_line_max(fb, stride, &clip, p0.x, p0.y, p1.x, p1.y, 1.5f, ink, a);
    p0 = at(5.f, 14.f, s, c, ox, oy), p1 = at(4.f, -67.f, s, c, ox, oy);
    fk_line_max(fb, stride, &clip, p0.x, p0.y, p1.x, p1.y, 1.5f, ink, a);
    const omni_pt_t rc = at(0.f, -78.f, s, c, ox, oy);
    fk_ring_max(fb, stride, &clip, rc.x, rc.y, 11.5f, 1.5f, ink, a);
    const omni_pt_t t0 = at(-3.f, -90.f, s, c, ox, oy), t1 = at(0.f, -104.f, s, c, ox, oy);
    const omni_pt_t t2 = at(3.f, -90.f, s, c, ox, oy);
    fk_line_max(fb, stride, &clip, t0.x, t0.y, t1.x, t1.y, 1.5f, ink, a);
    fk_line_max(fb, stride, &clip, t1.x, t1.y, t2.x, t2.y, 1.5f, ink, a);
    fk_sincos(st.ma, &s, &c);
    const omni_pt_t m[5] = {at(-5.f, 20.f, s, c, ox, oy), at(-4.f, -120.f, s, c, ox, oy), at(0.f, -194.f, s, c, ox, oy),
                            at(4.f, -120.f, s, c, ox, oy), at(5.f, 20.f, s, c, ox, oy)};
    fk_poly_stroke_max(fb, stride, &clip, m, 5, 1.5f, ink, a);
    omni_pt_t hx[6];
    hex(ox, oy, 8.f, hx);
    fk_poly_stroke_max(fb, stride, &clip, hx, 6, 1.5f, GREEN, 89);
    if (st.stale)   /* the stale lamp, dim: inside the marker's rect on every orbit offset */
        fk_disc(fb, stride, &clip, 181.f + (float)in->odx, 141.f + (float)in->ody, 2.2f, RGB(96, 64, 22), 255);
}

static long area(omni_rect_t r) { return (long)(r.x1 - r.x0) * (r.y1 - r.y0); }

static omni_rect_t join(omni_rect_t a, omni_rect_t b)
{
    return (omni_rect_t){a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
                         a.y1 > b.y1 ? a.y1 : b.y1};
}

/* r into the one of out[0..n) whose box grows least */
static void merge(omni_rect_t *out, int n, omni_rect_t r)
{
    int best = 0;
    long grow = -1;
    for (int i = 0; i < n; i++) {
        const long g = area(join(out[i], r)) - area(out[i]);
        if (grow < 0 || g < grow) best = i, grow = g;
    }
    out[best] = join(out[best], r);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t r4_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->wake_ms != b->wake_ms || a->reduced != b->reduced)
        return full(out);   /* the mode, the state, the orbit, the wake, reduced motion */
    if (!b->clk.set) return 0;   /* nothing moves on an unset clock */
    if (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday || a->clk.mon != b->clk.mon ||
        a->clk.wday != b->clk.wday)
        return full(out);   /* the minute: the hands jump (the lit hour, the date) */
    if (b->glance) return 0;   /* no seconds in the glance */
    r4_state_t ra, rb;
    state_of(a, &ra);
    state_of(b, &rb);
    const bool breath = ra.breath != rb.breath;   /* the chime's: the hour roundel's cell */
    const float sa = dw_second_angle_in(a), sb = dw_second_angle_in(b);
    if (sa == sb) {
        if (!breath) return 0;
        out[0] = hour_cell(&b->clk);
        return 1;
    }
    dw_hand_second_rects(sa, out);
    dw_hand_second_rects(sb, out + 4);
    if (breath) merge(out, 8, hour_cell(&b->clk));   /* no 9th rect: into the box it grows least */
    return 8;
}

const fk_face_ops_t DW_FACE_ROUNDEL = {
    .key = "roundel",
    .ctx_bytes = sizeof(r4_t),
    .cadence_ms = 40,
    .bind = r4_bind,
    .draw = r4_draw,
    .glance = r4_glance,
    .dirty = r4_dirty,
    .tap = NULL,
    .reveal = false,
    .wake_len_ms = DW_R4_WAKE_MS,
};
