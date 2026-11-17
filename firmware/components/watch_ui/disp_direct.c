#include "disp_direct.h"
#include <string.h>
#include "board.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "omni/omni_dial.h"
#include "core/lv_obj_draw_private.h"   /* lv_obj_get_ext_draw_size: the snapshot's margin (as lv_snapshot.c) */

static const char *TAG = "direct";

/* R120's starting values. */
#define SENDER_STACK 3072           /* bytes, PSRAM (the bench's spdld) */
#define SENDER_PRIO 6
#define SENDER_CORE 0
#define ORPHAN_POLL_MS 50
#define ORPHAN_MS 250               /* claimed, no band put for this long: abort + release (D6) */
#define BAND_PX ((size_t)OMNI_DIAL_W * DISP_DIRECT_ROWS)
#define BAND_BYTES (BAND_PX * 2)
#define SENTINELS 4                 /* NULLs abort posts to the free pool, one per blocked band() (2 render tasks) */
#define FIFO_LEN (2 * DISP_DIRECT_BANDS + 2)   /* bands in flight <= the pool, plus the frame-end markers between */
#define DMA_INT (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)

_Static_assert(DISP_DIRECT_ROWS > 0 && DISP_DIRECT_ROWS % 2 == 0, "bands hold an even number of rows");
_Static_assert(DISP_DIRECT_DEFAULT_MODE == DISP_DIRECT_MODE_AUTO, "the production mode is auto (R121/C1)");

/* The mode and the reserve: production constants; mutable only in console builds (R101, C1). */
#if CONFIG_OMNI_DEBUG_CONSOLE
static volatile disp_direct_mode_t s_mode = DISP_DIRECT_DEFAULT_MODE;
static volatile uint32_t s_reserve = DISP_DIRECT_RESERVE;
#else
#define s_mode DISP_DIRECT_DEFAULT_MODE
#define s_reserve ((uint32_t)DISP_DIRECT_RESERVE)
#endif

typedef struct {
    uint16_t *band;   /* NULL: the frame-end marker for seq */
    omni_rect_t r;
    uint32_t seq;
} item_t;

/* Created once, at the first claim, and kept (R120, B13): the queues in PSRAM, the sender's stack in PSRAM (its TCB
 * internal), the orphan timer. */
static QueueHandle_t s_fifo;        /* item_t: render tasks -> the sender */
static QueueHandle_t s_free;        /* uint16_t *: the band pool (NULL entries: abort's wake-ups) */
static TaskHandle_t s_sender;
static lv_timer_t *s_orphan;

/* LVGL ctx (claim, release; set_blocked under bsp_display_lock) */
static bool s_claimed;              /* bands allocated and begin done: the panel is the path's, until hand_back */
static bool s_releasing;            /* release asked (aborted): hand_back once every band is back (D1) */
static int64_t s_release_t0;
static bool s_drain_noted;   /* this release's drain time-out is already warned and counted */
static bool s_stuck;                /* handed back after DISP_DIRECT_DRAIN_MS: the new bands are kept until idle (D1) */
static omni_dd_why_t s_check_why = OMNI_DD_OK;   /* the last refusing check since the claim: an exit's reason (M2) */
static uint16_t *s_new[DISP_DIRECT_BANDS];   /* the bands allocated at the claim */
static int s_new_n;
static volatile bool s_blocked;     /* provisioning (set_blocked) */
static bool s_backoff;              /* D7 */
static int64_t s_backoff_t0;
static omni_dd_why_t s_backoff_why = OMNI_DD_OK;   /* what started it: a refusal's reason, or an exit's (OK: plain) */
static bool s_inactive_seen;        /* the orphan timer has seen the claimed path inactive, since s_inactive_ms */
static uint32_t s_inactive_ms;

/* Shared with the render tasks and the sender, under s_lk. */
static portMUX_TYPE s_lk = portMUX_INITIALIZER_UNLOCKED;
static bool s_aborted;
static uint32_t s_taken, s_put;     /* bands handed out / put since the claim (D1: taken - put are rendering) */
static uint32_t s_inflight;         /* bands put and not yet back in the pool (in the FIFO, or on the wire) */
static bool s_frame_bad;            /* a band of the current frame was skipped or discarded */
static uint32_t s_lost, s_lost_total;
static volatile uint32_t s_last_put_ms;
static volatile uint32_t s_shown;
static int64_t s_frame_t;           /* the sender: the last whole frame's last band on the wire (0: none yet) */
static uint64_t s_frame_us_sum;     /* the sender: whole frame to whole frame, since the claim */
static uint32_t s_frame_n, s_frame_us_max;

/* Counters (`disp`): any task writes them under s_lk or from its own context only. */
static uint32_t s_claims, s_refusals[OMNI_DD__COUNT], s_display_waits, s_backoffs, s_frames, s_bands, s_orphans,
    s_drain_timeouts;
static int32_t s_perm_bytes = -1;

#if CONFIG_OMNI_DEBUG_CONSOLE
static volatile bool s_shadow_on;
static uint16_t *s_shadow;          /* OMNI_DIAL_W x OMNI_DIAL_W RGB565 (native order), PSRAM */
#endif

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ------------------------------------------------------------ the sender (core 0, priority 6) */

