#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "omni/batrec.h"
#include "omni/clockcal.h"
#include "omni/join.h"
#include "omni/nav.h"
#include "omni/nav_alarm.h"
#include "omni/prov_session.h"
#include "omni/settings.h"
#include "omni/sync.h"
#include "omni/timefmt.h"
#include "omni/version.h"
#include "board.h"
#include "power.h"
#include "omni_events.h"
#include "debug_console.h"
#include "watch_theme.h"
#include "store.h"
#include "net.h"
#include "sfx.h"
#include "shell.h"
#include "disp_stats.h"
#include "disp_direct.h"
#include "theme.h"
#include "assets.h"
#include "scr_provision.h"
#include "scr_netlist.h"
#include "scr_keyboard.h"

static const char *TAG = "app";

static const watch_theme_t *s_theme;   /* set once, first thing in watch_main */

const watch_theme_t *watch_theme(void) { return s_theme; }

static omni_pwr_sm_t s_pwr;        /* owned by the event loop task */
static omni_nav_t s_nav;           /* owned by the event loop task; the shell reads it under the LVGL lock */
static omni_sync_t s_sync;         /* owned by the event loop; RAM only (kept through sleep, not reboots) */
static omni_batrec_t s_rec;        /* battery session record: owned by the event loop (R27); NVS via store */
static omni_clockcal_t s_clk;      /* clock calibration: owned by the event loop (R27); NVS via store */
static bool s_vbus;                /* VBUS as the event loop last saw it */
static bool s_ui;                  /* display up, shell running */
static bool s_loud;                /* the sync in flight reports a failure on screen (boot / sync now) */
static bool s_dimmed;              /* the provisioning screen is idle-dimmed */
static bool s_prov_pending;        /* a provisioning start was asked for; its first state has not arrived */
static bool s_prov_resume;         /* unprovisioned: the session stopped as the watch slept on the QR; restart at wake */
static bool s_prov_had_saved;      /* a network was saved when the current phone session was asked for (spec 7.6) */
static TaskHandle_t s_loop_task;
static volatile bool s_hold_awake; /* console `awake hold`: the tick runs no glance/sleep timeout (RAM only) */
static volatile uint32_t s_hold_since_ms; /* when it was set: it lapses after OMNI_AWAKE_HOLD_MAX_MS (R66) */

#define BATREC_CHECKPOINT_S 1800   /* coarse checkpoint while on battery, so a brown-out still leaves data */
#define STATUS_PERIOD_S 10         /* battery is read over I2C: refresh the face's status this often */
#define PROV_SLEEP_S 170           /* QR screen: glance (dimmed) at 170 s, panel off 10 s later = 3 min (prov_session.h) */
#define PROV_DIM_MS 60000          /* QR screen dims when idle this long */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

#if CONFIG_OMNI_DEBUG_CONSOLE
/* Console task: one flag store; the event loop reads it at each tick. Cleared by a reboot. */
/* Console task. The event loop also clears the flag (R66 expiry); since is written first, and a hold set just after
 * the tick sampled its time reads as not expired (omni_hold_expired is signed). */
void app_hold_awake(bool hold)
{
    s_hold_since_ms = now_ms();
    s_hold_awake = hold;
}

bool app_awake_hold(uint32_t *held_ms)
{
    const bool h = s_hold_awake;
    *held_ms = now_ms() - s_hold_since_ms;
    return h;
}
#endif

/* Shell calls from this task take the LVGL lock; power_apply is always called without it (R26). */
static void ui_glance(bool on)
{
    if (on) sfx_stop(); /* the glance is quiet */
    if (!s_ui) return;
    bsp_display_lock(-1);
    if (on) scr_keyboard_mask();   /* spec 6.5: the glance and every wake start masked */
    shell_set_glance(on);
    bsp_display_unlock();
}

/* The face's wake/sleep hooks (shell_wake / shell_sleep), under the lock and never round power_apply (R26). */
static void ui_wake(bool reveal)
{
    if (!s_ui) return;
    bsp_display_lock(-1);
    scr_keyboard_mask();   /* every wake (full or skipping the reveal) starts masked */
    shell_wake(reveal);
    bsp_display_unlock();
}

static void ui_sleep(void)
{
    sfx_stop(); /* shell_sleep's path: nothing plays asleep */
    if (!s_ui) return;
    bsp_display_lock(-1);
    shell_sleep();
    bsp_display_unlock();
}

static void ui_status(void)
{
    if (!s_ui) return;
    shell_status_t st = {.battery_pct = board_battery_percent(), .charging = board_charging(), .sync = s_sync};
    bsp_display_lock(-1);
    shell_set_status(&st);
    bsp_display_unlock();
}

omni_sync_t app_sync(void) { return s_sync; }
omni_batrec_t app_batrec(void) { return s_rec; }
omni_clockcal_t app_clockcal(void) { return s_clk; }
unsigned app_events_stack_hwm(void) { return s_loop_task ? (unsigned)uxTaskGetStackHighWaterMark(s_loop_task) : 0; }


static omni_bsnap_t rec_snap(void)
{
    int64_t now = (int64_t)time(NULL);
    return omni_batrec_snap(&s_rec, omni_time_is_set(now) ? now : 0, (uint32_t)(esp_timer_get_time() / 1000000),
                            board_battery_percent(), board_vbat_mv());
}

static void rec_save(void)
{
    esp_err_t err = store_batrec_save(&s_rec);
    if (err != ESP_OK) ESP_LOGE(TAG, "battery record save: %s", esp_err_to_name(err));
}

/* VBUS edge: closes or opens a session right now, before charging moves the %. Called from the tick (awake),
 * EV_VBUS (power.c saw it while asleep) and each wake, so the edge is seen whichever state the watch is in. */
/* The clock calibration follows the power source: on battery (light sleep) an unlearned rate is sampled densely. */
static void clock_power_source(bool vbus)
{
    if (vbus) {
        omni_clk_on_usb(&s_clk);
        return;
    }
    omni_clk_on_battery(&s_clk);
    omni_sync_set_period(&s_sync, (int32_t)omni_clk_interval_s(&s_clk));
}

static void rec_vbus_check(void)
{
    bool v = board_vbus_present();
    if (v == s_vbus) return;
    s_vbus = v;
    if (omni_batrec_vbus(&s_rec, v, rec_snap())) rec_save();
    clock_power_source(v);
    ESP_LOGI("batrec", "VBUS %s: %d%% %d mV", v ? "plugged" : "unplugged", board_battery_percent(), board_vbat_mv());
}

