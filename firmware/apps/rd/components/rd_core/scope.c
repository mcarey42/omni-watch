/* Nav Scope (`scope`, rd/faces.h; rd wave 2 Task 7, spec section 10.5; faces-bridge.js:403-573: giantSprite(), build(),
 * numerals(), cursor(), frame() and sleepFrame()).
 *
 * The base, two layers cut from the mockup's own canvases (tools/export/rd_layers.json):
 *   rd_sc_giant, RGB565 284 x 150: giantSprite()'s 292 px canvas on black (each pixel its colour at its alpha), its
 *   top-left 284 x 150, laid at (116, 272 + the wake's rise) and clipped to the glass (r 150 about the centre: each
 *   pixel's exact coverage, kept from bind), at the corona's breath times the wake's fade (k: the whole sprite drawn at
 *   alpha k over black, as the mockup's globalAlpha);
 *   rd_sc_dial, A8 466 x 466: build()'s canvas with numerals()'s twelve stencilled numerals over it (the rims, the
 *   ticks, the rivets, the graticule and its scale), each byte a colour and an alpha of RD_SC_DIAL_RUNS laid over what
 *   lies under it: the giant inside the glass, black elsewhere (a byte 0 is nothing). The numerals are the mockup's own
 *   paintSprite() pixels (the brief's first form: the layer, not rd_f_s30 at draw), so the dial is the mockup's.
 * Over the base, in frame()'s order, inside the glass only (RD_SC_GLASS): the wake's warm-up (black over r rr to 151 and
 * a phosphor ring at rr, rr = 150 easeOut3(1.25 wk)); the sweep's afterglow fan (8 wedges of .08 rad behind it,
 * phosphor at .09 (1 - i / 8), clipped to r 149) and the sweep (1.6 px phosphor .85 to r 149); the four contacts (r 2.4
 * + 1.4 L at .15 + .8 L, L = e^(-since / 1.8) of the angle since the sweep passed) and the TAKEAWAY? tag (VT323 20,
 * phosphor .3 + .6 L); the readout (phosText(): VT323 20 at .75 and 28 at .95, glow plane in phosphorDim then the core in
 * phosphor; the stale marker in the top line's place); the cursors (cursor(): a phosphorDim .9 stroke w + 3 with its
 * shadowBlur 8 and the w phosphor core, round caps and joins, over the shaft from r 12 and the two 14 px chevron arms;
 * hour 88 at 4.5, minute 138 at 2.8) or, unset, --:-- (VT323 28 paintDim); the hub (a black disc r 9 and the two
 * phosphor bars).
 * The cursors' shadow is the blurred stroke's profile across its width (a stripe of half-width (w + 3) / 2 under a
 * Gaussian of sigma 4, the canvas's shadowBlur / 2) at each pixel's distance from the path: const tables by the squared
 * distance (SH_HOUR, SH_MIN; the profile is Phi((h - d) / 4) - Phi((-h - d) / 4)). The contacts' fade is a const table
 * of e^(-x / 1.8) (FADE_EXP). Pure: no esp_*, freertos or LVGL; no libm (angles through fk_sincos, square roots by
 * Newton's steps); no allocation; every array a draw needs over 256 B is in the context, written at bind.
 *
 * The sweep's mode (W2-D11): RD_SC_STEP_MS, 0 continuous or 250 the 4 Hz stepped sweep (C8), unless the console's
 * override is set (rd_sc_set_step_ms: RAM, lost at a reboot). In the stepped mode the sweep, its fan, the contacts and
 * the corona's breath all read the clock floored to the step, so nothing changes between steps. A console switch of the
 * mode may leave the frame shown out of date for up to one step: dirty() reads both of its inputs under the new mode, so
 * a continuous frame drawn under the old one stays until the next quarter-second step redraws the glass (the bench
 * only; the Task 7 review's M2). */
#include <stddef.h>
#include <string.h>
#include "rd/faces.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "fk/compose.h"
#include "fk/ease.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define C 233
#define GR 150.f           /* the glass */
#define GX0 116            /* rd_sc_giant at rest: GIANT (262, 418) less the sprite's half (146) */
#define GY0 272
#define GW 284
#define GH 150
#define MX0 116            /* the giant's glass mask: screen columns [MX0, MX1), rows [MY0, MY1) */
#define MX1 384
#define MY0 272
#define MY1 384
#define WAKE_MS 1000
#define PULSE_MS 250       /* the readout's fade after a tap: clamp((t - pageAt) / 250) */
#define RISE 24.f
#define Y_L1 141           /* the readout: VT323 20 'middle' 135 + 6 */
#define Y_L2 165           /* VT323 28 'middle' 157 + 8.4 */
#define Y_HUB 291          /* --:-- at 'middle' C + 50 + 8.4 */
#define VT_TRACK 0.2f      /* VT323 28's 11.2 px advance less the atlas's 11 */
#define STALE_CX 235       /* staleMark(233, 135): the shared marker's STALE lands from (235, 139) */
#define STALE_CY 139
#define S30_DY 7.63f       /* sleepFrame()'s fillText(n, x, y + 1) on the 'middle' line: + 1 + .221 em at 30 px */
#define PI_F 3.14159265359f
#define TAU 6.28318530718f
#define FAN_N 8
#define FAN_W 0.08f
#define CORONA_MEAN 0.92f
#define CORONA_AMP 0.08f
#define CORONA_MS 12000

