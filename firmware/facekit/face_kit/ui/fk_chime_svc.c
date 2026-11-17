/* fk_chime_svc (fk_chime_svc.h; from the first theme's wave 1 Task 16, its spec section 13, R136 item 8, B3, B4, C10,
 * H7; face_kit since rd wave 1 Task 2, the theme's plan by callback).
 *
 * While the theme's chime is on, one esp_timer one-shot (ESP_TIMER_TASK dispatch; an esp_timer alarm wakes the CPU from
 * light sleep) is armed for fk_chime_arm_ms: never more than 60 s, so a clock first set or stepped is seen within a
 * minute (B4). At boot the first tick (fk_chime_svc_sync from after_store) runs before watch_main's sfx_config: it
 * passes sound_pending (sfx_config_known) and gets FK_CH_BOOT, which marks nothing and re-arms in a second (M2). Each
 * fire builds the context from the sound mirror (sfx_config_get), the theme's snapshot (its ctx: the chime and the
 * quiet window), net_provisioning() and the clock, never settings() (R27), runs fk_chime_sched_tick and, on PLAY,
 * calls the theme's play with the verdict at its gain. It never touches the panel: asleep, the chime sounds without
 * waking it. FK_CH_ERROR does nothing and re-arms (the scheduler's cap). The state (fk_chime_state_t) is never reset at
 * a sleep or a wake; only an offset change zeroes it (fk_chime_sched_at).
 *
 * Synchronisation: s_lock (a static mutex, created on the boot task by the first sync, before the event loop and the
 * console run) guards s_timer and s_sched. The timer's callback (esp_timer task), sync (boot task, event loop),
 * play_now and the console's `chime at` and `chime` (console task) take it for their whole step. The callback never
 * blocks: it tries the lock and, if a holder has it, re-arms a 10 ms retry and returns (fire()); esp_timer frees a
 * deleted timer later on its own task, after any running callback. The fire path logs nothing (no vprintf on the
 * esp_timer task's 3 584 B stack); the theme's play keeps its arrays off that stack. The picture's inputs (the start
 * and the verdict played) are published under a spinlock (s_pub) for the faces (LVGL task). */
#include "fk_chime_svc.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "sfx.h"
#include "omni/timefmt.h"

static const char *TAG = "fk_chime";

static const fk_chime_theme_t *s_theme;   /* fk_chime_svc_init's, before the first sync */
static StaticSemaphore_t s_lock_mem;
static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_timer;   /* s_lock; NULL while the chime is off */
static fk_chime_sched_t s_sched;     /* s_lock */
static int64_t s_due_us;             /* s_lock: when the timer fires (esp_timer us); 0 not armed */
static uint32_t s_fires, s_plays;    /* s_lock */
static uint32_t s_unplayed;          /* s_lock: plays the theme's play did not sound (an asset missing, refused) */
static uint32_t s_retries;           /* atomic: fires that found s_lock held and re-armed RETRY_MS */
#define RETRY_MS 10

static portMUX_TYPE s_pub = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_started_us;         /* s_pub */
static fk_chime_verdict_t s_played;  /* s_pub: the verdict that sounded at s_started_us */

/* ---- under s_lock */
static void arm(uint32_t ms)
{
    s_due_us = 0;
    if (!s_timer) return;
    esp_timer_stop(s_timer);   /* not armed: ESP_ERR_INVALID_STATE, harmless */
    if (ms && esp_timer_start_once(s_timer, (uint64_t)ms * 1000) == ESP_OK) s_due_us = esp_timer_get_time() + ms * 1000LL;
}

/* the theme's settings snapshot (chime_on, the quiet window); zeros without a theme */
static fk_chime_ctx_t theme_ctx(void)
{
    fk_chime_ctx_t c;
    memset(&c, 0, sizeof c);
    if (s_theme && s_theme->ctx) s_theme->ctx(&c);
    return c;
}

static bool play(const fk_chime_verdict_t *v)
{
    if (!s_theme || !s_theme->play || !s_theme->play(v, v->gain_pct)) {   /* it did not sound (counted) */
        s_unplayed++;
        return false;
    }
    s_plays++;
    taskENTER_CRITICAL(&s_pub);
    s_started_us = esp_timer_get_time();
    s_played = *v;
    taskEXIT_CRITICAL(&s_pub);
    return true;
}