static void rec_checkpoint(void)
{
    /* The due test is pure uptime arithmetic; the PMU is read only when a checkpoint is actually taken. */
    if (omni_batrec_checkpoint_due(&s_rec, (uint32_t)(esp_timer_get_time() / 1000000), BATREC_CHECKPOINT_S) &&
        omni_batrec_checkpoint(&s_rec, rec_snap(), BATREC_CHECKPOINT_S))
        rec_save();
}

/* Shows nav's top. A pushed screen the shell has no view for is popped at once, so nav never thinks a screen
 * is up while the face is drawn. */
static void ui_show_top(void)
{
    if (!s_ui) return;
    bsp_display_lock(-1);
    bool ok = shell_show_top();
    bsp_display_unlock();
    if (!ok && omni_nav_pop(&s_nav) == NAV_SCREEN_POPPED) ui_show_top();
}

static void ui_nav(omni_nav_effect_t eff)
{
    if (eff == NAV_FACE_CHANGED) store_face_commit(s_nav.face_idx);   /* t_<id>/local, 2 s debounced (R27: here) */
    if (!s_ui) return;
    if (eff == NAV_SCREEN_PUSHED || eff == NAV_SCREEN_POPPED) {
        s_dimmed = false;
        ui_show_top();
        return;
    }
    if (eff == NAV_REPLAY_REVEAL) { /* BOOT double */
        ui_wake(true);
        return;
    }
    if (eff != NAV_FACE_CHANGED && eff != NAV_TRANSIENT) return;
    bsp_display_lock(-1);
    shell_show_current();
    shell_show_top(); /* a pick from the picker also closed it */
    if (eff == NAV_FACE_CHANGED) shell_show_pips(s_nav.face_idx, settings()->carousel_len);   /* not for a transient */
    bsp_display_unlock();
}

/* A theme's lift face (omni_nav_lift moved face_idx): shown as a transient view, so face_idx is not committed (no
 * t_<id>/local write per lift) and no pips cover the reveal (the Task 10 review's m1). A swipe from it commits as usual.
 * The face is on top (omni_nav_lift's guard), so no screen needs closing. */
static void ui_show_lift(void)
{
    if (!s_ui) return;
    bsp_display_lock(-1);
    shell_show_current();
    bsp_display_unlock();
}

/* A pick (UI_PICK, UI_ALARM): as ui_nav, and it saves the shown face even when it was already shown, if a transient
 * lift showed it unsaved (omni_nav_pick_saves; the Task 11 review's M4). */
static void ui_pick(omni_nav_effect_t eff)
{
    if (eff != NAV_FACE_CHANGED && omni_nav_pick_saves(eff, s_nav.face_idx, store_face()))
        store_face_commit(s_nav.face_idx);   /* t_<id>/local, 2 s debounced (R27: here) */
    ui_nav(eff);
}

static void ui_refresh(void)
{
    if (!s_ui) return;
    bsp_display_lock(-1);
    shell_refresh();
    bsp_display_unlock();
}

/* UI_SET_CAROUSEL's mailbox (spec 8.2): the editor (LVGL task) or the console writes the whole order here, then posts. A
 * later write before the loop takes it replaces it, so the loop applies the latest order. */
static omni_carousel_t s_car_box;
static portMUX_TYPE s_car_lock = portMUX_INITIALIZER_UNLOCKED;

bool ui_carousel_post(const omni_carousel_t *c)
{
    taskENTER_CRITICAL(&s_car_lock);
    s_car_box = *c;
    taskEXIT_CRITICAL(&s_car_lock);
    return shell_post(UI_SET_CAROUSEL, 0);
}

/* The order from the mailbox: checked by omni_local_validate on a static copy (off this task's stack, Minor 18); one it
 * would change is ignored and logged. face_idx follows the saved face (c.face, never a transient one: Task 7's
 * omni_carousel_face_idx gives 0 for a face not in the order). A transient face stays shown as it was (nav's transient is
 * untouched: a swipe from it goes to the new neighbours of the saved face). Saved through the existing 2 s debounce (RAM
 * only on a read-only boot); the face under the screens follows (shell_show_current re-creates it only when it changed). */
static void set_carousel(void)
{
    static omni_local_t chk;   /* every field set below but its tail, which stays empty */
    omni_carousel_t c;
    taskENTER_CRITICAL(&s_car_lock);
    c = s_car_box;
    taskEXIT_CRITICAL(&s_car_lock);
    chk.reveal_style = settings()->reveal_style;
    chk.carousel_len = c.len;
    memcpy(chk.carousel, c.ids, sizeof chk.carousel);
    chk.face_idx = omni_carousel_face_idx(&c);
    if (!omni_local_validate(&chk, &s_theme->limits)) {
        ESP_LOGW(TAG, "carousel ignored (len %u, face %u): not a valid order", c.len, c.face);
        return;
    }
    omni_settings_t *s = settings_edit();
    memcpy(s->carousel, chk.carousel, sizeof s->carousel);
    s->carousel_len = chk.carousel_len;
    s_nav.face_idx = chk.face_idx;
    settings_commit();
    store_face_commit(s_nav.face_idx);
    ui_show_lift();   /* the face under the screens: re-created only if the saved one changed (else just refreshed) */
}

static void open_settings(void);

static void open_screen(omni_screen_t scr)
{
    if (omni_nav_top(&s_nav) == scr) return;
    if (scr == SCR_SETTINGS) {   /* in the picker's place, never a second copy (omni_nav_open_settings) */
        open_settings();
        return;
    }
    ui_nav(omni_nav_push(&s_nav, scr));
}

/* UI_SET_*: the one writer of the settings (R27). settings_commit applies TZ and brightness at once (R26: from
 * here, never from LVGL) and saves 2 s later; the screen on top then redraws with the applied value. */
/* A brightness drag posts a value per 5 % step; each commit pauses LVGL to send the panel command. Only the
 * latest is applied: it waits while more events are queued (at most BRIGHT_HOLD_MS), see event_loop. */
#define BRIGHT_HOLD_MS 120
static int s_bright_pending = -1;
static uint32_t s_bright_since;
static unsigned s_bright_steps; /* steps folded into the pending one */

static void commit_setting(int code, int v);

