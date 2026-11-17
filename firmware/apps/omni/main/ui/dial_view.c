#include "ui/dial_view_int.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "omni/blend565.h"
#include "omni/iris.h"
#include "omni/raster.h"
#include "assets.h"
#include "ui/assets_meta.h"
#include "ui/globe_pie.h"
#include "theme.h"

static const char *TAG = "dial";

/* The image path swaps the image's data behind LVGL and frees it without lv_image_cache_drop (globe_view's m11). */
#if !defined(CONFIG_LV_CACHE_DEF_SIZE) || !defined(CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT) || \
    CONFIG_LV_CACHE_DEF_SIZE != 0 || CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT != 0
#error "dial_view needs the LVGL image cache and image header cache off (LV_CACHE_DEF_SIZE, LV_IMAGE_HEADER_CACHE_DEF_CNT)"
#endif

#define WORKER_PRIO 2       /* under LVGL, as globe_view */
#define BUILD_PRIO 4        /* while building: above LVGL's draw thread (3), under its task (6) */
#define BAND_HELP_PRIO 6    /* BAND: the helper runs only band chunks the LVGL task (6) is waiting for */
#define WORKER_CORE 1
#define HELPER_CORE 0
#define BAND_CHUNK 8        /* BAND: a 16-line LVGL band is two chunks */
#define MAX_RECTS OMNI_RECTS_MAX
#define LUT_ROWS 8          /* globe LUT rows per build step (each step yields) */
#define DISC_BAND 16
#define DISC_MARGIN 11      /* px each side of a band: the home marker (radius 5 + 4 pulse, its ring 0.7, + AA) */
#define CHORD_BAND 8        /* the chord bands' rows: their pixels then lie within r 119.2, clear of rings and marks */

portMUX_TYPE dial_mux = portMUX_INITIALIZER_UNLOCKED;
static dial_view_t *s_current;
/* The next view's switches (`dial path`, `dial perf tex`): each view created takes them and puts them back to the
 * defaults, so a switch applies to one view and nothing outlives it (T7 M3). */
static int s_path = -1;   /* -1: DISP_PATH_FULLSCREEN */
#if CONFIG_OMNI_DEBUG_CONSOLE
static bool s_next_tex = true, s_next_base = true;
#endif

disp_path_t dial_view_path(void) { return s_path < 0 ? DISP_PATH_FULLSCREEN : (disp_path_t)s_path; }
void dial_view_set_path(disp_path_t p) { s_path = (int)p; }
#if CONFIG_OMNI_DEBUG_CONSOLE
void dv_set_next_tex(bool on) { s_next_tex = on; }
void dv_set_next_base(bool on) { s_next_base = on; }
bool dv_next_tex(void) { return s_next_tex; }
bool dv_next_base(void) { return s_next_base; }
#else
bool dv_next_tex(void) { return true; }
bool dv_next_base(void) { return true; }
#endif

uint32_t dv_us_since(int64_t t0) { return (uint32_t)(esp_timer_get_time() - t0); }

static void *ps_alloc(dial_view_t *v, size_t n)
{
    void *p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM);
    portENTER_CRITICAL(&dial_mux);
    if (p) v->bytes += (uint32_t)n;
    portEXIT_CRITICAL(&dial_mux);
    return p;
}

static bool quitting(dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    const bool q = v->quit;
    portEXIT_CRITICAL(&dial_mux);
    return q;
}

/* ---- The helper (core 0): runs one function the caller handed it, then gives help_done */

static void help_start(dial_view_t *v, help_fn fn, int a, int b)
{
    v->h_fn = fn;
    v->h_a = a;
    v->h_b = b;
    portENTER_CRITICAL(&dial_mux);
    v->h_pending = true;
    v->h_started = false;
    portEXIT_CRITICAL(&dial_mux);
    xTaskNotifyGive(v->helper);
}

/* withdrawable: a job the caller may have finished alone (claimed chunks): a helper that has not started it by now (its
 * core is busy) is withdrawn rather than waited for. */
static void help_wait(dial_view_t *v, bool withdrawable)
{
    portENTER_CRITICAL(&dial_mux);
    const bool withdrawn = withdrawable && v->h_pending && !v->h_started;
    if (withdrawn) v->h_pending = false;
    portEXIT_CRITICAL(&dial_mux);
    if (!withdrawn) xSemaphoreTake(v->help_done, portMAX_DELAY);
}

static void helper(void *arg)
{
    dial_view_t *v = arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&dial_mux);
        const bool quit = v->h_quit && !v->h_pending, go = v->h_pending;
        v->h_pending = false;
        v->h_started = go;
        portEXIT_CRITICAL(&dial_mux);
        if (quit) break;
        if (!go) continue;   /* withdrawn */
        v->h_fn(v, 1, v->h_a, v->h_b);
        xSemaphoreGive(v->help_done);
    }
    xSemaphoreGive(v->h_exited);
    vTaskSuspend(NULL);   /* destroy deletes it */
}

/* ---- Rendering: a job's chunks, on whichever cores claim them */

/* R88 candidate 5: every other row of c rendered into fb (a whole frame's layout), the next row its copy. */
static void render_half_rows(dial_view_t *v, uint16_t *fb, const omni_rect_t *c)
{
    for (int y = c->y0; y < c->y1; y += 2) {
        const omni_rect_t row = { c->x0, (int16_t)y, c->x1, (int16_t)(y + 1) };
        omni_dial_render_prepared(v->prep, fb, &row);
        if (y + 1 < c->y1)
            memcpy(fb + (size_t)(y + 1) * SCR + c->x0, fb + (size_t)y * SCR + c->x0, (size_t)(c->x1 - c->x0) * 2);
    }
}

/* The direct job's fill: the band is chunk c's rows of a whole frame (BAND's row-offset trick, A8), rendered from
 * prep (half rows on a lossy frame: the chunks are even, so each row pair is inside one), or copied from j_src. */
void dv_direct_fill(void *ctx, uint16_t *band, omni_rect_t c)
{
    dial_view_t *v = ctx;
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (!(v->j_perf & PERF_RENDER)) return;   /* `dial perf render off`: the path's own ceiling (stale bands) */
#endif
    uint16_t *fb = (uint16_t *)(void *)((uintptr_t)band - (uintptr_t)c.y0 * SCR * 2);
    if (v->j_src) {
        for (int y = c.y0; y < c.y1; y++)
            memcpy(fb + (size_t)y * SCR + c.x0, v->j_src + (size_t)y * SCR + c.x0, (size_t)(c.x1 - c.x0) * 2);
    } else if (v->j_lossy) {
        render_half_rows(v, fb, &c);
    } else {
        omni_dial_render_prepared(v->prep, fb, &c);
    }
    if (v->j_lov_n) omni_band_overlay(band, SCR, c, v->j_lov, v->j_lov_n);   /* Task 6: LVGL overlays, then the pack */
}

static void render_chunk(dial_view_t *v, int who, const omni_rect_t *c)
{
    if (v->j_direct) {
#if CONFIG_OMNI_DEBUG_CONSOLE
        if (__atomic_exchange_n(&v->dd_lose, false, __ATOMIC_RELAXED)) {   /* `dial direct lose`: one band never sent */
            disp_direct_skip();
            return;
        }
#endif
        if (!disp_direct_send_chunk(*c, dv_direct_fill, v)) {   /* aborted: no chunk after it is sent either */
            portENTER_CRITICAL(&dial_mux);
            v->j_dd_fail = true;
            v->j_next = v->j_n;
            portEXIT_CRITICAL(&dial_mux);
        }
        return;
    }
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (v->j_dry) {   /* `dial verify`: the direct pipeline without the send, un-swapped into a whole frame */
        uint16_t *b = v->j_dry_band[who];
        dv_direct_fill(v, b, *c);
        omni_band_pack(b, SCR, *c);
        const int w = c->x1 - c->x0;
        for (int y = c->y0; y < c->y1; y++)
            for (int x = 0; x < w; x++)
                v->j_dry_out[(size_t)y * SCR + c->x0 + x] = __builtin_bswap16(b[(size_t)(y - c->y0) * w + x]);
        return;
    }
#endif
    if (v->j_fb && v->j_lossy) {
        render_half_rows(v, v->j_fb, c);
        return;
    }
    if (v->j_fb) {
        if (v->j_stage == 1) omni_dial_render_under(v->prep, v->j_fb, c);
        else if (v->j_stage == 2) omni_dial_render_over(v->prep, v->j_fb, c);
        else omni_dial_render_prepared(v->prep, v->j_fb, c);
        return;
    }
    /* BAND: rows [c->y0, c->y1) into this claimer's block (as rows of a whole frame: the block is the frame's rows from
     * c->y0), then the rect's columns into the layer */
    uint16_t *blk = v->band_scratch[who];
    uint16_t *fb = (uint16_t *)(void *)((uintptr_t)blk - (uintptr_t)c->y0 * SCR * 2);
    omni_dial_render_prepared(v->prep, fb, c);
    const int w = c->x1 - c->x0;
    for (int y = c->y0; y < c->y1; y++)
        memcpy(v->j_dst + (size_t)(y - v->j_y0) * v->j_stride + (c->x0 - v->j_x0), blk + (size_t)(y - c->y0) * SCR + c->x0,
               (size_t)w * 2);
}

static void run_chunks(dial_view_t *v, int who, int a, int b)
{
    (void)a, (void)b;
    for (;;) {
        portENTER_CRITICAL(&dial_mux);
        const int c = v->j_next < v->j_n ? v->j_next++ : -1;
        portEXIT_CRITICAL(&dial_mux);
        if (c < 0) return;
        render_chunk(v, who, &v->j_chunk[c]);
    }
}

/* The rects' rows in chunks of `rows` (doubled until they fit MAX_CHUNKS), made disjoint first (omni_rects_disjoint:
 * both cores render the chunks at once, and two overlapping rects would blend a pixel twice or from a half-drawn
 * state; seen on the device as a streak where the globe box met a hand's box, `dial verify`). */
void dv_set_chunks(dial_view_t *v, const rects_t *L0, int rows)
{
    rects_t D;
    omni_rects_disjoint(L0, &D);
    const rects_t *L = &D;
    int total = 0;
    for (int i = 0; i < L->n; i++) total += L->r[i].y1 - L->r[i].y0;
    while (total / rows + L->n > MAX_CHUNKS) rows *= 2;
    v->j_n = 0;
    for (int i = 0; i < L->n; i++)
        for (int y = L->r[i].y0; y < L->r[i].y1; y += rows)
            v->j_chunk[v->j_n++] = (omni_rect_t){ L->r[i].x0, (int16_t)y, L->r[i].x1, (int16_t)dv_imin(y + rows, L->r[i].y1) };
    v->j_next = 0;
}

/* Runs the job set up: the caller claims chunks, and so does the helper if both. */
void dv_dispatch(dial_view_t *v, bool both)
{
    if (both) help_start(v, run_chunks, 0, 0);
    run_chunks(v, 0, 0, 0);
    if (both) help_wait(v, true);
}

/* The loupe's animated value as one of its built sizes (the fidelity rule: the opening and closing are motion; the
 * open loupe, a = 1, is exact). 0: no loupe. */
static float loupe_step(float a)
{
    if (!(a > 0)) return 0;
    const float q = floorf(a * LOUPE_STEPS + 0.5f);
    return q < 1 ? 0 : (q > LOUPE_STEPS ? LOUPE_STEPS : q) / LOUPE_STEPS;
}

/* Points the frame's resources at the loupe built for a (a step: loupe_step), before either core renders (omni_dial.h:
 * a happens-before through the helper's notify). */
static void loupe_for(dial_view_t *v, float a)
{
    const int i = (int)floorf(a * LOUPE_STEPS + 0.5f) - 1;
    const bool ok = i >= 0 && i < LOUPE_STEPS && v->loupe_ok[i];
    v->res.loupe_map = ok ? &v->loupe_map[i] : NULL;   /* NULL: the frame draws the loupe's frame without content */
    v->res.loupe_base = ok ? v->loupe_base[i] : NULL;
    /* T6 M2 / M3: the map must be this frame's before either core renders; a miss is a bug here, logged once */
    if (a > 0 && !omni_dial_loupe_ready(&v->res, a) && !v->loupe_miss_logged) {
        v->loupe_miss_logged = true;
        ESP_LOGE(TAG, "loupe at %.3f has no map built for it: drawn without its content", (double)a);
    }
}

/* Builds loupe step i (its map, variant B, and the base zoomed under it). */
static void loupe_build(dial_view_t *v, int who, int i, int unused)
{
    (void)who, (void)unused;
    const int64_t t0 = esp_timer_get_time();
    const omni_pmap_cfg_t c = omni_dial_loupe_cfg((float)(i + 1) / LOUPE_STEPS, v->res.dial_map.cfg.n_theta);
    const size_t need = omni_pmap_bytes(&c, false, true), px = (size_t)(c.rect.x1 - c.rect.x0) * (c.rect.y1 - c.rect.y0);
    v->loupe_mem[i] = ps_alloc(v, need);
    v->loupe_base[i] = ps_alloc(v, px * 2);
    v->loupe_ok[i] = v->loupe_mem[i] && v->loupe_base[i] &&
                     omni_pmap_build(&v->loupe_map[i], &c, false, true, v->loupe_mem[i], need);
    if (v->loupe_ok[i]) omni_dial_zoom_base(v->res.base, &c, v->loupe_base[i]);
    const uint32_t us = dv_us_since(t0);
    portENTER_CRITICAL(&dial_mux);
    v->loupe_us_sum += us;
    if (us > v->loupe_us_max) v->loupe_us_max = us;
    portEXIT_CRITICAL(&dial_mux);
}