static void give_back(uint16_t *band)
{
    xQueueSend(s_free, &band, portMAX_DELAY);   /* the pool holds every band: never full of them */
    taskENTER_CRITICAL(&s_lk);
    s_inflight--;
    taskEXIT_CRITICAL(&s_lk);
}

static void band_lost(void)
{
    taskENTER_CRITICAL(&s_lk);
    s_lost++;
    s_lost_total++;
    s_frame_bad = true;
    taskEXIT_CRITICAL(&s_lk);
}

#if CONFIG_OMNI_DEBUG_CONSOLE
static void shadow_copy(const uint16_t *band, omni_rect_t r)
{
    if (!s_shadow_on || s_shadow == NULL) return;
    const int w = r.x1 - r.x0;
    for (int y = r.y0; y < r.y1; y++) {
        const uint16_t *src = band + (size_t)(y - r.y0) * w;
        uint16_t *dst = s_shadow + (size_t)y * OMNI_DIAL_W + r.x0;
        for (int x = 0; x < w; x++) dst[x] = __builtin_bswap16(src[x]);   /* back from the panel's byte order */
    }
}
#endif

static void sender_task(void *arg)
{
    (void)arg;
    uint16_t *held = NULL;   /* the last band drawn: its transfer may still be on the wire */
    for (;;) {
        item_t it;
        if (xQueueReceive(s_fifo, &it, held ? 0 : portMAX_DELAY) != pdTRUE) {
            board_direct_wait();   /* nothing queued: let the last band finish, so the pool is whole when idle */
            give_back(held);
            held = NULL;
            continue;
        }
        if (it.band == NULL) {     /* frame end: its last band returns once it is on the wire */
            if (held) {
                board_direct_wait();
                give_back(held);
                held = NULL;
            }
            const int64_t t = esp_timer_get_time();
            taskENTER_CRITICAL(&s_lk);
            const bool whole = !s_frame_bad;
            s_frame_bad = false;
            if (whole) {
                s_shown = it.seq;
                s_frames++;
                if (s_frame_t) {
                    const uint32_t us = (uint32_t)(t - s_frame_t);
                    s_frame_us_sum += us;
                    s_frame_n++;
                    if (us > s_frame_us_max) s_frame_us_max = us;
                }
            }
            s_frame_t = whole ? t : 0;
            taskEXIT_CRITICAL(&s_lk);
            continue;
        }
        taskENTER_CRITICAL(&s_lk);
        const bool drop = s_aborted;
        taskEXIT_CRITICAL(&s_lk);
        const esp_err_t err =
            drop ? ESP_ERR_INVALID_STATE : board_direct_draw(it.r.x0, it.r.y0, it.r.x1, it.r.y1, it.band);
        /* A band returns when the next draw call returns: that draw's CASET is a polling transaction, which waits for
         * every queued colour transfer first (inv. section 2). A draw that did not go out waited for nothing. */
        if (held) {
            if (err != ESP_OK) board_direct_wait();
            give_back(held);
            held = NULL;
        }
        if (err == ESP_OK) {
            s_bands++;
#if CONFIG_OMNI_DEBUG_CONSOLE
            shadow_copy(it.band, it.r);
#endif
            held = it.band;
        } else {
            if (err == ESP_ERR_INVALID_STATE && board_direct_ended()) {   /* the display went off: the path is over */
                taskENTER_CRITICAL(&s_lk);
                s_aborted = true;
                taskEXIT_CRITICAL(&s_lk);
            }
            band_lost();
            give_back(it.band);
        }
    }
}

/* ------------------------------------------------------------ render-task side */

uint16_t *disp_direct_band(void)
{
    if (s_free == NULL) return NULL;
    for (;;) {
        if (board_direct_ended()) {   /* D4: the stage is LVGL's again; no band may be written */
            taskENTER_CRITICAL(&s_lk);
            s_aborted = true;
            taskEXIT_CRITICAL(&s_lk);
        }
        taskENTER_CRITICAL(&s_lk);
        const bool stop = s_aborted;
        taskEXIT_CRITICAL(&s_lk);
        if (stop) return NULL;
        uint16_t *b = NULL;
        if (xQueueReceive(s_free, &b, pdMS_TO_TICKS(20)) != pdTRUE || b == NULL) continue;
        taskENTER_CRITICAL(&s_lk);
        const bool ok = !s_aborted;
        if (ok) s_taken++;
        taskEXIT_CRITICAL(&s_lk);
        if (ok) return b;
        xQueueSend(s_free, &b, 0);   /* aborted meanwhile: it stays in the pool */
        return NULL;
    }
}

void disp_direct_put(uint16_t *band, omni_rect_t c)
{
    const item_t it = {.band = band, .r = c};
    taskENTER_CRITICAL(&s_lk);
    s_put++;
    s_inflight++;
    taskEXIT_CRITICAL(&s_lk);
    s_last_put_ms = now_ms();
    xQueueSend(s_fifo, &it, portMAX_DELAY);
}