static void tick(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    const int64_t now = (int64_t)tv.tv_sec;
    bool on = false;
    uint8_t vol = 0;
    const bool pending = !sfx_config_known();   /* M2: read first (acquire), then the mirror */
    sfx_config_get(&on, &vol);
    const fk_chime_ctx_t t = theme_ctx();
    const fk_chime_ctx_t c = {
        .chime_on = t.chime_on,
        .sound_on = on,
        .time_set = omni_time_is_set(now),
        .provisioning = net_provisioning(),
        .volume = vol,
        .quiet_from_h = t.quiet_from_h,
        .quiet_to_h = t.quiet_to_h,
        .sound_pending = pending,   /* the boot's first tick runs before sfx_config (watch_main): FK_CH_BOOT, 1 s on */
    };
    uint32_t ms = fk_chime_sched_tick(&s_sched, now, &c);
    if (s_sched.last.why == FK_CH_PLAY) play(&s_sched.last);
    /* the scheduler counts whole seconds from now's second: land just past the boundary, not the fraction after it */
    const uint32_t frac = (uint32_t)(tv.tv_usec / 1000);
    if (ms >= 1000 && ms < FK_CHIME_ARM_CAP_MS) ms = ms - frac + 2;
    arm(ms);
}

/* The esp_timer task never blocks here: with s_lock held elsewhere (a sync, play_now, `chime at`, `chime`), the fire
 * re-arms RETRY_MS and returns. s_timer is read without the lock: a handle being deleted is still valid memory until
 * this callback returns (esp_timer frees it later, on this same task), and esp_timer_start_once checks under its list
 * lock, so it refuses a timer whose deletion is queued (armed) and a stop/delete that follows our start removes it
 * (sync retries the delete). NULL: stopped, nothing to do. */
static void fire(void *arg)
{
    (void)arg;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) {
        const esp_timer_handle_t t = __atomic_load_n(&s_timer, __ATOMIC_ACQUIRE);
        if (t) {
            __atomic_fetch_add(&s_retries, 1u, __ATOMIC_RELAXED);
            esp_timer_start_once(t, RETRY_MS * 1000);
        }
        return;
    }
    if (s_timer) {
        s_fires++;
        tick();
    }
    xSemaphoreGive(s_lock);
}

/* ---- the API */
void fk_chime_svc_init(const fk_chime_theme_t *t) { s_theme = t; }

void fk_chime_svc_sync(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lock_mem);
    const bool want = theme_ctx().chime_on;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (want && !s_timer) {
        const esp_timer_create_args_t a = {.callback = fire, .dispatch_method = ESP_TIMER_TASK, .name = "fk_chime"};
        esp_timer_handle_t t = NULL;
        if (esp_timer_create(&a, &t) == ESP_OK) {
            __atomic_store_n(&s_timer, t, __ATOMIC_RELEASE);
            ESP_LOGI(TAG, "on (the sound %u ms)", s_theme ? (unsigned)s_theme->len_ms : 0u);
            tick();
        } else {
            s_timer = NULL;
            ESP_LOGE(TAG, "no timer: the chime stays silent");
        }
    } else if (!want && s_timer) {
        const esp_timer_handle_t t = s_timer;
        __atomic_store_n(&s_timer, NULL, __ATOMIC_RELEASE);   /* a fire's retry now finds none */
        /* freed on the esp_timer task, after any running callback; a retry armed between our stop and delete makes
         * the delete refuse (armed): stop and delete again */
        esp_err_t err = ESP_ERR_INVALID_STATE;
        for (int i = 0; i < 4 && err == ESP_ERR_INVALID_STATE; i++) {
            esp_timer_stop(t);
            err = esp_timer_delete(t);
        }
        if (err != ESP_OK) ESP_LOGE(TAG, "the timer was not deleted: %s", esp_err_to_name(err));
        s_due_us = 0;
        ESP_LOGI(TAG, "off: no timer");
    }
    xSemaphoreGive(s_lock);
}

int64_t fk_chime_started_us(void)
{
    taskENTER_CRITICAL(&s_pub);
    const int64_t t = s_started_us;
    taskEXIT_CRITICAL(&s_pub);
    return t;
}