/* ---- Frames */

vframe_t dv_make_frame(dial_view_t *v, const omni_dial_state_t *st)
{
    vframe_t vf = { .f = { .res = &v->res, .st = *st } };
    vf.f.st.loupe = loupe_step(st->loupe);
    for (int i = 0; i < OMNI_RINGS; i++) vf.f.pose.val[i] = st->val[i];
    omni_rings_fidelity(&vf.f.pose, st->speed);
    bool still = st->lon_speed == 0, fast = false;
    for (int i = 0; i < OMNI_RINGS; i++) {
        still = still && fabsf(st->speed[i]) < OMNI_STILL;   /* T2 m7: OMNI_STILL decides a still frame */
        fast = fast || vf.f.pose.level[i] > 0;
    }
    vf.f.fast = fast;      /* the fidelity rule: a ring moving fast drops the hand shadows */
    vf.still = still;      /* still (and the first frame after motion stops): full fidelity, the linear globe (R76) */
    omni_dial_marker_box(&vf.f, &vf.mbox);
    return vf;
}

/* True if a and b differ only in the globe's longitude (a drift step at rest). */
static bool lon_only(const vframe_t *a, const vframe_t *b)
{
    vframe_t t = *b;
    t.f.st.lon_deg = a->f.st.lon_deg;
    t.f.st.lon_speed = a->f.st.lon_speed;
    return t.still == a->still && t.f.fast == a->f.fast && memcmp(&t.f.st, &a->f.st, sizeof t.f.st) == 0 &&
           memcmp(&t.f.pose, &a->f.pose, sizeof t.f.pose) == 0;
}

/* What changed from prev to cur (omni_dial_dirty), plus the globe box when the globe kernel changes. A drift step's
 * globe box (the disc and the marker boxes inside it: nothing else changes) becomes the disc's bands, about 15 % fewer
 * pixels, and both marker boxes whole: the bands' margin is horizontal only, and a marker near the limb reaches rows of
 * the next band whose spans are narrower (T7 M1: up to 2 ring pixels outside the bands). A marker reaching out of the
 * box makes the union larger than the box, and it is kept whole. */
/* M7: a list less the open loupe that did not fit in OMNI_RECTS_MAX: counted (`dial`), logged once a view, and the
 * caller renders the list whole instead (correctness over speed: a dropped piece would keep the old pixels). */
static void minus_full(dial_view_t *v, const char *where)
{
    if (__atomic_fetch_add(&v->minus_full, 1u, __ATOMIC_RELAXED) == 0)
        ESP_LOGW(TAG, "%s less the loupe: more than %d rects, rendered whole", where, OMNI_RECTS_MAX);
}

int dv_frame_dirty(dial_view_t *v, const vframe_t *prev, const vframe_t *cur, rects_t *L, bool *drift_out)
{
    omni_rect_t r[OMNI_DIRTY_MAX];
    L->n = 0;
    bool drift_step = false;
    const int n = omni_dial_dirty(prev ? &prev->f : NULL, &cur->f, r);
    const bool drift = prev && v->disc.n && lon_only(prev, cur);
    for (int i = 0; i < n; i++) {
        if (drift && memcmp(&r[i], &v->gbox, sizeof r[i]) == 0) {
            /* the open loupe's content rect (its opaque frame and zoomed content, no globe) does not change either */
            omni_rect_t hole = { 0, 0, 0, 0 };
            if (cur->f.st.loupe >= 1) hole = omni_dial_loupe_cfg(1, v->res.dial_map.cfg.n_theta).rect;
            if (!omni_rects_minus(&v->disc, &hole, L)) {   /* disjoint rows */
                minus_full(v, "disc bands");
                omni_rects_add(L, r[i]);   /* the globe box whole: not a drift step */
                continue;
            }
            omni_rects_add(L, prev->mbox);
            omni_rects_add(L, cur->mbox);
            drift_step = true;
            continue;
        }
        omni_rects_add(L, r[i]);
    }
    if (prev && prev->still != cur->still && v->res.clut) {
        omni_rects_add(L, v->gbox);
        drift_step = false;
    }
    *drift_out = drift_step;
    return L->n;
}

void dv_prepare(dial_view_t *v, const vframe_t *f)
{
    loupe_for(v, f->f.st.loupe);
    v->res.ckernel = f->still ? omni_globe_cspan_lin_fast : v->kmove;   /* R76: linear at rest */
    omni_dial_prepare(&f->f, v->prep);
}

uint16_t *dv_ucell(const dial_view_t *v, int x, int y)
{
    const int S = v->gbox.x1 - v->gbox.x0;
    return v->ucache + (size_t)(y - v->gbox.y0) * S + (x - v->gbox.x0);
}

static ukey_t ukey_of(const dial_view_t *v, const vframe_t *f)
{
    ukey_t k;
    memset(&k, 0, sizeof k);   /* padding too: compared with memcmp */
    if (v->ukey_rings) k.pose = f->f.pose;   /* else: no ring or V mark reaches a chord band (chord_rmax < 125) */
    k.hour = f->f.st.hour_rad;
    k.min = f->f.st.min_rad;
    if (v->ukey_rings) {
        memcpy(k.sel_old, f->f.st.sel_old, sizeof k.sel_old);
        memcpy(k.sel_new, f->f.st.sel_new, sizeof k.sel_new);
        k.mark_old = f->f.st.mark_old;
        k.mark_new = f->f.st.mark_new;
    }
    k.fast = f->f.fast;
    return k;
}

/* R88 candidate 2. The back buffer holds frame n - 2, and frames n - 2 .. n differ only in the globe's longitude (both
 * dirty lists are drift steps): only disc pixels and the marker changed. The disc's chord bands get their cached under
 * stage (rebuilt here when the rings, hands or marks changed since) and the over stage of frame n; the three frames'
 * marker boxes are rendered whole after them. Every pixel then holds frame n's value, as a whole render gives it
 * (test_under_over_drift). */
static bool drift_render(dial_view_t *v, const vframe_t *cur, int back)
{
    uint16_t *fb = v->px[back];
    const ukey_t k = ukey_of(v, cur);
    omni_rect_t hole = { 0, 0, 0, 0 };   /* the open loupe's content: frame n - 2's value is frame n's (drift steps) */
    if (cur->f.st.loupe >= 1) hole = omni_dial_loupe_cfg(1, v->res.dial_map.cfg.n_theta).rect;
    rects_t P = { .n = 0 }, M = { .n = 0 };
    if (!omni_rects_minus(&v->chord, &hole, &P)) {   /* M7: nothing done yet; the caller renders whole */
        minus_full(v, "chord bands");
        return false;
    }
    if (!v->u_ok || memcmp(&k, &v->ukey, sizeof k) != 0) {
        /* the under stage into the chord bands of the back buffer, kept in the cache; the hole, overwritten with it,
         * is rendered whole afterwards */
        v->j_stage = 1;
        dv_set_chunks(v, &v->chord, CHUNK_ROWS);
        dv_dispatch(v, true);
        for (int i = 0; i < v->chord.n; i++) {
            const omni_rect_t *b = &v->chord.r[i];
            const int w = b->x1 - b->x0;
            for (int y = b->y0; y < b->y1; y++)
                memcpy(dv_ucell(v, b->x0, y), fb + (size_t)y * SCR + b->x0, (size_t)w * 2);
        }
        v->ukey = k;
        v->u_ok = true;
        omni_rects_add(&M, hole);
        v->u_builds++;
    } else {
        for (int j = 0; j < P.n; j++) {   /* the cache back into the pieces (the hole keeps its pixels) */
            const omni_rect_t *q = &P.r[j];
            for (int y = q->y0; y < q->y1; y++)
                memcpy(fb + (size_t)y * SCR + q->x0, dv_ucell(v, q->x0, y), (size_t)(q->x1 - q->x0) * 2);
        }
    }
    v->j_stage = 2;
    dv_set_chunks(v, &P, CHUNK_ROWS);
    dv_dispatch(v, true);
    /* then the markers (and a hole the rebuild covered), whole: a separate dispatch, so no pixel is in both at once */
    omni_rects_add(&M, v->mbox_pp);
    omni_rects_add(&M, v->prev.mbox);
    omni_rects_add(&M, cur->mbox);
    if (M.n) {
        v->j_stage = 0;
        dv_set_chunks(v, &M, CHUNK_ROWS);
        dv_dispatch(v, true);
    }
    v->drift_frames++;
    return true;
}

/* The trace and the window's counters for a frame rendered into buffer `back` (rects r, us). */
static void frame_note(dial_view_t *v, const vframe_t *cur, const rects_t *r, int back, uint8_t path_used, bool drift,
                       uint32_t us)
{
    if (cur->f.st.lid || cur->f.st.iris) {   /* the reveal's peak (R70): PSRAM free with the lid or the iris on
                                              * screen, the globe spinning up */
        const uint32_t ps = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        portENTER_CRITICAL(&dial_mux);
        if (v->reveal_frames < 24) {
            v->reveal_log[v->reveal_frames].v = cur->f.st.lid ? cur->f.st.lid_theta * 57.29578f : cur->f.st.iris_ap;
            v->reveal_log[v->reveal_frames].iris = !cur->f.st.lid;
            v->reveal_log[v->reveal_frames].us = us;
        }
        v->reveal_frames++;
        v->reveal_us_sum += us;
        if (us > v->reveal_us_max) v->reveal_us_max = us;
        if (ps < v->reveal_psram_low) v->reveal_psram_low = ps;
        portEXIT_CRITICAL(&dial_mux);
    }
#if CONFIG_OMNI_DEBUG_CONSOLE   /* only `dial` reads them (T12 M7) */
    portENTER_CRITICAL(&dial_mux);
    for (int i = 0; i < OMNI_RINGS; i++) v->lvl_n[i][cur->f.pose.level[i] < 3 ? cur->f.pose.level[i] : 2]++;
    if (cur->f.st.flash > 0) {
        v->flash_frames++;
        v->flash_us_sum += us;
        if (us > v->flash_us_max) v->flash_us_max = us;
    }
    portEXIT_CRITICAL(&dial_mux);
#endif
    int y0 = SCR, y1 = 0;
    for (int i = 0; i < r->n; i++) {
        y0 = r->r[i].y0 < y0 ? r->r[i].y0 : y0;
        y1 = r->r[i].y1 > y1 ? r->r[i].y1 : y1;
    }
    const uint32_t k = v->ntrace++ % 16;
    v->trace[k].hour = cur->f.st.hour_rad;
    memcpy(v->trace[k].lv, cur->f.pose.level, 4);
    v->trace[k].fast = cur->f.fast;
    v->trace[k].path = path_used;
    v->trace[k].back = (uint8_t)back;
    v->trace[k].nr = (uint8_t)r->n;
    v->trace[k].dd = (uint8_t)(drift | v->d_prev_drift << 1);
    v->trace[k].y0 = (int16_t)y0;
    v->trace[k].y1 = (int16_t)y1;
    v->trace[k].min = cur->f.st.min_rad;
    portENTER_CRITICAL(&dial_mux);
    v->st_frames++;
    v->st_sum += us;
    v->st_px += omni_rects_px(r);
    if (us > v->st_max) v->st_max = us;
    if (us > 30000) {
        v->st_late++;
        v->st_late_drift += path_used == 1;   /* T7 M8 */
    }
    portEXIT_CRITICAL(&dial_mux);
    disp_stats_frame_rendered();
}

/* Renders r into j_fb on both cores, whole or at half rows, from the frame prepared (no drift cache). */
static void render_rects(dial_view_t *v, const rects_t *r, bool lossy)
{
    v->j_stage = 0;
    v->j_lossy = lossy;
    dv_set_chunks(v, r, CHUNK_ROWS);
    dv_dispatch(v, true);
    v->j_lossy = false;
}

/* Lever B's "still" (D7, D8): the frame is at rest. vframe.still is the globe kernel's (R76) and the fidelity rule's,
 * and it holds under a lid or an iris over a still globe; those reveals are motion here (full-screen frames). The idle
 * drift moves the globe (lon_speed about 0.07 deg a frame), so a face at rest is rarely settled: motion mostly ends by
 * D8's third frame under DISP_DIRECT_MIN_PX, and its back-off then runs out (R2-1 keeps disp_direct_still for a
 * settled frame with no motion-dirty pixels). */
static bool settled(const vframe_t *f) { return f->still && !f->f.st.lid && !f->f.st.iris; }

typedef enum { IMG_SHOWN, IMG_SAME, IMG_CLAIMED } img_t;

