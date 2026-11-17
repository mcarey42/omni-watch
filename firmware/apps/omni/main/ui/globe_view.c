#include "ui/globe_view.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "omni/globe_check.h"
#include "assets.h"
#include "disp_stats.h"
#include "ui/globe_pie.h"
#include "theme.h"

static const char *TAG = "globe";

/* m11: poll swaps the image's source between two buffers whose pixels the worker rewrites behind LVGL, and a destroy
 * frees them without lv_image_cache_drop. Both are right only while LVGL caches no image data or headers. */
#if !defined(CONFIG_LV_CACHE_DEF_SIZE) || !defined(CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT) || \
    CONFIG_LV_CACHE_DEF_SIZE != 0 || CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT != 0
#error "globe_view needs the LVGL image cache and image header cache off (LV_CACHE_DEF_SIZE, LV_IMAGE_HEADER_CACHE_DEF_CNT)"
#endif

#define WORKER_STACK 4096
#define WORKER_PRIO 2   /* under LVGL; it yields (vTaskDelay 1) between LUT row batches and bench frames for the task WDT */
#define BUILD_PRIO 4    /* while building (no globe frame to show yet): above LVGL's draw threads (3), under its task (6) */
#define LUT_ROWS 8      /* LUT rows per build step (measured in the build log, "step max"): destroy waits for at most
                         * one step, and at BUILD_PRIO the event loop (priority 3) and LVGL's draw threads wait for it */
#define WORKER_CORE 1
/* The helper (O1): a second task on the other core that builds the second half of the LUT rows and renders box
 * rows [S/2, S) of every frame while the worker renders [0, S/2) (the disc is symmetric, so the halves cost the
 * same). It runs only what the worker hands it and never touches LVGL or the panel either. */
#define HELPER_STACK 3072
#define HELPER_CORE 0
#define RENDER_CHUNK 8   /* rows a core takes at a time when the two share a frame */
#define REST_GAP_US 1500000     /* a frame after a longer gap is a rest render (logged on its own) */
#define LOG_WINDOW_US 10000000  /* spec §12: a stats line every 10 s while frames flow */
#define JOB_TIMEOUT_MS 30000

omni_globe_span_fn globe_view_kernel = omni_globe_span_fast;     /* planar LUTs; `globe kernel` switches (console) */
omni_globe_cspan_fn globe_view_ckernel = globe_cspan_pie;        /* compact LUTs: PIE SIMD (globe_pie.c) */
void (*globe_view_over_run)(const uint32_t *, uint16_t *, int) = globe_over_pie;   /* compact overlay runs */

/* Everything the worker and LVGL context share is guarded by this one spinlock (a few word copies at a time). It is
 * static, so in internal RAM: the view itself lives in PSRAM. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static globe_view_t *s_current;

typedef enum { JOB_NONE, JOB_BENCH, JOB_SELFTEST } job_t;

typedef struct {
    bool ok;
    int pass;
    char why[112];
    uint32_t ref_avg, ref_max, k_avg, k_max, dual_avg, dual_max, keep_avg, keep_max, base_avg, base_max, shade_avg,
        shade_max;
    uint32_t rd_kbs, wr_kbs, cp_kbs, hot_ns_px[2], cold_ns_px[2], disc_px, keep_noover_avg, over_disc_px;
    uint64_t synth_ref, synth_k, csynth_ref, csynth_k, lut_digest, shade_digest;
} job_res_t;

struct globe_view {
    globe_view_desc_t d;
    int32_t x, y;                 /* the box origin on screen (under_name's crop) */
    bool bad_lon0_logged;         /* a non-finite lon0 was logged (once per view: it would come every frame) */
#if CONFIG_OMNI_DEBUG_CONSOLE
    bool null_render;             /* console `globe kernel none`, this view only (R100/R101: a new view renders) */
#endif
    lv_obj_t *img;
    lv_draw_buf_t *buf[2];
    int front;                    /* buf[front] is shown; the worker renders into buf[!front] */
    /* built by the worker (first job), read only after `built` */
    void *lut_mem;                /* the planar LUT (freed once packed into the compact one) */
    size_t lut_bytes;
    omni_globe_lut_t lut;
    bool compact;                 /* frames render from clut (ortho, tex_w <= 512) */
    void *clut_mem;
    omni_globe_clut_t clut;
    uint32_t *tex_px;
    omni_globe_tex_t tex;
    omni_globe_shade_t *shade;
    float shade_gain;             /* the gain shade was built for (NAN: none yet) */
    bool shade_pie_ok;            /* shade is within globe_cspan_pie's exact range (GLOBE_PIE_SHADE_MAX) */
    uint32_t step_max_us;         /* the longest LUT build step (LUT_ROWS rows, BUILD_PRIO): logged at build */
    uint32_t psram_low;           /* m9: the least PSRAM free at the build's peaks (logged at build) */
    uint16_t *clean;              /* the base asset over black, and under_name outside the disc (NULL: neither) */
    uint16_t *base;               /* clean + halo (NULL until a halo is asked for) */
    bool base_halo;               /* base holds a halo (the key below) */
    uint32_t base_gen;            /* bumped whenever base changes */
    int gen_rect[2][4];           /* the rect base_gen g changed (inclusive), at [g & 1]; x1 < x0: none */
    uint16_t *ring;               /* clean + the halo without a lobe (live's lobe moves with lon0: R58) */
    float ring_strength;          /* NAN: ring not built */
    int lobe_rect[4];             /* where base may differ from ring (the lobe's rect); x1 < x0: none */
    float lobe_dir[2];            /* the lobe base holds ({0, 0}: none) */
    struct { const uint16_t *under; uint32_t gen; bool valid; } bkey[2];   /* what buf[i] holds outside the disc */
    float key_dir[2], key_strength;
    void *over_mem;
    omni_overlay_t over;
    bool has_over;
    TaskHandle_t task, helper;
    SemaphoreHandle_t job_done, exited, help_done;
    /* the worker's request to the helper (written before the notify, read after it; the worker waits on help_done) */
    enum { HELP_NONE, HELP_LUT, HELP_PACK, HELP_RENDER, HELP_LOBE, HELP_QUIT } help;
    float help_dir[2], help_old[2], help_strength;   /* HELP_LOBE */
    const omni_globe_frame_t *help_frame;
    uint16_t *help_out;
    int help_y0, help_y1;
    bool lin;                     /* the frame being rendered is a still one: linear-in-u kernel (R76) */
    bool help_keep, help_ref, help_started;   /* help_started: the helper took the job (under s_mux) */
    const int *help_dirty;
    volatile int next_row;        /* HELP_RENDER: the next unclaimed chunk of rows (both cores take chunks) */
    esp_pm_lock_handle_t pm;      /* CPU_FREQ_MAX while the worker has work (asleep, DFS would run it at 40 MHz) */
    uint32_t bytes;
    /* shared under s_mux */
    bool built, failed, quit, ready, has_pending, has_last;
    globe_view_frame_t pending, last;
    uint32_t pending_seq, last_seq, back_seq;   /* the sequence numbers of those frames and of the back buffer's */
    double hold;
    uint32_t back_us;             /* render time of the frame waiting in the back buffer */
    job_t job;
    int job_n;
    job_res_t res;
    uint32_t st_frames, st_max, st_base_max, build_ms;
    uint64_t st_sum;
    /* LVGL context only: sequence numbers (globe_view_submit) and the log window */
    uint32_t seq, shown_seq;
    uint32_t keep_from;           /* globe_view_reset: frames numbered below this are dropped */
    uint32_t resets, dropped;     /* stats */
    disp_swap_t swap;             /* the last swap: drawn yet (shown/rendered) */
    int64_t win_t0, last_swap;
    uint32_t win_frames, win_max;
    uint64_t win_sum;
};

static void *ps_alloc(globe_view_t *v, size_t n)
{
    void *p = heap_caps_aligned_alloc(16, n, MALLOC_CAP_SPIRAM);
    portENTER_CRITICAL(&s_mux);   /* the worker allocates too (LUT, halo base); stats read it */
    if (p) v->bytes += (uint32_t)n;
    portEXIT_CRITICAL(&s_mux);
    return p;
}

