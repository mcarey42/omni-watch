/* fk_view (fk_view.h): a face's PSRAM canvas and context, and its full-screen reveal over it (from the first theme's
 * wave 1 Task 11, its spec section 10; face_kit since rd wave 1 Task 2, the reveal behind fk_reveal_ops_t).
 *
 * At rest the face draws straight into the canvas's RGB565 buffer and the view invalidates only what was drawn, so
 * LVGL flushes those rects (awake and in the glance: the face's dirty rects either way).
 *
 * The reveal (Review Focus 4), driven through the face's reveal_ops (fk/reveal.h; the first theme's doors are one):
 *   0. fk_face fills the face input on the LVGL side and starts the reveal (fk_view_reveal);
 *   1. the view claims the direct path (disp_direct_claim) and a worker (core 1) and its helper (core 0: the Stage 4a
 *      two-core pattern) render each frame as 16-row jobs claimed by both: the worker asks the reveal for the frame's
 *      plan (ops->frame: the interior's rect to redraw, its wake_ms and reveal_live), and a job draws its rows of that
 *      rect of the face's interior into a PSRAM interior buffer (only the round panel's pixels, fk_rs_job_rows), then
 *      sends their 8-row bands built by ops->band (disp_direct_send_chunk); then disp_direct_frame_end;
 *   2. the LVGL timer checks every direct frame (disp_direct_check); on !disp_direct_active() it ends the reveal at
 *      once: the settled frame is drawn into the canvas and LVGL shows it. No half frame stays.
 *   A tap gives the reveal's skip (the end frame). A refused claim runs the frames through the canvas at LVGL's rate.
 *   At the end the canvas gets the settled frame (the reveal's end frame is the face's settled frame exactly), the path
 *   is released and disp_direct_still() ends its back-off. Only still frames are CRC-compared; never a frame mid-reveal.
 * The face's own reveal, ops->reveal_asset and ops->reveal_arg, passed to the reveal as they are, for the reveal and the
 * bench alike (NULL: the reveal's default). The interior is drawn with in.reveal_live as the plan says (the first
 * theme's: true while the swing runs, so a keep face leaves its kept leaves to the band; false through the reduced and
 * Fade cross-fade, so the kept leaves fade in with the face). The reveal's pop sound (ops->pop_asset) plays at pop_ms.
 * The decisions (the mode, the buffers' fate, the latch) are fk/reveal_sm.h's, tested on the host.
 * Memory: the interior (434 KB PSRAM) and the reveal's context (ops->rctx_bytes) only while the reveal runs; the copy
 * of the reveal's assets only when chosen (the bench's copy, fk_view_set_copy; the first theme measured it no faster:
 * the leaf is read in place). Both tasks' stacks are PSRAM (4 KB each). No internal allocation but the direct path's
 * own bands (R118).
 *
 * The rest motion (the first theme's wave 2 Task 14, a .direct face; never a revealing face, so it has the view's
 * worker plumbing to itself: reveal_t's control fields under s_mux, the two tasks, worker_stop and N1): while the face
 * is awake (fk_view_motion), not the glance and not reduced, the LVGL timer claims the direct path (no interior
 * buffer: each 8-row job draws the face straight into its band, so a frame is ops->draw whole at the newest input moved
 * on by fk_face_in_advance); it admits every direct frame shown (disp_direct_check) and cuts at once on a refusal, the
 * path going inactive, the glance, reduced motion, the bench or the sleep: the canvas then gets the newest frame whole
 * and LVGL shows it, and the claim is tried again later (fk_dm_retry_at). Meanwhile the face's own draws are kept for
 * the worker. Every claim moves s_claim_gen and every release moves it again before disp_direct_release (M1), and the
 * worker takes a job, sends a band and ends a frame only while its claim is current, exactly as the reveal's worker.
 * One holder (fk_dm_may_claim): disp_direct_claim hands an active claim to a second claimer, so the view keeps the
 * holder (s_holder) and refuses every other claim while one holds: a lift's reveal, a rest motion and the console's
 * bench never share a claim. */
#include "fk_view.h"
#include <inttypes.h>
#include <string.h>
#include <sys/time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "assets.h"
#include "disp_direct.h"
#include "disp_stats.h"
#include "sfx.h"
#include "theme.h"
#include "fk/reveal_sm.h"

#define W THEME_W
#define POLL_MS 10            /* the doors' LVGL timer: the end, the checks, the fallback's frames */
#define WORKER_STACK 4096     /* PSRAM */
#define WORKER_PRIO 2         /* under LVGL, as the dial's worker */
#define WORKER_CORE 1
#define HELPER_CORE 0         /* the second core's share of each frame (the Stage 4a two-core pattern, the Omni dial's) */
#define JOB_ROWS 16           /* a frame job: the interior's rows and their bands (two of DISP_DIRECT_ROWS) */
#define JOBS ((W + JOB_ROWS - 1) / JOB_ROWS)
#define STOP_WAIT_MS 300      /* the longest wait for the worker to come back */
#define MJOBS ((W + DISP_DIRECT_ROWS - 1) / DISP_DIRECT_ROWS)   /* the rest motion's jobs: one band each */
#define PANEL_WAIT_MS 400     /* a lift's doors wait this long for the panel to come on, then run through the canvas */
#define COPY_DEFAULT 0        /* production (R101): the leaf read in place from the flash mapping */
#define COPY_MAX FK_REVEAL_BIND_ASSETS_MAX   /* the copy: the reveal's bind reads at most this many (fk/reveal.h) */
static const char *TAG = "fk_view";

typedef struct {
    fk_rs_t ds;                /* the reveal's mode, the stuck buffers, the latch (fk/reveal_sm.h: the decisions) */
    bool reduced;
    int64_t t0_us;
    fk_face_in_t in;           /* the input at the start (its clock advanced by the elapsed time per frame) */
    uint16_t *interior;        /* PSRAM 466 x 466, the reveal's lifetime */
    const fk_reveal_ops_t *ro; /* the face's reveal (ops->reveal_ops) */
    const void *arg;           /* the face's reveal_arg, passed to every call */
    void *rctx;                /* the reveal's own context (ro->rctx_bytes, PSRAM), the reveal's lifetime */
    void *copy[COPY_MAX];      /* the PSRAM copies of the assets its bind read, or none: read in place */
    uint8_t ncopy;
    int64_t unix0;             /* the wall clock at the start, ms: each frame's clock is fk_clock_from(it + t) */
    int64_t wait_until;        /* esp_timer us: the end of the wait for the panel (FK_RS_WAIT) */
    /* shared with the worker under s_mux */
    bool go, quit, skip, busy, worker_done, worker_cut;
    /* a frame's jobs (the worker sets them up; both cores claim them under s_mux): JOB_ROWS rows of the interior drawn
     * (job_draw: the plan's rect job_need of job_in, or nothing), then their bands sent */
    int job_next, job_draw;
    omni_rect_t job_need;
    bool job_fail;
    uint32_t job_draw_us;       /* the frame's interior drawing, us summed over both cores (s_mux) */
    fk_face_in_t job_in;
    bool h_go, h_quit;          /* the helper's go and quit (s_mux) */
    uint32_t claim_gen;         /* s_claim_gen when this reveal's direct claim began (s_mux; Task 13, M1) */
    /* the worker's alone while it runs; read by the LVGL side after */
    uint32_t seq, frames, us_sum, us_max, psram_min, int_min, prep_sum, prep_max;
    uint32_t shown0, lost0;
#if CONFIG_OMNI_DEBUG_CONSOLE
    /* the bench (R101: console builds only) */
    int bench_left;
    bool bench_copy, bench_on;
    fk_reveal_stats_t bench;
#endif
} reveal_t;
#if CONFIG_OMNI_DEBUG_CONSOLE
#define BENCH_ON(d) ((d)->bench_on)
#else
#define BENCH_ON(d) false
#endif

