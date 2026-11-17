/* Bunk Clock (`bunk`, rd/faces.h; rd wave 1 Task 14, spec section 10.12; faces-quarters.js:678-821: buildBg(),
 * cardFace(), card(), holoLamp(), frame(), pointer() and sleepFrame()).
 *
 * The base: #060606, the shelf's rows below y 336 (rd_bk_shelf, one column of buildBg()'s canvas, drawn across), then
 * the case layer (rd_bk_case, buildBg()'s canvas cut to its ink: RGB565 384 x 221 at (42, 116): the bar, the case, the
 * tape and the wells). Over it, in the mockup's order:
 *   the bar's press (black .45 over its top 4 rows for 200 ms after a snooze) and SNOOZE (Chakra 13 hazardDark,
 *   tracked 1.8, centred, 3 px lower while pressed; unset at .5);
 *   the lamp (holoLamp()): the steelDeep ring r 16, the lens r 13 (its coverage and the radial gradient's position per
 *   pixel worked out at bind, in integers), filled with the gradient phosphor .95 L to phosphorDim .6 L (or #0c120e
 *   while paused), the scanlines (black .45 every third row from y 67 + roll, shifted jx by a tear), then its glow
 *   (glowSprite() r 26 at .25 L, added, from the const table RD_QTR_LUT_26);
 *   the label ROOMMATE . PROJECTED (paintDim) or ROOMMATE . PAUSED (BLISS) (paint), Chakra 13 tracked 1.8 from x 184,
 *   or the stale marker in its place (staleMark(222, 80): the quarters marker centred there);
 *   the cards (card()): each the blank and its digits, or, mid-flip, two composed leaves (below) shown as the new top
 *   and the old bottom behind, with the old top folding down to the hinge (p < .5: cos(p pi) of its height, black
 *   .55 (1 - s) over it) or the new bottom falling from it (-cos(p pi), black .45 (1 - s)), each a half-card scaling
 *   through fk_rows_blit (rows of the flat squash: the mockup's drawImage, sampled nearest); then the split (2 px
 *   black) and the hinge pins (5 x 8 steel);
 *   the date line (Chakra 13 paintDim, tracked 1.8, centred) and the count line (VT323 28 amber phosphor: the glow
 *   plane then the core, centred), the old value under the new at 30 % while it lingers.
 * The mockup's y are canvas 'middle' baselines: Chakra Petch's (992, 308) put the alphabetic baseline .263 em below
 * (3.4 px at 13), VT323's (800, 200) .3 em (8.4 px at 28).
 * The composed leaves (PR24): fk_rows_blit takes an RGB565 leaf, and a card is the blank with its digits drawn, so a
 * flipping card's two faces (the outgoing and the incoming) are composed into 174 x 150 RGB565 leaves in the face's
 * context, kept by their text and colour (a leaf is a pure function of them, so a frame does not depend on which draw
 * composed it). Four slots (the Task 14 review): at the hour's flip and in the wake both cards flip at once, four leaves
 * a frame (or three: the wake's -- is both cards' outgoing face), so each frame's draws find every leaf composed already
 * and compose none; a miss takes the slot used longest ago, never the other leaf of the card being drawn. draw() writes them: the one write a draw makes outside its frame, so this face must stay a canvas face
 * (no .direct, no reveal: fk_view draws it from the LVGL task alone; test_rd_bk_face asserts both).
 * The digits are dimmed by colour, at alpha 255 (the numerals' rule); so are the count's linger and its unset line.
 * Pure: no esp_*, freertos or LVGL; no libm (lesson 6: the flap's cosine through fk_sincos, the lens's geometry in
 * integers at bind, the glow from its const table); no allocation; no settings, store or NVS symbol (the count is the
 * clock's, the taps and the pause the RAM variant's: test_rd_bk_no_nvs). */
#include <stdint.h>
#include <string.h>
#include "rd/faces.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "fk/compose.h"
#include "fk/rows.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
/* the R86 rounding rd_pack.py packs with (tools/facepack.py rgb565()), for colours the layers also hold */
#define R86(r, g, b) \
    (uint16_t)((((r) * 31 + 127) / 255) << 11 | (((g) * 63 + 127) / 255) << 5 | (((b) * 31 + 127) / 255))
