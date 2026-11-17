/* SCR_THEME for the dw theme: the numeral key, #15 (dw wave 1 Task 15, spec section 12). A tap on #14, or the
 * "?" on #16, posts UI_OPEN_THEME (face_kit's fk_face.c), and the event loop pushes this screen (Task 10's mechanism).
 *
 * It is an fk_view of DW_KEY (dw_core gkey.c): its three pages, drawn at the face's cadence (each page's build, the
 * sum, the reading) and redrawn by its dirty rects. The input: the clock now, the page, and wake_ms = the wall-clock
 * ms since the page opened, so page 2's time, the clock less wake_ms, stays the time the page opened (a clock that
 * steps back, or becomes set, opens the page again). A tap turns the page and plays the key-page tick (dw_tick.pcm,
 * Task 8); the back button posts UI_POP, as BOOT does (the core's pop). The glance (dim) shows the 0-9 strip, dim, on
 * the orbit, and stops the page timer. Never writes settings (R26/R27). */
#include <string.h>
#include <sys/time.h>
#include "fk_view.h"
#include "dw/faces.h"
#include "esp_timer.h"
#include "sfx.h"
#include "power.h"
#include "store.h"
#include "dw/assets_meta.h"
#include "omni/power.h"
#include "omni/sync.h"
#include "omni/timefmt.h"

static struct {
    fk_view_t *view;
    lv_timer_t *timer;
    uint8_t page;
    int64_t open_ms;   /* the wall clock (ms) when the page opened */
    bool open_set;     /* whether the clock was set then */
    bool dim;          /* the glance: no page timer */
} S;

static void draw(bool full)
{
    if (S.view == NULL) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);   /* one read: the second and its milliseconds */
    const int64_t now = (int64_t)tv.tv_sec, wall = now * 1000 + tv.tv_usec / 1000;
    fk_face_in_t in;
    memset(&in, 0, sizeof in);
    const shell_status_t *st = shell_status();
    fk_clock_from(now, (uint16_t)(tv.tv_usec / 1000), omni_sync_lens(now, &st->sync) == OMNI_LENS_STALE, &in.clk);
    if (wall < S.open_ms || in.clk.set != S.open_set) {   /* the clock stepped back, or was set: open the page again */
        S.open_ms = wall;
        S.open_set = in.clk.set;
        full = true;
    }
    const int64_t e = wall - S.open_ms;
    in.wake_ms = e >= (int64_t)UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)e;
    in.page = S.page;
    in.h24 = settings()->h24;
    in.reduced = settings()->reduced_motion;
    in.battery = -1;
    in.mono_ms = (uint32_t)(esp_timer_get_time() / 1000);
    omni_orbit_offset(now, &in.odx, &in.ody);
    fk_view_draw(S.view, &in, full);
}

static void open_page(uint8_t page)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    S.page = page;
    S.open_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    S.open_set = omni_time_is_set((int64_t)tv.tv_sec);
}

static void tick_cb(lv_timer_t *t);

static void timer_on(bool on)
{
    if (on && S.timer == NULL && S.view) S.timer = lv_timer_create(tick_cb, DW_KEY.cadence_ms, NULL);
    if (!on && S.timer) {
        lv_timer_delete(S.timer);
        S.timer = NULL;
    }
}

/* Review I3: a direct sleep (PWR: ui_sleep -> shell_sleep) reaches only the face, never a screen, so the page timer
 * stops itself once the panel is not awake. It comes back from update(): the shell's 1 s tick (shell.c tick_cb, an
 * lv_timer made in shell_init) calls refresh(), which calls the screen's update() whenever a screen is on top, awake
 * or not; the first update() after the wake (within a second) restarts it, as dim(false) does after a glance. */
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (power_state() != PWR_AWAKE) {
        timer_on(false);   /* deleting the running timer from its own callback is allowed in LVGL 9 */
        return;
    }
    draw(false);
}

static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    S.view = fk_view_create(parent, &DW_KEY);   /* NULL without PSRAM: logged, the screen stays black */
    open_page(0);
    timer_on(true);
    draw(true);
}

static void update(int64_t now)
{
    (void)now;
    if (!S.dim && power_state() == PWR_AWAKE) timer_on(true);   /* back after a sleep the screen never saw (I3) */
    draw(false);
}

static void destroy(void)
{
    timer_on(false);
    fk_view_destroy(S.view);
    memset(&S, 0, sizeof S);
}

static void gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    (void)t_ms;
    if (g->kind != G_TAP || S.view == NULL || DW_KEY.tap == NULL) return;
    switch (DW_KEY.tap(fk_view_ctx(S.view), g->x, g->y)) {
    case FK_TAP_BACK: shell_post(UI_POP, 0); break;   /* as BOOT: back to the face */
    case FK_TAP_NEXT:
        open_page((uint8_t)((S.page + 1) % DW_KEY_PAGES));
        sfx_play_asset(ASSET_DW_TICK);   /* the key-page tick (spec section 13); quiet when sound is off */
        draw(true);
        break;
    default: break;
    }
}

static void dim(bool on)
{
    fk_view_set_glance(S.view, on);   /* the 0-9 strip; the next draw is whole */
    S.dim = on;
    timer_on(!on && power_state() == PWR_AWAKE);
    draw(true);
}

const omni_screen_vt_t SCR_KEY_VT = {
    .id = SCR_THEME, .create = create, .update = update, .destroy = destroy, .gesture = gesture, .dim = dim,
};