void disp_direct_skip(void) { band_lost(); }

bool disp_direct_send_chunk(omni_rect_t c, void (*fill)(void *ctx, uint16_t *band, omni_rect_t c), void *ctx)
{
    if (c.y1 - c.y0 > DISP_DIRECT_ROWS || c.x0 < 0 || c.x1 > OMNI_DIAL_W || c.x1 <= c.x0 || c.y1 <= c.y0) {
        disp_direct_skip();   /* a chunk the band cannot hold: the client's bug, counted as a lost band */
        return false;
    }
    uint16_t *b = disp_direct_band();
    if (b == NULL) {
        disp_direct_skip();
        return false;
    }
    /* M1: a band whose claim has ended may be the lent stage, which LVGL uses again after the hand-back: no write into
     * it then. put() only queues the pointer; the sender discards it (lost) and returns it to the pool. */
    if (!disp_direct_active()) {
        disp_direct_put(b, c);
        return false;
    }
    fill(ctx, b, c);
    if (!disp_direct_active()) {   /* ended during the fill: no pack either */
        disp_direct_put(b, c);
        return false;
    }
    omni_band_pack(b, OMNI_DIAL_W, c);
    disp_direct_put(b, c);
    return true;
}

void disp_direct_frame_end(uint32_t seq)
{
    if (s_fifo == NULL) return;
    const item_t it = {.band = NULL, .seq = seq};
    xQueueSend(s_fifo, &it, portMAX_DELAY);
}

uint32_t disp_direct_shown(void) { return s_shown; }

uint32_t disp_direct_lost(void)
{
    taskENTER_CRITICAL(&s_lk);
    const uint32_t v = s_lost;
    taskEXIT_CRITICAL(&s_lk);
    return v;
}

void disp_direct_abort(void)
{
    taskENTER_CRITICAL(&s_lk);
    const bool was = s_aborted;
    s_aborted = true;
    taskEXIT_CRITICAL(&s_lk);
    if (was || s_free == NULL) return;
    uint16_t *none = NULL;
    for (int i = 0; i < SENTINELS; i++) xQueueSend(s_free, &none, 0);   /* wake every band() blocked on the pool */
}

bool disp_direct_active(void)
{
    taskENTER_CRITICAL(&s_lk);
    const bool aborted = s_aborted;
    taskEXIT_CRITICAL(&s_lk);
    return s_claimed && !s_releasing && !aborted && !board_direct_ended();
}

/* ------------------------------------------------------------ admission (LVGL ctx) */

/* ------------------------------------------------------------ the overlay registry (LVGL ctx, Task 6) */

typedef struct {
    lv_obj_t *obj;
    bool dirty;                     /* disp_direct_overlay_changed since the last snapshot */
    bool failed;                    /* the last snapshot failed: refuses as an unknown overlay until it changes */
    bool shown;                     /* in the last disp_direct_overlays' out[], at r */
    lv_opa_t opa;                   /* the opacity the sprite was taken at */
    omni_rect_t r;                  /* the sprite's screen rect: the object's coords and ext draw size */
    lv_draw_buf_t buf;              /* over mem: ARGB8888, header.stride bytes a row */
    void *mem;                      /* PSRAM, grown to fit (never DMA'd) */
    uint32_t cap;
    uint32_t snaps, us_max;
    uint64_t us_sum;
} ov_t;

static ov_t *s_ov;                  /* DISP_DIRECT_OVERLAYS, in PSRAM from the first add (R118: no internal .bss) */

static ov_t *ov_find(const lv_obj_t *obj)
{
    for (int i = 0; s_ov && obj && i < DISP_DIRECT_OVERLAYS; i++)
        if (s_ov[i].obj == obj) return &s_ov[i];
    return NULL;
}

static uint32_t s_ov_gen;    /* bumped by every drop: a client's published set older than it holds freed sprites */
static uint32_t s_ov_hold;   /* clients copying published sprites (disp_direct_overlay_hold) */

uint32_t disp_direct_overlay_gen(void) { return __atomic_load_n(&s_ov_gen, __ATOMIC_SEQ_CST); }

void disp_direct_overlay_hold(bool on)
{
    if (on) __atomic_add_fetch(&s_ov_hold, 1, __ATOMIC_SEQ_CST);
    else __atomic_sub_fetch(&s_ov_hold, 1, __ATOMIC_SEQ_CST);
}

/* m3: the gen moves first, then the sprite waits out a copy that began before it (a copy that begins after sees the new
 * gen and skips); a copy that never ends leaks the sprite rather than freeing it under the reader. */
static void ov_drop(ov_t *o)
{
    __atomic_add_fetch(&s_ov_gen, 1, __ATOMIC_SEQ_CST);
    const int64_t t0 = esp_timer_get_time();
    while (__atomic_load_n(&s_ov_hold, __ATOMIC_SEQ_CST) && esp_timer_get_time() - t0 < 1000 * 1000) vTaskDelay(1);
    if (__atomic_load_n(&s_ov_hold, __ATOMIC_SEQ_CST))
        ESP_LOGW(TAG, "overlay: a sprite copy did not end in 1 s; the sprite is leaked");
    else
        heap_caps_free(o->mem);
    memset(o, 0, sizeof *o);
}

