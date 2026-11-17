/* Deck Sign (`deck`, rd/faces.h; rd wave 1 Task 12, spec section 10.6; faces-bridge.js:578-691: build(), spray(),
 * beacon(), frame() and sleepFrame()).
 *
 * The frame layer (rd_dk_frame, the mockup's build() canvas cut to its ink: A8 394 x 301 at (36, 96), each byte an index
 * into RD_DK_RAMP's four runs from black) is copied through the ramp's RGB565 table, then the hazard band's rows over it
 * (rd_dk_haz, RGB565 384 x 12 at (41, 121), the band's clip rect). Over them, in the mockup's order:
 *   unset, the dashes (unsetDashes(): two per column, 0.66 x the '0' advance apart, rd.paintDim);
 *   the hour (paint .92) and the minute (hullLit .94) in stencil 132, each string laid out on the font's own advances
 *   (the atlas's cells sit centred in the time's fixed 86 px advance: DK_SHIFT takes each back to its own pen) and
 *   centred on its column as paintSprite() and stencil() place the sprite: x0 = Math.round(cx - (ceil(w) + 12) / 2) + 6,
 *   the canvas 'middle' at 246.5 (the middle of the em box: Saira Stencil One's ascent 1135 and descent 439 put the
 *   alphabetic baseline (1135 - 439) / (1135 + 439) / 2 = .221 em below it: 276; Chakra Petch's 992 and 308, .263 em,
 *   3.4 px at 13). While a number sprays on, only the pixels whose RD_DK_SPRAY_LUT level is at most the spray's show;
 *   the beacon at (233, 88): its glow (rd.hazard, alpha (.10 + .28 face) on (1 - d / 46), the linear table r 46), its
 *   dome (an arc r 14 about (233, 92) over a 28 x 4 foot: #5A3A0A lit, #16110A dark; its coverage 8 x 8 supersampled at
 *   bind), the turning highlight (star0, .95 on (.35 + .65 face) at its middle, 16 px wide, about 233 + 14 sin(2 pi ph))
 *   and its cage's three 1 px steel bars, face = max(0, cos(2 pi ph));
 *   the notice (Chakra 13, tracked 1.8, centred on 234, baseline 384: the mockup's 381.5 middle is 385, a pixel lower,
 *   which would take LAUNCH's H 0.6 px past the 200 px text radius), or the stale marker in its place;
 *   the three triangles (FWD's, AFT's and NEXT TOILET's) are polygons, as the shots draw them: the design page's
 *   web-font subset has no U+25C0 or U+25B6, so its browser drew the fallback font's (DejaVu Sans: 1575 of 2048 wide,
 *   from 252 below the baseline to 1316 above, 13 px), larger than Chakra Petch's; rd_dk_frame leaves its own out
 *   (a8.erase);
 *   the chime's click: the rivet line's heads blend toward paint.
 * Numerals are dimmed by colour, at alpha 255 (the numerals' rule). Pure: no esp_*, freertos or LVGL; no libm (lesson 6:
 * the beacon's angle through fk_sincos, the glow from its const table); no allocation. */
#include <string.h>
#include "rd/faces.h"
#include "rd/release.h"
#include "rd/assets_meta.h"
#include "fk/compose.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"
#include "spray_lut.h"

#define W 466
#define FX 36      /* rd_dk_frame's crop */
#define FY 96
#define FW 394
#define FH 301
#define ZX 41      /* rd_dk_haz: the hazard band's clip rect (PX + 5, PY + 5, PW - 10, 12) */
#define ZY 121
#define ZW 384
#define ZH 12
#define HX 134     /* the columns' centres */
#define MX 332
#define Y_NUM 276      /* the numbers' baseline: 246.5 middle + 29.2 (below) */
#define Y_DASH 246     /* the unset dashes' centre (NY) */
#define BX 233     /* the beacon */
#define BY 88
#define GLOW_R 46
#define DOME_X0 218    /* the dome's box: the mockup's fillRect(x - 15, y - 12, 30, 22) */
#define DOME_Y0 76
#define DOME_W 30
#define DOME_H 22
#define NOTE_CX 234.f
#define Y_NOTE 384     /* 381.5 middle + 3.4 is 385: a pixel higher, so LAUNCH's H keeps inside the 200 px radius */
#define TRACK 1.8f     /* util(ctx, 13): (13 x 0.14).toFixed(1) px */
#define STALE_CX 235   /* staleMark(233, 381.5): the shared marker's STALE lands there from (235, 385) */
#define STALE_CY 385
#define Y_LABEL 161    /* the glance's DECK and SECTION: 158 middle + 3.4 */
#define Y_ARROW 321    /* FWD and AFT: 318 middle + 3.4 */
#define X_FWD 58.f     /* the arrows' pens: '\u25C0 FWD' left at PX + 22; 'AFT \u25B6' right at PX + PW - 20, so its
                        * triangle's pen is 410 less its advance and the trailing 1.8 */
