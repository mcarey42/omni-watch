/* Vending Machine (`vending`, rd/faces.h; rd wave 2 Task 8, spec section 10.9; faces-quarters.js:183-345: buildBg(),
 * drawFlap(), item(), digits(), frame(), pointer() and sleepFrame()).
 *
 * The base: the front (rd_vm_front, the mockup's buildBg() canvas cut to (61, 41, 340 x 390): the fascia under it, the
 * lightbox's box, the display's bezel and window, the six buttons and their names, the coin slot and COIN, the tray's
 * surround and well, three of the four rivets), and outside it the shared rd_grime tiled and tinted hull-dark over the
 * fascia's #120807 (the notes' "fascia ground is the shared grime tinted"): each pixel #120807 toward hullDeep by a
 * quarter of the grime's alpha. The canvas's ink runs one row and four columns past the crop, so the face draws those
 * itself: the tray surround's last row (y 431: the rrect (156, 382) 154 x 50 r 7 in #2a1d19) and the rivet at (402,
 * 300)'s columns 401-404 (rivet(): the steel disc r 3 and its black pixel at (403, 301)). Over the base, in frame()'s
 * order:
 *   the lightbox: the rrect (112, 46) 242 x 26 r 4 in rd.hull at .55 lit (the chime's thrum swells it to .55 + .3
 *   thrum), lit 1 at rest, the wake's stutter ([0,1,0,0,1,0,1,1] of 65 ms steps to 520 ms: .9 or .08) and the buzz
 *   (rd_vm_buzz: .45 in the 30 ms slots the mockup's 4 000-4 120 ms window flickers in); then its legend DRINKS .
 *   SNACKS . ??? (Chakra 13, tracked 1.8, centred on 233, its 'middle' 60 on the baseline 63) in #ffd6be at .85 lit, or
 *   the stale marker in its place (staleMark(233, 59): the quarters marker, centred);
 *   the display (digits()): the four seven-segment digits (segPath(): w 42, h 80, th 10, gap 1.6) at x 112, 168, 248,
 *   304 and y 106 and the colon's two 10 px squares at x 229 (y 106 + 80 x .28 and .66), all under the mockup's
 *   transform(1, 0, -.07, 1, 15, 0) (x' = x - .07 y + 15). Each digit in turn: its ghost 8 in #1d1206, then its lit
 *   segments in rd.amber; each fill casts the canvas's shadow (shadowBlur 14: a Gaussian of sigma 7) in rgba(255, 150,
 *   30, .55), so the ghost's glow is under every digit and the lit segments' glow over it. The blurred coverage of each
 *   segment and of the colon is built at bind (radius 21, integer weights); a pass lays the glow, the next the fill, in
 *   the canvas's order. The chime's thrum raises the glow to .55 + .35 thrum;
 *   the ticker (phos(): VT323 28, the glow plane then the core, inside the clip (104, 196) 258 x 26, the 'middle' 209
 *   on the baseline 217): the machine's opinions scrolling left at 45 px/s from the clock's ms of the day (mono_ms's
 *   while unset), two copies a message's width apart, so it wraps seamlessly; a tap's message (centred, at 1) for 2.6
 *   s; under reduced motion a still line (ALL ITEMS SOLD OUT, unset AWAITING TIME SIGNAL) at .8; blank in the wake to
 *   650 ms;
 *   the buttons' lamps: each its 72 x 16 r 2 lamp in its colour at .14 a and its legend (Chakra 13, centred) at a: SOLD
 *   OUT in rd.lamp, MYSTERY's ? ? ? in rd.amber; MYSTERY blinks (a 1 while sin(t / 260) > -.2, else .3, t the clock's
 *   ms of the day: 870 to 1 581 ms of each 1 634 ms are its dark part), .25 unset, 1 under reduced motion, .3 + .7 beep
 *   on the chime's notes; a pressed sold-out button flashes (.25 or 1 by 120 ms) for 1.2 s (not reduced); every button
 *   is .08 to 250 ms of the wake; a pressed button is darkened (black .45 over its r 5 rrect) for 160 ms;
 *   the TODAY sticker: the rrect (-108, -18) 172 x 38 r 2 in #857a66 about (225, 362), turned -.035 rad, with TODAY and
 *   the date (12 h: with its AM/PM suffix, and the sticker 208 wide to hold it) in #1b1510 and SERVICED: NEVER in
 *   #3a2a5a at .9, each pixel sampled from the turned rect and glyphs (bilinear, from the Chakra 13 atlas);
 *   the tray: after a MYSTERY tap (set), the flap opens over 250 ms (easeOut3), holds, and closes over 3 200-3 600 ms
 *   (at once under reduced motion: no swing); while open, the amber glow (the radial (233, 418) r 2 to 80, .25 to 0)
 *   over the well, the item's drawing and its two lines at clamp(open x 1.4) (the second line at 1: the mockup's lab()
 *   resets globalAlpha after the first); then the flap (drawFlap(): 38 cos(1.25 open) tall, its #4a3f39 to #2b2420
 *   gradient, PUSH in paintDim while over 16 px) unless it is open past .98. The wake's empty drop opens it to .25 sin
 *   over 880-1 200 ms.
 * The glance (sleepFrame()): the four digits (w 38, h 66, th 5) as filled segments in rd.g.amber through the orbit and
 * the same transform, the colon's 5 px squares, the MYSTERY dot (r 3 at (331, 286)) and the stale marker
 * (staleMark(233, 70)). The variant (RAM only): bits 0-2 the last pressed button, bits 3-5 MYSTERY's next item, bit 6
 * flipped by every press (so a press always moves it). Pure: no esp_*, freertos or LVGL; no libm (square roots by
 * Newton's steps, the flap's sine through fk_sincos); no allocation; every array a draw needs over 256 B is in the
 * context, written at bind, but the flap's rrect outline (omni_rrect_t, 512 B on the stack: its height moves). */
#include <string.h>
#include "rd/faces.h"
#include "rd/seg7.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "fk/compose.h"
#include "fk/ease.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "omni/raster.h"