struct fk_view {
    const fk_face_ops_t *ops;
    lv_obj_t *canvas;
    lv_draw_buf_t *buf;
    void *ctx;
    fk_face_in_t last;   /* the last drawn input: ops->dirty compares against it */
    fk_face_in_t latest; /* the newest input (kept while the doors run, for the end) */
    bool drawn, glance;
    reveal_t d;
    /* the rest motion (wave 2 Task 14): LVGL's side */
    bool m_want, m_on;     /* the face is awake (fk_view_motion); a motion claim is held */
    bool m_still;          /* the motion ended without a redraw: the next whole draw is its first exact frame (R2-1) */
    int64_t m_retry;       /* esp_timer us: no claim before */
    /* under s_mux: the newest input for the worker, and when it was filled (esp_timer us) */
    fk_face_in_t m_in;
    int64_t m_in_us;
    lv_timer_t *timer;     /* the doors' poll, or the rest motion's (never both: a .direct face has no doors) */
    TaskHandle_t task, helper;
    SemaphoreHandle_t exited, h_exited, h_done;
};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
/* M1 (Task 13; review I1): moves (under s_mux) at every direct claim a reveal begins and at every release of one. A
 * worker takes a job, sends a band or ends a frame only while it still equals its reveal's claim_gen, so a worker
 * left running by a destroy that could not stop it (N1) puts nothing into a later view's claim: its reveal's release
 * moved it (the window between each check and its call: the comment in reveal_worker). */
static uint32_t s_claim_gen;

/* wave 2 Task 14: who holds the direct path (LVGL ctx): a view (its doors or its rest motion), the bench's token, or
 * NULL; and the bench's bracket (console builds only) */
static const void *s_holder;
#if CONFIG_OMNI_DEBUG_CONSOLE
static fk_view_t *s_current;   /* the live revealing face's view (the console's reveal bench) */
static bool s_bench;
static const char s_bench_token = 0;
#else
static const bool s_bench = false;
#endif

/* LVGL ctx: disp_direct_claim for `me` under the one-holder rule (fk/reveal_sm.h): another holder's claim is never handed
 * to it (OMNI_DD_BACKOFF, as the transport's own back-off: the caller waits it out or runs through the canvas) */
static omni_dd_why_t claim_as(const void *me, lv_obj_t *obj, bool me_is_bench)
{
    if (!fk_dm_may_claim(s_holder, me, s_bench, me_is_bench)) return OMNI_DD_BACKOFF;
    const omni_dd_why_t why = disp_direct_claim(obj);
    if (why == OMNI_DD_OK) s_holder = me;
    return why;
}

/* LVGL ctx: `me`'s claim ends: s_claim_gen moves first (M1: no worker of it puts a band or a frame end into a later
 * claim), then the panel goes back; the holder is cleared */
static void release_as(const void *me)
{
    portENTER_CRITICAL(&s_mux);
    s_claim_gen++;
    portEXIT_CRITICAL(&s_mux);
    disp_direct_release();   /* invalidates what LVGL dropped */
    if (s_holder == me) s_holder = NULL;
}
#if CONFIG_OMNI_DEBUG_CONSOLE
static bool s_copy = COPY_DEFAULT;
#else
static const bool s_copy = COPY_DEFAULT;
#endif

fk_view_t *fk_view_create(lv_obj_t *parent, const fk_face_ops_t *ops)
{
    fk_view_t *v = heap_caps_calloc(1, sizeof *v, MALLOC_CAP_SPIRAM);
    lv_draw_buf_t *buf = v ? theme_buf_create(W, W, LV_COLOR_FORMAT_RGB565) : NULL;
    void *ctx = buf && ops->ctx_bytes ? heap_caps_calloc(1, ops->ctx_bytes, MALLOC_CAP_SPIRAM) : NULL;
    if (buf == NULL || (ops->ctx_bytes && ctx == NULL)) {
        ESP_LOGE(TAG, "%s: no PSRAM for the canvas (%u B) and context (%u B): black", ops->key ? ops->key : "?",
                 (unsigned)(W * W * 2), (unsigned)ops->ctx_bytes);
        heap_caps_free(ctx);
        theme_buf_destroy(buf);
        heap_caps_free(v);
        return NULL;
    }
    memset(buf->data, 0, buf->data_size);   /* black until the first frame (R102) */
    v->ops = ops;
    v->buf = buf;
    v->ctx = ctx;
    if (ops->bind && !ops->bind(ctx, assets_raw))
        ESP_LOGW(TAG, "%s: assets missing: drawn without them", ops->key ? ops->key : "?");
    v->canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(v->canvas, buf);
    lv_obj_set_pos(v->canvas, 0, 0);
#if CONFIG_OMNI_DEBUG_CONSOLE
    /* the doors face's view only (the final review, M5): a pushed view without doors (the key) would take its place,
     * and its destroy would clear it while the face's view is still live */
    if (ops->reveal) s_current = v;
#endif
    return v;
}

static uint16_t *canvas_px(const fk_view_t *v) { return (uint16_t *)v->buf->data; }
static int canvas_stride(const fk_view_t *v) { return (int)(v->buf->header.stride / 2); }

static void draw_rect(fk_view_t *v, const fk_face_in_t *in, omni_rect_t r)
{
    void (*fn)(const void *, const fk_face_in_t *, omni_rect_t, uint16_t *, int) =
        v->glance && v->ops->glance ? v->ops->glance : v->ops->draw;
    if (fn == NULL || r.x0 >= r.x1 || r.y0 >= r.y1) return;
    const int stride = canvas_stride(v);
    fn(v->ctx, in, r, canvas_px(v) + r.y0 * stride + r.x0, stride);
    lv_area_t a = {r.x0, r.y0, r.x1 - 1, r.y1 - 1};
    lv_obj_invalidate_area(v->canvas, &a);
}