/* IMAGE: the worker renders cur into the back buffer, if anything changed. IMG_CLAIMED: poll claimed the direct path
 * while it rendered, so the frame is not swapped in (the panel is the path's now): the caller sends it as the first
 * direct frame. */
static img_t image_frame(dial_view_t *v, const vframe_t *cur_in)
{
    const vframe_t cur = *cur_in;
    portENTER_CRITICAL(&dial_mux);
    const unsigned perf = v->perf;
    const uint32_t epoch = v->perf_epoch, gen = v->mode_gen;
    const bool old_buf[2] = { v->buf_old[0], v->buf_old[1] };
    portEXIT_CRITICAL(&dial_mux);
    if (gen != v->w_gen) {
        /* back from DIRECT (an exit or an abort): the panel shows the front buffer (an exit swapped it in, an abort
         * invalidated it whole), so the dirty rects run from its frame; the buffers' history is buf_old's (B17) */
        v->w_gen = gen;
        v->have_prev = v->buf_valid[v->front];
        if (v->have_prev) v->prev = v->bufframe[v->front];
        v->d_prev_drift = false;
    }
    const bool repaint = epoch != v->perf_seen;   /* `dial perf`: the buffers were drawn the other way */
    rects_t d;
    bool drift;
    if (dv_frame_dirty(v, v->have_prev ? &v->prev : NULL, &cur, &d, &drift) == 0 && !repaint) {
        portENTER_CRITICAL(&dial_mux);
        if (v->mode_gen == gen) v->want_direct = false;
        portEXIT_CRITICAL(&dial_mux);
        return IMG_SAME;
    }
    const int back = !v->front;   /* front changes only in poll, which waits for ready */
    if (repaint) {
        v->perf_seen = epoch;
        v->buf_valid[0] = v->buf_valid[1] = false;
        v->u_ok = false;
    }
    const bool old = old_buf[back] && v->buf_valid[back];
    rects_t r = { .n = 0 };
    if (!v->buf_valid[back]) {
        omni_rects_add(&r, DV_SCREEN);
    } else if (old) {   /* B17: what differs from the buffer's own frame, never d_prev (it describes another buffer) */
        bool dr;
        dv_frame_dirty(v, &v->bufframe[back], &cur, &r, &dr);
        if (omni_rect_area(&v->lossy[back])) omni_rects_add(&r, v->lossy[back]);
    } else {
        r = v->d_prev;   /* the back buffer holds the frame before the last one */
        for (int i = 0; i < d.n; i++) omni_rects_add(&r, d.r[i]);
    }
    const int64_t t0 = esp_timer_get_time();
    dv_prepare(v, &cur);   /* before the helper is notified: both cores read it */
    v->j_fb = v->px[back];
    /* R88 candidate 5, measured: a flash frame renders half its rows (the flash's full-screen frames 6.3 -> 8.1 fps).
     * Whirl frames stay exact: half rows only tied there (10.2 -> 10.4; a 1-row render's setup eats the rows saved). An
     * exact frame re-renders whatever of this buffer was rendered so, and poll redraws both buffers' such pixels. */
    const bool lossy = (perf & PERF_LOSSY) && cur.f.st.flash > 0;
    rects_t inv = d;
    if (repaint) omni_rects_add(&inv, DV_SCREEN);
    const bool cleanup = !lossy && omni_rect_area(&v->lossy[back]);
    if (cleanup) {
        omni_rects_add(&r, v->lossy[back]);
        omni_rects_add(&inv, v->lossy[back]);
        v->lossy[back] = (omni_rect_t){ 0, 0, 0, 0 };
    }
    if (omni_rect_area(&v->lossy[!back])) omni_rects_add(&inv, v->lossy[!back]);   /* the shown buffer's half rows go */
    uint8_t path_used = 0;   /* 0 whole, 1 drift, 2 none (`dial perf render off`: the display path's ceiling) */
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (!(perf & PERF_RENDER)) {   /* `dial perf render off` only: a release build always renders (R101) */
        path_used = 2;
    } else
#endif
    if (!lossy && !cleanup && !old && (perf & PERF_DRIFT) && v->ucache && drift && v->d_prev_drift &&
        v->buf_valid[back] && drift_render(v, &cur, back)) {
        path_used = 1;
    } else {
        render_rects(v, &r, lossy);
        if (lossy) {
            for (int i = 0; i < r.n; i++) v->lossy[back] = omni_rect_unite(v->lossy[back], r.r[i]);
            for (int i = 0; i < r.n; i++) omni_rects_add(&inv, r.r[i]);
        }
    }
    const uint32_t us = dv_us_since(t0);
    v->buf_valid[back] = true;
    v->bufframe[back] = cur;
    frame_note(v, &cur, &r, back, path_used, drift, us);
    v->mbox_pp = v->have_prev ? v->prev.mbox : (omni_rect_t){ 0, 0, 0, 0 };
    v->prev = cur;
    v->have_prev = true;
    v->d_prev = d;
    v->d_prev_drift = drift;
    /* D7 entry: a frame that is not still and this dirty is full-screen motion (it goes out here, through IMAGE) */
    const bool motion = DISP_DIRECT_BUILT && !settled(&cur) && omni_rects_px(&d) >= DISP_DIRECT_MIN_PX;
    /* R2-1: the first exact frame after a motion frame: settled, not lossy, with no motion-dirty pixels; calm frames
     * with no motion before them do not count */
    const bool calm = settled(&cur) && !lossy && omni_rects_px(&d) < DISP_DIRECT_MIN_PX;
    const bool first_rest = calm && v->w_moved;
    if (motion) v->w_moved = true;
    else if (calm) v->w_moved = false;
    portENTER_CRITICAL(&dial_mux);
    const bool same_gen = v->mode_gen == gen, claimed = !same_gen && v->mode == DV_DIRECT;
    if (same_gen || claimed) v->buf_old[back] = false;   /* it holds bufframe[back] exactly (an abort's stays set) */
    if (!claimed) {
        v->back_rects = inv;
        v->ready = true;
    }
    if (same_gen) {
        v->want_direct = motion;
        if (settled(&cur)) v->backoff_until = 0;   /* a still frame ends the client's back-off (D7) */
        if (first_rest) v->still_req = true;
    }
    portEXIT_CRITICAL(&dial_mux);
    return claimed ? IMG_CLAIMED : IMG_SHOWN;
}

/* ---- Task 6: the LVGL overlays' sets (LVGL context publishes, the worker takes) */

bool dv_lov_take(dial_view_t *v, rects_t *areas, bool all)
{
    for (;;) {   /* a publish takes at most its snapshots (<= 2 ms each, R120) */
        portENTER_CRITICAL(&dial_mux);
        const bool busy = v->lov_writing;
        if (!busy) v->lov_copying = true;
        portEXIT_CRITICAL(&dial_mux);
        if (!busy) break;
        vTaskDelay(1);
    }
    /* lov_pub, lov_pub_n, lov_areas and lov_fresh change only inside a publish, which waits for lov_copying */
    bool ok = true;
    /* m3: a delete waits for this copy, or moves the gen first (then it is skipped) */
    disp_direct_overlay_hold(true);
    for (int i = 0; i < v->lov_owed_pub.n; i++) omni_rects_add(&v->lov_owed, v->lov_owed_pub.r[i]);
    v->lov_owed_pub.n = 0;
    /* a published sprite was freed: keep the old copy */
    const bool stale = v->lov_pub_gen != disp_direct_overlay_gen();
    if (!stale && (all || (v->lov_fresh && (v->lov_areas.n || v->lov_pub_n != v->lov_n)))) {
        size_t need = 0;
        for (int i = 0; i < v->lov_pub_n; i++)
            need += (size_t)v->lov_pub[i].stride * (size_t)(v->lov_pub[i].r.y1 - v->lov_pub[i].r.y0);
        if (need > v->lov_cap) {
            heap_caps_free(v->lov_mem);
            v->lov_mem = heap_caps_malloc(need * 4, MALLOC_CAP_SPIRAM);
            v->lov_cap = v->lov_mem ? need : 0;
        }
        v->lov_n = 0;
        if (need && !v->lov_mem) {
            ok = false;
        } else {
            size_t off = 0;
            for (int i = 0; i < v->lov_pub_n; i++) {
                const size_t px = (size_t)v->lov_pub[i].stride * (size_t)(v->lov_pub[i].r.y1 - v->lov_pub[i].r.y0);
                memcpy(v->lov_mem + off, v->lov_pub[i].argb, px * 4);
                v->lov[i] = v->lov_pub[i];
                v->lov[i].argb = v->lov_mem + off;
                off += px;
            }
            v->lov_n = v->lov_pub_n;
        }
        if (!ok)   /* m1: nothing blends now, yet LVGL shows them: the hand-back redraws what was published */
            for (int i = 0; i < v->lov_pub_n; i++)
                omni_rects_add(&v->lov_owed, omni_band_even(v->lov_pub[i].r, SCR, SCR));
    }
    disp_direct_overlay_hold(false);
    /* stale: lov_fresh stays and the areas wait for the next publish, which replaces the set: lov_due sees the gen
     * moved and publishes again (minor 9), so a stale set never stalls the publishes */
    if (!stale) v->lov_fresh = false;
    if (areas && !stale) {
        for (int i = 0; i < v->lov_areas.n; i++) omni_rects_add(areas, v->lov_areas.r[i]);
        v->lov_areas.n = 0;
    }
    portENTER_CRITICAL(&dial_mux);
    v->lov_copying = false;
    portEXIT_CRITICAL(&dial_mux);
    return ok;
}

bool dv_lov_publish(dial_view_t *v, bool claim)
{
    portENTER_CRITICAL(&dial_mux);
    const bool busy = v->lov_copying;
    if (!busy) v->lov_writing = true;
    portEXIT_CRITICAL(&dial_mux);
    if (busy) return false;   /* the next poll publishes */
    if (claim) {   /* minor 10: what the last claim's sets owed after its last take is not this claim's */
        v->lov_owed_pub.n = 0;
        v->lov_areas.n = 0;
    }
    rects_t a = { .n = 0 }, owed = { .n = 0 };
    v->lov_pub_n = disp_direct_overlays(v->lov_pub, &a, &owed);
    v->lov_pub_gen = disp_direct_overlay_gen();
    for (int i = 0; i < a.n; i++) omni_rects_add(&v->lov_areas, a.r[i]);
    for (int i = 0; i < owed.n; i++) omni_rects_add(&v->lov_owed_pub, owed.r[i]);   /* m1 */
    portENTER_CRITICAL(&dial_mux);
    v->lov_fresh = true;
    v->lov_writing = false;
    portEXIT_CRITICAL(&dial_mux);
    return true;
}

/* LVGL ctx: the worker took the last published set, or it went stale (a sprite freed, m3) and the take will skip it:
 * a publish is due (minor 9). One publish per take: at most one snapshot per overlay per direct frame (minor 7). */
static bool lov_due(dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    const bool fresh = v->lov_fresh;
    portEXIT_CRITICAL(&dial_mux);
    return !fresh || v->lov_pub_gen != disp_direct_overlay_gen();
}

/* ---- Lever B: the direct path (Stage 4a Task 5). The worker's side; the LVGL side is poll's. */

/* The claim's entry, once per claim (B18): the panel shows the buffer that was the front at the claim (dd_front), its
 * half rows included. Not v->front: an IMAGE frame that became ready while poll claimed is swapped in by that poll with
 * LVGL's flush dropped, so it never reached the panel (final review I1). */
static void direct_entry(dial_view_t *v, uint32_t gen)
{
    if (v->dd_gen == gen) return;
    v->dd_gen = gen;
    portENTER_CRITICAL(&dial_mux);
    const int f = v->dd_front;
    portEXIT_CRITICAL(&dial_mux);
    v->panel = v->bufframe[f];
    v->panel_unknown = !v->buf_valid[f];
    v->lossy_panel = v->lossy[f];
    v->below_n = 0;
    v->lov_owed.n = 0;
    v->lov_entry = true;   /* LVGL drew the overlays on the panel: the first direct frame sends them all again */
    v->lov_sent.n = 0;
    const uint32_t lost = disp_direct_lost();
    /* minor 2: disp_direct_shown() is global and never reset (an earlier view, `disp bench direct`): this view's frame
     * numbers go on from it, so direct_exit's wait compares like with like. The sender is idle at a claim. */
    const uint32_t shown = disp_direct_shown();
    portENTER_CRITICAL(&dial_mux);
    v->lost_seen = lost;
    v->dd_seq = v->dd_last_seq = shown;
    portEXIT_CRITICAL(&dial_mux);
}

typedef enum { DD_SENT, DD_DEAD, DD_ABORTED } dd_t;

/* Sends L (what differs from the panel) through the direct path, rendered from cur, or copied from src (a PSRAM buffer
 * holding cur, with half rows in src_lossy). DD_ABORTED: mode_gen moved under it (B19), the caller renders on through
 * IMAGE. DD_DEAD: a chunk was refused with mode_gen unchanged (the path went inactive): poll aborts next. */