#define X_AFT (410.f - TRI_ADV - TRACK)
#define TRI_ADV (1575.f * 13.f / 2048.f)
#define RIV_Y 338      /* the rivet line: rivet(x, PY + PH - 14) for x = PX + 20 .. PX + PW - 20 every 22 */
#define RIV_X0 56
#define RIV_N 17
#define SPRAY_MS 700
#define WAKE_H_MS 50     /* the wake: the hour sprays from here, the minute from WAKE_M_MS */
#define WAKE_M_MS 200
#define WAKE_DONE_MS 760 /* from here the wake shows both whole: its last frame at the 40 ms cadence */
#define FRESH_FULL_MS 5000
#define FRESH_FADE_MS 5600
#define REST_TURN_MS 4000   /* one turn of the beacon at rest (the mockup's 2400 ms x 1 / .6) */
#define REST_ON 0.45f

const omni_rect_t RD_DK_BEACON = {BX - GLOW_R, BY - GLOW_R, BX + GLOW_R, BY + GLOW_R};
const omni_rect_t RD_DK_SPRAY_H = {HX - 87, 171, HX + 87, 321};
const omni_rect_t RD_DK_SPRAY_M = {MX - 87, 171, MX + 87, 321};
const omni_rect_t RD_DK_NOTICE = {95, 368, 372, 396};
const omni_rect_t RD_DK_STALE = {196, 372, 276, 392};
const omni_rect_t RD_DK_TAP = {80, 359, 386, 404};
const omni_rect_t RD_DK_RIVETS = {RIV_X0 - 2, RIV_Y - 2, RIV_X0 + 22 * (RIV_N - 1) + 4, RIV_Y + 4};

/* rd_dk_frame's ramp (tools/export/rd_layers.json's row names the same stops): black to steelHi (steel and steelDeep
 * lie on it), to paint (paintDim), to hull (the rust's hullDeep), to hullLit */
const rd_ramp_stop_t RD_DK_RAMP[RD_DK_RAMP_STOPS] = {
    {0, 0x00, 0x00, 0x00},   {63, 0x8A, 0x90, 0x96},  {64, 0x00, 0x00, 0x00},  {127, 0xDC, 0xD3, 0xBE},
    {128, 0x00, 0x00, 0x00}, {191, 0x8E, 0x2B, 0x1E}, {192, 0x00, 0x00, 0x00}, {255, 0xC4, 0x49, 0x2F},
};

/* Saira Stencil One's digit advances (hmtx, units of 1000: x 132 / 1000 px) and, for each, how far the atlas moved its
 * cell to centre it in the time's fixed advance (rd_pack.py: round((86 - adv) / 2), Python's round) */
static const int16_t DK_ADV_U[10] = {660, 402, 614, 610, 654, 628, 662, 576, 676, 662};
static const int8_t DK_SHIFT[10] = {-1, 16, 2, 3, 0, 2, -1, 5, -2, -1};

static const char *const NOTES[RD_DK_NOTES - 1] = {"LIFT OUT OF ORDER SINCE LAUNCH", "NO RUNNING \267 ALSO NO JOGGING",
                                                   "NEXT TOILET 4 KM"};
#define NOTE_ARROW 2   /* the note followed by the triangle */
#define UNSET_NOTE "TIME NOT SET \267 AWAITING SIGNAL"

