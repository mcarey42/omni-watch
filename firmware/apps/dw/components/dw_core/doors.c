#include "dw/doors.h"
#include <math.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "fk/ease.h"   /* face_kit's five one-line math helpers (PR1c) */
#include "fk/reveal_sm.h"

/* The doors (doors.h): the timeline and the two door kinds, over face_kit's column renderer (fk/cols.h: the column
 * maps, the band compositor and the kept leaves; rd wave 1 Task 1). Single precision in a fixed order
 * (-ffp-contract=off, dw_core's CMakeLists). The timeline follows revealState and drawDoorsPhase in Designer A's
 * faces-reveal.js. */

#define N 466                   /* the screen, and the door image, is N x N */
#define C 233                   /* the eye's line: cx = cy */
#define F 560.0f
#define D2R 0.017453292519943295f
#define POP_RAD (DW_DOORS_POP_DEG * D2R)

/* The kinds (fk/cols.h): open_deg, then the pop the leaf's light rises with, then the correctly rounded float sine and
 * cosine of the float angle open_deg x D2R (worked out in long double), which an inward leaf takes at its kept angle
 * instead of libm (wave 2 Task 11; test_doors_kept_leaves_libm_free). An outward kind never reads them: DW_DOORS_OUT's
 * are its own open angle's all the same. No look (rd wave 2 Task 4): NULL is face_kit's FK_COLS_LOOK_DW, this
 * theme's. */
const fk_cols_kind_t DW_DOORS_OUT = { false, false, DW_DOORS_OPEN_DEG, DW_DOORS_POP_DEG,
                                       0x1.f838b8p-1f, 0x1.63a1aep-3f, NULL };   /* 80 x D2R = 0x1.657184p+0 */
const fk_cols_kind_t DW_DOORS_IN_KEEP = { true, true, 86.0f, DW_DOORS_POP_DEG,
                                           0x1.fec0b8p-1f, 0x1.1db9p-4f, NULL };   /* 86 x D2R = 0x1.80406ep+0 */

/* The timeline's other points. Designer A's (revealState): the fade 0-180, the seam's crack outCubic 110-300 and its
 * fall 560-760, the pop's rise 250-320. The device's compress the 380 ms before Designer A's swing into the 150 ms
 * before the device's (x .4; spec §10: "0-0.15 s: the doors fade up and light cracks the seam; a 2 deg latch pop"),
 * and put the seam's fall at the same points of the swing (Designer A's swing + 180 and + 380 of 780). */
#if defined(DW_DOORS_MOCKUP) && DW_DOORS_MOCKUP
#define FADE_MS 180
#define SEAM_UP0 110
#define SEAM_UP1 300
#define SEAM_DN0 560
#define SEAM_DN1 760
#define POP_T0 250
#define POP_T1 320
#else
#define FADE_MS 150
#define SEAM_UP0 40
#define SEAM_UP1 120
#define SEAM_DN0 290
#define SEAM_DN1 440
#define POP_T0 100
#define POP_T1 150
#endif
_Static_assert(POP_T1 <= DW_DOORS_SWING_T0_MS, "the pop is up before the swing starts from it");
_Static_assert(FADE_MS < DW_DOORS_REDUCED_MS, "reduced: the cross-fade follows the fade");
_Static_assert(DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS + DW_DOORS_SWING_MS < DW_DOORS_END_MS, "the swing ends first");

void dw_doors_cols(float th_left, float th_right, fk_col_t cols[466])
{
    fk_cols_k(&DW_DOORS_OUT, th_left, th_right, cols);
}

/* ---- the timeline ---- */

static float leaf_theta(float t, float t0, float open)
{
    const float pop = POP_RAD * fk_ease_out_cubic(fk_ease_ph(t, POP_T0, POP_T1));
    return pop + (open - POP_RAD) * fk_ease_in_out_cubic(fk_ease_ph(t, t0, t0 + DW_DOORS_SWING_MS));
}

