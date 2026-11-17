/* fk_face (fk_face.h): the runtime behind FK_FACE_VT's trampolines (from the first theme's face_generic.c; face_kit
 * since rd wave 1 Task 2). A face is a fk_face_ops_t (pure, a theme's core); this file gives it the shell's life
 * cycle: create (its view), update every second and at the face's cadence while awake, the glance, wake/sleep (the
 * reveal), and taps. The input is filled here, on the LVGL side, from what the event loop has published
 * (shell_status, the settings, the city's next sun event, the chime service's channels) and the theme's hooks, never
 * in a theme's core.
 *
 * The time: the shell's `now` (seconds) with the milliseconds of the same second; the cadence timer reads the clock
 * once and splits it the same way.
 *
 * The wake: omni_reveal_for over the theme's reveal setting (the Omni's FLIP, IRIS, NONE ids) gives the reveal, its
 * cross-fade, or the plain entry; a lift's wake (the take_lift hook) gives the reveal whatever the style. Reduced
 * motion gives the cross-fade. A swipe, the picker or a re-lift within 10 s gives the plain entry. While the reveal
 * runs, wake_ms is its elapsed time (the face's tail); otherwise UINT32_MAX (settled). A tap during it skips to the
 * end frame. A face of its own wake (ops->wake_len_ms, no reveal) gets wake_ms from a revealing wake for that long, and
 * a FK_TAP_PULSE its pulse_ms for pulse_len_ms; both stop at the sleep. The next sun event's local time is worked out
 * here (the app's zone), never in a theme's core; today's sunrise and sunset come from the sun_today hook. A
 * FK_TAP_ALT of a face with alt_len_ms holds in->alt that long: a one-shot timer clears it and draws; a second tap,
 * the sleep and the destroy clear it too. mono_ms is esp_timer's ms. in->fresh / fresh_ms: the time since the last
 * revealing wake (a reveal not NONE), for 60 s, whatever the motion setting: what a wake shows as information shows
 * under reduced motion too; the own-wake animation itself still never runs under reduced motion. The timer follows
 * fk_face_timer_ms (timer_sync): a face of no cadence with a wake or pulse of its own gets a 40 ms timer only while
 * one runs, deleted at its end (cadence_cb), at the sleep and at the destroy. in->variant is the face's own counter
 * (rt->variant: it survives the view and the sleep, RAM only); a FK_TAP_CYCLE adds one and starts the face's pulse; a
 * tap_var (used instead of tap when set) may set it. FK_TAP_CHIME asks the theme's chime_preview hook. A .direct
 * face's rest motion (fk_view_motion) is turned on at each wake and off at the sleep. The hooks' on_sleep runs at
 * every face's sleep; woke at every face's wake, before its first frame (the cause, reveal and lift); slept fills
 * in->slept_s. in->sync_age_s is fk_sync_age(now, shell_status()->sync.last_ok) (rd wave 2 Task 6). */
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "fk_face.h"
#include "fk_chime_svc.h"
#include "esp_timer.h"
#include "store.h"
#include "omni/cities.h"
#include "omni/power.h"
#include "omni/sun.h"
#include "omni/sync.h"
#include "omni/timefmt.h"

/* the milliseconds of second `now` (the shell's): from one clock read, 0 or 999 if the clock has moved past it */
static uint16_t ms_of(int64_t now)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if ((int64_t)tv.tv_sec == now) return (uint16_t)(tv.tv_usec / 1000);
    return (int64_t)tv.tv_sec > now ? 999 : 0;
}

static const fk_face_hooks_t *s_hooks;   /* the theme's (fk_face_set_hooks), set once before the faces register */

void fk_face_set_hooks(const fk_face_hooks_t *h) { s_hooks = h; }

/* today's local sunrise and sunset for the home city (the theme's sun_today hook), -1 none (polar day or night, no
 * city, an unset clock, no hook, or a face that does not read them: ops->uses_sun) */
static void sun_today(fk_face_rt_t *rt, int64_t now, const omni_city_t *c, bool set)
{
    rt->sun_rise_m = rt->sun_set_m = -1;
    if (set && c && rt->ops->uses_sun && s_hooks && s_hooks->sun_today)
        s_hooks->sun_today(now, &rt->sun_rise_m, &rt->sun_set_m);
}

