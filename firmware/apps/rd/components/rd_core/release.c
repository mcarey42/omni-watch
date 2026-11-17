/* Stasis Release (rd/release.h; rd wave 1 Task 10, spec sections 9.1-9.3; faces-reveal.js stasisR() 444-458, drawLid()
 * 460-479, frame_stasis() 480-517): the timeline, the mist's paths, the counter's text, and RD_REVEAL_STASIS, the
 * reveal on fk_view's direct path through face_kit's row renderer (fk/rows.h). It lives here in rd_core (PR12), so
 * the Stasis Clock names it and the host links it from rd_core alone.
 *
 * The lid (RW6): bind composes it once into a 466 x 466 RGB565 leaf in the reveal's PSRAM context: rd_st_lid's A8
 * through the lid's ramp, the window's glass (its recipe, glass_at) with rd_st_frost's A8 in rd.frost over it,
 * rd_st_haz's rows 352-381, so the band reads
 * one leaf pixel per pixel. The counter recess (rd_st_lidf's crop, 190 x 52 at 138, 262) is kept in both versions,
 * the shut one and the final one (its printed 3 000 000 YRS in amber and its lamp: go-green with the clock set, the
 * lamp's red without, since the lamp stays red then); a frame puts the version its time shows into the leaf.
 *
 * A band, in order (spec section 9.2): the interior (the Stasis Clock at the reveal's time, settling and thawing), the
 * cold flash (additive) and the mist (additive glows from the const tables at the quantised radii, RW9); the lid's rows
 * through fk_rows_band in place (its interior NULL: the band holds the interior, flash and mist already), its edge's
 * quad and rim; before the swing the counter, the lamp and the stasis field's shimmer over the shifted leaf (theta 0:
 * leaf rows map 1:1 with dy); then the fade. After 1620 ms no lid is drawn at all (PR11). Reduced (and the Reveal
 * row's Fade): the final lid cross-faded over the settled face, nothing else.
 *
 * The clock's state at the reveal: the reveal ops see no face input, so the theme says it before the reveal binds
 * (rd_rel_set_clock from rd_theme.c's woke hook, which fk_face calls before the reveal starts); bind latches it.
 *
 * Pure: no esp_*, freertos or LVGL; no allocation; no libm here (fk_sincos for every angle; the hinge engine's trig is
 * omni_core's, inside fk_rows_map). */
#include "rd/release.h"
#include <string.h>
#include "fk/compose.h"
#include "fk/cols.h"   /* fk_panel_row */
#include "fk/ease.h"
#include "fk/shapes.h"
#include "omni/blend565.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "rd/faces.h"

#define W 466
#define C 233
#define PI_F 3.14159265358979f
#define D2R 0.017453292519943295f
#define F_LID 560.0f

/* the counter recess (rd_st_lidf's crop) and the window (rd_st_frost's) */
#define LF_X 138
#define LF_Y 262
#define LF_W 190
#define LF_H 52
#define FR_X 108
#define FR_Y 96
#define FR_W 250
#define FR_H 150
#define HZ_Y 352
#define HZ_H 30
#define LAMP_X 311
#define LAMP_Y 276
#define SHIMMER_Y 178   /* WIN.y + WIN.h x .55 = 178.5, on a whole pixel */
#define C_LAMP RD_RGB(0xFF, 0x3B, 0x24)
#define C_MIST RD_RGB(190, 215, 200)

const omni_hinge_t RD_LID_HINGE = { OMNI_HINGE_HORIZONTAL, 0.0f, F_LID, W, 0.0f, 0.0f };
const rd_ramp_stop_t RD_LID_RAMP[RD_LID_RAMP_STOPS] = {
    {0, 0x00, 0x00, 0x00},   {32, 0x0A, 0x0B, 0x0C},  {64, 0x14, 0x16, 0x18},  {176, 0x2B, 0x2F, 0x34},
    {208, 0x5A, 0x60, 0x68}, {232, 0x8A, 0x90, 0x96}, {255, 0xDC, 0xD3, 0xBE},
};

void rd_lid_ramp(uint16_t out[256]) { rd_ramp565(RD_LID_RAMP, RD_LID_RAMP_STOPS, out); }

void rd_ramp565(const rd_ramp_stop_t *stops, int ns, uint16_t out[256])
{
    for (int k = 0; k + 1 < ns; k++) {
        const rd_ramp_stop_t a = stops[k], b = stops[k + 1];
        const int n = b.i - a.i, last = k + 2 == ns;
        for (int i = a.i; i < b.i || (last && i == b.i); i++) {
            const int r = (a.r * (b.i - i) + b.r * (i - a.i) + n / 2) / n;
            const int g = (a.g * (b.i - i) + b.g * (i - a.i) + n / 2) / n;
            const int bl = (a.b * (b.i - i) + b.b * (i - a.i) + n / 2) / n;
            /* to RGB565 rounded (not RD_RGB's truncation: on the lid's dark steel truncation drops red and blue
             * by up to a step more than green, a green cast) */
            out[i] = (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((bl * 31 + 127) / 255));
        }
    }
}