#define BG565 R86(6, 6, 6)       /* buildBg()'s #060606 */
#define KX 42      /* rd_bk_case's crop */
#define KY 116
#define KW 384
#define KH 221
#define SHELF_Y 336
#define SHELF_H 130
#define CARD_W 174
#define CARD_H 150
#define CARD_Y 173
#define HALF 75
#define CARD_HX 53     /* CX = [cx - 6 - CARD.w, cx + 6] */
#define CARD_MX 239
#define CARD_R 7
#define DIG_CX 87      /* drawTime(g, str, 132, col, CARD.w / 2, CARD.h / 2 + 47): the card's own centre and baseline */
#define DIG_BASE 122
#define SPLIT_Y 247    /* fillRect(x, y + h2 - 1, CARD.w, 2) */
#define PIN_Y 244      /* fillRect(x - 3, y + h2 - 4, 5, 8) and (x + CARD.w - 2, ...) */
#define LX 160         /* LAMP = {x: 160, y: 80, r: 13} */
#define LY 80
#define LR 13
#define RING_R 16
#define GLOW_R 26
#define LENS_X0 (LX - RING_R)
#define LENS_Y0 (LY - RING_R)
#define LENS_N (2 * RING_R)
#define BAR_X 128      /* BAR = {x: 128, y: 116, w: 210, h: 30} */
#define BAR_Y 116
#define BAR_W 210
#define TRACK 1.8f     /* lab(): (13 x .14).toFixed(1) px */
#define LABEL_X 184    /* lab(..., LAMP.x + 24, LAMP.y + 1, 13, ..., 'left'): 81 middle + 3.4 */
#define Y_LABEL 84
#define Y_SNOOZE 134   /* BAR.y + 15 = 131 middle + 3.4 */
#define Y_DATE 355     /* 352 middle + 3.4 */
#define Y_COUNT 392    /* 384 middle + 8.4 */
#define VT_TRACK 0.2f  /* VT323 28's 11.2 px advance less the atlas's 11 */
#define TEXT_CX 233.f  /* lab()'s centred x + size x .07 less its trailing letterSpacing's half: 233.91 - .9 */
#define STALE_X 222    /* staleMark(LAMP.x + 62, LAMP.y) */
#define STALE_CY 84    /* the shared marker's ring at cy - 4.5 (the mockup's 80) and STALE on 84 (81 middle + 3.4) */
#define FLIP_MS 450
#define WAKE_M_MS 250  /* the wake: the minutes flip in from here, the hours from 0 */
#define WAKE_DONE_MS 700
#define STUTTER_MS 400
#define PRESS_MS 200
#define LOGGED_MS 1500
#define LINGER_MS 120
#define TEAR_PERIOD 7300u
#define TEAR_MS 160u
#define ROLL_MS 60u
#define LINGER_K 77    /* .3 */
#define LEAVES 4       /* the composed leaves' slots: both cards flipping at once */

const omni_rect_t RD_BK_HIT_BAR = {BAR_X, 107, BAR_X + BAR_W, 155};
const omni_rect_t RD_BK_HIT_LAMP = {LX - 24, LY - 24, LX + 24, LY + 24};
const omni_rect_t RD_BK_LAMP = {LX - GLOW_R, LY - GLOW_R, LX + GLOW_R, LY + GLOW_R};
const omni_rect_t RD_BK_LENS = {LENS_X0, LENS_Y0, LENS_X0 + LENS_N, LENS_Y0 + LENS_N};
const omni_rect_t RD_BK_LABEL = {182, 68, 416, 92};
const omni_rect_t RD_BK_STALE = {182, 72, 262, 90};
const omni_rect_t RD_BK_ROOM = {LX - GLOW_R, 54, 416, LY + GLOW_R};
const omni_rect_t RD_BK_BAR = {BAR_X, BAR_Y, BAR_X + BAR_W, 146};
const omni_rect_t RD_BK_FLIP_H = {CARD_HX, CARD_Y, CARD_HX + CARD_W, CARD_Y + CARD_H};
const omni_rect_t RD_BK_FLIP_M = {CARD_MX, CARD_Y, CARD_MX + CARD_W, CARD_Y + CARD_H};
const omni_rect_t RD_BK_DATE = {80, 342, 386, 362};
const omni_rect_t RD_BK_COUNT = {68, 362, 398, 404};

#define LABEL_ON "ROOMMATE \267 PROJECTED"
#define LABEL_OFF "ROOMMATE \267 PAUSED (BLISS)"
#define LOGGED "[ REQUEST LOGGED ]"

typedef struct {
    fk_img_t case_img, shelf_img, s132_img, s132o_img, ch13_img;
    fk_atlas_t s132, s132o, ch13;
    rd_atlas2_t vt28;   /* used where filled, never copied */
    const uint8_t *g26;
    int16_t stale_cx;
    uint8_t lens_cov[LENS_N][LENS_N];   /* the lens disc r 13's coverage (0..255) */
    uint8_t lens_t[LENS_N][LENS_N];     /* the radial gradient's position at the pixel centre, (d - 1) / 12 (0..255) */
    uint16_t blank[CARD_H * CARD_W];    /* the card blank over the well, RGB565 */
    /* the composed leaves (draw()'s scratch; see the header): each slot's text and colour, valid when key[0], and when
     * it was last used (a count of leaf_of calls) */
    uint16_t leaf[LEAVES][CARD_H * CARD_W];
    char key[LEAVES][3];
    uint16_t key_rgb[LEAVES];
    uint32_t used[LEAVES], tick;
    uint32_t composed;   /* how many leaves draw() has composed (rd_bk_leaf_composes) */
} bk_t;

/* ---- small helpers */
static uint8_t a8_of(float a)
{
    if (!(a > 0)) return 0;
    if (a >= 1) return 255;
    return (uint8_t)(a * 255.f + 0.5f);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }

static uint16_t dim565(uint16_t c, uint8_t k)
{
    return omni_pack565(omni_ex_r(c) * k / 255, omni_ex_g(c) * k / 255, omni_ex_b(c) * k / 255);
}

/* a rect [x0, x1) x [y0, y1) in rgb at a8 (canvas fillRect at whole pixels) */
static void rect_fill(uint16_t *fb, int stride, omni_rect_t clip, int x0, int y0, int x1, int y1, uint16_t rgb,
                      uint8_t a8)
{
    const int a = x0 > clip.x0 ? x0 : clip.x0, b = x1 < clip.x1 ? x1 : clip.x1;
    for (int y = y0 > clip.y0 ? y0 : clip.y0; y < y1 && y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = a; x < b; x++) d[x] = a8 == 255 ? rgb : omni_blend565(d[x], rgb, a8);
    }
}

