/* Face 3, Through the Doors (dw/faces.h; dw wave 2 Task 11, spec sections 10 and 11), from faces-reveal.js
 * buildDoorwayBase, doorwayInterior, doorwayOver and doorwaySleep: the canon inward swing. The doors open away from the
 * eye and their leaves stay, held at 86 degrees as the receding inner faces of the box (DW_DOORS_IN_KEEP); through them
 * a frontal view of the crystal room, the digital time over a smoke backplate, and the hours, minutes and seconds on
 * the floor ring's perspective dial.
 *
 * Layers (tools/export/dw_layers.json): dw_d3_base (the room, RGB565), dw_d3_lance (the six rays from (233, 250),
 * alpha only, added in a warm tint; only its lit box, DW_D3_LANCES), dw_d3_digf (the time's fill, 70 px IBM Plex Sans
 * 500, A8, tinted #fff3de) over dw_d3_digs (the same glyphs' glow and 4 px stroke alone, A8, drawn tinted under the
 * fill: WD25, R147 PR8), dw_d3_plate (the smoke backplate's alpha, blended black), dw_d3_g50 (the glance's time, 50 px,
 * tinted #6b5a3c), and the shared dw_dymo (the date's letters, tinted #b9d6ff; the stale strip). The kept leaves are
 * the doors' own (fk_cols_leaves over the column map built at bind from ASSET_DW_DOORS_M, the swap asset: the leaves
 * follow the build's art set, WD26, WD28), drawn unless in->reveal_live (the doors' band draws them while the swing
 * runs); the leaves' trig is libm-free at the kept angle (doors.c), so the stills that show them are host-identical.
 *
 * Order (the mockup's frame(): the interior, the doors, then doorwayOver): the base, the column's glow (r 120 about
 * (233, 250)), the lances, the hour and minute marks and the seconds light on the floor ring, the chime's ripple; then
 * the kept leaves; then the time: the backplate, the glyphs' glow and stroke, the fill, and the date; then the stale
 * marker. Nothing drawn after the leaves reaches their columns (they end at 80 and start at 386, the backplate's lit box
 * is 97..369), so a frame drawn with doors_live under the doors' done band is the frame drawn without it, byte for byte
 * (test_doorway_end_seam_both_sets).
 *
 * At rest (WD29: the phases from the clock's ms of the day): the column glows at .35 + .08 sin(t / 700) in (255, 180,
 * 90) (t mod 4398, the breath's 2 pi 700 ms period to the ms), redrawing DW_D3_COLUMN each frame; the lances at .4; the
 * hour mark ((h % 12) 5 + m / 12) and the minute mark on the floor ellipse (cx 233, cy 384, rx 158, ry 40), the hour
 * one's r 18 glow; the blue seconds light at s / 60 turns, stepping each second (its old and new boxes). The minute and
 * the date redraw the whole frame.
 * The wake (wake_ms < UINT32_MAX) is the doors' tail on the device timeline (dw_mockup_te, #1's map): the lances flare
 * (revealState's lance), the column takes the bloom (.2), and the time fades in by outCubic(te TEXT_TE0 .. + 600) (the
 * whole time layer at that alpha, as the mockup's globalAlpha: a frame in motion only, never a still); it ends in the
 * settled frame exactly. The mockup starts that fade at te 900, drawing the time over the leaves; here the band draws
 * the leaves over the interior, so the fade starts once both leaves' free edges clear the time's box (x 97 .. 369):
 * TEXT_TE0, te 1000 on the device timeline (683 ms; the edges are at 94 and 385 by 680 ms) and 1040 on Designer A's (the
 * edges at 96 and 383), and a shown time is never under the leaves (wave 2 Task 12, the Task 11 review's M5; Task 11's
 * deviation 7 assumed the leaves were clear by te 900, which holds on neither timeline).
 * The chime (spec section 13; not under reduced motion, in the glance or unset): the column (+ .6 x the envelope) and
 * the lances (+ .8 x) breathe, and a ripple ellipse 74 x 18 about the floor's centre grows as each swell falls (k = 2.44
 * - 1.44 x the envelope, at .8 x the envelope, 2 px): the face has the envelope, not the pings' clock, so the mockup's
 * per-ping ripple follows the envelope's falls. No full-screen tint (WD33).
 * Reduced motion: the column steady at .35, the seconds light still steps; the doors cross-fade with the kept leaves in
 * the face (doors_live false throughout, R147 PR11).
 * Unset: no marks and no seconds light; --:-- and no date; the column breathes on the free-running mono_ms (the clock's
 * fields are not read). Stale: dw_stale_mark centred (233, 372), over the floor; in the glance, its lamp dim.
 * Glance (WD32, art-set neutral): the seam hairline (#20222c, x 232, y 196-416) and the time from dw_d3_g50 in #6b5a3c
 * centred (233, 250), on the glance orbit; no window outlines.
 * Pure: the table trig (fk_sincos) in every still; sqrtf only in the chime's ripple (a frame in motion); no
 * allocation. */