/* ---- the timeline (stasisR) */
static float out_quart(float s) { const float u = 1 - s; return 1 - u * u * u * u; }
static int16_t round16(float v) { return (int16_t)(v >= 0 ? (int)(v + 0.5f) : -(int)(-v + 0.5f)); }

void rd_rel_skip(rd_rel_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->rows.theta = 67.0f * D2R;
    st->rows.fade = 255;
    st->counter = 3000000u;
    st->lamp = 2;
    st->wake_ms = RD_REL_MS;
    st->final_lid = st->lid_gone = st->done = true;
    st->door_a8 = 255;
}

void rd_rel_at(uint32_t t_ms, bool reduced, bool clock_set, rd_rel_state_t *st)
{
    /* t_ms is an elapsed time from the reveal's start, not a timestamp: plain comparisons are right */
    const float t = (float)t_ms;
    if (reduced) {   /* the final lid's cross-fade over the settled face: no counter, pop, mist or swing */
        memset(st, 0, sizeof *st);
        st->rows.fade = 255;
        st->counter = 3000000u;
        st->lamp = clock_set ? 2 : 1;
        st->wake_ms = t_ms;
        st->final_lid = true;
        st->reduced = true;
        st->door_a8 = fk_ease_to8(1.0f - fk_ease_out_cubic(fk_ease_ph(t, 0, RD_REL_FADE_MS)));
        st->done = t_ms >= RD_REL_FADE_MS;
        return;
    }
    if (t_ms >= RD_REL_MS) {
        rd_rel_skip(st);
        st->lamp = clock_set ? 2 : 1;
        st->wake_ms = t_ms;
        return;
    }
    memset(st, 0, sizeof *st);
    st->wake_ms = t_ms;
    st->door_a8 = 255;
    st->counter = t_ms >= 860 ? 3000000u : (uint32_t)(3000000.0f * out_quart(fk_ease_ph(t, 120, 860)) + 0.5f);
    st->final_lid = t_ms >= 880;
    st->lamp = st->final_lid && clock_set ? 2 : (uint8_t)((t_ms / 140u) % 2u);
    float s, c;
    fk_sincos(PI_F * fk_ease_ph(t, 900, 1010), &s, &c);
    st->rows.dy = t_ms < 900 || t_ms >= 1010 ? 0 : round16(3.5f * s);
    const float th = 67.0f * D2R * fk_ease_in_out_cubic(fk_ease_ph(t, 1010, RD_REL_GONE_MS));
    st->rows.theta = th;
    st->rows.fade = fk_ease_to8(fk_ease_ph(t, 0, 160));
    st->flash_a8 = fk_ease_to8(0.13f * fk_ease_out_cubic(fk_ease_ph(t, 1060, 1260)) *
                               (1.0f - fk_ease_in_out_cubic(fk_ease_ph(t, 1260, 1700))));
    st->lid_gone = t_ms > RD_REL_GONE_MS;
    if (th <= 0) {   /* shut: the stasis field shimmers behind the frost */
        fk_sincos(t / 90.0f, &s, &c);
        st->shimmer_a8 = fk_ease_to8(0.06f + 0.05f * (0.5f + 0.5f * s));
        return;
    }
    /* the swing's look (drawLid): the uniform shade, the lower edge's 10 px thickness quad and its cold-light line */
    float sn, ct;
    fk_sincos(th, &sn, &ct);
    st->rows.shade = fk_ease_to8(0.55f * (1.0f - ct));
    const float Wf = (float)W, T = 10.0f, Z = Wf * sn, s0 = F_LID / (F_LID - Z), y0 = C + (Wf * ct - C) * s0;
    const float Zb = Wf * sn - T * ct, Yb = Wf * ct + T * sn, sb = F_LID / (F_LID - Zb), y1 = C + (Yb - C) * sb;
    if (y1 > y0 && sn > 0.02f) {
        const float h = y1 - y0 + 0.5f;
        st->rows.edge_px = (uint8_t)(h < 1 ? 1 : h > 255 ? 255 : (int)h);
        st->rows.edge_rgb = RD_RGB(0x0C, 0x0E, 0x0F);
        st->rows.rim_rgb = RD_RGB(170, 230, 190);
        st->rows.rim_a8 = fk_ease_to8(0.55f * fk_ease_out_cubic(fk_ease_ph(th, 0, 0.5f)));
        st->rows.rim_px = 1.5f;
    }
}