/* the next sunrise or sunset for the home city, looked up once a minute (B11); today's rise and set with it (Task 9) */
static void sun(fk_face_rt_t *rt, int64_t now, bool set)
{
    const uint16_t city = settings()->city;
    /* the minute gate bounds a polar city too (no next event: sun_next_s 0), Task 9 review M2 */
    if (rt->sun_min == now / 60 && rt->sun_city == city && (rt->sun_next_s == 0 || now < rt->sun_next_s)) return;
    rt->sun_min = now / 60;
    rt->sun_city = city;
    rt->sun_next_s = 0;
    rt->sun_kind = 0;
    const omni_city_t *c = omni_city_at(city);
    sun_today(rt, now, c, set);
    int64_t when;
    bool rise;
    if (set && c && omni_sun_next_event(now, c->lat, c->lon, &when, &rise)) {
        rt->sun_next_s = when;
        rt->sun_kind = rise ? 1 : 2;
        struct tm tm;
        const time_t t = (time_t)when;
        if (localtime_r(&t, &tm)) rt->sun_h = (uint8_t)tm.tm_hour, rt->sun_m = (uint8_t)tm.tm_min;   /* the zone */
        else rt->sun_kind = 0;
    }
}

/* ms since t0 (esp_timer us) while under len, else stop the run (UINT32_MAX) */
static uint32_t run_ms(bool *on, int64_t t0, uint16_t len)
{
    if (!*on) return UINT32_MAX;
    const int64_t e = (esp_timer_get_time() - t0) / 1000;
    if (e >= 0 && e < len) return (uint32_t)e;
    *on = false;
    return UINT32_MAX;
}

static void fill_in(fk_face_rt_t *rt, int64_t now, uint16_t ms, fk_face_in_t *in)
{
    memset(in, 0, sizeof *in);
    const shell_status_t *st = shell_status();
    fk_clock_from(now, ms, omni_sync_lens(now, &st->sync) == OMNI_LENS_STALE, &in->clk);
    in->h24 = settings()->h24;
    in->reduced = settings()->reduced_motion;
    in->wake_ms = fk_view_reveal_active(rt->view) ? (uint32_t)((esp_timer_get_time() - rt->reveal_t0) / 1000)
                  : run_ms(&rt->own_wake, rt->own_t0, rt->ops->wake_len_ms);   /* the doors' tail, or the face's own */
    const uint32_t p = run_ms(&rt->pulse, rt->pulse_t0, rt->ops->pulse_len_ms);
    in->pulsing = p != UINT32_MAX;
    in->pulse_ms = in->pulsing ? (uint16_t)p : 0;
    omni_orbit_offset(now, &in->odx, &in->ody);
    if (rt->alt && rt->ops->alt_len_ms && (esp_timer_get_time() - rt->alt_t0) / 1000 >= rt->ops->alt_len_ms)
        rt->alt = false;   /* the twins come back (the one-shot timer normally got here first) */
    in->alt = rt->alt;
    if (rt->fresh) {
        const int64_t f = (esp_timer_get_time() - rt->fresh_t0) / 1000;
        if (f >= 0 && f < 60000) in->fresh = true, in->fresh_ms = (uint16_t)f;
        else rt->fresh = false;
    }
    in->mono_ms = (uint32_t)(esp_timer_get_time() / 1000);
    fk_chime_svc_fill(in);   /* the chime picture's channels now: the breathing faces read them at their cadence */
    in->battery = (int8_t)(st->battery_pct < 0 ? -1 : st->battery_pct > 100 ? 100 : st->battery_pct);
    in->charging = st->charging;
    sun(rt, now, in->clk.set);
    in->sun_next_s = rt->sun_next_s;
    in->sun_kind = rt->sun_kind;
    in->sun_h = rt->sun_h;
    in->sun_m = rt->sun_m;
    in->sun_rise_m = rt->sun_rise_m;
    in->sun_set_m = rt->sun_set_m;
    in->variant = rt->variant;
    if (s_hooks && s_hooks->slept) s_hooks->slept(in);   /* the last completed sleep (0 without the hook) */
    in->sync_age_s = fk_sync_age(now, st->sync.last_ok);   /* rd wave 2 Task 6: the last good sync's age (-1 never) */
}

static void draw_at(fk_face_rt_t *rt, int64_t now, uint16_t ms, bool full)
{
    fk_face_in_t in;
    fill_in(rt, now, ms, &in);
    fk_view_draw(rt->view, &in, full);
}

static void draw_now(fk_face_rt_t *rt, bool full)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);   /* one read: the second and its milliseconds */
    draw_at(rt, (int64_t)tv.tv_sec, (uint16_t)(tv.tv_usec / 1000), full);
}

static void cadence_cb(lv_timer_t *t);