#include <math.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/doors.h"
#include "dw/faces.h"
#include "omni/blend565.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define COL_X 233                      /* the column glow's centre (glowAt(.., C, 250, 120, ..)) */
#define COL_Y 250
#define COL_R 120
#define COL_RGB RGB(255, 180, 90)
#define BREATH_MS 4398u                /* sin(t / 700): 2 pi 700 = 4398.2 ms */
#define LANCE_RGB RGB(255, 205, 150)   /* the rays' gradient (255, 240, 210) -> (255, 170, 80), weighted by its alpha */
#define FL_CX 233.f                    /* FLOOR: the floor ring's ellipse */
#define FL_CY 384.f
#define FL_RX 158.f
#define FL_RY 40.f
#define HOUR_RGB RGB(0xff, 0xd8, 0xa0)
#define HOUR_GLOW RGB(255, 190, 110)
#define MIN_RGB RGB(0xff, 0xf4, 0xe0)
#define SEC_GLOW RGB(61, 139, 255)
#define SEC_RGB RGB(0xd6, 0xe6, 0xff)
#define PING_RGB RGB(255, 210, 150)
#define FILL_RGB RGB(0xff, 0xf3, 0xde)
#define GLOW_RGB RGB(255, 170, 80)     /* the glyphs' glow, rgba(255, 170, 80, .35) (its alpha is in dw_d3_digs) */
#define STROKE_RGB RGB(10, 4, 2)       /* the glyphs' stroke, rgba(10, 4, 2, .9) */
#define STROKE_LO 51                   /* dw_d3_digs above .2 is the stroke's (under(); judged against the shot) */
#define DATE_RGB RGB(0xb9, 0xd6, 0xff)
#define SEAM_RGB RGB(0x20, 0x22, 0x2c)
#define GLANCE_RGB RGB(0x6b, 0x5a, 0x3c)
#define STALE_DIM RGB(96, 64, 22)
#define TIME_BASE 322                  /* textBaseline middle at 296: the baseline .375 em lower (70 px: 26.25) */
#define DATE_BASE 348                  /* middle at 343 (13 px: 4.875) */
#define G50_BASE 269                   /* middle at 250 (50 px: 18.75) */
#define STALE_X 233.f
#define STALE_Y 372.f

const omni_rect_t DW_D3_COLUMN = {COL_X - COL_R, COL_Y - COL_R, COL_X + COL_R, COL_Y + COL_R};
const omni_rect_t DW_D3_STALE = {164, 357, 302, 387};   /* dw_stale_mark(233, 372)'s reach */
const omni_rect_t DW_D3_LANCES = {ASSET_DW_D3_LANCE_X, ASSET_DW_D3_LANCE_Y, ASSET_DW_D3_LANCE_X + ASSET_DW_D3_LANCE_W,
                                  ASSET_DW_D3_LANCE_Y + ASSET_DW_D3_LANCE_H};
/* the chime's ripple at its widest (k 2.44: rx 180.6, ry 43.9) and its 2 px stroke */
const omni_rect_t DW_D3_RIPPLE = {50, 338, 416, 430};
/* everything the time layer draws: the backplate's lit box, which holds the glyphs and the date (test_doorway_text) */
const omni_rect_t DW_D3_TIME = {ASSET_DW_D3_PLATE_X, ASSET_DW_D3_PLATE_Y, ASSET_DW_D3_PLATE_X + ASSET_DW_D3_PLATE_W,
                                ASSET_DW_D3_PLATE_Y + ASSET_DW_D3_PLATE_H};