typedef struct {
    fk_img_t frame, haz, s132_img, s132o_img, ch13_img;
    fk_atlas_t s132, s132o, ch13;
    const uint8_t *lin46;
    uint16_t ramp[256];
    uint8_t dome[DOME_H][DOME_W];   /* the dome's coverage (0..255) */
} dk_t;

/* ---- small helpers */
static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}

static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

static uint16_t dim565(uint16_t c, uint8_t k)
{
    return omni_pack565(omni_ex_r(c) * k / 255, omni_ex_g(c) * k / 255, omni_ex_b(c) * k / 255);
}

static uint32_t mulberry32(uint32_t *a)   /* the mockups' rng(), as integers */
{
    uint32_t t = (*a += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

const uint8_t *rd_dk_spray_lut(void) { return RD_DK_SPRAY_LUT; }

unsigned rd_dk_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_DK_BEACON;
    out[1] = RD_DK_RIVETS;
    return 2;
}

/* 253 easeOut3(t / 700), floored, in integers: 253 - ceil(253 (700 - t)^3 / 700^3) */
int rd_dk_spray_level(long t)
{
    if (t <= 0) return 0;
    if (t >= SPRAY_MS) return RD_DK_SPRAY_DONE;
    const uint64_t u = (uint64_t)(SPRAY_MS - t), d = (uint64_t)SPRAY_MS * SPRAY_MS * SPRAY_MS;
    return 253 - (int)((253u * u * u * u + d - 1) / d);
}

bool rd_dk_notice(const fk_face_in_t *in, char out[48])
{
    out[0] = 0;
    if (!in->clk.set) {
        strcpy(out, UNSET_NOTE);
        return false;
    }
    if (in->clk.stale) return false;
    const unsigned v = in->variant % RD_DK_NOTES;
    if (v < RD_DK_NOTES - 1) {
        strcpy(out, NOTES[v]);
        return v == NOTE_ARROW;
    }
    char d[16];   /* the date . ALL DECKS (12 h: the date's AM/PM suffix) */
    fk_date_str(&in->clk, d);
    strcpy(out, d);
    strcat(out, fk_ampm(&in->clk, in->h24));
    strcat(out, " \267 ALL DECKS");
    return false;
}

/* ---- bind */
static bool bind(void *ctx, fk_asset_fn get)
{
    dk_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_DK_FRAME, &n);
    bool ok = b && fk_img_from_bin(&k->frame, b, n) && k->frame.a && !k->frame.rgb && k->frame.w == FW &&
              k->frame.h == FH;
    b = get(ASSET_RD_DK_HAZ, &n);
    ok = ok && b && fk_img_from_bin(&k->haz, b, n) && k->haz.rgb && !k->haz.a && k->haz.w == ZW && k->haz.h == ZH;
    b = get(ASSET_RD_F_S132, &n);
    ok = ok && b && fk_img_from_bin(&k->s132_img, b, n) && k->s132_img.a && !k->s132_img.rgb;
    b = get(ASSET_RD_F_S132O, &n);
    ok = ok && b && fk_img_from_bin(&k->s132o_img, b, n) && k->s132o_img.a;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    k->s132 = (fk_atlas_t){&k->s132_img, RD_CELLS_RD_F_S132, RD_GLYPHS_RD_F_S132, ASSET_RD_F_S132_LINE_H};
    k->s132o = (fk_atlas_t){&k->s132o_img, RD_CELLS_RD_F_S132O, RD_GLYPHS_RD_F_S132O, ASSET_RD_F_S132O_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->lin46 = rd_glow_lut(GLOW_R);
    ok = ok && k->lin46;
    rd_ramp565(RD_DK_RAMP, RD_DK_RAMP_STOPS, k->ramp);
    /* the dome: arc(233, 92, 14, pi, 2 pi), then down to (247, 96), across to (219, 96), closed; each pixel's share of
     * 8 x 8 sample points, in sixteenths of a pixel */
    for (int j = 0; j < DOME_H; j++)
        for (int i = 0; i < DOME_W; i++) {
            int hit = 0;
            for (int sy = 0; sy < 8; sy++)
                for (int sx = 0; sx < 8; sx++) {
                    const int X = 16 * (DOME_X0 + i) + 2 * sx + 1, Y = 16 * (DOME_Y0 + j) + 2 * sy + 1;
                    const int dx = X - 16 * BX, dy = Y - 16 * (BY + 4);
                    if (Y <= 16 * (BY + 4)) hit += dx * dx + dy * dy <= (16 * 14) * (16 * 14);
                    else hit += Y <= 16 * (BY + 8) && X >= 16 * (BX - 14) && X <= 16 * (BX + 14);
                }
            k->dome[j][i] = (uint8_t)((hit * 255 + 32) / 64);
        }
    return ok;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced;
    char hs[3], ms[3];      /* the hour (12 h: no leading zero) and the minute */
    int lh, lm;             /* their spray levels (RD_DK_SPRAY_DONE: whole) */
    uint32_t seed_m;        /* the minute's seed (8 + the day's minute mod 50; the hour's is 7) */
    float on, ph;           /* the beacon's strength (0: dark) and phase */
    float click;            /* the rivets' blink */
    char note[48];
    bool arrow;
} dk_state_t;

static void hm_of(const fk_face_in_t *in, char hs[3], char ms[3])
{
    char t[6];
    const int n = fk_time_str(&in->clk, in->h24, t);   /* "10:09", "9:05" */
    const int c = n - 3;
    memcpy(hs, t, (size_t)c);
    hs[c] = 0;
    ms[0] = t[c + 1], ms[1] = t[c + 2], ms[2] = 0;
}

static void state_of(const fk_face_in_t *in, dk_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    s->lh = s->lm = RD_DK_SPRAY_DONE;
    if (s->set) hm_of(in, s->hs, s->ms);
    const bool live = s->set && !in->reduced && !in->glance;
    if (live) {   /* the minute's spray from the clock; the hour's too when the hour changed */
        const long e = (long)in->clk.s * 1000 + in->clk.ms;
        s->lm = rd_dk_spray_level(e);
        if (in->clk.m == 0) s->lh = s->lm;
        if (in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_DONE_MS) {
            const int wh = rd_dk_spray_level((long)in->wake_ms - WAKE_H_MS);
            const int wm = rd_dk_spray_level((long)in->wake_ms - WAKE_M_MS);
            s->lh = wh < s->lh ? wh : s->lh;
            s->lm = wm < s->lm ? wm : s->lm;
        }
        s->seed_m = 8u + (uint32_t)((in->clk.h * 60 + in->clk.m) % 50);
        if (s->lm == RD_DK_SPRAY_DONE) s->seed_m = 0;
    }
    /* the beacon */
    if (!s->set) {
        s->on = 0.f;
    } else if (in->reduced) {   /* the mockup's reduced pose */
        s->on = 0.5f, s->ph = 0.25f;
    } else {
        s->ph = (float)((in->clk.s * 1000u + in->clk.ms) % REST_TURN_MS) / (float)REST_TURN_MS;
        s->on = REST_ON;
        if (in->fresh && !in->glance && in->fresh_ms < FRESH_FADE_MS) {
            if (in->fresh_ms < FRESH_FULL_MS) {   /* full, and one more turn over the 5 s */
                s->on = 1.f;
                s->ph += (float)in->fresh_ms / (float)FRESH_FULL_MS;
                if (s->ph >= 1.f) s->ph -= 1.f;
            } else {
                s->on = REST_ON + (1.f - REST_ON) * (float)(FRESH_FADE_MS - in->fresh_ms) /
                                      (float)(FRESH_FADE_MS - FRESH_FULL_MS);
            }
        }
        if (live && in->thrum > 0) {   /* the chime: full strength while the thrum runs */
            const float th = in->thrum * 3.f > 1.f ? 1.f : in->thrum * 3.f, c = REST_ON + (1.f - REST_ON) * th;
            s->on = c > s->on ? c : s->on;
        }
        if (live) s->click = in->click > 1.f ? 1.f : in->click < 0.f ? 0.f : in->click;
    }
    s->arrow = rd_dk_notice(in, s->note);
}

/* ---- drawing */
/* the string's glyph pens (paintSprite()'s sprite centred on cx: its text from Math.round(cx - (ceil(w) + 12) / 2) + 6,
 * then each digit at its own advance), as the x to hand fk_atlas_text_rgb (the pen less the atlas's centring shift) */
static int pens(const char *t, int cx, int out[2])
{
    float w = 0;
    int n = 0;
    for (const char *p = t; *p && n < 2; p++, n++) w += (float)DK_ADV_U[*p - '0'] * 0.132f;
    const int cw = (int)w + ((float)(int)w < w ? 1 : 0);
    const float x0 = (float)cx - (float)(cw + 12) / 2.f;
    const int left = (x0 >= 0 ? (int)(x0 + 0.5f) : -(int)(-x0 + 0.5f)) + 6;   /* Math.round: halves up (x0 > 0 here) */
    float pen = (float)left;
    for (int i = 0; i < n; i++) {
        const int d = t[i] - '0';
        out[i] = (int)(pen + 0.5f) - DK_SHIFT[d];
        pen += (float)DK_ADV_U[d] * 0.132f;
    }
    return n;
}

/* one number: whole (level RD_DK_SPRAY_DONE) through fk_atlas_text_rgb, or only the pixels whose mask level (the tile
 * offset by the seed, from the column's corner) is at most lv, each at the atlas's alpha as fk_atlas_text_rgb blends
 * it */
static void number(const fk_atlas_t *at, uint16_t *fb, int stride, omni_rect_t clip, const char *t, int cx,
                   uint16_t rgb, int lv, uint32_t seed, const omni_rect_t *col)
{
    if (!*t || lv <= 0) return;
    int px[2];
    const int n = pens(t, cx, px);
    if (lv >= RD_DK_SPRAY_DONE) {
        for (int i = 0; i < n; i++) {
            const char g[2] = {t[i], 0};
            fk_atlas_text_rgb(fb, stride, clip, at, g, px[i], Y_NUM, 0, rgb, 255, 0);
        }
        return;
    }
    uint32_t sd = seed;
    const int ox = (int)(mulberry32(&sd) >> 25), oy = (int)(mulberry32(&sd) >> 25);
    for (int i = 0; i < n; i++) {
        const fk_cell_t *c = &at->cells[t[i] - '0'];   /* the atlas's glyphs begin "0123456789" */
        const int gx = px[i] + c->ox, gy = Y_NUM + c->oy;
        const int x0 = gx > clip.x0 ? gx : clip.x0, x1 = gx + c->w < clip.x1 ? gx + c->w : clip.x1;
        const int y0 = gy > clip.y0 ? gy : clip.y0, y1 = gy + c->h < clip.y1 ? gy + c->h : clip.y1;
        for (int y = y0; y < y1; y++) {
            const uint8_t *src = at->img->a + (size_t)(c->y + y - gy) * at->img->w + (size_t)(c->x - gx);
            const size_t my = (size_t)((y - col->y0 + oy) & (RD_DK_SPRAY_TILE - 1));
            const uint8_t *m = RD_DK_SPRAY_LUT + my * RD_DK_SPRAY_TILE;
            uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
            for (int x = x0; x < x1; x++) {
                const uint8_t a = src[x];
                if (a && m[(x - col->x0 + ox) & (RD_DK_SPRAY_TILE - 1)] <= lv) d[x] = omni_blend565(d[x], rgb, a);
            }
        }
    }
}

/* a rect [x0, x1) x [y0, y1) in rgb (canvas fillRect at whole pixels) */
static void rect_fill(uint16_t *fb, int stride, omni_rect_t clip, float x0, float y0, float x1, float y1, uint16_t rgb)
{
    const omni_pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, &clip, p, 4, rgb, 255);
}