static uint32_t us_since(int64_t t0) { return (uint32_t)(esp_timer_get_time() - t0); }

/* m9: PSRAM free now, if lower than the build has seen (called at its peaks, by the worker) */
static void psram_note(globe_view_t *v)
{
    const uint32_t f = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (f < v->psram_low) v->psram_low = f;
}

/* ---- Worker */

static inline uint16_t pack565(int r, int g, int b)
{
    return (uint16_t)(((unsigned)r >> 3) << 11 | ((unsigned)g >> 2) << 5 | ((unsigned)b >> 3));
}

/* Box pixel (x, y) lies wholly outside the disc: its centre is R + 1/2 or more from the disc's, so the LUT gives it
 * coverage 0 (omni_globe_lut_rows) and the frame shows the base (and overlay) there. */
static bool outside_disc(const globe_view_t *v, int x, int y)
{
    const float c = v->d.cfg.size / 2.0f, r = v->d.cfg.diam / 2.0f + 0.5f;
    const float dx = (float)x + 0.5f - c, dy = (float)y + 0.5f - c;
    return dx * dx + dy * dy >= r * r;
}

/* clean: under_name outside the disc, black inside (a face uses this or a base asset, not both) */
static bool build_under(globe_view_t *v)
{
    lv_image_dsc_t dsc;
    const int S = v->d.cfg.size;
    if (!assets_image(v->d.under_name, &dsc) || dsc.header.cf != LV_COLOR_FORMAT_RGB565 || v->x < 0 || v->y < 0 ||
        v->x + S > (int)dsc.header.w || v->y + S > (int)dsc.header.h)
        return false;
    const uint32_t stride = dsc.header.stride / 2;
    for (int y = 0; y < S; y++) {
        const uint16_t *src = (const uint16_t *)dsc.data + (size_t)(v->y + y) * stride + v->x;
        uint16_t *dst = v->clean + (size_t)y * S;
        for (int x = 0; x < S; x++) dst[x] = outside_disc(v, x, y) ? src[x] : 0;
    }
    return true;
}

/* ARGB8888 (flash) over true black -> RGB565, the renderer's own rule (alpha' = a + a>>7, truncating pack). */
static bool build_clean(globe_view_t *v)
{
    lv_image_dsc_t dsc;
    const uint16_t S = v->d.cfg.size;
    memset(v->clean, 0, (size_t)S * S * 2);
    if (v->d.under_name && !build_under(v)) return false;
    if (!v->d.base_name) return true;
    if (!assets_image(v->d.base_name, &dsc) || dsc.header.cf != LV_COLOR_FORMAT_ARGB8888 || dsc.header.w > S ||
        dsc.header.h > S)
        return false;
    const uint32_t stride = dsc.header.stride / 4;
    for (uint32_t y = 0; y < dsc.header.h; y++) {
        const uint32_t *src = (const uint32_t *)dsc.data + (size_t)y * stride;
        uint16_t *dst = v->clean + (size_t)y * S;
        for (uint32_t x = 0; x < dsc.header.w; x++) {
            const uint32_t c = src[x];
            const int a = (int)(c >> 24), ap = a + (a >> 7);
            dst[x] = pack565(((int)(c >> 16 & 255) * ap) >> 8, ((int)(c >> 8 & 255) * ap) >> 8, ((int)(c & 255) * ap) >> 8);
        }
    }
    return true;
}

static bool build_over(globe_view_t *v)
{
    lv_image_dsc_t dsc;
    if (!assets_image(v->d.over_name, &dsc) || dsc.header.cf != LV_COLOR_FORMAT_ARGB8888) return false;
    const uint32_t *argb = (const uint32_t *)dsc.data;
    uint16_t w = dsc.header.w > v->d.cfg.size ? v->d.cfg.size : dsc.header.w;
    uint16_t h = dsc.header.h > v->d.cfg.size ? v->d.cfg.size : dsc.header.h;
    uint16_t stride = (uint16_t)(dsc.header.stride / 4);
    uint32_t *tmp = NULL;   /* the overlay placed in the box and dropped outside the disc (under_name), transient */
    if (v->d.over_x || v->d.over_y || v->d.under_name) {
        const int S = v->d.cfg.size;
        tmp = heap_caps_calloc((size_t)S * S, 4, MALLOC_CAP_SPIRAM);
        if (!tmp) return false;
        psram_note(v);
        for (int y = 0; y < S; y++) {
            const int sy = y - v->d.over_y;
            if (sy < 0 || sy >= (int)dsc.header.h) continue;
            for (int x = 0; x < S; x++) {
                const int sx = x - v->d.over_x;
                if (sx < 0 || sx >= (int)dsc.header.w || (v->d.under_name && outside_disc(v, x, y))) continue;
                tmp[(size_t)y * S + x] = argb[(size_t)sy * stride + sx];
            }
        }
        argb = tmp;
        w = h = stride = (uint16_t)S;
    }
    const size_t n = omni_overlay_bytes(argb, w, h, stride);
    v->over_mem = ps_alloc(v, n);
    psram_note(v);
    const bool ok = v->over_mem && omni_overlay_build(&v->over, argb, w, h, stride, v->over_mem, n);
    heap_caps_free(tmp);
    return ok;
}

static bool quitting(globe_view_t *v)
{
    portENTER_CRITICAL(&s_mux);
    const bool q = v->quit;
    portEXIT_CRITICAL(&s_mux);
    return q;
}

/* LUT rows [py0, py1) in steps, yielding between them; false if asked to quit */
static bool lut_rows_steps(globe_view_t *v, int py0, int py1)
{
    for (int py = py0; py < py1; py += LUT_ROWS) {
        if (quitting(v)) return false;
        const int64_t t0 = esp_timer_get_time();
        omni_globe_lut_rows(&v->lut, py, py + LUT_ROWS < py1 ? py + LUT_ROWS : py1);
        const uint32_t us = us_since(t0);
        portENTER_CRITICAL(&s_mux);   /* both tasks run steps */
        if (us > v->step_max_us) v->step_max_us = us;
        portEXIT_CRITICAL(&s_mux);
        vTaskDelay(1);
    }
    return true;
}

/* Box rows [y0, y1) of frame o into out, from the compact LUT when the view has one; ref: the reference kernel. */
static void render_rows(globe_view_t *v, const omni_globe_frame_t *o, uint16_t *out, int y0, int y1, bool keep, bool ref,
                        const int *dirty)
{
    if (v->compact) {
        omni_globe_frame_t f = *o;
        f.over_run = ref ? NULL : globe_view_over_run;   /* the reference: C throughout */
        omni_globe_cspan_fn fn = ref ? omni_globe_cspan_ref : v->lin ? omni_globe_cspan_lin_fast : globe_view_ckernel;
        if (fn == globe_cspan_pie && (o->shade != v->shade || !v->shade_pie_ok)) fn = omni_globe_cspan_fast;
        omni_globe_render_c(&f, out, (uint16_t)y0, (uint16_t)y1, fn, keep, dirty);
    }
    else omni_globe_render(o, out, (uint16_t)y0, (uint16_t)y1, ref ? omni_globe_span_ref : globe_view_kernel);
}

static void render_chunks(globe_view_t *v, const omni_globe_frame_t *o, uint16_t *out, bool keep, const int *dirty)
{
    const int S = v->d.cfg.size;
    for (;;) {
        int y0;
        portENTER_CRITICAL(&s_mux);
        y0 = v->next_row;
        v->next_row = y0 + RENDER_CHUNK;
        portEXIT_CRITICAL(&s_mux);
        if (y0 >= S) return;
        render_rows(v, o, out, y0, y0 + RENDER_CHUNK < S ? y0 + RENDER_CHUNK : S, keep, false, dirty);
    }
}