#define W 466
#define FX 61      /* rd_vm_front's crop */
#define FY 41
#define FW 340
#define FH 390
#define GR_N 256   /* rd_grime's tile */
/* the display: digits() at DX, DY (cells 56, the colon's 24), the transform x' = x - .07 y + 15 */
#define DX 112
#define DY 106
#define DCW 42.f
#define DCH 80.f
#define DTH 10.f
#define GAP 1.6f
#define SKEW .07f
#define SKX 15.f
#define CELL 56
#define COLW 24
/* the glow maps: a digit's map from (x0 - 25, 79), 104 x 136 (the digit's skewed box and the blur's radius 21) */
#define GRAD 21
#define MXO 25
#define MY0 79
#define MW 104
#define MH 136
#define CMX 209    /* the colon's map, screen */
#define CMY 105
#define CMW 60
#define CMH 88
/* the glance's digits (sleepFrame()): w 38, h 66, th 5, cells 54, the colon's 20, at (127, 120) */
#define GX0 127
#define GY0 120
#define GCW 38.f
#define GCH 66.f
#define GTH 5.f
#define GCELL 54
#define GCOLW 20
#define TRACK 1.8f      /* lab(ctx, .., 13): (13 x .14).toFixed(1) px */
#define VT_ADV 11.2f    /* VT323 28's advance */
#define TK_X0 104       /* the ticker's clip (WIN.x + 8, 196, WIN.w - 16, 26) */
#define TK_Y0 196
#define TK_X1 362
#define TK_Y1 222
#define TK_BASE 217     /* 209 middle + 8.4 */
#define TK_MPX_MS 45    /* 0.045 px a ms, in 1/1000 px */
#define Y_LABEL 63      /* the lightbox's legend: 60 middle + 3.4 */
#define STALE_X 233     /* staleMark(233, 59) */
#define STALE_CY 63     /* the shared marker's ring at cy - 4.5 (the mockup's 59) and STALE on 63 (60 middle + 3.4) */
#define GSTALE_CY 74    /* the glance's staleMark(233, 70): STALE on 74 */
#define WAKE_MS 1200
#define VEND_MS 3600
#define MSG_MS 2600
#define TRAY_X 162      /* TRAY (162, 388) 142 x 38 */
#define TRAY_Y 388
#define TRAY_W 142
#define TRAY_H 38
#define ST_CX 225.f     /* the sticker: translate(225, 362) rotate(-.035) */
#define ST_CY 362.f
#define ST_COS 0.99938756f
#define ST_SIN (-0.03499285f)
#define PI_F 3.14159265359f
#define BLINK_P 1634    /* 2 pi 260 ms, rounded */
#define BLINK_OFF0 870  /* sin(t / 260) <= -.2: from (pi + asin .2) to (2 pi - asin .2) of the period */
#define BLINK_OFF1 1582

#define C_FASCIA RD_RGB(0x12, 0x08, 0x07)
#define C_GHOST RD_RGB(0x1D, 0x12, 0x06)
#define C_GLOW RD_RGB(255, 150, 30)
#define C_LEGEND RD_RGB(255, 214, 190)
#define C_STICKER RD_RGB(0x85, 0x7A, 0x66)
#define C_INK RD_RGB(0x1B, 0x15, 0x10)
#define C_PURPLE RD_RGB(0x3A, 0x2A, 0x5A)
#define C_SURROUND RD_RGB(0x2A, 0x1D, 0x19)
#define C_FLAP0 RD_RGB(0x4A, 0x3F, 0x39)
#define C_FLAP1 RD_RGB(0x2B, 0x24, 0x20)

/* the buttons (btnRect(i)): (101 + 90 (i % 3), 238 + 54 (i / 3)) 84 x 48 */
#define BTN_X(i) (101 + 90 * ((i) % 3))
#define BTN_Y(i) (238 + 54 * ((i) / 3))
#define BTN_W 84
#define BTN_H 48
const omni_rect_t RD_VM_BTN[RD_VM_BTN_N] = {
    {101, 238, 185, 286}, {191, 238, 275, 286}, {281, 238, 365, 286},
    {101, 292, 185, 340}, {191, 292, 275, 340}, {281, 292, 365, 340},
};
const omni_rect_t RD_VM_LIGHTBOX = {108, 42, 358, 76};
const omni_rect_t RD_VM_STALE = {112, 46, 354, 72};
const omni_rect_t RD_VM_TIME = {DX - MXO, MY0, DX + 3 * CELL + COLW - MXO + MW, MY0 + MH};
const omni_rect_t RD_VM_COLON = {CMX, CMY, CMX + CMW, CMY + CMH};
const omni_rect_t RD_VM_TICKER = {TK_X0, TK_Y0, TK_X1, TK_Y1};
const omni_rect_t RD_VM_MYSTERY = {287, 318, 359, 334};
const omni_rect_t RD_VM_FLAP = {TRAY_X, TRAY_Y, TRAY_X + TRAY_W, TRAY_Y + TRAY_H};
const omni_rect_t RD_VM_STICKER = {115, 339, 327, 388};
const omni_rect_t RD_VM_PANEL = {101, 196, 365, 340};

static const char *const BTN_NAME[RD_VM_BTN_N] = {"COFFEE", "TEA", "SOUP", "CRISPS", "CURRY", "MYSTERY"};
static const char *const ITEM_L1[RD_VM_ITEMS] = {"ONE SOCK", "SPANNER", "ONE PEA", "A FISH", "AN IOU", "BATTERY"};
static const char *const ITEM_L2[RD_VM_ITEMS] = {"(WORN)", "13 MM", "QTY 1", "(?)", "FOR 1 TEA", "(FLAT)"};
static const char *const ITEM_SHORT[RD_VM_ITEMS] = {"SOCK", "SPANNER", "PEA", "FISH", "IOU", "BATTERY"};
#define MSG_SET \
    "INSERT COIN  \267  EXACT CHANGE ONLY  \267  NO REFUNDS  \267  ALL ITEMS SOLD OUT  \267  HAVE A NICE SHIFT  \267  "
#define MSG_UNSET "AWAITING TIME SIGNAL  \267  PRICES MAY VARY  \267  "
#define LEGEND "DRINKS \267 SNACKS \267 ???"
#define NO_DATE "--- -- ---"

/* exp(-i^2 / 98) (a Gaussian of sigma 7) over i = 0 .. 21, in 4 096ths of the kernel's sum */
static const uint16_t GAUSS[GRAD + 1] = {230, 232, 225, 213, 199, 181, 162, 142, 122, 102, 84,
                                         68,  54,  42,  32,  24,  17,  12,  9,   6,   4,  3};

typedef struct {
    fk_img_t front, grime, ch13_img;
    fk_atlas_t ch13;
    rd_atlas2_t vt28;   /* used where filled, never copied */
    int16_t stale_cx, gstale_cx;
    int16_t vt_adv;     /* the VT323 atlas's own advance (11), its tracking to 11.2 */
    uint8_t glow[7][MH][MW];   /* each segment's blurred coverage, the digit's map */
    uint8_t ghost[MH][MW];     /* the seven's sum (the ghost 8's glow), saturated */
    uint8_t cglow[CMH][CMW];   /* the colon's two squares' blurred coverage */
    omni_rrect_t lbox, surround, lamp[RD_VM_BTN_N], press[RD_VM_BTN_N];
} vm_t;

