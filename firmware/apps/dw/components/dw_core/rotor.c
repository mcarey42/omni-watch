/* Face 7, Rotor (dw/faces.h; dw wave 1 Task 14, spec sections 10 and 11), from faces-classic.js rotorGlass,
 * rotorFront and classic-rotor (frame, sleepFrame).
 *
 * Layers (tools/export/dw_layers.json): dw_t7_base (the glass column, the collar and its sloping console panels over
 * black, RGB565; the collar left out over the column), dw_t7_front (the collar and the top rim's front over the column,
 * RGB565A8, laid over the stack), dw_t7_stack (the perspex plates, rods and chrome spheres, 90 x 170, RGB565A8, at the
 * plates' brightest), dw_t7_dig (the 88 px digits, alpha only, tinted cream), and the shared atlases dw_b5_note
 * (AM / PM) and dw_dymo (the date tape, TIME NOT SET, the stale strip). Drawn here: the sixty rim pinpoints, the core's
 * light (a lamp-coloured band and a white bar, at the core's level), the stack's place and level, the glance.
 *
 * Motion at rest stays in regions (spec section 11): the stack rises on the even second and falls on the odd one
 * (dw_rotor_lift, 30 px, its core a little brighter at the top), moving in whole pixels inside the column
 * (DW_T7_COLUMN, 98 x 208: the mockup's clip), and each second the old and the new rim pinpoint redraw. cadence_ms 50.
 * The core breathes with the chime's envelope (spec section 13), not under reduced motion. In the glance nothing
 * moves within a minute.
 * The wake is a take-off (wake_ms < UINT32_MAX, the face's own: no doors): the stack rises out of the collar as the
 * core brightens (easeOut3 from 150 ms over 1100 ms); only the column changes, and by DW_T7_WAKE_MS it is the settled
 * frame exactly. Reduced motion (faces-classic.js rm): the stack stands still at mid-stroke.
 * Unset: the stack at rest at the bottom (no take-off either), the core dark (0.08), the digits -- and --, the tape
 * TIME NOT SET; no pinpoints. Stale: the amber NOT SYNCED strip replaces the date tape.
 * The stack's plates are one sprite blended at the plates' level (0.26 + 0.40 coreL of 0.66): the mockup strokes each
 * plate at that level and keeps the spheres and rods at theirs (close, not the same, below full brightness).
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
#define WHITE RGB(0xff, 0xff, 0xff)
#define TAPE_INK RGB(220, 218, 213)   /* tape()'s rgba(238, 236, 230, .92) over its #111 strip */
/* faces-classic.js ROT: the glass column */
#define X0 183
#define X1 283
#define TOP 92
#define BOT 300
#define RX 50.f
#define RY 11.f
#define DIG_BASE 236                  /* the digits' baseline; 50 px cells, hours ending at X0 - 14, minutes from X1 + 14 */
#define TAPE_Y 382.f

const omni_rect_t DW_T7_COLUMN = {ASSET_DW_T7_FRONT_X, ASSET_DW_T7_FRONT_Y, ASSET_DW_T7_FRONT_X + ASSET_DW_T7_FRONT_W,
                                  ASSET_DW_T7_FRONT_Y + ASSET_DW_T7_FRONT_H};
const omni_rect_t DW_T7_STALE = {164, 367, 300, 397};

typedef struct {
    bool base_ok, front_ok, stack_ok, dig_ok, note_ok, dymo_ok;
    fk_img_t base, front, stack, dig_img, note_img, dymo_img;
    fk_atlas_t dig, note, dymo;
} t7_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ease_out3(float x) { const float u = 1 - clampf(x, 0, 1); return 1 - u * u * u; }

float dw_rotor_lift(uint32_t ms_in_2s)
{
    float s, c;
    fk_sincos((float)(ms_in_2s % 2000u) / 2000.f * 2 * PI_F, &s, &c);
    return (1 - c) / 2;
}