/* R147 PR6 (wave 2 Task 12): the face's one timer as fk_face_timer_ms wants it now: a cadence face's at its cadence; a
 * face of no cadence (#8) only while its own wake or a pulse runs, so the wake's frames show, deleted at its end (1 Hz
 * at rest, the shell's update). Created at most once (rt->timer is the only handle); the sleep and the destroy delete
 * it too. */
static void timer_sync(fk_face_rt_t *rt)
{
    const uint16_t ms = rt->view ? fk_face_timer_ms(rt->ops, rt->own_wake || rt->pulse) : 0;
    if (ms == 0) {
        if (rt->timer) lv_timer_delete(rt->timer);   /* safe inside its own callback (LVGL 9: timer_deleted) */
        rt->timer = NULL;
    } else if (rt->timer == NULL) {
        rt->timer = lv_timer_create(cadence_cb, ms, rt);
    }
}

/* a frame at the cadence; then the timer is kept or (a cadence-0 face whose wake or pulse just ended, which this
 * frame's fill_in found: it drew the settled frame) deleted */
static void cadence_cb(lv_timer_t *t)
{
    fk_face_rt_t *rt = lv_timer_get_user_data(t);
    draw_now(rt, false);
    timer_sync(rt);
}

/* the end of a FK_TAP_ALT's alt_len_ms (one shot: LVGL deletes the timer after this call) */
static void alt_cb(lv_timer_t *t)
{
    fk_face_rt_t *rt = lv_timer_get_user_data(t);
    rt->alt_timer = NULL;
    rt->alt = false;
    draw_now(rt, false);
}

static void alt_timer_stop(fk_face_rt_t *rt)
{
    if (rt->alt_timer) lv_timer_delete(rt->alt_timer);
    rt->alt_timer = NULL;
}

void fk_face_create(fk_face_rt_t *rt, lv_obj_t *parent)
{
    rt->view = fk_view_create(parent, rt->ops);
    rt->timer = NULL;
    rt->alt = false;
    rt->alt_timer = NULL;
    rt->fresh = false;
    rt->wt = (omni_wake_track_t){0};
    rt->reveal_t0 = 0;
    rt->sun_min = -1;
    rt->sun_rise_m = rt->sun_set_m = -1;
    rt->own_wake = rt->pulse = false;
}

void fk_face_update(fk_face_rt_t *rt, int64_t now, bool glance)
{
    fk_view_set_glance(rt->view, glance);
    draw_at(rt, now, ms_of(now), false);
}

void fk_face_sleep(fk_face_rt_t *rt)
{
    omni_wake_track_sleep(&rt->wt);
    if (s_hooks && s_hooks->on_sleep) s_hooks->on_sleep();   /* the theme's, at every face's sleep */
    fk_view_reveal_stop(rt->view);   /* no half frame survives the sleep (Review Focus 4) */
    rt->own_wake = rt->pulse = false;   /* nor a wake or a pulse in motion: the glance draws settled */
    fk_view_motion(rt->view, false);    /* wave 2 Task 14: a .direct face's rest motion ends (the variant stays) */
    rt->alt = false;                    /* hidden twins come back with the next wake */
    rt->fresh = false;
    alt_timer_stop(rt);
    if (rt->timer) lv_timer_delete(rt->timer);
    rt->timer = NULL;
}

void fk_face_destroy(fk_face_rt_t *rt)
{
    alt_timer_stop(rt);
    if (rt->timer) lv_timer_delete(rt->timer);
    rt->timer = NULL;
    fk_view_destroy(rt->view);
    rt->view = NULL;
}

