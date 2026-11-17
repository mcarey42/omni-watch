/* The airlock (rd/airlock.h; rd wave 2 Task 4, spec section 10.2; faces-reveal.js buildAirDoors() 524-550, leafProj()
 * and drawLeaf() 571-590, airR() 591-611, frame_airlock() 689-729): the timeline, the dust, and RD_REVEAL_AIRLOCK, the
 * reveal on fk_view's direct path through face_kit's column renderer (fk/cols.h), with the airlock's own leaf look
 * (RD_AIR_LOOK, W2-D3). It lives here in rd_core, so Bay Mouth names it and the host links it from rd_core alone.
 *
 * The leaf (RW6): bind composes it once into a 466 x 466 RGB565 leaf in the reveal's PSRAM context: rd_by_doors's A8
 * through RD_BY_DOOR_RAMP, then rd_by_kick's rows 312-361 (the chevron kick plate), so the band reads one leaf pixel per
 * pixel. The lamp's housing is in the leaf; the lamp itself is drawn per frame.
 *
 * A band, in order (W2-D4: the shake lives here, so fk_cols_band is unchanged):
 *   0. the band's rows whose scene row (the screen row less sy) falls outside 0-465 are black (fk_cols_band leaves rows
 *      outside the screen untouched);
 *   1. fk_cols_band over the band's rect moved up by sy rows, written straight into out (its layout is relative to its
 *      band's y0), with the state's fade at 255 and its xfade the doors' weight (I7);
 *   2. in the same scene coordinates: the starlight through the gap ('lighter'), the lamp (a disc r 5, then its glow r
 *      26) and the beacon's wash ('lighter') while theta < 12 deg, and the dust (additive soft tables); then black off
 *      the panel again (the glows reach past the rim);
 *   3. each row moved by sx in place, the columns it uncovers black;
 *   4. the opening fade (fade8), once, over the whole composed band.
 * The band is the whole screen's width (fk_view's bands are): step 3 moves whole rows. Nothing is held on the stack
 * beyond a few scalars.
 *
 * The plan: the first frame draws the whole interior; then every frame of the reveal's time draws the columns the leaves
 * do not cover whole at full weight (the band reads the interior nowhere else, and the shake moves the composed band,
 * never the reading), all of them once the leaves are done; Bay Mouth's own dirty() is the whole frame through its wake
 * (its labels, drop and caption move with wake_ms), so this is that rect cut to what the frame shows, and every clock
 * change (a ring step, a star shift, M7) lands in it. The settled end, a tap's end frame and reduced motion's cross-fade
 * draw the settled face whole once and reuse it.
 *
 * Pure: no esp_*, freertos or LVGL; no allocation; no libm here (fk_sincos for every angle, a const table for every
 * exponential; the column renderer's trig is face_kit's, inside fk_cols_k and fk_cols_free_edge). */
#include "rd/airlock.h"
#include <string.h>
#include "fk/compose.h"
#include "fk/ease.h"
#include "fk/shapes.h"
#include "omni/blend565.h"
#include "rd/assets_meta.h"
#include "rd/faces.h"

#define W 466
#define C 233
#define D2R 0.017453292519943295f
#define TAU_F 6.28318530718f
#define PI_F 3.14159265358979f
#define F_LEAF 560.0f
#define LAMP_D 203.0f            /* the lamp housing's distance from the left hinge (C - 30) */
#define NEAR_RAD (12.0f * D2R)   /* the lamp and the beacon while the leaves are near shut */
#define C_DUST RD_RGB(150, 140, 128)

const fk_cols_look_t RD_AIR_LOOK = { 0.7f, 0.08f, 0.8f, 0.5f, 6, { 255, 110, 60 }, 12.0f, { 0x12, 0x14, 0x16 },
                                     { 255, 120, 64 }, 0.6f };
/* an outward kind never reads its kept trig (fk/cols.h) */
const fk_cols_kind_t RD_AIR_KIND = { false, false, 84.0f, 7.0f, 0.0f, 0.0f, &RD_AIR_LOOK };