const omni_rect_t RD_SC_GLASS = {82, 82, 384, 384};
const omni_rect_t RD_SC_READOUT = {96, 104, 370, 190};
const omni_rect_t RD_SC_STALE = {96, 104, 370, 158};
const omni_rect_t RD_SC_TAP = {0, 0, W, W};

/* rd_sc_dial's runs (the row's a8.runs): paint, paintDim, steel, steelDeep, steelHi, phosphorDim */
const rd_hp_run_t RD_SC_DIAL_RUNS[RD_SC_DIAL_RUNS_N] = {
    {1, 96, 0xDC, 0xD3, 0xBE},    {97, 136, 0x8C, 0x84, 0x74},  {137, 184, 0x5A, 0x60, 0x68},
    {185, 200, 0x1E, 0x21, 0x24}, {201, 224, 0x8A, 0x90, 0x96}, {225, 255, 0x2F, 0x7A, 0x48},
};
#define PHOS_RUN0 225

/* the contacts: CONTACTS's angle (from 3 o'clock, less a quarter turn on screen) and radius; 1 is TAKEAWAY? */
static const float CON_A[4] = {0.95f, 4.3f, 5.55f, 2.0f};
static const float CON_R[4] = {118.f, 74.f, 122.f, 56.f};
#define TAG_CONTACT 1

/* exp(-x / 1.8) at x = i / 64 of a turn (2 pi i / 64), i = 0 .. 64 */
static const float FADE_EXP[65] = {
    1.000000f, 0.946919f, 0.896656f, 0.849061f, 0.803992f, 0.761315f, 0.720904f, 0.682638f,
    0.646403f, 0.612091f, 0.579601f, 0.548835f, 0.519703f, 0.492116f, 0.465994f, 0.441259f,
    0.417837f, 0.395658f, 0.374656f, 0.354769f, 0.335937f, 0.318105f, 0.301220f, 0.285231f,
    0.270091f, 0.255754f, 0.242179f, 0.229324f, 0.217151f, 0.205624f, 0.194710f, 0.184374f,
    0.174587f, 0.165320f, 0.156545f, 0.148235f, 0.140367f, 0.132916f, 0.125861f, 0.119180f,
    0.112854f, 0.106863f, 0.101191f, 0.095820f, 0.090734f, 0.085917f, 0.081357f, 0.077038f,
    0.072949f, 0.069077f, 0.065410f, 0.061938f, 0.058650f, 0.055537f, 0.052589f, 0.049798f,
    0.047154f, 0.044651f, 0.042281f, 0.040037f, 0.037912f, 0.035899f, 0.033994f, 0.032189f,
    0.030481f,
};
/* the cursors' shadow at .9 of 255 before its .9: the stroke w + 3 (4.5 + 3, 2.8 + 3) blurred by a Gaussian of sigma 4,
 * Phi((h - d) / 4) - Phi((-h - d) / 4) at d = sqrt(i), h = 3.75 (the hour) and 2.9 (the minute), x 255 rounded; 0
 * past the end */