void fk_view_draw(fk_view_t *v, const fk_face_in_t *in_, bool full)
{
    if (v == NULL) return;
    fk_face_in_t g = *in_;
    g.glance = v->glance;   /* dirty() compares glance frames as the glance draws them (I1) */
    const fk_face_in_t *in = &g;
    v->latest = g;
    if (v->d.ds.mode != FK_RS_OFF) return;   /* the doors own the panel; their end draws the newest input */
    if (v->m_on) {   /* the rest motion owns the panel: its worker draws from the newest input; its end draws whole */
        portENTER_CRITICAL(&s_mux);
        v->m_in = g;
        v->m_in_us = esp_timer_get_time();
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    omni_rect_t rects[8];
    uint8_t n = 0;
    bool still_after = false;
    if (full || !v->drawn || v->ops->dirty == NULL) {
        rects[0] = (omni_rect_t){0, 0, W, W};
        n = 1;
    } else {   /* awake or in the glance: the face's dirty rects (the Task 10 review's m4) */
        n = v->ops->dirty(v->ctx, &v->last, in, rects);
        if (n > 8) n = 8;
    }
    if (v->m_still && n == 1 && rects[0].x0 == 0 && rects[0].y0 == 0 && rects[0].x1 == W && rects[0].y1 == W) {
        v->m_still = false;   /* drawn below, whole: the first exact frame after the rest motion (R2-1, Task 14 review) */
        still_after = true;
    }
    for (uint8_t i = 0; i < n; i++) {
        omni_rect_t r = rects[i];   /* clipped to the canvas */
        if (r.x0 < 0) r.x0 = 0;
        if (r.y0 < 0) r.y0 = 0;
        if (r.x1 > W) r.x1 = W;
        if (r.y1 > W) r.y1 = W;
        draw_rect(v, in, r);
    }
    v->last = *in;
    v->drawn = true;
    if (still_after) disp_direct_still();
}

/* ------------------------------------------------------------ the doors */

/* the input with its clock t ms after the doors' start, from the wall clock read then (the date rolls at midnight) */
static fk_face_in_t advanced(const reveal_t *d, uint32_t t)
{
    fk_face_in_t in = d->in;
    if (!in.clk.set) return in;
    const int64_t ms = d->unix0 + t;
    fk_clock_from(ms / 1000, (uint16_t)(ms % 1000), in.clk.stale, &in.clk);
    return in;
}

/* The fallback's frame at t ms (LVGL, no direct path): the plan's interior drawn whole, as the jobs would. */
static bool frame_plan(fk_view_t *v, uint32_t t, bool skip);
static bool reveal_prepare(fk_view_t *v, uint32_t t, bool skip)
{
    reveal_t *d = &v->d;
    const bool done = frame_plan(v, t, skip);
    if (d->job_draw) {
        const omni_rect_t r = d->job_need;
        v->ops->draw(v->ctx, &d->job_in, r, d->interior + (size_t)r.y0 * W + r.x0, W);
    }
    return done;
}

static void reveal_fill(void *ctx, uint16_t *band, omni_rect_t c)   /* render task: c at stride 466 (band layout) */
{
    const reveal_t *d = ctx;
    d->ro->band(d->arg, d->rctx, d->interior, c, band);
}

static uint32_t elapsed_ms(const reveal_t *d) { return (uint32_t)((esp_timer_get_time() - d->t0_us) / 1000); }

static void frame_stats(reveal_t *d, uint32_t us)
{
    d->frames++;
    d->us_sum += us;
    if (us > d->us_max) d->us_max = us;
    const uint32_t ps = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint32_t in = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (ps < d->psram_min) d->psram_min = ps;
    if (in < d->int_min) d->int_min = in;
}

/* A frame's interior plan (the worker; reveal_prepare's, for the jobs): the reveal's plan at its time (ops->frame:
 * its state for the bands, then what the jobs draw into the interior: the settled frame whole (once), the part the
 * reveal shows at its time, or nothing). True on the reveal's settled end frame. */
static bool frame_plan(fk_view_t *v, uint32_t t, bool skip)
{
    reveal_t *d = &v->d;
    fk_rframe_t fr;
    d->ro->frame(d->arg, d->rctx, t, skip, d->reduced, &fr);
    d->job_need = fr.need;
    d->job_draw = fr.need.x0 < fr.need.x1 && fr.need.y0 < fr.need.y1;
    if (!d->job_draw) return fr.settled;
    d->job_in = advanced(d, t);
    d->job_in.wake_ms = fr.wake_ms;   /* the plan's (fk_rs_job_in's for the first theme's doors, host-tested) */
    d->job_in.reveal_live = fr.live;
    return fr.settled;
}

/* Both cores: claim jobs until none are left or the path aborts. A job draws its rows of the interior (each row is
 * drawn by one job, and a band reads only its own rows), then sends their bands. */
static void run_jobs(fk_view_t *v)
{
    reveal_t *d = &v->d;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const int j = fk_rs_job_take(&d->job_next, JOBS, &d->job_fail, d->claim_gen, s_claim_gen);   /* I1 */
        portEXIT_CRITICAL(&s_mux);
        if (j < 0) return;
        const int y0 = j * JOB_ROWS, y1 = y0 + JOB_ROWS < W ? y0 + JOB_ROWS : W;
        /* the job's rows of the plan's rect, on the panel (the bands read nothing else): any rect, the doors' columns
         * or a row renderer's rows (fk_rs_job_rows, rd wave 1 Task 3) */
        omni_rect_t r = {0, 0, 0, 0};
        if (d->job_draw) fk_rs_job_rows(y0, y1, d->job_need, &r);
        if (r.x0 < r.x1 && r.y0 < r.y1) {
            const int64_t t0 = esp_timer_get_time();
            v->ops->draw(v->ctx, &d->job_in, r, d->interior + (size_t)r.y0 * W + r.x0, W);
            const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            portENTER_CRITICAL(&s_mux);
            d->job_draw_us += us;
            portEXIT_CRITICAL(&s_mux);
        }
        for (int b0 = y0; b0 < y1; b0 += DISP_DIRECT_ROWS) {
            const omni_rect_t c = {0, (int16_t)b0, W, (int16_t)(b0 + DISP_DIRECT_ROWS < y1 ? b0 + DISP_DIRECT_ROWS : y1)};
            portENTER_CRITICAL(&s_mux);
            const bool ours = fk_rs_claim_ours(&d->job_fail, d->claim_gen, s_claim_gen);   /* I1 */
            portEXIT_CRITICAL(&s_mux);
            if (!ours) return;   /* a later view's claim: none of this reveal's bands go into it */
            if (!disp_direct_send_chunk(c, reveal_fill, (void *)d)) {   /* aborted: no job after it either */
                portENTER_CRITICAL(&s_mux);
                d->job_fail = true;
                portEXIT_CRITICAL(&s_mux);
                return;
            }
        }
    }
}

/* ------------------------------------------------------------ the rest motion's frames (wave 2 Task 14) */

/* a band of the rest motion's frame (render task): the face straight into the band (row 0 = c.y0, column x at offset x,
 * stride W), at the frame's input */
static void motion_fill(void *ctx, uint16_t *band, omni_rect_t c)
{
    fk_view_t *v = ctx;
    const int64_t t0 = esp_timer_get_time();
    v->ops->draw(v->ctx, &v->d.job_in, c, band + c.x0, W);
    const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    portENTER_CRITICAL(&s_mux);
    v->d.job_draw_us += us;
    portEXIT_CRITICAL(&s_mux);
}

