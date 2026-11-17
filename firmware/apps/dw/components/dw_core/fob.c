/* Face 13, the Fob watch (dw/faces.h; dw wave 2 Task 7, spec sections 10 and 11), the Modern pocket watch from
 * faces-modern.js fwBuild, fwLid, fwHands and fob-watch: a silver hunter case that swings open on its top hinge, the
 * light that escapes, a black enamel dial with Breguet hands and a sub-seconds hand that ticks four times a second.
 *
 * Layers (tools/export/dw_layers.json): dw_f13_lid (fwBuild's lid, the outside of the case: brushed silver, the
 * guilloche, the engraved rings, dots and sentence circle of script, the rim shadow; 466 RGB565 over black, its disc
 * cut here), dw_f13_dial (fwBuild's dial over black: the chapter rings, the Cormorant numerals with no 6, the
 * sub-seconds ring at (233, 337) r 42 and the small script roundel under 12), the shared dw_c1_mono (the unset --:--)
 * and dw_dymo (the stale strip). The lid's back (fw.lidBack) is never drawn: past omni_hinge_gone_rad no lid row shows.
 * Drawn here: the lid through the shared hinge engine, the escaping light (a glow of r 233 and 14 wisps of r 70, from
 * dw_glow_lut: const data), the Breguet hands (hands.c dw_hand_breguet), the sub-seconds hand and hub, the centre cap,
 * the stale marker and the glance.
 *
 * The lid (WD44): each screen row is omni_hinge_lines_at(&DW_F13_HINGE, theta)'s source row v and scale 1 / s, so a
 * pixel samples u = floor(233 + (x + .5 - 233) / s) (Q17 integers), inside the lid's disc (r 233, by the source
 * pixel's centre), shaded by 1 - .55 sin theta (fk_sincos: the table trig). The engine is omni_core's, unchanged; its
 * eye line is at C, not the mockup's hinge-row projection. It runs through the canvas at the canvas rate on the 40 ms
 * cadence timer (WD31), never a direct-path client.
 * The wake (wake_len_ms = HOLD + OPEN + LIGHT = 2200): the closed lid for DW_F13_HOLD_MS (WD31: the mockup's 450 would
 * break the 0.5 s lid rule), the swing to 0.62 pi by easeOut3 over DW_F13_OPEN_MS, and the light from HOLD on. A tap
 * anywhere (FK_TAP_PULSE, pulse_len_ms = CLOSE + OPEN + LIGHT = 2520, R147 PR5): the lid closes by inOut over
 * DW_F13_CLOSE_MS and opens again at once (no hold), with the light from CLOSE on. A pulse takes precedence over the
 * face's own wake (R147 PR20). Every wisp is out when the light ends (each sp >= .62), so the wake's end is the settled
 * frame exactly.
 * Motion at rest (cadence 40, R147 PR4): the sub-seconds hand at floor(s x 4) / 4 / 60 turns; dirty() is DW_F13_SUB
 * alone when floor(s x 4) changes and nothing otherwise, so the timer draws nothing in between; the hour and minute hands
 * step once a minute (their old and new boxes). Under reduced motion (R147 PR7) the dial shows at once with no lid (no
 * own wake), the sub-seconds tick whole seconds, there are no wisps or light, and a tap fades the flat lid out over
 * DW_F13_FADE_MS (a tap's own feedback may animate).
 * Unset: no hands; --:-- in silver (200, 205, 212) about (233, 173). Stale: the amber lamp and NOT SYNCED centred
 * (233, 277), under the hands. The chime does not breathe here.
 * Pure: no libm trig of its own (fk_sincos); the hinge engine's lines (omni_core, sinf and cosf) are read only in
 * frames in motion, never in a still. No allocation. */
