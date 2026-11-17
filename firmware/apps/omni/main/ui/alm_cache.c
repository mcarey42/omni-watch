/* The almanac's render worker and bitmap cache (alm_cache.h, R140). */
#include "alm_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "theme.h"
#include "omni/almanac.h"

#define STACK 6144                 /* PSRAM; its high-water mark is logged with each render (review M4) */
#define PRIO 1                     /* under everything but idle: a render waits for any real work */
#define CORE 1
#define MOON_D 88
#define REAP_MS 50                 /* a stopped worker is deleted once it has parked (polled, never waited for) */
#define START_WAIT_TICKS 5         /* a start finding one parked but not yet suspended waits this long at most */

static const char *TAG = "almanac";

/* One job slot per kind. Ownership follows the state: IDLE: nobody's buffer; GO: the worker's (LVGL context filled
 * buf and the parameters first); DONE: back to LVGL context (alm_cache_poll takes it). Every state change is made
 * under s_mux. A worker told to quit frees the buffers of the slots it holds itself, then parks (review I2): stop
 * never waits, and nothing LVGL context still points at is ever freed by the worker. */
typedef enum { SLOT_IDLE, SLOT_GO, SLOT_DONE } slot_state_t;
typedef struct {
    slot_state_t state;
    lv_draw_buf_t *buf;
    uint32_t us, stack_free;
} slot_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static slot_t s_ring_slot, s_moon_slot;
static alm_ring_job_t s_ring_job;              /* read by the worker while the ring slot is GO */
static long s_moon_job_key;
static double s_moon_job_f;
static bool s_quit, s_parked;

/* LVGL context only */
static TaskHandle_t s_task;                    /* the worker serving the face */
static TaskHandle_t s_dying;                   /* a stopped worker not yet parked and deleted */
static lv_timer_t *s_reaper;
static lv_draw_buf_t *s_ring_spare, *s_moon_spare;   /* stale renders' buffers, reused by the next dispatch */
static alm_ring_job_t s_ring_wanted;
static bool s_ring_wanted_ok;
static long s_moon_wanted_key = -1;
static double s_moon_wanted_f;
static lv_draw_buf_t *s_ring, *s_moon;          /* the cache: outlives the face */
static omni_alm_ring_key_t s_ring_key;
static long s_moon_key = -1;

static uint32_t us_since(int64_t t0) { return (uint32_t)(esp_timer_get_time() - t0); }

static void slot_done(slot_t *s, uint32_t us)
{
    const uint32_t free_b = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    portENTER_CRITICAL(&s_mux);
    s->state = SLOT_DONE;
    s->us = us;
    s->stack_free = free_b;
    portEXIT_CRITICAL(&s_mux);
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            /* quit is read before every job: a stop costs the face at most the render already running, and never
             * a wait (review I2) */
            lv_draw_buf_t *drop[2] = {NULL, NULL};
            portENTER_CRITICAL(&s_mux);
            const bool quit = s_quit;
            const bool ring = !quit && s_ring_slot.state == SLOT_GO, moon = !quit && s_moon_slot.state == SLOT_GO;
            if (quit) {   /* the worker's own slots (GO, and DONE ones no poll took): freed here, below */
                if (s_ring_slot.state != SLOT_IDLE) drop[0] = s_ring_slot.buf;
                if (s_moon_slot.state != SLOT_IDLE) drop[1] = s_moon_slot.buf;
                s_ring_slot = s_moon_slot = (slot_t){0};
            }
            portEXIT_CRITICAL(&s_mux);
            if (quit) {
                theme_buf_destroy(drop[0]);
                theme_buf_destroy(drop[1]);
                portENTER_CRITICAL(&s_mux);
                const bool still = s_quit;   /* a start may have taken it back meanwhile */
                if (still) s_parked = true;
                portEXIT_CRITICAL(&s_mux);
                if (still) vTaskSuspend(NULL);   /* deleted by the reaper or the next start */
                break;
            }
            if (ring) {
                const int64_t t0 = esp_timer_get_time();
                const alm_ring_job_t *j = &s_ring_job;
                omni_alm_ring_render((uint16_t *)s_ring_slot.buf->data, j->now, j->lat, j->lon, j->rise_h, j->set_h,
                                     j->set);
                slot_done(&s_ring_slot, us_since(t0));
                continue;
            }
            if (moon) {
                const int64_t t0 = esp_timer_get_time();
                omni_alm_moon_render(s_moon_slot.buf->data, MOON_D, s_moon_job_f);
                slot_done(&s_moon_slot, us_since(t0));
                continue;
            }
            break;
        }
    }
}