const rd_ramp_stop_t RD_BY_DOOR_RAMP[RD_BY_DOOR_RAMP_STOPS] = {
    {0, 0x00, 0x00, 0x00},   {80, 0x1A, 0x1D, 0x20},  {96, 0x30, 0x35, 0x3A},  {112, 0x5A, 0x60, 0x68},
    {127, 0x8A, 0x90, 0x96}, {128, 0x24, 0x28, 0x2C}, {191, 0xDC, 0xD3, 0xBE}, {192, 0x24, 0x28, 0x2C},
    {255, 0xE2, 0xA6, 0x2A},
};

/* ---- the timeline (airR) */

/* e^-x for x = k / 16, k = 0 .. 96 (lesson 6: a const table, never expf), read with a linear step between entries */
static const float EXP_NEG[97] = {
    1.0f, 0.939413063f, 0.882496903f, 0.829029118f, 0.778800783f, 0.731615629f, 0.687289279f, 0.645648526f,
    0.60653066f, 0.569782825f, 0.535261429f, 0.502831578f, 0.472366553f, 0.44374731f, 0.41686202f, 0.391605627f,
    0.367879441f, 0.345590753f, 0.324652467f, 0.304982769f, 0.286504797f, 0.269146349f, 0.252839596f, 0.237520819f,
    0.22313016f, 0.209611387f, 0.196911675f, 0.1849814f, 0.173773943f, 0.163245512f, 0.153354967f, 0.144063659f,
    0.135335283f, 0.127135733f, 0.119432968f, 0.112196891f, 0.105399225f, 0.0990134084f, 0.0930144892f,
    0.0873790262f, 0.0820849986f, 0.07711172f, 0.072439757f, 0.068050854f, 0.0639278612f, 0.0600546679f,
    0.0564161395f, 0.0529980584f, 0.0497870684f, 0.0467706224f, 0.0439369336f, 0.0412749294f, 0.0387742078f,
    0.0364249973f, 0.0342181183f, 0.0321449473f, 0.0301973834f, 0.0283678164f, 0.0266490973f, 0.0250345101f,
    0.0235177459f, 0.0220928777f, 0.0207543379f, 0.0194968961f, 0.0183156389f, 0.0172059504f, 0.0161634946f,
    0.015184198f, 0.0142642339f, 0.0134000077f, 0.0125881422f, 0.0118254653f, 0.0111089965f, 0.0104359365f,
    0.00980365504f, 0.0092096816f, 0.0086516952f, 0.00812751549f, 0.00763509422f, 0.00717250725f, 0.006737947f,
    0.00632971543f, 0.00594621736f, 0.00558595426f, 0.0052475184f, 0.00492958733f, 0.00463091873f, 0.00435034555f,
    0.00408677144f, 0.00383916647f, 0.00360656314f, 0.00338805252f, 0.0031827808f, 0.00298994586f, 0.00280879419f,
    0.00263861796f, 0.00247875218f,
};

static float exp_neg(float x)   /* x >= 0 */
{
    const float u = x * 16.0f;
    if (!(u < 96.0f)) return EXP_NEG[96];
    const int k = (int)u;
    const float f = u - (float)k;
    return EXP_NEG[k] + (EXP_NEG[k + 1] - EXP_NEG[k]) * f;
}

static float sin_of(float a)
{
    float s, c;
    fk_sincos(a, &s, &c);
    return s;
}

static float cos_of(float a)
{
    float s, c;
    fk_sincos(a, &s, &c);
    return c;
}

static float out_quart(float s) { const float u = 1 - s; return 1 - u * u * u * u; }
static int8_t round8(float v) { return (int8_t)(v >= 0 ? (int)(v + 0.5f) : -(int)(-v + 0.5f)); }
static int16_t round16(float v) { return (int16_t)(v >= 0 ? (int)(v + 0.5f) : -(int)(-v + 0.5f)); }

void rd_air_skip(rd_air_state_t *st)
{
    memset(st, 0, sizeof *st);
    const float open = RD_AIR_KIND.open_deg * D2R;
    st->cols = (fk_cols_state_t){ .th_l = open, .th_r = open, .fade = 255, .xfade = 255, .tail = 255, .done = true };
    st->fade8 = 255;
    st->settled = true;
    st->wake_ms = RD_AIR_MS;
}

