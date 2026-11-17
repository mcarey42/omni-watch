#include "disp_stats.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "core/lv_obj_event_private.h"   /* lv_cover_check_info_t: LVGL has no public setter that can raise to COVER */
#include "theme.h"
#include "disp_direct.h"
#include "omni/omni_dial.h"

/* Decided by Stage 3 Task 7 with the dial compositor (disp_stats.h): IMAGE. */
const disp_path_t DISP_PATH_FULLSCREEN = DISP_PATH_IMAGE;

/* Event-side state: LVGL task only, except the two frame counters (workers) and the copy out (any task). */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static disp_stats_t s_win;
static uint64_t s_render_sum;
static int64_t s_render_t0, s_flush_t0, s_wait_t0;
static bool s_rendered;              /* this refresh rendered something (RENDER_START seen) */
static volatile uint32_t s_refr_done;

static void on_event(lv_event_t *e)   /* LVGL task */
{
    const int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_RENDER_START:
        s_render_t0 = now;
        s_rendered = true;
        break;
    case LV_EVENT_RENDER_READY:
        if (s_render_t0) {
            const uint32_t us = (uint32_t)(now - s_render_t0);
            taskENTER_CRITICAL(&s_lock);
            s_win.refr++;
            s_render_sum += us;
            if (us > s_win.lv_render_us_max) s_win.lv_render_us_max = us;
            taskEXIT_CRITICAL(&s_lock);
            s_render_t0 = 0;
        }
        break;
    case LV_EVENT_FLUSH_START: {
        const lv_area_t *a = lv_event_get_param(e);
        s_flush_t0 = now;
        taskENTER_CRITICAL(&s_lock);
        s_win.flushes++;
        if (a) s_win.flush_px += (uint32_t)lv_area_get_size(a);
        taskEXIT_CRITICAL(&s_lock);
        break;
    }
    case LV_EVENT_FLUSH_FINISH:
        if (s_flush_t0) {
            const uint32_t us = (uint32_t)(now - s_flush_t0);
            taskENTER_CRITICAL(&s_lock);
            if (us > s_win.flush_us_max) s_win.flush_us_max = us;
            taskEXIT_CRITICAL(&s_lock);
            s_flush_t0 = 0;
        }
        break;
    case LV_EVENT_FLUSH_WAIT_START:
        s_wait_t0 = now;
        break;
    case LV_EVENT_FLUSH_WAIT_FINISH:
        if (s_wait_t0) {
            taskENTER_CRITICAL(&s_lock);
            s_win.flush_wait_us += (uint32_t)(now - s_wait_t0);
            taskEXIT_CRITICAL(&s_lock);
            s_wait_t0 = 0;
        }
        break;
    case LV_EVENT_REFR_READY:
        if (s_rendered) s_refr_done++;
        s_rendered = false;
        break;
    default:
        break;
    }
}

void disp_stats_init(lv_display_t *disp)
{
    static const lv_event_code_t CODES[] = {
        LV_EVENT_RENDER_START, LV_EVENT_RENDER_READY, LV_EVENT_FLUSH_START, LV_EVENT_FLUSH_FINISH,
        LV_EVENT_FLUSH_WAIT_START, LV_EVENT_FLUSH_WAIT_FINISH, LV_EVENT_REFR_READY,
    };
    if (disp == NULL) return;
    for (size_t i = 0; i < sizeof CODES / sizeof CODES[0]; i++) lv_display_add_event_cb(disp, on_event, CODES[i], NULL);
}

uint32_t disp_stats_refr_done(void) { return s_refr_done; }

void disp_stats_get(disp_stats_t *s, bool reset)
{
    taskENTER_CRITICAL(&s_lock);
    *s = s_win;
    s->lv_render_us_avg = s_win.refr ? (uint32_t)(s_render_sum / s_win.refr) : 0;
    if (reset) {
        memset(&s_win, 0, sizeof s_win);
        s_render_sum = 0;
    }
    taskEXIT_CRITICAL(&s_lock);
}

void disp_stats_frame_rendered(void)
{
    taskENTER_CRITICAL(&s_lock);
    s_win.frames_rendered++;
    taskEXIT_CRITICAL(&s_lock);
}