#include <math.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/faces.h"
#include "omni/blend565.h"
#include "omni/lid.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define HAND_RGB RGB(0xe8, 0xec, 0xf0)
#define SUB_RGB RGB(0xe0, 0xa5, 0x4a)
#define UNSET_RGB RGB(200, 205, 212)
#define UNSET_BASE 177   /* the atlas's cells span baseline - 10 .. baseline + 1: centred on row 173 */
#define STALE_Y 277.f
#define SUB_X 233.f
#define SUB_Y 337.f
#define BIG_RGB RGB(255, 190, 100)
#define WISP_RGB RGB(255, 200, 110)
#define BIG_R 233
#define WISP_R 70
#define LINES 16         /* the hinge lines worked out at a time (on the stack) */
#define WAKE_LEN (DW_F13_HOLD_MS + DW_F13_OPEN_MS + DW_F13_LIGHT_MS)
#define PULSE_LEN (DW_F13_CLOSE_MS + DW_F13_OPEN_MS + DW_F13_LIGHT_MS)

/* faces-modern.js :836-851: f 900 with Z x .9 is F 1000; the hinge on the top row; flat (dome 0) */
const omni_hinge_t DW_F13_HINGE = {OMNI_HINGE_HORIZONTAL, 0.f, 1000.f, 466, 0.f, 0.f};
const omni_rect_t DW_F13_SUB = {185, 289, 281, 385};     /* 96 x 96 about (233, 337) */
const omni_rect_t DW_F13_STALE = {164, 262, 302, 292};   /* dw_stale_mark(233, 277)'s reach */

/* The wisps (:834's ranges, fixed seed: the mockup's rng(99) draws, its second sp raised from .6091 to .62 so that
 * every wisp is out by the light's end, 2.1 s after it starts): a, sp, curl, r0, size */
static const float WISPS[14][5] = {
    {1.6366f, 1.2439f, 0.0981f, 61.41f, 0.4009f},  {5.1368f, 0.6200f, -0.9470f, 43.79f, 0.9368f},
    {2.3009f, 0.6515f, -0.3867f, 63.59f, 0.6024f}, {1.1783f, 0.9665f, 0.6384f, 49.51f, 0.6184f},
    {3.4986f, 1.2098f, -0.2859f, 66.41f, 1.1771f}, {3.2595f, 0.8087f, 1.1091f, 32.58f, 0.7648f},
    {4.3240f, 0.9130f, -0.4857f, 34.53f, 0.9526f}, {2.4767f, 1.3223f, -0.7384f, 68.99f, 0.7886f},
    {3.8045f, 1.1612f, 0.6212f, 36.63f, 0.8740f},  {4.8277f, 0.9638f, -0.8201f, 21.25f, 0.9171f},
    {6.2208f, 1.0355f, -0.4884f, 63.31f, 1.0567f}, {3.1387f, 1.1597f, -0.5893f, 64.63f, 0.8908f},
    {4.8242f, 0.7195f, 0.5784f, 25.18f, 0.6202f},  {3.9641f, 0.8995f, -0.3964f, 70.91f, 0.4535f},
};

_Static_assert(ASSET_DW_F13_LID_W == W && ASSET_DW_F13_LID_H == W, "dw_f13_lid is the whole screen");
_Static_assert(ASSET_DW_F13_DIAL_W == W && ASSET_DW_F13_DIAL_H == W, "dw_f13_dial is the whole screen");

typedef struct {
    bool lid_ok, dial_ok, mono_ok, dymo_ok;
    fk_img_t lid, dial, mono_img, dymo_img;
    fk_atlas_t mono, dymo;
    const uint8_t *g233, *g70;   /* the light's tables (dw_glow_lut: const data) */
    float gone;                  /* omni_hinge_gone_rad(&DW_F13_HINGE): past it no lid row shows */
} f13_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float out_cubic(float s) { const float u = 1 - clampf(s, 0, 1); return 1 - u * u * u; }
static float in_out_cubic(float s)
{
    s = clampf(s, 0, 1);
    if (s < 0.5f) return 4 * s * s * s;
    const float u = -2 * s + 2;
    return 1 - u * u * u / 2;
}