static dd_t direct_send(dial_view_t *v, const vframe_t *cur, const rects_t *L, unsigned perf, uint32_t gen,
                        const uint16_t *src, omni_rect_t src_lossy)
{
    const bool lossy = !src && (perf & PERF_LOSSY) && cur->f.st.flash > 0;
    rects_t S = *L;
    if (!lossy && omni_rect_area(&v->lossy_panel)) omni_rects_add(&S, v->lossy_panel);   /* the panel's half rows go */
    if (!dv_lov_take(v, &S, v->lov_entry)) {   /* no PSRAM for the overlays' copy: sent without them, then the exit */
        portENTER_CRITICAL(&dial_mux);
        if (v->mode_gen == gen) v->exit_req = true;
        portEXIT_CRITICAL(&dial_mux);
    }
    if (v->lov_entry)
        for (int i = 0; i < v->lov_n; i++) omni_rects_add(&S, omni_band_even(v->lov[i].r, SCR, SCR));
    v->lov_entry = false;
    int n = omni_band_chunks(&S, DISP_DIRECT_ROWS, SCR, SCR, v->j_chunk, MAX_CHUNKS, NULL);
    if (n < 0) {   /* more than MAX_CHUNKS: the bounding rect (at most 59 chunks) */
        rects_t B = { .n = 0 };
        omni_rect_t bb = S.r[0];
        for (int i = 1; i < S.n; i++) bb = omni_rect_unite(bb, S.r[i]);
        omni_rects_add(&B, bb);
        n = omni_band_chunks(&B, DISP_DIRECT_ROWS, SCR, SCR, v->j_chunk, MAX_CHUNKS, NULL);
    }
    if (n <= 0) {   /* nothing differs: the panel shows cur already, and the overlays as they are */
        v->panel = *cur;
        return DD_SENT;
    }
    const int64_t t0 = esp_timer_get_time();
    if (!src) dv_prepare(v, cur);
    const uint32_t lost0 = disp_direct_lost();
    v->j_n = n;
    v->j_next = 0;
    v->j_fb = NULL;
    v->j_src = src;
    v->j_lossy = lossy;
    v->j_perf = perf;
    v->j_dd_fail = false;
    v->j_lov = v->lov;
    v->j_lov_n = v->lov_n;
    v->j_direct = true;
    dv_dispatch(v, true);
    v->j_direct = false;
    v->j_lov_n = 0;
    v->j_lossy = false;
    v->j_src = NULL;
    disp_direct_frame_end(++v->dd_seq);
    const uint32_t us = dv_us_since(t0);
    const bool cut = v->j_dd_fail;
    const uint32_t lost1 = disp_direct_lost();
    portENTER_CRITICAL(&dial_mux);
    const bool same = v->mode_gen == gen;
    if (same) {
        v->dd_last_seq = v->dd_seq;
        v->dd_ended++;
        v->lost_seen = lost1;
        v->dd_frames++;
        v->dd_frame_us_sum += us;
        if (us > v->dd_frame_us_max) v->dd_frame_us_max = us;
        v->dd_copies += src != NULL;
        v->st_frames++;
        v->st_sum += us;
        v->st_px += omni_rects_px(&S);
        if (us > v->st_max) v->st_max = us;
        if (cut) v->dd_dead = true;
    }
    portEXIT_CRITICAL(&dial_mux);
    disp_stats_frame_rendered();
    if (!same) return DD_ABORTED;
    v->w_moved = true;
    if (cut || lost1 != lost0) {   /* D3: a band never reached the panel */
        v->panel_unknown = true;
    } else {
        /* I1: a whole screen on the wire with no loss makes the panel known again (L was SCREEN for that frame only) */
        for (int i = 0; i < S.n; i++)
            if (memcmp(&S.r[i], &DV_SCREEN, sizeof S.r[i]) == 0) v->panel_unknown = false;
        v->panel = *cur;
        v->lov_sent.n = 0;   /* the overlays the panel shows now (any older rect was in S: redrawn without it) */
        for (int i = 0; i < v->lov_n; i++) omni_rects_add(&v->lov_sent, omni_band_even(v->lov[i].r, SCR, SCR));
        if (src) v->lossy_panel = src_lossy;
        else if (!lossy) v->lossy_panel = (omni_rect_t){ 0, 0, 0, 0 };
        else
            for (int i = 0; i < S.n; i++) v->lossy_panel = omni_rect_unite(v->lossy_panel, S.r[i]);
    }
    return cut ? DD_DEAD : DD_SENT;
}

/* The exit frame (D7, D8, B17): rendered into the back buffer by what differs from the buffer's own frame (never the
 * drift cache), and swapped in by poll after the release with back_rects = what differs from the panel. */
static void exit_frame(dial_view_t *v, const vframe_t *cur, unsigned perf, uint32_t epoch, uint32_t gen, int why,
                       bool below)
{
    const int64_t t0 = esp_timer_get_time();
    const int back = !v->front;
    const bool repaint = epoch != v->perf_seen;
    if (repaint) {   /* A5: `dial perf` while DIRECT */
        v->perf_seen = epoch;
        v->buf_valid[0] = v->buf_valid[1] = false;
        v->u_ok = false;
    }
    rects_t r = { .n = 0 }, inv = { .n = 0 };
    bool dr;
    if (!v->buf_valid[back]) {
        omni_rects_add(&r, DV_SCREEN);
    } else {
        dv_frame_dirty(v, &v->bufframe[back], cur, &r, &dr);
        if (omni_rect_area(&v->lossy[back])) omni_rects_add(&r, v->lossy[back]);
    }
    if (v->panel_unknown || repaint) {
        omni_rects_add(&inv, DV_SCREEN);
    } else {
        dv_frame_dirty(v, &v->panel, cur, &inv, &dr);
        if (omni_rect_area(&v->lossy_panel)) omni_rects_add(&inv, v->lossy_panel);
        /* Task 6: the overlays the direct frames drew, and the set taken last: LVGL draws them itself at rest */
        for (int i = 0; i < v->lov_sent.n; i++) omni_rects_add(&inv, v->lov_sent.r[i]);
        for (int i = 0; i < v->lov_n; i++) omni_rects_add(&inv, omni_band_even(v->lov[i].r, SCR, SCR));
        /* m1: failed copies or snapshots */
        for (int i = 0; i < v->lov_owed.n; i++) omni_rects_add(&inv, v->lov_owed.r[i]);
    }
    const bool lossy = (perf & PERF_LOSSY) && cur->f.st.flash > 0;
    v->lossy[back] = (omni_rect_t){ 0, 0, 0, 0 };   /* r holds them: re-rendered */
    uint8_t path_used = 0;
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (!(perf & PERF_RENDER)) path_used = 2;
    else
#endif
    if (r.n) {
        dv_prepare(v, cur);
        v->j_fb = v->px[back];
        render_rects(v, &r, lossy);
    }
    if (lossy) {
        for (int i = 0; i < r.n; i++) v->lossy[back] = omni_rect_unite(v->lossy[back], r.r[i]);
        for (int i = 0; i < r.n; i++) omni_rects_add(&inv, r.r[i]);
    }
    const uint32_t us = dv_us_since(t0);
    v->mbox_pp = v->bufframe[back].mbox;
    v->bufframe[back] = *cur;
    v->buf_valid[back] = true;
    frame_note(v, cur, &r, back, path_used, false, us);
    v->prev = *cur;
    v->have_prev = true;
    v->d_prev = r;
    v->d_prev_drift = false;
    v->u_ok = false;
    v->w_moved = !(settled(cur) && below && !lossy);   /* a settled exit is the first exact frame (exit_still) */
    portENTER_CRITICAL(&dial_mux);
    if (v->mode_gen == gen) {
        v->mode = DV_EXIT;
        v->exit_still = why == DV_EX_STILL && !lossy && below;   /* R2-1, as image_frame's */
    } else {
        omni_rects_add(&inv, DV_SCREEN);   /* aborted while it rendered: IMAGE again, whole */
    }
    v->exit_req = false;
    v->buf_old[back] = false;
    v->back_rects = inv;
    v->ready = true;
    v->dd_exits++;
    v->dd_exit_why[why]++;
    v->dd_exit_us_sum += us;
    if (us > v->dd_exit_us_max) v->dd_exit_us_max = us;
    portEXIT_CRITICAL(&dial_mux);
}

typedef enum { STEP_DONE, STEP_DEAD, STEP_IMAGE } step_t;

/* A frame taken while DIRECT: the exit frame (D7, D8, A5), or a direct frame. */
static step_t direct_step(dial_view_t *v, const vframe_t *cur, unsigned perf, uint32_t epoch, uint32_t gen, bool req)
{
    direct_entry(v, gen);
    const uint32_t lost = disp_direct_lost();
    portENTER_CRITICAL(&dial_mux);
    const bool moved = lost != v->lost_seen;
    v->lost_seen = lost;
    portEXIT_CRITICAL(&dial_mux);
    if (moved) v->panel_unknown = true;   /* a band of an earlier frame was lost after it ended (D3) */
    /* D8 judges the motion (what differs from the last frame sent whole), not the forced whole screen (I1) */
    rects_t M = { .n = 0 }, L = { .n = 0 };
    bool drift;
    dv_frame_dirty(v, &v->panel, cur, &M, &drift);
    if (v->panel_unknown) omni_rects_add(&L, DV_SCREEN);
    else L = M;
    const bool below = omni_rects_px(&M) < DISP_DIRECT_MIN_PX;
    const int why = settled(cur) ? DV_EX_STILL : req ? DV_EX_REQ : epoch != v->perf_seen ? DV_EX_EPOCH
                  : below && v->below_n >= 2 ? DV_EX_BELOW : -1;
    if (why >= 0) {
        exit_frame(v, cur, perf, epoch, gen, why, below);
        return STEP_DONE;
    }
    v->below_n = below ? v->below_n + 1 : 0;
    const dd_t r = direct_send(v, cur, &L, perf, gen, NULL, (omni_rect_t){ 0, 0, 0, 0 });
    return r == DD_ABORTED ? STEP_IMAGE : r == DD_DEAD ? STEP_DEAD : STEP_DONE;
}

/* The frame the worker rendered through IMAGE while poll claimed: it is in the back buffer, so it goes out as the
 * first direct frame by a copy of what differs from the panel, not a second render. */
static void direct_copy(dial_view_t *v, const vframe_t *cur)
{
    portENTER_CRITICAL(&dial_mux);
    const uint32_t gen = v->mode_gen;
    const unsigned perf = v->perf;
    const bool direct = v->mode == DV_DIRECT;
    portEXIT_CRITICAL(&dial_mux);
    if (!direct) {   /* M6: aborted meanwhile (IMAGE again): the frame it took is swapped in, the screen redrawn */
        portENTER_CRITICAL(&dial_mux);
        v->back_rects = (rects_t){ .r = { DV_SCREEN }, .n = 1 };
        v->ready = true;
        portEXIT_CRITICAL(&dial_mux);
        return;
    }
    direct_entry(v, gen);
    const int back = !v->front;
    rects_t L = { .n = 0 };
    bool drift;
    if (v->panel_unknown) omni_rects_add(&L, DV_SCREEN);
    else dv_frame_dirty(v, &v->panel, cur, &L, &drift);
    if (direct_send(v, cur, &L, perf | PERF_RENDER, gen, v->px[back], v->lossy[back]) == DD_ABORTED) {
        portENTER_CRITICAL(&dial_mux);
        v->back_rects = (rects_t){ .r = { DV_SCREEN }, .n = 1 };   /* IMAGE again: show the buffer it is in */
        v->ready = true;
        portEXIT_CRITICAL(&dial_mux);
    }
}

/* The worker: renders state st (submit seq) by the mode. false: not taken (a direct frame cut short: it is pending
 * again, for poll's abort to bring it through IMAGE). */
static bool render_frame(dial_view_t *v, const omni_dial_state_t *st, uint32_t seq)
{
    const vframe_t cur = dv_make_frame(v, st);
    portENTER_CRITICAL(&dial_mux);
    const unsigned perf = v->perf;
    const uint32_t epoch = v->perf_epoch, gen = v->mode_gen;
    const bool direct = v->mode == DV_DIRECT, req = v->exit_req;
    portEXIT_CRITICAL(&dial_mux);
    if (direct) {
        const step_t s = direct_step(v, &cur, perf, epoch, gen, req);
        if (s == STEP_DONE) return true;
        if (s == STEP_DEAD) {
            portENTER_CRITICAL(&dial_mux);
            if (!v->has_pending) {
                v->pending = *st;
                v->pending_seq = seq;
                v->has_pending = true;
            }
            portEXIT_CRITICAL(&dial_mux);
            return false;
        }
        /* STEP_IMAGE: aborted under the frame (B19): it renders on through IMAGE (image_frame starts from the front) */
    }
    if (image_frame(v, &cur) == IMG_CLAIMED) direct_copy(v, &cur);
    return true;
}

/* ---- Build (the worker, BUILD_PRIO) */