/* Both cores: one band a job until none are left, the path aborts or the claim is no longer this run's (I1, M1) */
static void motion_jobs(fk_view_t *v)
{
    reveal_t *d = &v->d;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const int j = fk_rs_job_take(&d->job_next, MJOBS, &d->job_fail, d->claim_gen, s_claim_gen);
        portEXIT_CRITICAL(&s_mux);
        if (j < 0) return;
        const int y0 = j * DISP_DIRECT_ROWS;
        const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + DISP_DIRECT_ROWS < W ? y0 + DISP_DIRECT_ROWS : W)};
        if (!disp_direct_send_chunk(c, motion_fill, v)) {   /* aborted: no job after it either */
            portENTER_CRITICAL(&s_mux);
            d->job_fail = true;
            portEXIT_CRITICAL(&s_mux);
            return;
        }
    }
}

/* The rest motion's worker (core 1): whole frames from a go until quit, an abort or the claim moving, each shared with
 * the helper (core 0). A frame's input is the newest the face drew (m_in), moved on to the frame's start. The same
 * guarantee as reveal_worker's: every job take, band send and frame end checks the claim is still this run's. */
static void motion_worker(void *arg)
{
    fk_view_t *v = arg;
    reveal_t *d = &v->d;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const bool quit = d->quit, go = d->go;
        d->go = false;
        if (go) d->busy = true;
        portEXIT_CRITICAL(&s_mux);
        if (quit) break;
        if (!go) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        bool cut = false;
        for (;;) {
            portENTER_CRITICAL(&s_mux);
            const bool stop = d->quit || d->claim_gen != s_claim_gen;
            fk_face_in_t in = v->m_in;
            const int64_t in_us = v->m_in_us;
            portEXIT_CRITICAL(&s_mux);
            if (stop || !disp_direct_active()) {
                cut = true;
                break;
            }
            const int64_t f0 = esp_timer_get_time();
            fk_face_in_advance(v->ops, &in, f0 > in_us ? (uint32_t)((f0 - in_us) / 1000) : 0u);
            d->job_in = in;
            portENTER_CRITICAL(&s_mux);
            d->job_next = 0;
            d->job_fail = false;
            d->job_draw_us = 0;
            d->h_go = v->helper != NULL;
            portEXIT_CRITICAL(&s_mux);
            if (v->helper) xTaskNotifyGive(v->helper);   /* the input before the notify: both cores read it */
            motion_jobs(v);
            if (v->helper) xSemaphoreTake(v->h_done, portMAX_DELAY);
            portENTER_CRITICAL(&s_mux);
            const bool sent = !d->job_fail, ours = d->claim_gen == s_claim_gen;
            const uint32_t prep = d->job_draw_us;
            portEXIT_CRITICAL(&s_mux);
            d->prep_sum += prep;
            if (prep > d->prep_max) d->prep_max = prep;
            if (!sent || !ours) {
                cut = true;
                break;
            }
            disp_direct_frame_end(++d->seq);
            disp_stats_frame_rendered();
            frame_stats(d, (uint32_t)(esp_timer_get_time() - f0));
            vTaskDelay(1);   /* IDLE's turn on this core between frames (the task watchdog; the bench's rule) */
        }
        portENTER_CRITICAL(&s_mux);
        d->busy = false;
        d->worker_done = true;
        d->worker_cut = cut;
        portEXIT_CRITICAL(&s_mux);
    }
    xSemaphoreGive(v->exited);
    vTaskSuspend(NULL);
}

/* The helper (core 1's partner on core 0): a frame's jobs on each go, then h_done. */
static void view_helper(void *arg)
{
    fk_view_t *v = arg;
    reveal_t *d = &v->d;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&s_mux);
        const bool quit = d->h_quit, go = d->h_go;
        d->h_go = false;
        portEXIT_CRITICAL(&s_mux);
        if (quit) break;
        if (!go) continue;
        if (v->ops->direct) motion_jobs(v);   /* the rest motion's frame (wave 2 Task 14) */
        else run_jobs(v);
        xSemaphoreGive(v->h_done);
    }
    xSemaphoreGive(v->h_exited);
    vTaskSuspend(NULL);
}

/* The worker: direct frames from a go until the end, an abort or quit, each frame's jobs shared with the helper. */
static void reveal_worker(void *arg)
{
    fk_view_t *v = arg;
    reveal_t *d = &v->d;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const bool quit = d->quit, go = d->go;
        d->go = false;
        if (go) d->busy = true;
        portEXIT_CRITICAL(&s_mux);
        if (quit) break;
        if (!go) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        bool cut = false;
        for (;;) {
            portENTER_CRITICAL(&s_mux);
            const bool stop = d->quit || d->claim_gen != s_claim_gen, skip = d->skip;
            portEXIT_CRITICAL(&s_mux);
            if (stop || !disp_direct_active()) {
                cut = true;
                break;
            }
            const int64_t f0 = esp_timer_get_time();
            const bool done = frame_plan(v, elapsed_ms(d), skip);
            portENTER_CRITICAL(&s_mux);
            d->job_next = 0;
            d->job_fail = false;
            d->job_draw_us = 0;
            d->h_go = v->helper != NULL;
            portEXIT_CRITICAL(&s_mux);
            if (v->helper) xTaskNotifyGive(v->helper);   /* the plan before the notify: both cores read it */
            run_jobs(v);
            if (v->helper) xSemaphoreTake(v->h_done, portMAX_DELAY);   /* its jobs end, or see the abort */
            portENTER_CRITICAL(&s_mux);
            const bool sent = !d->job_fail;
            const uint32_t prep = d->job_draw_us;
            portEXIT_CRITICAL(&s_mux);
            d->prep_sum += prep;
            if (prep > d->prep_max) d->prep_max = prep;
            const uint32_t us = (uint32_t)(esp_timer_get_time() - f0);
            portENTER_CRITICAL(&s_mux);
            const bool ours = d->claim_gen == s_claim_gen;   /* M1: the claim is still this reveal's */
            portEXIT_CRITICAL(&s_mux);
            if (!sent || !ours) {   /* aborted (no chunk after it went either), or released under us */
                cut = true;
                break;
            }
            /* What is guaranteed: every job take, every band send and this frame end first check that the claim is
             * still this reveal's (s_claim_gen, under s_mux), and the release moves it before disp_direct_release.
             * disp_direct_active() alone is not enough: a later view's disp_direct_claim makes it true again. What
             * is left is the few instructions between a check and the call after it (disp_direct_send_chunk or
             * disp_direct_frame_end): a release and a new claim both landing there would let one band, or one frame
             * end, of the old reveal reach the new claim. Both come from the LVGL task in turn, and a claim follows
             * a release by at least its back-off or a still frame, so in practice the window is closed. */
            disp_direct_frame_end(++d->seq);
            disp_stats_frame_rendered();
            frame_stats(d, us);
            if (done) break;
        }
        portENTER_CRITICAL(&s_mux);
        d->busy = false;
        d->worker_done = true;
        d->worker_cut = cut;
        portEXIT_CRITICAL(&s_mux);
    }
    xSemaphoreGive(v->exited);
    vTaskSuspend(NULL);
}