/* unsetDashes(cx, NY, 132, stencil, 2): the '0' advance x .66 apart, .72 of that wide, 13.2 px tall, Math.round'ed */
static void dashes(uint16_t *fb, int stride, omni_rect_t clip, int cx, int cy, uint16_t rgb)
{
    const float adv = (float)DK_ADV_U[0] * 0.132f * 0.66f, bw = adv * 0.72f, bh = 132.f * 0.1f;
    for (int i = 0; i < 2; i++) {
        const float x = (float)(int)((float)cx + ((float)i - 0.5f) * adv - bw / 2.f + 0.5f);
        const float y = (float)(int)((float)cy - bh / 2.f + 0.5f);
        rect_fill(fb, stride, clip, x, y, x + (float)(int)(bw + 0.5f), y + (float)(int)(bh + 0.5f), rgb);
    }
}

/* a triangle glyph at pen x on baseline y (DejaVu Sans's U+25B6, or U+25C0 mirrored, in 2048ths of 13 px) */
static void triangle(uint16_t *fb, int stride, omni_rect_t clip, float x, float y, bool right, uint16_t rgb)
{
    const float u = 13.f / 2048.f;
    const float xa = x + 6.f * u, xb = x + 1569.f * u, top = y - 1316.f * u, bot = y + 252.f * u, mid = y - 532.f * u;
    const omni_pt_t r[3] = {{xa, bot}, {xa, top}, {xb, mid}}, l[3] = {{xa, mid}, {xb, top}, {xb, bot}};
    omni_fill_poly(fb, stride, &clip, right ? r : l, 3, rgb, 255);
}