static void lut_rows_steps(dial_view_t *v, int who, int py0, int py1)
{
    (void)who;
    for (int py = py0; py < py1 && !quitting(v); py += LUT_ROWS) {
        omni_globe_lut_rows(&v->lut, py, dv_imin(py + LUT_ROWS, py1));
        vTaskDelay(1);
    }
}
static void pack_spans(dial_view_t *v, int who, int s0, int s1)
{
    (void)who;
    omni_globe_compact_spans(&v->clut, &v->lut, (unsigned)s0, (unsigned)s1);
}

static bool build_layer(dial_view_t *v, int slot, omni_dial_layer_t *L, const char *name, int x, int y)
{
    if (quitting(v)) return false;   /* T7 M2: a destroy waits for one layer at most (the overlays are 89 ms) */
    lv_image_dsc_t d;
    if (!assets_image(name, &d) || d.header.cf != LV_COLOR_FORMAT_ARGB8888) {
        ESP_LOGE(TAG, "%s: missing or not ARGB8888", name);
        return false;
    }
    const uint32_t *argb = (const uint32_t *)(const void *)d.data;
    const uint16_t w = (uint16_t)d.header.w, h = (uint16_t)d.header.h, stride = (uint16_t)(d.header.stride / 4);
    const size_t n = omni_overlay_bytes(argb, w, h, stride);
    v->ov_mem[slot] = ps_alloc(v, n);
    L->x = (int16_t)x;
    L->y = (int16_t)y;
    return v->ov_mem[slot] && omni_overlay_build(&L->ov, argb, w, h, stride, v->ov_mem[slot], n);
}

static bool build_bloom(omni_dial_a8_t *B, const char *name, int x, int y, uint16_t in, uint16_t out)
{
    lv_image_dsc_t d;
    if (!assets_image(name, &d) || d.header.cf != LV_COLOR_FORMAT_A8 || d.header.stride != d.header.w) {
        ESP_LOGE(TAG, "%s: missing or not A8", name);
        return false;
    }
    *B = (omni_dial_a8_t){ .a = d.data, .x = (int16_t)x, .y = (int16_t)y, .w = (int16_t)d.header.w,
                           .h = (int16_t)d.header.h, .rgb_in = in, .rgb_out = out };
    return true;
}

/* R80: the dial map (variant B only, T2 m13) from the prebuilt asset, copied to PSRAM; built here on any mismatch. */
static bool map_create(dial_view_t *v, const omni_pmap_cfg_t *cfg)
{
    size_t size;
    const void *f = assets_raw(ASSET_OMNI_DIALMAP, &size);
    char why[64] = "no " ASSET_OMNI_DIALMAP;
    v->map_bytes = f ? omni_pmap_file_mem(f, size) : 0;
    if (f && !v->map_bytes) snprintf(why, sizeof why, "malformed header");
    v->map_mem = v->map_bytes ? ps_alloc(v, v->map_bytes) : NULL;
    if (v->map_bytes && !v->map_mem) snprintf(why, sizeof why, "no PSRAM for %u B", (unsigned)v->map_bytes);
    v->map_loaded = v->map_mem && omni_pmap_load(&v->res.dial_map, cfg, f, size, v->map_mem, v->map_bytes, why, sizeof why);
    if (v->map_loaded) return true;
    heap_caps_free(v->map_mem);
    portENTER_CRITICAL(&dial_mux);
    v->bytes -= v->map_mem ? (uint32_t)v->map_bytes : 0;
    portEXIT_CRITICAL(&dial_mux);
    v->map_bytes = omni_pmap_bytes(cfg, false, true);
    v->map_mem = ps_alloc(v, v->map_bytes);
    const bool ok = v->map_mem && omni_pmap_build(&v->res.dial_map, cfg, false, true, v->map_mem, v->map_bytes);
    ESP_LOGW(TAG, "dial map not loaded (%s): built%s", why, ok ? "" : ", and the build FAILED");
    return ok;
}

/* Each phase checks quit (T7 M2): a destroy during the build waits for one step, not the build's 500 ms. */
static bool build_steps(dial_view_t *v)
{
    const int64_t t0 = esp_timer_get_time();
    omni_dial_res_t *R = &v->res;
    bool ok = true;
    /* the strips, in place (R70), the L0 planes copied to PSRAM (R79: slow turns miss 30 fps from flash, Task 2) */
    for (int l = 0; l < 3 && ok; l++) {
        char name[24];
        snprintf(name, sizeof name, "omni_strip%d.bin", l);
        size_t size;
        const void *p = assets_raw(name, &size);
        ok = p && omni_strip_map(&v->strip[l], p, size) && v->strip[l].level == l;
        if (!ok) ESP_LOGE(TAG, "%s: missing or malformed", name);
        v->strip_flash += size;
        R->strips[l] = &v->strip[l];
    }
    if (ok) {
        omni_strip_t *s = &v->strip[0];
        const size_t n = (size_t)s->stride * s->n_r;
        v->l0_bytes = n * 3;
        v->l0_copy = ps_alloc(v, v->l0_bytes);
        ok = v->l0_copy != NULL;
        if (ok) {
            uint8_t *p = v->l0_copy;
            memcpy(p, s->rgb, n * 2);
            memcpy(p + n * 2, s->a, n);
            s->rgb = (const uint16_t *)(void *)p;
            s->a = p + n * 2;
        }
    }
    ok = ok && !quitting(v);
    const uint32_t strip_us = dv_us_since(t0);
    int64_t t1 = esp_timer_get_time();
    if (ok) {
        const omni_pmap_cfg_t dc = omni_dial_pmap_cfg(v->strip[0].n_theta);
        ok = map_create(v, &dc);
    }
    if (ok) {
        char why[96];
        ok = omni_rings_compatible(R->strips, &R->dial_map, why, sizeof why);
        if (!ok) ESP_LOGE(TAG, "strips do not fit the dial map: %s", why);
    }
    const uint32_t map_us = dv_us_since(t1);
    t1 = esp_timer_get_time();
    lv_image_dsc_t bd;
    ok = ok && !quitting(v) && assets_image(ASSET_OMNI_BASE, &bd) && bd.header.cf == LV_COLOR_FORMAT_RGB565 && bd.header.w == SCR &&
         bd.header.h == SCR && bd.header.stride == SCR * 2;
    if (ok) R->base = (const uint16_t *)(const void *)bd.data;   /* in place (R70), or the PSRAM copy below (R114) */
    /* R114 (lever A): flash reads cost the kernel; a failed copy keeps the mapping */
    if (ok && (v->perf & PERF_BASE)) {
        const size_t bn = (size_t)SCR * SCR * 2;
        v->base_copy = ps_alloc(v, bn);
        if (v->base_copy) {
            memcpy(v->base_copy, bd.data, bn);
            R->base = (const uint16_t *)v->base_copy;
        } else {
            ESP_LOGW(TAG, "no PSRAM for the omni_base copy; reading the flash mapping");
        }
    }
    if (ok) {   /* the magnifier: its map (B) and the base zoomed under it */
        const omni_pmap_cfg_t mc = omni_dial_mag_cfg(v->strip[0].n_theta);
        const size_t mb = (omni_pmap_bytes(&mc, false, true) + 15) / 16 * 16,
                     pb = (size_t)(mc.rect.x1 - mc.rect.x0) * (mc.rect.y1 - mc.rect.y0) * 2;
        v->mag_mem = ps_alloc(v, mb + pb);   /* the map, then the zoomed base (16-byte aligned) */
        ok = v->mag_mem && omni_pmap_build(&R->mag_map, &mc, false, true, v->mag_mem, mb);
        if (ok) {
            R->mag_base = (uint16_t *)(void *)((uint8_t *)v->mag_mem + mb);
            omni_dial_zoom_base(R->base, &mc, R->mag_base);
        }
    }
    if (ok) {   /* the loupe at its sizes, two at a time (one on each core) */
        for (int i = 0; i < LOUPE_STEPS && !quitting(v); i += 2) {
            help_start(v, loupe_build, i + 1, 0);
            loupe_build(v, 0, i, 0);
            help_wait(v, false);
        }
        for (int i = 0; i < LOUPE_STEPS; i++) ok = ok && v->loupe_ok[i];
        if (!ok && !quitting(v)) ESP_LOGE(TAG, "loupe maps not built");
        R->loupe_map = NULL;
        R->loupe_base = NULL;
    }
    const uint32_t mag_us = dv_us_since(t1);
    t1 = esp_timer_get_time();
    ok = ok && build_layer(v, 0, &R->top, ASSET_OMNI_TOP, ASSET_OMNI_TOP_X, ASSET_OMNI_TOP_Y) &&
         build_layer(v, 1, &R->over, ASSET_OMNI_OVER, ASSET_OMNI_OVER_X, ASSET_OMNI_OVER_Y) &&
         build_layer(v, 2, &R->dome, ASSET_OMNI_DOME, ASSET_OMNI_DOME_X, ASSET_OMNI_DOME_Y) &&
         build_layer(v, 3, &R->glass, ASSET_OMNI_GLASS, ASSET_OMNI_GLASS_X, ASSET_OMNI_GLASS_Y) &&
         build_layer(v, 4, &R->lens[0], ASSET_OMNI_LENS_RED_OFF, ASSET_OMNI_LENS_RED_OFF_X, ASSET_OMNI_LENS_RED_OFF_Y) &&
         build_layer(v, 5, &R->lens[1], ASSET_OMNI_LENS_RED_ON, ASSET_OMNI_LENS_RED_ON_X, ASSET_OMNI_LENS_RED_ON_Y) &&
         build_layer(v, 6, &R->lens[2], ASSET_OMNI_LENS_GREEN_OFF, ASSET_OMNI_LENS_GREEN_OFF_X, ASSET_OMNI_LENS_GREEN_OFF_Y) &&
         build_layer(v, 7, &R->lens[3], ASSET_OMNI_LENS_GREEN_ON, ASSET_OMNI_LENS_GREEN_ON_X, ASSET_OMNI_LENS_GREEN_ON_Y);
    /* the blooms (A8, in place), tinted by the stops Task 6 derived (R85: red in (255,113,63) out (200,20,10), green in
     * (149,255,146) out (20,180,60)) */
    ok = ok && build_bloom(&R->bloom[0], ASSET_OMNI_BLOOM_RED, ASSET_OMNI_BLOOM_RED_X, ASSET_OMNI_BLOOM_RED_Y,
                           omni_pack565(255, 113, 63), omni_pack565(200, 20, 10)) &&
         build_bloom(&R->bloom[1], ASSET_OMNI_BLOOM_GREEN, ASSET_OMNI_BLOOM_GREEN_X, ASSET_OMNI_BLOOM_GREEN_Y,
                     omni_pack565(149, 255, 146), omni_pack565(20, 180, 60));
    /* the lid (Task 9): read in place from the flash mapping (R70), with its rows' alpha runs (the opaque run lets a
     * frame skip the dial beneath it) and its spec colours by column, built here once, in PSRAM */
    lv_image_dsc_t ld;
    if (ok && assets_image(ASSET_OMNI_LID, &ld) && ld.header.cf == LV_COLOR_FORMAT_RGB565A8 && ld.header.w == SCR &&
        ld.header.h == SCR && ld.header.stride == SCR * 2) {
        v->lid_run = ps_alloc(v, sizeof(int16_t) * 4 * SCR + 3 * SCR);
        ok = v->lid_run != NULL;
        if (ok) {
            uint8_t *spec = (uint8_t *)(v->lid_run + 4 * SCR);
            omni_dial_lid_runs(ld.data, v->lid_run);
            omni_dial_lid_spec(spec);
            R->lid = ld.data;
            R->lid_run = v->lid_run;
            R->lid_spec = spec;
        }
    } else if (ok) {
        ESP_LOGE(TAG, "%s: missing or not 466 x 466 RGB565A8: no lid", ASSET_OMNI_LID);
    }
    /* the iris's grain (Task 10): omni_blade's A8 plane, read in place from the flash mapping (R70) */
    lv_image_dsc_t gd;
    if (ok && assets_image(ASSET_OMNI_BLADE, &gd) && gd.header.cf == LV_COLOR_FORMAT_A8 && gd.header.w == 256 &&
        gd.header.h == 512 && gd.header.stride == 256)
        R->iris_grain = gd.data;
    else if (ok)
        ESP_LOGE(TAG, "%s: missing or not 256 x 512 A8: the iris without its grain", ASSET_OMNI_BLADE);
    const uint32_t ov_us = dv_us_since(t1);
    t1 = esp_timer_get_time();
    /* the Omni globe (Task 5): planar LUT rows on both cores, packed into the compact LUT, the planar one freed */
    const omni_globe_cfg_t *gc = &OMNI_DIAL_GLOBE_CFG;
    size_t tex_size = 0;
    ok = ok && !quitting(v);
    const void *tex = ok ? assets_raw(ASSET_OMNI512, &tex_size) : NULL;
    ok = ok && tex && tex_size == (size_t)ASSET_OMNI512_W * ASSET_OMNI512_H * 4 && gc->tex_w == ASSET_OMNI512_W &&
         omni_globe_compact_ok(gc);
    if (ok) {
        v->lut_bytes = omni_globe_lut_bytes(gc);
        v->lut_mem = ps_alloc(v, v->lut_bytes);
        ok = v->lut_mem && omni_globe_lut_begin(&v->lut, gc, v->lut_mem, v->lut_bytes);
    }
    if (ok) {
        const int half = gc->diam / 2;
        help_start(v, lut_rows_steps, half, gc->diam);
        lut_rows_steps(v, 0, 0, half);
        help_wait(v, false);
        ok = !quitting(v);
    }
    uint32_t psram_low = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (ok) {
        const size_t cb = omni_globe_compact_bytes(&v->lut);
        v->clut_mem = ps_alloc(v, cb);
        psram_low = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);   /* both LUTs at once: the build's peak */
        ok = v->clut_mem && omni_globe_compact_begin(&v->clut, &v->lut, v->clut_mem, cb);
        if (ok) {
            const unsigned mid = v->lut.nspans / 2;
            help_start(v, pack_spans, (int)mid, v->lut.nspans);
            pack_spans(v, 0, 0, (int)mid);
            help_wait(v, false);
            if (v->clut.bad_runs) ESP_LOGE(TAG, "globe: %u spans not one alpha-255 run", (unsigned)v->clut.bad_runs);
        }
        ok = ok && !quitting(v);
        heap_caps_free(v->lut_mem);
        v->lut_mem = NULL;
        portENTER_CRITICAL(&dial_mux);
        v->bytes -= (uint32_t)v->lut_bytes;
        portEXIT_CRITICAL(&dial_mux);
    }
    if (ok) {
        v->shade = ps_alloc(v, sizeof *v->shade);
        ok = v->shade != NULL;
    }
    if (ok) {
        omni_globe_shade_build(v->shade, &OMNI_DIAL_GLOBE_LOOK);
        v->tex = (omni_globe_tex_t){ ASSET_OMNI512_W, ASSET_OMNI512_H, (const uint32_t *)tex };   /* in place (R70) */
        if (v->perf & PERF_TEX) {   /* R88 candidate 3: a PSRAM copy (flash reads cost the kernel about 15 %) */
            v->tex_copy = ps_alloc(v, tex_size);
            if (v->tex_copy) {
                memcpy(v->tex_copy, tex, tex_size);
                v->tex.px = v->tex_copy;
            }
        }
        R->globe = (omni_globe_frame_t){ .tex = &v->tex, .shade = v->shade, .clut = &v->clut, .fade = 255 };
        omni_globe_sun_studio(&R->globe.sun);
        R->clut = &v->clut;
        v->kmove = globe_pie_shade_ok(v->shade) ? globe_cspan_pie : omni_globe_cspan_fast;
        R->ckernel = v->kmove;
        R->rkernel = omni_rings_span_codes;
        R->over_run = globe_over_pie;   /* the layers' overlay rule on the PIE unit (bit-exact with the C rule) */
        const int S = v->clut.cfg.size, o = SCR / 2 - S / 2;
        v->gbox = (omni_rect_t){ (int16_t)o, (int16_t)o, (int16_t)(o + S), (int16_t)(o + S) };
        v->disc.n = 0;
        v->chord.n = 0;
        for (int y = 0; y < S && v->disc.n < MAX_RECTS; y += DISC_BAND) {
            int x0 = S, x1 = 0;
            for (unsigned i = 0; i < v->clut.nspans; i++) {
                const omni_globe_cspan_t *sp = &v->clut.span[i];
                if (sp->y < y || sp->y >= y + DISC_BAND) continue;
                x0 = sp->x0 < x0 ? sp->x0 : x0;
                x1 = sp->x0 + sp->n > x1 ? sp->x0 + sp->n : x1;
            }
            if (x1 <= x0) continue;
            x0 = x0 - DISC_MARGIN < 0 ? 0 : x0 - DISC_MARGIN;
            x1 = x1 + DISC_MARGIN > S ? S : x1 + DISC_MARGIN;
            v->disc.r[v->disc.n++] = (omni_rect_t){ (int16_t)(o + x0), (int16_t)(o + y), (int16_t)(o + x1),
                                                    (int16_t)(o + dv_imin(y + DISC_BAND, S)) };
        }
        v->chord_rmax = 0;
        for (int y = 0; y < S && v->chord.n < MAX_RECTS; y += CHORD_BAND) {   /* the spans' exact extent, 8 rows */
            int x0 = S, x1 = 0;
            for (unsigned i = 0; i < v->clut.nspans; i++) {
                const omni_globe_cspan_t *sp = &v->clut.span[i];
                if (sp->y < y || sp->y >= y + CHORD_BAND) continue;
                x0 = sp->x0 < x0 ? sp->x0 : x0;
                x1 = sp->x0 + sp->n > x1 ? sp->x0 + sp->n : x1;
            }
            if (x1 <= x0) continue;
            const omni_rect_t b = { (int16_t)(o + x0), (int16_t)(o + y), (int16_t)(o + x1), (int16_t)(o + dv_imin(y + CHORD_BAND, S)) };
            v->chord.r[v->chord.n++] = b;
            for (int k = 0; k < 4; k++) {   /* the pixel centres farthest from the dial's centre */
                const float px = (k & 1 ? b.x1 - 1 : b.x0) + 0.5f - SCR / 2.0f, py = (k & 2 ? b.y1 - 1 : b.y0) + 0.5f - SCR / 2.0f;
                const float rr = sqrtf(px * px + py * py);
                if (rr > v->chord_rmax) v->chord_rmax = rr;
            }
        }
        v->ukey_rings = v->chord_rmax >= 125.0f;   /* the rings start at r 126 (whole rows), the V marks at 127.4 */
        {   /* M7 (review recommendation 5): the pieces the open loupe cuts the bands into, against OMNI_RECTS_MAX */
            const omni_rect_t hole = omni_dial_loupe_cfg(1, v->res.dial_map.cfg.n_theta).rect;
            rects_t P = { .n = 0 };
            v->disc_pieces = omni_rects_minus(&v->disc, &hole, &P) ? P.n : -1;
            P.n = 0;
            v->chord_pieces = omni_rects_minus(&v->chord, &hole, &P) ? P.n : -1;
        }
        ESP_LOGI(TAG, "disc: %d bands, %u px (the box %d); chord %d bands, %u px, r max %.1f%s; less the loupe %d / %d "
                 "pieces of %d", v->disc.n, (unsigned)omni_rects_px(&v->disc), S * S, v->chord.n,
                 (unsigned)omni_rects_px(&v->chord), (double)v->chord_rmax,
                 v->ukey_rings ? " (the cache is keyed on the rings too)" : "", v->disc_pieces, v->chord_pieces,
                 OMNI_RECTS_MAX);
        if (v->disc_pieces < 0 || v->chord_pieces < 0)
            ESP_LOGW(TAG, "the bands less the loupe overflow %d rects: drift frames with the loupe open render whole",
                     OMNI_RECTS_MAX);
        if (v->path == DISP_PATH_IMAGE) {   /* the drift path's cache of the under stage */
            v->ucache = ps_alloc(v, (size_t)S * S * 2);
        }
    }
    const uint32_t globe_us = dv_us_since(t1);
    const uint32_t ms = dv_us_since(t0) / 1000;
    portENTER_CRITICAL(&dial_mux);
    v->build_ms = ms;
    portEXIT_CRITICAL(&dial_mux);
    if (ok)
        ESP_LOGI(TAG, "dial built in %u ms (strips + L0 copy %u, map %s %u, magnifier %u, overlays %u, globe %u); "
                 "%u B PSRAM (L0 %u, map %u, clut %u), strips %u B mapped, PSRAM free at the build's low %u; %s kernel",
                 (unsigned)ms, (unsigned)(strip_us / 1000), v->map_loaded ? "loaded" : "built", (unsigned)(map_us / 1000),
                 (unsigned)(mag_us / 1000), (unsigned)(ov_us / 1000), (unsigned)(globe_us / 1000), (unsigned)v->bytes,
                 (unsigned)v->l0_bytes, (unsigned)v->map_bytes, (unsigned)v->clut.nblk * 64, (unsigned)v->strip_flash,
                 (unsigned)psram_low, v->kmove == globe_cspan_pie ? "PIE" : "C (shade out of the PIE range)");
    return ok;
}