void disp_stats_frame_shown(void)
{
    taskENTER_CRITICAL(&s_lock);
    s_win.frames_shown++;
    taskEXIT_CRITICAL(&s_lock);
}

/* ---- Swap bookkeeping (LVGL context) */

void disp_swap_poll(disp_swap_t *s)
{
    if (s->await && disp_stats_refr_done() != s->refr) {
        s->await = false;
        disp_stats_frame_shown();
    }
}

void disp_swap_did(disp_swap_t *s)
{
    if (s->await) s->unseen++;   /* swapped out before any refresh drew it */
    s->await = true;
    s->refr = disp_stats_refr_done();
}

/* ---- Cover */

/* LVGL 9.5's base lv_obj check marks any object with a transparent background NOT_COVER, and lv_image never upgrades
 * that (lv_image.c draw_image, COVER_CHECK: it only ever lowers the result). So an opaque image without this redraws
 * everything beneath it on every frame. Runs after the class's own check, which still decides MASKED. */
static void image_cover(lv_event_t *e)
{
    lv_cover_check_info_t *info = lv_event_get_param(e);
    lv_obj_t *obj = lv_event_get_current_target(e);
    if (info->res == LV_COVER_RES_MASKED) return;
    /* the image class's own conditions, which this would otherwise override (a later fade by image_opa, a transform) */
    if (lv_obj_get_style_image_opa(obj, LV_PART_MAIN) != LV_OPA_COVER || lv_obj_get_style_opa(obj, LV_PART_MAIN) <
        LV_OPA_MAX || (lv_obj_check_type(obj, &lv_image_class) &&
                       (lv_image_get_rotation(obj) != 0 || lv_image_get_scale(obj) != LV_SCALE_NONE)))
        return;
    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    const lv_area_t *a = info->area;
    if (a->x1 >= c.x1 && a->y1 >= c.y1 && a->x2 <= c.x2 && a->y2 <= c.y2) info->res = LV_COVER_RES_COVER;
}

void disp_image_cover(lv_obj_t *img) { lv_obj_add_event_cb(img, image_cover, LV_EVENT_COVER_CHECK, NULL); }
void disp_image_uncover(lv_obj_t *img) { lv_obj_remove_event_cb(img, image_cover); }

/* ---- `disp bench` */
#if CONFIG_OMNI_DEBUG_CONSOLE
static const char *TAG = "disp";

#define BENCH_W_FULL 466
#define BENCH_BOX 350
#define BENCH_BOX_X 58
#define BENCH_BOX_Y 46
#define BENCH_STACK 3072
#define BENCH_PRIO 2      /* as the globe workers: under LVGL */
#define BENCH_CORE 1
#define BENCH_POLL_MS 2
#define PAT 64            /* the pattern's period in px */

typedef struct {
    disp_bench_t mode;
    int w, h, x, y;
    lv_obj_t *obj;
    lv_draw_buf_t *buf[2];
    int front;
    uint16_t *line;               /* one row of the pattern, w + PAT px */
    TaskHandle_t task;
    SemaphoreHandle_t exited;
    lv_timer_t *timer;
    disp_swap_t swap;
    uint32_t band_frame;          /* BAND: the frame being drawn (advanced at each refresh) */
    /* DIRECT (LVGL task, except chunks/nchunks: the worker reads them, fixed after start) */
    bool claimed, done;
    omni_dd_why_t why;            /* the claim's refusal or the exit's reason (OK: an abort or bench off) */
    int64_t retry_t;              /* after a refusal (claim or check): claim again from then on (0: never) */
    uint32_t last_shown, direct_frames;
    lv_obj_t *overlay;
    int64_t inval_t;
    int nchunks;
    omni_rect_t chunks[OMNI_DIAL_W / DISP_DIRECT_ROWS + 1];
    uint32_t band_conflicts;      /* BAND: draw events with tasks already queued on the layer (skipped) */
    /* shared with the worker under s_lock */
    bool quit, ready, want;
    uint32_t frame;               /* the frame number the back buffer holds */
} bench_t;

static bench_t *s_bench;

/* Rows [y0, y1) of frame k, w px wide, at dst (stride px): diagonal stripes moving 3 px a frame. The row-range
 * callback a band view would use. */
