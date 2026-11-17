/* The real-time globe face: design/mockups/faces-practical.js, `id: 'globe-live'` (frame, lines 378-457) and its
 * sleepFrame (glanceTime with the sun sub-line). Positions, sizes and colours are the mockup's.
 *
 * Awake: the globe (globe_face / globe_view "live": the hour ring and dome are its base and overlay assets, the halo
 * and its sun-side lobe are rendered into the frame), the sun bead on the ring and the sun glow on the disc, the
 * pulsing home marker, the date (Cormorant SC), sunrise and sunset in the side margins, the time in fixed cells
 * (IBM Plex Sans 50 px) with AM/PM, and the home city. Unset clock: "--:--", em-dash margins, no date, the disc lit
 * from the view centre with no lobe, bead or glow (R48/C3). Drags inside the disc turn it (omni_drift); it drifts
 * home 4 s after release. Wake: the 1.4 s spin-up (omni_reveal_live), a fade with reduced motion; a tap skips it.
 * Glance: the shared glance layout with the sunrise/sunset countdown; the globe is stopped.
 *
 * The bead, glow and home marker are LVGL objects over the globe box. They are positioned for a frame when the
 * globe shows that frame: the tick computes them with the frame and globe_face applies them (shown) in the timer run
 * that swaps it in, so they never lead or lag the globe. A revealing wake hides them until the reveal's first
 * frame (blanked): the box is black then, and the old positions belong to the frame from before the sleep (I1). */
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
#include "assets.h"
#include "assets_meta.h"
#include "store.h"
#include "omni/cities.h"
#include "omni/globe.h"
#include "omni/globe_motion.h"
#include "omni/sun.h"
#include "omni/timefmt.h"

#define CX THEME_CX
#define GY 212                /* mockup globe centre y (gy) */
#define R 131                 /* mockup globe radius (G.R for 262 px) */
#define BEAD_R (R + 9)        /* the ring the bead rides on */
#define MARK_SIZE 36          /* home marker object: the pulse ring reaches r 16 + 0.6 */
#define MARK_MS 66            /* home marker pulse redraw */
#define PULSE_MS 2400
#define HALO_STRENGTH 0.9f
#define TIME_CELL 29          /* drawCells(..., 29, 14) at 50 px */
#define TIME_COLON 14
#define TIME_BASE 398
#define MARGIN_X 52           /* SUNRISE / SUNSET centres: 52 and W - 52 */
#define MAX_CELLS GLANCE_MAX_CELLS

typedef struct {               /* what the bead, glow and marker show for one frame */
    bool set;                  /* clock set: bead and glow exist */
    float sp[3], hp[3];        /* projected sun and home (unit disc, z) */
    float fade;
} overlay_t;

typedef struct {
    lv_obj_t *awake, *date, *rise_lbl, *set_lbl, *rise, *set, *cell[MAX_CELLS], *ampm, *city, *chev[2];
    lv_obj_t *bead, *glow, *mark;
    glance_t *glance;
    globe_face_t *gf;
    int32_t gy;                /* the disc centre y on screen (the box's even origin moves it 1 px up: Task 11) */
    lv_image_dsc_t bead_dsc, glow_dsc;
    lv_timer_t *mark_timer;
    omni_drift_t drift;
    bool running;
    float fade;                /* the reveal's label fade, as applied */
    overlay_t pend;            /* the overlays of the last submitted frame (globe_face shows them with it) */
    int32_t mark_x, mark_y;    /* home marker centre, screen */
    int64_t marg_min;          /* the minute the margins were computed for (-1: none) */
    uint16_t marg_city;
    bool marg_h24, marg_set;
} live_t;

static live_t *L;