/* ---- the mist (faces-reveal.js:483-492): the mockup's rng (mulberry32), seed 77, three draws a puff */
static float rng_next(uint32_t *seed)
{
    *seed += 0x6D2B79F5u;
    uint32_t t = (*seed ^ (*seed >> 15)) * (1u | *seed);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) / 16777216.0f;   /* [0, 1) in 24 bits */
}

int rd_rel_mist(uint32_t t_ms, rd_puff_t out[14])
{
    const float puff = fk_ease_ph((float)t_ms, 920, 1900);
    if (!(puff > 0) || !(puff < 1)) return 0;
    uint32_t seed = 77;
    int n = 0;
    for (int i = 0; i < 14; i++) {
        const float a = PI_F * (0.15f + 0.7f * rng_next(&seed)) + (i % 2 ? 0.0f : PI_F * 0.02f);
        const float dd = 70.0f + rng_next(&seed) * 70.0f;
        const float p = fk_ease_clampf((puff - rng_next(&seed) * 0.25f) / 0.75f, 0, 1);
        if (p <= 0) continue;
        float s, c;
        fk_sincos(a, &s, &c);
        const float d = 240.0f - dd * fk_ease_out_cubic(p);
        float sp, cp;
        fk_sincos(PI_F * p, &sp, &cp);
        const uint8_t a8 = fk_ease_to8(0.11f * sp);
        if (!a8) continue;
        const int ri = (int)(50.0f * p / 8.0f + 0.5f);   /* (80 + 100 p) / 2 = 40 + 50 p, to 8 px steps */
        out[n++] = (rd_puff_t){ round16(C + d * c * (i % 3 == 0 ? -1.0f : 1.0f)), round16(C + d * s * 0.9f),
                                (uint8_t)(ri > RD_MIST_N_RADII - 1 ? RD_MIST_N_RADII - 1 : ri), a8 };
    }
    return n;
}

int rd_rel_counter_str(uint32_t n, char out[16])
{
    /* the mockup's String(yrs).padStart(7, '0') grouped by threes (faces-reveal.js:497): always seven digits, so the
     * text keeps its width as it rolls; clamped to seven (review M1: out[16] holds 13 characters and the NUL) */
    if (n > 9999999u) n = 9999999u;
    char d[7];
    const int k = 7;
    for (int i = 0; i < k; i++) d[i] = (char)('0' + n % 10u), n /= 10u;
    int o = 0;
    for (int i = k - 1; i >= 0; i--) {
        out[o++] = d[i];
        if (i && i % 3 == 0) out[o++] = ' ';
    }
    memcpy(out + o, " YRS", 5);
    return o + 4;
}

/* ---- the clock at the reveal (see the header comment) */
static bool s_clock_set = true;
void rd_rel_set_clock(bool set) { s_clock_set = set; }

/* ---- the frame timing (review M5): each frame's wall time (the next frame's reveal time less its own, both the
 * view's elapsed ms) by the window its time falls in; the worker's frame op writes it, the console reads it after */
static rd_rel_timing_t s_timing;
void rd_rel_timing_reset(void) { memset(&s_timing, 0, sizeof s_timing); }
void rd_rel_timing_get(rd_rel_timing_t *out) { *out = s_timing; }
void rd_rel_timing_note(int w, uint32_t ms)
{
    if (w < 0 || w > 3) return;
    s_timing.frames[w]++;
    s_timing.ms[w] += ms;
    if (ms > s_timing.max_ms[w]) s_timing.max_ms[w] = ms;
}
static int window_of(uint32_t t)
{
    return t < 1010 ? 0 : t < RD_REL_GONE_MS ? 1 : t < 1900 ? 2 : 3;
}