float dw_f13_theta(uint32_t t_ms, bool reduced)
{
    if (reduced || t_ms < DW_F13_HOLD_MS) return 0;
    return DW_F13_OPEN_RAD * out_cubic((float)(t_ms - DW_F13_HOLD_MS) / (float)DW_F13_OPEN_MS);
}

float dw_f13_tap_theta(uint32_t pulse_ms)
{
    if (pulse_ms < DW_F13_CLOSE_MS) return DW_F13_OPEN_RAD * (1 - in_out_cubic((float)pulse_ms / (float)DW_F13_CLOSE_MS));
    return DW_F13_OPEN_RAD * out_cubic((float)(pulse_ms - DW_F13_CLOSE_MS) / (float)DW_F13_OPEN_MS);
}

/* ---- the face's motion for an input: the lid (its angle and weight, 0: none) and the light (lt_ms since it began,
 * light false: none) */
typedef struct {
    float theta;
    uint8_t a8;
    bool light;
    uint32_t lt_ms;
} f13_motion_t;

static void motion_of(const fk_face_in_t *in, f13_motion_t *m)
{
    memset(m, 0, sizeof *m);
    if (in->glance) return;
    if (in->pulsing && in->pulse_ms < PULSE_LEN) {   /* a tap, over the wake (R147 PR20) */
        if (in->reduced) {                           /* the flat lid fades out (R147 PR7) */
            if (in->pulse_ms < DW_F13_FADE_MS)
                m->a8 = (uint8_t)((uint32_t)(DW_F13_FADE_MS - in->pulse_ms) * 255u / DW_F13_FADE_MS);
            return;
        }
        m->theta = dw_f13_tap_theta(in->pulse_ms);
        m->a8 = 255;
        m->light = in->pulse_ms >= DW_F13_CLOSE_MS;
        m->lt_ms = m->light ? in->pulse_ms - DW_F13_CLOSE_MS : 0;
        return;
    }
    if (in->reduced || in->wake_ms >= WAKE_LEN) return;   /* settled; reduced: the dial at once */
    m->theta = dw_f13_theta(in->wake_ms, false);
    m->a8 = 255;
    m->light = in->wake_ms >= DW_F13_HOLD_MS;
    m->lt_ms = m->light ? in->wake_ms - DW_F13_HOLD_MS : 0;
}

void dw_f13_lid(const fk_face_in_t *in, float *theta, uint8_t *a8)
{
    f13_motion_t m;
    motion_of(in, &m);
    *theta = m.theta;
    *a8 = m.a8;
}

/* the frame differs from the settled one (the lid or the light shows) */
static bool moving(const fk_face_in_t *in)
{
    f13_motion_t m;
    motion_of(in, &m);
    return m.a8 || m.light;
}