/* ---- small helpers */
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static uint8_t a8_of(float a) { return fk_ease_to8(a); }
static bool meets(omni_rect_t a, omni_rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static omni_rect_t cut(omni_rect_t a, omni_rect_t b)
{
    omni_rect_t r = {a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1,
                     a.y1 < b.y1 ? a.y1 : b.y1};
    return r;
}
static uint16_t *at(uint16_t *fb, int stride, omni_rect_t clip, int x, int y)
{
    return fb + (size_t)(y - clip.y0) * (size_t)stride + (x - clip.x0);
}
/* sqrt(v) for v >= 0 by Newton's steps from the float's halved exponent (no libm) */
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
static long ms_of_day(const fk_clock_t *c) { return (((long)c->h * 60 + c->m) * 60 + c->s) * 1000L + c->ms; }
static bool waking(const fk_face_in_t *in)
{
    return !in->reduced && in->wake_ms != UINT32_MAX && in->wake_ms < WAKE_MS;
}
static bool chiming(const fk_face_in_t *in) { return in->clk.set && !in->reduced && !in->glance; }
/* the pressed button while its pulse plays (-1 none) */
static int pressed(const fk_face_in_t *in)
{
    if (!in->pulsing) return -1;
    const int b = in->variant & 7;
    return b < RD_VM_BTN_N ? b : -1;
}

/* ---- the face's readings */
bool rd_vm_buzz(const fk_clock_t *c)
{
    if (!c->set) return false;
    const long sod = ((long)c->h * 60 + c->m) * 60 + c->s;
    return sod % 9 == 0 && c->ms < 100;
}

float rd_vm_lit(const fk_face_in_t *in)
{
    if (waking(in) && in->wake_ms < 520) {
        static const uint8_t STUTTER[8] = {0, 1, 0, 0, 1, 0, 1, 1};
        return STUTTER[in->wake_ms / 65] ? 0.9f : 0.08f;
    }
    if (!in->reduced && rd_vm_buzz(&in->clk) && ((4000 + in->clk.ms) / 30) % 2) return 0.45f;
    return 1.f;
}

int rd_vm_item(const fk_face_in_t *in)
{
    if (pressed(in) != RD_VM_MYSTERY_BTN || !in->clk.set || in->pulse_ms >= VEND_MS) return -1;
    return (int)((((in->variant >> 3) & 7) % RD_VM_ITEMS + RD_VM_ITEMS - 1) % RD_VM_ITEMS);
}

float rd_vm_flap_open(const fk_face_in_t *in)
{
    if (rd_vm_item(in) >= 0) {
        const float vt = (float)in->pulse_ms;
        if (in->reduced) return 1.f;
        if (vt < 250.f) return fk_ease_out_cubic(vt / 250.f);
        if (vt > 3200.f) return 1.f - (vt - 3200.f) / 400.f;
        return 1.f;
    }
    if (waking(in) && in->wake_ms > 880) {
        float s, c;
        fk_sincos((float)(in->wake_ms - 880) / 370.f * PI_F, &s, &c);
        return s * 0.25f;
    }
    return 0.f;
}

float rd_vm_lamp(const fk_face_in_t *in, int i)
{
    if (waking(in) && in->wake_ms < 250) return 0.08f;
    const int p = pressed(in);
    if (i == RD_VM_MYSTERY_BTN) {
        if (!in->clk.set) return 0.25f;
        if (chiming(in) && in->beep > 0) return 0.3f + 0.7f * clamp01(in->beep);
        if (in->reduced) return 1.f;
        const long ph = ms_of_day(&in->clk) % BLINK_P;
        return ph >= BLINK_OFF0 && ph < BLINK_OFF1 ? 0.3f : 1.f;
    }
    if (p == i && in->pulse_ms < 1200 && !in->reduced) return (in->pulse_ms / 120) % 2 ? 0.25f : 1.f;
    return 1.f;
}

void rd_vm_digits(const fk_face_in_t *in, char out[5])
{
    if (waking(in) && in->wake_ms < 250) {
        memcpy(out, "    ", 5);
        return;
    }
    if (waking(in) && in->wake_ms < 650) {
        memcpy(out, "8888", 5);
        return;
    }
    rd_seg7_str(in, out);
}

bool rd_vm_colon(const fk_face_in_t *in)
{
    if (waking(in) && in->wake_ms < 650) return in->wake_ms >= 250;
    return in->reduced || !in->clk.set || in->clk.ms < 600;
}

int rd_vm_ticker(const fk_face_in_t *in, char out[48], uint8_t *alpha)
{
    const int p = pressed(in);
    *alpha = 255;
    if (p >= 0 && in->pulse_ms < MSG_MS) {
        if (p != RD_VM_MYSTERY_BTN) {
            strcpy(out, BTN_NAME[p]);
            strcat(out, ": SOLD OUT");
        } else if (!in->clk.set) {
            strcpy(out, "PLEASE WAIT");
        } else {
            strcpy(out, "ENJOY YOUR ");
            strcat(out, ITEM_SHORT[rd_vm_item(in)]);
        }
        return RD_VM_TK_MSG;
    }
    *alpha = 204;
    if (in->reduced) {
        strcpy(out, in->clk.set ? "ALL ITEMS SOLD OUT" : "AWAITING TIME SIGNAL");
        return RD_VM_TK_STILL;
    }
    if (waking(in) && in->wake_ms <= 650) {
        out[0] = 0;
        return RD_VM_TK_NONE;
    }
    out[0] = 0;   /* the scroll draws MSG_SET or MSG_UNSET (rd_vm_ticker_off) */
    return RD_VM_TK_SCROLL;
}

/* the scroll's message and its width, in 1/1000 px (11 200 a character: a VT323 28 advance) */
static const char *scroll_msg(bool set) { return set ? MSG_SET : MSG_UNSET; }
static long scroll_mw(bool set) { return (long)strlen(scroll_msg(set)) * 11200L; }

float rd_vm_ticker_mw(bool set) { return (float)scroll_mw(set) / 1000.f; }

/* the scroll's offset in 1/1000 px: 45 a ms of the clock's day (mono_ms's while unset), modulo the message's width */
static long ticker_off_m(const fk_face_in_t *in)
{
    const long long t = in->clk.set ? (long long)ms_of_day(&in->clk) : (long long)in->mono_ms;
    return (long)((t * TK_MPX_MS) % (long long)scroll_mw(in->clk.set));
}

float rd_vm_ticker_off(const fk_face_in_t *in) { return (float)ticker_off_m(in) / 1000.f; }

void rd_vm_sticker_date(const fk_face_in_t *in, char out[24])
{
    if (!in->clk.set) {
        strcpy(out, NO_DATE);
        return;
    }
    fk_date_str(&in->clk, out);
    strcat(out, fk_ampm(&in->clk, in->h24));
}

unsigned rd_vm_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX])
{
    out[0] = RD_VM_TIME;
    out[1] = RD_VM_LIGHTBOX;
    out[2] = RD_VM_MYSTERY;
    return 3;
}

/* ---- the segments (segPath()): segment seg (0 a .. 6 g) of the cell at (x0, y0), w x h, thickness th, under the
 * transform x' = ox + x - .07 y + 15, y' = oy + y */
static void seg_poly(int seg, float x0, float y0, float w, float h, float th, float ox, float oy, omni_pt_t out[6])
{
    const float hw = th / 2.f, m = h / 2.f;
    float q[6][2];
    if (seg == 0 || seg == 6 || seg == 3) {   /* H(x0, x0 + w, y) */
        const float y = seg == 0 ? y0 : seg == 6 ? y0 + m : y0 + h, x1 = x0 + GAP, x2 = x0 + w - GAP;
        const float P[6][2] = {{x1, y},      {x1 + hw, y - hw}, {x2 - hw, y - hw},
                               {x2, y},      {x2 - hw, y + hw}, {x1 + hw, y + hw}};
        memcpy(q, P, sizeof q);
    } else {   /* V(x, y1, y2): b, c at x0 + w; e, f at x0 */
        const float x = (seg == 1 || seg == 2) ? x0 + w : x0;
        const bool top = seg == 1 || seg == 5;
        const float y1 = (top ? y0 : y0 + m) + GAP, y2 = (top ? y0 + m : y0 + h) - GAP;
        const float P[6][2] = {{x, y1},      {x + hw, y1 + hw}, {x + hw, y2 - hw},
                               {x, y2},      {x - hw, y2 - hw}, {x - hw, y1 + hw}};
        memcpy(q, P, sizeof q);
    }
    for (int i = 0; i < 6; i++) out[i] = (omni_pt_t){ox + q[i][0] - SKEW * q[i][1] + SKX, oy + q[i][1]};
}