/* ------------------------------------------------------------ overlays (bead, glow, home marker) */
static void overlay_apply(const overlay_t *o)
{
    const lv_opa_t opa = (lv_opa_t)lroundf(o->fade * 255);
    /* bead: on the ring at the sun's direction (always, when the clock is set, even with the sun behind) */
    if (o->set) {
        const float a = atan2f(o->sp[1], o->sp[0]);
        lv_obj_set_pos(L->bead, CX + lroundf(cosf(a) * BEAD_R) - ASSET_SUN_BEAD_PX,
                       L->gy + lroundf(sinf(a) * BEAD_R) - ASSET_SUN_BEAD_PY);
        lv_obj_set_style_image_opa(L->bead, opa, 0);
        lv_obj_remove_flag(L->bead, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(L->bead, LV_OBJ_FLAG_HIDDEN);
    }
    /* glow: where the sun is overhead, when that is on the near side */
    if (o->set && o->sp[2] > 0) {
        lv_obj_set_pos(L->glow, CX + lroundf(o->sp[0] * R) - ASSET_SUN_GLOW_PX,
                       L->gy + lroundf(o->sp[1] * R) - ASSET_SUN_GLOW_PY);
        lv_obj_set_style_image_opa(L->glow, opa, 0);
        lv_obj_remove_flag(L->glow, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(L->glow, LV_OBJ_FLAG_HIDDEN);
    }
    /* home marker: hidden while the reveal fades in (it draws at fixed opacities; no layer opacity) */
    if (o->hp[2] > 0 && o->fade >= 1) {
        L->mark_x = CX + lroundf(o->hp[0] * R);
        L->mark_y = L->gy + lroundf(o->hp[1] * R);
        lv_obj_set_pos(L->mark, L->mark_x - MARK_SIZE / 2, L->mark_y - MARK_SIZE / 2);
        lv_obj_remove_flag(L->mark, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(L->mark, LV_OBJ_FLAG_HIDDEN);
    }
}

static void overlay_shown(globe_face_t *gf)
{
    const live_t *l = globe_face_user(gf);
    overlay_apply(&l->pend);
}

static void overlay_hide(globe_face_t *gf)
{
    const live_t *l = globe_face_user(gf);
    lv_obj_add_flag(l->bead, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(l->glow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(l->mark, LV_OBJ_FLAG_HIDDEN);
}

static void mark_draw(lv_event_t *e)
{
    const uint32_t p = (uint32_t)(lv_tick_get() % PULSE_MS);
    globe_face_home_ring(lv_event_get_layer(e), L->mark_x, L->mark_y, 5.5f, 1.8f, (float)p / PULSE_MS);
}

static void mark_tick(lv_timer_t *t)
{
    (void)t;
    if (!lv_obj_has_flag(L->mark, LV_OBJ_FLAG_HIDDEN)) lv_obj_invalidate(L->mark);
}

/* ------------------------------------------------------------ labels */
/* The reveal's fade, through each label's own opacity (R28: no parent opacity). */
static void labels_fade(float fade)
{
    if (fade == L->fade) return;
    L->fade = fade;
    const lv_opa_t opa = (lv_opa_t)lroundf(fade * 255);
    lv_obj_t *const all[] = {L->date, L->rise_lbl, L->set_lbl, L->rise, L->set, L->ampm, L->city};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) lv_obj_set_style_text_opa(all[i], opa, 0);
    for (int i = 0; i < MAX_CELLS; i++) lv_obj_set_style_text_opa(L->cell[i], opa, 0);
    lv_obj_invalidate(L->chev[0]);
    lv_obj_invalidate(L->chev[1]);
}

/* The sun chevrons: 1.3 px brass, up over SUNRISE at (47, 187)-(52, 182)-(57, 187), down over SUNSET (user data:
 * the centre x; the sunset one points down). */
static void chev_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const int32_t x = (int32_t)(intptr_t)lv_event_get_user_data(e);
    const lv_opa_t opa = (lv_opa_t)lroundf(L->fade * 0.75f * 255);   /* 2 px at 0.75: the 1.3 px stroke's weight */
    const bool up = x == MARGIN_X;
    const int32_t wing = up ? GY - 25 : GY - 30, tip = up ? GY - 30 : GY - 25;
    /* a 1.3 px canvas stroke on integer coordinates spreads over 2 px; LVGL's 1 px line reads too thin beside it */
    theme_draw_line(layer, x - 5, wing, x, tip, 2, THEME_BRASS, opa, true);
    theme_draw_line(layer, x, tip, x + 5, wing, 2, THEME_BRASS, opa, true);
}

/* ------------------------------------------------------------ globe */
static void tick(globe_face_t *gf, const globe_tick_t *t, globe_view_frame_t *f, bool *animating)
{
    omni_drift_step(&L->drift, t->now_ms, t->dt);
    const omni_city_t *c = globe_face_home();
    const omni_globe_cfg_t *cfg = globe_view_cfg(globe_face_view(gf));
    const double lon0 = omni_wrap180((c ? c->lon : 0) + L->drift.off_lon + t->rv.lon_off_deg);
    f->lon0_deg = lon0;
    f->gain = t->rv.gain;
    f->fade = (uint8_t)lroundf(t->rv.fade * 255);

    omni_sun_t raw;
    overlay_t o = {.set = globe_face_sun((int64_t)time(NULL), cfg->lat0_deg, lon0, &f->sun, &raw),
                   .sp = {0, 0, -1}, .hp = {0, 0, -1}, .fade = t->rv.fade};
    if (o.set) omni_globe_project(cfg, raw.subsolar_lat, raw.subsolar_lon, lon0, o.sp);
    if (c) omni_globe_project(cfg, c->lat, c->lon, lon0, o.hp);
    f->halo = true;
    f->halo_strength = HALO_STRENGTH;
    if (o.set && o.sp[2] > -0.3f) {   /* the lobe toward the sun, unless it is well behind the globe */
        f->halo_dir[0] = o.sp[0];
        f->halo_dir[1] = o.sp[1];
    }
    L->pend = o;
    labels_fade(t->rv.fade);
    *animating = *animating || L->drift.dragging || omni_drift_moving(&L->drift);
}

/* The view sits under the bead, glow and marker (index 0); it is rebuilt there for a new home city (lat0 follows). */
static const globe_face_ops_t OPS = {
    .view = "live",
    .cad = OMNI_CAD_LIVE,
    .z = 0,
    .reveal = omni_reveal_live,
    .blank = GLOBE_BLANK_BLACK,   /* the reveal starts at fade 0 */
    .tick = tick,
    .shown = overlay_shown,
    .blanked = overlay_hide,
};

/* ------------------------------------------------------------ build */
static lv_obj_t *image(lv_obj_t *parent, const char *name, lv_image_dsc_t *dsc)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    if (assets_image(name, dsc)) lv_image_set_src(img, dsc);
    return img;
}

static void face_create(lv_obj_t *parent)
{
    L = heap_caps_calloc(1, sizeof *L, MALLOC_CAP_SPIRAM);
    if (L == NULL) return;
    L->marg_min = -1;
    L->fade = -1;
    L->awake = theme_box(parent, 0, 0, THEME_W, THEME_W);

    L->glow = image(L->awake, ASSET_SUN_GLOW, &L->glow_dsc);
    L->bead = image(L->awake, ASSET_SUN_BEAD, &L->bead_dsc);
    L->mark = theme_box(L->awake, 0, 0, MARK_SIZE, MARK_SIZE);
    lv_obj_add_flag(L->mark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(L->mark, mark_draw, LV_EVENT_DRAW_MAIN, NULL);
    L->gf = globe_face_create(L->awake, &OPS, L);
    if (L->gf) {
        int32_t bx, by;
        globe_face_box(L->gf, &bx, &by);
        L->gy = by + globe_view_cfg(globe_face_view(L->gf))->size / 2;
    }

    L->date = theme_label(L->awake, &font_cormorant_700_19, THEME_BRASS, LV_OPA_COVER, 2);       /* 600 19px FD, 2.2 */
    L->rise_lbl = theme_label(L->awake, &font_plex_sans_600_10, THEME_INK_DIM, LV_OPA_COVER, 2); /* 600 9.5px FS, 1.6 */
    L->set_lbl = theme_label(L->awake, &font_plex_sans_600_10, THEME_INK_DIM, LV_OPA_COVER, 2);
    L->rise = theme_label(L->awake, &font_plex_mono_500_16, THEME_INK, LV_OPA_COVER, 0);          /* 500 16px FM */
    L->set = theme_label(L->awake, &font_plex_mono_500_16, THEME_INK, LV_OPA_COVER, 0);
    lv_label_set_text(L->rise_lbl, "SUNRISE");
    lv_label_set_text(L->set_lbl, "SUNSET");
    theme_place(L->rise_lbl, THEME_CENTER, MARGIN_X, GY - 8);
    theme_place(L->set_lbl, THEME_CENTER, THEME_W - MARGIN_X, GY - 8);
    for (int i = 0; i < 2; i++) {
        const int32_t x = i ? THEME_W - MARGIN_X : MARGIN_X;
        L->chev[i] = theme_box(L->awake, x - 8, GY - 33, 16, 12);
        lv_obj_add_event_cb(L->chev[i], chev_draw, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)x);
    }
    for (int i = 0; i < MAX_CELLS; i++)
        L->cell[i] = theme_label(L->awake, &font_plex_sans_500_50, THEME_INK, LV_OPA_COVER, 0);  /* 500 50px FS */
    L->ampm = theme_label(L->awake, &font_plex_sans_600_13, THEME_BRASS, LV_OPA_COVER, 1);      /* 600 13px FS, 1 */
    L->city = theme_label(L->awake, &font_plex_sans_500_11, THEME_INK_DIM, LV_OPA_COVER, 2);    /* 500 10.5px FS, 1.8 */

    L->mark_timer = lv_timer_create(mark_tick, MARK_MS, NULL);
    if (L->mark_timer) lv_timer_pause(L->mark_timer);
    L->glance = glance_create(parent);
    labels_fade(0);   /* until the first wake's reveal */
}

static void face_destroy(void)
{
    if (L == NULL) return;
    if (L->mark_timer) lv_timer_delete(L->mark_timer);
    globe_face_destroy(L->gf);   /* stops the worker before the objects go with the parent */
    glance_free(L->glance);
    heap_caps_free(L);
    L = NULL;
}

/* ------------------------------------------------------------ hooks */
static void face_wake(bool reveal)
{
    if (L == NULL) return;
    memset(&L->drift, 0, sizeof L->drift);   /* mockup wake(): back home, no velocity */
    L->running = true;
    globe_face_wake(L->gf, reveal);
    labels_fade(globe_face_revealing(L->gf) ? 0 : 1);   /* no view (no assets / PSRAM): no reveal to fade them in */
    if (L->mark_timer) lv_timer_resume(L->mark_timer);
}

static void face_sleep(void)
{
    if (L == NULL) return;
    L->running = false;
    if (L->drift.dragging) omni_drift_up(&L->drift, L->drift.last_t);   /* a drag cut by sleep ends here */
    globe_face_sleep(L->gf);
    if (L->mark_timer) lv_timer_pause(L->mark_timer);
}

/* INPUT task, no lock: constant geometry only (the face may be being destroyed). */
static bool face_claim(int16_t x0, int16_t y0)
{
    const float dx = (float)(x0 - CX), dy = (float)(y0 - GY);
    return dx * dx + dy * dy <= (float)(R * R);
}

static void face_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    if (L == NULL || !L->running || L->gf == NULL) return;
    switch (g->kind) {
    case G_DRAG_START:
        /* down where the finger went down; the 12 px that made it a drag are followed at once, without the
         * velocity a 0 ms move would give */
        omni_drift_down(&L->drift, t_ms, g->x0);
        L->drift.off_lon += globe_face_drag_lead(g, 0.45f);
        L->drift.last_x = g->x;
        globe_face_touch(L->gf);
        globe_face_kick(L->gf);   /* a frame now: at rest the next one is up to 30 s away */
        break;
    case G_DRAG_MOVE: omni_drift_move(&L->drift, t_ms, g->x); break;
    case G_DRAG_END: omni_drift_up(&L->drift, t_ms); break;
    case G_TAP: globe_face_skip(L->gf); break;   /* a tap skips the reveal to its end */
    default: break;
    }
}

/* ------------------------------------------------------------ update */
static void update_margins(int64_t now, bool set, bool h24)
{
    const uint16_t city = settings()->city;
    if (now / 60 == L->marg_min && city == L->marg_city && h24 == L->marg_h24 && set == L->marg_set) return;
    L->marg_min = now / 60;
    L->marg_city = city;
    L->marg_h24 = h24;
    L->marg_set = set;
    const omni_city_t *c = globe_face_home();
    char rise[8] = "\xe2\x80\x94", sset[8] = "\xe2\x80\x94";   /* em dash */
    if (set && c) omni_sun_margins(now, c->lat, c->lon, h24, rise, sset);
    theme_set_text(L->rise, rise);
    theme_set_text(L->set, sset);
    theme_place(L->rise, THEME_CENTER, MARGIN_X, GY + 12);
    theme_place(L->set, THEME_CENTER, THEME_W - MARGIN_X, GY + 12);
}

static void update_awake(int64_t now)
{
    const bool set = omni_time_is_set(now), h24 = settings()->h24;
    char hhmm[6] = "--:--", ap[3] = "", buf[48] = "";
    struct tm tm = {0};
    if (set) {
        const time_t t = (time_t)now;
        localtime_r(&t, &tm);
        omni_fmt_clock(now, h24, hhmm, ap);
        snprintf(buf, sizeof buf, "%s \xc2\xb7 %d %s", omni_day_name(tm.tm_wday), tm.tm_mday, omni_month_name(tm.tm_mon));
    }
    theme_set_text(L->date, buf);   /* hidden (empty) while the clock is unset */
    theme_place(L->date, THEME_CENTER, CX, 56);

    update_margins(now, set, h24);

    const float tcx = (float)CX - (ap[0] ? 12 : 0);
    const float tw = glance_place_cells(L->cell, hhmm, tcx, TIME_BASE, TIME_CELL, TIME_COLON);
    for (int i = 0; i < MAX_CELLS; i++)   /* the unset dashes are a placeholder, not a time: dimmer (as digital) */
        lv_obj_set_style_text_color(L->cell[i], lv_color_hex(set ? THEME_INK : THEME_INK_DIM), 0);
    theme_set_text(L->ampm, ap);
    theme_place(L->ampm, THEME_LEFT, lroundf(tcx + tw / 2 + 5), 380);

    const omni_city_t *c = globe_face_home();
    snprintf(buf, sizeof buf, "%s, %s", c ? c->name : "", c ? c->country : "");
    omni_ascii_upper(buf);
    theme_set_text(L->city, c ? buf : "");
    theme_place(L->city, THEME_CENTER, CX, 424);
}

static void face_update(int64_t now, bool glance)
{
    if (L == NULL) return;
    /* A new home city (a new view at its lat0), or a clock set or stepped (a frame now: its sun, lobe, bead, glow and
     * marker follow; the text below is current already). */
    globe_face_update(L->gf, now);
    const bool h24 = settings()->h24;
    if (glance) {
        lv_obj_add_flag(L->awake, LV_OBJ_FLAG_HIDDEN);
        char sub[40] = "";
        const omni_city_t *c = globe_face_home();
        if (c && omni_time_is_set(now)) omni_sun_glance_text(now, c->lat, c->lon, h24, sub, sizeof sub);
        glance_show(L->glance, true);
        glance_update(L->glance, now, h24, sub);
        return;
    }
    glance_show(L->glance, false);
    lv_obj_remove_flag(L->awake, LV_OBJ_FLAG_HIDDEN);
    update_awake(now);
}

const omni_face_vt_t FACE_GLOBE_LIVE_VT = {
    .name = "globe-live",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
    .wake = face_wake,
    .sleep = face_sleep,
    .claim = face_claim,
    .gesture = face_gesture,
};
