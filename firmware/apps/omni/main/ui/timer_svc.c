/* The one timer, its alarm and its bell (timer_svc.h). The decisions are omni_core's (omni/tmr_alarm.h, host-tested);
 * this file carries them out. */
#include "timer_svc.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "shell.h"
#include "omni/tmr_alarm.h"
#include "omni_faces.h"
#include "omni_sfx.h"

#define POLL_MS 250

static const char *TAG = "tmr";
static omni_tmr_t s_t;
static omni_tmr_bell_t s_bell;
static esp_timer_handle_t s_alarm;
/* Shared with the one-shot's callback (esp_timer task); written only by the LVGL side, except s_fire_gen:
 *  - s_mode, s_end_ms: copies of the machine (the touch claim reads s_mode too, INPUT task);
 *  - s_gen: bumped by every change of what the one-shot should do (tmr_svc_changed, a DONE seen on the face);
 *  - s_fire_gen: the generation the armed one-shot belongs to (written by whichever task arms it);
 *  - s_unhandled: the DONE has not yet been seen on the face (the callback's retries stop when it is).
 * m2: a callback whose generation is stale never posts; it arms the real end instead. One that fires before the end
 * (a retry that raced a change) also only arms the end. So no post is ever early, and the end is always armed. */
static volatile omni_tmr_mode_t s_mode = OMNI_TMR_IDLE;
static volatile uint32_t s_end_ms, s_gen, s_fire_gen;
static volatile bool s_unhandled;
static uint32_t s_fails;        /* the callback's failed posts in a row */
static bool s_face_awake;       /* the timer face is shown and awake */
static bool s_polled;
static uint32_t s_last_poll;
#if CONFIG_OMNI_DEBUG_CONSOLE
static uint32_t s_postfail;     /* `tmr postfail <n>`: the next n posts fail (Task 9 drives F5's paths); atomics */
#endif

uint32_t tmr_svc_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
omni_tmr_t *tmr_svc(void) { return &s_t; }
omni_tmr_mode_t tmr_svc_mode(void) { return s_mode; }