/* a colon square fillRect(x, y, s, s) under the transform */
static void square_poly(float x, float y, float s, float ox, float oy, omni_pt_t out[4])
{
    const float P[4][2] = {{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}};
    for (int i = 0; i < 4; i++) out[i] = (omni_pt_t){ox + P[i][0] - SKEW * P[i][1] + SKX, oy + P[i][1]};
}

/* ---- bind */
/* a separable blur of the map (w x h, row stride w) by GAUSS, in place, rounded at each pass */
static void blur(uint8_t *m, int w, int h)
{
    uint8_t line[MH > MW ? MH : MW];
    for (int y = 0; y < h; y++) {
        memcpy(line, m + y * w, (size_t)w);
        for (int x = 0; x < w; x++) {
            uint32_t acc = 0;
            for (int k = -GRAD; k <= GRAD; k++) {
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
            for (int k = -GRAD; k <= GRAD; k++) {
                const int yy = y + k;
                if (yy >= 0 && yy < h) acc += (uint32_t)line[yy] * GAUSS[k < 0 ? -k : k];
            }
            m[y * w + x] = (uint8_t)((acc + 2048) >> 12);
        }
    }
}

static bool bind(void *ctx, fk_asset_fn get)
{
    vm_t *k = ctx;
    memset(k, 0, sizeof *k);
    size_t n = 0;
    const void *b = get(ASSET_RD_VM_FRONT, &n);
    bool ok = b && fk_img_from_bin(&k->front, b, n) && k->front.rgb && !k->front.a && k->front.w == FW &&
              k->front.h == FH;
    b = get(ASSET_RD_GRIME, &n);
    ok = ok && b && fk_img_from_bin(&k->grime, b, n) && k->grime.a && k->grime.w == GR_N && k->grime.h == GR_N;
    b = get(ASSET_RD_F_CH13, &n);
    ok = ok && b && fk_img_from_bin(&k->ch13_img, b, n) && k->ch13_img.a;
    k->ch13 = (fk_atlas_t){&k->ch13_img, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    b = get(ASSET_RD_F_VT28, &n);
    ok = ok && b && rd_atlas2_from_bin(b, n, &k->vt28);
    if (!ok) return false;
    /* the quarters marker (staleMark(x, y)): its ring at x0 + 4, STALE from x0 + 14, x0 = x - w / 2 and w STALE's
     * tracked width (the canvas counts the last glyph's spacing) + 14; the shared marker puts them at cx - 30 and
     * cx - 20 */
    const float wt = fk_atlas_width_tracked(&k->ch13, "STALE", TRACK) + TRACK + 14.f;
    k->stale_cx = (int16_t)((float)STALE_X - wt / 2.f + 34.f + 0.5f);
    k->gstale_cx = k->stale_cx;
    k->vt_adv = (int16_t)fk_atlas_width_tracked(&k->vt28.core, "0", 0.f);
    omni_rrect_prepare(&k->lbox, 112.f, 46.f, 354.f, 72.f, 4.f);
    omni_rrect_prepare(&k->surround, 156.f, 382.f, 310.f, 432.f, 7.f);
    for (int i = 0; i < RD_VM_BTN_N; i++) {
        const float x = (float)BTN_X(i), y = (float)BTN_Y(i);
        omni_rrect_prepare(&k->press[i], x, y, x + BTN_W, y + BTN_H, 5.f);
        omni_rrect_prepare(&k->lamp[i], x + 6.f, y + 26.f, x + 6.f + 72.f, y + 26.f + 16.f, 2.f);
    }
    /* the segments' glow: each one's exact coverage in the digit's map (the cell at (MXO, DY - MY0) of it), blurred */
    for (int sg = 0; sg < 7; sg++) {
        omni_pt_t p[6];
        seg_poly(sg, (float)MXO, (float)DY, DCW, DCH, DTH, 0.f, (float)-MY0, p);
        for (int y = 0; y < MH; y++) omni_cover_poly(p, 6, y, 0, MW, k->glow[sg][y]);
        blur(&k->glow[sg][0][0], MW, MH);
    }
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            int s = 0;
            for (int sg = 0; sg < 7; sg++) s += k->glow[sg][y][x];
            k->ghost[y][x] = (uint8_t)(s > 255 ? 255 : s);
        }
    /* the colon's: fillRect(px + 5, y + ch x .28, th, th) and (.., y + ch x .66), px = DX + 2 CELL */
    for (int i = 0; i < 2; i++) {
        omni_pt_t p[4];
        square_poly((float)(DX + 2 * CELL + 5), (float)DY + DCH * (i ? 0.66f : 0.28f), DTH, (float)-CMX,
                    (float)-CMY, p);
        for (int y = 0; y < CMH; y++) {
            uint8_t row[CMW];
            omni_cover_poly(p, 4, y, 0, CMW, row);
            for (int x = 0; x < CMW; x++) {
                const int v = k->cglow[y][x] + row[x];
                k->cglow[y][x] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
    }
    blur(&k->cglow[0][0], CMW, CMH);
    return true;
}

/* ---- the state a frame shows */
typedef struct {
    bool set, stale, reduced, h24;
    uint8_t lit;          /* the lightbox's level (255 x lit) */
    uint8_t lbox_a;       /* its fill's alpha */
    char dig[5];
    bool colon;
    uint8_t glow_a;       /* the digits' shadow alpha (.55, raised by the chime) */
    int tk_kind;
    char tk[48];
    uint8_t tk_a;
    long tk_off;          /* the scroll's offset, 1/1000 px */
    uint8_t lamp[RD_VM_BTN_N];
    bool dark[RD_VM_BTN_N];
    char date[24];
    int item;
    float open;
} vm_state_t;

static void state_of(const fk_face_in_t *in, vm_state_t *s)
{
    memset(s, 0, sizeof *s);
    s->set = in->clk.set;
    s->stale = s->set && in->clk.stale;
    s->reduced = in->reduced;
    s->h24 = in->h24;
    const float lit = rd_vm_lit(in);
    s->lit = a8_of(lit);
    const float th = chiming(in) ? clamp01(in->thrum) : 0.f;
    s->lbox_a = a8_of((0.55f + 0.3f * th) * lit);
    rd_vm_digits(in, s->dig);
    s->colon = rd_vm_colon(in);
    s->glow_a = a8_of(0.55f + 0.35f * th);
    s->tk_kind = rd_vm_ticker(in, s->tk, &s->tk_a);
    if (s->tk_kind == RD_VM_TK_SCROLL) s->tk_off = ticker_off_m(in);
    const int p = pressed(in);
    for (int i = 0; i < RD_VM_BTN_N; i++) {
        s->lamp[i] = a8_of(rd_vm_lamp(in, i));
        s->dark[i] = p == i && in->pulse_ms < 160;
    }
    rd_vm_sticker_date(in, s->date);
    s->item = rd_vm_item(in);
    s->open = rd_vm_flap_open(in);
}

/* ---- drawing */
/* a rounded rect prepared at bind, over box, in rgb at a (the coverage's share of it) */
static void rr_fill(const omni_rrect_t *rr, omni_rect_t box, uint16_t *fb, int stride, omni_rect_t clip, uint16_t rgb,
                    uint8_t a)
{
    const omni_rect_t c = cut(box, clip);
    if (c.x0 >= c.x1 || c.y0 >= c.y1 || !a) return;
    uint8_t cov[OMNI_COVER_W];
    for (int y = c.y0; y < c.y1; y++)
        for (int x0 = c.x0; x0 < c.x1; x0 += OMNI_COVER_W) {
            const int x1 = x0 + OMNI_COVER_W < c.x1 ? x0 + OMNI_COVER_W : c.x1;
            omni_cover_rrect_prepared(rr, y, x0, x1, cov);
            uint16_t *d = at(fb, stride, clip, x0, y);
            for (int x = x0; x < x1; x++, d++) {
                const int cv = cov[x - x0];
                if (cv) *d = omni_blend565(*d, rgb, a == 255 ? cv : (cv * a + 127) / 255);
            }
        }
}

static void base_draw(const vm_t *k, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = at(fb, stride, clip, clip.x0, y);
        const uint8_t *g = k->grime.a + (size_t)(y % GR_N) * GR_N;
        const bool rows = y >= FY && y < FY + FH;
        for (int x = clip.x0; x < clip.x1; x++, d++) {
            if (rows && x >= FX && x < FX + FW) {
                *d = k->front.rgb[(size_t)(y - FY) * FW + (size_t)(x - FX)];
                continue;
            }
            *d = omni_blend565(C_FASCIA, RD_C_HULL_DEEP, g[x % GR_N] >> 2);
        }
    }
    /* the canvas's ink past the crop: the surround's last row and the rivet's last columns */
    const omni_rect_t row = {156, FY + FH, 310, FY + FH + 1};
    rr_fill(&k->surround, row, fb, stride, clip, C_SURROUND, 255);
    const omni_rect_t riv = cut((omni_rect_t){FX + FW, 296, FX + FW + 5, 304}, clip);
    if (riv.x0 < riv.x1 && riv.y0 < riv.y1) {
        fk_disc(fb + (size_t)(riv.y0 - clip.y0) * (size_t)stride + (riv.x0 - clip.x0), stride, &riv, 402.f, 300.f, 3.f,
                RD_C_STEEL, 255);
        if (403 >= riv.x0 && 403 < riv.x1 && 301 >= riv.y0 && 301 < riv.y1) *at(fb, stride, clip, 403, 301) = 0x0000;
    }
}

static void lightbox_draw(const vm_t *k, const vm_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_VM_LIGHTBOX, clip)) return;
    rr_fill(&k->lbox, RD_VM_STALE, fb, stride, clip, RD_C_HULL, s->lbox_a);
    if (s->stale) {
        rd_stale_mark(fb, stride, &clip, &k->ch13, k->stale_cx, STALE_CY, false);
        return;
    }
    const uint8_t a = (uint8_t)((217 * s->lit + 127) / 255);   /* .85 lit */
    fk_atlas_text_tracked(fb, stride, clip, &k->ch13, LEGEND, 233.f, Y_LABEL, TRACK, true, C_LEGEND, a);
}