static void pattern_rows(const bench_t *b, uint32_t k, uint16_t *dst, int stride, int x0, int y0, int w, int y1)
{
    for (int y = y0; y < y1; y++) memcpy(dst + (size_t)(y - y0) * stride, b->line + ((x0 + y + 3 * k) % PAT), (size_t)w * 2);
}

static void bench_worker(void *arg)
{
    bench_t *b = arg;
    uint32_t k = 0;
    for (;;) {
        taskENTER_CRITICAL(&s_lock);
        const bool quit = b->quit, go = b->want && !b->ready;
        if (go) b->want = false;
        const int back = !b->front;
        taskEXIT_CRITICAL(&s_lock);
        if (quit) break;
        if (!go) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        k++;
        pattern_rows(b, k, (uint16_t *)b->buf[back]->data, b->w, 0, 0, b->w, b->h);
        disp_stats_frame_rendered();
        taskENTER_CRITICAL(&s_lock);
        b->frame = k;
        b->ready = true;
        taskEXIT_CRITICAL(&s_lock);
    }
    xSemaphoreGive(b->exited);
    vTaskSuspend(NULL);
}

typedef struct {
    const bench_t *b;
    uint32_t k, frames;
    bool stalled;
} direct_fill_t;

#define STALL_AFTER 30
#define STALL_MS 600

static void direct_fill(void *ctx, uint16_t *band, omni_rect_t c)   /* render task: c at stride OMNI_DIAL_W */
{
    direct_fill_t *f = ctx;
    if (f->b->mode == DISP_BENCH_DIRECT_STALL && !f->stalled && f->frames >= STALL_AFTER) {
        f->stalled = true;
        vTaskDelay(pdMS_TO_TICKS(STALL_MS));   /* a render task stuck with a band */
    }
    pattern_rows(f->b, f->k, band + c.x0, OMNI_DIAL_W, c.x0, c.y0, c.x1 - c.x0, c.y1);
}

/* DIRECT: frame after frame once the timer has claimed (want), until the path stops taking bands or quit. */
static void direct_worker(void *arg)
{
    bench_t *b = arg;
    direct_fill_t f = {.b = b};
    bool go = false;
    for (;;) {
        taskENTER_CRITICAL(&s_lock);
        const bool quit = b->quit, claim = b->want;
        if (b->want) go = true;
        b->want = false;
        taskEXIT_CRITICAL(&s_lock);
        if (quit) break;
        /* each claim's frame numbers go on from disp_direct_shown(), global and never reset: a dial view or an earlier
         * bench left it there (a lower number would read as ~2^32 frames shown, final-fix wave) */
        if (claim) f.k = disp_direct_shown();
        if (!go) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        f.k++;
        int i = 0;
        while (i < b->nchunks && disp_direct_send_chunk(b->chunks[i], direct_fill, &f)) i++;
        if (i < b->nchunks) {   /* aborted: the timer sees !active and releases */
            go = false;
            continue;
        }
        f.frames++;
        disp_direct_frame_end(f.k);
        disp_stats_frame_rendered();
    }
    xSemaphoreGive(b->exited);
    vTaskSuspend(NULL);
}

/* LVGL task: hand the panel back (the dropped areas are invalidated by the release) and redraw the object. */
static void direct_exit(bench_t *b, omni_dd_why_t why, const char *what)
{
    disp_direct_release();
    b->claimed = false;
    b->done = true;
    b->why = why;
    /* a refusal, not an abort: like a client in motion, claim again once the back-off has passed (step 8 counts the
     * RAM refusals during syncs this way) */
    const bool retry = why != OMNI_DD_OK && why != OMNI_DD_HIDDEN;
    b->retry_t = retry ? esp_timer_get_time() + DISP_DIRECT_BACKOFF_MS * 1000LL : 0;
    if (b->obj) lv_obj_invalidate(b->obj);
    ESP_LOGI(TAG, "disp bench direct: %s %s after %u frames, lost %u", what, disp_direct_why_name(why),
             (unsigned)b->direct_frames, (unsigned)disp_direct_lost());
}