/* Any task: shell_post, or a failure the console asked for. */
static bool post(int code, int arg)
{
#if CONFIG_OMNI_DEBUG_CONSOLE
    uint32_t n = __atomic_load_n(&s_postfail, __ATOMIC_SEQ_CST);
    while (n > 0)
        if (__atomic_compare_exchange_n(&s_postfail, &n, n - 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return false;
#endif
    return shell_post(code, arg);
}

/* Any task: the one-shot for generation gen, us from now. Stopped first (a start on an armed timer fails); a start that
 * still fails (the other task armed it in between) is tried once more. */
static void arm(uint32_t gen, uint64_t us)
{
    s_fire_gen = gen;
    for (int i = 0; i < 2; i++) {
        esp_timer_stop(s_alarm);   /* ESP_ERR_INVALID_STATE when not armed: harmless */
        if (esp_timer_start_once(s_alarm, us) == ESP_OK) return;
    }
    ESP_LOGE(TAG, "alarm not armed");
}

static uint64_t us_until(uint32_t end)
{
    const int32_t d = (int32_t)(end - tmr_svc_now_ms());
    return d > 0 ? (uint64_t)d * 1000u : 0;
}

#define CB_REARMS 4                /* re-arms in one call, each raced by a fresh change (a bound, never reached) */

/* esp_timer task: only posts and re-arms itself (R26). */
static void alarm_cb(void *arg)
{
    (void)arg;
    uint32_t gen = s_gen;
    omni_tmr_mode_t mode = s_mode;
    /* stale or early: re-arm only. A change (tmr_svc_changed: mode and end, then s_gen) landing while this arms could
     * have its own, earlier arming replaced by this one's older end (final review, finding 4): so after each arm s_gen
     * is read again, and a moved one means arming again from what the change wrote. */
    for (int i = 0; s_fire_gen != gen || (mode == OMNI_TMR_COUNT && us_until(s_end_ms) > 0); i++) {
        if (mode == OMNI_TMR_COUNT) arm(gen, us_until(s_end_ms));   /* the real end, no post */
        else if (mode == OMNI_TMR_DONE && s_unhandled) arm(gen, 0); /* DONE unseen: post it now */
        else return;
        if (s_gen == gen || i + 1 >= CB_REARMS) return;
        gen = s_gen;   /* re-checked from the top: a change whose end has come falls through to the post */
        mode = s_mode;
    }
    if (mode != OMNI_TMR_COUNT && !(mode == OMNI_TMR_DONE && s_unhandled)) return;
    const bool ok = post(UI_ALARM, FACE_TIMER);
    s_fails = ok ? 0 : s_fails < UINT32_MAX ? s_fails + 1 : s_fails;
    const uint32_t us = omni_tmr_retry_us(ok, s_fails);   /* F5, m3: never given up while it fails */
    if (us && s_gen == gen) arm(gen, us);
}

static void disarm(void)
{
    s_gen++;
    if (s_alarm) esp_timer_stop(s_alarm);
}

void tmr_svc_changed(void)
{
    s_end_ms = s_t.end_ms;
    s_mode = s_t.mode;
    disarm();
    s_fails = 0;
    uint32_t end;
    if (s_alarm && omni_tmr_alarm_at(&s_t, &end)) arm(s_gen, us_until(end));   /* an end <= now arms 0 */
    if (s_t.mode != OMNI_TMR_DONE) {
        s_unhandled = false;
        if (omni_tmr_bell_silence(&s_bell)) sfx_stop();   /* dismissed or reset */
    }
}

void tmr_svc_poll(void)
{
    const uint32_t now = tmr_svc_now_ms();
    const bool woke = omni_tmr_woke(s_polled, s_last_poll, now);
    s_polled = true;
    s_last_poll = now;
    const bool stepped = omni_tmr_tick(&s_t, now);   /* the step to DONE (also one a tap at the end made) */
    if (stepped) s_unhandled = true;
    s_mode = s_t.mode;
    omni_tmr_bell_out_t o;
    omni_tmr_bell_poll(&s_bell, s_t.mode, s_t.done_ms, stepped, s_face_awake, woke, now, &o);
    if (o.ring) sfx_play(SFX_ALARM);   /* dropped while the sound is off, at volume 0 or blocked (the QR) */
    if (o.silence) sfx_stop();
    if (o.keep) post(UI_ALARM, UI_ALARM_KEEP);   /* activity: the watch stays awake while it rings */
    if (o.handled_now) {
        s_unhandled = false;
        disarm();   /* the callback's retries stop */
    }
    if (o.post_alarm && s_alarm) {   /* F5: a wake with the DONE unseen; the one-shot posts it, retrying as it must */
        s_fails = 0;
        arm(s_gen, 0);
    }
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    tmr_svc_poll();
}

void tmr_svc_face_awake(bool on)
{
    s_face_awake = on;
    if (on) tmr_svc_poll();                              /* an alarm's wake: DONE and the bell before the first frame */
    else if (omni_tmr_bell_silence(&s_bell)) sfx_stop(); /* leaving the face, the glance, sleep */
}

#if CONFIG_OMNI_DEBUG_CONSOLE
void tmr_svc_debug_postfail(uint32_t n) { __atomic_store_n(&s_postfail, n, __ATOMIC_SEQ_CST); }
uint32_t tmr_svc_debug_postfail_left(void) { return __atomic_load_n(&s_postfail, __ATOMIC_SEQ_CST); }
#endif

void tmr_svc_init(void)
{
    omni_tmr_init(&s_t);
    s_mode = s_t.mode;
    const esp_timer_create_args_t args = {.callback = alarm_cb, .name = "tmr_alarm"};
    if (esp_timer_create(&args, &s_alarm) != ESP_OK) {
        s_alarm = NULL;
        ESP_LOGE(TAG, "no alarm timer: a countdown rings only while the watch is awake");
    }
    if (lv_timer_create(poll_cb, POLL_MS, NULL) == NULL) ESP_LOGE(TAG, "no poll timer");
}