/* The lobe move for box rows [y0, y1): old back to the ring (unless {0, 0}), new drawn ({0, 0}: none). */
static void lobe_rows(globe_view_t *v, const float old[2], const float dir[2], float strength, int y0, int y1)
{
    const uint16_t S = v->d.cfg.size;
    const float c = S / 2.0f, R = v->d.cfg.diam / 2.0f;
    if (old[0] != 0 || old[1] != 0) omni_globe_halo_unlobe(v->base, v->ring, S, c, c, R, old, strength, y0, y1);
    if (dir[0] != 0 || dir[1] != 0) omni_globe_halo_lobe(v->base, v->clean, S, c, c, R, dir, strength, y0, y1);
}

/* Hands the helper a job (it gives help_done when finished); the caller does its own share, then takes help_done. */
static void helper_run(globe_view_t *v, int what, const omni_globe_frame_t *f, uint16_t *out, int y0, int y1, bool keep,
                       const int *dirty)
{
    v->help_dirty = dirty;
    v->help_frame = f;
    v->help_out = out;
    v->help_y0 = y0;
    v->help_y1 = y1;
    v->help_keep = keep;
    portENTER_CRITICAL(&s_mux);
    v->help = what;
    v->help_started = false;
    portEXIT_CRITICAL(&s_mux);
    xTaskNotifyGive(v->helper);
}

static void helper(void *arg)
{
    globe_view_t *v = arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&s_mux);
        const int what = v->help;
        v->help = HELP_NONE;
        v->help_started = what != HELP_NONE;
        portEXIT_CRITICAL(&s_mux);
        if (what == HELP_QUIT) break;
        if (what == HELP_LUT) lut_rows_steps(v, v->help_y0, v->help_y1);
        else if (what == HELP_PACK) omni_globe_compact_spans(&v->clut, &v->lut, (unsigned)v->help_y0, (unsigned)v->help_y1);
        else if (what == HELP_RENDER) render_chunks(v, v->help_frame, v->help_out, v->help_keep, v->help_dirty);
        else if (what == HELP_LOBE) lobe_rows(v, v->help_old, v->help_dir, v->help_strength, v->help_y0, v->help_y1);
        else continue;
        xSemaphoreGive(v->help_done);
    }
    xSemaphoreGive(v->help_done);
    vTaskSuspend(NULL);   /* the worker's exit path waited for help_done; destroy deletes it */
}

/* A whole frame on both cores: each takes RENDER_CHUNK rows at a time until none are left, so a core LVGL's draw
 * threads hold up does fewer (a fixed half each waited for the slower core). Rows are independent. */
static void render_split(globe_view_t *v, const omni_globe_frame_t *o, uint16_t *out, bool keep, const int *dirty)
{
    v->next_row = 0;
    helper_run(v, HELP_RENDER, o, out, 0, 0, keep, dirty);
    render_chunks(v, o, out, keep, dirty);
    /* All rows are claimed. If the helper has not taken the job yet (LVGL's draw threads hold its core), withdraw it:
     * it will find nothing to do and give nothing, and o (on this stack) is not read after we return. */
    portENTER_CRITICAL(&s_mux);
    const bool withdrawn = !v->help_started && v->help == HELP_RENDER;
    if (withdrawn) v->help = HELP_NONE;
    portEXIT_CRITICAL(&s_mux);
    if (!withdrawn) xSemaphoreTake(v->help_done, portMAX_DELAY);
}

static bool build_steps(globe_view_t *v);

/* The build at BUILD_PRIO on both tasks: a face switch's LVGL redraws otherwise preempt it (spin's build varied
 * 470-630 ms with the helper's core busy drawing); there is no globe frame to show until it is done. */
static bool build(globe_view_t *v)
{
    vTaskPrioritySet(NULL, BUILD_PRIO);
    vTaskPrioritySet(v->helper, BUILD_PRIO);
    const bool ok = build_steps(v);
    vTaskPrioritySet(v->helper, WORKER_PRIO);
    vTaskPrioritySet(NULL, WORKER_PRIO);
    return ok;
}

static bool build_steps(globe_view_t *v)
{
    int64_t t0 = esp_timer_get_time();
    v->psram_low = UINT32_MAX;
    v->lut_bytes = omni_globe_lut_bytes(&v->d.cfg);   /* integer sizing (microseconds); begin repeats it */
    v->lut_mem = v->lut_bytes ? ps_alloc(v, v->lut_bytes) : NULL;
    if (!v->lut_mem) return false;
    if (!omni_globe_lut_begin(&v->lut, &v->d.cfg, v->lut_mem, v->lut_bytes)) return false;
    const uint32_t size_us = us_since(t0);
    const int64_t ts = esp_timer_get_time();
    const int half = v->d.cfg.diam / 2;
    helper_run(v, HELP_LUT, NULL, NULL, half, v->d.cfg.diam, false, NULL);
    const bool rows_ok = lut_rows_steps(v, 0, half);
    xSemaphoreTake(v->help_done, portMAX_DELAY);   /* the helper's half (it stops early on quit too) */
    const uint32_t rows_us = us_since(ts);
    if (!rows_ok || quitting(v)) return false;
    /* pack into the compact LUT and drop the planar one */
    const int64_t tc = esp_timer_get_time();
    if (omni_globe_compact_ok(&v->d.cfg)) {
        const size_t cb = omni_globe_compact_bytes(&v->lut);
        v->clut_mem = ps_alloc(v, cb);
        psram_note(v);   /* the planar and compact LUTs coexist until the pack is done */
        if (!v->clut_mem || !omni_globe_compact_begin(&v->clut, &v->lut, v->clut_mem, cb)) return false;
        const unsigned mid = v->lut.nspans / 2;   /* half the spans on each core */
        helper_run(v, HELP_PACK, NULL, NULL, (int)mid, v->lut.nspans, false, NULL);
        omni_globe_compact_spans(&v->clut, &v->lut, 0, mid);
        xSemaphoreTake(v->help_done, portMAX_DELAY);
        if (v->clut.bad_runs)
            ESP_LOGE(TAG, "globe[%s] %u spans' alpha-255 pixels are not one run: those spans take the C path",
                     v->d.name, (unsigned)v->clut.bad_runs);
        heap_caps_free(v->lut_mem);
        v->lut_mem = NULL;
        portENTER_CRITICAL(&s_mux);
        v->bytes -= (uint32_t)v->lut_bytes;
        portEXIT_CRITICAL(&s_mux);
        v->compact = true;
    }
    psram_note(v);   /* a planar view's peak: its LUT */
    const uint32_t pack_us = us_since(tc);
    const uint32_t lut_us = us_since(t0);
    int64_t t1 = esp_timer_get_time();
    size_t tex_size;
    const void *tex = assets_raw(GLOBE_TEX, &tex_size);
    if (!tex || tex_size != (size_t)v->tex.w * v->tex.h * 4) return false;
    memcpy(v->tex_px, tex, tex_size);   /* PSRAM: read in place from flash, spin's frame took 32.3 ms, not 26.9 (Task 1) */
    const uint32_t tex_us = us_since(t1);
    t1 = esp_timer_get_time();
    if (v->clean && !build_clean(v)) return false;
    const uint32_t base_us = us_since(t1);
    t1 = esp_timer_get_time();
    if (v->d.over_name && !build_over(v)) return false;
    v->has_over = v->d.over_name != NULL;
    const uint32_t over_us = us_since(t1);
    const uint32_t ms = us_since(t0) / 1000;
    portENTER_CRITICAL(&s_mux);
    v->build_ms = ms;
    portEXIT_CRITICAL(&s_mux);
    /* ESP_LOG on the worker's 4 KB stack: once, here (the per-frame logging is the LVGL side's) */
    psram_note(v);
    ESP_LOGI(TAG, "globe[%s] built in %u ms: LUT %u ms (begin %u, rows %u, step max %u.%u, %s %u), texture %u ms, "
             "base %u ms, overlay %u ms; %u bytes PSRAM, PSRAM free at the build's low %u", v->d.name, (unsigned)ms,
             (unsigned)(lut_us / 1000),
             (unsigned)(size_us / 1000), (unsigned)(rows_us / 1000), (unsigned)(v->step_max_us / 1000),
             (unsigned)(v->step_max_us / 100 % 10), v->compact ? "compact" : "planar",
             (unsigned)(pack_us / 1000), (unsigned)(tex_us / 1000), (unsigned)(base_us / 1000),
             (unsigned)(over_us / 1000), (unsigned)v->bytes, (unsigned)v->psram_low);
    return true;
}

