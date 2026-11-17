/* The hull pass (rd/pass.h; rd wave 2 Task 5, spec section 10.3 as ruling R166 has it; faces-reveal.js hullR() 797-802):
 * the timeline, and RD_REVEAL_PASS, a direct-path reveal over Drift Plate's own band. The face draws the pass itself
 * (its draw() at the plan's wake_ms renders the band at the pass's offset, rd/faces.h RD_FACE_DRIFT), so the reveal
 * keeps no leaf: its bind reads nothing, its rctx is the last plan, and its band() copies the interior and lays the
 * opening fade.
 *
 * The plan (M7): the first frame draws the whole interior. Every later frame of the pass draws one rect, the band's
 * rows (RD_DP_BAND: the plates, the frame numbers, the time and WASH ME riding the offset, and the drift's 10 s step),
 * with the windows' from 1450 ms (lights > 0, and a second's boundary can change the count up to the end; the newest
 * one's glow reaches a row past the band) and the labels' from 1250 ms (their settle: the label, the date, midnight's
 * date among them, and the caption show from it), as one bounding box. Outside it nothing the face draws changes during
 * the pass: before 1250 ms the labels and the date are dark, the stars are still (C13), the stale marker and the
 * clock's state hold for a reveal's input, and the chime picture waits for the settled frame (the face draws none while
 * wake_ms runs). The settled end, a tap's
 * end frame and reduced motion's fade draw the settled face whole once and reuse it.
 *
 * Pure: no esp_*, freertos or LVGL; no allocation; no libm (the overshoot's sine through fk_sincos). */
#include "rd/pass.h"
#include <string.h>
#include "fk/ease.h"
#include "fk/shapes.h"
#include "omni/blend565.h"
#include "rd/faces.h"
#include "rd/release.h"

#define W 466
#define PI_F 3.14159265358979f

void rd_pass_skip(rd_pass_state_t *st)
{
    *st = (rd_pass_state_t){ .off = 0.0f, .lights8 = 255, .settle8 = 255, .fade8 = 255, .done = true, .settled = true };
}

void rd_pass_at(uint32_t t_ms, bool reduced, rd_pass_state_t *st)
{
    /* t_ms is an elapsed time from the reveal's start, not a timestamp: plain comparisons are right */
    const float t = (float)t_ms;
    memset(st, 0, sizeof *st);
    if (reduced) {   /* a fade from black onto the settled face: no pass */
        st->lights8 = st->settle8 = 255;
        st->fade8 = fk_ease_to8(fk_ease_out_cubic(fk_ease_ph(t, 0, RD_PASS_FADE_MS)));
        st->done = st->settled = t_ms >= RD_PASS_FADE_MS;
        return;
    }
    const float p = fk_ease_ph(t, 0, 1500), u = fk_ease_ph(t, 1500, RD_PASS_PARK_MS);
    float s, c;
    fk_sincos(PI_F * u, &s, &c);
    st->off = 2700.0f * (1.0f - fk_ease_out_cubic(p)) - 7.0f * s * (1.0f - u);
    if (t_ms >= RD_PASS_PARK_MS) st->off = 0.0f;   /* parked: sin(pi) leaves a float's crumb, the band none */
    st->lights8 = fk_ease_to8(fk_ease_ph(t, 1450, 1950));
    st->settle8 = fk_ease_to8(fk_ease_out_cubic(fk_ease_ph(t, 1250, RD_PASS_SETTLED_MS)));
    st->fade8 = fk_ease_to8(fk_ease_ph(t, 0, 200));
    st->done = t_ms > RD_PASS_PARK_MS;
    st->settled = t_ms >= RD_PASS_SETTLED_MS;
}

/* ---- RD_REVEAL_PASS */
typedef struct {
    rd_pass_state_t st;   /* the last frame's state: the band reads its fade */
    bool first;           /* the first frame after bind draws the whole interior */
    bool settled_drawn;   /* the interior holds the settled face */
    int32_t prev_t;       /* the last frame's time (-1: none yet), for the timing */
} pass_t;