void dw_doors_skip_k(const fk_cols_kind_t *k, fk_cols_state_t *st)
{
    const float open = k->open_deg * D2R;
    *st = (fk_cols_state_t){ .th_l = open, .th_r = open, .fade = 255, .seam = 0, .glow = 0, .xfade = 0,
                              .tail = 255, .leaves = k->keep, .done = true, .keep = k->keep, .inward = k->inward };
}

void dw_doors_skip(fk_cols_state_t *st) { dw_doors_skip_k(&DW_DOORS_OUT, st); }

void dw_doors_at(uint32_t t_ms, bool reduced, fk_cols_state_t *st) { dw_doors_at_k(&DW_DOORS_OUT, t_ms, reduced, st); }

void dw_doors_at_k(const fk_cols_kind_t *kind, uint32_t t_ms, bool reduced, fk_cols_state_t *st)
{
    /* t_ms is an elapsed time from the doors' start, not a timestamp: plain comparisons are right here */
    const float t = (float)t_ms;
    if (reduced) {
        if (t_ms >= DW_DOORS_REDUCED_MS) {   /* the end, the leaves never having moved; kept ones at open_deg */
            dw_doors_skip_k(kind, st);
            if (!kind->keep) st->th_l = st->th_r = 0;
            return;
        }
        /* Designer A's order (revealState): the fade from black, then the closed doors cross-fade to the face (with
         * keep, the face's interior shows its kept leaves: they fade in with it) */
        const uint8_t x = fk_ease_to8(1.0f - fk_ease_out_cubic(fk_ease_ph(t, FADE_MS, DW_DOORS_REDUCED_MS)));
        *st = (fk_cols_state_t){ .th_l = 0, .th_r = 0, .fade = fk_ease_to8(fk_ease_ph(t, 0, FADE_MS)), .seam = 0,
                                  .glow = 0, .xfade = x, .tail = 255, .leaves = x > 0, .done = false, .keep = kind->keep,
                                  .inward = kind->inward };
        return;
    }
    if (t_ms >= DW_DOORS_END_MS) {
        dw_doors_skip_k(kind, st);
        return;
    }
    const bool in = kind->inward;
    const float t0r = DW_DOORS_SWING_T0_MS, t0l = DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS;
    const float open_rad = kind->open_deg * D2R;
    const float th_r = leaf_theta(t, t0r, open_rad), th_l = leaf_theta(t, t0l, open_rad);
    /* revealState's sliver, with its flicker */
    const float sliver =
        fk_ease_out_cubic(fk_ease_ph(t, SEAM_UP0, SEAM_UP1)) * (1.0f - fk_ease_ph(t, SEAM_DN0, SEAM_DN1));
    const float seam = sliver * (0.85f + 0.15f * sinf(sliver * 40.0f));
    /* drawDoorsPhase's gap glow: brightest while the gap is narrow, out once both leaves are open */
    const float open = (fk_ease_in_out_cubic(fk_ease_ph(t, t0r, t0r + DW_DOORS_SWING_MS)) +
                        fk_ease_in_out_cubic(fk_ease_ph(t, t0l, t0l + DW_DOORS_SWING_MS))) * 0.5f;
    const float gap = ((float)N - fk_cols_free_edge(kind, th_r, in)) - fk_cols_free_edge(kind, th_l, in);
    const float k = gap > 0.5f && open < 1.0f ? (1.0f - open) * fk_ease_clampf(gap / 30.0f, 0, 1) : 0.0f;
    const float gone = omni_hinge_gone_rad(&FK_COLS_HINGE);
    *st = (fk_cols_state_t){ .th_l = th_l, .th_r = th_r, .fade = fk_ease_to8(fk_ease_ph(t, 0, FADE_MS)),
                              .seam = sliver > 0.005f ? fk_ease_to8(seam) : 0, .glow = fk_ease_to8(k * 0.9f),
                              .xfade = 255,
                              .tail = fk_ease_to8(fk_ease_ph(t, DW_DOORS_SWING_T0_MS, DW_DOORS_END_MS)),
                              .leaves = fk_cols_leaf_on_screen(th_l, gone, in) ||
                                        fk_cols_leaf_on_screen(th_r, gone, in),
                              .done = false, .keep = kind->keep, .inward = in };
}