void rd_air_at(uint32_t t_ms, bool reduced, rd_air_state_t *st)
{
    /* t_ms is an elapsed time from the reveal's start, not a timestamp: plain comparisons are right */
    const float t = (float)t_ms;
    memset(st, 0, sizeof *st);
    st->wake_ms = t_ms;
    if (reduced) {   /* the shut doors cross-fade off the settled face: no swing, shake, dust, lamp or beacon */
        const uint8_t x = fk_ease_to8(1.0f - fk_ease_out_cubic(fk_ease_ph(t, 0, RD_AIR_FADE_MS)));
        st->cols = (fk_cols_state_t){ .fade = 255, .xfade = x, .tail = 255, .leaves = x > 0,
                                      .done = t_ms >= RD_AIR_FADE_MS };
        st->fade8 = 255;
        st->settled = t_ms >= RD_AIR_FADE_MS;
        return;
    }
    /* theta: the crack, the jam's shudder, the kick */
    float th;
    if (t_ms < 1080) {
        th = 7.0f * D2R * fk_ease_out_cubic(fk_ease_ph(t, 450, 600));
        if (t_ms > 600) {
            const float u = t - 600.0f;
            th += 1.6f * D2R * sin_of(u / 19.0f) * exp_neg(u / 120.0f);
        }
    } else {
        th = 7.0f * D2R + out_quart(fk_ease_ph(t, 1080, RD_AIR_DONE_MS)) * (RD_AIR_KIND.open_deg - 7.0f) * D2R;
    }
    const bool done = t_ms > RD_AIR_DONE_MS;
    st->cols = (fk_cols_state_t){ .th_l = th, .th_r = th, .fade = 255, .xfade = 255,
                                  .tail = fk_ease_to8(fk_ease_ph(t, 1080, RD_AIR_DONE_MS)),
                                  .leaves = !done && fk_cols_leaf_on_screen_k(&RD_AIR_KIND, th), .done = done };
    /* the shake: the jam's, then the kick's bang */
    if (t_ms > 600 && t_ms < 900) {
        const float k = 2.6f * exp_neg((t - 600.0f) / 70.0f);
        st->sx = round8(k * sin_of(t / 7.0f));
        st->sy = round8(k * cos_of(t / 9.0f));
    } else if (t_ms > 1080 && t_ms < 1400) {
        const float k = 4.5f * exp_neg((t - 1080.0f) / 80.0f);
        st->sx = round8(k * sin_of(t / 6.0f));
        st->sy = round8(k * cos_of(t / 8.0f) * 0.7f);
    }
    /* the lamp: red, then amber/off at 110 ms while it cracks, red/off at 80 ms through the jam, amber after */
    if (t_ms > 200 && t_ms < 600) st->lamp = (t_ms / 110u) % 2u ? 2 : 0;
    else if (t_ms >= 600 && t_ms < 1080) st->lamp = (t_ms / 80u) % 2u ? 1 : 0;
    else st->lamp = t_ms >= 1080 ? 2 : 1;
    st->beacon_a8 = fk_ease_to8(fk_ease_ph(t, 150, 300) * (1.0f - fk_ease_ph(t, 1300, 1600)));
    st->beacon_ang = t / 260.0f * TAU_F;
    /* the starlight through the gap, brightest while it is narrow */
    if (th >= 0.0005f) {
        const float xl = fk_cols_free_edge(&RD_AIR_KIND, th, false), gap = (float)W - 2.0f * xl;
        if (gap > 0.5f && th < RD_AIR_KIND.open_deg * D2R * 0.9f)
            st->gap_a8 = fk_ease_to8(0.28f * fk_ease_clampf(gap / 40.0f, 0, 1) *
                                     (1.0f - fk_ease_ph(th, 15.0f * D2R, 45.0f * D2R)));
    }
    st->fade8 = fk_ease_to8(fk_ease_ph(t, 0, 160));
    st->settled = t_ms >= RD_AIR_SETTLED_MS;
}

/* ---- the rim light (airR's light, outCubic(ph(t, 450, 700)); review I1). face_kit's leaf_light is clamp(theta /
 * pop_deg, 0, 1), and pop_deg is read nowhere else (not by the geometry, the gap or the edge's place), so a frame's
 * kind carries pop_deg = theta / L for the light L: the kind's own 7 while the leaves are shut (theta 0, no light), a
 * pop far below any theta once L is 1 */