/* the sum of n glow maps (each w x h, at screen (mx, my)) in C_GLOW, at its value x ga / 255 */
static void glow_pass(const uint8_t *const *maps, int n, int w, int h, int mx, int my, uint8_t ga, uint16_t *fb,
                      int stride, omni_rect_t clip)
{
    const omni_rect_t c = cut((omni_rect_t){(int16_t)mx, (int16_t)my, (int16_t)(mx + w), (int16_t)(my + h)}, clip);
    if (c.x0 >= c.x1 || c.y0 >= c.y1 || !n) return;
    for (int y = c.y0; y < c.y1; y++) {
        uint16_t *d = at(fb, stride, clip, c.x0, y);
        const size_t o = (size_t)(y - my) * (size_t)w;
        for (int x = c.x0; x < c.x1; x++, d++) {
            int v = 0;
            for (int i = 0; i < n; i++) v += maps[i][o + (size_t)(x - mx)];
            if (!v) continue;
            if (v > 255) v = 255;
            const int a = (v * ga + 127) / 255;
            if (a) *d = omni_blend565(*d, C_GLOW, a);
        }
    }
}

static void digit_draw(const vm_t *k, int ch, int x0, uint8_t ga, uint16_t *fb, int stride, omni_rect_t clip)
{
    const int mx = x0 - MXO;
    const omni_rect_t box = {(int16_t)mx, MY0, (int16_t)(mx + MW), MY0 + MH};
    if (!meets(box, clip)) return;
    const uint8_t *g[7] = {&k->ghost[0][0]};
    glow_pass(g, 1, MW, MH, mx, MY0, ga, fb, stride, clip);   /* the ghost 8's shadow */
    for (int sg = 0; sg < 7; sg++) {
        omni_pt_t p[6];
        seg_poly(sg, (float)x0, (float)DY, DCW, DCH, DTH, 0.f, 0.f, p);
        omni_fill_poly(fb, stride, &clip, p, 6, C_GHOST, 255);
    }
    const unsigned lit = rd_seg7_segs(ch);
    if (!lit) return;
    int n = 0;
    for (int sg = 0; sg < 7; sg++)
        if ((lit >> sg) & 1u) g[n++] = &k->glow[sg][0][0];
    glow_pass(g, n, MW, MH, mx, MY0, ga, fb, stride, clip);   /* the lit segments' shadow */
    for (int sg = 0; sg < 7; sg++) {
        if (!((lit >> sg) & 1u)) continue;
        omni_pt_t p[6];
        seg_poly(sg, (float)x0, (float)DY, DCW, DCH, DTH, 0.f, 0.f, p);
        omni_fill_poly(fb, stride, &clip, p, 6, RD_C_AMBER, 255);
    }
}

static void colon_draw(const vm_t *k, uint8_t ga, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_VM_COLON, clip)) return;
    const uint8_t *g[1] = {&k->cglow[0][0]};
    glow_pass(g, 1, CMW, CMH, CMX, CMY, ga, fb, stride, clip);
    for (int i = 0; i < 2; i++) {
        omni_pt_t p[4];
        square_poly((float)(DX + 2 * CELL + 5), (float)DY + DCH * (i ? 0.66f : 0.28f), DTH, 0.f, 0.f, p);
        omni_fill_poly(fb, stride, &clip, p, 4, RD_C_AMBER, 255);
    }
}

static void display_draw(const vm_t *k, const vm_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_VM_TIME, clip)) return;
    static const int XS[4] = {DX, DX + CELL, DX + 2 * CELL + COLW, DX + 3 * CELL + COLW};
    for (int i = 0; i < 4; i++) {
        digit_draw(k, s->dig[i], XS[i], s->glow_a, fb, stride, clip);
        if (i == 1 && s->colon) colon_draw(k, s->glow_a, fb, stride, clip);
    }
}

/* one VT323 glyph run in both planes at whole-pixel pens: the characters of text whose pens (x0m + i x 11 200, in
 * 1/1000 px, rounded) can reach the ticker's clip */