void fk_face_wake(fk_face_rt_t *rt, bool reveal)
{
    const omni_wake_cause_t cause = omni_wake_track_wake(&rt->wt);
    const bool lift = s_hooks && s_hooks->take_lift && s_hooks->take_lift();   /* a lift: the reveal whatever the style */
    if (s_hooks && s_hooks->woke) s_hooks->woke(cause, reveal, lift);   /* the theme's, before the first frame */
    const omni_rv_t rv = omni_reveal_for(cause, reveal, settings()->reveal_style);
    bool doors = false, reduced = settings()->reduced_motion;
    if (rt->ops->reveal && reveal) {
        if (lift || rv == OMNI_RV_FLIP) doors = true;          /* Doors */
        else if (rv == OMNI_RV_IRIS) doors = reduced = true;   /* Fade: the doors' cross-fade */
    }
    rt->fresh = reveal && rv != OMNI_RV_NONE;   /* a revealing wake, also under reduced motion (I1) */
    if (rt->fresh) rt->fresh_t0 = esp_timer_get_time();
    timer_sync(rt);   /* a cadence face's timer (own_wake and pulse are false here: the sleep cleared them) */
    if (doors && rt->view) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        fk_face_in_t in;
        fill_in(rt, (int64_t)tv.tv_sec, (uint16_t)(tv.tv_usec / 1000), &in);
        rt->reveal_t0 = esp_timer_get_time();
        in.wake_ms = 0;
        if (fk_view_reveal(rt->view, &in, reduced)) return;
    }
    /* a face's own wake (Task 13: #4's wave, #5's lamp): a revealing wake, not reduced motion (it shows settled) */
    rt->own_wake = !doors && reveal && rt->ops->wake_len_ms && rv != OMNI_RV_NONE && !reduced;
    if (rt->own_wake) rt->own_t0 = esp_timer_get_time();
    timer_sync(rt);   /* a cadence-0 face's 40 ms timer for its wake (R147 PR6) */
    draw_now(rt, rt->own_wake);   /* the plain entry (whole when the wake starts: its first frame differs) */
    fk_view_motion(rt->view, true);   /* wave 2 Task 14: a .direct face's rest motion, after its first frame */
}

void fk_face_gesture(fk_face_rt_t *rt, const omni_gesture_t *g, uint32_t t_ms)
{
    (void)t_ms;
    if (g->kind != G_TAP || rt->view == NULL) return;
    if (fk_view_reveal_active(rt->view)) {   /* a tap during the doors: the end frame (the core's skip) */
        fk_view_reveal_skip(rt->view);
        return;
    }
    if (rt->ops->tap == NULL && rt->ops->tap_var == NULL) return;
    const uint16_t v0 = rt->variant;
    fk_tap_t act;
    if (rt->ops->tap_var) {   /* the face's input now (rd wave 1 Task 13: what the tap would change is on screen) */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        fk_face_in_t in;
        fill_in(rt, (int64_t)tv.tv_sec, (uint16_t)(tv.tv_usec / 1000), &in);
        act = rt->ops->tap_var(fk_view_ctx(rt->view), &in, g->x, g->y, &rt->variant);
    } else {
        act = rt->ops->tap(fk_view_ctx(rt->view), g->x, g->y);
    }
    if (rt->variant != v0 && act != FK_TAP_CYCLE && act != FK_TAP_ALT)
        draw_now(rt, false);   /* the tap_var's new variant (CYCLE and ALT draw below) */
    switch (act) {
    case FK_TAP_SCREEN: shell_post(UI_OPEN_THEME, 0); break;
    case FK_TAP_ALT:
        rt->alt = !rt->alt;
        rt->alt_t0 = esp_timer_get_time();
        alt_timer_stop(rt);
        if (rt->alt && rt->ops->alt_len_ms) {   /* M1: back after exactly alt_len_ms */
            rt->alt_timer = lv_timer_create(alt_cb, rt->ops->alt_len_ms, rt);
            if (rt->alt_timer) lv_timer_set_repeat_count(rt->alt_timer, 1);
        }
        draw_now(rt, true);
        break;
    case FK_TAP_CYCLE:   /* wave 2 Task 14: the next variant (#12's palette) and its pulse (the name), from 0 */
        rt->variant = fk_variant_next(rt->variant);   /* modulo 255 (a multiple of 3): no palette repeats at the wrap */
        if (rt->ops->pulse_len_ms) {
            rt->pulse = true;
            rt->pulse_t0 = esp_timer_get_time();
        }
        timer_sync(rt);
        draw_now(rt, false);   /* dirty() gives the whole frame for a new palette */
        break;
    case FK_TAP_PULSE: {   /* the call box's lamp: its pulses again (Task 13), drawn at the cadence */
        if (!rt->ops->pulse_len_ms) break;
        /* R148 (a): not while the playing pulse is under the face's guard (#13's lid closing) */
        const uint32_t p = run_ms(&rt->pulse, rt->pulse_t0, rt->ops->pulse_len_ms);
        if (!fk_pulse_retap(rt->ops, p != UINT32_MAX, p)) break;
        rt->pulse = true;
        rt->pulse_t0 = esp_timer_get_time();
        timer_sync(rt);   /* a cadence-0 face's timer for the pulse (R147 PR6) */
        draw_now(rt, false);
        break;
    }
    case FK_TAP_CHIME:   /* the theme's chime preview (rd wave 1 Task 2); nothing without the hook */
        if (s_hooks && s_hooks->chime_preview) s_hooks->chime_preview();
        break;
    default: break;
    }
}