/* integer square root (floor) */
static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) v -= r + b, r = (r >> 1) + b;
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

uint32_t rd_bk_count(const fk_clock_t *c, uint16_t variant)
{
    if (c == NULL || !c->set) return 0;
    return RD_BK_COUNT_BASE + c->yday + (uint32_t)(variant & RD_BK_TAPS);
}

void rd_bk_count_str(uint32_t n, char out[32])
{
    if (n == 0) {
        strcpy(out, "SNOOZED ? TIMES");
        return;
    }
    char d[12];
    int k = 0;
    for (uint32_t v = n; v && k < 10; v /= 10) d[k++] = (char)('0' + v % 10);   /* the digits, least first */
    char *o = out;
    memcpy(o, "SNOOZED ", 8), o += 8;
    for (int i = k - 1; i >= 0; i--) {   /* group3(): a space before every group of three from the right */
        *o++ = d[i];
        if (i && i % 3 == 0) *o++ = ' ';
    }
    memcpy(o, " TIMES", 7);
}

uint32_t rd_bk_leaf_composes(const void *ctx) { return ((const bk_t *)ctx)->composed; }

unsigned rd_bk_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_BK_LAMP;
    out[1] = RD_BK_FLIP_M;
    return 2;
}

/* ---- bind */
static bool bind(void *ctx, fk_asset_fn get)
{
    bk_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_BK_CASE, &n);
    bool ok = b && fk_img_from_bin(&k->case_img, b, n) && k->case_img.rgb && !k->case_img.a &&
              k->case_img.w == KW && k->case_img.h == KH;
    b = get(ASSET_RD_BK_SHELF, &n);
    ok = ok && b && fk_img_from_bin(&k->shelf_img, b, n) && k->shelf_img.rgb && k->shelf_img.w == 1 &&
         k->shelf_img.h == SHELF_H;
    b = get(ASSET_RD_F_S132, &n);
    ok = ok && b && fk_img_from_bin(&k->s132_img, b, n) && k->s132_img.a && !k->s132_img.rgb;
    b = get(ASSET_RD_F_S132O, &n);
    ok = ok && b && fk_img_from_bin(&k->s132o_img, b, n) && k->s132o_img.a;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    b = get(ASSET_RD_F_VT28, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt28);
    k->s132 = (fk_atlas_t){&k->s132_img, RD_CELLS_RD_F_S132, RD_GLYPHS_RD_F_S132, ASSET_RD_F_S132_LINE_H};
    k->s132o = (fk_atlas_t){&k->s132o_img, RD_CELLS_RD_F_S132O, RD_GLYPHS_RD_F_S132O, ASSET_RD_F_S132O_LINE_H};
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    k->g26 = rd_glow_lut(GLOW_R);
    ok = ok && k->g26;
    if (ok) {
        /* staleMark(222, 80)'s group: w = measureText('STALE') with its five trailing 1.8 px spacings + 14, the ring at
         * x0 + 4, STALE from x0 + 14; the shared marker puts them at cx - 30 and cx - 20 */
        const float wt = fk_atlas_width_tracked(&k->ch13, "STALE", TRACK) + TRACK + 14.f;
        k->stale_cx = (int16_t)((float)STALE_X - wt / 2.f + 34.f + 0.5f);
    }
    /* the lens: the disc r 13 about (160, 80), each pixel's share of 16 x 16 sample points; and the gradient's
     * position at the pixel centre, (d - 1) / 12, d from the integer square root of 2^16 d^2 */
    for (int j = 0; j < LENS_N; j++)
        for (int i = 0; i < LENS_N; i++) {
            int hit = 0;
            for (int sy = 0; sy < 16; sy++)
                for (int sx = 0; sx < 16; sx++) {
                    const int dx = 32 * (LENS_X0 + i - LX) + 2 * sx + 1, dy = 32 * (LENS_Y0 + j - LY) + 2 * sy + 1;
                    hit += dx * dx + dy * dy <= (32 * LR) * (32 * LR);
                }
            k->lens_cov[j][i] = (uint8_t)((hit * 255 + 128) / 256);
            const int ex = 2 * (LENS_X0 + i - LX) + 1, ey = 2 * (LENS_Y0 + j - LY) + 1;
            const uint32_t d256 = isqrt32((uint32_t)(ex * ex + ey * ey) * 16384u);   /* 256 d */
            const int t = d256 <= 256u ? 0 : (int)((d256 - 256u) * 255u / (12u * 256u));
            k->lens_t[j][i] = (uint8_t)(t > 255 ? 255 : t);
        }
    /* the card blank (cardFace()): the rounded rect r 7, a linear gradient down it (#2a2724, #1c1a18 at .5, #151312
     * from .5, #201d1b), each pixel at its centre's row, its corners' coverage from 16 x 16 sample points over the
     * well; then to RGB565 through a 4 x 4 ordered dither (the gradient spans two or three 565 steps a channel:
     * rounded alone they band, each band a different hue) */
    static const uint8_t STOP[4][3] = {{0x2A, 0x27, 0x24}, {0x1C, 0x1A, 0x18}, {0x15, 0x13, 0x12}, {0x20, 0x1D, 0x1B}};
    static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    static const uint8_t WELL[3] = {5, 4, 3};
    for (int y = 0; y < CARD_H; y++) {
        const int t2 = 2 * y + 1;   /* the row centre in half pixels: t = t2 / 300 */
        const uint8_t *c0 = t2 < CARD_H ? STOP[0] : STOP[2], *c1 = t2 < CARD_H ? STOP[1] : STOP[3];
        const float u = (float)(t2 < CARD_H ? t2 : t2 - CARD_H) / (float)CARD_H;   /* the share of the half */
        float rgb[3];
        for (int q = 0; q < 3; q++) rgb[q] = (float)c0[q] + ((float)c1[q] - (float)c0[q]) * u;
        for (int x = 0; x < CARD_W; x++) {
            int hit = 256;
            const int cx = x < CARD_R ? CARD_R : x >= CARD_W - CARD_R ? CARD_W - CARD_R : -1;
            const int cy = y < CARD_R ? CARD_R : y >= CARD_H - CARD_R ? CARD_H - CARD_R : -1;
            if (cx >= 0 && cy >= 0) {
                hit = 0;
                for (int sy = 0; sy < 16; sy++)
                    for (int sx = 0; sx < 16; sx++) {
                        const int dx = 32 * (x - cx) + 2 * sx + 1, dy = 32 * (y - cy) + 2 * sy + 1;
                        hit += dx * dx + dy * dy <= (32 * CARD_R) * (32 * CARD_R);
                    }
            }
            const float a = (float)hit / 256.f, th = ((float)BAYER[y & 3][x & 3] + 0.5f) / 16.f;
            int q5[3];
            for (int q = 0; q < 3; q++) {
                const float v = (float)WELL[q] + (rgb[q] - (float)WELL[q]) * a, top = q == 1 ? 63.f : 31.f;
                const int k = (int)(v * top / 255.f + th);
                q5[q] = k > (int)top ? (int)top : k;
            }
            k->blank[y * CARD_W + x] = (uint16_t)(q5[0] << 11 | q5[1] << 5 | q5[2]);
        }
    }
    return ok;
}