static void vt_run(const vm_t *k, const char *text, long x0m, uint16_t rgb, uint16_t *fb, int stride, omni_rect_t c)
{
    const int n = (int)strlen(text);
    for (int plane = 0; plane < 2; plane++) {
        const fk_atlas_t *at2 = plane ? &k->vt28.core : &k->vt28.glow;
        for (int i = 0; i < n; i++) {
            const long pm = x0m + (long)i * 11200L;
            const long fl = pm + 500 >= 0 ? (pm + 500) / 1000 : -((-(pm + 500) + 999) / 1000);
            const int x = (int)fl;
            if (x + 24 < TK_X0 || x - 12 >= TK_X1 || text[i] == ' ') continue;
            const char one[2] = {text[i], 0};
            fk_atlas_text_rgb(fb, stride, c, at2, one, x, TK_BASE, 0, rgb, 255, 0);
        }
    }
}

static void ticker_draw(const vm_t *k, const vm_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_VM_TICKER, clip) || s->tk_kind == RD_VM_TK_NONE) return;
    const omni_rect_t c = cut(RD_VM_TICKER, clip);
    uint16_t *f = at(fb, stride, clip, c.x0, c.y0);
    const uint16_t rgb = s->tk_a == 255 ? RD_C_AMBER
                                        : omni_pack565(omni_ex_r(RD_C_AMBER) * s->tk_a / 255,
                                                       omni_ex_g(RD_C_AMBER) * s->tk_a / 255,
                                                       omni_ex_b(RD_C_AMBER) * s->tk_a / 255);
    if (s->tk_kind == RD_VM_TK_SCROLL) {
        const char *m = scroll_msg(s->set);
        const long x0m = (long)TK_X0 * 1000L - s->tk_off, mw = scroll_mw(s->set);
        vt_run(k, m, x0m, rgb, f, stride, c);
        vt_run(k, m, x0m + mw, rgb, f, stride, c);
        return;
    }
    /* centred on 233 (VT323's 11.2 advance: the line's width n x 11.2) */
    const long x0m = 233000L - (long)strlen(s->tk) * 5600L;
    vt_run(k, s->tk, x0m, rgb, f, stride, c);
}

static void buttons_draw(const vm_t *k, const vm_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    for (int i = 0; i < RD_VM_BTN_N; i++) {
        if (!meets(RD_VM_BTN[i], clip)) continue;
        if (s->dark[i]) rr_fill(&k->press[i], RD_VM_BTN[i], fb, stride, clip, 0x0000, 115);
        const bool live = i == RD_VM_MYSTERY_BTN;
        const uint16_t col = live ? RD_C_AMBER : RD_C_LAMP;
        const uint8_t a = s->lamp[i];
        const omni_rect_t lb = {(int16_t)(BTN_X(i) + 6), (int16_t)(BTN_Y(i) + 26), (int16_t)(BTN_X(i) + 78),
                                (int16_t)(BTN_Y(i) + 42)};
        rr_fill(&k->lamp[i], lb, fb, stride, clip, col, (uint8_t)((36 * a + 127) / 255));   /* .14 a */
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, live ? "? ? ?" : "SOLD OUT", (float)(BTN_X(i) + 42),
                              BTN_Y(i) + 38, TRACK, true, col, a);
    }
}

/* ---- the sticker: the turned rect and its three lines, sampled per pixel */
typedef struct { const char *text; float u0, base; uint16_t rgb; uint8_t a; } st_line_t;

/* the atlas's alpha at (sx, sy) of cell c (bilinear; 0 outside it) */
static int cell_alpha(const fk_img_t *img, const fk_cell_t *c, float sx, float sy)
{
    const float fx = sx - 0.5f, fy = sy - 0.5f;
    const int ix = fx < 0 ? -1 : (int)fx, iy = fy < 0 ? -1 : (int)fy;
    const float ax = fx - (float)ix, ay = fy - (float)iy;
    int v[2][2];
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            const int xx = ix + i, yy = iy + j;
            v[j][i] = xx >= 0 && xx < c->w && yy >= 0 && yy < c->h
                          ? img->a[(size_t)(c->y + yy) * img->w + (size_t)(c->x + xx)]
                          : 0;
        }
    const float top = (float)v[0][0] + ((float)v[0][1] - (float)v[0][0]) * ax;
    const float bot = (float)v[1][0] + ((float)v[1][1] - (float)v[1][0]) * ax;
    return (int)(top + (bot - top) * ay + 0.5f);
}

/* the line's coverage at (u, v): its glyphs from u0 on the baseline, tracked */
static int line_alpha(const vm_t *k, const st_line_t *L, float u, float v)
{
    if (v < L->base - 12.f || v > L->base + 5.f || u < L->u0 - 2.f) return 0;
    float pen = L->u0;
    for (const char *p = L->text; *p; p++) {
        const char *g = strchr(k->ch13.glyphs, *p);
        if (!g) continue;
        const fk_cell_t *c = &k->ch13.cells[g - k->ch13.glyphs];
        const float gx = pen + (float)c->ox, gy = L->base + (float)c->oy;
        if (u >= gx - 1.f && u < gx + (float)c->w + 1.f && v >= gy - 1.f && v < gy + (float)c->h + 1.f) {
            const int a = cell_alpha(k->ch13.img, c, u - gx, v - gy);
            if (a) return a;
        }
        pen += (float)c->adv + TRACK;
        if (pen > u + 2.f) break;
    }
    return 0;
}

static void sticker_draw(const vm_t *k, const vm_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_VM_STICKER, clip)) return;
    const omni_rect_t c = cut(RD_VM_STICKER, clip);
    const bool wide = s->set && !s->h24;
    const float uL = -108.f, uR = wide ? 100.f : 64.f, vT = -18.f, vB = 20.f, rr = 2.f;
    const float cu = (uL + uR) / 2.f, cv = (vT + vB) / 2.f, hx = (uR - uL) / 2.f - rr, hy = (vB - vT) / 2.f - rr;
    const st_line_t L[3] = {{"TODAY", -100.f, -2.6f, C_INK, 255},
                            {s->date, -40.f, -2.6f, C_INK, 255},
                            {"SERVICED: NEVER", -100.f, 13.4f, C_PURPLE, 230}};
    for (int y = c.y0; y < c.y1; y++) {
        uint16_t *d = at(fb, stride, clip, c.x0, y);
        const float dy = (float)y + 0.5f - ST_CY;
        for (int x = c.x0; x < c.x1; x++, d++) {
            const float dx = (float)x + 0.5f - ST_CX;
            const float u = dx * ST_COS + dy * ST_SIN, v = -dx * ST_SIN + dy * ST_COS;
            float qx = (u > cu ? u - cu : cu - u) - hx, qy = (v > cv ? v - cv : cv - v) - hy;
            float dist;
            if (qx > 0 && qy > 0) dist = sqrt_f(qx * qx + qy * qy) - rr;
            else dist = (qx > qy ? qx : qy) - rr;
            const float cov = clamp01(0.5f - dist);
            if (!(cov > 0)) continue;
            *d = omni_blend565(*d, C_STICKER, (int)(cov * 255.f + 0.5f));
            for (int l = 0; l < 3; l++) {
                const int a = line_alpha(k, &L[l], u, v);
                if (a) *d = omni_blend565(*d, L[l].rgb, (a * L[l].a + 127) / 255);
            }
        }
    }
}