void rd_air_kind_at(uint32_t t_ms, float th, fk_cols_kind_t *k)
{
    *k = RD_AIR_KIND;
    if (!(th > 0)) return;
    const float L = fk_ease_out_cubic(fk_ease_ph((float)t_ms, 450, 700));
    if (L >= 1.0f) k->pop_deg = 1e-6f;
    else if (L > 0) k->pop_deg = th / D2R / L;
}

/* ---- the dust (:722-726): puff(p, pts, size, a) */
typedef struct { int16_t x, y, vx, vy; } pt_t;
static const pt_t JAM[4] = { {C, 20, -20, 30}, {C, 446, 18, -26}, {C, 312, 30, 0}, {C, 312, -30, 6} };
static const pt_t KICK[5] = { {C - 60, 120, -90, 10}, {C + 60, 330, 90, -10}, {C, 230, 0, 0}, {C - 30, 400, -70, -20},
                              {C + 40, 60, 80, 20} };

static int puffs(float p, const pt_t *pts, int n, float size, float a, rd_dust_t *out)
{
    if (!(p > 0) || !(p < 1)) return 0;
    const float e = fk_ease_out_cubic(p);
    const uint8_t a8 = fk_ease_to8(a * sin_of(PI_F * p));
    if (!a8) return 0;
    const float s = size * (0.4f + e);
    int r = (int)(s / 8.0f + 0.5f) * 8;   /* M6: to 8 px steps, 16 at least */
    r = r < 16 ? 16 : r;
    for (int i = 0; i < n; i++)
        out[i] = (rd_dust_t){ round16((float)pts[i].x + (float)pts[i].vx * e),
                              round16((float)pts[i].y + (float)pts[i].vy * e), (uint8_t)r, a8 };
    return n;
}

int rd_air_dust(uint32_t t_ms, rd_dust_t out[RD_AIR_DUST_MAX])
{
    const float t = (float)t_ms;
    int n = puffs(fk_ease_ph(t, 600, 1350), JAM, 4, 26.0f, 0.35f, out);
    n += puffs(fk_ease_ph(t, 1080, 1700), KICK, 5, 44.0f, 0.3f, out + n);
    return n;
}

/* ---- the lamp (:710-713) */
float rd_air_lamp_x(float th)
{
    float sn, ct;
    fk_sincos(th, &sn, &ct);
    const float s = F_LEAF / (F_LEAF - LAMP_D * sn);
    return (float)C + (LAMP_D * ct - (float)C) * s;
}

void rd_air_lamp_draw(uint16_t *fb, omni_rect_t clip, float th, uint8_t lamp, int dx, int dy)
{
    if (!lamp || !(th < NEAR_RAD)) return;
    const uint16_t col = lamp == 1 ? RD_C_LAMP : RD_C_AMBER;
    const float lx = rd_air_lamp_x(th) + (float)dx;
    fk_disc(fb, W, &clip, lx, (float)(RD_AIR_LAMP_Y + dy), 5.0f, col, 255);
    fk_glow_add_lut(fb, W, &clip, (int)(lx + 0.5f), RD_AIR_LAMP_Y + dy, 26, rd_soft_lut(26), col, fk_ease_to8(0.7f));
}

/* ---- RD_REVEAL_AIRLOCK */
#define DUST_LUTS 7   /* r 16 .. 64 every 8 */
typedef struct {
    uint16_t leaf[W * W];                   /* the composed doors (RW6) */
    fk_cols_kind_t kind;                    /* RD_AIR_KIND with this frame's rim light (rd_air_kind_at) */
    fk_col_t cols[W];
    rd_air_state_t st;                      /* the last frame's state: the bands read it */
    int ndust;
    rd_dust_t dust[RD_AIR_DUST_MAX];
    const uint8_t *dust_lut[DUST_LUTS];
    uint8_t star[W][3], beacon[W][3];       /* the starlight's and the beacon's additions, per column (8-bit) */
    int16_t star0, star1, beacon0, beacon1; /* their columns [x0, x1) */
    bool near;                              /* theta < 12 deg: the lamp and the beacon */
    bool first;                             /* the first frame after bind draws the whole interior */
    bool settled_drawn;                     /* the interior holds the settled face */
    int32_t prev_t;                         /* the last frame's time (-1: none yet), for the timing */
} air_t;