/* ---- the state a frame shows */
typedef struct {
    char old[3], cur[3];      /* the outgoing and the incoming text ("--" unset) */
    uint16_t old_rgb, cur_rgb;
    float p;                  /* the flip, eased: 1 settled */
} card_t;

typedef struct {
    bool set, stale, reduced, paused;
    card_t hc, mc;
    bool on;                  /* the lamp's lens is lit (not paused) */
    float L;                  /* its level */
    int8_t jx, so;            /* the tear's shift and the scanlines' roll */
    uint8_t glow;             /* its glow's alpha (the chime's swell in it) */
    bool pressed;
    char date[40];
    char cnt[32], old[32];    /* the count line and its lingering old value ("" none) */
    uint8_t kc, ko;           /* their levels */
} bk_state_t;

static void hm_of(const fk_clock_t *c, bool h24, char hs[3], char ms[3])
{
    char t[6];
    const int n = fk_time_str(c, h24, t);   /* "10:09", "9:05" */
    const int k = n - 3;
    memcpy(hs, t, (size_t)k);
    hs[k] = 0;
    ms[0] = t[k + 1], ms[1] = t[k + 2], ms[2] = 0;
}

static void card_set(card_t *c, const char *old, uint16_t old_rgb, const char *cur, uint16_t cur_rgb, float p)
{
    strcpy(c->old, old), strcpy(c->cur, cur);
    c->old_rgb = old_rgb, c->cur_rgb = cur_rgb;
    c->p = p;
    if (p >= 1.f || (strcmp(old, cur) == 0 && old_rgb == cur_rgb)) {   /* nothing to flip (the twitch sets p after) */
        strcpy(c->old, cur);
        c->old_rgb = cur_rgb;
        c->p = 1.f;
    }
}

/* the lamp's flicker as it comes on: t ms in, over 400 ms, every other 50 ms at .6 */
static float stutter(uint32_t t) { return clamp01((float)t / (float)STUTTER_MS) * ((t / 50u) % 2u ? 0.6f : 1.f); }