/* ---- the ordered dither (review M4): a 4 x 4 Bayer matrix by screen pixel, so a band is the frame's */
static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* ---- RD_REVEAL_STASIS */
typedef struct {
    uint16_t leaf[W * W];                       /* the composed lid (RW6) */
    uint16_t recess[2][LF_W * LF_H];            /* the counter recess: shut, final */
    rd_atlas2_t vt28;                           /* the counter (used where filled, never copied) */
    const uint8_t *g14, *g16, *g120, *mist[RD_MIST_N_RADII];
    bool clock_set;
    uint8_t in_leaf;                            /* the recess version the leaf holds */
    /* the last frame's plan, for the bands */
    rd_rel_state_t st;
    fk_rows_state_t rs;                         /* st.rows at fade 255 (the band fades last itself) */
    fk_row_t rows[W];
    fk_rows_edge_t edge;
    int16_t cov0, cov1;                         /* the screen rows the leaf covers whole: [cov0, cov1) */
    uint16_t flash[3];                          /* the flash's 8-bit channels to add */
    char ctext[16];
    int16_t cx;                                 /* the counter's pen: right-aligned at RD_REL_COUNTER_X */
    int npuff;
    rd_puff_t puff[14];
    uint8_t puff_r[14];                         /* each puff's radius cut to where its addition reaches 0 (M5) */
    uint16_t sh_add[3][256];                    /* the shimmer's additions in 8.8 per table alpha (M4) */
    int32_t prev_t;                             /* the last frame's time (-1: none yet), for the timing */
    bool settled_drawn;                         /* the interior holds the settled face (the final review, M4) */
} rel_t;

/* The glass under the frost (tools/export/rd_layers.json's rd_st_lid row, "under"; buildStasisDoor():334-336, 350):
 * #060807, the stasis field's radial glow in phosphor (alpha .10 to r 10, falling to 0 at r 155 about (233, 178.5)) and
 * the window's sheen (white at .07 at its top-left corner, 0 from .35 of the way along its diagonal), inside the
 * window's rounded rect (250 x 150 at 108, 96, r 24, at pixel centres). rd_st_lid's ramp has no green, so bind lays the
 * glass from this recipe, then the frost over it. -1 outside the window. */
static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) v -= r + bit, r = (r >> 1) + bit;
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static int glass_at(int x, int y)
{
    const float px = (float)x + 0.5f, py = (float)y + 0.5f, x0 = FR_X, y0 = FR_Y, x1 = FR_X + FR_W, y1 = FR_Y + FR_H, rr = 24;
    if (px < x0 || px > x1 || py < y0 || py > y1) return -1;
    const float cx = px < x0 + rr ? x0 + rr : px > x1 - rr ? x1 - rr : px;
    const float cy = py < y0 + rr ? y0 + rr : py > y1 - rr ? y1 - rr : py;
    if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > rr * rr) return -1;
    float c[3] = {6, 8, 7};
    const int dx = 2 * x + 1 - 466, dy = 2 * y + 1 - 357;   /* doubled, about (233, 178.5) */
    const float d = (float)isqrt32((uint32_t)(dx * dx + dy * dy) * 256u) / 32.0f;
    const float ar = 0.10f * (1.0f - fk_ease_clampf((d - 10.0f) / 145.0f, 0, 1));
    static const float PH[3] = {124, 255, 158};
    for (int j = 0; j < 3; j++) c[j] = c[j] * (1 - ar) + PH[j] * ar;
    const float t = ((px - x0) * 250.0f + (py - y0) * 150.0f) / (250.0f * 250.0f + 150.0f * 150.0f);
    const float as = 0.07f * fk_ease_clampf(1.0f - (t > 0 ? t : 0) / 0.35f, 0, 1);
    for (int j = 0; j < 3; j++) c[j] = c[j] * (1 - as) + 255.0f * as;
    /* to RGB565 with the ordered dither (M4): the glass's faint radial would band into rings at five bits */
    const float t16 = ((float)BAYER4[y & 3][x & 3] + 0.5f) / 16.0f;
    int q[3];
    static const float MAXQ[3] = {31, 63, 31};
    for (int j = 0; j < 3; j++) {
        const int v = (int)(c[j] * MAXQ[j] / 255.0f + t16);
        q[j] = v > (int)MAXQ[j] ? (int)MAXQ[j] : v;
    }
    return q[0] << 11 | q[1] << 5 | q[2];
}

static bool img_of(fk_asset_fn get, const char *name, uint8_t cf, int w, int h, fk_img_t *img)
{
    size_t n = 0;
    const void *b = get(name, &n);
    return b && fk_img_from_bin(img, b, n) && img->cf == cf && img->w == w && img->h == h;
}