/* ---- the tray */
static void item_draw(int it, uint8_t a, uint16_t *fb, int stride, omni_rect_t clip)
{
    const float ox = (float)(TRAY_X + 20), oy = (float)(TRAY_Y + 19);
#define P(x, y) ((omni_pt_t){ox + (float)(x), oy + (float)(y)})
    if (it == 0) {   /* one sock */
        const omni_pt_t p[6] = {P(-4, -9), P(3, -9), P(3, 2), P(8, 5), P(6, 9), P(-4, 6)};
        fk_poly_stroke(fb, stride, &clip, p, 6, 2.f, RD_C_PAINT, a);
    } else if (it == 1) {   /* a spanner */
        fk_line(fb, stride, &clip, ox - 9.f, oy + 7.f, ox + 5.f, oy - 5.f, 2.f, RD_C_PAINT, a);
        fk_ring(fb, stride, &clip, ox + 7.f, oy - 7.f, 4.f, 2.f, RD_C_PAINT, a);
    } else if (it == 2) {   /* one pea */
        fk_disc(fb, stride, &clip, ox, oy + 2.f, 3.5f, RD_C_GO, a);
    } else if (it == 3) {   /* a fish: the ellipse (-1, 0) 8 x 4 and its tail */
        omni_pt_t e[8];
        static const float C8[8][2] = {{1, 0}, {.70710678f, .70710678f}, {0, 1}, {-.70710678f, .70710678f},
                                       {-1, 0}, {-.70710678f, -.70710678f}, {0, -1}, {.70710678f, -.70710678f}};
        for (int i = 0; i < 8; i++) e[i] = P(-1.f + 8.f * C8[i][0], 4.f * C8[i][1]);
        fk_poly_stroke(fb, stride, &clip, e, 8, 2.f, RD_C_PAINT, a);
        const omni_pt_t t[3] = {P(7, 0), P(11, -4), P(11, 4)};
        fk_poly_stroke(fb, stride, &clip, t, 3, 2.f, RD_C_PAINT, a);
    } else if (it == 4) {   /* an IOU: the slip and its two lines at 1.2 px */
        const omni_pt_t r[4] = {P(-8, -7), P(8, -7), P(8, 7), P(-8, 7)};
        fk_poly_stroke(fb, stride, &clip, r, 4, 2.f, RD_C_PAINT, a);
        fk_line(fb, stride, &clip, ox - 4.f, oy - 2.f, ox + 4.f, oy - 2.f, 1.2f, RD_C_PAINT, a);
        fk_line(fb, stride, &clip, ox - 4.f, oy + 2.f, ox + 2.f, oy + 2.f, 1.2f, RD_C_PAINT, a);
    } else {   /* a flat battery: the cell and its terminal */
        const omni_pt_t r[4] = {P(-4, -8), P(4, -8), P(4, 8), P(-4, 8)};
        fk_poly_stroke(fb, stride, &clip, r, 4, 2.f, RD_C_PAINT, a);
        fk_line(fb, stride, &clip, ox - 2.f, oy - 10.f, ox + 2.f, oy - 10.f, 2.f, RD_C_PAINT, a);
    }
#undef P
}

static void tray_draw(const vm_t *k, const vm_state_t *s, uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!meets(RD_VM_FLAP, clip)) return;
    const omni_rect_t c = cut(RD_VM_FLAP, clip);
    if (s->item >= 0) {
        const float al = clamp01(s->open * 1.4f);
        const uint8_t a = a8_of(al);
        /* the glow: createRadialGradient(233, 418, 2, 233, 418, 80), .25 to 0, over the well */
        for (int y = c.y0; y < c.y1; y++) {
            uint16_t *d = at(fb, stride, clip, c.x0, y);
            const float dy = (float)y + 0.5f - 418.f;
            for (int x = c.x0; x < c.x1; x++, d++) {
                const float dx = (float)x + 0.5f - 233.f, r = sqrt_f(dx * dx + dy * dy);
                const float g = r <= 2.f ? 1.f : r >= 80.f ? 0.f : (80.f - r) / 78.f;
                const int ga = (int)(0.25f * g * al * 255.f + 0.5f);
                if (ga) *d = omni_blend565(*d, RD_C_AMBER, ga);
            }
        }
        item_draw(s->item, a, fb, stride, clip);
        const float w1 = fk_atlas_width_tracked(&k->ch13, ITEM_L1[s->item], TRACK);
        const float w2 = fk_atlas_width_tracked(&k->ch13, ITEM_L2[s->item], TRACK);
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, ITEM_L1[s->item], (float)(TRAY_X + 38) + w1 / 2.f,
                              TRAY_Y + 15, TRACK, true, RD_C_PAINT, a);
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, ITEM_L2[s->item], (float)(TRAY_X + 38) + w2 / 2.f,
                              TRAY_Y + 30, TRACK, true, RD_C_PAINT_DIM, 255);
    }
    if (s->open >= 0.98f) return;
    float sn, cs;
    fk_sincos(s->open * 1.25f, &sn, &cs);
    const float h = (float)TRAY_H * cs, hh = h > 2.f ? h : 2.f;
    omni_rrect_t rr;
    omni_rrect_prepare(&rr, (float)TRAY_X, (float)TRAY_Y, (float)(TRAY_X + TRAY_W), (float)TRAY_Y + hh, 4.f);
    uint8_t cov[OMNI_COVER_W];
    const int r0 = omni_ex_r(C_FLAP0), g0 = omni_ex_g(C_FLAP0), b0 = omni_ex_b(C_FLAP0);
    const int r1 = omni_ex_r(C_FLAP1), g1 = omni_ex_g(C_FLAP1), b1 = omni_ex_b(C_FLAP1);
    for (int y = c.y0; y < c.y1; y++) {
        if ((float)y >= (float)TRAY_Y + hh) break;
        /* the gradient from (0, TRAY.y) to (0, TRAY.y + h): its colour at the row's centre */
        const float t = clamp01(((float)y + 0.5f - (float)TRAY_Y) / h);
        const uint16_t col = omni_pack565((int)((float)r0 + (float)(r1 - r0) * t + 0.5f),
                                          (int)((float)g0 + (float)(g1 - g0) * t + 0.5f),
                                          (int)((float)b0 + (float)(b1 - b0) * t + 0.5f));
        for (int x0 = c.x0; x0 < c.x1; x0 += OMNI_COVER_W) {
            const int x1 = x0 + OMNI_COVER_W < c.x1 ? x0 + OMNI_COVER_W : c.x1;
            omni_cover_rrect_prepared(&rr, y, x0, x1, cov);
            uint16_t *d = at(fb, stride, clip, x0, y);
            for (int x = x0; x < x1; x++, d++)
                if (cov[x - x0]) *d = omni_blend565(*d, col, cov[x - x0]);
        }
    }
    if (h > 16.f) {
        const int base = (int)((float)TRAY_Y + h / 2.f + 1.f + 3.4f + 0.5f);
        fk_atlas_text_tracked(fb, stride, clip, &k->ch13, "PUSH", 233.f, base, TRACK, true, RD_C_PAINT_DIM, 255);
    }
}