static void shade_for(globe_view_t *v, float gain)
{
    if (gain == v->shade_gain) return;
    omni_globe_look_t look = v->d.look;
    look.gain = gain;
    omni_globe_shade_build(v->shade, &look);
    v->shade_gain = gain;
    v->shade_pie_ok = globe_pie_shade_ok(v->shade);
}

static void rect_union(int r[4], const int a[4])
{
    if (a[2] < a[0]) return;
    if (r[2] < r[0]) {
        memcpy(r, a, 4 * sizeof *r);
        return;
    }
    if (a[0] < r[0]) r[0] = a[0];
    if (a[1] < r[1]) r[1] = a[1];
    if (a[2] > r[2]) r[2] = a[2];
    if (a[3] > r[3]) r[3] = a[3];
}

/* The base under the globe for frame f (NULL = black): the clean base, plus the halo when f asks for one; rebuilt
 * only when the halo's key changes. The ring (no lobe) is kept per strength, so a new lobe direction costs only
 * the lobe's rect: restore the previous one from ring, draw the new one from clean (omni_globe_halo_lobe_rect:
 * bit-identical to a whole halo). Each change records its rect for the kept buffers. Returns the rebuild time in us
 * (0 if none). */
static uint32_t base_for(globe_view_t *v, const globe_view_frame_t *f, const uint16_t **under)
{
    const uint16_t S = v->d.cfg.size;
    if (!f->halo) {
        *under = v->clean;
        return 0;
    }
    if (v->base_halo && v->key_dir[0] == f->halo_dir[0] && v->key_dir[1] == f->halo_dir[1] &&
        v->key_strength == f->halo_strength) {
        *under = v->base;
        return 0;
    }
    if (!v->base) v->base = ps_alloc(v, (size_t)S * S * 2);
    if (!v->ring) v->ring = ps_alloc(v, (size_t)S * S * 2);
    if (!v->base || !v->ring) {
        *under = v->clean;
        return 0;
    }
    int64_t t0 = esp_timer_get_time();
    const float c = S / 2.0f, R = v->d.cfg.diam / 2.0f;
    const bool lobe = f->halo_dir[0] != 0 || f->halo_dir[1] != 0;
    int dirty[4] = { 0, 0, -1, -1 };
    if (v->ring_strength != f->halo_strength) {   /* also NAN: first time */
        if (v->clean) memcpy(v->ring, v->clean, (size_t)S * S * 2);
        else memset(v->ring, 0, (size_t)S * S * 2);
        omni_globe_halo(v->ring, S, c, c, R, NULL, f->halo_strength);
        memcpy(v->base, v->ring, (size_t)S * S * 2);
        v->ring_strength = f->halo_strength;
        v->lobe_rect[0] = v->lobe_rect[1] = 0;
        v->lobe_rect[2] = v->lobe_rect[3] = -1;
        const int all[4] = { 0, 0, S - 1, S - 1 };
        memcpy(dirty, all, sizeof dirty);
    }
    /* the old lobe's pixels back to the ring, the new one drawn: rows split across both cores */
    const float none[2] = { 0, 0 };
    const float *old = v->lobe_rect[2] >= v->lobe_rect[0] ? v->lobe_dir : none, *dir = lobe ? f->halo_dir : none;
    memcpy(v->help_old, old, sizeof v->help_old);
    memcpy(v->help_dir, dir, sizeof v->help_dir);
    v->help_strength = f->halo_strength;
    rect_union(dirty, v->lobe_rect);
    omni_globe_halo_lobe_rect(S, c, c, R, lobe ? f->halo_dir : NULL, f->halo_strength, v->lobe_rect);
    rect_union(dirty, v->lobe_rect);
    const int mid = dirty[3] >= dirty[1] ? (dirty[1] + dirty[3] + 1) / 2 : S / 2;   /* half the lobes' rows each */
    helper_run(v, HELP_LOBE, NULL, NULL, mid, S, false, NULL);
    lobe_rows(v, old, dir, f->halo_strength, 0, mid);
    xSemaphoreTake(v->help_done, portMAX_DELAY);
    v->lobe_dir[0] = f->halo_dir[0];
    v->lobe_dir[1] = f->halo_dir[1];
    v->base_halo = true;
    v->base_gen++;
    memcpy(v->gen_rect[v->base_gen & 1], dirty, sizeof dirty);
    v->key_dir[0] = f->halo_dir[0];
    v->key_dir[1] = f->halo_dir[1];
    v->key_strength = f->halo_strength;
    *under = v->base;
    const uint32_t us = us_since(t0);
    return us ? us : 1;
}

static void frame_setup(globe_view_t *v, const globe_view_frame_t *f, omni_globe_frame_t *o, uint32_t *base_us)
{
    shade_for(v, f->gain);
    const uint16_t *under;
    const uint32_t bu = base_for(v, f, &under);
    if (base_us) *base_us = bu;
    *o = (omni_globe_frame_t){ .lut = v->compact ? NULL : &v->lut, .clut = v->compact ? &v->clut : NULL,
                               .tex = &v->tex, .shade = v->shade, .sun = f->sun, .lon0_deg = f->lon0_deg,
                               .under = under, .over = v->has_over ? &v->over : NULL, .fade = f->fade };
}