static lv_draw_buf_t *buf_new(uint32_t d, lv_color_format_t cf)
{
    lv_draw_buf_t *b = theme_buf_create(d, d, cf);
    if (b && b->header.stride != d * 2) {   /* both renderers write packed rows (and RGB565A8's packed alpha plane) */
        theme_buf_destroy(b);
        b = NULL;
    }
    return b;
}

static slot_state_t state_of(const slot_t *s)
{
    portENTER_CRITICAL(&s_mux);
    const slot_state_t st = s->state;
    portEXIT_CRITICAL(&s_mux);
    return st;
}

static void go(slot_t *s, lv_draw_buf_t *b)
{
    portENTER_CRITICAL(&s_mux);
    s->buf = b;
    s->state = SLOT_GO;
    portEXIT_CRITICAL(&s_mux);
}

/* Hands the worker what is wanted and not cached, one buffer of each kind at a time. */
static void dispatch(void)
{
    if (s_task == NULL) return;
    bool kick = false;
    if (state_of(&s_ring_slot) == SLOT_IDLE && s_ring_wanted_ok &&
        !(s_ring && omni_alm_ring_key_eq(&s_ring_key, &s_ring_wanted.key))) {
        lv_draw_buf_t *b = s_ring_spare ? s_ring_spare : buf_new(OMNI_ALM_BOX, LV_COLOR_FORMAT_RGB565);
        s_ring_spare = NULL;
        if (b) {
            s_ring_job = s_ring_wanted;
            go(&s_ring_slot, b);
            kick = true;
        } else {
            ESP_LOGE(TAG, "no PSRAM for a ring render (largest %u B)",
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
            s_ring_wanted_ok = false;   /* asked again at the face's next minute */
        }
    }
    if (state_of(&s_moon_slot) == SLOT_IDLE && s_moon_wanted_key >= 0 && !(s_moon && s_moon_key == s_moon_wanted_key)) {
        lv_draw_buf_t *b = s_moon_spare ? s_moon_spare : buf_new(MOON_D, LV_COLOR_FORMAT_RGB565A8);
        s_moon_spare = NULL;
        if (b) {
            s_moon_job_key = s_moon_wanted_key;
            s_moon_job_f = s_moon_wanted_f;
            go(&s_moon_slot, b);
            kick = true;
        } else {
            s_moon_wanted_key = -1;
        }
    }
    if (kick) xTaskNotifyGive(s_task);
}

/* LVGL context: deletes a stopped worker once it has parked and suspended; true when there is none left. Parked
 * alone is not enough: the worker sets it, leaves the critical section, then suspends, and deleting it in between
 * (from this core) would free the stack it is still running on (final review, finding 5). */
static bool reap(void)
{
    if (s_dying == NULL) return true;
    portENTER_CRITICAL(&s_mux);
    const bool parked = s_parked;
    portEXIT_CRITICAL(&s_mux);
    if (!parked || eTaskGetState(s_dying) != eSuspended) return false;
    vTaskDeleteWithCaps(s_dying);   /* suspended for good, holding nothing */
    s_dying = NULL;
    return true;
}

static void reaper(lv_timer_t *t)
{
    if (reap()) {
        lv_timer_delete(t);
        s_reaper = NULL;
    }
}

bool alm_cache_start(void)
{
    if (s_task) return true;
    if (s_dying) {
        portENTER_CRITICAL(&s_mux);
        const bool revive = !s_parked;   /* still finishing a render: take it back (it checks quit under s_mux) */
        if (revive) s_quit = false;
        portEXIT_CRITICAL(&s_mux);
        if (revive) {
            s_task = s_dying;
            s_dying = NULL;
        } else {   /* parked: it suspends within a few instructions, unless core 1's higher work holds it off */
            for (int i = 0; !reap() && i < START_WAIT_TICKS; i++) vTaskDelay(1);
        }
        if (s_dying) {   /* still not suspended: the reaper keeps polling; this visit shows the plain ring */
            ESP_LOGW(TAG, "the stopped worker has not suspended yet: no render worker this visit");
            return false;
        }
        if (s_reaper) {
            lv_timer_delete(s_reaper);
            s_reaper = NULL;
        }
        if (s_task) return true;
    }
    portENTER_CRITICAL(&s_mux);
    s_quit = s_parked = false;
    s_ring_slot = s_moon_slot = (slot_t){0};
    portEXIT_CRITICAL(&s_mux);
    if (xTaskCreatePinnedToCoreWithCaps(worker, "almanac", STACK, NULL, PRIO, &s_task, CORE, MALLOC_CAP_SPIRAM) !=
        pdPASS) {
        ESP_LOGE(TAG, "no render worker: the plain ring stays");
        s_task = NULL;
        return false;
    }
    return true;
}

void alm_cache_stop(void)
{
    if (s_task == NULL) return;
    alm_cache_poll(NULL, NULL);   /* a render already finished is kept for the next visit (the images are gone) */
    portENTER_CRITICAL(&s_mux);
    s_quit = true;                /* one in flight is the worker's: it frees it itself, then parks */
    portEXIT_CRITICAL(&s_mux);
    xTaskNotifyGive(s_task);
    s_dying = s_task;
    s_task = NULL;
    if (s_reaper == NULL) s_reaper = lv_timer_create(reaper, REAP_MS, NULL);
    theme_buf_destroy(s_ring_spare);
    theme_buf_destroy(s_moon_spare);
    s_ring_spare = s_moon_spare = NULL;
    s_ring_wanted_ok = false;
    s_moon_wanted_key = -1;
}

lv_draw_buf_t *alm_cache_ring(const omni_alm_ring_key_t *k)
{
    return s_ring && omni_alm_ring_key_eq(&s_ring_key, k) ? s_ring : NULL;
}

lv_draw_buf_t *alm_cache_moon(long *key)
{
    *key = s_moon_key;
    return s_moon;
}

void alm_cache_ring_want(const alm_ring_job_t *j)
{
    s_ring_wanted = *j;
    s_ring_wanted_ok = true;
    dispatch();
}

void alm_cache_ring_keep(const omni_alm_ring_key_t *k)
{
    s_ring_wanted.key = *k;   /* a render in flight for another key is now stale: poll drops it (review M2) */
    s_ring_wanted_ok = alm_cache_ring(k) != NULL;
}

void alm_cache_moon_want(long key, double f)
{
    s_moon_wanted_key = key;
    s_moon_wanted_f = f;
    dispatch();
}

bool alm_cache_busy(void) { return state_of(&s_ring_slot) != SLOT_IDLE || state_of(&s_moon_slot) != SLOT_IDLE; }

/* Takes a DONE slot's buffer back into LVGL context (NULL: not done). */
static lv_draw_buf_t *take(slot_t *s, uint32_t *us, uint32_t *stack_free)
{
    lv_draw_buf_t *b = NULL;
    portENTER_CRITICAL(&s_mux);
    if (s->state == SLOT_DONE) {
        b = s->buf;
        *us = s->us;
        *stack_free = s->stack_free;
        *s = (slot_t){0};
    }
    portEXIT_CRITICAL(&s_mux);
    return b;
}

unsigned alm_cache_poll(lv_obj_t *ring_img, lv_obj_t *moon_img)
{
    unsigned changed = 0;
    uint32_t us = 0, sf = 0;
    lv_draw_buf_t *b = take(&s_ring_slot, &us, &sf);
    if (b) {
        const bool fresh = s_ring_wanted_ok && omni_alm_ring_key_eq(&s_ring_job.key, &s_ring_wanted.key);
        ESP_LOGI(TAG, "ring %u us (worker, stack free %u B)%s", (unsigned)us, (unsigned)sf, fresh ? "" : ", stale");
        if (fresh) {
            lv_draw_buf_t *old = s_ring;
            s_ring = b;
            s_ring_key = s_ring_job.key;
            if (ring_img) {   /* re-pointed before the old buffer goes */
                lv_image_set_src(ring_img, s_ring);
                lv_obj_invalidate(ring_img);
            }
            theme_buf_destroy(old);
            changed |= 1;
        } else {   /* the key moved on (review M2): never shown; its buffer is reused */
            theme_buf_destroy(s_ring_spare);
            s_ring_spare = b;
        }
    }
    b = take(&s_moon_slot, &us, &sf);
    if (b) {
        const bool fresh = s_moon_job_key == s_moon_wanted_key;
        ESP_LOGI(TAG, "moon %u us (worker, stack free %u B)%s", (unsigned)us, (unsigned)sf, fresh ? "" : ", stale");
        if (fresh) {
            lv_draw_buf_t *old = s_moon;
            s_moon = b;
            s_moon_key = s_moon_job_key;
            if (moon_img) {
                lv_image_set_src(moon_img, s_moon);
                lv_obj_invalidate(moon_img);
            }
            theme_buf_destroy(old);
            changed |= 2;
        } else {
            theme_buf_destroy(s_moon_spare);
            s_moon_spare = b;
        }
    }
    dispatch();
    return changed;
}
