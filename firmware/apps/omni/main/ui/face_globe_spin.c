/* The spinning desk globe: design/mockups/faces-practical.js, `id: 'globe-spin'` (frame, lines 553-581) and its
 * sleepFrame (glanceTime, no sub-line). Positions, sizes and colours are the mockup's.
 *
 * Awake: the globe (globe_face / globe_view "spin": tilted 23.44 deg under the studio light, the dome and brass
 * meridian are its overlay asset, the halo with its static upper-left lobe is rendered into the base) turning on
 * the omni_spin flywheel, and the quiet brass time below it. Wake: the globe arrives turning fast and coasts down
 * to its idle pace while it fades in (omni_reveal_spin; reduced motion: idle pace and a 0.4 s fade). Drags inside
 * the disc spin it (omni_spin_*); a tap brightens the time and shows the date for 4 s, and skips a reveal to its end.
 * The globe re-renders every 33 ms, every 66 ms 20 s after the last wake or touch (omni_globe_period_ms).
 * Glance: the shared glance layout; the globe is stopped. Unset clock: "--:--" and no date (the studio-lit globe
 * does not depend on the time). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "shell.h"
#include "glance.h"
#include "globe_face.h"
#include "theme.h"
#include "omni_fonts.h"
#include "omni_faces.h"
#include "store.h"
#include "omni/globe.h"
#include "omni/globe_motion.h"
#include "omni/timefmt.h"

#define CX THEME_CX
#define GY 222                /* mockup globe centre y (cy) */
#define R 158                 /* mockup globe radius (G.R for 316 px) */
#define HALO_STRENGTH 0.75f
#define TIME_BASE 428         /* quiet; 428 - 6 loud */
#define DATE_BASE 440
#define DRAG_DEG_PER_PX 0.42f /* omni_spin_move's */

typedef struct {
    lv_obj_t *awake, *quiet, *loud, *date;
    glance_t *glance;
    globe_face_t *gf;
    omni_spin_t spin;
    bool running;
    float appear;              /* the reveal's fade (1 outside it) */
    lv_opa_t opa_q, opa_l, opa_d;
    int32_t base;              /* the time's baseline, as placed (0: none) */
} spin_t;

static spin_t *S;

static void set_opa(lv_obj_t *l, lv_opa_t *cur, float a)
{
    const lv_opa_t opa = THEME_OPA(a < 0 ? 0 : a > 1 ? 1 : a);
    if (opa == *cur) return;
    *cur = opa;
    lv_obj_set_style_text_opa(l, opa, 0);
}

/* The time and date for the flywheel's loud (a tap) and the reveal's appear, as the mockup draws them: brass at
 * 0.55 + 0.45 loud, the 24 px and 28 px cuts cross-faded by loud, baseline 428 - 6 loud; the date at loud. */
static void labels_apply(uint32_t t)
{
    const float loud = omni_spin_loud(&S->spin, t), a = S->appear * (0.55f + 0.45f * loud);
    set_opa(S->quiet, &S->opa_q, a * (1 - loud));
    set_opa(S->loud, &S->opa_l, a * loud);
    set_opa(S->date, &S->opa_d, S->appear * loud);
    const int32_t base = TIME_BASE - lroundf(6 * loud);
    if (base == S->base) return;
    S->base = base;
    theme_place(S->quiet, THEME_CENTER, CX, base);
    theme_place(S->loud, THEME_CENTER, CX, base);
}

/* ------------------------------------------------------------ globe */
static void tick(globe_face_t *gf, const globe_tick_t *t, globe_view_frame_t *f, bool *animating)
{
    (void)gf;
    omni_spin_advance(&S->spin, t->dt);   /* the idle pace (7 deg/s) at 30 fps and 15 fps alike */
    f->lon0_deg = S->spin.lon;
    f->gain = t->rv.gain;
    f->fade = (uint8_t)lroundf(t->rv.fade * 255);
    omni_globe_sun_studio(&f->sun);
    f->halo = true;
    f->halo_strength = HALO_STRENGTH;
    f->halo_dir[0] = -0.6f;
    f->halo_dir[1] = -0.55f;
    S->appear = t->rv.fade;
    labels_apply(t->now_ms);
    *animating = true;   /* spin always turns; its cadence follows since_active alone (R51) */
}

static const globe_face_ops_t OPS = {
    .view = "spin",
    .cad = OMNI_CAD_SPIN,
    .z = 0,                       /* under the time and date */
    .reveal = omni_reveal_spin,
    .blank = GLOBE_BLANK_BLACK,   /* the reveal starts at fade 0 */
    .tick = tick,
};

/* ------------------------------------------------------------ build */
static void face_create(lv_obj_t *parent)
{
    S = heap_caps_calloc(1, sizeof *S, MALLOC_CAP_SPIRAM);
    if (S == NULL) return;
    S->awake = theme_box(parent, 0, 0, THEME_W, THEME_W);
    S->opa_q = S->opa_l = S->opa_d = LV_OPA_COVER;
    S->gf = globe_face_create(S->awake, &OPS, S);   /* the mockup's (58, 47) moves up 1 px: an even origin (Task 11) */

    S->quiet = theme_label(S->awake, &font_cormorant_700_24, THEME_BRASS, LV_OPA_COVER, 2);   /* 600 24px FD, 1.5 */
    S->loud = theme_label(S->awake, &font_cormorant_700_28, THEME_BRASS, LV_OPA_COVER, 2);    /* 600 28px FD, 1.5 */
    S->date = theme_label(S->awake, &font_plex_sans_500_11, THEME_INK_DIM, LV_OPA_COVER, 2);  /* 500 11px FS, 2 */
    S->glance = glance_create(parent);
    S->appear = 0;   /* until the first wake's reveal */
    labels_apply(globe_face_now_ms());
}