static void direct_timer(lv_timer_t *t)
{
    bench_t *b = lv_timer_get_user_data(t);
    if (b->done && b->obj && b->retry_t && esp_timer_get_time() >= b->retry_t) b->done = false;   /* retry */
    if (b->done) return;
    if (!b->obj) {   /* the screen went: give the panel back */
        if (b->claimed) direct_exit(b, OMNI_DD_HIDDEN, "object gone,");
        b->done = true;
        return;
    }
    if (!b->claimed) {
        const omni_dd_why_t why = disp_direct_claim(b->obj);
        if (why == OMNI_DD_DISPLAY) return;   /* the panel comes back on by itself: wait */
        if (why != OMNI_DD_OK) {
            if (!b->retry_t) ESP_LOGI(TAG, "disp bench direct: claim refused %s", disp_direct_why_name(why));
            b->done = true;
            b->why = why;
            b->retry_t = esp_timer_get_time() + DISP_DIRECT_BACKOFF_MS * 1000LL;
            return;
        }
        if (b->retry_t) ESP_LOGI(TAG, "disp bench direct: claimed again");
        b->retry_t = 0;
        b->claimed = true;
        b->last_shown = disp_direct_shown();
        b->inval_t = esp_timer_get_time();
        taskENTER_CRITICAL(&s_lock);
        b->want = true;
        taskEXIT_CRITICAL(&s_lock);
        xTaskNotifyGive(b->task);
        return;
    }
    if (!disp_direct_active()) {
        direct_exit(b, OMNI_DD_OK, "aborted,");
        return;
    }
    const uint32_t shown = disp_direct_shown();
    if (shown != b->last_shown) {   /* a direct frame went out: admission once per frame, with the heap walk */
        for (uint32_t n = shown - b->last_shown; n > 0 && n < 64; n--) disp_stats_frame_shown();
        b->direct_frames += shown - b->last_shown;
        b->last_shown = shown;
        const omni_dd_why_t why = disp_direct_check(b->obj, true);
        if (why != OMNI_DD_OK) {
            direct_exit(b, why, "check refused");
            return;
        }
        if (b->mode == DISP_BENCH_DIRECT_OVERLAY && !b->overlay && b->direct_frames >= 30) {
            b->overlay = lv_obj_create(lv_layer_top());
            lv_obj_remove_style_all(b->overlay);
            lv_obj_set_pos(b->overlay, 214, 214);
            lv_obj_set_size(b->overlay, 40, 40);
            lv_obj_set_style_bg_color(b->overlay, lv_color_hex(0xc04040), 0);
            lv_obj_set_style_bg_opa(b->overlay, LV_OPA_COVER, 0);
            lv_obj_remove_flag(b->overlay, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    if (b->mode == DISP_BENCH_DIRECT_INVAL && esp_timer_get_time() - b->inval_t >= 33000) {
        b->inval_t += 33000;
        const lv_area_t box = {133, 300, 332, 319};   /* caption-sized, 200 x 20 */
        lv_obj_invalidate_area(b->obj, &box);
    }
}

static void bench_obj_deleted(lv_event_t *e)   /* the screen went (a face switch): the bench idles until off */
{
    bench_t *b = lv_event_get_user_data(e);
    b->obj = NULL;
}

static void bench_timer(lv_timer_t *t)   /* IMAGE modes: swap a finished frame in */
{
    bench_t *b = lv_timer_get_user_data(t);
    if (!b->obj) return;
    taskENTER_CRITICAL(&s_lock);
    const bool ready = b->ready;
    taskEXIT_CRITICAL(&s_lock);
    disp_swap_poll(&b->swap);
    if (!ready) return;
    b->front = !b->front;
    lv_image_set_src(b->obj, b->buf[b->front]);
    lv_obj_invalidate(b->obj);
    disp_swap_did(&b->swap);
    taskENTER_CRITICAL(&s_lock);
    b->ready = false;
    b->want = true;   /* the next frame renders while LVGL draws this one */
    taskEXIT_CRITICAL(&s_lock);
    xTaskNotifyGive(b->task);
}

/* BAND: each LVGL band is drawn straight into the band buffer. This object covers the screen, so LVGL draws it first
 * in each band and nothing is queued on the layer before it (checked: a band with queued tasks is skipped and counted). */
static void band_draw(lv_event_t *e)
{
    bench_t *b = lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (layer->draw_task_head != NULL || layer->draw_buf == NULL ||
        layer->draw_buf->header.cf != LV_COLOR_FORMAT_RGB565) {
        b->band_conflicts++;
        return;
    }
    const lv_area_t *c = &layer->_clip_area, *f = &layer->buf_area;   /* the clip within the band */
    const lv_area_t a = {LV_MAX(c->x1, f->x1), LV_MAX(c->y1, f->y1), LV_MIN(c->x2, f->x2), LV_MIN(c->y2, f->y2)};
    if (a.x2 < a.x1 || a.y2 < a.y1) return;
    const int stride = (int)(layer->draw_buf->header.stride / 2);
    uint16_t *dst = (uint16_t *)layer->draw_buf->data + (size_t)(a.y1 - layer->buf_area.y1) * stride +
                    (a.x1 - layer->buf_area.x1);
    pattern_rows(b, b->band_frame, dst, stride, a.x1, a.y1, lv_area_get_width(&a), a.y2 + 1);
}

static void band_timer(lv_timer_t *t)   /* BAND: a new frame whenever the last one was drawn */
{
    bench_t *b = lv_timer_get_user_data(t);
    if (!b->obj) return;
    disp_swap_poll(&b->swap);
    if (b->swap.await) return;   /* with or without back-pressure: a band frame exists only as it is drawn */
    b->band_frame++;
    disp_stats_frame_rendered();   /* "rendered" as it is drawn, in the bands of the next refresh */
    lv_obj_invalidate(b->obj);
    disp_swap_did(&b->swap);
}

static void bench_free(bench_t *b)
{
    if (b->timer) lv_timer_delete(b->timer);
    if (b->claimed) direct_exit(b, OMNI_DD_OK, "bench off,");   /* abort + release: the worker gets NULL bands */
    if (b->overlay) lv_obj_delete(b->overlay);
    if (b->task) {
        taskENTER_CRITICAL(&s_lock);
        b->quit = true;
        taskEXIT_CRITICAL(&s_lock);
        xTaskNotifyGive(b->task);
        xSemaphoreTake(b->exited, portMAX_DELAY);
        vTaskDeleteWithCaps(b->task);
    }
    if (b->obj) {
        lv_obj_remove_event_cb_with_user_data(b->obj, bench_obj_deleted, b);
        lv_obj_delete(b->obj);
    }
    theme_buf_destroy(b->buf[0]);
    theme_buf_destroy(b->buf[1]);
    heap_caps_free(b->line);
    if (b->exited) vSemaphoreDeleteWithCaps(b->exited);
    heap_caps_free(b);
}

const char *disp_bench_name(disp_bench_t mode)
{
    static const char *const N[] = {"off", "full", "box", "full band", "direct", "direct overlay", "direct inval",
                                    "direct stall"};
    return (unsigned)mode < sizeof N / sizeof N[0] ? N[mode] : "?";
}

disp_bench_t disp_bench_mode(void) { return s_bench ? s_bench->mode : DISP_BENCH_OFF; }

void disp_bench_stop(void)
{
    if (!s_bench) return;
    bench_free(s_bench);
    s_bench = NULL;
}

bool disp_bench_start(disp_bench_t mode)
{
    disp_bench_stop();
    if (mode == DISP_BENCH_OFF) return true;
    bench_t *b = heap_caps_calloc(1, sizeof *b, MALLOC_CAP_SPIRAM);
    if (!b) return false;
    b->mode = mode;
    const bool box = mode == DISP_BENCH_BOX;
    b->w = b->h = box ? BENCH_BOX : BENCH_W_FULL;
    b->x = box ? BENCH_BOX_X : 0;
    b->y = box ? BENCH_BOX_Y : 0;
    b->line = heap_caps_malloc((size_t)(BENCH_W_FULL + PAT) * 2, MALLOC_CAP_SPIRAM);
    bool ok = b->line != NULL;
    for (int i = 0; ok && i < BENCH_W_FULL + PAT; i++) {   /* a brass-ish ramp, RGB565 */
        const int v = i % PAT < PAT / 2 ? (i % PAT) * 8 : (PAT - i % PAT) * 8;
        b->line[i] = (uint16_t)(((v >> 3) << 11) | ((v * 3 / 4 >> 2) << 5) | (v / 3 >> 3));
    }
    const bool direct =
        mode == DISP_BENCH_DIRECT || mode == DISP_BENCH_DIRECT_OVERLAY || mode == DISP_BENCH_DIRECT_INVAL ||
        mode == DISP_BENCH_DIRECT_STALL;
    if (ok && direct) {
        const omni_rects_t all = {.r = {{0, 0, OMNI_DIAL_W, OMNI_DIAL_W}}, .n = 1};
        b->nchunks = omni_band_chunks(&all, DISP_DIRECT_ROWS, OMNI_DIAL_W, OMNI_DIAL_W, b->chunks,
                                      (int)(sizeof b->chunks / sizeof b->chunks[0]), NULL);
        b->buf[0] = theme_buf_create((uint32_t)b->w, (uint32_t)b->h, LV_COLOR_FORMAT_RGB565);
        ok = b->nchunks > 0 && b->buf[0] && b->buf[0]->header.stride == (uint32_t)b->w * 2;
        if (ok) pattern_rows(b, 0, (uint16_t *)b->buf[0]->data, b->w, 0, 0, b->w, b->h);
        b->exited = ok ? xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM) : NULL;
        ok = ok && b->exited;
        if (ok) {
            b->obj = lv_image_create(lv_screen_active());
            lv_obj_remove_flag(b->obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(b->obj, 0, 0);
            lv_image_set_src(b->obj, b->buf[0]);
            disp_image_cover(b->obj);
            lv_obj_add_event_cb(b->obj, bench_obj_deleted, LV_EVENT_DELETE, b);
            ok = xTaskCreatePinnedToCoreWithCaps(direct_worker, "dispbench", BENCH_STACK, b, BENCH_PRIO, &b->task,
                                                 BENCH_CORE, MALLOC_CAP_SPIRAM) == pdPASS;
            if (!ok) b->task = NULL;
        }
        if (ok) {
            disp_direct_still();   /* a bench start is a fresh motion: no back-off from the last bench */
            b->timer = lv_timer_create(direct_timer, BENCH_POLL_MS, b);
            ok = b->timer != NULL;
        }
    } else if (ok && mode == DISP_BENCH_BAND) {
        b->obj = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(b->obj);
        lv_obj_set_pos(b->obj, b->x, b->y);
        lv_obj_set_size(b->obj, b->w, b->h);
        lv_obj_remove_flag(b->obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b->obj, band_draw, LV_EVENT_DRAW_MAIN, b);
        disp_image_cover(b->obj);
        lv_obj_add_event_cb(b->obj, bench_obj_deleted, LV_EVENT_DELETE, b);
        b->timer = lv_timer_create(band_timer, BENCH_POLL_MS, b);
        ok = b->timer != NULL;
    } else if (ok) {
        for (int i = 0; i < 2 && ok; i++) {
            b->buf[i] = theme_buf_create((uint32_t)b->w, (uint32_t)b->h, LV_COLOR_FORMAT_RGB565);
            ok = b->buf[i] && b->buf[i]->header.stride == (uint32_t)b->w * 2;
            if (ok) pattern_rows(b, 0, (uint16_t *)b->buf[i]->data, b->w, 0, 0, b->w, b->h);
        }
        b->exited = ok ? xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM) : NULL;
        ok = ok && b->exited;
        if (ok) {
            b->obj = lv_image_create(lv_screen_active());
            lv_obj_remove_flag(b->obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(b->obj, b->x, b->y);
            lv_image_set_src(b->obj, b->buf[0]);
            disp_image_cover(b->obj);
            lv_obj_add_event_cb(b->obj, bench_obj_deleted, LV_EVENT_DELETE, b);
            b->want = true;
            ok = xTaskCreatePinnedToCoreWithCaps(bench_worker, "dispbench", BENCH_STACK, b, BENCH_PRIO, &b->task,
                                                 BENCH_CORE, MALLOC_CAP_SPIRAM) == pdPASS;
            if (!ok) b->task = NULL;
        }
        if (ok) {
            b->timer = lv_timer_create(bench_timer, BENCH_POLL_MS, b);
            ok = b->timer != NULL;
        }
    }
    if (!ok) {
        ESP_LOGE(TAG, "disp bench %s: not created (PSRAM free %u)", disp_bench_name(mode),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        bench_free(b);
        return false;
    }
    s_bench = b;
    return true;
}
#endif /* CONFIG_OMNI_DEBUG_CONSOLE */