static const uint8_t SH_HOUR[] = {
    166, 162, 159, 155, 151, 148, 145, 141, 138, 135, 132, 129, 126, 123, 120, 117, 114, 112, 109, 107,
    104, 102, 99, 97, 95, 93, 90, 88, 86, 84, 82, 80, 78, 77, 75, 73, 71, 70, 68, 66,
    65, 63, 62, 60, 59, 57, 56, 55, 53, 52, 51, 50, 49, 47, 46, 45, 44, 43, 42, 41,
    40, 39, 38, 37, 36, 35, 35, 34, 33, 32, 31, 31, 30, 29, 28, 28, 27, 26, 26, 25,
    25, 24, 23, 23, 22, 22, 21, 21, 20, 20, 19, 19, 18, 18, 17, 17, 17, 16, 16, 15,
    15, 15, 14, 14, 14, 13, 13, 13, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10, 9,
    9, 9, 9, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6,
    6, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
};
static const uint8_t SH_MIN[] = {
    136, 132, 129, 125, 122, 119, 116, 113, 110, 107, 104, 102, 99, 96, 94, 91, 89, 87, 85, 82,
    80, 78, 76, 74, 72, 70, 68, 67, 65, 63, 62, 60, 58, 57, 55, 54, 53, 51, 50, 49,
    47, 46, 45, 44, 43, 41, 40, 39, 38, 37, 36, 35, 34, 33, 33, 32, 31, 30, 29, 29,
    28, 27, 26, 26, 25, 24, 24, 23, 22, 22, 21, 21, 20, 20, 19, 19, 18, 18, 17, 17,
    16, 16, 15, 15, 15, 14, 14, 13, 13, 13, 12, 12, 12, 11, 11, 11, 11, 10, 10, 10,
    10, 9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6,
    6, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

typedef struct { uint16_t rgb; uint8_t a; } mark_t;

typedef struct {
    fk_img_t dial, giant, s30_img, ch13_img;
    fk_atlas_t s30, ch13;
    rd_atlas2_t vt20, vt28;   /* used where filled, never copied */
    mark_t mark[256];         /* rd_sc_dial's bytes: a colour and an alpha (0: none) */
    uint16_t dial0[256];      /* and each over black */
    uint8_t gmask[MY1 - MY0][MX1 - MX0];   /* the glass's coverage (r 150) over the giant's rows */
    float con_x[4], con_y[4];
} sc_t;

/* ---- the sweep's mode (W2-D11) */
static uint16_t s_step = RD_SC_STEP_MS;

uint16_t rd_sc_step_ms(void) { return s_step; }

void rd_sc_set_step_ms(uint16_t ms)
{
    if (ms == 0 || ms == 250) s_step = ms;
}

float rd_sc_sweep_turn(const fk_clock_t *c, uint16_t step_ms)
{
    uint16_t ms = c->ms;
    if (step_ms) ms = (uint16_t)(ms - ms % step_ms);
    return ((float)c->s + (float)ms / 1000.f) / 60.f;
}

/* ---- small helpers */
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static uint8_t a8_of(float a) { return fk_ease_to8(a); }
static float ease3(float x) { return fk_ease_out_cubic(clamp01(x)); }
static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static omni_rect_t cut(omni_rect_t a, omni_rect_t b)
{
    omni_rect_t r = {a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1,
                     a.y1 < b.y1 ? a.y1 : b.y1};
    return r;
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
/* a turn (any real) as a canvas angle in [0, TAU): from 3 o'clock, clockwise; turn 0 is 12 o'clock */
static float angle_of(float turn)
{
    float a = turn * TAU - PI_F / 2.f;
    while (a < 0) a += TAU;
    while (a >= TAU) a -= TAU;
    return a;
}
static long ms_of_day(const fk_clock_t *c) { return (((long)c->h * 60 + c->m) * 60 + c->s) * 1000L + c->ms; }
static bool waking(const fk_face_in_t *in) { return !in->reduced && in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_MS; }
static bool chiming(const fk_face_in_t *in) { return in->clk.set && !in->reduced && !in->glance && in->thrum > 0; }

unsigned rd_sc_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_SC_GLASS;
    return 1;
}

/* ---- the face's readings */
float rd_sc_corona(const fk_face_in_t *in)
{
    if (in->reduced) return CORONA_MEAN;   /* no breathing */
    long t = in->clk.set ? ms_of_day(&in->clk) : (long)(in->mono_ms % 86400000u);
    const uint16_t step = rd_sc_step_ms();
    if (step) t -= t % step;   /* the stepped mode: at each step only */
    float s, c;
    fk_sincos((float)(t % CORONA_MS) / (float)CORONA_MS * TAU, &s, &c);
    return CORONA_MEAN + CORONA_AMP * s;
}

bool rd_sc_sweep(const fk_face_in_t *in, float *turn)
{
    *turn = 0;
    if (!in->clk.set) return false;   /* unset: the sweep is dark */
    float t;
    if (in->reduced) t = (float)in->clk.s / 60.f;   /* once a second */
    else if (chiming(in)) t = 0;                     /* the chime: held where the hour's chime began */
    else t = rd_sc_sweep_turn(&in->clk, rd_sc_step_ms());
    if (waking(in)) t -= 1.f - ease3((float)in->wake_ms / (float)WAKE_MS);   /* one decelerating turn onto it */
    *turn = t;
    return true;
}

float rd_sc_contact(const fk_face_in_t *in, int i)
{
    if (!in->clk.set) return 0.1f;
    float L;
    if (in->reduced) {
        L = 0.6f;
    } else {
        float turn;
        rd_sc_sweep(in, &turn);
        float since = angle_of(turn) - (CON_A[i] - PI_F / 2.f);
        while (since < 0) since += TAU;
        while (since >= TAU) since -= TAU;
        const float p = since / TAU * 64.f;
        const int k = (int)p;
        L = k >= 64 ? FADE_EXP[64] : FADE_EXP[k] + (FADE_EXP[k + 1] - FADE_EXP[k]) * (p - (float)k);
    }
    if (i == TAG_CONTACT && !in->reduced && !in->glance && in->beep > 0) {   /* the chime: TAKEAWAY? on each note */
        const float b = in->beep > 1 ? 1.f : in->beep;
        L = b > L ? b : L;
    }
    return L;
}

void rd_sc_readout(const fk_face_in_t *in, char l1[24], char l2[24])
{
    if (!in->clk.set) {
        strcpy(l1, "NO TIME FIX");
        strcpy(l2, "AWAITING SIGNAL");
        return;
    }
    switch (in->variant % RD_SC_PAGES) {
    case 0: strcpy(l1, "HDG  HOME"), strcpy(l2, "ETA: EVENTUALLY"); break;
    case 1: strcpy(l1, "RED GIANT"), strcpy(l2, "STILL RED"); break;
    case 2: strcpy(l1, "NEAREST TAKEAWAY"), strcpy(l2, "9 742 LY"); break;
    default: {
        char d[16];
        fk_date_str(&in->clk, d);
        strcpy(l1, d);
        strcat(l1, fk_ampm(&in->clk, in->h24));
        rd_sync_str(in->sync_age_s, l2);
        break;
    }
    }
    if (in->clk.stale) l1[0] = 0;   /* the marker replaces the top line */
}

void rd_sc_hand_turns(const fk_face_in_t *in, float *hour, float *minute)
{
    *minute = (float)in->clk.m / 60.f;   /* each steps once a minute (lesson 9) */
    *hour = ((float)(in->clk.h % 12) + (float)in->clk.m / 60.f) / 12.f;
}

/* ---- bind */
static bool bind(void *ctx, fk_asset_fn get)
{
    sc_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_SC_DIAL, &n);
    bool ok = b && fk_img_from_bin(&k->dial, b, n) && k->dial.a && !k->dial.rgb && k->dial.w == W && k->dial.h == W;
    b = get(ASSET_RD_SC_GIANT, &n);
    ok = ok && b && fk_img_from_bin(&k->giant, b, n) && k->giant.rgb && !k->giant.a && k->giant.w == GW &&
         k->giant.h == GH;
    b = get(ASSET_RD_F_S30, &n);
    ok = ok && b && fk_img_from_bin(&k->s30_img, b, n) && k->s30_img.a;
    k->s30 = (fk_atlas_t){&k->s30_img, RD_CELLS_RD_F_S30, RD_GLYPHS_RD_F_S30, ASSET_RD_F_S30_LINE_H};
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    b = get(ASSET_RD_F_VT20, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt20);
    b = get(ASSET_RD_F_VT28, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt28);
    for (int r = 0; r < RD_SC_DIAL_RUNS_N; r++) {
        const rd_hp_run_t *q = &RD_SC_DIAL_RUNS[r];
        const int len = q->i1 - q->i0 + 1;
        for (int i = q->i0; i <= q->i1; i++) {
            k->mark[i] = (mark_t){omni_pack565(q->r, q->g, q->b), (uint8_t)(((i - q->i0 + 1) * 255 + len / 2) / len)};
            k->dial0[i] = omni_blend565(0x0000, k->mark[i].rgb, k->mark[i].a);
        }
    }
    /* the glass's exact coverage over the giant's rows (the mockup's clip of r 150) */
    for (int y = MY0; y < MY1; y++)
        for (int x = MX0; x < MX1; x += OMNI_COVER_W) {
            const int xb = x + OMNI_COVER_W < MX1 ? x + OMNI_COVER_W : MX1;
            omni_cover_ring((float)C, (float)C, 0.f, GR, y, x, xb, &k->gmask[y - MY0][x - MX0]);
        }
    for (int i = 0; i < 4; i++) {
        float s, c;
        fk_sincos(CON_A[i] - PI_F / 2.f, &s, &c);
        k->con_x[i] = (float)C + c * CON_R[i], k->con_y[i] = (float)C + s * CON_R[i];
    }
    return ok;
}