uint32_t dw_doors_pop_ms(void) { return POP_T0; }


/* ---- the doors as a reveal (rd wave 1 Task 2): what the view (face_kit's fk_view) ran itself until then, the same
 * calls in the same order: the state at the doors' time (or the skip's), the kind's columns, fk_rs_plan's interior
 * plan and fk_rs_job_in's input; then fk_cols_band per band. arg is the face's reveal_arg (its door kind; NULL:
 * DW_DOORS_OUT), asset its reveal_asset (NULL: ASSET_DW_DOORS_M; room's ASSET_DW_DOORS_C). */
typedef struct {
    const fk_cols_kind_t *kind;
    const uint16_t *door;   /* the leaf image, read in place (or from the view's PSRAM copy, `dw doors bench .. copy`) */
    const uint8_t *glow;
    bool settled;           /* the interior holds the settled frame (fk_rs_plan's, from frame to frame) */
    fk_cols_state_t st;     /* the last frame's state: the bands read it */
    fk_col_t cols[N];
} doors_rctx_t;

static bool doors_bind(const void *arg, const char *asset, fk_asset_fn get, void *rctx)
{
    doors_rctx_t *r = rctx;
    size_t n = 0;
    fk_img_t door, glow;
    const void *dp = get(asset ? asset : ASSET_DW_DOORS_M, &n);
    if (!dp || !fk_img_from_bin(&door, dp, n) || door.cf != FK_CF_RGB565 || door.w != N || door.h != N) return false;
    const void *gp = get(ASSET_DW_GLOW, &n);
    if (!gp || !fk_img_from_bin(&glow, gp, n) || !glow.a || glow.w != 64 || glow.h != 256) return false;
    r->kind = arg ? arg : &DW_DOORS_OUT;
    r->door = door.rgb;
    r->glow = glow.a;
    r->settled = false;
    return true;
}

static void doors_frame(const void *arg, void *rctx, uint32_t t_ms, bool skip, bool reduced, fk_rframe_t *out)
{
    (void)arg;   /* the kind bound */
    doors_rctx_t *r = rctx;
    if (skip) dw_doors_skip_k(r->kind, &r->st);
    else dw_doors_at_k(r->kind, t_ms, reduced, &r->st);
    fk_cols_k(r->kind, r->st.th_l, r->st.th_r, r->cols);
    fk_rp_t p;
    fk_rs_plan(&r->st, r->cols, reduced, &r->settled, &p);
    out->settled = r->st.done;
    out->live = false;
    out->wake_ms = UINT32_MAX;
    if (p.kind == FK_RP_NONE) {
        out->need = (omni_rect_t){ 0, 0, 0, 0 };
        return;
    }
    out->need = (omni_rect_t){ p.xa, 0, p.xb, N };   /* columns: the whole height */
    fk_rs_job_in(&p, t_ms, reduced, &out->wake_ms, &out->live);
}

static void doors_band(const void *arg, const void *rctx, const uint16_t *interior, omni_rect_t band, uint16_t *out)
{
    (void)arg;
    const doors_rctx_t *r = rctx;
    fk_cols_band(r->kind, &r->st, r->cols, r->door, interior, r->glow, band, out);
}

const fk_reveal_ops_t DW_REVEAL_DOORS = {
    .len_ms = DW_DOORS_END_MS, .reduced_ms = DW_DOORS_REDUCED_MS, .pop_ms = POP_T0,
    .pop_asset = ASSET_DW_LATCH,   /* the latch at the pop (WD15) */
    .rctx_bytes = sizeof(doors_rctx_t),
    .bind = doors_bind, .frame = doors_frame, .band = doors_band,
};