static void brightness_flush(bool force)
{
    if (s_bright_pending < 0) return;
    if (!force && uxQueueMessagesWaiting(omni_events()) > 0 && now_ms() - s_bright_since < BRIGHT_HOLD_MS) return;
    int v = s_bright_pending;
    s_bright_pending = -1;
    ESP_LOGI(TAG, "brightness %d (%u step%s)", v, s_bright_steps, s_bright_steps == 1 ? "" : "s");
    s_bright_steps = 0;
    commit_setting(UI_SET_BRIGHTNESS, v);
}

static void apply_setting(int code, int v)
{
    if (code == UI_SET_BRIGHTNESS) { /* defer: a later step may already be queued */
        if (s_bright_pending < 0) s_bright_since = now_ms();
        s_bright_pending = v;
        s_bright_steps++;
        return;
    }
    commit_setting(code, v);
}

static void commit_setting(int code, int v)
{
    omni_settings_t *s = settings_edit();
    switch (code) {
    case UI_SET_CITY: s->city = (uint16_t)v; break;
    case UI_SET_BRIGHTNESS: s->brightness = (uint8_t)v; break;
    case UI_SET_SLEEP: s->sleep_s = (uint8_t)v; break;
    case UI_SET_H24: s->h24 = v != 0; break;
    case UI_SET_SOUND: s->sound_on = v != 0; break;
    case UI_SET_VOLUME: s->volume = (uint8_t)v; break;
    case UI_SET_REVEAL: s->reveal_style = (uint8_t)v; break;
    case UI_SET_MOTION: s->reduced_motion = v != 0; break;
    default: return;
    }
    settings_commit();
    if (code == UI_SET_SOUND || code == UI_SET_VOLUME) sfx_config(s->sound_on, s->volume);
    ui_refresh();
}

/* ------------------------------------------------------------ network: provisioning screen and time sync */
/* v: an EV_NET value (state | NET_EV_* flags). qr_pending: a new session's PoP is not there yet (blank plate). */
static void ui_prov(int v, bool qr_pending)
{
    if (!s_ui) return;
    scr_prov_info_t i = {.state = NET_EV_STATE(v), .prov = (v & NET_EV_PROV) != 0,
                         .offer_forget = (v & NET_EV_FORGET) != 0, .last_ok = s_sync.last_ok,
                         .next_try = s_sync.next_try, .had_saved = s_prov_had_saved};
    net_ssid(i.ssid);
    strlcpy(i.name, net_prov_name(), sizeof i.name);
    if (!qr_pending) net_qr_json(i.qr, sizeof i.qr);
    bsp_display_lock(-1);
    scr_provision_set(&i);
    bsp_display_unlock();
}

static void show_provision(void)
{
    if (omni_nav_top(&s_nav) != SCR_PROVISION) ui_nav(omni_nav_push(&s_nav, SCR_PROVISION));
}

/* A new session (or back to the QR of the running one). The QR stays blank until the net task has made the
 * session's PoP (EV_NET PROV_WAITING). replace: the QR takes the top screen's place (the list's "Use your phone", B1)
 * instead of being pushed. */
static void start_provisioning_at(bool replace)
{
    disp_direct_set_blocked(true);   /* first: no direct band exists once provisioning starts (D2, R118) */
    s_prov_resume = false;
    s_prov_pending = !net_provisioning();
    if (s_prov_pending) s_prov_had_saved = net_is_provisioned();   /* before the session can change it */
    ui_prov(NET_PROV_WAITING | NET_EV_PROV, s_prov_pending);
    if (replace) ui_nav(omni_nav_replace(&s_nav, SCR_PROVISION));
    else show_provision();
    if (!net_start_provisioning()) s_prov_pending = false;
}

static void start_provisioning(void) { start_provisioning_at(false); }

/* The wearer left the QR screen: the session ends (omni_prov_stop_on decides on the net task). */
static void prov_left(void)
{
    s_prov_resume = false;
    if (net_provisioning() || s_prov_pending) net_stop_provisioning(PROV_WHY_LEFT);
}

/* May a screen change close the QR screen (R212, I5)? Yes with no session running (prov_left then drops a queued one),
 * or while the session would stop (omni_prov_type_instead_ok, as "Type it instead"); never while it is joining or done:
 * net would refuse the stop, so the QR stays over the running session and the request is ignored (logged). */
static bool prov_may_leave(const char *what)
{
    const omni_prov_phase_t ph = net_prov_phase();
    if (ph == PROV_PH_NONE || omni_prov_type_instead_ok(ph)) return true;
    ESP_LOGI(TAG, "%s ignored: the phone session cannot stop now (phase %d)", what, (int)ph);
    return false;
}

/* UI_OPEN_SETTINGS (final review M2): omni_nav_open_settings on a copy first; when that would close the QR screen it
 * is applied only if the session may stop (then it ends first, R212's order), else nothing changes. */
static void open_settings(void)
{
    omni_nav_t n = s_nav;
    const omni_nav_effect_t e = omni_nav_open_settings(&n);
    const bool closes_qr = omni_nav_in_stack(&s_nav, SCR_PROVISION) && !omni_nav_in_stack(&n, SCR_PROVISION);
    if (closes_qr && !prov_may_leave("settings")) return;
    s_nav = n;
    if (closes_qr) prov_left();
    ui_nav(e);
}

/* The watch goes to sleep (PA_TO_SLEEP) with the QR screen on top: the session ends, so BLE and Wi-Fi are never
 * left on in sleep. A Set up from settings was abandoned: back to the face. An unprovisioned watch keeps the QR
 * screen and starts a new session (new PoP) at its next wake. Called before the panel goes off. */
static void prov_sleep(void)
{
    if (omni_nav_top(&s_nav) != SCR_PROVISION || !(net_provisioning() || s_prov_pending)) return;
    if (!omni_prov_stop_on(net_prov_phase(), PROV_WHY_SLEEP)) return; /* joining: its result wakes the screen */
    net_stop_provisioning(PROV_WHY_SLEEP);
    if (net_is_provisioned()) ui_nav(omni_nav_pop(&s_nav));
    else s_prov_resume = true;
}

/* Sound is blocked while a provisioning session is up or being started (no I2S RAM under the QR): kept equal to it
 * after every event, which also covers the sessions net ends by itself (their EV_NET). */
static void sfx_block_sync(void)
{
    const bool prov = net_provisioning() || s_prov_pending;
    sfx_set_blocked(prov);
    disp_direct_set_blocked(prov);   /* the direct path's bands likewise (R118) */
}