static void face_destroy(void)
{
    if (S == NULL) return;
    globe_face_destroy(S->gf);   /* stops the worker before the objects go with the parent */
    glance_free(S->glance);
    heap_caps_free(S);
    S = NULL;
}

/* ------------------------------------------------------------ hooks */
static void face_wake(bool reveal)
{
    if (S == NULL) return;
    const uint32_t t = globe_face_now_ms();
    S->running = true;
    if (reveal) omni_spin_wake(&S->spin, t, settings()->reduced_motion);   /* mockup wake(): fast, time shown */
    globe_face_wake(S->gf, reveal);   /* the 20 s to 15 fps count from here */
    S->appear = globe_face_revealing(S->gf) ? 0 : 1;   /* no view (no assets / PSRAM): no reveal to fade it in */
    labels_apply(t);
}

static void face_sleep(void)
{
    if (S == NULL) return;
    S->running = false;
    if (S->spin.dragging) omni_spin_up(&S->spin, S->spin.last_t);   /* a drag cut by sleep ends here */
    globe_face_sleep(S->gf);
}

/* INPUT task, no lock: constant geometry only (the face may be being destroyed). */
static bool face_claim(int16_t x0, int16_t y0)
{
    return hypotf((float)(x0 - CX), (float)(y0 - GY)) <= (float)R;
}

static void face_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    if (S == NULL || !S->running || S->gf == NULL) return;
    globe_face_touch(S->gf);
    switch (g->kind) {
    case G_DRAG_START:
        /* down where the finger went down; the 12 px that made it a drag are followed at once, without the
         * velocity a 0 ms move would give (and they are more than the 6 px that make it a move, not a tap) */
        omni_spin_down(&S->spin, t_ms, g->x0);
        S->spin.lon = omni_wrap180f(S->spin.lon + globe_face_drag_lead(g, DRAG_DEG_PER_PX));
        S->spin.last_x = g->x;
        S->spin.moved = true;
        break;
    case G_DRAG_MOVE: omni_spin_move(&S->spin, t_ms, g->x); break;
    case G_DRAG_END: omni_spin_up(&S->spin, t_ms); break;
    case G_TAP:   /* taps are never claimed: a tap anywhere is the mockup's short press */
        if (globe_face_skip(S->gf)) S->appear = 1;   /* the reveal skips to its end (R48/C15) */
        omni_spin_down(&S->spin, t_ms, g->x);
        omni_spin_up(&S->spin, t_ms);
        labels_apply(globe_face_now_ms());
        break;
    default: break;
    }
}

/* ------------------------------------------------------------ update */
static void update_awake(int64_t now)
{
    const bool set = omni_time_is_set(now);
    char hhmm[6] = "--:--", ap[3] = "", buf[24] = "";
    if (set) {
        omni_fmt_clock(now, settings()->h24, hhmm, ap);
        const time_t t = (time_t)now;
        struct tm tm;
        localtime_r(&t, &tm);
        snprintf(buf, sizeof buf, "%s %d %.3s", omni_day_name(tm.tm_wday), tm.tm_mday, omni_month_name(tm.tm_mon));
        omni_ascii_upper(buf);   /* "SUNDAY 25 OCT" */
    }
    theme_set_text(S->date, buf);   /* empty while the clock is unset */
    theme_place(S->date, THEME_CENTER, CX, DATE_BASE);
    char tm_txt[12];   /* "9:30 pm": the mockup's tm.hm + ' ' + ap.toLowerCase() */
    snprintf(tm_txt, sizeof tm_txt, "%s%s%s", hhmm, ap[0] ? " " : "", ap);
    for (char *p = tm_txt; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    if (strcmp(lv_label_get_text(S->quiet), tm_txt) != 0) {
        lv_label_set_text(S->quiet, tm_txt);
        lv_label_set_text(S->loud, tm_txt);
        S->base = 0;   /* re-centre */
    }
    labels_apply(globe_face_now_ms());
}

static void face_update(int64_t now, bool glance)
{
    if (S == NULL) return;
    if (glance) {
        lv_obj_add_flag(S->awake, LV_OBJ_FLAG_HIDDEN);
        glance_show(S->glance, true);
        glance_update(S->glance, now, settings()->h24, NULL);
        return;
    }
    glance_show(S->glance, false);
    lv_obj_remove_flag(S->awake, LV_OBJ_FLAG_HIDDEN);
    update_awake(now);
}

const omni_face_vt_t FACE_GLOBE_SPIN_VT = {
    .name = "globe-spin",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
    .wake = face_wake,
    .sleep = face_sleep,
    .claim = face_claim,
    .gesture = face_gesture,
};