static void state_of(const fk_face_in_t *in, bk_state_t *s)
{
    memset(s, 0, sizeof *s);
    const uint16_t v = in->variant;
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    s->paused = (v & RD_BK_PAUSED) != 0;
    const bool live = s->set && !in->reduced && !in->glance;   /* the clock's motion */
    const bool waking = in->wake_ms != UINT32_MAX && !in->reduced && !in->glance && in->wake_ms < WAKE_DONE_MS;
    const bool lamp_pulse = in->pulsing && (v & RD_BK_LAMP_LAST) != 0;
    const bool bar_pulse = in->pulsing && (v & RD_BK_LAMP_LAST) == 0;
    const uint32_t T = ((uint32_t)(in->clk.h * 60u + in->clk.m) * 60u + in->clk.s) * 1000u + in->clk.ms;

    /* the cards */
    char hs[3] = "--", ms[3] = "--";
    if (s->set) hm_of(&in->clk, in->h24, hs, ms);
    const uint16_t ink = s->set ? RD_C_PAINT : RD_C_PAINT_DIM;
    card_set(&s->hc, hs, ink, hs, ink, 1.f);
    card_set(&s->mc, ms, ink, ms, ink, 1.f);
    if (waking) {   /* from -- in dim paint: the hours over 0-450 ms, the minutes over 250-700 ms (easeIn2) */
        const float ph = clamp01((float)in->wake_ms / (float)FLIP_MS);
        const float pm = clamp01(((float)in->wake_ms - (float)WAKE_M_MS) / (float)FLIP_MS);
        card_set(&s->hc, "--", RD_C_PAINT_DIM, hs, ink, ph * ph);
        card_set(&s->mc, "--", RD_C_PAINT_DIM, ms, ink, pm * pm);
    } else if (live && in->clk.s == 0 && in->clk.ms < FLIP_MS) {   /* the minute's change, from the clock */
        fk_clock_t pc = in->clk;
        const int m = (in->clk.h * 60 + in->clk.m + 1439) % 1440;
        pc.h = (uint8_t)(m / 60), pc.m = (uint8_t)(m % 60);
        char ph[3], pm[3];
        hm_of(&pc, in->h24, ph, pm);
        const float lin = (float)in->clk.ms / (float)FLIP_MS, p = lin * lin;
        card_set(&s->hc, ph, ink, hs, ink, p);
        card_set(&s->mc, pm, ink, ms, ink, p);
    }
    /* the chime's twitch: the minute card's top folds a quarter of the way (half a flap) with beep */
    if (live && s->mc.p >= 1.f && in->beep > 0) {
        const float b = clamp01(in->beep);
        s->mc.p = 0.25f * b;
        if (!(s->mc.p > 0)) s->mc.p = 1.f;
    }

    /* the lamp */
    s->on = !s->paused;
    s->L = 1.f;
    if (live) {
        const uint32_t z = T % TEAR_PERIOD;
        if (z < TEAR_MS) {   /* the tear: .9 and -2 px, .25 and +3 px, every 40 ms */
            const bool odd = (z / 40u) % 2u != 0;
            s->L = odd ? 0.25f : 0.9f;
            s->jx = (int8_t)(odd ? 3 : -2);
        }
        s->so = (int8_t)((T / ROLL_MS) % 3u);
    }
    if (s->on && !in->reduced && !in->glance) {   /* it stutters on at the wake, and comes back on after a lamp tap */
        if (in->wake_ms != UINT32_MAX && in->wake_ms < STUTTER_MS) s->L *= stutter(in->wake_ms);
        if (lamp_pulse && in->pulse_ms < STUTTER_MS) s->L *= stutter(in->pulse_ms);
    }
    if (!s->on) s->L = 0, s->jx = 0;
    float g = 0.25f * s->L;
    if (s->on && !in->reduced && !in->glance && in->thrum > 0) g *= 1.f + 1.5f * clamp01(in->thrum);   /* the swell */
    s->glow = a8_of(g);

    /* the bar */
    s->pressed = s->set && bar_pulse && in->pulse_ms < PRESS_MS;

    /* the date line */
    char d[16];
    fk_date_str(&in->clk, d);
    strcpy(s->date, d);
    strcat(s->date, fk_ampm(&in->clk, in->h24));
    strcat(s->date, " \267 BUNK 2");

    /* the count line */
    const uint32_t n = rd_bk_count(&in->clk, v);
    if (!s->set) {
        rd_bk_count_str(0, s->cnt);
        s->kc = 153;   /* .6 */
    } else if (bar_pulse && in->pulse_ms < LOGGED_MS) {
        strcpy(s->cnt, LOGGED);
        s->kc = 255;
        if (!in->reduced && in->pulse_ms < LINGER_MS) {   /* the old count at .3 under the message at .5 */
            const uint16_t pv = (uint16_t)((v & ~RD_BK_TAPS) | ((v + RD_BK_TAPS) & RD_BK_TAPS));
            rd_bk_count_str(rd_bk_count(&in->clk, pv), s->old);
            s->ko = LINGER_K;
            s->kc = 128;
        }
    } else {
        rd_bk_count_str(n, s->cnt);
        s->kc = 255;
        /* midnight: the day's count lingers (not on 1 January: the old year's length is not the clock's) */
        if (!in->reduced && !in->glance && T < LINGER_MS && in->clk.yday > 1) {
            rd_bk_count_str(n - 1u, s->old);
            s->ko = LINGER_K;
        }
    }
}

/* ---- drawing */
static void base_draw(const bk_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;
        const uint16_t g = y < SHELF_Y ? BG565 : k->shelf_img.rgb[y - SHELF_Y];
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = g;
        if (y < KY || y >= KY + KH) continue;
        const int a = clip.x0 > KX ? clip.x0 : KX, b = clip.x1 < KX + KW ? clip.x1 : KX + KW;
        if (a < b) memcpy(d + (a - clip.x0), k->case_img.rgb + (size_t)(y - KY) * KW + (a - KX), (size_t)(b - a) * 2);
    }
}

/* a card's text on its fixed advances, centred on the card (drawTime()): the pen x for a card at cardx */
static int dig_pen(const bk_t *k, const char *t, int cardx)
{
    return cardx + DIG_CX - fk_atlas_width(&k->s132, t, 0) / 2;
}

/* the card at rest: the blank and its digits */
static void card_still(const bk_t *k, uint16_t *fb, int stride, omni_rect_t clip, int cardx, const char *t,
                       uint16_t rgb)
{
    const int x0 = clip.x0 > cardx ? clip.x0 : cardx, x1 = clip.x1 < cardx + CARD_W ? clip.x1 : cardx + CARD_W;
    const int y0 = clip.y0 > CARD_Y ? clip.y0 : CARD_Y, y1 = clip.y1 < CARD_Y + CARD_H ? clip.y1 : CARD_Y + CARD_H;
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; y++)
        memcpy(fb + (size_t)(y - clip.y0) * (size_t)stride + (x0 - clip.x0),
               k->blank + (size_t)(y - CARD_Y) * CARD_W + (x0 - cardx), (size_t)(x1 - x0) * 2);
    fk_atlas_text_rgb(fb, stride, clip, &k->s132, t, dig_pen(k, t, cardx), CARD_Y + DIG_BASE, 0, rgb, 255, 0);
}