static bool pass_bind(const void *arg, const char *asset, fk_asset_fn get, void *rctx)
{
    (void)arg, (void)asset, (void)get;   /* the face reads its own tile: nothing here */
    pass_t *r = rctx;
    memset(r, 0, sizeof *r);
    r->first = true;
    r->prev_t = -1;
    return true;
}

static int window_of(uint32_t t)
{
    return t < RD_PASS_WIN_RUSH ? 0 : t < RD_PASS_WIN_SLOW ? 1 : t < RD_PASS_PARK_MS ? 2 : 3;
}

static void unite(omni_rect_t *a, omni_rect_t b)
{
    a->x0 = b.x0 < a->x0 ? b.x0 : a->x0, a->y0 = b.y0 < a->y0 ? b.y0 : a->y0;
    a->x1 = b.x1 > a->x1 ? b.x1 : a->x1, a->y1 = b.y1 > a->y1 ? b.y1 : a->y1;
}

static void pass_frame(const void *arg, void *rctx, uint32_t t_ms, bool skip, bool reduced, fk_rframe_t *out)
{
    (void)arg;
    pass_t *r = rctx;
    if (r->prev_t >= 0 && (uint32_t)r->prev_t <= t_ms)   /* the last frame's wall time, by its window */
        rd_rel_timing_note(window_of((uint32_t)r->prev_t), t_ms - (uint32_t)r->prev_t);
    r->prev_t = skip ? -1 : (int32_t)t_ms;
    if (skip) rd_pass_skip(&r->st);
    else rd_pass_at(t_ms, reduced, &r->st);
    const bool first = r->first;
    r->first = false;
    out->settled = r->st.settled;
    out->live = !reduced;
    out->need = (omni_rect_t){ 0, 0, W, W };
    if (r->st.settled || reduced || skip) {   /* the settled face: drawn whole on the first such frame, then reused */
        out->wake_ms = UINT32_MAX;
        if (r->settled_drawn) out->need = (omni_rect_t){ 0, 0, 0, 0 };
        r->settled_drawn = true;
        return;
    }
    r->settled_drawn = false;   /* this frame draws the pass's time into the interior */
    out->wake_ms = t_ms;
    if (first) return;
    omni_rect_t need = RD_DP_BAND;
    if (t_ms >= 1450) unite(&need, RD_DP_WINDOWS);
    if (t_ms >= 1250) unite(&need, RD_DP_STALE), unite(&need, RD_DP_CAPTION);
    out->need = need;
}

static void pass_band(const void *arg, const void *rctx, const uint16_t *interior, omni_rect_t band, uint16_t *out)
{
    (void)arg;
    const pass_t *r = rctx;
    const int y0 = band.y0 > 0 ? band.y0 : 0, y1 = band.y1 < W ? band.y1 : W;
    if (y0 >= y1) return;
    uint16_t *ob = out + (ptrdiff_t)(y0 - band.y0) * W;   /* screen row y0 */
    memcpy(ob, interior + (size_t)y0 * W, (size_t)(y1 - y0) * W * 2);
    /* the opening fade (omni_blend565(0, q, fade8): the black destination's terms are 0) */
    const uint8_t f8 = r->st.fade8;
    if (f8 == 255) return;
    const uint32_t fp = (uint32_t)omni_amul(f8);
    for (int i = 0; i < (y1 - y0) * W; i++) {
        const uint16_t q = ob[i];
        const uint32_t rb = ((uint32_t)omni_ex_r(q) << 16 | (uint32_t)omni_ex_b(q)) * fp;
        const uint32_t g = (uint32_t)omni_ex_g(q) * fp;
        ob[i] = (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
    }
}

const fk_reveal_ops_t RD_REVEAL_PASS = {
    .len_ms = RD_PASS_MS, .reduced_ms = RD_PASS_FADE_MS, .pop_ms = 0,
    .pop_asset = NULL,   /* no latch sound */
    .rctx_bytes = sizeof(pass_t),
    .bind = pass_bind, .frame = pass_frame, .band = pass_band,
};