static bool build(dial_view_t *v)
{
    vTaskPrioritySet(NULL, BUILD_PRIO);
    if (v->path == DISP_PATH_IMAGE) vTaskPrioritySet(v->helper, BUILD_PRIO);
    const bool ok = build_steps(v);
    vTaskPrioritySet(v->helper, v->path == DISP_PATH_IMAGE ? WORKER_PRIO : BAND_HELP_PRIO);
    vTaskPrioritySet(NULL, WORKER_PRIO);
    return ok;
}

static void worker(void *arg)
{
    dial_view_t *v = arg;
    if (v->pm) esp_pm_lock_acquire(v->pm);
    const bool ok = build(v);
    portENTER_CRITICAL(&dial_mux);
    v->built = ok;
    v->failed = !ok;
    portEXIT_CRITICAL(&dial_mux);
    if (!ok && !quitting(v))
        ESP_LOGE(TAG, "dial build failed (PSRAM free %u, largest %u); the view stays black",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    for (;;) {
        portENTER_CRITICAL(&dial_mux);
        const bool quit = v->quit;
        const job_t job = v->job;
        const bool take = ok && v->path == DISP_PATH_IMAGE && v->has_pending && !v->ready && !v->dd_dead;
        omni_dial_state_t st;
        uint32_t seq = 0;
        if (take) {
            st = v->pending;
            seq = v->pending_seq;
            v->has_pending = false;
        }
        portEXIT_CRITICAL(&dial_mux);
        if (quit) break;
        if (job) {
            bool jok = false;
#if CONFIG_OMNI_DEBUG_CONSOLE
            if (!ok) printf("DIAL job: the view is not built\n");
            else jok = dv_job(v, job);
            /* T7 M5: the jobs prepare their own frames, which are gone now; BAND's draws render from v->prep, so it
             * is the shown frame's again (the console holds the LVGL lock: no band is drawn meanwhile) */
            if (ok && v->path == DISP_PATH_BAND && v->have_prev) dv_prepare(v, &v->cur);
#endif
            portENTER_CRITICAL(&dial_mux);
            v->job = JOB_NONE;
            v->job_ok = jok;
            portEXIT_CRITICAL(&dial_mux);
            xSemaphoreGive(v->job_done);
            continue;
        }
        if (take) {
            const bool taken = render_frame(v, &st, seq);   /* ready, sent direct, or the same as the last frame */
            portENTER_CRITICAL(&dial_mux);
            if (taken) v->prev_seq = seq;   /* after ready: poll's freshness check lags a frame's swap by one poll */
            portEXIT_CRITICAL(&dial_mux);
            continue;
        }
        if (v->pm) esp_pm_lock_release(v->pm);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (v->pm) esp_pm_lock_acquire(v->pm);
    }
    if (v->pm) esp_pm_lock_release(v->pm);
    xSemaphoreGive(v->exited);
    vTaskSuspend(NULL);   /* destroy deletes it */
}

/* ---- LVGL context */

static void direct_abort(dial_view_t *v);
static bool poll_image(dial_view_t *v);

/* BAND: LVGL draws this screen-covering object first in each band (cover check), so the band is written directly. */
static void band_draw(lv_event_t *e)
{
    dial_view_t *v = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (layer->draw_task_head != NULL || layer->draw_buf == NULL ||
        layer->draw_buf->header.cf != LV_COLOR_FORMAT_RGB565) {
        v->band_conflicts++;
        return;
    }
    const lv_area_t *c = &layer->_clip_area, *f = &layer->buf_area;
    const lv_area_t a = { LV_MAX(c->x1, f->x1), LV_MAX(c->y1, f->y1), LV_MIN(c->x2, f->x2), LV_MIN(c->y2, f->y2) };
    if (a.x2 < a.x1 || a.y2 < a.y1) return;
    const int64_t t0 = esp_timer_get_time();
    if (!v->band_t0) v->band_t0 = t0;
    v->j_stride = (int)(layer->draw_buf->header.stride / 2);
    v->j_dst = (uint16_t *)layer->draw_buf->data + (size_t)(a.y1 - f->y1) * v->j_stride + (a.x1 - f->x1);
    v->j_x0 = a.x1;
    v->j_y0 = a.y1;
    v->j_fb = NULL;
    const rects_t L = { .r = { { (int16_t)a.x1, (int16_t)a.y1, (int16_t)(a.x2 + 1), (int16_t)(a.y2 + 1) } }, .n = 1 };
    dv_set_chunks(v, &L, BAND_CHUNK);
    dv_dispatch(v, true);
    v->band_us += dv_us_since(t0);
}

static void band_refr_ready(lv_event_t *e)   /* BAND: one refresh's band draws, as a frame's render time */
{
    dial_view_t *v = lv_event_get_user_data(e);
    if (!v->band_t0) return;
    portENTER_CRITICAL(&dial_mux);
    if (v->band_us > v->st_max) v->st_max = v->band_us;
    v->st_sum += v->band_us;
    portEXIT_CRITICAL(&dial_mux);
    v->band_t0 = 0;
    v->band_us = 0;
}

static void obj_deleted(lv_event_t *e)   /* the parent went first (a screen cleaned under the view) */
{
    dial_view_t *v = lv_event_get_user_data(e);
    v->obj = NULL;
}

static void view_free(dial_view_t *v)
{
    if (v->obj) {
        lv_obj_remove_event_cb_with_user_data(v->obj, obj_deleted, v);
        disp_image_uncover(v->obj);
        lv_obj_delete(v->obj);
    }
    lv_display_remove_event_cb_with_user_data(lv_display_get_default(), band_refr_ready, v);
    if (v->buf[0]) v->buf[0]->data = (uint8_t *)v->px[0];   /* its own pixels back before it is freed */
    theme_buf_destroy(v->buf[0]);
    theme_buf_destroy(v->buf[1]);
    heap_caps_free(v->l0_copy);
    heap_caps_free(v->map_mem);
    heap_caps_free(v->mag_mem);
    for (int i = 0; i < 8; i++) heap_caps_free(v->ov_mem[i]);
    heap_caps_free(v->lut_mem);
    heap_caps_free(v->clut_mem);
    heap_caps_free(v->shade);
    for (int i = 0; i < LOUPE_STEPS; i++) {
        heap_caps_free(v->loupe_mem[i]);
        heap_caps_free(v->loupe_base[i]);
    }
    heap_caps_free(v->prep);
    heap_caps_free(v->ucache);
    heap_caps_free(v->tex_copy);
    heap_caps_free(v->base_copy);
    heap_caps_free(v->lid_run);
    heap_caps_free(v->lov_mem);
    heap_caps_free(v->band_scratch[0]);
    heap_caps_free(v->band_scratch[1]);
    if (v->exited) vSemaphoreDeleteWithCaps(v->exited);
    if (v->h_exited) vSemaphoreDeleteWithCaps(v->h_exited);
    if (v->help_done) vSemaphoreDeleteWithCaps(v->help_done);
    if (v->job_done) vSemaphoreDeleteWithCaps(v->job_done);
    if (v->pm) esp_pm_lock_delete(v->pm);
    if (s_current == v) s_current = NULL;
    heap_caps_free(v);
}

/* Stops a task: quit set, notified, its exit (done) given, deleted. */
static void task_stop(dial_view_t *v, TaskHandle_t *t, SemaphoreHandle_t done)
{
    if (!*t) return;
    portENTER_CRITICAL(&dial_mux);
    if (*t == v->helper) v->h_quit = true;
    else v->quit = true;
    portEXIT_CRITICAL(&dial_mux);
    xTaskNotifyGive(*t);
    xSemaphoreTake(done, portMAX_DELAY);
    vTaskDeleteWithCaps(*t);
    *t = NULL;
}

dial_view_t *dial_view_create(lv_obj_t *parent)
{
    static const char *const NEED[] = { ASSET_OMNI_BASE, ASSET_OMNI_STRIP0, ASSET_OMNI_STRIP1, ASSET_OMNI_STRIP2,
                                        ASSET_OMNI_DIALMAP, ASSET_OMNI512, ASSET_OMNI_TOP, ASSET_OMNI_OVER,
                                        ASSET_OMNI_DOME, ASSET_OMNI_GLASS };
    if (!assets_ready()) return NULL;
    for (size_t i = 0; i < sizeof NEED / sizeof NEED[0]; i++)
        if (!assets_raw(NEED[i], NULL)) {
            ESP_LOGE(TAG, "no %s", NEED[i]);
            return NULL;
        }
    dial_view_t *v = heap_caps_calloc(1, sizeof *v, MALLOC_CAP_SPIRAM);
    if (!v) return NULL;
    v->bytes = sizeof *v;
    v->path = dial_view_path();   /* the next view's switches: this view takes them, the defaults come back (T7 M3) */
    v->perf = (PERF_DEFAULT & ~(unsigned)PERF_NEXT_VIEW) | (dv_next_tex() ? PERF_TEX : 0)
            | (dv_next_base() ? PERF_BASE : 0);
    s_path = -1;
#if CONFIG_OMNI_DEBUG_CONSOLE
    s_next_tex = s_next_base = true;
#endif
    v->hold = NAN;
    v->lid_hold = NAN;
    v->iris_hold = -1;
    v->reveal_psram_low = UINT32_MAX;
    v->want_on = true;
    bool ok = true;
    if (v->path == DISP_PATH_IMAGE) {
        for (int i = 0; i < 2 && ok; i++) {
            v->buf[i] = theme_buf_create(SCR, SCR, LV_COLOR_FORMAT_RGB565);
            ok = v->buf[i] && v->buf[i]->header.stride == SCR * 2;
            if (ok) {
                v->px[i] = (uint16_t *)(void *)v->buf[i]->data;
                v->bytes += v->buf[i]->data_size;
            }
        }
    } else {
        for (int i = 0; i < 2 && ok; i++) {
            v->band_scratch[i] = ps_alloc(v, (size_t)SCR * BAND_CHUNK * 2);
            ok = v->band_scratch[i] != NULL;
        }
    }
    if (ok) {
        v->prep = ps_alloc(v, sizeof *v->prep);
        ok = v->prep != NULL;
    }
    if (ok) {
        v->exited = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
        v->h_exited = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
        v->help_done = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
        v->job_done = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
        ok = v->exited && v->h_exited && v->help_done && v->job_done;
        if (ok && esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "dial", &v->pm) != ESP_OK) v->pm = NULL;
    }
    if (ok) {
        if (v->path == DISP_PATH_IMAGE) {
            v->obj = lv_image_create(parent);
            lv_image_set_src(v->obj, v->buf[0]);
        } else {
            v->obj = lv_obj_create(parent);
            lv_obj_remove_style_all(v->obj);
            lv_obj_set_size(v->obj, SCR, SCR);
            lv_obj_add_event_cb(v->obj, band_draw, LV_EVENT_DRAW_MAIN, v);
            lv_display_add_event_cb(lv_display_get_default(), band_refr_ready, LV_EVENT_REFR_READY, v);
        }
        lv_obj_remove_flag(v->obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(v->obj, 0, 0);
        lv_obj_add_flag(v->obj, LV_OBJ_FLAG_HIDDEN);   /* until its first frame: black (the screen) meanwhile */
        disp_image_cover(v->obj);
        lv_obj_add_event_cb(v->obj, obj_deleted, LV_EVENT_DELETE, v);
        /* the helper on core 0 (IMAGE: renders the worker's frames with it; BAND: the bands with the LVGL task) */
        ok = xTaskCreatePinnedToCoreWithCaps(helper, "dial2", STACK, v, WORKER_PRIO, &v->helper, HELPER_CORE,
                                             MALLOC_CAP_SPIRAM) == pdPASS;
        if (!ok) v->helper = NULL;
        ok = ok && xTaskCreatePinnedToCoreWithCaps(worker, "dial", STACK, v, WORKER_PRIO, &v->task, WORKER_CORE,
                                                   MALLOC_CAP_SPIRAM) == pdPASS;
        if (!ok) v->task = NULL;
    }
    if (!ok) {
        ESP_LOGE(TAG, "dial view not created (PSRAM free %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        task_stop(v, &v->helper, v->h_exited);
        view_free(v);
        return NULL;
    }
    ESP_LOGI(TAG, "dial view created (%s path); building on its worker",
             v->path == DISP_PATH_IMAGE ? "IMAGE" : "BAND");
    s_current = v;
    return v;
}

void dial_view_destroy(dial_view_t *v)
{
    if (!v) return;
    direct_abort(v);   /* B19: the panel back before the buffers go (a render task in band() wakes with NULL) */
    task_stop(v, &v->task, v->exited);       /* at most one build step (a loupe pair, about 90 ms: T7 M2) or a frame; it
                                              * never waits on LVGL */
    task_stop(v, &v->helper, v->h_exited);   /* idle: the worker waited for its every job */
    view_free(v);
}

bool dial_view_ready(const dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    const bool b = v->built;
    portEXIT_CRITICAL(&dial_mux);
    return b;
}

bool dial_view_failed(const dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    const bool b = v->failed;
    portEXIT_CRITICAL(&dial_mux);
    return b;
}

dial_view_t *dial_view_current(void) { return s_current; }

void dial_view_submit(dial_view_t *v, const omni_dial_state_t *st_in)
{
    omni_dial_state_t st = *st_in;
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (v->scen != DIAL_SCEN_OFF) {   /* the measurement overlay (dial_view_scenario) */
        const float sp = v->scen == DIAL_SCEN_SLOW ? 0.05f : 3.0f;
        for (int i = 0; i < OMNI_RINGS; i++) {
            v->scen_val[i] = fmodf(v->scen_val[i] + sp, (float)OMNI_RING_GEOM[i].slots);
            st.val[i] += v->scen_val[i];
            st.speed[i] = sp;
        }
        if (v->scen != DIAL_SCEN_SLOW) {
            v->scen_lon = fmodf(v->scen_lon + FAST_LON, 360.0f);
            st.lon_deg = fmodf(st.lon_deg + v->scen_lon + 540.0f, 360.0f) - 180.0f;
            st.lon_speed = FAST_LON;
        }
        if (v->scen == DIAL_SCEN_FLASH) st.flash = 0.3f;
    }
#endif
    portENTER_CRITICAL(&dial_mux);
    if (!isnan(v->hold)) {
        st.lon_deg = v->hold;
        st.lon_speed = 0;
    }
    if (!isnan(v->lid_hold)) {
        st.lid = true;
        st.lid_theta = v->lid_hold;
        st.fade = 0;
    }
    if (v->iris_hold >= 0) {
        float ap, phi;
        omni_iris_at((uint32_t)v->iris_hold, false, &ap, &phi);
        st.iris = ap < OMNI_IRIS_AP_OPEN;
        st.iris_ap = ap;
        st.iris_phi = phi;
        st.iris_s = ap / OMNI_IRIS_AP_OPEN;
        st.lid = false;
        st.fade = 0;
    }
    v->pending = st;
    v->pending_seq = ++v->sub_seq;
    v->has_pending = true;
    v->last = *st_in;
    v->has_last = true;
    const bool wake = v->path == DISP_PATH_IMAGE && !v->ready && !v->job;
    portEXIT_CRITICAL(&dial_mux);
    if (wake) xTaskNotifyGive(v->task);
}

/* The object shows once it has had a frame, while dial_view_show wants it, and not while a blank waits for a fresh frame. */
static void vis_apply(dial_view_t *v)
{
    if (!v->obj) return;
    const bool on = v->shown && v->want_on && !v->blanked;
    if (on == !lv_obj_has_flag(v->obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (on) lv_obj_remove_flag(v->obj, LV_OBJ_FLAG_HIDDEN);   /* invalidates the whole object */
    else lv_obj_add_flag(v->obj, LV_OBJ_FLAG_HIDDEN);
}

static void invalidate(dial_view_t *v, const rects_t *L)
{
    if (!v->obj) return;
    if (!v->shown) {   /* the first frame: the whole object */
        v->shown = true;
        vis_apply(v);
        return;
    }
    for (int i = 0; i < L->n; i++) {
        const lv_area_t a = { L->r[i].x0, L->r[i].y0, L->r[i].x1 - 1, L->r[i].y1 - 1 };
        lv_obj_invalidate_area(v->obj, &a);
    }
}

void dial_view_show(dial_view_t *v, bool on)
{
    if (v == NULL) return;
    if (!on) direct_abort(v);   /* B19: the glance */
    v->want_on = on;
    vis_apply(v);
}

void dial_view_blank(dial_view_t *v)
{
    if (v == NULL) return;
    direct_abort(v);   /* B15, B19: a sleep, a screen opening (face_sleep) */
    v->fresh_seq = v->sub_seq + 1;   /* the next submit */
    v->blanked = true;
    v->blank_t0 = esp_timer_get_time();
    vis_apply(v);
}

void dial_view_unblank(dial_view_t *v)
{
    if (v == NULL || !v->blanked) return;
    v->blanked = false;   /* the last frame shows again at once (not counted as an ended blank) */
    vis_apply(v);
}

bool dial_view_blanked(const dial_view_t *v)
{
    if (v == NULL || !v->blanked) return false;
    portENTER_CRITICAL(&dial_mux);
    const bool failed = v->failed;   /* a failed build never brings a fresh frame */
    portEXIT_CRITICAL(&dial_mux);
    return !failed;
}

/* LVGL ctx, after each poll: a blank ends once the state on screen is from a submit after it (prev_seq with nothing
 * waiting to swap: the last frame rendered, or one found the same as it, is the shown one). */
static void blank_check(dial_view_t *v)
{
    if (!v->blanked) return;
    portENTER_CRITICAL(&dial_mux);
    const bool fresh = !v->ready && (int32_t)(v->prev_seq - v->fresh_seq) >= 0;
    portEXIT_CRITICAL(&dial_mux);
    if (!fresh) return;
    v->blanked = false;
    const uint32_t us = dv_us_since(v->blank_t0);
    v->blank_n++;
    v->blank_last_us = us;
    if (us > v->blank_max_us) v->blank_max_us = us;
    vis_apply(v);
}

static bool poll_band(dial_view_t *v)
{
    disp_swap_poll(&v->swap);
    if (v->swap.await) return false;   /* the last frame is not drawn yet: the latest state waits */
    portENTER_CRITICAL(&dial_mux);
    const bool take = v->built && v->has_pending;
    omni_dial_state_t st;
    if (take) {
        st = v->pending;
        v->prev_seq = v->pending_seq;   /* BAND draws it at the next refresh (or it is the same as the last) */
        v->has_pending = false;
    }
    portEXIT_CRITICAL(&dial_mux);
    if (!take) return false;
    const vframe_t cur = dv_make_frame(v, &st);
    rects_t d;
    bool drift;
    if (dv_frame_dirty(v, v->have_prev ? &v->prev : NULL, &cur, &d, &drift) == 0) return false;
    v->cur = cur;
    dv_prepare(v, &v->cur);   /* LVGL context: the bands of the next refreshes render from it */
    v->prev = cur;
    v->have_prev = true;
    invalidate(v, &d);
    portENTER_CRITICAL(&dial_mux);
    v->st_frames++;
    v->st_px += omni_rects_px(&d);
    portEXIT_CRITICAL(&dial_mux);
    disp_stats_frame_rendered();   /* "rendered" as it is drawn, in the bands of the next refresh */
    disp_swap_did(&v->swap);
    return true;
}


bool dial_view_poll(dial_view_t *v)
{
    const bool got = v->path == DISP_PATH_BAND ? poll_band(v) : poll_image(v);
    blank_check(v);
    return got;
}

void dial_view_overlays_publish(dial_view_t *v)
{
    v->lov_self = true;   /* the client publishes after its steps: poll's own per-frame publish stands down (minor 7) */
    portENTER_CRITICAL(&dial_mux);
    const bool direct = v->mode == DV_DIRECT;
    portEXIT_CRITICAL(&dial_mux);
    if (direct && lov_due(v)) dv_lov_publish(v, false);   /* once per take: the first step after it (minor 7) */
}

#define EXIT_WAIT_MS 100   /* the exit waits at most this for the last direct frame on the wire (else: whole screen) */

/* LVGL ctx: B19's abort (DISPLAY, HIDDEN, the path inactive, a blank, a hide, a destroy): the path is released at once,
 * both buffers lose their history (buf_old) and the view is redrawn whole from the front buffer. A worker frame under
 * way finds mode_gen moved and renders on through IMAGE. */
static void direct_abort(dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    const bool on = v->mode != DV_IMAGE;
    portEXIT_CRITICAL(&dial_mux);
    if (!on) return;
    disp_direct_abort();
    disp_direct_release();   /* may finish on the orphan timer (active() false meanwhile): nothing here assumes it */
    portENTER_CRITICAL(&dial_mux);
    v->mode = DV_IMAGE;
    v->mode_gen++;
    v->buf_old[0] = v->buf_old[1] = true;
    v->exit_req = v->want_direct = v->dd_dead = false;
    v->dd_aborts++;
    const bool more = v->has_pending && !v->ready && v->task;
    portEXIT_CRITICAL(&dial_mux);
    v->exit_t0 = 0;
    if (v->obj) lv_obj_invalidate(v->obj);
#if CONFIG_OMNI_DEBUG_CONSOLE
    v->sh_ok = v->sh_armed && disp_direct_shadow_frame() != NULL;
    v->sh_whole = true;
#endif
    if (more) xTaskNotifyGive(v->task);
}

/* LVGL ctx, every poll while the path is built: counts the direct frames shown, checks each direct frame's admission
 * (D7: once a frame, with the heap walk), aborts or asks for the exit (B19), and claims on full-screen motion. True if
 * a direct frame reached the panel since the last poll. */
static bool direct_poll(dial_view_t *v, bool ready)
{
    bool got = false;
    const uint32_t shown = disp_direct_shown();
    portENTER_CRITICAL(&dial_mux);
    const dv_mode_t mode = v->mode;
    const uint32_t ended = v->dd_ended;
    const bool still = v->still_req, want = v->want_direct;
    const int64_t bo = v->backoff_until;
    v->still_req = false;
    portEXIT_CRITICAL(&dial_mux);
    if (shown != v->dd_shown) {
        if (mode != DV_IMAGE)
            for (uint32_t n = shown - v->dd_shown; n > 0 && n < 64; n--) disp_stats_frame_shown();
        got = mode != DV_IMAGE;
        v->dd_shown = shown;
    }
    if (still) disp_direct_still();   /* R2-1: once, on the first exact frame after motion */
    if (mode == DV_DIRECT || (mode == DV_EXIT && !ready)) {
        if (!disp_direct_active()) {   /* orphan, provisioning, the display force-ended (B19) */
            direct_abort(v);
            return got;
        }
        /* the next direct frame's overlays (once a frame), unless the client publishes them after its step (minor 7) */
        if (mode == DV_DIRECT && !v->lov_self && lov_due(v)) dv_lov_publish(v, false);
        if (ended == v->dd_checked) return got;
        v->dd_checked = ended;
        const omni_dd_why_t why = disp_direct_check(v->obj, true);
        if (why == OMNI_DD_OK) return got;
        portENTER_CRITICAL(&dial_mux);
        v->dd_checks[why]++;
        if (why != OMNI_DD_DISPLAY && why != OMNI_DD_HIDDEN) {   /* OFF, OVERLAY, PROV, RAM: the next frame exits */
            v->exit_req = true;
            v->backoff_until = esp_timer_get_time() + DISP_DIRECT_BACKOFF_MS * 1000LL;
        }
        portEXIT_CRITICAL(&dial_mux);
        if (why == OMNI_DD_DISPLAY || why == OMNI_DD_HIDDEN) direct_abort(v);
        return got;
    }
    if (mode != DV_IMAGE || ready || !want || v->swap.await || !v->obj || !v->shown || !v->want_on || v->blanked)
        return got;
    if (esp_timer_get_time() < bo) return got;
    portENTER_CRITICAL(&dial_mux);
    v->want_direct = false;   /* one attempt per motion frame */
    portEXIT_CRITICAL(&dial_mux);
    const omni_dd_why_t why = disp_direct_claim(v->obj);
    /* the first direct frame's overlays, before the worker sees DIRECT */
    if (why == OMNI_DD_OK) dv_lov_publish(v, true);
    portENTER_CRITICAL(&dial_mux);
    if (why == OMNI_DD_OK) {
        /* I1: the panel shows the front buffer now; an IMAGE frame that became ready during the claim is swapped in by
         * this poll with its flush dropped, so the worker's entry takes the panel from dd_front, not from front */
        v->dd_front = v->front;
        v->mode = DV_DIRECT;
        v->mode_gen++;
        v->exit_req = false;
        v->dd_claims++;
    } else if (why == OMNI_DD_DISPLAY) {
        v->dd_waits++;      /* the panel comes back on by itself (B16) */
    } else if (why == OMNI_DD_BACKOFF) {
        v->dd_backoffs++;   /* the transport's own back-off (D7, N1): waited out, not lengthened here */
    } else {
        v->dd_refused[why]++;
        v->backoff_until = esp_timer_get_time() + DISP_DIRECT_BACKOFF_MS * 1000LL;
    }
    const bool claimed = why == OMNI_DD_OK;
    v->dd_checked = v->dd_ended;
    portEXIT_CRITICAL(&dial_mux);
    if (!claimed) return got;
    v->dd_shown = disp_direct_shown();
#if CONFIG_OMNI_DEBUG_CONSOLE
    /* D3: the shadow starts as the panel, the front buffer (no band is out yet; the sender writes it only per band) */
    uint16_t *sh = (uint16_t *)(uintptr_t)disp_direct_shadow_frame();
    v->sh_armed = sh != NULL;
    if (sh) memcpy(sh, v->px[v->front], (size_t)SCR * SCR * 2);
#endif
    return got;
}

/* LVGL ctx, the exit frame ready: once the last direct frame is on the wire (or EXIT_WAIT_MS), the path is released
 * and *d is what poll invalidates as it swaps the exit frame in (the whole screen if a band was lost, D3).
 * false: wait. */
static bool direct_exit(dial_view_t *v, rects_t *d)
{
    portENTER_CRITICAL(&dial_mux);
    const uint32_t last = v->dd_last_seq, seen = v->lost_seen;
    const bool still = v->exit_still;
    portEXIT_CRITICAL(&dial_mux);
    if (disp_direct_active() && (int32_t)(disp_direct_shown() - last) < 0) {
        const int64_t now = esp_timer_get_time();
        if (!v->exit_t0) v->exit_t0 = now;
        if (now - v->exit_t0 < EXIT_WAIT_MS * 1000LL) return false;
    }
    v->exit_t0 = 0;
    const bool active = disp_direct_active();
    /* I2: timed out with the last frame not all on the wire: the release discards bands, and counts them lost only
     * as the sender gets to them, so the hand-back is whole whatever lost says now */
    const bool timed_out = active && (int32_t)(disp_direct_shown() - last) < 0;
    disp_direct_release();
    if (!active || timed_out || disp_direct_lost() != seen) *d = (rects_t){ .r = { DV_SCREEN }, .n = 1 };
    if (still) {   /* the exit frame is the first exact frame after motion (R2-1): its back-off ends now */
        disp_direct_still();
        portENTER_CRITICAL(&dial_mux);
        v->backoff_until = 0;
        portEXIT_CRITICAL(&dial_mux);
    }
#if CONFIG_OMNI_DEBUG_CONSOLE
    v->sh_ok = v->sh_armed && disp_direct_shadow_frame() != NULL;
    v->sh_whole = false;
    v->sh_frame = v->bufframe[!v->front];   /* the exit frame (the worker is idle while ready) */
    v->sh_patch = *d;
#endif
    portENTER_CRITICAL(&dial_mux);
    v->mode = DV_IMAGE;
    v->mode_gen++;
    portEXIT_CRITICAL(&dial_mux);
    return true;
}

static bool poll_image(dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    bool ready = v->ready;
    portEXIT_CRITICAL(&dial_mux);
    disp_swap_poll(&v->swap);
    bool got = false, exited = false;
    if (DISP_DIRECT_BUILT) got = direct_poll(v, ready);
    portENTER_CRITICAL(&dial_mux);
    ready = v->ready;
    const bool exit = ready && v->mode == DV_EXIT;
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (ready && v->mode == DV_DIRECT) v->dd_claim_swaps++;   /* I1's interleaving: the entry models dd_front */
#endif
    rects_t d;
    if (ready) d = v->back_rects;
    portEXIT_CRITICAL(&dial_mux);
    if (!ready) return got;
    if (exit) {
        if (!direct_exit(v, &d)) return got;
        exited = true;
    }
    v->front = !v->front;
    v->buf[0]->data = (uint8_t *)v->px[v->front];   /* the image shows the other buffer; only d is redrawn */
    invalidate(v, &d);
    disp_swap_did(&v->swap);
    portENTER_CRITICAL(&dial_mux);
    v->ready = false;
    if (exited) v->buf_old[!v->front] = true;   /* the frame from before the claim, older than prev (B17) */
    const bool more = v->has_pending;
    portEXIT_CRITICAL(&dial_mux);
    if (more) xTaskNotifyGive(v->task);
    return true;
}

void dial_view_stats(dial_view_t *v, dial_view_stats_t *s, bool reset)
{
    portENTER_CRITICAL(&dial_mux);
    const uint32_t n = v->st_frames;
    s->frames = n;
    s->render_us_avg = n ? (uint32_t)(v->st_sum / n) : 0;
    s->render_us_max = v->st_max;
    s->dirty_px_avg = n ? (uint32_t)(v->st_px / n) : 0;
    s->build_ms = v->build_ms;
    s->bytes = v->bytes;
    if (reset) {
        v->st_frames = 0;
        v->st_sum = v->st_px = 0;
        v->st_max = 0;
        v->st_late = v->st_late_drift = 0;
    }
    portEXIT_CRITICAL(&dial_mux);
}

static void resubmit(dial_view_t *v)   /* a hold shows now, even at rest */
{
    portENTER_CRITICAL(&dial_mux);
    const bool again = v->has_last && !v->has_pending;
    const omni_dial_state_t st = v->last;
    portEXIT_CRITICAL(&dial_mux);
    if (again) dial_view_submit(v, &st);
}

void dial_view_hold(dial_view_t *v, float lon_deg)
{
    portENTER_CRITICAL(&dial_mux);
    v->hold = lon_deg;
    portEXIT_CRITICAL(&dial_mux);
    resubmit(v);
}

#if CONFIG_OMNI_DEBUG_CONSOLE
void dial_view_lid(dial_view_t *v, float theta_rad)
{
    portENTER_CRITICAL(&dial_mux);
    v->lid_hold = theta_rad;
    portEXIT_CRITICAL(&dial_mux);
    resubmit(v);
}

void dial_view_iris(dial_view_t *v, int32_t ms)
{
    portENTER_CRITICAL(&dial_mux);
    v->iris_hold = ms;
    portEXIT_CRITICAL(&dial_mux);
    resubmit(v);
}
#endif