static void draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const vm_t *k = ctx;
    vm_state_t s;
    state_of(in, &s);
    base_draw(k, fb, stride, clip);
    lightbox_draw(k, &s, fb, stride, clip);
    display_draw(k, &s, fb, stride, clip);
    ticker_draw(k, &s, fb, stride, clip);
    buttons_draw(k, &s, fb, stride, clip);
    sticker_draw(k, &s, fb, stride, clip);
    tray_draw(k, &s, fb, stride, clip);
}

static void glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const vm_t *k = ctx;
    for (int y = clip.y0; y < clip.y1; y++)
        memset(fb + (size_t)(y - clip.y0) * (size_t)stride, 0, (size_t)(clip.x1 - clip.x0) * sizeof fb[0]);
    const float ox = (float)in->odx, oy = (float)in->ody;
    char t[5];
    rd_seg7_str(in, t);
    static const int XS[4] = {GX0, GX0 + GCELL, GX0 + 2 * GCELL + GCOLW, GX0 + 3 * GCELL + GCOLW};
    for (int i = 0; i < 4; i++) {
        const unsigned lit = rd_seg7_segs(t[i]);
        for (int sg = 0; sg < 7; sg++) {
            if (!((lit >> sg) & 1u)) continue;
            omni_pt_t p[6];
            seg_poly(sg, (float)XS[i], (float)GY0, GCW, GCH, GTH, ox, oy, p);
            omni_fill_poly(fb, stride, &clip, p, 6, RD_G_AMBER, 255);
        }
    }
    for (int i = 0; i < 2; i++) {
        omni_pt_t p[4];
        square_poly((float)(GX0 + 2 * GCELL + 5), (float)GY0 + GCH * (i ? 0.66f : 0.28f), GTH, ox, oy, p);
        omni_fill_poly(fb, stride, &clip, p, 4, RD_G_AMBER, 255);
    }
    fk_disc(fb, stride, &clip, 331.f + ox, 286.f + oy, 3.f, RD_G_AMBER, 255);
    if (in->clk.set && in->clk.stale)
        rd_stale_mark(fb, stride, &clip, &k->ch13, k->gstale_cx + in->odx, GSTALE_CY + in->ody, true);
}

/* ---- dirty(): what changed between a and b */
typedef struct { omni_rect_t r[8]; unsigned n; bool over; } rects_t;

static void add(rects_t *L, omni_rect_t r)
{
    if (L->n < 8) L->r[L->n++] = r;
    else L->over = true;
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
        char ta[5], tb[5];
        rd_seg7_str(a, ta);
        rd_seg7_str(b, tb);
        const bool same = a->clk.set == b->clk.set && (a->clk.set && a->clk.stale) == (b->clk.set && b->clk.stale) &&
                          a->odx == b->odx && a->ody == b->ody && strcmp(ta, tb) == 0;
        return same ? 0 : whole(out);
    }
    vm_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    if (sa.set != sb.set || sa.h24 != sb.h24 || sa.reduced != sb.reduced) return whole(out);
    rects_t L;
    memset(&L, 0, sizeof L);
    if (sa.lit != sb.lit || sa.lbox_a != sb.lbox_a) add(&L, RD_VM_LIGHTBOX);
    else if (sa.stale != sb.stale) add(&L, RD_VM_STALE);
    if (strcmp(sa.dig, sb.dig) != 0 || sa.glow_a != sb.glow_a) add(&L, RD_VM_TIME);
    else if (sa.colon != sb.colon) add(&L, RD_VM_COLON);
    if (sa.tk_kind != sb.tk_kind || strcmp(sa.tk, sb.tk) != 0 || sa.tk_a != sb.tk_a || sa.tk_off != sb.tk_off)
        add(&L, RD_VM_TICKER);
    unsigned nb = 0;
    for (int i = 0; i < RD_VM_BTN_N; i++) nb += sa.dark[i] != sb.dark[i] || sa.lamp[i] != sb.lamp[i];
    if (nb > 3) {
        add(&L, (omni_rect_t){101, 238, 365, 340});   /* the six */
    } else {
        for (int i = 0; i < RD_VM_BTN_N; i++) {
            if (sa.dark[i] != sb.dark[i]) add(&L, RD_VM_BTN[i]);
            else if (sa.lamp[i] != sb.lamp[i])
                add(&L, i == RD_VM_MYSTERY_BTN ? RD_VM_MYSTERY
                                               : (omni_rect_t){(int16_t)(BTN_X(i) + 4), (int16_t)(BTN_Y(i) + 26),
                                                               (int16_t)(BTN_X(i) + 80), (int16_t)(BTN_Y(i) + 42)});
        }
    }
    if (strcmp(sa.date, sb.date) != 0) add(&L, RD_VM_STICKER);
    if (sa.item != sb.item || sa.open != sb.open) add(&L, RD_VM_FLAP);
    if (L.over) return whole(out);
    for (unsigned i = 0; i < L.n; i++) out[i] = L.r[i];
    return (uint8_t)L.n;
}

/* ---- taps: the six buttons' 84 x 48 boxes (inclusive at every edge: [x, x + 84) x [y, y + 48)) */
static int button_at(int x, int y)
{
    for (int i = 0; i < RD_VM_BTN_N; i++)
        if (x >= RD_VM_BTN[i].x0 && x < RD_VM_BTN[i].x1 && y >= RD_VM_BTN[i].y0 && y < RD_VM_BTN[i].y1) return i;
    return -1;
}

static fk_tap_t tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return button_at(x, y) >= 0 ? FK_TAP_PULSE : FK_TAP_NONE;
}

/* a button: the pulse, the variant's last press (bits 0-2) and its parity (bit 6); MYSTERY with a set clock moves its
 * next item on (bits 3-5), so the pulse shows the one it was. A NULL in (only from outside the runtime, fk/face.h)
 * counts as a set clock */
static fk_tap_t tap_var(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant)
{
    (void)ctx;
    const int i = button_at(x, y);
    if (i < 0) return FK_TAP_NONE;
    unsigned v = *variant, next = ((v >> 3) & 7u) % RD_VM_ITEMS;
    if (i == RD_VM_MYSTERY_BTN && (in == NULL || in->clk.set)) next = (next + 1u) % RD_VM_ITEMS;
    v = (unsigned)i | (next << 3) | ((v ^ RD_VM_VAR_PARITY) & RD_VM_VAR_PARITY);
    *variant = (uint16_t)v;
    return FK_TAP_PULSE;
}

const fk_face_ops_t RD_FACE_VENDING = {
    .key = "vending",
    .ctx_bytes = sizeof(vm_t),
    .cadence_ms = 50,
    .bind = bind,
    .draw = draw,
    .glance = glance,
    .dirty = dirty,
    .tap = tap,
    .wake_len_ms = WAKE_MS,
    .pulse_len_ms = VEND_MS,   /* the flap's 3.6 s (a message 2.6 s, a flash 1.2 s, the press 160 ms) */
    .tap_var = tap_var,
    .stills_extra = 1,         /* still 11: MYSTERY's first item (RD_VM_TAPPED_VARIANT), vended (RD_VM_PULSE_STILL_MS) */
};