static bool rel_bind(const void *arg, const char *asset, fk_asset_fn get, void *rctx)
{
    (void)arg, (void)asset;   /* one lid, Stasis Clock's (it sets no reveal_asset) */
    rel_t *r = rctx;
    fk_img_t lid, lidf, haz, frost;
    if (!img_of(get, ASSET_RD_ST_LID, FK_CF_A8, W, W, &lid) || !img_of(get, ASSET_RD_ST_LIDF, FK_CF_A8, LF_W, LF_H, &lidf) ||
        !img_of(get, ASSET_RD_ST_HAZ, FK_CF_RGB565, W, HZ_H, &haz) ||
        !img_of(get, ASSET_RD_ST_FROST, FK_CF_A8, FR_W, FR_H, &frost))
        return false;
    size_t n = 0;
    const void *vb = get(ASSET_RD_F_VT28, &n);
    if (!vb || !rd_atlas2_from_bin(vb, n, &r->vt28)) return false;
    static const int MR[RD_MIST_N_RADII] = RD_MIST_RADII;
    r->g14 = rd_glow_lut(14), r->g16 = rd_glow_lut(16), r->g120 = rd_glow_lut(RD_SHIMMER_R);
    bool ok = r->g14 && r->g16 && r->g120;
    for (int i = 0; i < RD_MIST_N_RADII; i++) ok = ok && (r->mist[i] = rd_glow_lut(MR[i])) != NULL;
    if (!ok) return false;
    r->clock_set = s_clock_set;
    /* the lid through its ramp, the frost over the window, the hazard band's rows */
    uint16_t ramp[256];
    rd_lid_ramp(ramp);
    for (int i = 0; i < W * W; i++) r->leaf[i] = ramp[lid.a[i]];
    for (int y = 0; y < FR_H; y++)
        for (int x = 0; x < FR_W; x++) {
            const int g = glass_at(FR_X + x, FR_Y + y);
            if (g < 0) continue;   /* the window's frame, outside its rounded corners: the lid's */
            const uint8_t a = frost.a[y * FR_W + x];
            uint16_t *p = &r->leaf[(FR_Y + y) * W + FR_X + x];
            *p = a ? omni_blend565((uint16_t)g, RD_C_FROST, a) : (uint16_t)g;
        }
    memcpy(&r->leaf[HZ_Y * W], haz.rgb, (size_t)W * HZ_H * 2);
    /* the recess, shut and final: the final's tint go-green within 6 px of the lamp, amber elsewhere; unset (review
     * M3) the lamp stays the shut lid's dark housing there, and the band keeps drawing the live red blink over it */
    for (int y = 0; y < LF_H; y++)
        for (int x = 0; x < LF_W; x++) {
            const uint16_t p = r->leaf[(LF_Y + y) * W + LF_X + x];
            const int dx = 2 * (LF_X + x) + 1 - 2 * LAMP_X, dy = 2 * (LF_Y + y) + 1 - 2 * LAMP_Y;
            const bool at_lamp = dx * dx + dy * dy <= 4 * 36;
            r->recess[0][y * LF_W + x] = p;
            r->recess[1][y * LF_W + x] =
                at_lamp && !r->clock_set ? p : omni_blend565(p, at_lamp ? RD_C_GO : RD_C_AMBER, lidf.a[y * LF_W + x]);
        }
    r->in_leaf = 0;
    r->prev_t = -1;
    r->settled_drawn = false;
    return true;
}

static void put_recess(rel_t *r, uint8_t v)
{
    if (r->in_leaf == v) return;
    for (int y = 0; y < LF_H; y++) memcpy(&r->leaf[(LF_Y + y) * W + LF_X], &r->recess[v][y * LF_W], LF_W * 2);
    r->in_leaf = v;
}