void fk_chime_svc_fill(fk_face_in_t *in)
{
    in->chime = in->thrum = in->beep = in->click = 0;
    taskENTER_CRITICAL(&s_pub);
    const int64_t t0 = s_started_us;
    const fk_chime_verdict_t v = s_played;
    taskEXIT_CRITICAL(&s_pub);
    if (t0 == 0 || !s_theme || !s_theme->env) return;
    const int64_t e = (esp_timer_get_time() - t0) / 1000;
    if (e < 0 || e > (int64_t)s_theme->len_ms + 1000) return;
    bool on = false;
    uint8_t vol = 0;
    sfx_config_get(&on, &vol);
    if (!on || vol == 0) return;   /* sound off or volume 0 cut it: the picture stops too */
    s_theme->env((uint32_t)e, &v, in);
}

bool fk_chime_svc_play_now(void)
{
    if (!s_lock || !s_theme) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const fk_chime_ctx_t c = theme_ctx();
    struct timeval tv;
    gettimeofday(&tv, NULL);
    const time_t now = (time_t)tv.tv_sec;
    struct tm tm;
    const int hour = localtime_r(&now, &tm) ? tm.tm_hour : 0;
    const fk_chime_verdict_t v = {
        .why = FK_CH_PLAY, .pings = fk_chime_pings(hour),
        .gain_pct = fk_chime_quiet(hour, c.quiet_from_h, c.quiet_to_h) ? FK_CHIME_QUIET_PCT : 100, .next_s = 0,
    };
    const bool ok = play(&v);
    xSemaphoreGive(s_lock);
    return ok;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
static const char *why_name(fk_chime_why_t w)
{
    static const char *const N[] = {"WAIT", "PLAY", "LATE", "UNSET", "PROV", "SILENT", "OFF", "ERROR", "BOOT"};
    return (unsigned)w < sizeof N / sizeof N[0] ? N[w] : "?";
}

void fk_chime_svc_print(const char *tag)
{
    if (!s_lock) {
        printf("%s: not started\n", tag);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool timer = s_timer != NULL;
    const fk_chime_sched_t s = s_sched;
    const int64_t due = s_due_us;
    const uint32_t fires = s_fires, plays = s_plays, unplayed = s_unplayed;
    xSemaphoreGive(s_lock);
    const bool chime_on = theme_ctx().chime_on;
    const int64_t now_us = esp_timer_get_time(), started = fk_chime_started_us();
    char next[24] = "none";
    if (timer && s.last.next_s) {   /* the boundary on the shifted clock (none while off: the last plan is stale) */
        const time_t t = (time_t)s.last.next_s;
        struct tm tm;
        if (localtime_r(&t, &tm)) snprintf(next, sizeof next, "%02d:%02d", tm.tm_hour, tm.tm_min);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        const size_t l = strlen(next);
        snprintf(next + l, sizeof next - l, " in %lld s", (long long)(s.last.next_s - s.off_s - (int64_t)tv.tv_sec));
    }
    TaskHandle_t et = xTaskGetHandle("esp_timer");
    printf("%s %s, timer %s; next boundary %s; offset %+lld s; last %s (pings %u, gain %u%%); state %lld; "
           "fires %lu, plays %lu; sound %lu ms; last start %s%lld ms ago; esp_timer stack free %u B\n", tag,
           chime_on ? "on" : "off", timer ? (due ? "armed" : "idle") : "none", next,
           (long long)s.off_s, why_name(s.last.why), s.last.pings, s.last.gain_pct, (long long)s.st.last_b, (unsigned long)fires, (unsigned long)plays,
           s_theme ? (unsigned long)s_theme->len_ms : 0ul, started ? "" : "never ",
           started ? (long long)((now_us - started) / 1000) : 0LL, et ? (unsigned)uxTaskGetStackHighWaterMark(et) : 0u);
    printf("%s not played %lu, lock retries %lu\n", tag, (unsigned long)unplayed,
           (unsigned long)__atomic_load_n(&s_retries, __ATOMIC_RELAXED));
    if (timer && due) printf("%s timer fires in %lld ms\n", tag, (long long)((due - now_us) / 1000));
}

bool fk_chime_svc_at(uint32_t n_s)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    const bool ok = fk_chime_sched_at(&s_sched, (int64_t)tv.tv_sec, n_s);
    if (ok && s_timer) tick();   /* the real path from now: the hour before is LATE, the timer armed for N s */
    xSemaphoreGive(s_lock);
    return ok;
}
#endif