static bool worker_start(fk_view_t *v)
{
    if (v->task) return true;
    v->exited = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
    v->h_exited = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
    v->h_done = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
    bool ok = v->exited && v->h_exited && v->h_done;
    portENTER_CRITICAL(&s_mux);
    v->d.quit = v->d.h_quit = v->d.h_go = false;
    portEXIT_CRITICAL(&s_mux);
    /* the helper first: the worker's frames use it whenever it exists; without it they run on one core */
    if (ok && xTaskCreatePinnedToCoreWithCaps(view_helper, "fkview2", WORKER_STACK, v, WORKER_PRIO, &v->helper,
                                              HELPER_CORE, MALLOC_CAP_SPIRAM) != pdPASS)
        v->helper = NULL;
    if (ok && xTaskCreatePinnedToCoreWithCaps(v->ops->direct ? motion_worker : reveal_worker, "fkview", WORKER_STACK,
                                              v, WORKER_PRIO, &v->task, WORKER_CORE, MALLOC_CAP_SPIRAM) != pdPASS)
        v->task = NULL;
    if (ok && v->task) return true;
    if (v->helper) {   /* idle: it stops at once */
        portENTER_CRITICAL(&s_mux);
        v->d.h_quit = true;
        portEXIT_CRITICAL(&s_mux);
        xTaskNotifyGive(v->helper);
        xSemaphoreTake(v->h_exited, portMAX_DELAY);
        vTaskDeleteWithCaps(v->helper);
        v->helper = NULL;
    }
    if (v->exited) vSemaphoreDeleteWithCaps(v->exited);
    if (v->h_exited) vSemaphoreDeleteWithCaps(v->h_exited);
    if (v->h_done) vSemaphoreDeleteWithCaps(v->h_done);
    v->exited = v->h_exited = v->h_done = NULL;
    return false;
}

static bool worker_busy(fk_view_t *v)
{
    portENTER_CRITICAL(&s_mux);
    const bool b = v->d.busy || v->d.go;
    portEXIT_CRITICAL(&s_mux);
    return b;
}

static bool worker_stop(fk_view_t *v)   /* the view's destroy; false: the worker did not stop (it and all it reads
                                        * are then left: N1) */
{
    if (!v->task) return true;
    portENTER_CRITICAL(&s_mux);
    v->d.quit = true;
    portEXIT_CRITICAL(&s_mux);
    xTaskNotifyGive(v->task);
    const bool stopped = xSemaphoreTake(v->exited, pdMS_TO_TICKS(STOP_WAIT_MS)) == pdTRUE;
    if (!stopped) {   /* never seen: the tasks, their semaphores and the view they read are left, never freed under them */
        ESP_LOGE(TAG, "the reveal's worker did not stop in %d ms: it and the view are left (leaked)", STOP_WAIT_MS);
        return false;
    }
    /* the worker has exited between frames, so the helper is idle (each frame waits for its jobs) */
    if (v->helper) {
        portENTER_CRITICAL(&s_mux);
        v->d.h_quit = true;
        portEXIT_CRITICAL(&s_mux);
        xTaskNotifyGive(v->helper);
        xSemaphoreTake(v->h_exited, portMAX_DELAY);
        vTaskDeleteWithCaps(v->helper);
    }
    vTaskDeleteWithCaps(v->task);
    vSemaphoreDeleteWithCaps(v->exited);
    vSemaphoreDeleteWithCaps(v->h_exited);
    vSemaphoreDeleteWithCaps(v->h_done);
    v->task = v->helper = NULL;
    v->exited = v->h_exited = v->h_done = NULL;
    return true;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
static void bench_account(fk_view_t *v)
{
    reveal_t *d = &v->d;
    if (!d->bench_on) return;
    fk_reveal_stats_t *b = &d->bench;
    b->swings++;
    b->frames += d->frames;
    b->us_sum += d->us_sum;
    if (d->us_max > b->us_max) b->us_max = d->us_max;
    if (d->ds.mode == FK_RS_DIRECT) b->lost += disp_direct_lost() - d->lost0;   /* since the claim, which zeroes the count */
    b->prep_sum += d->prep_sum;
    if (d->prep_max > b->prep_max) b->prep_max = d->prep_max;
    if (d->psram_min < b->psram_min) b->psram_min = d->psram_min;
    if (d->int_min < b->int_min) b->int_min = d->int_min;
    if (d->ds.mode == FK_RS_DIRECT) b->direct++;
    if (v->task) {   /* Task 12: the 4 KB stacks' margin, the smaller of the two */
        uint32_t hw = (uint32_t)uxTaskGetStackHighWaterMark(v->task);
        if (v->helper && (uint32_t)uxTaskGetStackHighWaterMark(v->helper) < hw)
            hw = (uint32_t)uxTaskGetStackHighWaterMark(v->helper);
        b->stack_hwm = hw;
    }
}
#endif

static void reveal_free(reveal_t *d)
{
    heap_caps_free(d->interior);
    heap_caps_free(d->rctx);
    for (uint8_t i = 0; i < d->ncopy; i++) heap_caps_free(d->copy[i]);
    d->interior = NULL;
    d->rctx = NULL;
    d->ncopy = 0;
}

/* The copy (the bench's copy, fk_view_set_copy): the reveal's bind reads its assets through this instead of assets_raw,
 * each one copied to PSRAM and kept with the reveal's buffers (freed with them). LVGL ctx, during the bind only. */
static reveal_t *s_copying;
static bool s_copy_short;   /* the copy found no PSRAM for an asset */
static bool s_copy_over;    /* the bind read more assets than COPY_MAX (the copy refused the one past it) */
static const void *copy_get(const char *name, size_t *size)
{
    reveal_t *d = s_copying;
    const void *p = assets_raw(name, size);
    if (p == NULL || d == NULL) return p;
    if (d->ncopy >= COPY_MAX) {
        s_copy_over = true;
        return NULL;
    }
    void *c = heap_caps_malloc(*size ? *size : 1, MALLOC_CAP_SPIRAM);
    if (c == NULL) {
        s_copy_short = true;
        return NULL;
    }
    memcpy(c, p, *size);
    d->copy[d->ncopy++] = c;
    return c;
}

static bool reveal_begin(fk_view_t *v, bool latch);
static void reveal_go(fk_view_t *v, omni_dd_why_t why, bool latch);
static void reveal_claim(fk_view_t *v);

/* LVGL ctx: the reveal ends (done, skipped, cut or stopped): the settled frame into the canvas, shown by LVGL. */
static void reveal_end(fk_view_t *v, bool cut)
{
    reveal_t *d = &v->d;
    const bool was_direct = fk_rs_end_releases(&d->ds);
    if (was_direct) {
        if (worker_busy(v)) {   /* cut while the worker is in a frame: it sees the abort and stops */
            disp_direct_abort();
            for (int i = 0; i < STOP_WAIT_MS && worker_busy(v); i++) vTaskDelay(pdMS_TO_TICKS(1));
        }
#if CONFIG_OMNI_DEBUG_CONSOLE
        const uint32_t lost_pre = disp_direct_lost() - d->lost0;   /* Task 17 (Task 12 M2): where a lost band falls */
#endif
        release_as(v);   /* M1: s_claim_gen moves (the claim is no longer this reveal's) before the panel goes back */
#if CONFIG_OMNI_DEBUG_CONSOLE
        const uint32_t lost_post = disp_direct_lost() - d->lost0;
        if (lost_post) ESP_LOGI(TAG, "reveal: lost %" PRIu32 " bands by the end of the swing, %" PRIu32 " after the release%s",
                                lost_pre, lost_post, cut ? " (cut)" : "");
#endif
    }
#if CONFIG_OMNI_DEBUG_CONSOLE
    bench_account(v);
    const bool again = d->bench_on && d->bench_left > 1 && !cut;
#else
    const bool again = false;
#endif
    const unsigned fate = fk_rs_end(&d->ds, cut, was_direct && worker_busy(v));
    if (fate & FK_RE_FREE) {
        reveal_free(d);
    } else {   /* M1: the worker may still read the interior and the copy: left until it is idle (fk_rs_gate, destroy) */
        ESP_LOGE(TAG, "reveal: the worker did not stop in %d ms: its buffers are left until it does", STOP_WAIT_MS);
    }
    if (v->timer && !again) {
        lv_timer_delete(v->timer);
        v->timer = NULL;
    }
    fk_face_in_t in = v->latest;
    in.wake_ms = UINT32_MAX;   /* settled from now on */
    fk_view_draw(v, &in, true);
    if (fate & FK_RE_STILL) disp_direct_still();   /* the first exact frame after the motion (R2-1) */
    ESP_LOGI(TAG, "reveal %s: %u frames (%s), %u us/frame avg, %u max", cut ? "cut" : "done", (unsigned)d->frames,
             was_direct ? "direct" : "canvas", (unsigned)(d->frames ? d->us_sum / d->frames : 0),
             (unsigned)d->us_max);
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (!d->bench_on) return;
    if (again) {
        d->bench_left--;
        if (reveal_begin(v, false)) return;   /* the bench is silent */
    }
    d->bench_left = 0;
    d->bench_on = false;
    d->bench.done = true;
    d->bench.cut = cut;
    if (v->timer) {
        lv_timer_delete(v->timer);
        v->timer = NULL;
    }
#endif
}

/* LVGL ctx: the fallback's frame into the canvas (no direct path): the interior, then the doors over it. */
static void canvas_frame(fk_view_t *v)
{
    reveal_t *d = &v->d;
    const int64_t f0 = esp_timer_get_time();
    const bool done = reveal_prepare(v, elapsed_ms(d), d->skip);
    uint16_t *px = canvas_px(v);
    if (canvas_stride(v) != W) {   /* never for a 466 RGB565 buffer; the band layout needs stride 466 */
        reveal_end(v, true);
        return;
    }
    for (int y0 = 0; y0 < W; y0 += DISP_DIRECT_ROWS) {   /* the band layout: row 0 = y0, column x at offset x */
        const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + DISP_DIRECT_ROWS < W ? y0 + DISP_DIRECT_ROWS : W)};
        d->ro->band(d->arg, d->rctx, d->interior, c, px + (size_t)y0 * W);
    }
    lv_obj_invalidate(v->canvas);
    frame_stats(d, (uint32_t)(esp_timer_get_time() - f0));
    if (done) reveal_end(v, false);
}