static void rel_frame(const void *arg, void *rctx, uint32_t t_ms, bool skip, bool reduced, fk_rframe_t *out)
{
    (void)arg;
    rel_t *r = rctx;
    rd_rel_state_t *st = &r->st;
    if (r->prev_t >= 0 && (uint32_t)r->prev_t <= t_ms) {   /* the last frame's wall time, by its window (M5) */
        rd_rel_timing_note(window_of((uint32_t)r->prev_t), t_ms - (uint32_t)r->prev_t);
    }
    r->prev_t = skip ? -1 : (int32_t)t_ms;
    if (skip) rd_rel_skip(st), st->lamp = r->clock_set ? 2 : 1;
    else rd_rel_at(t_ms, reduced, r->clock_set, st);
    put_recess(r, st->final_lid ? 1 : 0);
    r->rs = st->rows;
    r->rs.fade = 255;
    r->cov0 = r->cov1 = 0;
    r->npuff = 0;
    out->settled = st->done;
    out->live = !reduced;
    out->need = (omni_rect_t){ 0, 0, W, W };
    out->wake_ms = st->done || st->reduced ? UINT32_MAX : t_ms;   /* the settled face; else the reveal's time */
    if (st->done || st->reduced) {   /* the settled face: drawn whole on the first such frame, then reused (M4) */
        if (r->settled_drawn) out->need = (omni_rect_t){ 0, 0, 0, 0 };
        r->settled_drawn = true;
        return;
    }
    r->settled_drawn = false;   /* this frame draws the reveal's time into the interior */
    const int ap = omni_amul(st->flash_a8);
    r->flash[0] = (uint16_t)((200 * ap) >> 8), r->flash[1] = (uint16_t)((255 * ap) >> 8);
    r->flash[2] = (uint16_t)((215 * ap) >> 8);
    r->npuff = rd_rel_mist(t_ms, r->puff);
    /* each puff's radius cut to the last table entry whose addition is not 0 (fk_glow_add_lut adds nothing past it:
     * the same pixels, fewer visited; M5). The tables fall with N, so the cut is the first zero onward. */
    {
        static const int MR[RD_MIST_N_RADII] = RD_MIST_RADII;
        const int cmax = omni_ex_g(C_MIST) > omni_ex_r(C_MIST) ? omni_ex_g(C_MIST) : omni_ex_r(C_MIST);
        const int cm = cmax > omni_ex_b(C_MIST) ? cmax : omni_ex_b(C_MIST);
        for (int i = 0; i < r->npuff; i++) {
            const int R = MR[r->puff[i].r_idx], gp = omni_amul(r->puff[i].a8);
            const uint8_t *lut = r->mist[r->puff[i].r_idx];
            int k = (int)FK_GLOW_LUT_N(R) - 1;
            while (k >= 0 && ((cm * ((omni_amul(lut[k]) * gp) >> 8)) >> 8) == 0) k--;
            int re = 1;   /* the least radius whose disc holds entry k: 4 re^2 > 8 k + 2 */
            while (re < R && 4 * re * re <= 8 * k + 2) re++;
            r->puff_r[i] = (uint8_t)(k < 0 ? 0 : re);
        }
    }
    if (st->lid_gone) return;   /* PR11: the interior, the flash and the mist alone */
    fk_rows_map(&RD_LID_HINGE, &r->rs, 0, W, r->rows);
    fk_rows_edge(&r->rs, r->rows, W, W, W, &r->edge);
    /* the rows the leaf covers whole (fk_rows_band reads no interior there): contiguous; the rest is the need */
    int y = 0;
    while (y < W && !(r->rows[y].v >= 0 && r->rows[y].inv_s_q16 <= 65536u)) y++;
    r->cov0 = (int16_t)y;
    while (y < W && r->rows[y].v >= 0 && r->rows[y].inv_s_q16 <= 65536u) y++;
    r->cov1 = (int16_t)y;
    if (r->cov0 >= r->cov1) r->cov0 = r->cov1 = 0;
    else if (r->cov0 == 0 && r->cov1 == W) out->need = (omni_rect_t){ 0, 0, 0, 0 };   /* the shut lid covers all */
    else if (r->cov0 == 0) out->need = (omni_rect_t){ 0, r->cov1, W, W };
    else if (r->cov1 == W) out->need = (omni_rect_t){ 0, 0, W, r->cov0 };
    if (st->shimmer_a8) {   /* the shimmer's additions in 8.8 (fk_glow_add_lut's gain rule, before its >> 8) */
        const int gp = omni_amul(st->shimmer_a8);
        const int c3[3] = {omni_ex_r(RD_C_PHOS), omni_ex_g(RD_C_PHOS), omni_ex_b(RD_C_PHOS)};
        for (int a = 0; a < 256; a++)
            for (int j = 0; j < 3; j++) r->sh_add[j][a] = (uint16_t)(c3[j] * ((omni_amul(a) * gp) >> 8));
    }
    if (r->rs.theta <= 0 && !st->final_lid) {   /* the live counter before the final lid */
        rd_rel_counter_str(st->counter, r->ctext);
        r->cx = (int16_t)(RD_REL_COUNTER_X - fk_atlas_width(&r->vt28.core, r->ctext, 0));
    }
}

/* the cold flash, additive ('lighter': each 8-bit channel plus the flash's, saturating) */
static void flash_row(const rel_t *r, uint16_t *o, int n)
{
    const int fr = r->flash[0], fg = r->flash[1], fb = r->flash[2];
    for (int x = 0; x < n; x++) {
        const uint16_t p = o[x];
        int R = omni_ex_r(p) + fr, G = omni_ex_g(p) + fg, B = omni_ex_b(p) + fb;
        R = R > 255 ? 255 : R, G = G > 255 ? 255 : G, B = B > 255 ? 255 : B;
        o[x] = omni_pack565(R, G, B);
    }
}

/* the mist over rows [y0, y1) of the band (fb: the pixel at (clip.x0, clip.y0)) */
static void mist(const rel_t *r, omni_rect_t clip, uint16_t *fb)
{
    if (clip.y0 >= clip.y1) return;
    for (int i = 0; i < r->npuff; i++) {
        const rd_puff_t *p = &r->puff[i];
        if (r->puff_r[i]) fk_glow_add_lut(fb, W, &clip, p->x, p->y, r->puff_r[i], r->mist[p->r_idx], C_MIST, p->a8);
    }
}