static void ov_deleted(lv_event_t *e)
{
    ov_t *o = ov_find(lv_event_get_target(e));
    if (o) ov_drop(o);   /* the object goes: its callback with it */
}

bool disp_direct_overlay_add(lv_obj_t *obj)
{
    if (obj == NULL) return false;
    if (ov_find(obj)) return true;
    if (s_ov == NULL) s_ov = heap_caps_calloc(DISP_DIRECT_OVERLAYS, sizeof *s_ov, MALLOC_CAP_SPIRAM);
    if (s_ov == NULL) return false;   /* the object then refuses as an unknown overlay */
    for (int i = 0; i < DISP_DIRECT_OVERLAYS; i++)
        if (s_ov[i].obj == NULL) {
            s_ov[i].obj = obj;
            s_ov[i].dirty = true;
            lv_obj_add_event_cb(obj, ov_deleted, LV_EVENT_DELETE, NULL);
            return true;
        }
    ESP_LOGW(TAG, "overlay registry full (%d): the object refuses as an unknown overlay", DISP_DIRECT_OVERLAYS);
    return false;
}

void disp_direct_overlay_remove(lv_obj_t *obj)
{
    ov_t *o = ov_find(obj);
    if (o == NULL) return;
    lv_obj_remove_event_cb(obj, ov_deleted);
    ov_drop(o);
}

void disp_direct_overlay_changed(lv_obj_t *obj)
{
    ov_t *o = ov_find(obj);
    if (o) o->dirty = true;
}

bool disp_direct_overlay_registered(const lv_obj_t *obj)
{
    const ov_t *o = ov_find(obj);
    return o && !o->failed;
}

/* Drawn: on the active screen or the top or system layer, neither it nor an ancestor hidden, and not wholly
 * transparent. */
static bool ov_visible(lv_obj_t *obj)
{
    lv_obj_t *scr = lv_obj_get_screen(obj);   /* the walk scans the active screen and both layers: so does this (m4) */
    if (scr != lv_screen_active() && scr != lv_layer_top() && scr != lv_layer_sys()) return false;
    for (lv_obj_t *p = obj; p != NULL; p = lv_obj_get_parent(p))
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) return false;
    return lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN) > LV_OPA_MIN;
}

static omni_rect_t ov_rect(lv_obj_t *obj)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int32_t ext = lv_obj_get_ext_draw_size(obj);
    return (omni_rect_t){ (int16_t)(a.x1 - ext), (int16_t)(a.y1 - ext), (int16_t)(a.x2 + 1 + ext),
                          (int16_t)(a.y2 + 1 + ext) };
}

/* The object as LVGL draws it, into o's PSRAM buffer (o->r its rect, even if it fails). lv_snapshot draws the object
 * itself at full opacity (only its children get theirs), so the object's opacity, with its ancestors', scales the
 * alpha after, as lv_obj_refr's layer opacity LV_OPA_MIX2(COVER, opa) scales the draw descriptors' (where a child
 * overlaps the object's own background the two differ a little: the snapshot's alpha is already composited). */
static bool ov_snap(ov_t *o, lv_opa_t opa)
{
    const int64_t t0 = esp_timer_get_time();
    lv_obj_update_layout(o->obj);   /* a new text's size, before the buffer is sized */
    const omni_rect_t r = ov_rect(o->obj);
    o->r = r;
    const int32_t w = r.x1 - r.x0, h = r.y1 - r.y0;
    if (w <= 0 || h <= 0) return false;
    const uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_ARGB8888),
                   need = stride * (uint32_t)h;
    if (need > o->cap) {
        heap_caps_free(o->mem);
        o->mem = heap_caps_malloc(need, MALLOC_CAP_SPIRAM);
        o->cap = o->mem ? need : 0;
        if (o->mem == NULL) {
            ESP_LOGE(TAG, "overlay: no PSRAM for a %dx%d sprite", (int)w, (int)h);
            return false;
        }
    }
    if (lv_draw_buf_init(&o->buf, (uint32_t)w, (uint32_t)h, LV_COLOR_FORMAT_ARGB8888, stride, o->mem, o->cap) !=
            LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(o->obj, LV_COLOR_FORMAT_ARGB8888, &o->buf) != LV_RESULT_OK)
        return false;
    if (opa < LV_OPA_MAX) {
        const lv_opa_t lo = LV_OPA_MIX2(LV_OPA_COVER, opa);
        for (int32_t y = 0; y < h; y++) {
            uint8_t *p = (uint8_t *)o->mem + (size_t)y * stride + 3;   /* B, G, R, A */
            for (int32_t x = 0; x < w; x++, p += 4) *p = (uint8_t)LV_OPA_MIX2(*p, lo);
        }
    }
    const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    o->snaps++;
    o->us_sum += us;
    if (us > o->us_max) o->us_max = us;
    return true;
}