static void base_draw(const dk_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        memset(d, 0, (size_t)(clip.x1 - clip.x0) * sizeof d[0]);
        if (y >= FY && y < FY + FH) {
            const int a = clip.x0 > FX ? clip.x0 : FX, b = clip.x1 < FX + FW ? clip.x1 : FX + FW;
            const uint8_t *src = k->frame.a + (size_t)(y - FY) * FW;
            for (int x = a; x < b; x++) d[x - clip.x0] = k->ramp[src[x - FX]];
        }
        if (y >= ZY && y < ZY + ZH) {
            const int a = clip.x0 > ZX ? clip.x0 : ZX, b = clip.x1 < ZX + ZW ? clip.x1 : ZX + ZW;
            if (a < b) memcpy(d + (a - clip.x0), k->haz.rgb + (size_t)(y - ZY) * ZW + (a - ZX), (size_t)(b - a) * 2);
        }
    }
    triangle(fb, stride, clip, X_FWD, Y_ARROW, false, RD_C_PAINT_DIM);
    triangle(fb, stride, clip, X_AFT, Y_ARROW, true, RD_C_PAINT_DIM);
}

/* beacon(ctx, t, on, rm): the glow, the dome, the highlight, the cage */
static void beacon(const dk_t *k, const dk_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_DK_BEACON, clip)) return;
    float sn, cs;
    fk_sincos(s->ph * 6.28318530718f, &sn, &cs);
    const float face = cs > 0 ? cs : 0;
    const int y0 = clip.y0 > RD_DK_BEACON.y0 ? clip.y0 : RD_DK_BEACON.y0;
    const int y1 = clip.y1 < RD_DK_BEACON.y1 ? clip.y1 : RD_DK_BEACON.y1;
    const int x0 = clip.x0 > RD_DK_BEACON.x0 ? clip.x0 : RD_DK_BEACON.x0;
    const int x1 = clip.x1 < RD_DK_BEACON.x1 ? clip.x1 : RD_DK_BEACON.x1;
    if (s->on > 0) {   /* the glow: rd.hazard at (.10 + .28 face) on, falling linearly to 0 at r 46 */
        const int A = a8_of((0.10f + 0.28f * face) * s->on);
        for (int y = y0; y < y1; y++) {
            uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
            const int ey = 2 * (y - BY) + 1;
            for (int x = x0; x < x1; x++) {
                const int ex = 2 * (x - BX) + 1, N = ex * ex + ey * ey;
                if (N >= 4 * GLOW_R * GLOW_R) continue;
                const int a = k->lin46[(N - 2) / 8] * A / 255;
                if (a) d[x] = omni_blend565(d[x], RD_C_HAZARD, a);
            }
        }
    }
    /* the dome and, lit, its turning highlight */
    const uint16_t dome = s->on > 0 ? RD_RGB(0x5A, 0x3A, 0x0A) : RD_RGB(0x16, 0x11, 0x0A);
    const float bx = (float)BX + sn * 14.f, ah = 0.95f * s->on * (0.35f + 0.65f * face);
    for (int y = clip.y0 > DOME_Y0 ? clip.y0 : DOME_Y0; y < clip.y1 && y < DOME_Y0 + DOME_H; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = clip.x0 > DOME_X0 ? clip.x0 : DOME_X0; x < clip.x1 && x < DOME_X0 + DOME_W; x++) {
            const int cov = k->dome[y - DOME_Y0][x - DOME_X0];
            if (!cov) continue;
            d[x] = omni_blend565(d[x], dome, cov);
            if (ah > 0) {
                const float dx = (float)x + 0.5f - bx, t = 1.f - (dx < 0 ? -dx : dx) / 8.f;
                if (t > 0) {
                    const int a = (int)(ah * t * (float)cov + 0.5f);
                    if (a) d[x] = omni_blend565(d[x], RD_C_STAR0, a);
                }
            }
        }
    }
    for (int i = -1; i <= 1; i++) {   /* the cage: x + 7i from y - 9 + 3 |i| to y + 8 */
        const float x = (float)(BX + 7 * i), ya = (float)(BY - 9 + 3 * (i < 0 ? -i : i));
        fk_line(fb, stride, &clip, x, ya, x, (float)(BY + 8), 1.f, RD_C_STEEL, 255);
    }
}