/* the leaf slot holding (t, rgb), composing it into an empty one or the one used longest ago (never `keep`) when none
 * does; the leaf is the card at cardx with its rows from CARD_Y, stride CARD_W (a pure function of t and rgb) */
static int leaf_of(bk_t *m, int cardx, const char *t, uint16_t rgb, int keep)
{
    const uint32_t now = ++m->tick;
    for (int i = 0; i < LEAVES; i++)
        if (m->key[i][0] && strcmp(m->key[i], t) == 0 && m->key_rgb[i] == rgb) {
            m->used[i] = now;
            return i;
        }
    int i = -1;
    for (int j = 0; j < LEAVES; j++) {
        if (j == keep) continue;
        if (!m->key[j][0]) {
            i = j;
            break;
        }
        if (i < 0 || now - m->used[j] > now - m->used[i]) i = j;
    }
    m->used[i] = now;
    const omni_rect_t box = {(int16_t)cardx, CARD_Y, (int16_t)(cardx + CARD_W), CARD_Y + CARD_H};
    m->composed++;
    memcpy(m->leaf[i], m->blank, sizeof m->leaf[i]);
    fk_atlas_text_rgb(m->leaf[i], CARD_W, box, &m->s132, t, dig_pen(m, t, cardx), CARD_Y + DIG_BASE, 0, rgb, 255, 0);
    strcpy(m->key[i], t);
    m->key_rgb[i] = rgb;
    return i;
}

/* one half of a card through fk_rows_blit: the leaf's half (rows from h0) squashed into box rows [d0, d0 + HALF s),
 * sampled at the row centres, black at shade over it */
static void half_fold(const uint16_t *leaf, int h0, int cardx, int box_y, float d0, float s, uint8_t shade,
                      omni_rect_t clip, uint16_t *fb, int stride)
{
    fk_row_t rows[HALF];
    for (int y = 0; y < HALF; y++) {
        const float c = (float)y + 0.5f - d0, e = (float)HALF * s;
        rows[y] = (fk_row_t){-1, 65536u};
        if (s > 0 && c >= 0 && c < e) {
            int v = (int)(c / s);
            rows[y].v = (int16_t)(v > HALF - 1 ? HALF - 1 : v);
        }
    }
    const fk_rows_state_t st = {.theta = 0, .dy = 0, .shade = shade, .fade = 255};
    const omni_rect_t box = {(int16_t)cardx, (int16_t)box_y, (int16_t)(cardx + CARD_W), (int16_t)(box_y + HALF)};
    fk_rows_blit(&st, rows, leaf + (size_t)h0 * CARD_W, CARD_W, HALF, box, clip, fb, stride);
}

/* card(): settled, or the flip at p (eased) from c->old to c->cur */
static void card_draw(const bk_t *k, const card_t *c, uint16_t *fb, int stride, omni_rect_t clip, int cardx)
{
    const omni_rect_t box = {(int16_t)cardx, CARD_Y, (int16_t)(cardx + CARD_W), CARD_Y + CARD_H};
    if (meets(box, clip)) {
        if (c->p >= 1.f) {
            card_still(k, fb, stride, clip, cardx, c->cur, c->cur_rgb);
        } else {
            bk_t *m = (bk_t *)(uintptr_t)k;   /* the leaves: draw()'s one write (see the header) */
            const int ia = leaf_of(m, cardx, c->old, c->old_rgb, -1);
            const int ib = leaf_of(m, cardx, c->cur, c->cur_rgb, ia);
            const uint16_t *A = m->leaf[ia], *B = m->leaf[ib];
            const int x0 = clip.x0 > cardx ? clip.x0 : cardx, x1 = clip.x1 < cardx + CARD_W ? clip.x1 : cardx + CARD_W;
            for (int y = clip.y0 > CARD_Y ? clip.y0 : CARD_Y; y < clip.y1 && y < CARD_Y + CARD_H; y++)
                if (x0 < x1)   /* the new top and the old bottom, behind */
                    memcpy(fb + (size_t)(y - clip.y0) * (size_t)stride + (x0 - clip.x0),
                           (y < CARD_Y + HALF ? B : A) + (size_t)(y - CARD_Y) * CARD_W + (x0 - cardx),
                           (size_t)(x1 - x0) * 2);
            float sn, cs;
            fk_sincos(c->p * 3.14159265359f, &sn, &cs);
            (void)sn;
            if (c->p < 0.5f) {   /* the old top folding down to the hinge */
                const float s = cs;
                half_fold(A, 0, cardx, CARD_Y, (float)HALF - (float)HALF * s, s, a8_of(0.55f * (1.f - s)), clip, fb,
                          stride);
            } else {             /* the new bottom falling from it */
                const float s = -cs;
                half_fold(B, HALF, cardx, CARD_Y + HALF, 0.f, s, a8_of(0.45f * (1.f - s)), clip, fb, stride);
            }
        }
    }
    rect_fill(fb, stride, clip, cardx, SPLIT_Y, cardx + CARD_W, SPLIT_Y + 2, 0x0000, 255);
    rect_fill(fb, stride, clip, cardx - 3, PIN_Y, cardx + 2, PIN_Y + 8, RD_C_STEEL, 255);
    rect_fill(fb, stride, clip, cardx + CARD_W - 2, PIN_Y, cardx + CARD_W + 3, PIN_Y + 8, RD_C_STEEL, 255);
}