static void ov_area(omni_rects_t *areas, omni_rect_t r)
{
    const omni_rect_t e = omni_band_even(r, OMNI_DIAL_W, OMNI_DIAL_W);
    if (omni_rect_area(&e)) omni_rects_add(areas, e);
}

int disp_direct_overlays(omni_band_overlay_t *out, omni_rects_t *areas, omni_rects_t *owed)
{
    int n = 0;
    for (int i = 0; s_ov && i < DISP_DIRECT_OVERLAYS; i++) {
        ov_t *o = &s_ov[i];
        if (o->obj == NULL) continue;
        if (!ov_visible(o->obj)) {
            if (o->shown) ov_area(areas, o->r);   /* hidden: its old pixels go */
            o->shown = false;
            continue;
        }
        const lv_opa_t opa = lv_obj_get_style_opa_recursive(o->obj, LV_PART_MAIN);
        const omni_rect_t at = ov_rect(o->obj);
        const bool changed = o->dirty || opa != o->opa || memcmp(&at, &o->r, sizeof at) != 0;
        if (o->failed && !changed) {   /* no retry until it changes */
            ov_area(owed, o->r);
            continue;
        }
        if (changed || !o->shown) {
            const omni_rect_t old = o->r;
            const bool was = o->shown;
            o->failed = !ov_snap(o, opa);
            o->dirty = false;
            o->opa = opa;
            if (was) ov_area(areas, old);
            o->shown = !o->failed;
            if (o->failed) {   /* m1: LVGL still shows it, the direct frames cannot: the hand-back redraws its rect */
                ov_area(owed, o->r);
                if (was) ov_area(owed, old);
                continue;
            }
            ov_area(areas, o->r);
        }
        out[n++] = (omni_band_overlay_t){ .r = o->r, .argb = (const uint32_t *)o->mem,
                                          .stride = (int)(o->buf.header.stride / 4) };   /* bytes to px (A9) */
    }
    return n;
}

static bool view_shown(lv_obj_t *view)
{
    if (view == NULL || lv_obj_get_screen(view) != lv_screen_active()) return false;
    for (lv_obj_t *o = view; o != NULL; o = lv_obj_get_parent(o))
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || lv_obj_get_style_opa(o, LV_PART_MAIN) != LV_OPA_COVER)
            return false;
    return true;
}

typedef struct {
    const lv_obj_t *view;
    bool after;   /* the walk has passed the view: what follows is drawn over it */
    bool found;
} walk_t;

/* Depth-first, in drawing order. A hidden or transparent object hides its subtree; a registered one covers its own;
 * the view's own subtree is never an overlay (M3). */
static void walk(lv_obj_t *o, walk_t *w, bool root)
{
    if (w->found) return;
    if (o == w->view) {
        w->after = true;
        return;   /* M3: the view's own children are part of its drawing, never overlays over it */
    } else if (!root) {
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || lv_obj_get_style_opa(o, LV_PART_MAIN) == LV_OPA_TRANSP) return;
        if (disp_direct_overlay_registered(o)) return;
        if (w->after) {
            lv_area_t a;
            lv_obj_get_coords(o, &a);
            if (a.x2 >= 0 && a.y2 >= 0 && a.x1 < OMNI_DIAL_W && a.y1 < OMNI_DIAL_W && a.x2 >= a.x1 && a.y2 >= a.y1) {
                w->found = true;
                return;
            }
        }
    }
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n && !w->found; i++) walk(lv_obj_get_child(o, (int32_t)i), w, false);
}

static bool unknown_overlay(lv_obj_t *view)
{
    walk_t w = {.view = view};
    walk(lv_screen_active(), &w, true);
    if (!w.found && lv_layer_top()) walk(lv_layer_top(), &w, true);
    if (!w.found && lv_layer_sys()) walk(lv_layer_sys(), &w, true);
    return w.found;
}

static omni_dd_why_t admit(lv_obj_t *view, uint32_t need, bool ram)
{
    omni_direct_env_t e = {
        .enabled = DISP_DIRECT_BUILT && s_mode == DISP_DIRECT_MODE_AUTO,
        .provisioning = s_blocked,
        .display_on = board_direct_enabled(),
        .int_free = UINT32_MAX,
        .dma_largest = UINT32_MAX,
    };
    /* the costly ones only when every earlier condition holds (admit returns the first failing one) */
    if (e.enabled && !e.provisioning && e.display_on) {
        e.view_shown = view_shown(view);
        e.unknown_overlay = e.view_shown && unknown_overlay(view);
        if (ram) {
            e.int_free = (uint32_t)heap_caps_get_free_size(DMA_INT);
            e.dma_largest = (uint32_t)heap_caps_get_largest_free_block(DMA_INT);
        }
    }
    return omni_direct_admit(&e, need, (uint32_t)BAND_BYTES, s_reserve);
}

omni_dd_why_t disp_direct_check(lv_obj_t *view, bool ram)
{
    const omni_dd_why_t why = admit(view, 0, ram);
    if (why != OMNI_DD_OK && s_claimed) s_check_why = why;
    return why;
}

