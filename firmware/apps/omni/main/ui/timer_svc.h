#pragma once
/* The one timer (Omni Stage 5 Task 6): omni_tmr_* state alive from boot, whichever face is shown, its alarm and its bell.
 *
 * The alarm: an esp_timer one-shot armed at the countdown's end. Its callback (esp_timer task) only posts UI_ALARM with
 * FACE_TIMER (watch_main: omni_nav_alarm's pick, never over the QR, then a wake without a reveal); when the post fails
 * (queue full) it re-arms itself, +100 ms 50 times, then every 1 s for as long as the post fails and the DONE has not
 * been seen on the face, asleep or not: it never gives up (preflight F5, fix round 1 m3). Every arming carries a
 * generation, so a retry that raced a change never posts early and the real end stays armed (m2). It never calls
 * power_* (R26).
 *
 * The poll (an lv_timer, 250 ms, LVGL context; it does not run asleep: LVGL is paused) ticks the machine. On the step to
 * DONE it plays the bell once. While DONE with the timer face awake, it rings again every 2 s for at most 30 s from the
 * countdown's end and posts UI_ALARM_KEEP (activity only, never a wake). Leaving the face, the glance, sleep or the 30 s
 * cap silences it; the mode stays DONE until a tap. A DONE the timer face has not yet been shown awake in is posted
 * again (through the one-shot, with its retries) on any wake: a poll after a pause of LVGL. The decisions are
 * omni_core's omni/tmr_alarm.h (host-tested).
 *
 * LVGL context (the lock held) unless noted. */
#include <stdbool.h>
#include "sdkconfig.h"
#include "omni/timer.h"

void tmr_svc_init(void);              /* state, an lv_timer (250 ms) running tmr_svc_poll, the esp_timer one-shot */
omni_tmr_t *tmr_svc(void);
/* After any omni_tmr_* call: esp_timer_stop, then esp_timer_start_once at the countdown's end (an end <= now arms 0);
 * nothing armed outside COUNT. Refreshes the claim's copy of the mode and silences a bell the change dismissed. */
void tmr_svc_changed(void);
void tmr_svc_face_awake(bool on);     /* the timer face's wake/sleep hooks (on: also polls at once) */
void tmr_svc_poll(void);
uint32_t tmr_svc_now_ms(void);        /* the machine's clock: esp_timer ms (any task) */
omni_tmr_mode_t tmr_svc_mode(void);   /* any task, no lock: the copy of the mode tmr_svc_changed and the poll refresh */
#if CONFIG_OMNI_DEBUG_CONSOLE
void tmr_svc_debug_postfail(uint32_t n);      /* `tmr postfail <n>`: the service's next n posts fail (any task) */
uint32_t tmr_svc_debug_postfail_left(void);
#endif