static void reveal_timer(lv_timer_t *t)
{
    fk_view_t *v = lv_timer_get_user_data(t);
    reveal_t *d = &v->d;
    portENTER_CRITICAL(&s_mux);
    const bool wdone = d->worker_done, wcut = d->worker_cut;
    portEXIT_CRITICAL(&s_mux);
    bool latch;
    const fk_rt_t what = fk_rs_tick(&d->ds, elapsed_ms(d), d->ro->pop_ms, wdone, disp_direct_active(), &latch);
    if (latch && d->ro->pop_asset) sfx_play_asset(d->ro->pop_asset);   /* the reveal's sound at the pop (WD15) */
    switch (what) {
    case FK_RT_CANVAS_FRAME: canvas_frame(v); return;
    case FK_RT_END: reveal_end(v, wcut); return;
    case FK_RT_END_CUT:   /* orphan, provisioning, the display force-ended: the end at once (Review Focus 4) */
        reveal_end(v, true);
        return;
    case FK_RT_IDLE: return;
    case FK_RT_CLAIM: reveal_claim(v); return;
    case FK_RT_CHECK: break;
    }
    const uint32_t shown = disp_direct_shown();
    if (shown != d->shown0) {   /* a direct frame went out: admission once a frame, with the heap walk */
        for (uint32_t n = shown - d->shown0; n > 0 && n < 64; n--) disp_stats_frame_shown();
        d->shown0 = shown;
        const omni_dd_why_t why = disp_direct_check(v->canvas, true);
        if (why != OMNI_DD_OK) {
            ESP_LOGW(TAG, "reveal: direct check refused (%d): cut", (int)why);
            reveal_end(v, true);
        }
    }
}

static bool reveal_begin(fk_view_t *v, bool latch)
{
    reveal_t *d = &v->d;
    switch (fk_rs_gate(&d->ds, worker_busy(v))) {
    case FK_RS_REFUSE: return false;   /* running, or M1: the last worker still reads its buffers */
    case FK_RS_GO_FREE: reveal_free(d); break;   /* M1: the last worker's buffers, freed now it is idle */
    case FK_RS_GO: break;
    }
    const fk_reveal_ops_t *ro = v->ops->reveal_ops;
    d->ro = ro;
    d->arg = v->ops->reveal_arg;
    d->rctx = heap_caps_calloc(1, ro->rctx_bytes ? ro->rctx_bytes : 1, MALLOC_CAP_SPIRAM);
    if (!d->rctx) {
        ESP_LOGW(TAG, "reveal: no PSRAM for the reveal's context: a plain wake");
        return false;
    }
#if CONFIG_OMNI_DEBUG_CONSOLE
    const bool copy = d->bench_on ? d->bench_copy : s_copy;
#else
    const bool copy = s_copy;
#endif
    s_copying = copy ? d : NULL;
    s_copy_short = s_copy_over = false;
    const bool bound = ro->bind(d->arg, v->ops->reveal_asset, copy ? copy_get : assets_raw, d->rctx);
    s_copying = NULL;
    if (!bound) {   /* its assets missing (or, with the copy, no PSRAM for it, or more than COPY_MAX): a plain wake */
        if (s_copy_short) ESP_LOGW(TAG, "reveal: no PSRAM for the leaf copy: a plain wake");
        else if (s_copy_over) ESP_LOGW(TAG, "reveal: the bind read more than %d assets to copy: a plain wake",
                                       COPY_MAX);
        reveal_free(d);
        return false;
    }
    d->interior = heap_caps_calloc((size_t)W * W, 2, MALLOC_CAP_SPIRAM);
    if (!d->interior) {
        ESP_LOGW(TAG, "reveal: no PSRAM for the interior: a plain wake");
        reveal_free(d);
        return false;
    }
    d->in = v->latest;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    d->unix0 = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    d->frames = d->us_sum = d->us_max = d->prep_sum = d->prep_max = 0;
    d->psram_min = d->int_min = UINT32_MAX;
    d->t0_us = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    d->skip = false;
    d->worker_done = d->worker_cut = false;
    portEXIT_CRITICAL(&s_mux);
    if (v->timer == NULL) v->timer = lv_timer_create(reveal_timer, POLL_MS, v);
    if (v->timer == NULL) {
        reveal_free(d);
        return false;
    }
    const omni_dd_why_t why = DISP_DIRECT_BUILT && worker_start(v) ? claim_as(v, v->canvas, false) : OMNI_DD_OFF;
    if (why == OMNI_DD_DISPLAY) {   /* a lift's wake: the panel is not on yet. Wait for it, the canvas black (the
                                     * doors' first frame) for the redraw that comes with it (B16) */
        fk_rs_waiting(&d->ds, latch);
        d->wait_until = esp_timer_get_time() + PANEL_WAIT_MS * 1000LL;
        memset(v->buf->data, 0, v->buf->data_size);
        lv_obj_invalidate(v->canvas);
        return true;
    }
    reveal_go(v, why, latch);
    return true;
}