static void render_frame(globe_view_t *v, const globe_view_frame_t *f, uint32_t seq)
{
    omni_globe_frame_t o;
    uint32_t base_us;
    frame_setup(v, f, &o, &base_us);
    const int bi = !v->front;   /* front only changes in poll, which waits for ready */
    uint16_t *out = (uint16_t *)v->buf[bi]->data;
    /* outside the disc a buffer keeps what its last frame drew there: the same base and overlay need no rewrite */
    const uint32_t gen = o.under == v->base ? v->base_gen : 0;
    bool keep = false;
    int dirty[4] = { 0, 0, -1, -1 };   /* what changed in the base since this buffer's last frame */
    if (v->compact && v->bkey[bi].valid && v->bkey[bi].under == o.under && f->fade == 255) {
        const uint32_t behind = gen - v->bkey[bi].gen;
        keep = behind <= 2;   /* two buffers: a buffer misses at most the other one's change and this one's */
        for (uint32_t g = 0; keep && g < behind; g++) rect_union(dirty, v->gen_rect[(gen - g) & 1]);
    }
    int64_t t0 = esp_timer_get_time();
    v->lin = f->still && v->compact;   /* read by both cores' render_rows; set before the helper is notified */
#if CONFIG_OMNI_DEBUG_CONSOLE
    const bool null_render = __atomic_load_n(&v->null_render, __ATOMIC_RELAXED);
#else
    const bool null_render = false;   /* a release build always renders */
#endif
    if (!null_render) render_split(v, &o, out, keep, dirty[2] >= dirty[0] ? dirty : NULL);
    v->lin = false;   /* the bench and selftest jobs time and check the motion kernel */
    const uint32_t us = us_since(t0);
    v->bkey[bi].under = o.under;
    v->bkey[bi].gen = gen;
    v->bkey[bi].valid = v->compact && f->fade == 255 && !null_render;   /* none: nothing was drawn */
    portENTER_CRITICAL(&s_mux);
    v->back_us = us;
    v->back_seq = seq;
    v->st_frames++;
    v->st_sum += us;
    if (us > v->st_max) v->st_max = us;
    if (base_us > v->st_base_max) v->st_base_max = base_us;
    v->ready = true;
    portEXIT_CRITICAL(&s_mux);
    disp_stats_frame_rendered();
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* Task 11 measurements: sequential PSRAM read / write / copy rates (KB/s), and the reference and active kernels' ns
 * per disc pixel cold (the whole frame on black, no overlay) and hot (a 4-row strip rendered 4 times: its LUT,
 * texture and rows then sit in the 64 KB cache). */
static uint32_t kbs(size_t bytes, uint32_t us) { return us ? (uint32_t)((uint64_t)bytes * 1000 / us) : 0; }

static void mem_bench(globe_view_t *v, const omni_globe_frame_t *o, uint16_t *out, job_res_t *r)
{
    const uint16_t S = v->d.cfg.size;
    const size_t box = (size_t)S * S * 2, tb = (size_t)v->tex.w * v->tex.h * 4;
    volatile uint32_t sink;
    uint32_t acc = 0;
    int64_t t0 = esp_timer_get_time();
    for (size_t i = 0; i < tb / 4; i += 4) acc += v->tex.px[i] + v->tex.px[i + 1] + v->tex.px[i + 2] + v->tex.px[i + 3];
    r->rd_kbs = kbs(tb, us_since(t0));
    sink = acc;
    (void)sink;
    t0 = esp_timer_get_time();
    memset(out, 0x11, box);
    r->wr_kbs = kbs(box, us_since(t0));
    t0 = esp_timer_get_time();
    memcpy(out, v->tex.px, box < tb ? box : tb);
    r->cp_kbs = kbs(box < tb ? box : tb, us_since(t0));
    uint32_t disc = 0, strip = 0;
    const unsigned ns = v->compact ? v->clut.nspans : v->lut.nspans;
    for (unsigned si = 0; si < ns; si++) {
        const int y = v->compact ? v->clut.span[si].y : v->lut.span[si].y;
        const uint32_t n = (uint32_t)(v->compact ? v->clut.span[si].n : v->lut.span[si].n);
        disc += n;
        if (y >= S / 2 - 2 && y < S / 2 + 2) strip += n;
    }
    r->disc_px = disc;
    omni_globe_frame_t p = *o;
    p.over = NULL;
    p.under = NULL;
    for (int k = 0; k < 2; k++) {
        t0 = esp_timer_get_time();
        render_rows(v, &p, out, 0, S, false, k == 0, NULL);
        r->cold_ns_px[k] = (uint32_t)((uint64_t)us_since(t0) * 1000 / disc);
        uint32_t best = UINT32_MAX;
        for (int i = 0; i < 4; i++) {
            t0 = esp_timer_get_time();
            render_rows(v, &p, out, S / 2 - 2, S / 2 + 2, false, k == 0, NULL);
            const uint32_t us = us_since(t0);
            if (us < best) best = us;
        }
        r->hot_ns_px[k] = (uint32_t)((uint64_t)best * 1000 / strip);
    }
}

typedef enum { B_REF, B_KERNEL, B_DUAL, B_KEEP } bmode_t;

static void time_frames(globe_view_t *v, const omni_globe_frame_t *o, uint16_t *out, int n, bmode_t m, uint32_t *avg,
                        uint32_t *mx)
{
    const uint16_t S = v->d.cfg.size;
    if (m == B_KEEP) render_split(v, o, out, false, NULL);   /* the buffer then holds this base and overlay */
    uint64_t sum = 0;
    *mx = 0;
    for (int i = 0; i < n; i++) {
        omni_globe_frame_t p = *o;
        p.lon0_deg += 1.5 * i;   /* a turning globe, as in a drag */
        const int64_t t0 = esp_timer_get_time();
        if (m == B_REF || m == B_KERNEL) render_rows(v, &p, out, 0, S, false, m == B_REF, NULL);
        else render_split(v, &p, out, m == B_KEEP && v->compact, NULL);
        const uint32_t us = us_since(t0);
        sum += us;
        if (us > *mx) *mx = us;
        vTaskDelay(1);
    }
    *avg = (uint32_t)(sum / (uint64_t)n);
}

static void job_bench(globe_view_t *v, const globe_view_frame_t *f, int n, job_res_t *r)
{
    const uint16_t S = v->d.cfg.size;
    uint16_t *out = heap_caps_malloc((size_t)S * S * 2, MALLOC_CAP_SPIRAM);
    if (!out) {
        snprintf(r->why, sizeof r->why, "no PSRAM for the bench buffer");
        return;
    }
    omni_globe_frame_t o;
    frame_setup(v, f, &o, NULL);
    time_frames(v, &o, out, n, B_REF, &r->ref_avg, &r->ref_max);
    time_frames(v, &o, out, n, B_KERNEL, &r->k_avg, &r->k_max);
    time_frames(v, &o, out, n, B_DUAL, &r->dual_avg, &r->dual_max);
    time_frames(v, &o, out, n, B_KEEP, &r->keep_avg, &r->keep_max);
    {
        omni_globe_frame_t p = o;
        p.over = NULL;
        uint32_t mx;
        time_frames(v, &p, out, n, B_KEEP, &r->keep_noover_avg, &mx);
        uint32_t px = 0;   /* overlay pixels on the disc's rows within the spans: recomposited every frame */
        if (o.over && v->compact)
            for (uint32_t i = 0; i < o.over->nspans; i++) {
                const omni_ospan_t *os = &o.over->span[i];
                for (unsigned si = 0; si < v->clut.nspans; si++) {
                    const omni_globe_cspan_t *cs = &v->clut.span[si];
                    if (cs->y != os->y) continue;
                    const int a = os->x0 > cs->x0 ? os->x0 : cs->x0;
                    const int b = os->x0 + os->n < cs->x0 + cs->n ? os->x0 + os->n : cs->x0 + cs->n;
                    if (b > a) px += (uint32_t)(b - a);
                }
            }
        r->over_disc_px = px;
    }
    /* the base rebuild a moving halo lobe costs (live re-bases on every animated frame), and the shade table a
     * changing gain costs */
    {
        uint64_t sum = 0;
        uint32_t mx = 0;
        const bool had_base = v->base != NULL;   /* analog's frames never ask for a halo: it has none */
        globe_view_frame_t g = *f;
        g.halo = true;
        if (g.halo_strength == 0) g.halo_strength = 0.9f;
        for (int i = 0; i < n; i++) {
            g.halo_dir[0] = 0.5f + 0.01f * (float)i;   /* a new key every time */
            g.halo_dir[1] = -0.3f;
            const uint16_t *under;
            const uint32_t us = base_for(v, &g, &under);
            sum += us;
            if (us > mx) mx = us;
            vTaskDelay(1);
        }
        r->base_avg = (uint32_t)(sum / (uint64_t)n);
        r->base_max = mx;
        v->base_halo = false;   /* the next real frame rebuilds its own */
        if (!had_base) {   /* m4: the buffers the bench forced are not kept for the view's life */
            /* N4: base_for keeps a lone base when ring fails, so count each buffer that exists */
            const uint32_t freed = (uint32_t)S * S * 2 * ((v->base != NULL) + (v->ring != NULL));
            heap_caps_free(v->base);
            heap_caps_free(v->ring);
            v->base = v->ring = NULL;
            v->ring_strength = NAN;
            v->lobe_rect[0] = v->lobe_rect[1] = 0;
            v->lobe_rect[2] = v->lobe_rect[3] = -1;
            v->lobe_dir[0] = v->lobe_dir[1] = 0;
            portENTER_CRITICAL(&s_mux);
            v->bytes -= freed;
            portEXIT_CRITICAL(&s_mux);
        }
    }
    {
        uint64_t sum = 0;
        uint32_t mx = 0;
        for (int i = 0; i < n; i++) {
            int64_t t0 = esp_timer_get_time();
            shade_for(v, 0.5f + 0.01f * (float)i);
            const uint32_t us = us_since(t0);
            sum += us;
            if (us > mx) mx = us;
            vTaskDelay(1);
        }
        r->shade_avg = (uint32_t)(sum / (uint64_t)n);
        r->shade_max = mx;
    }
    mem_bench(v, &o, out, r);
    heap_caps_free(out);
    r->ok = true;
}

static void job_selftest(globe_view_t *v, const globe_view_frame_t *f, job_res_t *r)
{
    static const double LON[5] = { -179.9, -60, 0, 60, 179.9 };
    const uint16_t S = v->d.cfg.size;
    const size_t box = (size_t)S * S * 2;
    const size_t sb = omni_globe_synth_bytes(), cb = omni_globe_csynth_bytes(), synth = sb > cb ? sb : cb;
    uint16_t *a = heap_caps_malloc(box, MALLOC_CAP_SPIRAM), *b = heap_caps_malloc(box, MALLOC_CAP_SPIRAM);
    uint16_t *ks = heap_caps_malloc((size_t)2 * (S + OMNI_KCHECK_GUARD) * 2, MALLOC_CAP_SPIRAM);
    void *sm = heap_caps_malloc(synth, MALLOC_CAP_SPIRAM);
    if (!a || !b || !ks || !sm) {
        snprintf(r->why, sizeof r->why, "no PSRAM for the selftest buffers");
        goto out;
    }
    r->pass = 0;
    for (int i = 0; i < 5; i++) {
        globe_view_frame_t g = *f;
        g.lon0_deg = LON[i];
        omni_globe_frame_t o;
        frame_setup(v, &g, &o, NULL);
        char tag[24];
        snprintf(tag, sizeof tag, "lon0 %.1f", LON[i]);
        memset(a, 0x5a, box);
        render_rows(v, &o, a, 0, S, false, true, NULL);   /* the reference, whole */
        memset(b, 0xa5, box);
        render_rows(v, &o, b, 0, S / 2, false, false, NULL);
        render_rows(v, &o, b, S / 2, S, false, false, NULL);
        if (memcmp(a, b, box) != 0) {
            snprintf(r->why, sizeof r->why, "%s: ref whole vs kernel halves differ", tag);
            continue;
        }
        memset(b, 0xa5, box);
        render_split(v, &o, b, false, NULL);   /* the frame path: both cores */
        if (memcmp(a, b, box) != 0) {
            snprintf(r->why, sizeof r->why, "%s: ref whole vs the dual-core frame differ", tag);
            continue;
        }
        if (v->compact) {   /* keep: over a buffer last drawn for another lon0 */
            omni_globe_frame_t p = o;
            p.lon0_deg += 41;
            render_split(v, &p, b, false, NULL);
            render_split(v, &o, b, o.fade == 255, NULL);
            if (memcmp(a, b, box) != 0) {
                snprintf(r->why, sizeof r->why, "%s: ref whole vs the kept dual-core frame differ", tag);
                continue;
            }
        }
        vTaskDelay(1);
        const bool exact = v->compact
                               ? omni_globe_ckernel_bitexact(globe_view_ckernel, &o, ks, tag, r->why, sizeof r->why)
                               : omni_globe_kernel_bitexact(globe_view_kernel, &o, omni_globe_uoff_q4(v->tex.w, LON[i]),
                                                            ks, tag, r->why, sizeof r->why);
        if (!exact) continue;
        r->pass++;
    }
    r->synth_ref = omni_globe_synth_digest(omni_globe_span_ref, sm, synth);
    r->synth_k = omni_globe_synth_digest(globe_view_kernel, sm, synth);
    r->csynth_ref = omni_globe_csynth_digest(omni_globe_cspan_ref, sm, synth);
    r->csynth_k = omni_globe_csynth_digest(globe_view_ckernel, sm, synth);
    r->lut_digest = v->compact ? omni_globe_clut_digest(&v->clut) : omni_globe_lut_digest(&v->lut);
    r->shade_digest = omni_globe_shade_digest(v->shade);   /* for the gain of the last frame above (f->gain) */
    r->ok = r->pass == 5 && r->synth_ref == OMNI_GLOBE_SYNTH_DIGEST && r->synth_k == OMNI_GLOBE_SYNTH_DIGEST &&
            r->csynth_ref == OMNI_GLOBE_CSYNTH_DIGEST && r->csynth_k == OMNI_GLOBE_CSYNTH_DIGEST;
    if (r->pass == 5 && !r->ok) snprintf(r->why, sizeof r->why, "synthetic digest differs");
out:
    heap_caps_free(a);
    heap_caps_free(b);
    heap_caps_free(ks);
    heap_caps_free(sm);
}
#endif /* CONFIG_OMNI_DEBUG_CONSOLE */

static void worker(void *arg)
{
    globe_view_t *v = arg;
    if (v->pm) esp_pm_lock_acquire(v->pm);
    const bool ok = build(v);
    portENTER_CRITICAL(&s_mux);
    v->built = ok;
    v->failed = !ok;   /* terminal: the face stops polling (globe_view_failed) */
    portEXIT_CRITICAL(&s_mux);
    if (!ok && !quitting(v))
        ESP_LOGE(TAG, "globe[%s] build failed (PSRAM free %u, largest %u); the view stays blank", v->d.name,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        const bool quit = v->quit;
        const job_t job = v->job;
        const bool take = ok && !job && v->has_pending && !v->ready;
        globe_view_frame_t f = v->pending, last = v->last;
        const uint32_t seq = v->pending_seq;
        const int n = v->job_n;
        if (take) {
            v->has_pending = false;
            if (!isnan(v->hold)) f.lon0_deg = v->hold;
        }
        if (!isnan(v->hold)) last.lon0_deg = v->hold;
        portEXIT_CRITICAL(&s_mux);
        if (quit) break;
        if (job) {
            job_res_t r = { 0 };
            if (!ok) snprintf(r.why, sizeof r.why, "view not built");
#if CONFIG_OMNI_DEBUG_CONSOLE
            else if (job == JOB_BENCH) job_bench(v, &last, n, &r);
            else job_selftest(v, &last, &r);
#else
            (void)last;
            (void)n;
#endif
            portENTER_CRITICAL(&s_mux);
            v->res = r;
            v->job = JOB_NONE;
            portEXIT_CRITICAL(&s_mux);
            xSemaphoreGive(v->job_done);
            continue;
        }
        if (take) {
            render_frame(v, &f, seq);
            continue;
        }
        if (v->pm) esp_pm_lock_release(v->pm);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (v->pm) esp_pm_lock_acquire(v->pm);
    }
    if (v->pm) esp_pm_lock_release(v->pm);
    helper_run(v, HELP_QUIT, NULL, NULL, 0, 0, false, NULL);
    xSemaphoreTake(v->help_done, portMAX_DELAY);
    xSemaphoreGive(v->exited);
    vTaskSuspend(NULL);   /* globe_view_destroy deletes it (vTaskDeleteWithCaps) */
}

/* ---- LVGL context */

/* The box is one opaque RGB565 image (no alpha, no transform, no style opacity), so it covers its area: LVGL then
 * starts each redraw at the image and never draws the face beneath it (disp_image_cover; without it every globe frame
 * redrew the whole face under the box, about 90 ms of the 120 ms LVGL spent per frame). LVGL 9.5.0 still has no public
 * way (Task 1 step 5): lv_image never raises COVER, and an opaque bg on the image (which passes the base check) adds a
 * fill under every redraw: spin 22.6 -> 21.5 fps. */

static void img_deleted(lv_event_t *e)   /* the parent went first (a screen cleaned under the view) */
{
    globe_view_t *v = lv_event_get_user_data(e);
    v->img = NULL;
}

static void view_free(globe_view_t *v)
{
    if (v->img) {
        lv_obj_remove_event_cb_with_user_data(v->img, img_deleted, v);
        disp_image_uncover(v->img);
        lv_obj_delete(v->img);
    }
    theme_buf_destroy(v->buf[0]);
    theme_buf_destroy(v->buf[1]);
    heap_caps_free(v->lut_mem);
    heap_caps_free(v->clut_mem);
    heap_caps_free(v->tex_px);
    heap_caps_free(v->shade);
    heap_caps_free(v->clean);
    heap_caps_free(v->base);
    heap_caps_free(v->ring);
    heap_caps_free(v->over_mem);
    if (v->job_done) vSemaphoreDeleteWithCaps(v->job_done);
    if (v->exited) vSemaphoreDeleteWithCaps(v->exited);
    if (v->help_done) vSemaphoreDeleteWithCaps(v->help_done);
    if (v->pm) esp_pm_lock_delete(v->pm);
    heap_caps_free(v);
}

globe_view_t *globe_view_create(lv_obj_t *parent, int32_t x, int32_t y, const globe_view_desc_t *d)
{
    lv_image_dsc_t probe;
    if (!assets_ready() || !assets_raw(GLOBE_TEX, NULL) || (d->base_name && !assets_image(d->base_name, &probe)) ||
        (d->over_name && !assets_image(d->over_name, &probe)) ||
        (d->under_name && !assets_image(d->under_name, &probe)))
        return NULL;
    globe_view_t *v = heap_caps_calloc(1, sizeof *v, MALLOC_CAP_SPIRAM);
    if (!v) return NULL;
    v->bytes = sizeof *v;
    v->d = *d;
    v->x = x;
    v->y = y;
    v->d.cfg.tex_w = ASSET_GLOBE_TEX_W;
    v->tex = (omni_globe_tex_t){ ASSET_GLOBE_TEX_W, ASSET_GLOBE_TEX_H, NULL };
    v->shade_gain = NAN;
    v->ring_strength = NAN;
    v->hold = NAN;
    const uint16_t S = v->d.cfg.size;
    bool ok = true;   /* the LUT (about 0.7-1.1 MB) is sized and allocated by the worker */
    if (ok) v->tex_px = ps_alloc(v, (size_t)v->tex.w * v->tex.h * 4);
    if (ok) v->shade = ps_alloc(v, sizeof *v->shade);
    if (ok && (d->base_name || d->under_name)) v->clean = ps_alloc(v, (size_t)S * S * 2);
    for (int i = 0; i < 2 && ok; i++) {
        v->buf[i] = theme_buf_create(S, S, LV_COLOR_FORMAT_RGB565);
        if (v->buf[i] && v->buf[i]->header.stride != (uint32_t)S * 2) {   /* omni_globe_render writes rows of S px */
            ESP_LOGE(TAG, "globe[%s] buffer stride %u, not %u", d->name, (unsigned)v->buf[i]->header.stride,
                     (unsigned)S * 2);
            ok = false;
        }
        if (v->buf[i]) {
            memset(v->buf[i]->data, 0, v->buf[i]->data_size);
            v->bytes += v->buf[i]->data_size;
        }
    }
    ok = ok && v->tex_px && v->shade && (!(d->base_name || d->under_name) || v->clean) && v->buf[0] && v->buf[1];
    if (ok && d->under_name) {   /* until the first frame the box shows the background, not a black square */
        ok = build_under(v);
        for (int i = 0; i < 2 && ok; i++) memcpy(v->buf[i]->data, v->clean, (size_t)S * S * 2);
    }
    v->tex.px = v->tex_px;
    if (ok) {
        v->job_done = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
        v->exited = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
        v->help_done = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
        ok = v->job_done && v->exited && v->help_done;
        if (ok && esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "globe", &v->pm) != ESP_OK) v->pm = NULL;
    }
    if ((x | y) & 1)   /* the panel's rounder widens every redraw to even bounds: the box then never covers its area */
        ESP_LOGW(TAG, "globe[%s] box origin %d,%d is odd: every frame also redraws what is beneath the box", d->name,
                 (int)x, (int)y);
    if (ok) {
        v->img = lv_image_create(parent);
        lv_obj_remove_flag(v->img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(v->img, x, y);
        lv_image_set_src(v->img, v->buf[0]);
        lv_obj_add_event_cb(v->img, img_deleted, LV_EVENT_DELETE, v);
        disp_image_cover(v->img);
        ok = xTaskCreatePinnedToCoreWithCaps(helper, "globe2", HELPER_STACK, v, WORKER_PRIO, &v->helper, HELPER_CORE,
                                             MALLOC_CAP_SPIRAM) == pdPASS;
        ok = ok && xTaskCreatePinnedToCoreWithCaps(worker, "globe", WORKER_STACK, v, WORKER_PRIO, &v->task, WORKER_CORE,
                                                   MALLOC_CAP_SPIRAM) == pdPASS;
        if (!ok && v->helper) {   /* the worker never started: stop the helper here */
            helper_run(v, HELP_QUIT, NULL, NULL, 0, 0, false, NULL);
            xSemaphoreTake(v->help_done, portMAX_DELAY);
            vTaskDeleteWithCaps(v->helper);
            v->helper = NULL;
        }
    }
    if (!ok) {
        ESP_LOGE(TAG, "globe[%s] not created (PSRAM free %u)", d->name,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        view_free(v);
        return NULL;
    }
    s_current = v;
    return v;
}

void globe_view_destroy(globe_view_t *v)
{
    if (!v) return;
    portENTER_CRITICAL(&s_mux);
    v->quit = true;
    portEXIT_CRITICAL(&s_mux);
    xTaskNotifyGive(v->task);
    xSemaphoreTake(v->exited, portMAX_DELAY);   /* at most the rest of a build or a frame: it never waits on LVGL */
    vTaskDeleteWithCaps(v->task);
    vTaskDeleteWithCaps(v->helper);   /* suspended: the worker waited for it to finish */
    if (s_current == v) s_current = NULL;
    view_free(v);
}

bool globe_view_failed(const globe_view_t *v)
{
    portENTER_CRITICAL(&s_mux);
    const bool b = v->failed;
    portEXIT_CRITICAL(&s_mux);
    return b;
}

bool globe_view_ready(const globe_view_t *v)
{
    portENTER_CRITICAL(&s_mux);
    const bool b = v->built;
    portEXIT_CRITICAL(&s_mux);
    return b;
}

uint32_t globe_view_submit(globe_view_t *v, const globe_view_frame_t *f_in)
{
    globe_view_frame_t fr = *f_in;
    const globe_view_frame_t *f = &fr;
    if (!isfinite(fr.lon0_deg)) {   /* never converted (omni_globe_uoff_q4's float-to-int would be UB): a face bug */
        if (!v->bad_lon0_logged) ESP_LOGE(TAG, "globe[%s] non-finite lon0 submitted; drawn at 0", v->d.name);
        v->bad_lon0_logged = true;
        fr.lon0_deg = 0;
    }
    if (++v->seq == 0) v->seq = 1;   /* 0 is "none" (a wrap every 4.5 years at 30 fps) */
    portENTER_CRITICAL(&s_mux);
    const bool wake = !v->ready && !v->job;
    v->pending = *f;
    v->pending_seq = v->seq;
    v->has_pending = true;
    v->last = *f;
    v->last_seq = v->seq;
    v->has_last = true;
    portEXIT_CRITICAL(&s_mux);
    if (wake) xTaskNotifyGive(v->task);
    return v->seq;
}

static void log_window(globe_view_t *v, int64_t until)
{
    if (v->win_frames == 0) return;
    const float s = (float)(until - v->win_t0) / 1e6f;
    ESP_LOGI(TAG, "globe[%s] %.1f s: %u frames %.1f fps, render avg %.1f ms max %.1f ms", v->d.name, s,
             (unsigned)v->win_frames, s > 0 ? v->win_frames / s : 0.0f, v->win_sum / 1000.0f / v->win_frames,
             v->win_max / 1000.0f);
}

bool globe_view_poll(globe_view_t *v)
{
    portENTER_CRITICAL(&s_mux);
    const bool ready = v->ready;
    const uint32_t us = v->back_us, seq = v->back_seq;
    const bool stale = ready && (int32_t)(seq - v->keep_from) < 0;   /* rendered for before a reset */
    if (stale) v->ready = false;
    const bool resume = stale && v->has_pending;
    portEXIT_CRITICAL(&s_mux);
    if (resume) xTaskNotifyGive(v->task);
    if (stale) v->dropped++;
    disp_swap_poll(&v->swap);
    if (!ready || stale) return false;
    v->front = !v->front;
    v->shown_seq = seq;
    if (v->img) {
        lv_image_set_src(v->img, v->buf[v->front]);
        lv_obj_invalidate(v->img);
    }
    disp_swap_did(&v->swap);
    portENTER_CRITICAL(&s_mux);
    v->ready = false;
    const bool more = v->has_pending;
    portEXIT_CRITICAL(&s_mux);
    if (more) xTaskNotifyGive(v->task);

    const int64_t now = esp_timer_get_time();
    if (v->last_swap == 0 || now - v->last_swap > REST_GAP_US) {
        log_window(v, v->last_swap);
        ESP_LOGI(TAG, "globe[%s] rest frame: render %.1f ms", v->d.name, us / 1000.0f);
        v->win_t0 = now;
        v->win_frames = 0;
        v->win_sum = 0;
        v->win_max = 0;
    } else {
        v->win_frames++;
        v->win_sum += us;
        if (us > v->win_max) v->win_max = us;
        if (now - v->win_t0 >= LOG_WINDOW_US) {
            log_window(v, now);
            v->win_t0 = now;
            v->win_frames = 0;
            v->win_sum = 0;
            v->win_max = 0;
        }
    }
    v->last_swap = now;
    return true;
}

uint32_t globe_view_shown(const globe_view_t *v) { return v->shown_seq; }

void globe_view_reset(globe_view_t *v, globe_view_blank_t look)
{
    v->keep_from = v->seq + 1;   /* a frame in flight is still rendered, then dropped by poll */
    v->resets++;
    portENTER_CRITICAL(&s_mux);
    if (v->has_pending) v->dropped++;
    v->has_pending = false;      /* one not taken yet is dropped here */
    const bool built = v->built;
    portEXIT_CRITICAL(&s_mux);
    if (!built) return;          /* nothing was shown yet: the box holds its initial look */
    /* The worker writes only buf[!front], and clean and the overlay are fixed once built. */
    const int64_t t0 = esp_timer_get_time();
    const uint16_t S = v->d.cfg.size;
    uint16_t *box = (uint16_t *)v->buf[v->front]->data;
    if (look == GLOBE_BLANK_EMPTY && v->clean) memcpy(box, v->clean, (size_t)S * S * 2);
    else memset(box, 0, (size_t)S * S * 2);
    if (look == GLOBE_BLANK_EMPTY && v->has_over) omni_overlay_draw(&v->over, box, S);
    v->bkey[v->front].valid = false;   /* no longer what a kept frame expects outside the disc */
    if (v->img) lv_obj_invalidate(v->img);
    ESP_LOGI(TAG, "globe[%s] reset to %s in %u us (frames dropped so far %u)", v->d.name,
             look == GLOBE_BLANK_EMPTY ? "empty" : "black", (unsigned)us_since(t0), (unsigned)v->dropped);
}

const omni_globe_cfg_t *globe_view_cfg(const globe_view_t *v) { return &v->d.cfg; }
lv_obj_t *globe_view_obj(globe_view_t *v) { return v->img; }
const char *globe_view_name(const globe_view_t *v) { return v->d.name; }
unsigned globe_view_worker_stack_free(const globe_view_t *v) { return (unsigned)uxTaskGetStackHighWaterMark(v->task); }
unsigned globe_view_helper_stack_free(const globe_view_t *v) { return (unsigned)uxTaskGetStackHighWaterMark(v->helper); }
globe_view_t *globe_view_current(void) { return s_current; }

void globe_view_stats(globe_view_t *v, globe_view_stats_t *s)
{
    portENTER_CRITICAL(&s_mux);
    s->frames = v->st_frames;
    s->render_us_avg = v->st_frames ? (uint32_t)(v->st_sum / v->st_frames) : 0;
    s->render_us_max = v->st_max;
    s->base_us_max = v->st_base_max;
    s->build_ms = v->build_ms;
    s->bytes = v->bytes;
    portEXIT_CRITICAL(&s_mux);
    s->resets = v->resets;   /* LVGL context, as the caller */
    s->dropped = v->dropped;
    s->unseen = v->swap.unseen;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
void globe_view_null_render(globe_view_t *v, bool on)
{
    if (v) __atomic_store_n(&v->null_render, on, __ATOMIC_RELAXED);
}
#endif

void globe_view_hold(globe_view_t *v, double lon_deg)
{
    portENTER_CRITICAL(&s_mux);
    v->hold = lon_deg;
    const bool resubmit = v->has_last && !v->has_pending;
    if (resubmit) {
        v->pending = v->last;   /* show the held longitude now, even at rest */
        v->pending_seq = v->last_seq;
        v->has_pending = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (resubmit) xTaskNotifyGive(v->task);
}

/* ---- Console jobs: run on the worker; the caller holds the LVGL lock, so nothing submits meanwhile. */
#if CONFIG_OMNI_DEBUG_CONSOLE

static bool run_job(globe_view_t *v, job_t job, int n, job_res_t *r)
{
    portENTER_CRITICAL(&s_mux);
    const bool have = v->has_last;
    if (have) {
        v->job = job;
        v->job_n = n;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!have) {
        snprintf(r->why, sizeof r->why, "no frame submitted yet");
        return false;
    }
    xSemaphoreTake(v->job_done, 0);
    xTaskNotifyGive(v->task);
    if (xSemaphoreTake(v->job_done, pdMS_TO_TICKS(JOB_TIMEOUT_MS)) != pdTRUE) {
        snprintf(r->why, sizeof r->why, "worker did not answer");
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    *r = v->res;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void globe_view_bench(globe_view_t *v, int n)
{
    job_res_t r = { 0 };
    if (!run_job(v, JOB_BENCH, n, &r) || !r.ok) {
        printf("BENCH %s failed: %s\n", v->d.name, r.why);
        return;
    }
    const char *lut = v->compact ? "compact" : "planar";
    printf("BENCH %s %s ref avg %u max %u (one core)\n", v->d.name, lut, (unsigned)r.ref_avg, (unsigned)r.ref_max);
    printf("BENCH %s %s kernel avg %u max %u (one core)\n", v->d.name, lut, (unsigned)r.k_avg, (unsigned)r.k_max);
    printf("BENCH %s dual-core avg %u max %u\n", v->d.name, (unsigned)r.dual_avg, (unsigned)r.dual_max);
    printf("BENCH %s dual-core kept avg %u max %u (the frame cost: same base and overlay)\n", v->d.name,
           (unsigned)r.keep_avg, (unsigned)r.keep_max);
    printf("BENCH %s dual-core kept without the overlay avg %u (overlay pixels over the disc: %u)\n", v->d.name,
           (unsigned)r.keep_noover_avg, (unsigned)r.over_disc_px);
    printf("BENCH %s base+halo avg %u max %u\n", v->d.name, (unsigned)r.base_avg, (unsigned)r.base_max);
    printf("BENCH %s shade avg %u max %u\n", v->d.name, (unsigned)r.shade_avg, (unsigned)r.shade_max);
    printf("BENCH %s mem: read %u KB/s, write %u KB/s, copy %u KB/s; ns/px cold/hot ref %u/%u kernel %u/%u "
           "(%u disc px)\n", v->d.name, (unsigned)r.rd_kbs, (unsigned)r.wr_kbs, (unsigned)r.cp_kbs,
           (unsigned)r.cold_ns_px[0], (unsigned)r.hot_ns_px[0], (unsigned)r.cold_ns_px[1], (unsigned)r.hot_ns_px[1],
           (unsigned)r.disc_px);
}

bool globe_view_selftest(globe_view_t *v)
{
    job_res_t r = { 0 };
    const bool ran = run_job(v, JOB_SELFTEST, 5, &r);
    if (ran)
        printf("GLOBE synth %016llx %016llx (pinned %016llx); compact synth %016llx %016llx (pinned %016llx); "
               "%s lut %016llx shade %016llx\n",
               (unsigned long long)r.synth_ref, (unsigned long long)r.synth_k,
               (unsigned long long)OMNI_GLOBE_SYNTH_DIGEST, (unsigned long long)r.csynth_ref,
               (unsigned long long)r.csynth_k, (unsigned long long)OMNI_GLOBE_CSYNTH_DIGEST,
               v->compact ? "compact" : "planar", (unsigned long long)r.lut_digest, (unsigned long long)r.shade_digest);
    if (ran && r.ok) {
        printf("GLOBE SELFTEST PASS 5/5\n");
        return true;
    }
    printf("GLOBE SELFTEST FAIL %d/5: %s\n", r.pass, r.why);
    return false;
}
#endif /* CONFIG_OMNI_DEBUG_CONSOLE */