/* ---- the state a frame shows (dirty() compares two) */
typedef struct {
    bool set, stale, reduced, sweep_on, fan, warm;
    uint8_t k8;           /* the giant: the corona's breath x the wake's fade */
    uint8_t pulse8;       /* the chime's pulse over the graticule (thrum) */
    uint8_t ring_a;       /* the wake's ring */
    int16_t rise;         /* the wake's rise, px */
    float turn;           /* the sweep (turns from 12, the wake's turn included) */
    float lvl[4];         /* the contacts */
    float rr;             /* the wake's warm radius */
    float grow, ha, ma;   /* the cursors */
    /* the readout */
    char l1[24], l2[24];
    uint8_t a1, a2;
} sc_state_t;

static void state_of(const fk_face_in_t *in, sc_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    const bool wk = waking(in);
    const float t = wk ? (float)in->wake_ms : (float)WAKE_MS;
    s->k8 = a8_of(rd_sc_corona(in) * (wk ? clamp01(t / 500.f) : 1.f));
    s->rise = (int16_t)(wk ? (int)(RISE * (1.f - ease3((t - 100.f) / 900.f)) + 0.5f) : 0);
    if (wk) {
        s->warm = true;
        s->rr = GR * ease3(t / (float)WAKE_MS * 1.25f);
        s->ring_a = a8_of(0.6f * (1.f - t / (float)WAKE_MS));
    }
    s->sweep_on = rd_sc_sweep(in, &s->turn);
    s->fan = s->sweep_on && !in->reduced;
    for (int i = 0; i < 4; i++) s->lvl[i] = rd_sc_contact(in, i);
    if (chiming(in)) s->pulse8 = a8_of(in->thrum);
    if (s->set) {
        rd_sc_hand_turns(in, &s->ha, &s->ma);
        s->grow = wk ? ease3((t - 250.f) / 650.f) : 1.f;
    }
    rd_sc_readout(in, s->l1, s->l2);
    float fade = 1.f;
    if (!in->reduced) {
        if (wk) fade *= clamp01((t - 500.f) / 300.f);
        if (in->pulsing && in->pulse_ms < PULSE_MS) fade *= (float)in->pulse_ms / (float)PULSE_MS;
    }
    s->a1 = a8_of(0.75f * fade);
    s->a2 = a8_of(0.95f * fade);
}