static void prov_wake(void)
{
    if (!s_prov_resume) return;
    s_prov_resume = false;
    if (omni_nav_top(&s_nav) == SCR_PROVISION && !net_is_provisioned()) start_provisioning();
}

/* A sync landed: feed its step to the clock calibration, which sets the next interval; persisted (R27). */
static void clock_synced(void)
{
    int64_t step;
    if (!net_take_step(&step)) return;
    omni_clk_on_sync(&s_clk, step, (int64_t)time(NULL), (uint64_t)esp_timer_get_time(), power_asleep_us());
    s_sync.period_s = (int32_t)omni_clk_interval_s(&s_clk);
    ESP_LOGI("clock", "sync step %+lld ms, rate %ld ppm per asleep s, next interval %ld s", (long long)(step / 1000),
             (long)s_clk.saved.rate_ppm, (long)s_sync.period_s);
    esp_err_t err = store_clock_save(&s_clk);
    if (err != ESP_OK) ESP_LOGE(TAG, "clock calibration save: %s", esp_err_to_name(err));
}

static int64_t wall_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static bool set_wall_us(int64_t t)
{
    struct timeval tv = {.tv_sec = (time_t)(t / 1000000), .tv_usec = (suseconds_t)(t % 1000000)};
    return settimeofday(&tv, NULL) == 0;
}

/* The start of the latest minute the screen showed: a correction never takes the clock back before it. Updated
 * while the screen is on (tick, wake, glance). */
static int64_t s_shown_minute_us;

static void clock_shown(void)
{
    int64_t t = wall_us();
    s_shown_minute_us = t - t % 60000000;
}

/* Between syncs: apply the calibrated correction for the light sleep since the last one. Called on wake before the
 * face redraws, and on each tick. Not while a sync runs (its step is measured as the change of the clock). A
 * backward correction is held at the last shown minute (the rest stays due and goes in after the next minute
 * starts): the face never steps back across a minute it showed. Steps rather than a slew: the corrections are
 * small and land mostly while the screen is off. */
static void clock_correct(void)
{
    int64_t now = wall_us();
    int64_t due = omni_clk_correction(&s_clk, net_busy(), now, power_asleep_us(), s_shown_minute_us);
    if (due == 0 || !set_wall_us(now + due)) return;
    omni_clk_applied(&s_clk, due);
    ESP_LOGI("clock", "correction %+lld ms (%ld ppm per asleep s)", (long long)(due / 1000), (long)s_clk.saved.rate_ppm);
}

/* Console settime, run here so it cannot race clock_correct; the next sync then only re-references the
 * calibration (SNTP undoing a hand-set time is not drift). */
static void clock_set_by_hand(int64_t unix_s)
{
    if (!set_wall_us(unix_s * 1000000)) {
        ESP_LOGE("clock", "settime %lld failed", (long long)unix_s);
        return;
    }
    omni_clk_invalidate_ref(&s_clk);
    s_shown_minute_us = 0; /* a hand-set time may go back */
    ESP_LOGI("clock", "set to %lld by hand; calibration reference dropped", (long long)unix_s);
}

static void sync_now(bool loud)
{
    if (net_busy()) return;
    s_loud = loud;
    net_sync_now();
}

/* EV_NET: follow the net state on the provisioning screen; book sync results (backoff, face status). Returns a
 * power action (a provisioning step wakes the screen; a background sync's error does not wake a sleeping watch,
 * it is shown at the next wake). */