/* The doors' time starts now: through the direct path (the claim held) or the canvas. */
static void reveal_go(fk_view_t *v, omni_dd_why_t why, bool latch)
{
    reveal_t *d = &v->d;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    d->unix0 = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    d->t0_us = esp_timer_get_time();
    fk_rs_started(&d->ds, why == OMNI_DD_OK, latch);
    if (why == OMNI_DD_OK) {
        d->shown0 = disp_direct_shown();
        d->lost0 = disp_direct_lost();   /* after the claim: it zeroes the count (the bench once read 2^32 - 1) */
        portENTER_CRITICAL(&s_mux);
        d->claim_gen = ++s_claim_gen;   /* M1: this reveal's claim */
        d->go = true;
        portEXIT_CRITICAL(&s_mux);
        xTaskNotifyGive(v->task);
    } else {   /* refused (or no direct path built): the frames through the canvas at LVGL's rate */
#if CONFIG_OMNI_DEBUG_CONSOLE
        if (d->bench_on) d->bench.refused++;
#endif
        ESP_LOGI(TAG, "reveal: direct claim refused (%d): through the canvas", (int)why);
    }
}

/* Waiting for the panel (FK_RS_WAIT): claim again; direct once it holds, the canvas if the wait is over first. */
static void reveal_claim(fk_view_t *v)
{
    reveal_t *d = &v->d;
    const omni_dd_why_t why = claim_as(v, v->canvas, false);
    if (why == OMNI_DD_DISPLAY && esp_timer_get_time() < d->wait_until) return;
    ESP_LOGI(TAG, "reveal: waited %d ms for the panel; claim %d", PANEL_WAIT_MS -
             (int)((d->wait_until - esp_timer_get_time()) / 1000), (int)why);
    reveal_go(v, why, d->ds.latch);
}

bool fk_view_reveal(fk_view_t *v, const fk_face_in_t *in, bool reduced)
{
    /* not under either bench (M1: a theme's own bench's bracket too) */
    if (v == NULL || v->d.ds.mode != FK_RS_OFF || BENCH_ON(&v->d) || s_bench || !v->ops->reveal) return false;
    if (v->ops->reveal_ops == NULL) {   /* a configuration error: reveal without its ops (a plain wake) */
        static bool logged;
        if (!logged) ESP_LOGE(TAG, "%s: reveal set without reveal_ops: a plain wake", v->ops->key ? v->ops->key : "?");
        logged = true;
        return false;
    }
    v->latest = *in;
    v->latest.glance = false;
    v->d.reduced = reduced;
    return reveal_begin(v, !reduced);   /* the Doors swing's latch, at the pop; never the cross-fade (Fade, reduced) */
}

void fk_view_reveal_skip(fk_view_t *v)
{
    if (v == NULL || v->d.ds.mode == FK_RS_OFF) return;
    portENTER_CRITICAL(&s_mux);
    v->d.skip = true;   /* the next frame is the reveal's skip: the end frame exactly */
    portEXIT_CRITICAL(&s_mux);
}

bool fk_view_reveal_active(const fk_view_t *v) { return v && v->d.ds.mode != FK_RS_OFF; }

void fk_view_reveal_stop(fk_view_t *v)
{
    if (v == NULL || v->d.ds.mode == FK_RS_OFF) return;
#if CONFIG_OMNI_DEBUG_CONSOLE
    v->d.bench_left = 0;
#endif
    reveal_end(v, true);
}

/* ------------------------------------------------------------ the rest motion's life (LVGL ctx; wave 2 Task 14) */

/* the motion's claim ends (cut or stopped): the worker aborted if it is in a frame, the claim released (s_claim_gen
 * moves first), and with redraw the canvas gets the newest frame whole, which LVGL shows (the release invalidated what it
 * dropped). A worker that does not come back in STOP_WAIT_MS finds the claim moved and stops at its next check; the
 * next claim waits for it (motion_begin), and the destroy leaves it the view (N1). */
static void motion_end(fk_view_t *v, bool redraw)
{
    if (!v->m_on) return;
    reveal_t *d = &v->d;
    if (worker_busy(v)) {
        disp_direct_abort();
        for (int i = 0; i < STOP_WAIT_MS && worker_busy(v); i++) vTaskDelay(pdMS_TO_TICKS(1));
    }
    release_as(v);
    v->m_on = false;
    v->m_retry = fk_dm_retry_at(OMNI_DD_OK, esp_timer_get_time());
    if (worker_busy(v)) ESP_LOGE(TAG, "rest motion: the worker did not stop in %d ms: it stops at its next check",
                                 STOP_WAIT_MS);
    ESP_LOGI(TAG, "rest motion %s: %u frames, %u us/frame avg (%u max), lost %u", v->ops->key ? v->ops->key : "?",
             (unsigned)d->frames, (unsigned)(d->frames ? d->us_sum / d->frames : 0), (unsigned)d->us_max,
             (unsigned)(disp_direct_lost() - d->lost0));
    if (redraw) {
        fk_face_in_t in = v->latest;
        fk_view_draw(v, &in, true);
        disp_direct_still();   /* the Task 14 review: the whole canvas frame ends the exit's back-off, as reveal_end does */
    } else {
        v->m_still = true;     /* the glance's (or the next wake's) first whole draw ends it (fk_view_draw) */
    }
}

/* the motion's claim begins (fk_dm_want_claim held): the worker started, the claim taken under the one-holder rule;
 * refused, the next try waits (fk_dm_retry_at) and the canvas carries on at the face's cadence */