/* ---- drawing */
/* the giant (inside the glass) and the dial over it; black and the dial elsewhere */
static void base_draw(const sc_t *k, const sc_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    const float front = (float)s->pulse8 / 255.f * (GR + 20.f), pk = (float)s->pulse8 / 255.f;
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        const uint8_t *row = k->dial.a + (size_t)y * W;
        for (int x = clip.x0; x < clip.x1; x++) d[x] = k->dial0[row[x]];
        /* the giant's span on this row */
        const int j = y - GY0 - s->rise;
        if (y >= MY0 && y < MY1 && j >= 0 && j < GH && s->k8) {
            const int xa = clip.x0 > MX0 ? clip.x0 : MX0, xb = clip.x1 < MX1 ? clip.x1 : MX1;
            const uint16_t *g = k->giant.rgb + (size_t)j * GW - GX0;
            const uint8_t *m = k->gmask[y - MY0] - MX0;
            for (int x = xa; x < xb; x++) {
                if (!m[x] || !g[x]) continue;
                const int kk = m[x] == 255 ? s->k8 : s->k8 * m[x] / 255;
                uint16_t v = kk == 255 ? g[x] : omni_blend565(0x0000, g[x], kk);
                const uint8_t b = row[x];
                if (b) v = omni_blend565(v, k->mark[b].rgb, k->mark[b].a);
                d[x] = v;
            }
        }
        /* the chime: the graticule pulses outward with the thrum (its phosphorDim run, inside the glass) */
        if (s->pulse8 && y >= RD_SC_GLASS.y0 && y < RD_SC_GLASS.y1) {
            const int xa = clip.x0 > RD_SC_GLASS.x0 ? clip.x0 : RD_SC_GLASS.x0;
            const int xb = clip.x1 < RD_SC_GLASS.x1 ? clip.x1 : RD_SC_GLASS.x1;
            const float dy = (float)y + 0.5f - (float)C;
            for (int x = xa; x < xb; x++) {
                const uint8_t b = row[x];
                if (b < PHOS_RUN0) continue;
                const float dx = (float)x + 0.5f - (float)C, r2 = dx * dx + dy * dy;
                if (r2 >= GR * GR) continue;
                const float w = pk * clamp01((front - sqrt_f(r2)) / 20.f);
                if (!(w > 0)) continue;
                const uint16_t lit = omni_blend565(k->mark[b].rgb, RD_C_PHOS, a8_of(0.7f * w));
                /* the pixel under the mark: the giant's (undo nothing: redraw it) or black */
                uint16_t under = 0x0000;
                const int jj = y - GY0 - s->rise;
                if (y >= MY0 && y < MY1 && jj >= 0 && jj < GH && x >= MX0 && x < MX1 && s->k8) {
                    const uint8_t mm = k->gmask[y - MY0][x - MX0];
                    const uint16_t gg = k->giant.rgb[(size_t)jj * GW + (x - GX0)];
                    const int kk = mm == 255 ? s->k8 : s->k8 * mm / 255;
                    if (mm && gg) under = kk == 255 ? gg : omni_blend565(0x0000, gg, kk);
                }
                d[x] = omni_blend565(under, lit, k->mark[b].a);
            }
        }
    }
}

/* fb, a frame at clip's origin, moved to the origin of sub (inside clip) */
static uint16_t *at_sub(uint16_t *fb, int stride, omni_rect_t clip, omni_rect_t sub)
{
    return fb + (size_t)(sub.y0 - clip.y0) * (size_t)stride + (sub.x0 - clip.x0);
}

/* a rect [x0, x1) x [y0, y1) in rgb at a */
static void rect_fill(uint16_t *fb, int stride, omni_rect_t clip, float x0, float y0, float x1, float y1, uint16_t rgb,
                      uint8_t a)
{
    const omni_pt_t p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    omni_fill_poly(fb, stride, &clip, p, 4, rgb, a);
}

/* a segment for the distance tests: its start, its vector and 1 / its squared length (0 for a point) */
typedef struct { float ax, ay, vx, vy, inv; } seg_t;

static seg_t seg_of(float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay, vv = vx * vx + vy * vy;
    return (seg_t){ax, ay, vx, vy, vv > 0 ? 1.f / vv : 0.f};
}

static float seg_q(const seg_t *g, float px, float py)   /* the squared distance of (px, py) from the segment */
{
    const float wx = px - g->ax, wy = py - g->ay, t = clamp01((wx * g->vx + wy * g->vy) * g->inv);
    const float dx = wx - t * g->vx, dy = wy - t * g->vy;
    return dx * dx + dy * dy;
}

/* the columns [*x0, *x1] of row py that can lie within R of the segment (false: none): its points within R of the
 * row, widened by R */
static bool seg_span(const seg_t *g, float py, float R, float *x0, float *x1)
{
    float ta = 0, tb = 1;
    const float avy = g->vy < 0 ? -g->vy : g->vy;
    if (avy < 1e-6f) {
        const float dy = g->ay - py;
        if (dy > R || dy < -R) return false;
    } else {
        ta = (py - R - g->ay) / g->vy, tb = (py + R - g->ay) / g->vy;
        if (ta > tb) {
            const float q = ta;
            ta = tb, tb = q;
        }
        if (tb < 0 || ta > 1) return false;
        ta = clamp01(ta), tb = clamp01(tb);
    }
    const float xa = g->ax + ta * g->vx, xb = g->ax + tb * g->vx;
    *x0 = (xa < xb ? xa : xb) - R, *x1 = (xa > xb ? xa : xb) + R;
    return true;
}