void disp_direct_still(void) { s_backoff = false; }

static void backoff_start(omni_dd_why_t why)
{
    s_backoff = true;
    s_backoff_t0 = esp_timer_get_time();
    s_backoff_why = why;
}

/* ------------------------------------------------------------ claim and release (LVGL ctx) */

static bool path_idle(void)
{
    taskENTER_CRITICAL(&s_lk);
    const bool idle = s_taken == s_put && s_inflight == 0;
    taskEXIT_CRITICAL(&s_lk);
    return idle;
}

/* The path is idle and handed back: empty the pool (bands and abort's NULLs), free the bands allocated at the claim,
 * stop the orphan timer. */
static void free_bands(void)
{
    uint16_t *b;
    while (s_free && xQueueReceive(s_free, &b, 0) == pdTRUE) {
    }
    for (int i = 0; i < s_new_n; i++) heap_caps_free(s_new[i]);
    s_new_n = 0;
    s_stuck = false;
    if (s_orphan) lv_timer_pause(s_orphan);
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (!s_shadow_on && s_shadow) {
        heap_caps_free(s_shadow);
        s_shadow = NULL;
    }
#endif
}

static void orphan_cb(lv_timer_t *t);

/* The permanent objects, once (B13): their internal cost is measured here. */
static bool create_once(void)
{
    if (s_sender) return true;
    const size_t f0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (!s_fifo) s_fifo = xQueueCreateWithCaps(FIFO_LEN, sizeof(item_t), MALLOC_CAP_SPIRAM);
    if (!s_free) s_free = xQueueCreateWithCaps(DISP_DIRECT_BANDS + SENTINELS, sizeof(uint16_t *), MALLOC_CAP_SPIRAM);
    if (!s_orphan) {
        s_orphan = lv_timer_create(orphan_cb, ORPHAN_POLL_MS, NULL);
        if (s_orphan) lv_timer_pause(s_orphan);
    }
    if (!s_fifo || !s_free || !s_orphan) return false;
    if (xTaskCreatePinnedToCoreWithCaps(sender_task, "ddsend", SENDER_STACK, NULL, SENDER_PRIO, &s_sender, SENDER_CORE,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        s_sender = NULL;
        return false;
    }
    s_perm_bytes = (int32_t)(f0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

static omni_dd_why_t refuse(omni_dd_why_t why)
{
    if (why == OMNI_DD_DISPLAY) {
        s_display_waits++;   /* the panel comes back on by itself: a wait, not a refusal (B16) */
        return why;
    }
    s_refusals[why]++;
    backoff_start(why);
    return why;
}

omni_dd_why_t disp_direct_claim(lv_obj_t *view)
{
    if (!DISP_DIRECT_BUILT) return refuse(OMNI_DD_OFF);
    if (s_claimed && disp_direct_active()) return OMNI_DD_OK;
    if (s_claimed || s_stuck) disp_direct_release();   /* aborted under the client: hand the panel back first */
    /* N2: the last claim's bands still draining back (D1) is part of the exit's back-off, not a RAM refusal */
    if (s_claimed || s_stuck ||
        (s_backoff && esp_timer_get_time() - s_backoff_t0 < (int64_t)DISP_DIRECT_BACKOFF_MS * 1000)) {
        s_backoffs++;
        return OMNI_DD_BACKOFF;   /* N1/M2: the back-off itself, never the reason of an older refusal */
    }
    s_backoff = false;
    size_t stage_bytes = 0;
    uint8_t *stage = board_stage_lend(&stage_bytes);   /* its size; the buffer is ours only after begin */
    int lent = stage ? (int)(stage_bytes / BAND_BYTES) : 0;
    if (lent > DISP_DIRECT_BANDS) lent = DISP_DIRECT_BANDS;
    const int fresh = DISP_DIRECT_BANDS - lent;
    const omni_dd_why_t why = admit(view, (uint32_t)(fresh * BAND_BYTES), true);
    if (why != OMNI_DD_OK) return refuse(why);
    if (!create_once()) {
        ESP_LOGE(TAG, "sender/queues/timer not created");
        return refuse(OMNI_DD_RAM);
    }
    for (int i = 0; i < fresh; i++) {
        s_new[i] = heap_caps_aligned_alloc(64, BAND_BYTES, DMA_INT);
        if (s_new[i] == NULL) {
            for (int j = 0; j < i; j++) heap_caps_free(s_new[j]);
            return refuse(OMNI_DD_RAM);
        }
    }
    s_new_n = fresh;
    if (!board_direct_begin()) {   /* the display went off since admission */
        free_bands();
        return refuse(OMNI_DD_DISPLAY);
    }
    stage = board_stage_lend(&stage_bytes);
    taskENTER_CRITICAL(&s_lk);
    s_aborted = false;
    s_taken = s_put = s_inflight = 0;
    s_frame_bad = false;
    s_lost = 0;
    s_frame_t = 0;
    s_frame_us_sum = 0;
    s_frame_n = s_frame_us_max = 0;
    taskEXIT_CRITICAL(&s_lk);
    uint16_t *b;
    while (xQueueReceive(s_free, &b, 0) == pdTRUE) {   /* stale wake-ups */
    }
    for (int i = 0; i < lent; i++) {
        b = (uint16_t *)(stage + (size_t)i * BAND_BYTES);
        xQueueSend(s_free, &b, 0);
    }
    for (int i = 0; i < fresh; i++) xQueueSend(s_free, &s_new[i], 0);
    s_last_put_ms = now_ms();
    s_inactive_seen = false;
    s_check_why = OMNI_DD_OK;
    s_releasing = false;
    s_claimed = true;
    s_claims++;
    lv_timer_resume(s_orphan);
    lv_timer_reset(s_orphan);
    return OMNI_DD_OK;
}

/* Every band back: LVGL flushes reach the panel again, and what it dropped is redrawn. */
static void hand_back(void)
{
    lv_area_t a[BOARD_DIRECT_DROP_MAX];
    const int n = board_direct_end(a);   /* waits for the wire under the panel mutex, then returns the stage */
    for (int i = 0; i < n; i++) lv_obj_invalidate_area(lv_screen_active(), &a[i]);   /* the display's (all layers) */
    s_claimed = false;
    s_releasing = false;
}

/* The sender has every band it was given back: it does no more bus work for this claim (aborted, it draws nothing, and
 * it waits on the wire only for a band it drew). */
static bool sender_drained(void)
{
    taskENTER_CRITICAL(&s_lk);
    const bool d = s_inflight == 0;
    taskEXIT_CRITICAL(&s_lk);
    return d;
}

/* Locked (LVGL ctx or bsp_display_lock): hand back once idle, or stuck once DISP_DIRECT_DRAIN_MS has passed since the
 * release began (D1: LVGL cannot stay dropped for good; the new bands stay allocated); free the bands once idle. A
 * stuck hand-back still waits for the sender to drain (M5, Task 5): until then LVGL's flushes stay dropped, since the
 * sender's board_direct_wait and an LVGL flush on the bus together race esp_lcd's unsynchronised SPI IO (the hang
 * board_direct_enable's wait caused). Only render tasks still holding bands make a release stuck. */
static void release_step(void)
{
    if (!s_claimed && !s_stuck) return;
    const bool idle = path_idle();
    const bool late = s_claimed && esp_timer_get_time() - s_release_t0 >= (int64_t)DISP_DIRECT_DRAIN_MS * 1000;
    if (late && !idle && !s_drain_noted && !sender_drained()) {
        /* R1-1: the hand-back waits for the sender, so a wedged one keeps LVGL's flushes dropped: say so, once */
        s_drain_noted = true;
        s_drain_timeouts++;
        ESP_LOGW(TAG, "release: sender not drained after %d ms: LVGL stays dropped until it is", DISP_DIRECT_DRAIN_MS);
    }
    if (s_claimed && (idle || (late && sender_drained()))) {
        hand_back();
        if (!idle) {
            s_stuck = true;
            if (!s_drain_noted) s_drain_timeouts++;
            s_drain_noted = true;
            taskENTER_CRITICAL(&s_lk);
            const uint32_t out = s_taken - s_put, inflight = s_inflight;
            taskEXIT_CRITICAL(&s_lk);
            ESP_LOGW(TAG, "release: bands not back after %d ms (%u rendering, %u in flight): handed back, bands kept "
                     "until they are", DISP_DIRECT_DRAIN_MS, (unsigned)out, (unsigned)inflight);
        }
    }
    if (!s_claimed && path_idle()) free_bands();
}

/* Every exit backs off (D7): a plain release, an orphan, a stuck one, an abort alike. A claim inside it returns
 * OMNI_DD_BACKOFF; the cause is kept for `disp` only (provisioning, the display off, the client's last refusing check,
 * or OK for a plain exit). */
static void exit_backoff(void)
{
    backoff_start(s_blocked ? OMNI_DD_PROV : board_direct_ended() ? OMNI_DD_DISPLAY : s_check_why);
}

void disp_direct_release(void)
{
    if (s_claimed && !s_releasing) {
        exit_backoff();
        disp_direct_abort();
        s_releasing = true;
        s_release_t0 = esp_timer_get_time();
        s_drain_noted = false;
    }
    /* M6: at most DISP_DIRECT_LOCKED_WAIT_MS here (the caller holds LVGL's lock); the orphan timer, which runs until
     * the bands are freed, finishes a release that needs longer (I1). */
    const int64_t t0 = esp_timer_get_time();
    while ((s_claimed || s_stuck) && !path_idle() &&
           esp_timer_get_time() - t0 < (int64_t)DISP_DIRECT_LOCKED_WAIT_MS * 1000)
        vTaskDelay(1);
    release_step();
}

/* Event loop, without bsp_display_lock: release, waiting for the bands with the lock dropped (M6), so nothing of the
 * path is left once it returns, unless DISP_DIRECT_DRAIN_MS passed (then the orphan timer frees them when idle). */
static void drain_release(void)
{
    bsp_display_lock(-1);
    disp_direct_release();
    bool more = s_claimed || s_stuck;
    bsp_display_unlock();
    const int64_t t0 = esp_timer_get_time();
    while (more && esp_timer_get_time() - t0 < (int64_t)DISP_DIRECT_DRAIN_MS * 1000) {
        vTaskDelay(1);
        more = !path_idle();
    }
    bsp_display_lock(-1);
    release_step();
    bsp_display_unlock();
}

/* An inactive path (aborted, force-ended) gets ORPHAN_MS from when this timer first sees it so, not from its last band:
 * across a sleep LVGL (and this timer) are paused, and the client's own poll must get the first chance to release. */
static void orphan_cb(lv_timer_t *t)
{
    (void)t;
    if (s_releasing || s_stuck) {   /* I1: finish a release, free a stuck release's bands, once the sender drained */
        release_step();
        return;
    }
    if (!s_claimed) return;
    uint32_t since = s_last_put_ms;
    if (!disp_direct_active()) {
        if (!s_inactive_seen) {
            s_inactive_seen = true;
            s_inactive_ms = now_ms();
        }
        since = s_inactive_ms;
    }
    if ((int32_t)(now_ms() - since) <= ORPHAN_MS) return;
    s_orphans++;
    ESP_LOGW(TAG, "orphan: no band for %d ms, released", ORPHAN_MS);
    disp_direct_release();
}

void disp_direct_set_blocked(bool on)
{
    if (!on) {
        s_blocked = false;
        return;
    }
    if (s_blocked) return;
    bsp_display_lock(-1);
    s_blocked = true;
    bsp_display_unlock();
    drain_release();   /* abort + D1's wait: no band exists once provisioning starts */
}

void disp_direct_display_off(void)
{
    if (!s_claimed && !s_stuck) return;   /* event loop; a claim cannot start now (the path is disabled) */
    drain_release();
}

/* ------------------------------------------------------------ console */
#if CONFIG_OMNI_DEBUG_CONSOLE
void disp_direct_shadow(bool on)
{
    if (on && s_shadow == NULL) {
        s_shadow = heap_caps_calloc((size_t)OMNI_DIAL_W * OMNI_DIAL_W, 2, MALLOC_CAP_SPIRAM);
        if (s_shadow == NULL) ESP_LOGE(TAG, "shadow: no PSRAM");
    }
    s_shadow_on = on && s_shadow != NULL;
    if (!on && !s_claimed && !s_stuck && s_shadow) {   /* the sender may be copying: freed with the bands */
        heap_caps_free(s_shadow);
        s_shadow = NULL;
    }
}

const uint16_t *disp_direct_shadow_frame(void) { return s_shadow_on ? s_shadow : NULL; }

void disp_direct_set_mode(disp_direct_mode_t m) { s_mode = m; }

void disp_direct_set_reserve(uint32_t bytes) { s_reserve = bytes; }

const char *disp_direct_why_name(omni_dd_why_t why)
{
    static const char *const N[] = {"OK", "OFF", "PROV", "DISPLAY", "HIDDEN", "OVERLAY", "RAM", "BACKOFF"};
    return (unsigned)why < sizeof N / sizeof N[0] ? N[why] : "?";
}

void disp_direct_stats(disp_direct_stats_t *s)
{
    memset(s, 0, sizeof *s);
    s->mode = s_mode;
    s->reserve = s_reserve;
    s->claimed = s_claimed;
    s->releasing = s_releasing;
    s->stuck = s_stuck;
    s->active = disp_direct_active();
    s->shadow = s_shadow_on;
    s->claims = s_claims;
    memcpy(s->refusals, s_refusals, sizeof s->refusals);
    s->display_waits = s_display_waits;
    s->backoffs = s_backoffs;
    s->backoff_cause = s_backoff_why;
    taskENTER_CRITICAL(&s_lk);
    s->frames = s_frames;
    s->lost_total = s_lost_total;
    s->lost = s_lost;
    s->frame_us_avg = s_frame_n ? (uint32_t)(s_frame_us_sum / s_frame_n) : 0;
    s->frame_us_max = s_frame_us_max;
    taskEXIT_CRITICAL(&s_lk);
    s->bands = s_bands;
    s->orphans = s_orphans;
    s->drain_timeouts = s_drain_timeouts;
    s->held_bytes = (uint32_t)(s_new_n * BAND_BYTES);
    s->perm_bytes = s_perm_bytes;
    s->stack_hwm = s_sender ? (uint32_t)uxTaskGetStackHighWaterMark(s_sender) : 0;
    for (int i = 0; s_ov && i < DISP_DIRECT_OVERLAYS; i++) {
        const ov_t *o = &s_ov[i];
        s->ov[i].used = o->obj != NULL;
        s->ov[i].shown = o->shown;
        s->ov[i].failed = o->failed;
        s->ov[i].r = o->r;
        s->ov[i].snaps = o->snaps;
        s->ov[i].us_avg = o->snaps ? (uint32_t)(o->us_sum / o->snaps) : 0;
        s->ov[i].us_max = o->us_max;
    }
}
#endif