/* the rim pinpoint of second i: its centre (r 219, i / 60 of a turn from 12) */
static void pin_xy(int i, float *x, float *y)
{
    float s, c;
    fk_sincos((float)i * (2 * PI_F / 60.f), &s, &c);
    *x = C + 219.f * s, *y = C - 219.f * c;
}

void dw_t7_pin_rect(int s, omni_rect_t *out)
{
    float x, y;
    pin_xy(s % 60, &x, &y);
    *out = (omni_rect_t){(int16_t)floorf(x - 3.6f), (int16_t)floorf(y - 3.6f), (int16_t)(floorf(x + 3.6f) + 1),
                         (int16_t)(floorf(y + 3.6f) + 1)};
}

/* ---- the stack's place and the core's level for an input */
typedef struct {
    int base_y;        /* the stack's base line (baseY), to the pixel */
    uint8_t core;      /* coreL, 0..255 */
} t7_state_t;

static void state_of(const fk_face_in_t *in, t7_state_t *st)
{
    const bool set = in->clk.set;
    /* the take-off only on a set clock: unset, the stack stays at rest */
    const float take = !set || in->reduced || in->wake_ms == UINT32_MAX ? 1.f
                                                                        : ease_out3(((float)in->wake_ms - 150.f) / 1100.f);
    const float bob = !set ? 0.f : in->reduced ? 0.5f : dw_rotor_lift((uint32_t)(in->clk.s % 2) * 1000u + in->clk.ms);
    const float by = (float)(BOT - 6) + (1 - take) * 150.f - bob * 30.f * take;
    float core = !set ? 0.08f : (0.35f + 0.65f * take) * (0.82f + 0.18f * bob);
    if (set && !in->reduced && in->chime > 0) core += (1 - core) * clampf(in->chime, 0, 1);   /* the chime's breath */
    st->base_y = (int)floorf(by + 0.5f);
    st->core = (uint8_t)(clampf(core, 0, 1) * 255.f + 0.5f);
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool t7_bind(void *ctx, fk_asset_fn get)
{
    t7_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->base_ok = img(get, ASSET_DW_T7_BASE, &k->base, FK_CF_RGB565, W, W);
    k->front_ok = img(get, ASSET_DW_T7_FRONT, &k->front, FK_CF_RGB565A8, ASSET_DW_T7_FRONT_W, ASSET_DW_T7_FRONT_H);
    k->stack_ok = img(get, ASSET_DW_T7_STACK, &k->stack, FK_CF_RGB565A8, DW_T7_STACK_W, DW_T7_STACK_H);
    k->dig_ok = img(get, ASSET_DW_T7_DIG, &k->dig_img, FK_CF_A8, ASSET_DW_T7_DIG_W, ASSET_DW_T7_DIG_H);
    k->note_ok = img(get, ASSET_DW_B5_NOTE, &k->note_img, FK_CF_A8, ASSET_DW_B5_NOTE_W, ASSET_DW_B5_NOTE_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->dig = (fk_atlas_t){&k->dig_img, DW_CELLS_DW_T7_DIG, DW_GLYPHS_DW_T7_DIG, ASSET_DW_T7_DIG_LINE_H};
    k->note = (fk_atlas_t){&k->note_img, DW_CELLS_DW_B5_NOTE, DW_GLYPHS_DW_B5_NOTE, ASSET_DW_B5_NOTE_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    return k->base_ok && k->front_ok && k->stack_ok && k->dig_ok && k->note_ok && k->dymo_ok;
}

/* ---- drawing */
static void fill(uint16_t *fb, int stride, omni_rect_t clip, uint16_t v)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = v;
    }
}

/* clip and r's overlap (false if none), and fb moved to its top-left */
static bool sub(omni_rect_t clip, omni_rect_t r, uint16_t *fb, int stride, omni_rect_t *out, uint16_t **ofb)
{
    out->x0 = clip.x0 > r.x0 ? clip.x0 : r.x0, out->y0 = clip.y0 > r.y0 ? clip.y0 : r.y0;
    out->x1 = clip.x1 < r.x1 ? clip.x1 : r.x1, out->y1 = clip.y1 < r.y1 ? clip.y1 : r.y1;
    if (out->x0 >= out->x1 || out->y0 >= out->y1) return false;
    *ofb = fb + (out->y0 - clip.y0) * stride + (out->x0 - clip.x0);
    return true;
}

static void rect(uint16_t *fb, int stride, const omni_rect_t *clip, float x0, float y0, float x1, float y1,
                 uint16_t rgb, uint8_t a8)
{
    const omni_pt_t q[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, clip, q, 4, rgb, a8);
}

/* the core's light (frame 724-728): a lamp band 52 px wide, 0 at its edges and .55 coreL in the middle, and the white
 * bar 6 px wide at .85 coreL, rising from the stack's base line; whole-pixel rects (cx 233), so no edge coverage */
static void core(uint16_t *fb, int stride, omni_rect_t c, const t7_state_t *st)
{
    const int by = st->base_y;
    for (int y = c.y0; y < c.y1; y++) {
        uint16_t *row = fb + (y - c.y0) * stride;
        const bool band = y >= by - 168 && y < by, bar = y >= by - 160 && y < by;
        if (!band) continue;
        for (int x = c.x0 < 207 ? 207 : c.x0; x < c.x1 && x < 259; x++) {
            const float u = ((float)x + 0.5f - 207.f) / 52.f, tri = 1.f - fabsf(2.f * u - 1.f);
            uint16_t *d = row + (x - c.x0);
            *d = omni_blend565(*d, LAMP, (int)(0.55f * tri * (float)st->core + 0.5f));
            if (bar && x >= 230 && x < 236) *d = omni_blend565(*d, WHITE, (int)(0.85f * (float)st->core + 0.5f));
        }
    }
}

static void column(const t7_t *k, const t7_state_t *st, omni_rect_t clip, uint16_t *fb, int stride)
{
    omni_rect_t c;
    uint16_t *cf;
    if (!sub(clip, DW_T7_COLUMN, fb, stride, &c, &cf)) return;
    core(cf, stride, c, st);
    const int plates = (int)((0.26f + 0.40f * (float)st->core / 255.f) / 0.66f * 255.f + 0.5f);
    if (k->stack_ok)
        fk_blit(cf, stride, c, &k->stack, 233 - DW_T7_STACK_W / 2, st->base_y - DW_T7_STACK_BASE,
                (uint8_t)(plates > 255 ? 255 : plates));
    if (k->front_ok) fk_blit(cf, stride, c, &k->front, ASSET_DW_T7_FRONT_X, ASSET_DW_T7_FRONT_Y, 255);
}

/* v as decimal digits, at least min (zero-padded) */
static void put_u(char *p, unsigned v, int min)
{
    char t[6];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while ((v || n < min) && n < 5);
    while (n) *p++ = t[--n];
    *p = 0;
}

/* the hours (12 h without a leading zero, 24 h with) right-aligned at X0 - 14 and the minutes left-aligned at X1 + 14,
 * 50 px cells (faces-classic.js cells(), 'right' and 'left'); -- and -- unset */
static void digits(const t7_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride, int dx, int dy,
                   uint8_t opa)
{
    char h[4] = "--", m[4] = "--";
    if (in->clk.set) {
        put_u(h, in->h24 ? in->clk.h % 24u : (in->clk.h % 24u + 11) % 12 + 1, in->h24 ? 2 : 1);
        put_u(m, in->clk.m % 60u, 2);
    }
    const int hw = 50 * (int)strlen(h);
    fk_atlas_text_rgb(fb, stride, clip, &k->dig, h, X0 - 14 - hw + dx, DIG_BASE + dy, 50, CREAM, opa, 0);
    fk_atlas_text_rgb(fb, stride, clip, &k->dig, m, X1 + 14 + dx, DIG_BASE + dy, 50, CREAM, opa, 0);
}

static const char MON[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char DOW[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static void t7_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const t7_t *k = ctx;
    t7_state_t st;
    state_of(in, &st);
    if (k->base_ok) fk_blit(fb, stride, clip, &k->base, 0, 0, 255);
    else fill(fb, stride, clip, 0);
    if (in->clk.set) {   /* the sixty rim pinpoints, the current second lit */
        for (int i = 0; i < 60; i++) {
            float x, y;
            pin_xy(i, &x, &y);
            if (x + 4 < clip.x0 || x - 4 >= clip.x1 || y + 4 < clip.y0 || y - 4 >= clip.y1) continue;
            const bool on = i == in->clk.s % 60, five = i % 5 == 0;
            fk_disc(fb, stride, &clip, x, y, on ? 2.6f : five ? 1.6f : 1.1f, on ? LAMP : CREAM, on ? 255 : five ? 82 : 36);
        }
    }
    column(k, &st, clip, fb, stride);
    if (k->dig_ok) digits(k, in, clip, fb, stride, 0, 0, in->clk.set ? 255 : 115);
    if (in->clk.set && !in->h24 && k->note_ok) {   /* AM / PM, left-aligned at X1 + 18 */
        const char *ap = in->clk.h < 12 ? "AM" : "PM";
        const float w = fk_atlas_width_tracked(&k->note, ap, 2.5f);
        fk_atlas_text_tracked(fb, stride, clip, &k->note, ap, (float)(X1 + 18) + w / 2, 158, 2.5f, true, CREAM, 153);
    }
    if (!k->dymo_ok) return;
    if (!in->clk.set) {
        dw_tape(fb, stride, &clip, &k->dymo, "TIME NOT SET", C, TAPE_Y, true, TAPE_INK);
    } else if (in->clk.stale) {
        if (DW_T7_STALE.x0 < clip.x1 && clip.x0 < DW_T7_STALE.x1 && DW_T7_STALE.y0 < clip.y1 && clip.y0 < DW_T7_STALE.y1)
            dw_stale_mark(fb, stride, &clip, &k->dymo, C, TAPE_Y);
    } else {   /* "THU 22 OCT" */
        char t[12];
        memcpy(t, in->clk.wday < 7 ? DOW[in->clk.wday] : "---", 3);
        t[3] = ' ';
        put_u(t + 4, in->clk.mday > 99 ? 99u : in->clk.mday, 1);
        const size_t n = strlen(t);
        t[n] = ' ';
        memcpy(t + n + 1, in->clk.mon < 12 ? MON[in->clk.mon] : "---", 4);
        dw_tape(fb, stride, &clip, &k->dymo, t, C, TAPE_Y, true, TAPE_INK);
    }
}

/* the glances' ink ('lighten'): each channel the larger of the pixel's and rgb's at coverage x a8 over black */
static uint16_t lighten(uint16_t d, uint16_t rgb, int a)
{
    const int r = omni_ex_r(rgb) * a / 255, g = omni_ex_g(rgb) * a / 255, b = omni_ex_b(rgb) * a / 255;
    const int dr = omni_ex_r(d), dg = omni_ex_g(d), db = omni_ex_b(d);
    return omni_pack565(r > dr ? r : dr, g > dg ? g : dg, b > db ? b : db);
}

/* an ellipse stroked width w (the edge distance by its first-order estimate), lighten ink; lower: its lower half alone
 * (canvas ellipse 0 .. pi) */
static void ellipse_max(uint16_t *fb, int stride, const omni_rect_t *clip, float cx, float cy, float rx, float ry,
                        float w, bool lower, uint16_t rgb, uint8_t a8)
{
    const int x0 = (int)floorf(cx - rx - w), x1 = (int)ceilf(cx + rx + w), y1 = (int)ceilf(cy + ry + w);
    const int y0 = lower ? (int)floorf(cy) : (int)floorf(cy - ry - w);
    for (int y = y0 > clip->y0 ? y0 : clip->y0; y < y1 && y < clip->y1; y++)
        for (int x = x0 > clip->x0 ? x0 : clip->x0; x < x1 && x < clip->x1; x++) {
            const float X = (float)x + 0.5f - cx, Y = (float)y + 0.5f - cy;
            if (lower && Y < 0) continue;
            const float g = X * X / (rx * rx) + Y * Y / (ry * ry) - 1.f;
            const float gx = X / (rx * rx), gy = Y / (ry * ry), gl = 2.f * sqrtf(gx * gx + gy * gy);
            if (gl <= 0) continue;
            const float cov = clampf(w / 2 + 0.5f - fabsf(g / gl), 0, 1);
            if (cov <= 0) continue;
            uint16_t *d = fb + (y - clip->y0) * stride + (x - clip->x0);
            *d = lighten(*d, rgb, (int)(cov * (float)a8 + 0.5f));
        }
}

/* sleepFrame(): the glass's outline, the parked core as a dim bar and the time at 30 %, on the glance orbit; no
 * pinpoints, no tape. Colours at most 110 per channel (cream .28 and .30, the lamp .22). */
static void t7_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const t7_t *k = ctx;
    fill(fb, stride, clip, 0);
    const float dx = (float)in->odx, dy = (float)in->ody;
    ellipse_max(fb, stride, &clip, C + dx, (float)TOP + dy, RX, RY, 1.f, false, CREAM, 71);
    fk_line_max(fb, stride, &clip, (float)X0 + dx, (float)TOP + dy, (float)X0 + dx, (float)BOT + dy, 1.f, CREAM, 71);
    fk_line_max(fb, stride, &clip, (float)X1 + dx, (float)TOP + dy, (float)X1 + dx, (float)BOT + dy, 1.f, CREAM, 71);
    ellipse_max(fb, stride, &clip, C + dx, (float)BOT + dy, RX, RY, 1.f, true, CREAM, 71);
    rect(fb, stride, &clip, C - 1.5f + dx, (float)(BOT - 120) + dy, C + 1.5f + dx, (float)(BOT - 6) + dy, LAMP, 56);
    if (k->dig_ok) digits(k, in, clip, fb, stride, in->odx, in->ody, 77);
    if (in->clk.set && in->clk.stale)   /* the stale lamp, dim: inside the marker's rect on every orbit offset */
        fk_disc(fb, stride, &clip, 181.f + dx, TAPE_Y + dy, 2.2f, RGB(96, 64, 22), 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t t7_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced || a->h24 != b->h24)
        return full(out);   /* the mode, the state, the orbit, reduced motion, the hours */
    if (b->clk.set && (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday ||
                       a->clk.mon != b->clk.mon || a->clk.wday != b->clk.wday))
        return full(out);   /* the minute: the digits, AM / PM, the date */
    if (b->glance) return 0;   /* nothing moves in the glance */
    uint8_t n = 0;
    t7_state_t sa, sb;   /* the stack's place and the core: the stroke, the take-off, the chime */
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.base_y != sb.base_y || sa.core != sb.core) out[n++] = DW_T7_COLUMN;
    if (b->clk.set && a->clk.s != b->clk.s) {
        dw_t7_pin_rect(a->clk.s, &out[n++]);
        dw_t7_pin_rect(b->clk.s, &out[n++]);
    }
    return n;
}

const fk_face_ops_t DW_FACE_ROTOR = {
    .key = "rotor",
    .ctx_bytes = sizeof(t7_t),
    .cadence_ms = 50,
    .bind = t7_bind,
    .draw = t7_draw,
    .glance = t7_glance,
    .dirty = t7_dirty,
    .tap = NULL,
    .reveal = false,
    .wake_len_ms = DW_T7_WAKE_MS,
};