static bool img_of(fk_asset_fn get, const char *name, uint8_t cf, int w, int h, fk_img_t *img)
{
    size_t n = 0;
    const void *b = get(name, &n);
    return b && fk_img_from_bin(img, b, n) && img->cf == cf && img->w == w && img->h == h;
}

static bool air_bind(const void *arg, const char *asset, fk_asset_fn get, void *rctx)
{
    (void)arg, (void)asset;   /* one pair of doors, Bay Mouth's (it sets no reveal_asset) */
    air_t *r = rctx;
    fk_img_t doors, kick;
    if (!img_of(get, ASSET_RD_BY_DOORS, FK_CF_A8, W, W, &doors) ||
        !img_of(get, ASSET_RD_BY_KICK, FK_CF_RGB565, W, ASSET_RD_BY_KICK_H, &kick))
        return false;
    for (int i = 0; i < DUST_LUTS; i++)
        if (!(r->dust_lut[i] = rd_soft_lut(16 + 8 * i))) return false;
    if (!rd_soft_lut(26)) return false;
    uint16_t ramp[256];
    rd_ramp565(RD_BY_DOOR_RAMP, RD_BY_DOOR_RAMP_STOPS, ramp);
    for (int i = 0; i < W * W; i++) r->leaf[i] = ramp[doors.a[i]];
    memcpy(&r->leaf[RD_BY_KICK_Y * W], kick.rgb, (size_t)W * ASSET_RD_BY_KICK_H * 2);
    r->kind = RD_AIR_KIND;
    r->first = true;
    r->settled_drawn = false;
    r->prev_t = -1;
    r->ndust = 0;
    return true;
}

static int window_of(uint32_t t)
{
    return t < RD_AIR_WIN_SHUT ? 0 : t < RD_AIR_WIN_JAM ? 1 : t < RD_AIR_DONE_MS ? 2 : 3;
}

/* the per-column additions: the starlight (:700-708; a gradient across [xl - 18, xr + 18], transparent (255, 110, 50) at
 * its ends and (255, 150, 90) at a = gap_a8 at its middle, interpolated unpremultiplied) and the beacon's wash (:714-720;
 * amber (255, 168, 58) at .16 k at C + 300 sin(beacon_ang), falling to 0 at 120 px either side) */
static void column_adds(air_t *r, float th)
{
    const rd_air_state_t *st = &r->st;
    memset(r->star, 0, sizeof r->star);
    memset(r->beacon, 0, sizeof r->beacon);
    r->star0 = r->star1 = r->beacon0 = r->beacon1 = 0;
    if (st->gap_a8) {
        const float xl = fk_cols_free_edge(&RD_AIR_KIND, th, false), x0 = xl - 18.0f, wd = (float)W - 2.0f * xl + 36.0f;
        const float peak = (float)st->gap_a8 / 255.0f;
        int a = W, z = 0;
        for (int x = 0; x < W; x++) {
            const float u = ((float)x + 0.5f - x0) / wd;
            if (!(u > 0) || !(u < 1)) continue;
            const float v = u < 0.5f ? u * 2.0f : (1.0f - u) * 2.0f, al = peak * v;
            const float g = 110.0f + 40.0f * v, b = 50.0f + 40.0f * v;
            r->star[x][0] = (uint8_t)(255.0f * al + 0.5f);
            r->star[x][1] = (uint8_t)(g * al + 0.5f);
            r->star[x][2] = (uint8_t)(b * al + 0.5f);
            a = x < a ? x : a, z = x + 1;
        }
        if (a < z) r->star0 = (int16_t)a, r->star1 = (int16_t)z;
    }
    if (r->near && st->beacon_a8) {
        float s, c;
        fk_sincos(st->beacon_ang, &s, &c);
        const float k = (float)st->beacon_a8 / 255.0f * (0.5f + 0.5f * (c > 0 ? c : 0)), peak = 0.16f * k;
        const float xc = (float)C + s * 300.0f;
        int a = W, z = 0;
        for (int x = 0; x < W; x++) {
            float d = ((float)x + 0.5f - xc) / 120.0f;
            d = d < 0 ? -d : d;
            if (!(d < 1)) continue;
            const float al = peak * (1.0f - d);
            r->beacon[x][0] = (uint8_t)(255.0f * al + 0.5f);
            r->beacon[x][1] = (uint8_t)(168.0f * al + 0.5f);
            r->beacon[x][2] = (uint8_t)(58.0f * al + 0.5f);
            a = x < a ? x : a, z = x + 1;
        }
        if (a < z) r->beacon0 = (int16_t)a, r->beacon1 = (int16_t)z;
    }
}