/* the notice's text centred on NOTE_CX, tracked; the triangle after it (its advance and a track) */
static void notice(const dk_t *k, const dk_state_t *s, uint16_t *fb, int stride, omni_rect_t clip, uint16_t rgb)
{
    if (!s->arrow) {
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, s->note, NOTE_CX, Y_NOTE, TRACK, true, rgb, 255);
        return;
    }
    char t[52];
    strcpy(t, s->note);
    strcat(t, " ");
    const float wt = fk_atlas_width_tracked(&k->ch13, t, TRACK), left = NOTE_CX - (wt + TRACK + TRI_ADV) / 2.f;
    fk_atlas_text_tracked(fb, stride, clip, &k->ch13, t, left + wt / 2.f, Y_NOTE, TRACK, true, rgb, 255);
    triangle(fb, stride, clip, (float)(int)(left + wt + TRACK + 0.5f), (float)Y_NOTE, true, rgb);
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const dk_t *k = ctx;
    dk_state_t s;
    state_of(in, &s);
    base_draw(k, fb, stride, clip);
    if (!s.set) {
        dashes(fb, stride, clip, HX, Y_DASH, RD_C_PAINT_DIM);
        dashes(fb, stride, clip, MX, Y_DASH, RD_C_PAINT_DIM);
    } else {
        if (meets(RD_DK_SPRAY_H, clip))
            number(&k->s132, fb, stride, clip, s.hs, HX, dim565(RD_C_PAINT, 235), s.lh, 7u, &RD_DK_SPRAY_H);
        if (meets(RD_DK_SPRAY_M, clip))
            number(&k->s132, fb, stride, clip, s.ms, MX, dim565(RD_C_HULL_LIT, 240), s.lm, s.seed_m, &RD_DK_SPRAY_M);
    }
    beacon(k, &s, fb, stride, clip);
    if (meets(RD_DK_NOTICE, clip)) {
        if (s.stale) rd_stale_mark(fb, stride, &clip, &k->ch13, STALE_CX, STALE_CY, false);
        else notice(k, &s, fb, stride, clip, s.set ? RD_C_PAINT : RD_C_AMBER);
    }
    if (s.click > 0 && meets(RD_DK_RIVETS, clip)) {   /* the click: the rivet heads (3 x 3 at x - 1, y - 1) blink */
        const int a = a8_of(s.click);
        for (int i = 0; i < RIV_N; i++)
            for (int y = RIV_Y - 1; y <= RIV_Y + 1; y++)
                for (int x = RIV_X0 + 22 * i - 1; x <= RIV_X0 + 22 * i + 1; x++)
                    if (x >= clip.x0 && x < clip.x1 && y >= clip.y0 && y < clip.y1) {
                        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride + (x - clip.x0);
                        *d = omni_blend565(*d, RD_C_PAINT, a);
                    }
    }
}