typedef struct {
    bool base_ok, lance_ok, digf_ok, digs_ok, plate_ok, g50_ok, dymo_ok, door_ok;
    fk_img_t base, lance, digf_img, digs_img, plate, g50_img, dymo_img, door;
    fk_atlas_t digf, digs, g50, dymo;
    const uint8_t *g120, *g18, *g14;   /* the column's, the hour mark's and the seconds light's glow tables */
    fk_col_t cols[W];                  /* the kept leaves' column map (fk_cols_k at the kept angle) */
} d3_t;

/* ---- the face's state for an input */
typedef struct {
    bool set, stale;
    uint8_t column;      /* the column glow's gain */
    float lance;         /* the lances' gain (above 1: twice, as the mockup's L > 1) */
    uint8_t text;        /* the time layer's alpha (255 settled) */
    uint8_t ping;        /* the chime's ripple: its alpha (0: none) */
    float ping_k;        /* and its scale */
    int hour_i, min_i, sec_s;   /* the floor ring's marks (-1: none) */
} d3_state_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ph(float t, float a, float b) { return clampf((t - a) / (b - a), 0, 1); }
static float out_cubic(float s) { const float u = 1 - s; return 1 - u * u * u; }
static float in_out_cubic(float s)
{
    if (s < 0.5f) return 4 * s * s * s;
    const float u = -2 * s + 2;
    return 1 - u * u * u / 2;
}
static float in_out_sine(float s)
{
    float sn, cs;
    fk_sincos(PI_F * s, &sn, &cs);
    return -(cs - 1) / 2;
}
static uint8_t a8_of(float x) { return (uint8_t)(clampf(x, 0, 1) * 255.f + 0.5f); }

/* the breath, sin(t / 700) for t in ms (the period rounded to 4398 ms) */
static float breath(uint32_t t)
{
    float s, c;
    fk_sincos((float)(t % BREATH_MS) / (float)BREATH_MS * 2 * PI_F, &s, &c);
    return s;
}

/* the time layer's fade start, in te (above) */
#if defined(DW_DOORS_MOCKUP) && DW_DOORS_MOCKUP
#define TEXT_TE0 1040.f   /* the leaves' free edges at x 96 and 383 at 1040 ms (101 at 1020) */
#else
#define TEXT_TE0 1000.f   /* >= dw_mockup_te(680): the free edges at x 94 and 385 (101 at 660) */
#endif