static void air_frame(const void *arg, void *rctx, uint32_t t_ms, bool skip, bool reduced, fk_rframe_t *out)
{
    (void)arg;
    air_t *r = rctx;
    rd_air_state_t *st = &r->st;
    if (r->prev_t >= 0 && (uint32_t)r->prev_t <= t_ms)   /* the last frame's wall time, by its window */
        rd_rel_timing_note(window_of((uint32_t)r->prev_t), t_ms - (uint32_t)r->prev_t);
    r->prev_t = skip ? -1 : (int32_t)t_ms;
    if (skip) rd_air_skip(st);
    else rd_air_at(t_ms, reduced, st);
    r->ndust = 0;
    r->near = false;
    r->star0 = r->star1 = r->beacon0 = r->beacon1 = 0;
    const bool first = r->first;
    r->first = false;
    out->settled = st->settled;
    out->live = !reduced;
    out->need = (omni_rect_t){ 0, 0, W, W };
    if (st->settled || reduced || skip) {   /* the settled face: drawn whole on the first such frame, then reused */
        out->wake_ms = UINT32_MAX;
        r->kind = RD_AIR_KIND;
        if (reduced) fk_cols_k(&r->kind, 0, 0, r->cols);   /* the shut doors (an identity map) */
        if (r->settled_drawn) out->need = (omni_rect_t){ 0, 0, 0, 0 };
        r->settled_drawn = true;
        return;
    }
    r->settled_drawn = false;   /* this frame draws the reveal's time into the interior */
    out->wake_ms = t_ms;
    const float th = st->cols.th_l;
    if (!st->cols.done) {
        rd_air_kind_at(t_ms, th, &r->kind);
        fk_cols_k(&r->kind, th, th, r->cols);
        /* the columns the leaves cover whole at full weight run from each hinge (fk_cols_band reads no interior there):
         * the rest is the need */
        int cx0 = 0, cx1 = W;
        while (cx0 < cx1 && r->cols[cx0].u >= 0 && r->cols[cx0].u < W && r->cols[cx0].inv_s_q16 <= 65536u) cx0++;
        while (cx1 > cx0 && r->cols[cx1 - 1].u >= 0 && r->cols[cx1 - 1].u < W && r->cols[cx1 - 1].inv_s_q16 <= 65536u)
            cx1--;
        if (!first)
            out->need = cx0 < cx1 ? (omni_rect_t){ (int16_t)cx0, 0, (int16_t)cx1, W } : (omni_rect_t){ 0, 0, 0, 0 };
        r->near = th < NEAR_RAD;
    }
    column_adds(r, th);
    r->ndust = rd_air_dust(t_ms, r->dust);
}

static void panel_of(int y, int *p0, int *p1)
{
    fk_panel_row(y, p0, p1);
}

/* d plus (r, g, b) per 8-bit channel, saturating ('lighter') */
static inline uint16_t add8(uint16_t d, const uint8_t a[3])
{
    const int R = omni_ex_r(d) + a[0], G = omni_ex_g(d) + a[1], B = omni_ex_b(d) + a[2];
    return omni_pack565(R < 255 ? R : 255, G < 255 ? G : 255, B < 255 ? B : 255);
}

/* one column-addition pass over scene rows [c.y0, c.y1) of fb (the pixel at (0, c.y0)), on the panel */
static void add_cols(const uint8_t (*add)[3], int x0, int x1, omni_rect_t c, uint16_t *fb)
{
    if (x0 >= x1) return;
    for (int y = c.y0; y < c.y1; y++) {
        uint16_t *o = fb + (size_t)(y - c.y0) * W;
        int p0, p1;
        panel_of(y, &p0, &p1);
        const int a = x0 > p0 ? x0 : p0, z = x1 < p1 ? x1 : p1;
        for (int x = a; x < z; x++) o[x] = add8(o[x], add[x]);
    }
}