/* holoLamp(): the ring, the lens (or dark), the scanlines, then the glow */
static void lamp_draw(const bk_t *k, const bk_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_BK_LAMP, clip)) return;
    fk_disc(fb, stride, &clip, (float)LX, (float)LY, (float)RING_R, RD_C_STEEL_DEEP, 255);
    const int y0 = clip.y0 > LENS_Y0 ? clip.y0 : LENS_Y0, y1 = clip.y1 < LENS_Y0 + LENS_N ? clip.y1 : LENS_Y0 + LENS_N;
    const int x0 = clip.x0 > LENS_X0 ? clip.x0 : LENS_X0, x1 = clip.x1 < LENS_X0 + LENS_N ? clip.x1 : LENS_X0 + LENS_N;
    /* the gradient's ends: phosphor at .95 L, phosphorDim at .6 L */
    const float a0 = 0.95f * s->L, a1 = 0.6f * s->L;
    const int r0 = omni_ex_r(RD_C_PHOS), g0 = omni_ex_g(RD_C_PHOS), b0 = omni_ex_b(RD_C_PHOS);
    const int r1 = omni_ex_r(RD_C_PHOS_DIM), g1 = omni_ex_g(RD_C_PHOS_DIM), b1 = omni_ex_b(RD_C_PHOS_DIM);
    const int sl0 = LY - LR + s->so;   /* the first scanline */
    for (int y = y0; y < y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        const bool line = y >= sl0 && y < LY + LR && (y - sl0) % 3 == 0;
        for (int x = x0; x < x1; x++) {
            const int cov = k->lens_cov[y - LENS_Y0][x - LENS_X0];
            if (!cov) continue;
            if (s->on) {
                const int t = k->lens_t[y - LENS_Y0][x - LENS_X0];
                const uint16_t c = omni_pack565((r0 * (255 - t) + r1 * t) / 255, (g0 * (255 - t) + g1 * t) / 255,
                                                (b0 * (255 - t) + b1 * t) / 255);
                const float a = (a0 * (float)(255 - t) + a1 * (float)t) / 255.f;
                const int av = (int)(a * (float)cov + 0.5f);
                if (av) d[x] = omni_blend565(d[x], c, av);
            } else {
                d[x] = omni_blend565(d[x], RD_RGB(0x0C, 0x12, 0x0E), cov);
            }
            if (line && x >= LX - LR + s->jx && x < LX + LR + s->jx)   /* black .45 at the lens's coverage */
                d[x] = omni_blend565(d[x], 0x0000, (cov * 115 + 127) / 255);
        }
    }
    if (s->glow) fk_glow_add_lut(fb, stride, &clip, LX, LY, GLOW_R, k->g26, RD_C_PHOS, s->glow);
}

/* phos(): the glow plane then the core, both in amber dimmed by k (at alpha 255), centred on 233; VT323's advance at 28
 * px is 11.2 (400 of 1000 units) where the atlas's cells hold 11, so each glyph steps on by VT_TRACK more */
static void count_text(const bk_t *k, uint16_t *fb, int stride, omni_rect_t clip, const char *t, uint8_t kk)
{
    if (!kk || !*t) return;
    const uint16_t c = dim565(RD_C_AMBER, kk);
    fk_atlas_text_tracked(fb, stride, clip, &k->vt28.glow, t, 233.f, Y_COUNT, VT_TRACK, true, c, 255);
    fk_atlas_text_tracked(fb, stride, clip, &k->vt28.core, t, 233.f, Y_COUNT, VT_TRACK, true, c, 255);
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const bk_t *k = ctx;
    bk_state_t s;
    state_of(in, &s);
    base_draw(k, fb, stride, clip);
    if (meets(RD_BK_BAR, clip)) {
        if (s.pressed) rect_fill(fb, stride, clip, BAR_X, BAR_Y, BAR_X + BAR_W, BAR_Y + 4, 0x0000, 115);
        const int y = Y_SNOOZE + (s.pressed ? 3 : 0);   /* down with the bar; unset, at .5 */
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "SNOOZE", TEXT_CX, y, TRACK, true, RD_C_HAZARD_DARK,
                              s.set ? 255 : 128);
    }
    lamp_draw(k, &s, fb, stride, clip);
    if (meets(RD_BK_LABEL, clip)) {
        if (s.stale) {
            rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx, STALE_CY, false);
        } else {
            const char *t = s.paused ? LABEL_OFF : LABEL_ON;
            const float w = fk_atlas_width_tracked(&k->ch13, t, TRACK);
            fk_atlas_text_tracked(fb, stride, clip, &k->ch13, t, (float)LABEL_X + w / 2.f, Y_LABEL, TRACK, true,
                                  s.paused ? RD_C_PAINT : RD_C_PAINT_DIM, 255);
        }
    }
    card_draw(k, &s.hc, fb, stride, clip, CARD_HX);
    card_draw(k, &s.mc, fb, stride, clip, CARD_MX);
    if (meets(RD_BK_DATE, clip))
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, s.date, TEXT_CX, Y_DATE, TRACK, true, RD_C_PAINT_DIM, 255);
    if (meets(RD_BK_COUNT, clip)) {
        count_text(k, fb, stride, clip, s.old, s.ko);
        count_text(k, fb, stride, clip, s.cnt, s.kc);
    }
}

/* sleepFrame(): the split lines, the outlined digits, the hollow lamp ring (a bar across it while projected), the
 * stale marker; through the orbit */