static int sh_at(const uint8_t *tab, int n, float q)   /* a shadow table at the squared distance q, linear between */
{
    if (!(q > 0)) return tab[0];
    const int i = (int)q;
    if (i >= n - 1) return 0;
    return tab[i] + (int)((float)(tab[i + 1] - tab[i]) * (q - (float)i));
}

/* cursor(ctx, ang, len, w): the shaft from r 12 and the open chevron at the tip; shadow (sh) then the w + 3 phosphorDim
 * .9 stroke, then the w phosphor core */
static void cursor_draw(uint16_t *fb, int stride, omni_rect_t clip, float turn, float len, float w, const uint8_t *sh,
                        int shn)
{
    const float a = angle_of(turn);
    float s, c;
    fk_sincos(a, &s, &c);
    const float tx = (float)C + c * len, ty = (float)C + s * len;
    const float sx = (float)C + c * 12.f, sy = (float)C + s * 12.f;
    float s1, c1, s2, c2;
    fk_sincos(a + PI_F - 0.6f, &s1, &c1);
    fk_sincos(a + PI_F + 0.6f, &s2, &c2);
    const seg_t g[3] = {seg_of(sx, sy, tx, ty), seg_of(tx + c1 * 14.f, ty + s1 * 14.f, tx, ty),
                        seg_of(tx + c2 * 14.f, ty + s2 * 14.f, tx, ty)};
    const float reach = sqrt_f((float)(shn - 1)) + 1.f, qmax = (float)(shn - 1);
    const float hg = (w + 3.f) / 2.f, hc = w / 2.f, qg = (hg + 0.5f) * (hg + 0.5f);
    for (int y = clip.y0; y < clip.y1; y++) {
        const float py = (float)y + 0.5f;
        float lo = 1e9f, hi = -1e9f;   /* the row's columns within reach of a segment */
        for (int i = 0; i < 3; i++) {
            float a0, a1;
            if (!seg_span(&g[i], py, reach, &a0, &a1)) continue;
            lo = a0 < lo ? a0 : lo, hi = a1 > hi ? a1 : hi;
        }
        if (lo > hi) continue;
        const int xa = lo - 1.f > (float)clip.x0 ? (int)(lo - 1.f) : clip.x0;
        const int xb = hi + 2.f < (float)clip.x1 ? (int)(hi + 2.f) : clip.x1;
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = xa; x < xb; x++) {
            const float px = (float)x + 0.5f;
            float q = seg_q(&g[0], px, py);
            const float q1 = seg_q(&g[1], px, py), q2 = seg_q(&g[2], px, py);
            q = q1 < q ? q1 : q;
            q = q2 < q ? q2 : q;
            if (q >= qmax) continue;
            const int sa = sh_at(sh, shn, q) * 230 / 255;   /* the shadow at .9 */
            if (sa) d[x] = omni_blend565(d[x], RD_C_PHOS_DIM, sa);
            if (q >= qg) continue;
            const float dist = sqrt_f(q);
            const float cg = clamp01(hg + 0.5f - dist), cc = clamp01(hc + 0.5f - dist);
            if (cg > 0) d[x] = omni_blend565(d[x], RD_C_PHOS_DIM, (int)(cg * 229.5f + 0.5f));
            if (cc > 0) d[x] = omni_blend565(d[x], RD_C_PHOS, (int)(cc * 255.f + 0.5f));
        }
    }
}