static void motion_begin(fk_view_t *v, int64_t now)
{
    reveal_t *d = &v->d;
    if (worker_busy(v)) return;   /* a cut run's worker still finishing its frame: the next poll */
    if (!DISP_DIRECT_BUILT || !worker_start(v)) {
        v->m_retry = fk_dm_retry_at(OMNI_DD_OFF, now);
        return;
    }
    const omni_dd_why_t why = claim_as(v, v->canvas, false);
    if (why != OMNI_DD_OK) {
        v->m_retry = fk_dm_retry_at(why, now);
        return;
    }
    v->m_on = true;
    v->m_still = false;   /* motion again: no still is owed */
    d->shown0 = disp_direct_shown();
    d->lost0 = disp_direct_lost();   /* after the claim: it zeroes the count */
    d->frames = d->us_sum = d->us_max = d->prep_sum = d->prep_max = 0;
    d->psram_min = d->int_min = UINT32_MAX;
    portENTER_CRITICAL(&s_mux);
    v->m_in = v->latest;
    v->m_in_us = esp_timer_get_time();
    d->worker_done = d->worker_cut = false;
    d->claim_gen = ++s_claim_gen;   /* M1: this run's claim */
    d->go = true;
    portEXIT_CRITICAL(&s_mux);
    xTaskNotifyGive(v->task);
}

/* every POLL_MS while the face is awake: claim when wanted; while held, admit each direct frame shown, and cut on a
 * refusal, the path going inactive, the worker's end, the glance, reduced motion or the bench */
static void motion_timer(lv_timer_t *t)
{
    fk_view_t *v = lv_timer_get_user_data(t);
    reveal_t *d = &v->d;
    const int64_t now = esp_timer_get_time();
    if (!v->m_on) {
        if (v->drawn && fk_dm_want_claim(v->m_want, v->glance, v->latest.reduced, s_bench, now, v->m_retry))
            motion_begin(v, now);
        return;
    }
    portENTER_CRITICAL(&s_mux);
    const bool wdone = d->worker_done;
    portEXIT_CRITICAL(&s_mux);
    if (wdone || !disp_direct_active() || !fk_dm_want_claim(v->m_want, v->glance, v->latest.reduced, s_bench, 0, 0)) {
        motion_end(v, true);   /* the orphan rule, provisioning, the display off, an abort; or no longer wanted */
        return;
    }
    const uint32_t shown = disp_direct_shown();
    if (shown != d->shown0) {   /* a direct frame went out: admission once a frame, with the heap walk */
        for (uint32_t n = shown - d->shown0; n > 0 && n < 64; n--) disp_stats_frame_shown();
        d->shown0 = shown;
        const omni_dd_why_t why = disp_direct_check(v->canvas, true);
        if (why != OMNI_DD_OK) {
            ESP_LOGW(TAG, "rest motion: direct check refused (%d): cut", (int)why);
            motion_end(v, true);
            v->m_retry = fk_dm_retry_at(why, esp_timer_get_time());
        }
    }
}

void fk_view_motion(fk_view_t *v, bool on)
{
    if (v == NULL || !v->ops->direct || v->ops->reveal) return;
    v->m_want = on;
    if (on) {
        v->m_retry = 0;
        if (v->timer == NULL) v->timer = lv_timer_create(motion_timer, POLL_MS, v);
        return;
    }
    motion_end(v, false);   /* the sleep: the glance draws next, whole */
    if (v->timer) lv_timer_delete(v->timer);
    v->timer = NULL;
}

void fk_view_set_glance(fk_view_t *v, bool glance)
{
    if (v == NULL || v->glance == glance) return;
    if (glance) fk_view_reveal_stop(v);   /* the glance never shows a half frame */
    if (glance) motion_end(v, false);    /* nor a direct one: the glance's first draw is whole */
    v->glance = glance;
    v->drawn = false;   /* the next draw is whole */
}

void fk_view_destroy(fk_view_t *v)
{
    if (v == NULL) return;
    fk_view_reveal_stop(v);
    motion_end(v, false);   /* wave 2 Task 14: the rest motion's claim released before the worker is asked to stop */
    const bool stopped = worker_stop(v);
    if (v->timer) lv_timer_delete(v->timer);
    v->timer = NULL;
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (s_current == v) s_current = NULL;
#endif
    /* the canvas object and its buffer are LVGL's side: the worker never touches them */
    if (v->canvas) lv_obj_delete(v->canvas);
    v->canvas = NULL;
    theme_buf_destroy(v->buf);
    v->buf = NULL;
    if (!fk_rs_destroy_frees(&v->d.ds, stopped))   /* N1: the worker still reads the view, its context (the face's
                                                    * draw) and the doors' buffers, and gives v->exited when it ends:
                                                    * all of it is left to it (leaked), never freed under it. The
                                                    * release in reveal_end moved s_claim_gen, so it ends no frame into
                                                    * a later view's claim (M1) */
        return;
    reveal_free(&v->d);   /* stuck ones too (M1): the worker is gone now */
    heap_caps_free(v->ctx);
    heap_caps_free(v);
}

const void *fk_view_ctx(const fk_view_t *v) { return v ? v->ctx : NULL; }

/* ------------------------------------------------------------ the console's bench (LVGL lock held; R101) */
#if CONFIG_OMNI_DEBUG_CONSOLE
fk_view_t *fk_view_current(void) { return s_current; }
const fk_face_ops_t *fk_view_ops(const fk_view_t *v) { return v ? v->ops : NULL; }

bool fk_view_reveal_bench(fk_view_t *v, int n, bool copy)
{
    if (v == NULL || n < 1 || v->d.ds.mode != FK_RS_OFF || v->d.bench_on || !v->ops->reveal || !v->ops->reveal_ops)
        return false;
    reveal_t *d = &v->d;
    memset(&d->bench, 0, sizeof d->bench);
    d->bench.psram_min = d->bench.int_min = UINT32_MAX;
    d->bench_on = true;
    d->bench_left = n;
    d->bench_copy = copy;
    d->reduced = false;
    if (!reveal_begin(v, false)) {   /* the bench is silent */
        d->bench_on = false;
        d->bench_left = 0;
        return false;
    }
    return true;
}

bool fk_view_reveal_bench_result(const fk_view_t *v, fk_reveal_stats_t *out)
{
    if (v == NULL || !v->d.bench.done) return false;
    *out = v->d.bench;
    return true;
}

void fk_view_set_copy(bool on) { s_copy = on; }

bool fk_view_bench_begin(void)
{
    if (s_bench || fk_view_reveal_active(s_current)) return false;   /* M1: not over the doors, nor a second bench */
    s_bench = true;   /* the rest motion ends at its next poll; fk_view_reveal refuses */
    return true;
}

void fk_view_bench_end(void)
{
    fk_view_bench_release();
    s_bench = false;
}

omni_dd_why_t fk_view_bench_claim(lv_obj_t *obj) { return claim_as(&s_bench_token, obj, true); }

void fk_view_bench_release(void)
{
    if (s_holder == &s_bench_token) release_as(&s_bench_token);
}
#endif