static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const bk_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const int ox = in->odx, oy = in->ody;
    const bool set = in->clk.set, stale = set && in->clk.stale, paused = (in->variant & RD_BK_PAUSED) != 0;
    const int cx[2] = {CARD_HX, CARD_MX};
    for (int i = 0; i < 2; i++) {
        const float x = (float)(cx[i] + ox), y = (float)(CARD_Y + HALF + oy);
        fk_line(fb, stride, &clip, x + 6.f, y, x + (float)CARD_W - 6.f, y, 1.2f, RD_G_STEEL, 255);
    }
    char hs[3] = "--", ms[3] = "--";
    if (set) hm_of(&in->clk, in->h24, hs, ms);
    const char *t[2] = {hs, ms};
    for (int i = 0; i < 2; i++) {
        const int x = cx[i] + ox + DIG_CX - fk_atlas_width(&k->s132o, t[i], 0) / 2;
        fk_atlas_text_rgb(fb, stride, clip, &k->s132o, t[i], x, CARD_Y + DIG_BASE + oy, 0, RD_G_PAINT, 255, 0);
    }
    fk_ring(fb, stride, &clip, (float)(LX + ox), (float)(LY + oy), (float)LR, 1.4f, RD_G_PHOS, 255);
    if (!paused) rect_fill(fb, stride, clip, LX - 6 + ox, LY - 1 + oy, LX + 6 + ox, LY + 1 + oy, RD_G_PHOS, 255);
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx + ox, STALE_CY + oy, true);
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

static uint8_t whole(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static bool card_same(const card_t *a, const card_t *b)
{
    return strcmp(a->old, b->old) == 0 && strcmp(a->cur, b->cur) == 0 && a->old_rgb == b->old_rgb &&
           a->cur_rgb == b->cur_rgb && a->p == b->p;
}

static uint8_t dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance) return whole(out);
    if (a->glance) {   /* redrawn whole when anything it shows changes (once a minute at most) */
        char ha[3] = "", ma[3] = "", hb[3] = "", mb[3] = "";
        if (a->clk.set) hm_of(&a->clk, a->h24, ha, ma);
        if (b->clk.set) hm_of(&b->clk, b->h24, hb, mb);
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody && strcmp(ha, hb) == 0 && strcmp(ma, mb) == 0 &&
                          (a->variant & RD_BK_PAUSED) == (b->variant & RD_BK_PAUSED);
        return same ? 0 : whole(out);
    }
    bk_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || a->h24 != b->h24 || sa.reduced != sb.reduced) return whole(out);
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.on != sb.on || sa.L != sb.L || sa.jx != sb.jx || sa.glow != sb.glow) add(&L, RD_BK_LAMP);
    else if (sa.so != sb.so) add(&L, RD_BK_LENS);
    if (sa.stale != sb.stale || sa.paused != sb.paused) add(&L, RD_BK_LABEL);
    if (sa.pressed != sb.pressed) add(&L, RD_BK_BAR);
    if (!card_same(&sa.hc, &sb.hc)) add(&L, RD_BK_FLIP_H);
    if (!card_same(&sa.mc, &sb.mc)) add(&L, RD_BK_FLIP_M);
    if (strcmp(sa.date, sb.date) != 0) add(&L, RD_BK_DATE);
    if (strcmp(sa.cnt, sb.cnt) != 0 || sa.kc != sb.kc || strcmp(sa.old, sb.old) != 0 || sa.ko != sb.ko)
        add(&L, RD_BK_COUNT);
    if (L.n > 8) {
        out[0] = L.u;
        return 1;
    }
    for (unsigned i = 0; i < L.n; i++) out[i] = L.r[i];
    return (uint8_t)L.n;
}

static bool in_rect(const omni_rect_t *r, int16_t x, int16_t y)
{
    return x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1;
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return in_rect(&RD_BK_HIT_BAR, x, y) || in_rect(&RD_BK_HIT_LAMP, x, y) ? FK_TAP_PULSE : FK_TAP_NONE;
}

/* the bar: one more snooze (bits 0-13, wrapping at 16 384) and bit 14 cleared, then the pulse ([ REQUEST LOGGED ]);
 * inert while the clock is unset (FK_TAP_NONE, the variant left: the count shows SNOOZED ? TIMES). The lamp: the pause
 * toggles (bit 15) and bit 14 is set, then the pulse (its flicker back on). RAM only. A NULL in (only from outside the
 * runtime, fk/face.h) counts as set */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    (void)ctx;
    const uint16_t v = *variant;
    if (in_rect(&RD_BK_HIT_BAR, x, y)) {
        if (in != NULL && !in->clk.set) return FK_TAP_NONE;
        *variant = (uint16_t)((v & RD_BK_PAUSED) | ((v + 1u) & RD_BK_TAPS));
        return FK_TAP_PULSE;
    }
    if (in_rect(&RD_BK_HIT_LAMP, x, y)) {
        *variant = (uint16_t)((v ^ RD_BK_PAUSED) | RD_BK_LAMP_LAST);
        return FK_TAP_PULSE;
    }
    return FK_TAP_NONE;
}

const fk_face_ops_t RD_FACE_BUNK = {
    .key = "bunk",
    .ctx_bytes = sizeof(bk_t),
    .cadence_ms = 40,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = 1000,
    .pulse_len_ms = 1500,   /* [ REQUEST LOGGED ] for 1.5 s */
    .tap_var = tap_var,
    .stills_extra = 1,      /* still 11: the roommate paused, one snooze (RD_BK_TAPPED_VARIANT) */
};