/* The shimmer (review M4): fk_glow_add_lut's soft glow at r 120, its addition kept in 8.8 and packed with the ordered
 * dither, so its faint green does not band into rings at RGB565. A channel with nothing to add keeps its bits. Pixel
 * centres about (cx, cy) as fk_glow_add_lut's (N = (2 (x - cx) + 1)^2 + (2 (y - cy) + 1)^2, entry (N - 2) / 8). */
static void shimmer(const rel_t *r, omni_rect_t clip, uint16_t *fb, int cx, int cy)
{
    const int R = RD_SHIMMER_R, n4 = 4 * R * R;
    const int y0 = clip.y0 > cy - R ? clip.y0 : cy - R, y1 = clip.y1 < cy + R ? clip.y1 : cy + R;
    const int x0 = clip.x0 > cx - R ? clip.x0 : cx - R, x1 = clip.x1 < cx + R ? clip.x1 : cx + R;
    for (int y = y0; y < y1; y++) {
        const int ty = 2 * (y - cy) + 1, ty2 = ty * ty;
        uint16_t *o = fb + (ptrdiff_t)(y - clip.y0) * W - clip.x0;
        for (int x = x0; x < x1; x++) {
            const int tx = 2 * (x - cx) + 1, N = tx * tx + ty2;
            if (N >= n4) continue;
            const int al = r->g120[(N - 2) >> 3];
            const int ar = r->sh_add[0][al], ag = r->sh_add[1][al], ab = r->sh_add[2][al];
            if (!(ar | ag | ab)) continue;
            const uint16_t p = o[x];
            const int d = BAYER4[y & 3][x & 3];
            int q5r = ar ? ((omni_ex_r(p) << 8) + ar + (d << 7)) >> 11 : p >> 11;
            int q6g = ag ? ((omni_ex_g(p) << 8) + ag + (d << 6)) >> 10 : (p >> 5) & 63;
            int q5b = ab ? ((omni_ex_b(p) << 8) + ab + (d << 7)) >> 11 : p & 31;
            q5r = q5r > 31 ? 31 : q5r, q6g = q6g > 63 ? 63 : q6g, q5b = q5b > 31 ? 31 : q5b;
            o[x] = (uint16_t)(q5r << 11 | q6g << 5 | q5b);
        }
    }
}