static omni_pwr_action_t on_net(int v, uint32_t t)
{
    /* a scan's or a trial join's state belongs to the network list and the keyboard (Tasks 11 and 12), not to the
     * provisioning screen or the background sync's booking */
    if (v & NET_EV_SCAN) {
        if (s_ui) {
            bsp_display_lock(-1);
            scr_netlist_net(v);   /* the records are kept even when the list is not shown */
            bsp_display_unlock();
        }
        return PA_NONE;
    }
    if (v & NET_EV_TRIAL) {   /* the keyboard's join (Task 12): its view, and the clock's booking as a sync */
        const omni_net_state_t ts = NET_EV_STATE(v);
        if (s_ui) {
            bsp_display_lock(-1);
            scr_keyboard_net(v);
            bsp_display_unlock();
        }
        const omni_trial_res_t r = ts == NET_OK ? OMNI_TRES_OK : ts == NET_ERR_SYNC ? OMNI_TRES_SYNC : OMNI_TRES_NONE;
        if (omni_join_books_sync(r)) {   /* the network joined: a sync result like any other */
            if (ts == NET_OK) clock_synced();
            omni_sync_result(&s_sync, (int64_t)time(NULL), ts == NET_OK);
            ui_status();
        }
        /* B2: a result while asleep (or in the glance) is shown at the next look; awake, each step keeps it awake */
        return s_pwr.st == PWR_AWAKE ? omni_pwr_on_touch(&s_pwr, t) : PA_NONE;
    }
    omni_net_state_t st = NET_EV_STATE(v);
    bool prov = v & NET_EV_PROV, loud = s_loud || (v & NET_EV_LOUD) || !omni_time_is_set((int64_t)time(NULL));
    bool err = st == NET_ERR_PASSWORD || st == NET_ERR_NO_AP || st == NET_ERR_SYNC || st == NET_ERR_NO_MEM;
    if (prov && st != NET_IDLE) s_prov_pending = false; /* a start always answers with a state */
    if (st == NET_IDLE) {
        /* IDLE | PROV: the session was stopped. Its QR, if still shown (the time limit), goes; not when a newer
         * start is on its way, or while an unprovisioned watch sleeps on the QR (it restarts at wake). */
        if (prov && !s_prov_pending && !s_prov_resume && s_pwr.st != PWR_ASLEEP &&
            omni_nav_top(&s_nav) == SCR_PROVISION)
            ui_nav(omni_nav_pop(&s_nav));
        return PA_NONE;
    }
    ui_prov(v, false);
    /* A sync result: everything but a QR-flow join failure (that network was never saved). */
    if (st == NET_OK || st == NET_ERR_SYNC || (err && !prov)) {
        if (st == NET_OK) clock_synced(); /* sets the interval omni_sync_result schedules with */
        omni_sync_result(&s_sync, (int64_t)time(NULL), st == NET_OK);
        ESP_LOGI("net", "sync %s, next in %lld s", st == NET_OK ? "ok" : "failed",
                 (long long)(s_sync.next_try - (int64_t)time(NULL)));
        ui_prov(v, false); /* with the new retry time */
        ui_status();
        if (!(v & NET_EV_LOUD)) s_loud = false;
    }
    if (st == NET_OK && !prov) return PA_NONE;
    if (prov || (err && loud)) {
        show_provision();
        if (st == NET_PROV_WAITING) return PA_NONE;
        if (!prov && s_pwr.st == PWR_ASLEEP) return PA_NONE; /* shown at the next wake */
        return omni_pwr_on_lift(&s_pwr, t); /* wake / keep awake for each step */
    }
    return PA_NONE;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* Console `nvs migrate force`, run here (R27, preflight B19): store_migrate_force cancels the pending save, re-imports
 * omni/settings over both records and reloads the RAM view; the loop then applies what the view drives. */
static void nvs_migrate_force(void)
{
    esp_err_t err = store_migrate_force();
    if (err == ESP_ERR_INVALID_STATE) {
        printf("NVS MIGRATE FORCE refused: read-only boot (a reboot retries)\n");
        return;
    }
    if (err == ESP_ERR_NOT_ALLOWED) {
        printf("NVS MIGRATE FORCE refused: %s (a newer record is never overwritten)\n", store_settings_origin());
        return;
    }
    printf("NVS MIGRATE FORCE %s: %s; settings %s\n", err == ESP_OK ? "ok" : "FAILED", store_migration(),
           store_settings_origin());
    if (s_theme->after_store) s_theme->after_store();   /* the records were reloaded: the theme reads its own again (F3) */
    sfx_config(settings()->sound_on, settings()->volume);
    s_nav.face_idx = store_face();
    ui_nav(NAV_FACE_CHANGED);
    ui_refresh();
}

/* Console `net boot-unprov`, written here (R27); refused on a read-only boot (R124 item 2). */
static void net_boot_unprov(void)
{
    esp_err_t e = net_debug_boot_unprovisioned(store_read_only());
    if (e == ESP_ERR_INVALID_STATE) printf("NET BOOT-UNPROV refused: read-only boot\n");
    else printf("NET BOOT-UNPROV %s (next boot only; saved Wi-Fi kept)\n", esp_err_to_name(e));
}
#endif

/* The event loop's Forget guard (C2, I4): Forget (and "Forget and re-scan") only on a writable boot with net idle,
 * evaluated before anything else runs; otherwise ignored and logged. net checks again with its own state. */
static bool forget_allowed(const char *what)
{
    if (omni_join_forget_allowed(store_read_only(), net_provisioning(), net_busy())) return true;
    ESP_LOGW("net", "%s ignored (read-only %d, provisioning %d, busy %d)", what, store_read_only(),
             net_provisioning(), net_busy());
    return false;
}

static void on_ui(int ev)
{
    int code = UI_EV_CODE(ev), arg = UI_EV_ARG(ev);
    switch (code) {
    case UI_POP: {
        omni_screen_t top = omni_nav_top(&s_nav);
        if (top != SCR_FACE) ui_nav(omni_nav_pop(&s_nav));
        if (top == SCR_PROVISION) prov_left();
        break;
    }
    case UI_RESCAN: start_provisioning(); break; /* back to the QR; the saved network stays */
    case UI_FORGET_RESCAN: /* the saved password was refused twice: the wearer chose to forget it */
        if (!forget_allowed("forget-rescan")) break;   /* I4: before start_provisioning sets busy */
        net_forget();
        start_provisioning();
        break;
    case UI_SYNC_NOW: sync_now(true); break;
    case UI_PICK: ui_pick(omni_nav_pick(&s_nav, settings(), (uint8_t)arg)); break;
    case UI_SHOW_FACE: { /* a carousel face is a pick (saved as UI_PICK is); a hidden, registered one is transient */
        bool prov = omni_nav_in_stack(&s_nav, SCR_PROVISION);
        ui_pick(omni_nav_show_id(&s_nav, settings(), (uint8_t)arg, shell_face_ready((uint8_t)arg) ? s_theme->face_count : 0));
        if (prov && !omni_nav_in_stack(&s_nav, SCR_PROVISION)) prov_left();
        break;
    }
    case UI_HOME: {
        bool prov = omni_nav_in_stack(&s_nav, SCR_PROVISION);
        ui_nav(omni_nav_home(&s_nav));
        if (prov) prov_left();
        break;
    }
    case UI_OPEN_PICKER: open_screen(SCR_PICKER); break;
    case UI_OPEN_QUICK: open_screen(SCR_QUICK); break;
    case UI_OPEN_SETTINGS: open_screen(SCR_SETTINGS); break;
    case UI_WIFI_SETUP: start_provisioning(); break; /* the saved network stays until a new one is accepted */
    case UI_WIFI_FORGET:
        if (forget_allowed("forget")) net_forget();
        break;
#if CONFIG_OMNI_DEBUG_CONSOLE
    case UI_NVS_MIGRATE_FORCE: nvs_migrate_force(); break;
    case UI_NET_BOOT_UNPROV: net_boot_unprov(); break;
#endif
    /* F4: the pick comes first (asleep or in the glance the face is created un-woken); handle() then runs
     * omni_pwr_on_alarm and apply() wakes it without the reveal. KEEP picks nothing. */
    case UI_ALARM:
        if (arg != UI_ALARM_KEEP) ui_pick(omni_nav_alarm(&s_nav, settings(), (uint8_t)arg));
        break;
    case UI_THEME_SET: { /* the theme's Settings row (watch_theme.h): applied here (R27), then the screen redraws */
        uint8_t row, v;
        if (s_theme->settings && omni_theme_set_unpack(arg, s_theme->settings_count, &row, &v) &&
            s_theme->settings[row].apply) {
            s_theme->settings[row].apply(v);
            ui_refresh();
        }
        break;
    }
    case UI_OPEN_THEME: open_screen(SCR_THEME); break;   /* none registered: popped at once (ui_show_top) */
    case UI_OPEN_KEYBOARD: open_screen(SCR_KEYBOARD); break;   /* scr_keyboard_prepare ran first */
    case UI_OPEN_FACES: open_screen(SCR_FACES); break;
    case UI_SET_CAROUSEL: set_carousel(); break;
    case UI_OPEN_NETLIST: { /* R212: in provisioning's place when it is in the stack (its session ended first), else pushed */
        if (omni_nav_top(&s_nav) == SCR_NETLIST) break;
        /* R212's order, as UI_PROV_TO_LIST: the session ends first (open_netlist always takes provisioning's place
         * when it is in the stack), then the list; never while the session cannot stop (I5: the QR stays) */
        const bool prov = omni_nav_in_stack(&s_nav, SCR_PROVISION);
        if (prov && !prov_may_leave("netlist")) break;
        if (prov) prov_left();
        ui_nav(omni_nav_open_netlist(&s_nav));
        break;
    }
    case UI_PROV_TO_LIST: /* B1 "Type it instead": the session ends the normal way (the snapshot restore runs), and the
                           * list replaces the QR; never while the session cannot stop (I5, as the pill itself) */
        if (omni_nav_top(&s_nav) != SCR_PROVISION || !omni_prov_type_instead_ok(net_prov_phase())) break;
        prov_left();
        ui_nav(omni_nav_replace(&s_nav, SCR_NETLIST));
        break;
    case UI_LIST_TO_PROV: /* B1 "Use your phone": the QR replaces the list and a new session starts, no second push */
        if (omni_nav_top(&s_nav) == SCR_NETLIST) start_provisioning_at(true);
        break;
    default: apply_setting(code, arg); break;
    }
}

/* The QR screen dims after 60 s idle (its own dim; the panel brightness is untouched). */
static void prov_dim_check(uint32_t t)
{
    if (!s_ui || power_state() != PWR_AWAKE) return;
    uint32_t ref = omni_pwr_idle_ref(&s_pwr);
    bool want = omni_nav_top(&s_nav) == SCR_PROVISION && net_state() == NET_PROV_WAITING && t - ref >= PROV_DIM_MS;
    if (want == s_dimmed) return;
    s_dimmed = want;
    bsp_display_lock(-1);
    shell_dim(want);
    bsp_display_unlock();
}

static void apply(omni_pwr_action_t a)
{
    static const char *const ACT[] = {"PA_NONE", "PA_WAKE_FULL", "PA_WAKE_SKIP_REVEAL", "PA_TO_GLANCE", "PA_TO_SLEEP"};
    if (a == PA_NONE) return;
    int64_t t0 = esp_timer_get_time();
    /* The awake layout is in place before the panel comes back on; the glance one after the switch. */
    if (a == PA_WAKE_FULL || a == PA_WAKE_SKIP_REVEAL) {
        clock_correct(); /* before the face redraws */
        s_dimmed = false;
        prov_wake();
        /* O4: a full wake closes the tool screens and drops a transient face before the lift face. The saved face is
         * redrawn at most once (here, or by the lift when it moves the face). */
        bool redraw = false;
        if (a == PA_WAKE_FULL) {
            const bool tr = s_nav.transient != 0;
            const omni_nav_effect_t e = omni_nav_wake_full(&s_nav);
            if (e == NAV_SCREEN_POPPED) ui_nav(e);
            redraw = tr && e != NAV_NONE;
        }
        /* The theme's lift face (WD7, B9): a full wake with the face on top only (omni_nav_lift's guard), so never over
         * provisioning, Settings or a theme screen. A transient view: the face changes before it wakes with its
         * reveal, and face_idx is not committed (ui_show_lift). */
        if (a == PA_WAKE_FULL && s_theme->lift_face && omni_nav_top(&s_nav) == SCR_FACE &&
            omni_nav_lift(&s_nav, settings(), s_theme->lift_face()) == NAV_FACE_CHANGED)
            redraw = true;
        if (redraw) ui_show_lift();
        ui_glance(false);
        ui_wake(a == PA_WAKE_FULL); /* the face starts animating (with its reveal on a full wake) */
    }
    if (a == PA_TO_SLEEP) {
        prov_sleep();
        /* spec 6.7, Minor 9: the keyboard is never shown again after sleep. Popped now (its destroy wipes the buffer), so
         * a re-lift shows the list, or Settings in city mode, never an emptied keyboard */
        if (omni_nav_top(&s_nav) == SCR_KEYBOARD) ui_nav(omni_nav_pop(&s_nav));
        ui_sleep(); /* the face stops before the panel goes off */
    }
    /* The picker and quick settings are transient: the glance (and the next wake) show the face. */
    while (a == PA_TO_GLANCE && (omni_nav_top(&s_nav) == SCR_PICKER || omni_nav_top(&s_nav) == SCR_QUICK))
        ui_nav(omni_nav_pop(&s_nav));
    power_apply(a);
    if (a == PA_TO_SLEEP) disp_direct_display_off();   /* force-ended at display_off: free the bands now (I2) */
    if (a == PA_TO_GLANCE) ui_glance(true);
    if (a != PA_TO_SLEEP) clock_shown();
    ESP_LOGI("pwr", "%s (%lld ms)", (unsigned)a < sizeof ACT / sizeof ACT[0] ? ACT[a] : "PA_?",
             (esp_timer_get_time() - t0) / 1000);
}

/* The internal heap's minimum since boot, logged with what was happening each time it falls a step lower: a wear
 * reached 7.8 KB (the floor is about 7 KB) with the cause unknown. Called after every event (net events arrive
 * asleep too), so a fall shows beside the event that made it. */
#define HEAP_LOW_STEP 4096
static void heap_low_watch(const omni_event_t *e)
{
    static size_t logged = SIZE_MAX;
    const size_t m = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    if (logged != SIZE_MAX && m + HEAP_LOW_STEP > logged) return;
    logged = m;
    static const char *const PWR[] = {"asleep", "awake", "glance"};
    ESP_LOGW(TAG, "internal heap minimum %u B (after event %d; power %s, net %s%s, VBUS %s)", (unsigned)m, (int)e->kind,
             (unsigned)power_state() < 3 ? PWR[power_state()] : "?", net_state_name(net_state()),
             net_busy() ? " busy" : "", s_vbus ? "yes" : "no");
}

/* Power policy wiring: a lift and PWR wake (PWR also sleeps); gestures and BOOT presses are user activity
 * (they extend AWAKE and wake from GLANCE; none reach here while asleep, the input task does not read them
 * then); a finger held on the screen counts as activity at every tick. Every event but the tick is logged
 * ("ev: EV_GESTURE G_TAP 233,240"). */
static void handle(const omni_event_t *e)
{
    omni_pwr_action_t a = PA_NONE;
    /* Gestures and BOOT navigate only on an awake screen; in the glance they just wake it. */
    bool awake = power_state() == PWR_AWAKE;
    switch (e->kind) {
    case EV_LIFT:
        a = omni_pwr_on_lift(&s_pwr, e->t_ms);
        if (a == PA_WAKE_FULL || a == PA_WAKE_SKIP_REVEAL) {
            rec_vbus_check();
            omni_batrec_wake(&s_rec, WAKE_LIFT);
        }
        rec_checkpoint();
        break;
    case EV_PWR:
        a = omni_pwr_on_button(&s_pwr, e->t_ms);
        break;
    case EV_VBUS:
        rec_vbus_check();
        break;
    case EV_SETTIME:
        clock_set_by_hand(e->unix_s);
        ui_refresh();
        break;
    case EV_SAVE: store_settings_save_now(); break;
    case EV_GESTURE: {
        bool held = false; /* a long press the face keeps for itself: no picker */
        if (awake && s_ui) {
            bsp_display_lock(-1);
            /* F1: asked before the face's gesture hook, which may change the answer (the timer resets on it) */
            if (e->g.kind == G_LONG_PRESS && omni_nav_top(&s_nav) == SCR_FACE) held = shell_face_holds(e->g.x0, e->g.y0);
            /* the screen on top (it answers with EV_UI), or the face's own gesture hook */
            shell_gesture(&e->g, e->t_ms);
            bsp_display_unlock();
        }
        if (awake && !held && omni_nav_top(&s_nav) == SCR_FACE) ui_nav(omni_nav_gesture(&s_nav, settings(), &e->g));
        a = omni_pwr_on_touch(&s_pwr, e->t_ms);
        break;
    }
    case EV_BOOT:
        if (awake) {
            bool qr = omni_nav_top(&s_nav) == SCR_PROVISION;
            omni_nav_effect_t eff = omni_nav_boot(&s_nav, settings(), e->press);
            ui_nav(eff);
            if (qr && eff == NAV_SCREEN_POPPED) prov_left();
        }
        a = omni_pwr_on_touch(&s_pwr, e->t_ms);
        break;
    case EV_TICK_1S: {
        /* The tick runs on "now", not its stamp: a queued tick can be older than an event already handled
         * (e.g. the boot wake), and the policy's unsigned (t - ref) would wrap. */
        uint32_t t = now_ms();
        static unsigned n;
        board_heap_sample();
        power_service(); /* a panel command a pause timeout deferred */
        if (n++ % STATUS_PERIOD_S == 0) ui_status();
        rec_vbus_check();
        rec_checkpoint();
        clock_correct();
        clock_shown();
        if (board_touch_pressed()) a = omni_pwr_on_touch(&s_pwr, t);
        /* The QR screen stays up 3 min while it waits for a scan or the phone (the owner needs the time); pose
         * shortcuts do not apply there. Its errors and a background sync's sleep on the normal timeout. */
        bool qr = omni_nav_top(&s_nav) == SCR_PROVISION && (s_prov_pending || omni_prov_long_timeout(net_prov_phase()));
        if (s_hold_awake && omni_hold_expired(s_hold_since_ms, t, OMNI_AWAKE_HOLD_MAX_MS)) {
            s_hold_awake = false; /* R66: a lost console must not leave the watch held awake */
            ESP_LOGW(TAG, "awake hold expired after %u min: sleep timeout back on",
                     (unsigned)(OMNI_AWAKE_HOLD_MAX_MS / 60000));
        }
        if (a == PA_NONE && !s_hold_awake)
            a = omni_pwr_tick(&s_pwr, t, qr ? PROV_SLEEP_S : omni_nav_sleep_s(&s_nav, settings()->sleep_s),
                              qr ? POSE_UNKNOWN : board_pose());
        /* Scheduled re-sync (6 h, or the failure backoff); runs while awake, the tick stops in sleep. */
        if (!net_busy() && net_is_provisioned() && omni_sync_due(&s_sync, (int64_t)time(NULL))) sync_now(false);
        break;
    }
    case EV_NET:
        a = on_net(e->net, e->t_ms);
        break;
    case EV_UI:
        on_ui(e->ui); /* UI_ALARM: the pick, before the wake below (F4) */
        if (UI_EV_CODE(e->ui) == UI_ALARM)
            a = UI_EV_ARG(e->ui) == UI_ALARM_KEEP ? omni_pwr_on_alarm_keep(&s_pwr, e->t_ms)
                                                  : omni_pwr_on_alarm(&s_pwr, e->t_ms);
        else
            a = omni_pwr_on_touch(&s_pwr, e->t_ms);
        break;
    default:
        break;
    }
    if ((e->kind == EV_GESTURE || e->kind == EV_BOOT || e->kind == EV_PWR || e->kind == EV_UI) &&
        (a == PA_WAKE_FULL || a == PA_WAKE_SKIP_REVEAL))
        omni_batrec_wake(&s_rec, WAKE_TOUCH);
    if (a == PA_TO_GLANCE) omni_batrec_wake(&s_rec, WAKE_GLANCE);
    sfx_block_sync(); /* before the wake below can play */
    apply(a);
    prov_dim_check(now_ms());
    heap_low_watch(e);
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* The event task's stack (Stage 5 final review, finding 3: it fell to 2 788 B free, unattributed): its high-water mark
 * logged after the first event of each kind (gestures by gesture kind, EV_UI by code) and after any event that takes
 * it lower, so a session shows which event goes deepest. The mark scans the unused stack: console builds only (R101). */
static void stack_low_watch(const omni_event_t *e)
{
    static unsigned low = UINT32_MAX;
    static uint32_t seen;          /* bit kind, or 16 + the gesture kind */
    static uint64_t seen_ui;       /* bit UI code */
    const unsigned free_b = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    bool first;
    if (e == NULL) {
        first = true;
    } else if (e->kind == EV_UI) {
        const unsigned c = (unsigned)UI_EV_CODE(e->ui) & 63u;
        first = !(seen_ui >> c & 1u);
        seen_ui |= 1ull << c;
    } else {
        const unsigned b = e->kind == EV_GESTURE ? 16u + ((unsigned)e->g.kind & 15u) : (unsigned)e->kind & 15u;
        first = !(seen >> b & 1u);
        seen |= 1u << b;
    }
    const bool lower = free_b < low;
    if (!first && !lower) return;
    char buf[64];
    ESP_LOGI(TAG, "events stack: %u B free after %s%s%s", free_b, e && first ? "the first " : "",
             e ? omni_event_format(e, buf, sizeof buf) : "the loop's start", lower && low != UINT32_MAX ? " (new low)" : "");
    if (lower) low = free_b;
}
#else
static inline void stack_low_watch(const omni_event_t *e) { (void)e; }
#endif

static void event_loop(void *arg)
{
    (void)arg;
    ui_status();
    /* Unprovisioned: straight to the QR. Provisioned: sync now (a failure shows on screen). */
    if (net_is_provisioned()) sync_now(true);
    else start_provisioning();
    sfx_block_sync(); /* before the power-on wake below */
    s_vbus = board_vbus_present();
    clock_power_source(s_vbus); /* a boot on battery (crash, brown-out) has no unplug edge */
    if (omni_batrec_boot(&s_rec, s_vbus, rec_snap())) rec_save();
    apply(omni_pwr_on_lift(&s_pwr, now_ms())); /* power-on counts as a wake: the screen times out as usual */
    stack_low_watch(NULL);
    omni_event_t e;
    char buf[64];
    bool hold_seen = false; /* a G_HOLD of the current press was already logged */
    uint16_t hold_age = 0;
    for (;;) {
        /* a deferred brightness waits at most BRIGHT_HOLD_MS for the queue to drain */
        if (xQueueReceive(omni_events(), &e, s_bright_pending >= 0 ? pdMS_TO_TICKS(20) : portMAX_DELAY) != pdTRUE) {
            brightness_flush(true);
            continue;
        }
        /* moves come at 60 Hz, and a hold ticks every 85 ms (and Delete repeats on it): log a press's first hold tick only.
         * A press is new after any other gesture (G_DOWN, a release's event) or when the hold's age falls back. */
        bool log_it = e.kind != EV_TICK_1S && !(e.kind == EV_GESTURE && e.g.kind == G_DRAG_MOVE);
        if (e.kind == EV_GESTURE) {
            if (e.g.kind == G_HOLD) {
                if (hold_seen && e.g.hold_ms >= hold_age) log_it = false;
                hold_seen = true;
                hold_age = e.g.hold_ms;
            } else if (e.g.kind != G_DRAG_MOVE) {
                hold_seen = false;
            }
        }
        if (log_it)
            ESP_LOGI("ev", "%s", omni_event_format(&e, buf, sizeof buf));
        handle(&e);
        brightness_flush(false);
        stack_low_watch(&e);
    }
}

/* The theme swap (store_theme_swapped: watch/theme named another theme at boot): the theme's name on black, in the
 * Settings title type, over everything for 1.5 s (owner Q2: the existing styles until the designed version). An
 * object on the top layer, so the direct path stays off while it shows (disp_direct's overlay walk). LVGL lock held. */
#define TOAST_MS 1500
static void toast_done(lv_timer_t *tm)
{
    lv_obj_delete((lv_obj_t *)lv_timer_get_user_data(tm));   /* the timer (repeat count 1) deletes itself */
}

static void swap_toast(const watch_theme_t *t)
{
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(bg);
    lv_label_set_text(l, t->name);
    lv_obj_set_style_text_font(l, &font_cormorant_700_15, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(THEME_BRASS), 0);
    lv_obj_set_style_text_letter_space(l, 3, 0);
    lv_obj_center(l);
    lv_timer_t *tm = lv_timer_create(toast_done, TOAST_MS, bg);
    lv_timer_set_repeat_count(tm, 1);
    ESP_LOGI(TAG, "toast %s %d ms", t->name, TOAST_MS);
}

/* The app's app_main: the boot order is Stage 4a's, with the theme's faces registered through its descriptor. */
void watch_main(const watch_theme_t *t)
{
    s_theme = t;   /* before assets_init: the image's file count and checksum are the theme's */
    ESP_LOGI(TAG, "Omni Watch %s", omni_version());
    esp_err_t err = board_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "board_init: %s", esp_err_to_name(err));
    assets_init(); /* mmap the assets partition (logs its own failure); a face that needs it is not registered */
    store_init(t->id, &t->limits, t->omni_legacy); /* NVS (never erased), the migration, TZ + brightness applied */
    if (t->after_store) t->after_store();   /* the theme's own record work, before the nav reads the face (boot task) */
    sfx_config(settings()->sound_on, settings()->volume); /* nothing is created until the first play */
    esp_err_t rec_err = store_batrec_load(&s_rec);
    if (rec_err != ESP_OK) ESP_LOGW(TAG, "battery record load: %s (starting empty)", esp_err_to_name(rec_err));
    net_init(store_read_only());   /* after NVS: PoP, Wi-Fi driver (radio off) */

    omni_nav_init(&s_nav);
    s_nav.face_idx = store_face();   /* the face t_<id>/local last saved (clamped to the carousel) */
    omni_sync_init(&s_sync);
    store_clock_load(&s_clk);
    s_sync.period_s = (int32_t)omni_clk_interval_s(&s_clk);
    if (lv_display_get_default() != NULL) {
        bsp_display_lock(-1);
        shell_init(&s_nav);
        disp_stats_init(lv_display_get_default());
        t->register_faces(assets_ready());   /* the theme's faces; those that need the mapped assets only if ready */
        shell_register_screen(&SCR_PROVISION_VT);
        shell_register_screen(&SCR_PICKER_VT);
        shell_register_screen(&SCR_QUICK_VT);
        shell_register_screen(&SCR_SETTINGS_VT);
        shell_register_screen(&SCR_NETLIST_VT);
        shell_register_screen(&SCR_KEYBOARD_VT);
        shell_register_screen(&SCR_FACES_VT);
        shell_show_current();
        if (store_theme_swapped()) swap_toast(t);
        bsp_display_unlock();
        s_ui = true;
    }

    /* The console first: the event loop (higher priority) starts provisioning or a sync at once, and Wi-Fi + BLE
     * bring-up would otherwise compete with the REPL's allocations for internal RAM. */
    debug_console_start();
    /* 8 KB like the LVGL task: power_apply's wake renders the whole screen (lv_refr_now) on this stack. */
    if (omni_events() != NULL) xTaskCreate(event_loop, "events", 8192, NULL, 3, &s_loop_task);
    /* The boot task's margin (CONFIG_ESP_MAIN_TASK_STACK_SIZE): store_init's migration runs here (~1.1 KB a part). */
    ESP_LOGI(TAG, "boot stack: %u B never used", (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