/* everything inside the glass over the base (fb at gc's origin, gc inside RD_SC_GLASS) */
static void glass_draw(const sc_t *k, const sc_state_t *s, uint16_t *fb, int stride, omni_rect_t gc)
{
    if (s->warm) {   /* the warm-up: black from rr out to r 151 (the mockup's evenodd), then the ring at rr */
        const uint16_t black = 0x0000;
        const fk_grad_t g = {0, 0, 0, 0, 0, &black, 1, NULL};
        fk_ring_fill(fb, stride, &gc, (float)C, (float)C, s->rr, GR + 1.f, &g, 255, false);
        fk_ring(fb, stride, &gc, (float)C, (float)C, s->rr > 1.f ? s->rr : 1.f, 2.f, RD_C_PHOS, s->ring_a);
    }
    if (s->sweep_on) {
        const float a = angle_of(s->turn), rs = GR - 1.f;
        if (s->fan)
            for (int i = 0; i < FAN_N; i++) {   /* the afterglow: wedge i from a - (i + 1) .08 to a - i .08 */
                omni_pt_t p[4];
                p[0] = (omni_pt_t){(float)C, (float)C};
                for (int e = 0; e < 3; e++) {
                    float sn, cs;
                    fk_sincos(a - ((float)i + (float)(2 - e) * 0.5f) * FAN_W, &sn, &cs);
                    p[1 + e] = (omni_pt_t){(float)C + cs * rs, (float)C + sn * rs};
                }
                omni_fill_poly(fb, stride, &gc, p, 4, RD_C_PHOS, a8_of(0.09f * (1.f - (float)i / (float)FAN_N)));
            }
        float sn, cs;
        fk_sincos(a, &sn, &cs);
        fk_line(fb, stride, &gc, (float)C, (float)C, (float)C + cs * rs, (float)C + sn * rs, 1.6f, RD_C_PHOS,
                a8_of(0.85f));
    }
    for (int i = 0; i < 4; i++) {
        const float L = s->lvl[i];
        fk_disc(fb, stride, &gc, k->con_x[i], k->con_y[i], 2.4f + 1.4f * L, RD_C_PHOS, a8_of(0.15f + 0.8f * L));
        if (i == TAG_CONTACT)   /* fillText(tag, x + 8, y - 9), 'left' on the 'middle' line */
            fk_atlas_text_rgb(fb, stride, gc, &k->vt20.core, "TAKEAWAY?", (int)(k->con_x[i] + 8.f + 0.5f),
                              (int)(k->con_y[i] - 3.f + 0.5f), 0, RD_C_PHOS, a8_of(0.3f + 0.6f * L), 0);
    }
    if (s->stale) {
        rd_stale_mark(fb, stride, &gc, &k->ch13, STALE_CX, STALE_CY, false);
    } else if (s->a1) {
        fk_atlas_text_tracked(fb, stride, gc, &k->vt20.glow, s->l1, (float)C, Y_L1, 0.f, true, RD_C_PHOS_DIM, s->a1);
        fk_atlas_text_tracked(fb, stride, gc, &k->vt20.core, s->l1, (float)C, Y_L1, 0.f, true, RD_C_PHOS, s->a1);
    }
    if (s->a2) {
        fk_atlas_text_tracked(fb, stride, gc, &k->vt28.glow, s->l2, (float)C, Y_L2, VT_TRACK, true, RD_C_PHOS_DIM,
                              s->a2);
        fk_atlas_text_tracked(fb, stride, gc, &k->vt28.core, s->l2, (float)C, Y_L2, VT_TRACK, true, RD_C_PHOS, s->a2);
    }
    if (s->set) {
        if (s->grow > 0) {
            cursor_draw(fb, stride, gc, s->ha, 88.f * s->grow, 4.5f, SH_HOUR, (int)sizeof SH_HOUR);
            cursor_draw(fb, stride, gc, s->ma, 138.f * s->grow, 2.8f, SH_MIN, (int)sizeof SH_MIN);
        }
    } else {
        fk_atlas_text_tracked(fb, stride, gc, &k->vt28.core, "--:--", (float)C, Y_HUB, VT_TRACK, true, RD_C_PAINT_DIM,
                              255);
    }
    fk_disc(fb, stride, &gc, (float)C, (float)C, 9.f, 0x0000, 255);
    rect_fill(fb, stride, gc, (float)C - 5.f, (float)C - 2.5f, (float)C + 5.f, (float)C + 2.5f, RD_C_PHOS, 255);
    rect_fill(fb, stride, gc, (float)C - 8.f, (float)C - 1.f, (float)C - 6.f, (float)C + 1.f, RD_C_PHOS, 255);
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const sc_t *k = ctx;
    sc_state_t s;
    state_of(in, &s);
    base_draw(k, &s, fb, stride, clip);
    if (!meets(clip, RD_SC_GLASS)) return;
    const omni_rect_t gc = cut(clip, RD_SC_GLASS);
    glass_draw(k, &s, at_sub(fb, stride, clip, gc), stride, gc);
}

/* a capsule (x0, y0)-(x1, y1) of width w, round caps, in the glance's ink (each pixel the larger per channel) */
static void capsule_max(uint16_t *fb, int stride, omni_rect_t clip, float x0, float y0, float x1, float y1, float w,
                        uint16_t rgb)
{
    const float h = w / 2.f + 1.f;
    const seg_t sg = seg_of(x0, y0, x1, y1);
    omni_rect_t bb = {(int16_t)((x0 < x1 ? x0 : x1) - h), (int16_t)((y0 < y1 ? y0 : y1) - h),
                      (int16_t)((x0 > x1 ? x0 : x1) + h + 1.f), (int16_t)((y0 > y1 ? y0 : y1) + h + 1.f)};
    bb = cut(bb, clip);
    for (int y = bb.y0; y < bb.y1; y++) {
        uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
        for (int x = bb.x0; x < bb.x1; x++) {
            const float q = seg_q(&sg, (float)x + 0.5f, (float)y + 0.5f);
            if (q >= h * h) continue;
            const float cov = clamp01(w / 2.f + 0.5f - sqrt_f(q));
            if (!(cov > 0)) continue;
            const uint16_t ink = omni_blend565(0x0000, rgb, (int)(cov * 255.f + 0.5f));
            const uint16_t o = d[x];
            const int r = omni_ex_r(o) > omni_ex_r(ink) ? omni_ex_r(o) : omni_ex_r(ink);
            const int g = omni_ex_g(o) > omni_ex_g(ink) ? omni_ex_g(o) : omni_ex_g(ink);
            const int b = omni_ex_b(o) > omni_ex_b(ink) ? omni_ex_b(o) : omni_ex_b(ink);
            d[x] = omni_pack565(r, g, b);
        }
    }
}

/* sleepFrame(): the glass rim, the giant's limb as one arc inside the glass, 12, 3, 6 and 9, the cursors as thin lines
 * (or --:--), the stale marker; all through the orbit */