/* black at 255 - fade over every panel pixel of row o (omni_blend565(0, q, fade), as fk_rows_band's fade) */
static void fade_row(uint16_t *o, int p0, int p1, uint8_t fade)
{
    const uint32_t fp = (uint32_t)omni_amul(fade);
    for (int x = p0; x < p1; x++) {
        const uint16_t q = o[x];
        const uint32_t rb = ((uint32_t)omni_ex_r(q) << 16 | (uint32_t)omni_ex_b(q)) * fp;
        const uint32_t g = (uint32_t)omni_ex_g(q) * fp;
        o[x] = (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
    }
}

static void panel_of(int y, omni_rect_t b, int *p0, int *p1)
{
    fk_panel_row(y, p0, p1);
    if (*p0 < b.x0) *p0 = b.x0;
    if (*p0 > b.x1) *p0 = b.x1;
    if (*p1 > b.x1) *p1 = b.x1;
    if (*p1 < *p0) *p1 = *p0;
}

static void rel_band(const void *arg, const void *rctx, const uint16_t *interior, omni_rect_t band, uint16_t *out)
{
    (void)arg;
    const rel_t *r = rctx;
    const rd_rel_state_t *st = &r->st;
    omni_rect_t b = band;   /* clipped to the screen; out keeps band's layout (row 0 = band.y0) */
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > W) b.x1 = W;
    if (b.y1 > W) b.y1 = W;
    if (b.x0 >= b.x1 || b.y0 >= b.y1) return;
    uint16_t *ob = out + (ptrdiff_t)(b.y0 - band.y0) * W;   /* screen row b.y0, column x at offset x */
    const bool lid = !st->done && !st->reduced && !st->lid_gone;
    const bool cov = lid && r->cov0 < r->cov1;   /* rows [cov0, cov1) are the leaf's alone */
    /* the interior (with the flash), or the reduced cross-fade, on the panel; black off it */
    for (int y = b.y0; y < b.y1; y++) {
        uint16_t *o = ob + (size_t)(y - b.y0) * W;
        int p0, p1;
        panel_of(y, b, &p0, &p1);
        memset(o + b.x0, 0, (size_t)(p0 - b.x0) * 2);
        memset(o + p1, 0, (size_t)(b.x1 - p1) * 2);
        if (p0 >= p1 || (cov && y >= r->cov0 && y < r->cov1)) continue;   /* the leaf draws a covered row whole */
        const uint16_t *in = interior + (size_t)y * W;
        if (st->reduced && !st->done) {   /* the final lid cross-faded over the settled face */
            const uint16_t *lf = r->leaf + (size_t)y * W;
            for (int x = p0; x < p1; x++) o[x] = omni_blend565(in[x], lf[x], st->door_a8);
            continue;
        }
        memcpy(o + p0, in + p0, (size_t)(p1 - p0) * 2);
        if (st->flash_a8 && !st->done) flash_row(r, o + p0, p1 - p0);
    }
    if (st->done || st->reduced) return;
    /* the mist under the lid, on the rows the leaf does not cover whole (above and below the covered ones: the leaf
     * would draw over the rest), then black off the panel again (the glows reach past the rim) */
    if (r->npuff) {
        /* the band's rows above the covered ones, [b.y0, a1), and below them, [z0, b.y1) */
        const int a0 = b.y0, a1 = !cov ? b.y1 : r->cov0 < b.y0 ? b.y0 : r->cov0 > b.y1 ? b.y1 : r->cov0;
        const int z0 = !cov ? b.y1 : r->cov1 < b.y0 ? b.y0 : r->cov1 > b.y1 ? b.y1 : r->cov1, z1 = b.y1;
        const omni_rect_t m1 = { b.x0, (int16_t)a0, b.x1, (int16_t)a1 }, m2 = { b.x0, (int16_t)z0, b.x1, (int16_t)z1 };
        if (m1.y0 < m1.y1) mist(r, m1, ob + (size_t)(m1.y0 - b.y0) * W + b.x0);
        if (cov && m2.y0 < m2.y1) mist(r, m2, ob + (size_t)(m2.y0 - b.y0) * W + b.x0);
        for (int y = b.y0; y < b.y1; y++) {
            uint16_t *o = ob + (size_t)(y - b.y0) * W;
            int p0, p1;
            panel_of(y, b, &p0, &p1);
            memset(o + b.x0, 0, (size_t)(p0 - b.x0) * 2);
            memset(o + p1, 0, (size_t)(b.x1 - p1) * 2);
        }
    }
    if (!lid) return;   /* PR11: after 1620 ms the band is the interior, the flash and the mist alone */
    fk_rows_band(&r->rs, r->rows, &r->edge, r->leaf, NULL, b, ob);
    if (r->rs.theta <= 0) {   /* before the swing: the counter, the lamp and the shimmer over the shifted leaf */
        const omni_rect_t clip = b;
        uint16_t *fb = ob + b.x0;
        const int dy = r->rs.dy;
        if (!st->final_lid) {
            fk_atlas_text_rgb(fb, W, clip, &r->vt28.glow, r->ctext, r->cx, RD_REL_COUNTER_Y + dy, 0, RD_C_AMBER, 255, 0);
            fk_atlas_text_rgb(fb, W, clip, &r->vt28.core, r->ctext, r->cx, RD_REL_COUNTER_Y + dy, 0, RD_C_AMBER, 255, 0);
        }
        if (st->lamp == 2) {
            fk_glow_add_lut(fb, W, &clip, LAMP_X, LAMP_Y + dy, 16, r->g16, RD_C_GO, 153);   /* .6 */
        } else {   /* red, the disc and its glow blinking together at 140 ms (unset: until the swing, M3) */
            const float blink = st->lamp ? 1.0f : 0.45f;
            fk_disc(fb, W, &clip, LAMP_X, (float)(LAMP_Y + dy), 4.2f, C_LAMP, fk_ease_to8(blink));
            fk_glow_add_lut(fb, W, &clip, LAMP_X, LAMP_Y + dy, 14, r->g14, C_LAMP, fk_ease_to8(0.5f * blink));
        }
        if (st->shimmer_a8) shimmer(r, clip, fb, C, SHIMMER_Y + dy);
    }
    if (st->rows.fade != 255)
        for (int y = b.y0; y < b.y1; y++) {
            int p0, p1;
            panel_of(y, b, &p0, &p1);
            fade_row(ob + (size_t)(y - b.y0) * W, p0, p1, st->rows.fade);
        }
}

const fk_reveal_ops_t RD_REVEAL_STASIS = {
    .len_ms = RD_REL_MS, .reduced_ms = RD_REL_FADE_MS, .pop_ms = RD_REL_POP_MS,
    .pop_asset = NULL,   /* no latch sound (PR3) */
    .rctx_bytes = sizeof(rel_t),
    .bind = rel_bind, .frame = rel_frame, .band = rel_band,
};