static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const dk_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int ox = in->odx, oy = in->ody;
    const bool set = in->clk.set, stale = set && in->clk.stale;
    if (!set) {
        dashes(fb, stride, clip, HX + ox, Y_DASH + oy, RD_G_STEEL);
        dashes(fb, stride, clip, MX + ox, Y_DASH + oy, RD_G_STEEL);
    } else {   /* the outlines (strokeText 1.5 px: the 132 stencil's outline atlas, on the same pens) */
        char hs[3], ms[3];
        hm_of(in, hs, ms);
        const struct { const char *t; int cx; uint16_t rgb; } N[2] = {{hs, HX, RD_G_PAINT}, {ms, MX, RD_G_HULL}};
        for (int j = 0; j < 2; j++) {
            int px[2];
            const int n = pens(N[j].t, N[j].cx + ox, px);
            for (int i = 0; i < n; i++) {
                const char g[2] = {N[j].t[i], 0};
                fk_atlas_text_rgb(fb, stride, clip, &k->s132o, g, px[i], Y_NUM + oy, 0, N[j].rgb, 255, 0);
            }
        }
    }
    fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "DECK", (float)(HX + ox), Y_LABEL + oy, TRACK, true, RD_G_PAINT,
                          255);
    fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "SECTION", (float)(MX + ox), Y_LABEL + oy, TRACK, true, RD_G_HULL,
                          255);
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
    dk_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        const bool same = sa.set == sb.set && sa.stale == sb.stale && a->odx == b->odx && a->ody == b->ody &&
                          strcmp(sa.hs, sb.hs) == 0 && strcmp(sa.ms, sb.ms) == 0;
        return same ? 0 : whole(out);
    }
    if (sa.set != sb.set || a->h24 != b->h24 || sa.reduced != sb.reduced) return whole(out);
    rects_t L;
    memset(&L, 0, sizeof L);
    if (strcmp(sa.hs, sb.hs) != 0 || sa.lh != sb.lh) add(&L, RD_DK_SPRAY_H);
    if (strcmp(sa.ms, sb.ms) != 0 || sa.lm != sb.lm || sa.seed_m != sb.seed_m) add(&L, RD_DK_SPRAY_M);
    if (sa.on != sb.on || sa.ph != sb.ph) add(&L, RD_DK_BEACON);
    if (sa.stale != sb.stale || sa.arrow != sb.arrow || strcmp(sa.note, sb.note) != 0) add(&L, RD_DK_NOTICE);
    if (sa.click != sb.click) add(&L, RD_DK_RIVETS);
    for (unsigned i = 0; i < L.n; i++) out[i] = L.r[i];
    return (uint8_t)L.n;
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= RD_DK_TAP.x0 && x < RD_DK_TAP.x1 && y >= RD_DK_TAP.y0 && y < RD_DK_TAP.y1 ? FK_TAP_PULSE : FK_TAP_NONE;
}

/* the next notice (RAM only); FK_TAP_PULSE with no pulse_len_ms: fk_face_gesture draws the new variant, nothing more.
 * While no notice shows (stale: the marker in its place; unset: the amber TIME NOT SET line) the variant stays: the
 * tap would turn a page nobody sees (the Task 12 review). A NULL in (only from outside the runtime, fk/face.h) counts
 * as shown */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    const fk_tap_t t = tap(ctx, x, y);
    const bool shown = in == NULL || (in->clk.set && !in->clk.stale);
    if (t == FK_TAP_PULSE && shown) *variant = (uint16_t)((*variant + 1u) % RD_DK_NOTES);
    return t;
}

const fk_face_ops_t RD_FACE_DECK = {
    .key = "deck",
    .ctx_bytes = sizeof(dk_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = 800,
    .tap_var = tap_var,
    .stills_extra = 1,   /* still 11: the second notice (RD_DK_TAPPED_VARIANT) */
};