static void state_of(const fk_face_in_t *in, d3_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->set = in->clk.set;
    st->stale = in->clk.set && in->clk.stale;
    st->hour_i = st->min_i = st->sec_s = -1;
    float lance = 1, bloom = 0, text = 1;
    if (in->wake_ms != UINT32_MAX) {   /* revealState's lance and bloom, doorwayOver's fade (te on Designer A's 2.3 s) */
        const float te = dw_mockup_te(in->wake_ms);
        const float flare = out_cubic(ph(te, 520, 980)) * (1 - in_out_sine(ph(te, 1000, 2250)));
        lance = 1 + 1.6f * flare - (1 - ph(te, 400, 900));
        bloom = 0.5f * out_cubic(ph(te, 430, 720)) * (1 - in_out_cubic(ph(te, 720, 1450)));
        text = out_cubic(ph(te, TEXT_TE0, TEXT_TE0 + 600.f));
    }
    const float w = in->clk.set && !in->reduced && in->chime > 0 ? clampf(in->chime, 0, 1) : 0.f;
    float col = 0.35f;
    if (!in->reduced) {   /* the breath: the ms of the day, or the free-running count on an unset clock */
        const uint32_t t = in->clk.set ? ((in->clk.h * 60u + in->clk.m) * 60u + in->clk.s) * 1000u + in->clk.ms
                                       : in->mono_ms;
        col += 0.08f * breath(t);
    }
    st->column = a8_of(col + w * 0.6f + bloom * 0.2f);
    st->lance = lance * 0.4f + w * 0.8f;
    if (st->lance < 0) st->lance = 0;
    st->text = a8_of(text);
    if (w > 0) {
        st->ping = a8_of(w * 0.8f);
        st->ping_k = 2.44f - 1.44f * w;
    }
    if (!st->set) return;
    st->hour_i = (int)(((in->clk.h % 12u) * 5u + in->clk.m / 12u) % 60u);
    st->min_i = (int)(in->clk.m % 60u);
    st->sec_s = (int)(in->clk.s % 60u);
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool d3_bind(void *ctx, fk_asset_fn get)
{
    d3_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->base_ok = img(get, ASSET_DW_D3_BASE, &k->base, FK_CF_RGB565, W, W);
    k->lance_ok = img(get, ASSET_DW_D3_LANCE, &k->lance, FK_CF_A8, ASSET_DW_D3_LANCE_W, ASSET_DW_D3_LANCE_H);
    k->digf_ok = img(get, ASSET_DW_D3_DIGF, &k->digf_img, FK_CF_A8, ASSET_DW_D3_DIGF_W, ASSET_DW_D3_DIGF_H);
    k->digs_ok = img(get, ASSET_DW_D3_DIGS, &k->digs_img, FK_CF_A8, ASSET_DW_D3_DIGS_W, ASSET_DW_D3_DIGS_H);
    k->plate_ok = img(get, ASSET_DW_D3_PLATE, &k->plate, FK_CF_A8, ASSET_DW_D3_PLATE_W, ASSET_DW_D3_PLATE_H);
    k->g50_ok = img(get, ASSET_DW_D3_G50, &k->g50_img, FK_CF_A8, ASSET_DW_D3_G50_W, ASSET_DW_D3_G50_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->door_ok = img(get, ASSET_DW_DOORS_M, &k->door, FK_CF_RGB565, W, W);   /* the swap asset: this build's art */
    k->digf = (fk_atlas_t){&k->digf_img, DW_CELLS_DW_D3_DIGF, DW_GLYPHS_DW_D3_DIGF, ASSET_DW_D3_DIGF_LINE_H};
    k->digs = (fk_atlas_t){&k->digs_img, DW_CELLS_DW_D3_DIGS, DW_GLYPHS_DW_D3_DIGS, ASSET_DW_D3_DIGS_LINE_H};
    k->g50 = (fk_atlas_t){&k->g50_img, DW_CELLS_DW_D3_G50, DW_GLYPHS_DW_D3_G50, ASSET_DW_D3_G50_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    k->g120 = dw_glow_lut(COL_R);
    k->g18 = dw_glow_lut(18);
    k->g14 = dw_glow_lut(14);
    /* the kept leaves at the angle the doors end on (dw_doors_skip_k's, so the band's done columns are these) */
    fk_cols_state_t ds;
    dw_doors_skip_k(&DW_DOORS_IN_KEEP, &ds);
    fk_cols_k(&DW_DOORS_IN_KEEP, ds.th_l, ds.th_r, k->cols);
    return k->g120 && k->g18 && k->g14 && k->base_ok && k->lance_ok && k->digf_ok && k->digs_ok && k->plate_ok &&
           k->g50_ok && k->dymo_ok && k->door_ok;
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

static int whole(float v) { return (int)floorf(v + 0.5f); }

/* point i (of 60, clockwise from 12) on the floor ellipse, and how near it is (1 at the front) */
static void floor_at(int i, float *x, float *y, float *near)
{
    float s, c;
    fk_sincos((float)i * (2 * PI_F / 60.f), &s, &c);
    *x = FL_CX + FL_RX * s, *y = FL_CY - FL_RY * c, *near = (1 - c) / 2;
}

/* a mark on the floor ring (doorwayInterior's mark()): a vertical stroke from y + 2 up len (.6 + .6 near), butt caps */
static void mark(uint16_t *fb, int stride, const omni_rect_t *clip, int i, uint16_t rgb, float len, float w)
{
    float x, y, near;
    floor_at(i, &x, &y, &near);
    fk_line(fb, stride, clip, x, y + 2.f, x, y - len * (0.6f + near * 0.6f), w, rgb, 255);
}

/* the hour mark's glow centre (glowAt(.., hx, hy - 6, 18, ..)), rounded to the pixel (fk_glow_add_lut) */
static void hour_glow_at(int i, int *cx, int *cy)
{
    float x, y, near;
    floor_at(i, &x, &y, &near);
    *cx = whole(x), *cy = whole(y - 6.f);
}

/* the seconds light: its glow's centre (rounded), its front-ness */
static void sec_at(int s, int *cx, int *cy, float *front)
{
    float x, y, near;
    floor_at(s, &x, &y, &near);
    *cx = whole(x), *cy = whole(y + 3.f), *front = near;
}

void dw_d3_sec_rect(int s, omni_rect_t *out)
{
    int cx, cy;
    float front;
    sec_at(s, &cx, &cy, &front);
    *out = (omni_rect_t){(int16_t)(cx - 15), (int16_t)(cy - 15), (int16_t)(cx + 15), (int16_t)(cy + 15)};
}

void dw_d3_mark_rect(int i, bool hour, omni_rect_t *out)
{
    float x, y, near;
    floor_at(i, &x, &y, &near);
    const float len = hour ? 16.f : 22.f;
    int x0 = (int)floorf(x) - 3, x1 = (int)floorf(x) + 4, y0 = (int)floorf(y - len * (0.6f + near * 0.6f)) - 1,
        y1 = (int)floorf(y) + 4;
    if (hour) {   /* and its r 18 glow */
        int cx, cy;
        hour_glow_at(i, &cx, &cy);
        x0 = cx - 18 < x0 ? cx - 18 : x0, x1 = cx + 18 > x1 ? cx + 18 : x1;
        y0 = cy - 18 < y0 ? cy - 18 : y0, y1 = cy + 18 > y1 ? cy + 18 : y1;
    }
    *out = (omni_rect_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}

/* The chime's ripple: an ellipse rx x ry about the floor's centre, stroked 2 px (the edge distance by the implicit
 * function over its gradient), additive-free source-over at a8 */
static void ripple(uint16_t *fb, int stride, const omni_rect_t *clip, float k, uint8_t a8)
{
    if (!a8 || !meets(*clip, DW_D3_RIPPLE)) return;
    const float rx = 74.f * k, ry = 18.f * k, irx2 = 1.f / (rx * rx), iry2 = 1.f / (ry * ry);
    const int x0 = clip->x0 > DW_D3_RIPPLE.x0 ? clip->x0 : DW_D3_RIPPLE.x0;
    const int x1 = clip->x1 < DW_D3_RIPPLE.x1 ? clip->x1 : DW_D3_RIPPLE.x1;
    const int y0 = clip->y0 > DW_D3_RIPPLE.y0 ? clip->y0 : DW_D3_RIPPLE.y0;
    const int y1 = clip->y1 < DW_D3_RIPPLE.y1 ? clip->y1 : DW_D3_RIPPLE.y1;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = fb + (y - clip->y0) * stride - clip->x0;
        const float dy = (float)y + 0.5f - FL_CY;
        for (int x = x0; x < x1; x++) {
            const float dx = (float)x + 0.5f - FL_CX;
            const float f = dx * dx * irx2 + dy * dy * iry2 - 1.f;
            const float gx = dx * irx2, gy = dy * iry2, g2 = gx * gx + gy * gy;
            if (!(g2 > 0)) continue;
            const float d = f / (2.f * sqrtf(g2));   /* about the distance to the ellipse */
            const float cov = clampf(1.5f - (d < 0 ? -d : d), 0, 1);   /* a 2 px stroke: half-width 1, a 1 px ramp */
            if (cov <= 0) continue;
            row[x] = omni_blend565(row[x], PING_RGB, (uint8_t)(cov * (float)a8 + 0.5f));
        }
    }
}

/* the time as text: HH:MM (12 h without a leading zero, as #6's), --:-- unset */
static void time_text(const fk_face_in_t *in, char out[8])
{
    if (!in->clk.set) {
        memcpy(out, "--:--", 6);
        return;
    }
    const unsigned h = in->h24 ? in->clk.h % 24u : (in->clk.h % 24u + 11) % 12 + 1, m = in->clk.m % 60u;
    char *p = out;
    if (h >= 10 || in->h24) *p++ = (char)('0' + h / 10);
    *p++ = (char)('0' + h % 10);
    *p++ = ':';
    *p++ = (char)('0' + m / 10);
    *p++ = (char)('0' + m % 10);
    *p = 0;
}

static const char MON[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char DOW[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

/* the date line: "SAT 24 OCT" */
static void date_text(const fk_face_in_t *in, char out[16])
{
    char *p = out;
    memcpy(p, DOW[in->clk.wday % 7u], 3), p += 3;
    *p++ = ' ';
    const unsigned d = in->clk.mday % 100u;
    if (d >= 10) *p++ = (char)('0' + d / 10);
    *p++ = (char)('0' + d % 10);
    *p++ = ' ';
    memcpy(p, MON[in->clk.mon % 12u], 3), p += 3;
    *p = 0;
}

static int text_x(const fk_atlas_t *at, const char *t) { return whole(C - (float)fk_atlas_width(at, t, 0) / 2.f); }

/* dw_d3_digs under the fill (WD25, R147 PR8): one A8 holds the glyphs' glow and their stroke together, as the mockup
 * composes them (the glow, then the stroke over it). Its alpha A splits by level: up to STROKE_LO (.2: the glow at .35,
 * blurred, stays under it outside the stroke; the level judged against shots/reveal-doorway-2-face.png, where .35 left
 * an orange rim the mockup does not have) it is the glow's, tinted GLOW_RGB at A; above it the glow takes STROKE_LO and
 * the stroke's dark the rest, (A - lo) / (1 - lo) over it: the two together reach A, as the mockup's two layers do. Each
 * glyph's cell at fk_atlas_text_rgb's place (proportional, pen + ox, baseline + oy), every pixel at a / 255. */
static void under(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *text, int x,
                  int baseline, uint8_t a)
{
    int pen = x;
    for (const char *p = text; *p; p++) {
        const char *g = strchr(at->glyphs, *p);
        if (!g) continue;
        const fk_cell_t *c = &at->cells[g - at->glyphs];
        const int dx = pen + c->ox, dy = baseline + c->oy;
        pen += c->adv;
        const int x0 = dx > clip.x0 ? dx : clip.x0, x1 = dx + c->w < clip.x1 ? dx + c->w : clip.x1;
        const int y0 = dy > clip.y0 ? dy : clip.y0, y1 = dy + c->h < clip.y1 ? dy + c->h : clip.y1;
        for (int y = y0; y < y1; y++) {
            const uint8_t *src = at->img->a + (size_t)(c->y + y - dy) * at->img->w + (size_t)(c->x - dx);
            uint16_t *d = fb + (y - clip.y0) * stride - clip.x0;
            for (int xx = x0; xx < x1; xx++) {
                const unsigned A = src[xx];
                if (!A) continue;
                const unsigned gl = A < STROKE_LO ? A : STROKE_LO;
                d[xx] = omni_blend565(d[xx], GLOW_RGB, (int)(gl * a / 255u));
                if (A > STROKE_LO) {
                    const unsigned st = (A - STROKE_LO) * 255u / (255u - STROKE_LO) * a / 255u;
                    if (st) d[xx] = omni_blend565(d[xx], STROKE_RGB, (int)st);
                }
            }
        }
    }
}

uint8_t dw_d3_text_level(const fk_face_in_t *in)
{
    d3_state_t st;
    state_of(in, &st);
    return st.text;
}

void dw_d3_text_boxes(const void *ctx, const fk_face_in_t *in, omni_rect_t *digits, omni_rect_t *date)
{
    const d3_t *k = ctx;
    char t[8], d[16];
    time_text(in, t);
    /* the cells' boxes (the stroke-and-glow atlas's are the widest): pen + ox .. + w, baseline + oy .. + h */
    int x = text_x(&k->digf, t), x0 = W, x1 = 0, y0 = W, y1 = 0;
    for (const char *p = t; *p; p++) {
        const char *g = strchr(DW_GLYPHS_DW_D3_DIGS, *p);
        const char *gf = strchr(DW_GLYPHS_DW_D3_DIGF, *p);
        if (!g || !gf) continue;
        const fk_cell_t *c = &DW_CELLS_DW_D3_DIGS[g - DW_GLYPHS_DW_D3_DIGS];
        const int cx0 = x + c->ox, cy0 = TIME_BASE + c->oy;
        x0 = cx0 < x0 ? cx0 : x0, x1 = cx0 + c->w > x1 ? cx0 + c->w : x1;
        y0 = cy0 < y0 ? cy0 : y0, y1 = cy0 + c->h > y1 ? cy0 + c->h : y1;
        x += DW_CELLS_DW_D3_DIGF[gf - DW_GLYPHS_DW_D3_DIGF].adv;
    }
    *digits = (omni_rect_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
    *date = (omni_rect_t){0, 0, 0, 0};
    if (!in->clk.set) return;
    date_text(in, d);
    const float wd = fk_atlas_width_tracked(&k->dymo, d, 1.f);
    *date = (omni_rect_t){(int16_t)floorf(C - wd / 2.f - 2.f), (int16_t)(DATE_BASE - ASSET_DW_DYMO_LINE_H - 2),
                          (int16_t)(floorf(C + wd / 2.f) + 3.f), (int16_t)(DATE_BASE + 4)};
}

/* the time layer (doorwayOver): the backplate, the glyphs' glow and stroke, the fill, the date; all at a */
static void over(const d3_t *k, const fk_face_in_t *in, uint8_t a, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!a || !meets(clip, DW_D3_TIME)) return;
    fk_blit_rgb(fb, stride, clip, &k->plate, ASSET_DW_D3_PLATE_X, ASSET_DW_D3_PLATE_Y, RGB(0, 0, 0), a);
    char t[8];
    time_text(in, t);
    const int x = text_x(&k->digf, t);
    under(fb, stride, clip, &k->digs, t, x, TIME_BASE, a);   /* the glow and the stroke, under the fill */
    fk_atlas_text_rgb(fb, stride, clip, &k->digf, t, x, TIME_BASE, 0, FILL_RGB, a, 0);
    if (!in->clk.set) return;
    char d[16];
    date_text(in, d);
    fk_atlas_text_tracked(fb, stride, clip, &k->dymo, d, C, DATE_BASE, 1.f, true, DATE_RGB, a);
}

static void d3_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const d3_t *k = ctx;
    d3_state_t st;
    state_of(in, &st);
    fk_blit(fb, stride, clip, &k->base, 0, 0, 255);
    /* the column's breath */
    if (meets(clip, DW_D3_COLUMN))
        fk_glow_add_lut(fb, stride, &clip, COL_X, COL_Y, COL_R, k->g120, COL_RGB, st.column);
    /* the lances, additive at L (and again above 1) */
    if (st.lance > 0 && meets(clip, DW_D3_LANCES)) {
        const float l1 = st.lance < 1 ? st.lance : 1;
        if (st.lance > 1)
            fk_add_a8_2(fb, stride, clip, &k->lance, ASSET_DW_D3_LANCE_X, ASSET_DW_D3_LANCE_Y, ASSET_DW_D3_LANCE_W,
                        ASSET_DW_D3_LANCE_H, LANCE_RGB, a8_of(l1), a8_of(st.lance - 1));
        else
            fk_add_a8(fb, stride, clip, &k->lance, ASSET_DW_D3_LANCE_X, ASSET_DW_D3_LANCE_Y, ASSET_DW_D3_LANCE_W,
                      ASSET_DW_D3_LANCE_H, LANCE_RGB, a8_of(l1));
    }
    /* the hour and minute marks on the floor ring, the blue seconds light */
    if (st.set) {
        omni_rect_t r;
        dw_d3_mark_rect(st.hour_i, true, &r);
        if (meets(clip, r)) {
            mark(fb, stride, &clip, st.hour_i, HOUR_RGB, 16.f, 3.f);
            int cx, cy;
            hour_glow_at(st.hour_i, &cx, &cy);
            fk_glow_add_lut(fb, stride, &clip, cx, cy, 18, k->g18, HOUR_GLOW, 153);   /* .6 */
        }
        dw_d3_mark_rect(st.min_i, false, &r);
        if (meets(clip, r)) mark(fb, stride, &clip, st.min_i, MIN_RGB, 22.f, 2.2f);
        dw_d3_sec_rect(st.sec_s, &r);
        if (meets(clip, r)) {
            int cx, cy;
            float front;
            sec_at(st.sec_s, &cx, &cy, &front);
            fk_glow_add_lut(fb, stride, &clip, cx, cy, 14, k->g14, SEC_GLOW, a8_of(0.5f + front * 0.5f));
            fk_disc(fb, stride, &clip, (float)cx, (float)cy, 1.6f + front, SEC_RGB, 255);
        }
    }
    ripple(fb, stride, &clip, st.ping_k, st.ping);
    /* the kept leaves (the band draws them while the swing runs) */
    if (!in->reveal_live) fk_cols_leaves(&DW_DOORS_IN_KEEP, k->cols, k->door.rgb, clip, fb, stride);
    over(k, in, st.text, clip, fb, stride);
    /* the stale marker, over the floor */
    if (st.stale && meets(clip, DW_D3_STALE)) dw_stale_mark(fb, stride, &clip, &k->dymo, STALE_X, STALE_Y);
}

/* doorwaySleep less its window outlines (WD32): the seam hairline and the time, on the glance orbit. Colours at most
 * 110 per channel (#6b5a3c). */
static void d3_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const d3_t *k = ctx;
    fill(fb, stride, clip, 0);
    const int sx = 232 + in->odx, sy0 = 196 + in->ody, sy1 = 416 + in->ody;
    if (sx >= clip.x0 && sx < clip.x1)
        for (int y = sy0 > clip.y0 ? sy0 : clip.y0; y < sy1 && y < clip.y1; y++)
            fb[(y - clip.y0) * stride + (sx - clip.x0)] = SEAM_RGB;
    char t[8];
    time_text(in, t);
    fk_atlas_text_rgb(fb, stride, clip, &k->g50, t, text_x(&k->g50, t) + in->odx, G50_BASE + in->ody, 0, GLANCE_RGB,
                      255, 0);
    if (in->clk.set && in->clk.stale)   /* the stale lamp, dim: inside the marker's rect on every orbit offset */
        fk_disc(fb, stride, &clip, STALE_X - 52.f + (float)in->odx, STALE_Y + (float)in->ody, 2.2f, STALE_DIM, 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t d3_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->wake_ms != b->wake_ms || a->reduced != b->reduced || a->reveal_live != b->reveal_live ||
        a->h24 != b->h24)
        return full(out);   /* the mode, the state, the orbit, the doors' tail, the leaves, reduced motion */
    if (b->clk.set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday ||
                       a->clk.mon != b->clk.mon || a->clk.wday != b->clk.wday))
        return full(out);   /* the minute: the marks step and the time turns (the date with it) */
    if (b->glance) return 0;   /* nothing moves in the glance within a minute */
    d3_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    uint8_t n = 0;
    if (sa.lance != sb.lance) out[n++] = DW_D3_LANCES;   /* the chime's breath */
    if (sa.column != sb.column) out[n++] = DW_D3_COLUMN;
    if (sa.ping != sb.ping || sa.ping_k != sb.ping_k) out[n++] = DW_D3_RIPPLE;
    if (sa.sec_s != sb.sec_s) {   /* the seconds light steps: its old place and its new one */
        dw_d3_sec_rect(sa.sec_s, &out[n++]);
        dw_d3_sec_rect(sb.sec_s, &out[n++]);
    }
    return n;
}

const fk_face_ops_t DW_FACE_DOORWAY = {
    .key = "doorway",
    .ctx_bytes = sizeof(d3_t),
    .cadence_ms = 40,
    .bind = d3_bind,
    .draw = d3_draw,
    .glance = d3_glance,
    .dirty = d3_dirty,
    .tap = NULL,
    .reveal = true,
    .reveal_ops = &DW_REVEAL_DOORS,   /* rd wave 1 Task 2: the doors as face_kit's reveal */
    .reveal_asset = NULL,   /* ASSET_DW_DOORS_M: the Modern doors, the build's art set */
    .reveal_arg = &DW_DOORS_IN_KEEP,
};