static void air_band(const void *arg, const void *rctx, const uint16_t *interior, omni_rect_t band, uint16_t *out)
{
    (void)arg;
    const air_t *r = rctx;
    const rd_air_state_t *st = &r->st;
    const int y0 = band.y0 > 0 ? band.y0 : 0, y1 = band.y1 < W ? band.y1 : W;
    if (y0 >= y1) return;
    uint16_t *ob = out + (ptrdiff_t)(y0 - band.y0) * W;   /* screen row y0, column x at offset x */
    const int sx = st->sx, sy = st->sy;
    /* 0. the rows the shake brings in from outside the scene: black */
    for (int y = y0; y < y1; y++)
        if (y - sy < 0 || y - sy >= W) memset(ob + (size_t)(y - y0) * W, 0, (size_t)W * 2);
    /* 1. the doors over the interior, in scene rows (fk_cols_band clips to the screen and keeps its band's layout) */
    const omni_rect_t sb = { 0, (int16_t)(y0 - sy), W, (int16_t)(y1 - sy) };
    fk_cols_band(&r->kind, &st->cols, r->cols, r->leaf, interior, NULL, sb, ob);
    /* 2. the light, the lamp, the beacon and the dust, in the same scene rows */
    const omni_rect_t c = { 0, (int16_t)(sb.y0 > 0 ? sb.y0 : 0), W, (int16_t)(sb.y1 < W ? sb.y1 : W) };
    if (c.y0 < c.y1 && (r->star0 < r->star1 || r->near || r->ndust)) {
        uint16_t *fb = ob + (ptrdiff_t)(c.y0 - sb.y0) * W;   /* scene row c.y0 */
        add_cols((const uint8_t (*)[3])r->star, r->star0, r->star1, c, fb);
        if (r->near) rd_air_lamp_draw(fb, c, st->cols.th_l, st->lamp, 0, 0);
        add_cols((const uint8_t (*)[3])r->beacon, r->beacon0, r->beacon1, c, fb);
        for (int i = 0; i < r->ndust; i++) {
            const rd_dust_t *d = &r->dust[i];
            fk_glow_add_lut(fb, W, &c, d->x, d->y, d->r, r->dust_lut[(d->r - 16) / 8], C_DUST, d->a8);
        }
        for (int y = c.y0; y < c.y1; y++) {   /* off the panel: black again (the glows reach past its rim) */
            uint16_t *o = fb + (size_t)(y - c.y0) * W;
            int p0, p1;
            panel_of(y, &p0, &p1);
            memset(o, 0, (size_t)p0 * 2);
            memset(o + p1, 0, (size_t)(W - p1) * 2);
        }
    }
    /* 3. the shake's columns: each row moved by sx, the columns it uncovers black */
    if (sx)
        for (int y = y0; y < y1; y++) {
            uint16_t *o = ob + (size_t)(y - y0) * W;
            if (sx > 0) {
                memmove(o + sx, o, (size_t)(W - sx) * 2);
                memset(o, 0, (size_t)sx * 2);
            } else {
                memmove(o, o - sx, (size_t)(W + sx) * 2);
                memset(o + W + sx, 0, (size_t)(-sx) * 2);
            }
        }
    /* 4. the opening fade, once (omni_blend565(0, q, fade8): the black destination's terms are 0) */
    if (st->fade8 != 255) {
        const uint32_t fp = (uint32_t)omni_amul(st->fade8);
        for (int y = y0; y < y1; y++) {
            uint16_t *o = ob + (size_t)(y - y0) * W;
            for (int x = 0; x < W; x++) {
                const uint16_t q = o[x];
                const uint32_t rb = ((uint32_t)omni_ex_r(q) << 16 | (uint32_t)omni_ex_b(q)) * fp;
                const uint32_t g = (uint32_t)omni_ex_g(q) * fp;
                o[x] = (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
            }
        }
    }
}

const fk_reveal_ops_t RD_REVEAL_AIRLOCK = {
    .len_ms = RD_AIR_MS, .reduced_ms = RD_AIR_FADE_MS, .pop_ms = 0,
    .pop_asset = NULL,   /* no latch sound */
    .rctx_bytes = sizeof(air_t),
    .bind = air_bind, .frame = air_frame, .band = air_band,
};