/* the sub-seconds' quarter of the minute: floor(s x 4), whole seconds under reduced motion */
static unsigned quarter(const fk_face_in_t *in)
{
    return (in->clk.s % 60u) * 4u + (in->reduced ? 0u : (in->clk.ms % 1000u) / 250u);
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool f13_bind(void *ctx, fk_asset_fn get)
{
    f13_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->lid_ok = img(get, ASSET_DW_F13_LID, &k->lid, FK_CF_RGB565, W, W);
    k->dial_ok = img(get, ASSET_DW_F13_DIAL, &k->dial, FK_CF_RGB565, W, W);
    k->mono_ok = img(get, ASSET_DW_C1_MONO, &k->mono_img, FK_CF_RGB565A8, ASSET_DW_C1_MONO_W, ASSET_DW_C1_MONO_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->mono = (fk_atlas_t){&k->mono_img, DW_CELLS_DW_C1_MONO, DW_GLYPHS_DW_C1_MONO, ASSET_DW_C1_MONO_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    k->g233 = dw_glow_lut(BIG_R);
    k->g70 = dw_glow_lut(WISP_R);
    k->gone = omni_hinge_gone_rad(&DW_F13_HINGE);
    return k->g233 && k->g70 && k->lid_ok && k->dial_ok && k->mono_ok && k->dymo_ok;
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

/* The lid over the frame at m: each row's hinge line (LINES at a time), each pixel the lid's at (u, v) when that
 * source pixel's centre lies inside the lid's disc, shaded by 1 - .55 sin theta and blended at m->a8 */
static void lid_draw(const f13_t *k, const f13_motion_t *m, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!m->a8 || !k->lid_ok || !(m->theta < k->gone)) return;
    float s, c;
    fk_sincos(m->theta, &s, &c);
    const int dark = (int)floorf(0.55f * s * 255.f + 0.5f);   /* black over the lid at .55 sin theta */
    const int32_t RR = W * W;
    omni_hinge_line_t ln[LINES];
    for (int y0 = clip.y0; y0 < clip.y1; y0 += LINES) {
        const int y1 = y0 + LINES < clip.y1 ? y0 + LINES : clip.y1;
        omni_hinge_lines_at(&DW_F13_HINGE, m->theta, y0, y1, ln);
        for (int y = y0; y < y1; y++) {
            const omni_hinge_line_t *l = &ln[y - y0];
            /* 1 / s <= 1 on this leaf (v >= pos, sin theta >= 0 for theta in [0, pi)), so the Q17 sum below fits int32:
             * |2 x + 1 - 466| x 65536 + (233 << 17) < 2^26; a larger one (none) is skipped */
            if (l->v < 0 || l->v >= W || l->inv_s_q16 > 65536u) continue;
            const int32_t dv = 2 * l->v + 1 - W, dv2 = dv * dv, inv = (int32_t)l->inv_s_q16;
            const uint16_t *src = k->lid.rgb + (size_t)l->v * W;
            uint16_t *d = fb + (size_t)(y - clip.y0) * (size_t)stride;   /* indexed by x - clip.x0 */
            for (int x = clip.x0; x < clip.x1; x++) {
                /* u = floor(233 + (x + .5 - 233) inv), in Q17: ((233 << 17) + (2 x + 1 - 466) inv_s_q16) >> 17 */
                const int32_t q = (233 << 17) + (2 * x + 1 - W) * inv;
                if (q < 0) continue;
                const int32_t u = q >> 17;
                if (u >= W) continue;
                const int32_t du = 2 * u + 1 - W;
                if (du * du + dv2 >= RR) continue;   /* off the lid's disc */
                uint16_t p = src[u];
                if (dark) p = omni_blend565(p, 0, dark);
                uint16_t *o = &d[x - clip.x0];
                *o = m->a8 == 255 ? p : omni_blend565(*o, p, m->a8);
            }
        }
    }
}

/* the escaping light (:892-904), lt_ms after it began: the glow of r 233 and the 14 wisps of r 70, additive */
static void light_draw(const f13_t *k, const f13_motion_t *m, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!m->light) return;
    const float lt = (float)m->lt_ms / 1000.f;
    /* glowSprite('255,190,100', 233, .7) at globalAlpha clamp(4 lt) clamp(1 - (lt - .3) / 1.2) .55 */
    const float big = clampf(lt * 4.f, 0, 1) * clampf(1.f - (lt - 0.3f) / 1.2f, 0, 1) * 0.55f * 0.7f;
    const uint8_t ba = (uint8_t)floorf(big * 255.f + 0.5f);
    if (ba) fk_glow_add_lut(fb, stride, &clip, 233, 233, BIG_R, k->g233, BIG_RGB, ba);
    for (int i = 0; i < 14; i++) {
        const float *w = WISPS[i];
        const float life = lt * w[1];
        const float al = clampf(life * 3.f, 0, 1) * clampf(1.f - (life - 0.4f) / 0.9f, 0, 1);
        if (!(al > 0)) continue;
        /* glowSprite('255,200,110', 70, .8) at globalAlpha al .8, at r0 + 150 life along a + curl life; the mockup's
         * sprite is drawn s = 70 size (.6 + life) wide about a point .2 s below that (here at r 70) */
        const float rr = w[3] + life * 150.f, a = w[0] + w[2] * life, sz = 70.f * w[4] * (0.6f + life);
        float sa, ca;
        fk_sincos(a, &sa, &ca);
        const int x = (int)floorf(C + ca * rr + 0.5f), y = (int)floorf(C + sa * rr + 0.2f * sz + 0.5f);
        const uint8_t a8 = (uint8_t)floorf(al * 0.8f * 0.8f * 255.f + 0.5f);
        /* R148 (d): a wisp whose glow misses the panel's disc is not drawn (it would light only pixels no panel shows) */
        const int32_t ex = x - 233, ey = y - 233, reach = 233 + WISP_R + 2;
        if (ex * ex + ey * ey >= reach * reach) continue;
        if (a8 && x + WISP_R + 2 > clip.x0 && x - WISP_R - 1 < clip.x1 && y + WISP_R + 2 > clip.y0 &&
            y - WISP_R - 1 < clip.y1)
            fk_glow_add_lut(fb, stride, &clip, x, y, WISP_R, k->g70, WISP_RGB, a8);
    }
}

/* the sub-seconds hand (:864-867): a 1.4 px #E0A54A line from 8 px behind its centre to 38, at quarter q / 240 of a
 * turn, and its hub r 2.4 */
static void sub_hand(omni_rect_t clip, uint16_t *fb, int stride, unsigned q)
{
    if (!meets(clip, DW_F13_SUB)) return;
    float s, c;
    fk_sincos((float)q * (2 * PI_F / 240.f), &s, &c);
    fk_line(fb, stride, &clip, SUB_X - 8.f * s, SUB_Y + 8.f * c, SUB_X + 38.f * s, SUB_Y - 38.f * c, 1.4f, SUB_RGB, 255);
    fk_disc(fb, stride, &clip, SUB_X, SUB_Y, 2.4f, SUB_RGB, 255);
}

static float hour_angle(const fk_face_in_t *in)
{
    return (((float)(in->clk.h % 12) + (float)(in->clk.m % 60) / 60.f) / 12.f) * 2 * PI_F;
}
static float minute_angle(const fk_face_in_t *in) { return ((float)(in->clk.m % 60) / 60.f) * 2 * PI_F; }

static void f13_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const f13_t *k = ctx;
    f13_motion_t m;
    motion_of(in, &m);
    if (k->dial_ok) fk_blit(fb, stride, clip, &k->dial, 0, 0, 255);
    else fill(fb, stride, clip, 0);
    if (!in->clk.set) {   /* no hands: --:-- in silver */
        if (k->mono_ok) {
            const int w = fk_atlas_width(&k->mono, "--:--", 0);
            fk_atlas_text_rgb(fb, stride, clip, &k->mono, "--:--", (W - w) / 2, UNSET_BASE, 0, UNSET_RGB, 255, 0);
        }
    } else {
        if (in->clk.stale && k->dymo_ok && meets(clip, DW_F13_STALE))
            dw_stale_mark(fb, stride, &clip, &k->dymo, C, STALE_Y);   /* under the hands */
        dw_hand_breguet(fb, stride, &clip, hour_angle(in), true, HAND_RGB, 255);
        dw_hand_breguet(fb, stride, &clip, minute_angle(in), false, HAND_RGB, 255);
        sub_hand(clip, fb, stride, quarter(in));
        fk_disc(fb, stride, &clip, C, C, 6.f, HAND_RGB, 255);   /* the centre cap */
        fk_disc(fb, stride, &clip, C, C, 2.f, RGB(0x22, 0x22, 0x22), 255);
    }
    light_draw(k, &m, clip, fb, stride);
    lid_draw(k, &m, clip, fb, stride);
}

/* fob-watch sleepFrame (:914-925): twelve dots at r 196 (#44484e; the quarters 3 px, the others 1.8) and the outline
 * Breguet hands (#5c6168, 2.2 px: the shaft from 12 px behind, the moon, the tip's line), on the glance orbit; unset,
 * the dots alone. Colours at most 104 per channel. */
static void f13_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    (void)ctx;
    fill(fb, stride, clip, 0);
    const float ox = C + (float)in->odx, oy = C + (float)in->ody;
    for (int h = 0; h < 12; h++) {
        float s, c;
        fk_sincos((float)h * (PI_F / 6.f), &s, &c);
        fk_disc(fb, stride, &clip, ox + 196.f * s, oy - 196.f * c, h % 3 ? 1.8f : 3.f, RGB(0x44, 0x48, 0x4e), 255);
    }
    if (!in->clk.set) return;
    const uint16_t ink = RGB(0x5c, 0x61, 0x68);
    for (int hand = 0; hand < 2; hand++) {
        const float a = hand ? minute_angle(in) : hour_angle(in), len = hand ? 170.f : 112.f;
        float s, c;
        fk_sincos(a, &s, &c);
        /* local (0, ly) about (ox, oy), turned as canvas rotate() turns it */
        fk_line_max(fb, stride, &clip, ox - 12.f * s, oy + 12.f * c, ox + 0.72f * len * s, oy - 0.72f * len * c, 2.2f,
                    ink, 255);
        fk_ring_max(fb, stride, &clip, ox + 0.78f * len * s, oy - 0.78f * len * c, 0.07f * len, 2.2f, ink, 255);
        fk_line_max(fb, stride, &clip, ox + 0.85f * len * s, oy - 0.85f * len * c, ox + len * s, oy - len * c, 2.2f, ink,
                    255);
    }
    if (in->clk.stale)   /* the stale lamp, dim: inside the marker's rect on every orbit offset */
        fk_disc(fb, stride, &clip, 181.f + (float)in->odx, STALE_Y + (float)in->ody, 2.2f, RGB(96, 64, 22), 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t f13_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced)
        return full(out);   /* the mode, the clock's state, the orbit, reduced motion */
    if (moving(a) || moving(b)) return full(out);   /* the lid, the light */
    if (!b->clk.set) return 0;                      /* nothing moves on an unset clock */
    if (a->clk.h != b->clk.h || a->clk.m != b->clk.m) {   /* the minute: the hands' old and new boxes */
        if (b->glance) return full(out);
        dw_hand_breguet_bbox(hour_angle(a), true, &out[0]);
        dw_hand_breguet_bbox(minute_angle(a), false, &out[1]);
        dw_hand_breguet_bbox(hour_angle(b), true, &out[2]);
        dw_hand_breguet_bbox(minute_angle(b), false, &out[3]);
        out[4] = DW_F13_SUB;
        return 5;
    }
    if (b->glance) return 0;   /* nothing moves in the glance within a minute */
    if (quarter(a) == quarter(b)) return 0;
    out[0] = DW_F13_SUB;
    return 1;
}

/* a tap anywhere on the screen (inclusive at the edge): the case snaps shut and opens again */
static fk_tap_t f13_tap(const void *ctx, int16_t x, int16_t y)
{
    (void)ctx;
    return x >= 0 && x < W && y >= 0 && y < W ? FK_TAP_PULSE : FK_TAP_NONE;
}

const fk_face_ops_t DW_FACE_FOB = {
    .key = "fob",
    .ctx_bytes = sizeof(f13_t),
    .cadence_ms = 40,
    .bind = f13_bind,
    .draw = f13_draw,
    .glance = f13_glance,
    .dirty = f13_dirty,
    .tap = f13_tap,
    .reveal = false,
    .wake_len_ms = WAKE_LEN,
    .pulse_len_ms = PULSE_LEN,
    .pulse_guard_ms = DW_F13_CLOSE_MS,   /* R148 (a): a tap while the lid closes is ignored (:928) */
};