static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const sc_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const float cx = (float)(C + in->odx), cy = (float)(C + in->ody);
    const bool set = in->clk.set, stale = set && in->clk.stale;
    fk_ring_max(fb, stride, &clip, cx, cy, GR + 3.f, 1.2f, RD_G_STEEL, 255);
    /* the limb: r 120 about GIANT (262, 418), 1.5 px, clipped to the glass (each pixel's coverage of both) */
    {
        const float lx = 262.f + (float)in->odx, ly = 418.f + (float)in->ody;
        omni_rect_t bb = {(int16_t)(cx - GR - 1.f), (int16_t)(ly - 122.f), (int16_t)(cx + GR + 2.f),
                          (int16_t)(cy + GR + 2.f)};
        bb = cut(bb, clip);
        for (int y = bb.y0; y < bb.y1; y++) {
            uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride - clip.x0;
            const float py = (float)y + 0.5f, gy = py - cy, my = py - ly;
            for (int x = bb.x0; x < bb.x1; x++) {
                const float px = (float)x + 0.5f, gx = px - cx, mx = px - lx;
                const float g2 = gx * gx + gy * gy;
                if (g2 >= (GR + 0.5f) * (GR + 0.5f)) continue;
                const float m2 = mx * mx + my * my;
                if (m2 <= 118.f * 118.f || m2 >= 122.f * 122.f) continue;
                const float dm = sqrt_f(m2), cl = clamp01(0.75f + 0.5f - (dm > 120.f ? dm - 120.f : 120.f - dm));
                const float cg = clamp01(GR + 0.5f - sqrt_f(g2)), cov = cl * cg;
                if (!(cov > 0)) continue;
                const uint16_t ink = omni_blend565(0x0000, RD_G_HULL, (int)(cov * 255.f + 0.5f));
                const uint16_t o = d[x];
                const int r = omni_ex_r(o) > omni_ex_r(ink) ? omni_ex_r(o) : omni_ex_r(ink);
                const int g = omni_ex_g(o) > omni_ex_g(ink) ? omni_ex_g(o) : omni_ex_g(ink);
                const int b = omni_ex_b(o) > omni_ex_b(ink) ? omni_ex_b(o) : omni_ex_b(ink);
                d[x] = omni_pack565(r, g, b);
            }
        }
    }
    static const struct { const char *t; float x, y; } NUM[4] = {{"12", 0, -196}, {"3", 196, 0}, {"6", 0, 196},
                                                                 {"9", -196, 0}};
    for (int i = 0; i < 4; i++)
        fk_atlas_text_tracked(fb, stride, clip, &k->s30, NUM[i].t, cx + NUM[i].x, (int)(cy + NUM[i].y + S30_DY + 0.5f),
                              0.f, true, RD_G_PAINT, 255);
    if (set) {
        float ht, mt, s, c;
        rd_sc_hand_turns(in, &ht, &mt);
        fk_sincos(angle_of(ht), &s, &c);
        capsule_max(fb, stride, clip, cx, cy, cx + c * 88.f, cy + s * 88.f, 3.f, RD_G_PHOS);
        fk_sincos(angle_of(mt), &s, &c);
        capsule_max(fb, stride, clip, cx, cy, cx + c * 138.f, cy + s * 138.f, 2.f, RD_G_PHOS);
    } else {
        fk_atlas_text_tracked(fb, stride, clip, &k->vt28.core, "--:--", cx, Y_HUB + in->ody, VT_TRACK, true, RD_G_STEEL,
                              255);
    }
    if (stale) rd_stale_mark(fb, stride, &clip, &k->ch13, STALE_CX + in->odx, STALE_CY + in->ody, true);
}

/* ---- dirty(): the glass (or the readout alone) when what it shows changed; the dial outside it never moves */
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
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody &&
                          (!a->clk.set || (a->clk.h % 12 == b->clk.h % 12 && a->clk.m == b->clk.m));
        return same ? 0 : whole(out);
    }
    sc_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    const size_t ro = offsetof(sc_state_t, l1);
    if (memcmp(&sa, &sb, ro) != 0) {
        out[0] = RD_SC_GLASS;
        return 1;
    }
    if (memcmp((const char *)&sa + ro, (const char *)&sb + ro, sizeof sa - ro) != 0) {
        out[0] = RD_SC_READOUT;
        return 1;
    }
    return 0;
}

/* ---- taps: anywhere on the face turns the readout's page (RAM only), and its new page fades in */
static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= 0 && x < W && y >= 0 && y < W ? FK_TAP_PULSE : FK_TAP_NONE;
}

/* unset, the readout shows its caption whatever the page: the tap leaves the variant and starts nothing. A NULL in
 * (only from outside the runtime, fk/face.h) counts as a set clock */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    const fk_tap_t t = tap(ctx, x, y);
    if (t == FK_TAP_NONE) return t;
    if (in != NULL && !in->clk.set) return FK_TAP_NONE;
    *variant = (uint16_t)((*variant + 1u) % RD_SC_PAGES);
    return t;
}

const fk_face_ops_t RD_FACE_SCOPE = {
    .key = "scope",
    .ctx_bytes = sizeof(sc_t),
    .cadence_ms = 70,       /* spec section 18: within 66-80 ms (12-15 fps on the glass) */
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = WAKE_MS,
    .pulse_len_ms = PULSE_MS,   /* the new page's fade */
    .tap_var = tap_var,
    .stills_extra = 1,      /* still 11: NEAREST TAKEAWAY (RD_SC_TAPPED_VARIANT) */
};
