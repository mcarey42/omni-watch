/* The almanac: design/mockups/faces-practical.js, `id: 'almanac'` (frame, lines 1074-1132, and buildAlmRing's numerals)
 * and its sleepFrame. The maths and both renderers are omni_core's (omni/almanac.h).
 *
 * Awake: the 24-hour sky ring (omni_alm_ring_render into an opaque RGB565 box in PSRAM by alm_cache's low-priority
 * worker, never under the LVGL lock or on the create path (R140), and cached across face switches by date, zone, city
 * and ticks: until a ring for the key lands, the face shows the ring it has, or before any a plain ring, the night band
 * as an LVGL border), the hour numerals, the rise and set times, the sun's needle and bead, the day length and its
 * change since yesterday, and one of two centre panels: the moon (an 88 px RGB565A8 render by the same worker, again
 * only when round(f x 400) changes; the cached moon meanwhile) or the sun (altitude, azimuth, solar noon, shadow). A
 * tap at r < 120 swaps the panel; the choice is RAM only and outlives the face. Everything follows the minute: no frame
 * is drawn between minutes. Wake: the bead and needle sweep from midnight to now over 1.1 s (easeOut3; none under
 * reduced motion) inside a 400 ms fade through each object's opacity (R28); a tap ends it. Clock unset (D13): the night
 * ring, no ticks, bead or needle, "--h  --m", and no delta, moon or sun panel. Glance: the rise/set ticks and a sun
 * ring in dim brass over the shared glance with the sun sub-line, all offset by omni_orbit_offset.
 *
 * Timing (Task 9): each ring and moon render logs "almanac: ring <us> us (worker, stack free <n> B)" / "almanac: moon
 * ..." at INFO when it lands; the first ring of a key comes after the face's create, the next at local midnight or on a city
 * change. The create itself renders nothing. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "shell.h"
#include "glance.h"
#include "theme.h"
#include "assets.h"
#include "store.h"
#include "omni_fonts.h"
#include "omni_faces.h"
#include "assets_meta.h"
#include "globe_face.h"
#include "face_fade.h"
#include "alm_cache.h"
#include "omni/alm_face.h"
#include "omni/almanac.h"
#include "omni/power.h"
#include "omni/sun.h"
#include "omni/timefmt.h"

#define CX THEME_CX
#define CY THEME_CY
#define NUM_R 178                  /* hour numerals, 500 11px FM, rgba(163,154,140,.85), baseline 'middle' */
#define MID_MONO11 4               /* 'middle' to alphabetic baseline: (1.025 - 0.275) / 2 em of Plex Mono */
#define MID_MONO13 5
#define MID_FD13 3                 /* Cormorant SC 13 px (face_omni.c, measured against the mockup) */
#define RS_R 158                   /* rise/set time at r 158 - 6, its caption at r 158 + 9 */
#define NEEDLE_R0 130
#define NEEDLE_R1 186
#define BEAD_R 202
#define GLOW_SCALE (LV_SCALE_NONE * 20 / ASSET_SUN_GLOW_PX)   /* the 52 px glow (r 26) at r 20 */
#define DOT_UP 14                  /* #fff0c4 disc, 14 px */
#define DOT_DOWN 17                /* black disc r 7.5 stroked 1.8 px brassHi: a 2 px border from r 6.5 to 8.5 */
#define GL_TICK_R0 192
#define GL_TICK_R1 212
#define GL_RING 12                 /* the glance's sun ring: r 5, 2 px (outer 6) */
#define GL_OPA THEME_OPA(0.35)
#define TAP_R 120
#define SWEEP_MS 1100
#define FADE_MS 400
#define TICK_MS 33
#define MOON_D 88
#define DELTA_GREEN 0xc8d9a8
#define NEEDLE_RGB 0xecd29a        /* (236,210,154) */
#define POLL_MS 50                 /* while a render is in flight: how soon a finished one is shown */
#define PLAIN_R 215                /* the plain ring: the band 189..215 as a border, until a render lands */
#define PLAIN_W 26

typedef struct {
    lv_obj_t *awake, *ring, *num[8], *rs_time[2], *rs_cap[2], *needle, *glow, *dot_up, *dot_down;
    lv_obj_t *daylight, *daylen, *delta, *moon, *moon_name, *moon_txt, *alt, *alt_cap, *line[3];
    lv_obj_t *gl, *gl_tick[2], *gl_ring;
    lv_obj_t *plain;
    glance_t *glance;
    lv_image_dsc_t glow_dsc;
    lv_point_precise_t needle_pt[2], gl_pt[2][2];
    lv_timer_t *timer, *poll;
    bool running, revealing;
    int64_t t0_ms;                 /* the reveal's start */
    lv_opa_t opa;
    /* what is shown */
    omni_alm_ring_key_t ring_key;  /* the ring wanted now (R140) */
    omni_alm_ring_key_t shown_key; /* the ring the image shows (ring_has) */
    bool ring_has, moon_has;       /* the images show a cached buffer */
    int64_t key_min;
    int key_city;
    bool key_h24, key_set;
    int key_panel;
    float sun_h;                   /* now as a local clock hour, < 0: no sun (clock unset) */
    bool sun_up;                   /* above -0.8 deg */
    double alt_deg;                /* the sun's altitude now (day_state) */
    float rs_h[2];                 /* local clock hours of rise and set, < 0: none */
    int64_t gl_min;
    int8_t gl_dx, gl_dy;
    bool gl_ok;
} face_t;

static face_t *F;
static uint8_t s_panel;            /* 0 the moon, 1 the sun: RAM only, kept across the face's destroy */

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* hourAngle(h) = pi/2 + h/24 * 2pi (canvas radians: midnight at the bottom, clockwise) */
static float hour_angle(float h) { return (float)M_PI / 2 + h / 24.0f * 2.0f * (float)M_PI; }
/* The point at radius r on angle a, plus (dx, dy). Not inlined: the face runs once a minute, and size counts here. */
static void __attribute__((noinline)) polar(float a, float r, int32_t dx, int32_t dy, int32_t *x, int32_t *y)
{
    *x = CX + dx + lroundf(cosf(a) * r);
    *y = CY + dy + lroundf(sinf(a) * r);
}

static float local_hour(int64_t t)
{
    const time_t tt = (time_t)t;
    struct tm tm;
    if (!localtime_r(&tt, &tm)) return 0;
    return (float)tm.tm_hour + (float)tm.tm_min / 60.0f + (float)tm.tm_sec / 3600.0f;
}

/* An lv_line along angle a from r0 to r1, its object only as large as the segment (a full-screen line object would
 * invalidate the whole screen on every move). */
static void __attribute__((noinline)) ray(lv_obj_t *l, lv_point_precise_t pt[2], float a, float r0, float r1,
                                          int32_t dx, int32_t dy)
{
    int32_t x0, y0, x1, y1;
    polar(a, r0, dx, dy, &x0, &y0);
    polar(a, r1, dx, dy, &x1, &y1);
    const int32_t ox = x0 < x1 ? x0 : x1, oy = y0 < y1 ? y0 : y1;
    lv_obj_set_pos(l, ox, oy);
    pt[0].x = x0 - ox;
    pt[0].y = y0 - oy;
    pt[1].x = x1 - ox;
    pt[1].y = y1 - oy;
    lv_line_set_points(l, pt, 2);
}

static lv_obj_t *line_create(lv_obj_t *parent, uint32_t rgb, lv_opa_t opa, int32_t w)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_color(l, lv_color_hex(rgb), 0);
    lv_obj_set_style_line_opa(l, opa, 0);
    lv_obj_set_style_line_width(l, w, 0);
    return l;
}

static lv_obj_t *disc_create(lv_obj_t *parent, int32_t d, uint32_t fill, int32_t border, uint32_t border_rgb,
                             lv_opa_t border_opa, lv_opa_t fill_opa)
{
    lv_obj_t *o = theme_box(parent, 0, 0, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(o, fill_opa, 0);
    lv_obj_set_style_border_width(o, border, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border_rgb), 0);
    lv_obj_set_style_border_opa(o, border_opa, 0);
    return o;
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void text_at(lv_obj_t *l, const char *s, int32_t x, int32_t base)
{
    theme_set_text(l, s);
    theme_place(l, THEME_CENTER, x, base);
}

/* ------------------------------------------------------------ the ring, the bead and the needle */
static void ring_show(lv_draw_buf_t *b)
{
    if (lv_image_get_src(F->ring) != b) lv_image_set_src(F->ring, b);
    F->ring_has = true;
    F->shown_key = F->ring_key;   /* b is the cache's ring for ring_key */
    show(F->ring, true);
    show(F->plain, false);
}

static void poll_start(void)
{
    if (F->poll && alm_cache_busy()) lv_timer_resume(F->poll);
}

/* The ring for this minute's key: the cached one, else a render asked of the worker while the ring we have (or the
 * plain ring) stays. Never renders here (R140). */
static void ring_update(int64_t now, const omni_city_t *c, bool set)
{
    omni_alm_ring_key_t k;
    omni_alm_ring_key(&k, now, getenv("TZ"), (int)settings()->city, set, F->rs_h[0], F->rs_h[1]);
    if (F->ring_has && omni_alm_ring_key_eq(&k, &F->shown_key)) {
        if (!omni_alm_ring_key_eq(&k, &F->ring_key)) {   /* back to the ring shown (review M2): drop the other */
            F->ring_key = k;
            alm_cache_ring_keep(&k);
        }
        return;
    }
    F->ring_key = k;
    lv_draw_buf_t *b = alm_cache_ring(&k);
    if (b) {
        ring_show(b);
        return;
    }
    const alm_ring_job_t j = {.key = k, .now = now, .lat = c ? c->lat : 0, .lon = c ? c->lon : 0,
                              .rise_h = F->rs_h[0], .set_h = F->rs_h[1], .set = set};
    alm_cache_ring_want(&j);
    poll_start();
}

static void poll_tick(lv_timer_t *t)
{
    (void)t;
    if (F == NULL) return;
    const unsigned ch = alm_cache_poll(F->ring, F->moon);   /* re-points the images before freeing the old buffers */
    lv_draw_buf_t *b = (ch & 1) ? alm_cache_ring(&F->ring_key) : NULL;
    if (b) ring_show(b);
    if (ch & 2) {
        F->moon_has = true;
        show(F->moon, F->key_set && s_panel == 0);
    }
    if (!alm_cache_busy()) lv_timer_pause(F->poll);
}

/* The bead and needle at the sweep's k (0 = midnight, 1 = now). */
static void sun_place(float k)
{
    const bool on = F->sun_h >= 0;
    show(F->needle, on);
    show(F->glow, on && F->sun_up);
    show(F->dot_up, on && F->sun_up);
    show(F->dot_down, on && !F->sun_up);
    if (!on) return;
    const float a0 = hour_angle(0), a = a0 + (hour_angle(F->sun_h) - a0) * k;
    ray(F->needle, F->needle_pt, a, NEEDLE_R0, NEEDLE_R1, 0, 0);
    int32_t bx, by;
    polar(a, BEAD_R, 0, 0, &bx, &by);
    lv_obj_set_pos(F->glow, bx - ASSET_SUN_GLOW_PX, by - ASSET_SUN_GLOW_PY);
    lv_obj_set_pos(F->dot_up, bx - DOT_UP / 2, by - DOT_UP / 2);
    lv_obj_set_pos(F->dot_down, bx - DOT_DOWN / 2, by - DOT_DOWN / 2);
}

static float sweep_k(int64_t t)
{
    if (!F->revealing || settings()->reduced_motion) return 1;
    const int64_t e = t - F->t0_ms;
    if (e >= SWEEP_MS) return 1;
    const float x = e <= 0 ? 0 : (float)e / SWEEP_MS, u = 1 - x;
    return 1 - u * u * u;   /* easeOut3 */
}

/* ------------------------------------------------------------ the minute's state */
static void moon_update(int64_t now)
{
    omni_alm_moon_t m;
    omni_alm_moon(now, &m);
    const long key = lround(m.f * 400);
    long have;
    lv_draw_buf_t *b = alm_cache_moon(&have);   /* the cached moon, even of a key some hours old, until the new lands */
    if (b && lv_image_get_src(F->moon) != b) lv_image_set_src(F->moon, b);
    F->moon_has = b != NULL;
    show(F->moon, F->moon_has);
    if (b == NULL || have != key) {
        alm_cache_moon_want(key, m.f);
        poll_start();
    }
    char buf[40];
    text_at(F->moon_name, omni_alm_moon_name(m.name), CX, CY + 82);
    omni_alm_moon_text(&m, buf);
    text_at(F->moon_txt, buf, CX, CY + 101);
}

static void sun_panel_update(int64_t now, const omni_city_t *c, bool h24)
{
    char a[12], l0[24], l1[24], l2[32];
    omni_alm_alt_text(F->alt_deg, a);   /* day_state's altitude: one omni_alm_altaz per update (review m5) */
    omni_alm_sun_lines(now, c->lat, c->lon, h24, l0, l1, l2);
    text_at(F->alt, a, CX, CY + 22);
    text_at(F->line[0], l0, CX, CY + 50);
    text_at(F->line[1], l1, CX, CY + 68);
    text_at(F->line[2], l2, CX, CY + 86);
}

/* The day at home now: F->rs_h, F->sun_h and F->sun_up, for the face and its glance. False: no clock or no city. */
static bool day_state(int64_t now, const omni_city_t *c, omni_alm_day_t *d)
{
    const bool set = c != NULL && omni_time_is_set(now);
    *d = (omni_alm_day_t){0};
    F->rs_h[0] = F->rs_h[1] = F->sun_h = -1;
    if (!set) return false;
    omni_alm_day(now, c->lat, c->lon, d);
    if (d->kind == SUN_NORMAL) {
        F->rs_h[0] = local_hour(d->rise);
        F->rs_h[1] = local_hour(d->set);
    }
    F->sun_h = local_hour(now);
    double alt, az;
    omni_alm_altaz(now, c->lat, c->lon, &alt, &az);
    F->alt_deg = alt;
    F->sun_up = alt > -0.8;
    return true;
}

/* Everything the minute (or a city, 12/24 h, clock or panel change) moves. */
static void state_update(int64_t now, bool force)
{
    const omni_city_t *c = globe_face_home();
    const bool set = c != NULL && omni_time_is_set(now);
    const bool h24 = settings()->h24;
    const int city = (int)settings()->city;
    if (!force && now / 60 == F->key_min && city == F->key_city && h24 == F->key_h24 && set == F->key_set &&
        s_panel == F->key_panel)
        return;
    const bool numerals = force || h24 != F->key_h24;
    F->key_min = now / 60;
    F->key_city = city;
    F->key_h24 = h24;
    F->key_set = set;
    F->key_panel = s_panel;

    if (numerals)
        for (int i = 0; i < 8; i++) {
            char s[4];
            int32_t x, y;
            omni_alm_hour_label(i * 3, h24, s);
            polar(hour_angle((float)(i * 3)), NUM_R, 0, MID_MONO11, &x, &y);
            text_at(F->num[i], s, x, y);
        }

    omni_alm_day_t d;
    day_state(now, c, &d);
    const int64_t rs[2] = {d.rise, d.set};
    for (int i = 0; i < 2; i++) {
        const bool on = F->rs_h[i] >= 0;
        show(F->rs_time[i], on);
        show(F->rs_cap[i], on);
        if (!on) continue;
        char t[8];
        int32_t x, y;
        omni_fmt_short_time(rs[i], h24, t);
        polar(hour_angle(F->rs_h[i]), RS_R, 0, 0, &x, &y);
        text_at(F->rs_time[i], t, x, y - 6 + MID_MONO13);
        theme_place(F->rs_cap[i], THEME_CENTER, x, y + 9 + MID_FD13);
    }
    ring_update(now, c, set);
    sun_place(sweep_k(now_ms()));

    char buf[28];
    omni_alm_daylen_text(&d, set, buf);
    text_at(F->daylen, buf, CX, CY - 56);
    omni_alm_delta_text(&d, set, buf);
    lv_obj_set_style_text_color(F->delta, lv_color_hex(d.delta_s >= 0 ? DELTA_GREEN : THEME_INK_DIM), 0);
    text_at(F->delta, buf, CX, CY - 37);
    show(F->delta, set);

    const bool moon = set && s_panel == 0, sun = set && s_panel == 1;
    show(F->moon, moon && F->moon_has);
    show(F->moon_name, moon);
    show(F->moon_txt, moon);
    show(F->alt, sun);
    show(F->alt_cap, sun);
    for (int i = 0; i < 3; i++) show(F->line[i], sun);
    if (moon) moon_update(now);
    if (sun) sun_panel_update(now, c, h24);
}

/* ------------------------------------------------------------ the glance's own marks */
static void glance_marks(int64_t now)
{
    int8_t dx, dy;
    omni_orbit_offset(now, &dx, &dy);
    if (F->gl_ok && now / 60 == F->gl_min && dx == F->gl_dx && dy == F->gl_dy) return;
    F->gl_ok = true;
    F->gl_min = now / 60;
    F->gl_dx = dx;
    F->gl_dy = dy;
    omni_alm_day_t d;
    const bool set = day_state(now, globe_face_home(), &d);
    for (int i = 0; i < 2; i++) {
        show(F->gl_tick[i], F->rs_h[i] >= 0);
        if (F->rs_h[i] >= 0) ray(F->gl_tick[i], F->gl_pt[i], hour_angle(F->rs_h[i]), GL_TICK_R0, GL_TICK_R1, dx, dy);
    }
    show(F->gl_ring, set);
    if (set) {
        int32_t x, y;
        polar(hour_angle(F->sun_h), BEAD_R, dx - GL_RING / 2, dy - GL_RING / 2, &x, &y);
        lv_obj_set_pos(F->gl_ring, x, y);
    }
}

/* ------------------------------------------------------------ the reveal */
static void opa_apply(lv_opa_t opa)
{
    face_fade_children(F->awake, &F->opa, opa);   /* R28 */
}

static void reveal_end(void)
{
    F->revealing = false;
    if (F->timer) lv_timer_pause(F->timer);
    opa_apply(LV_OPA_COVER);
    sun_place(1);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (F == NULL || !F->running || !F->revealing) return;
    const int64_t e = now_ms() - F->t0_ms;
    if (e >= SWEEP_MS) {
        reveal_end();
        return;
    }
    opa_apply(e >= FADE_MS ? LV_OPA_COVER : (lv_opa_t)(e < 0 ? 0 : e * 255 / FADE_MS));
    sun_place(sweep_k(now_ms()));
}

/* ------------------------------------------------------------ build */
static void face_create(lv_obj_t *parent)
{
    F = heap_caps_calloc(1, sizeof *F, MALLOC_CAP_SPIRAM);
    if (F == NULL) return;
    F->opa = LV_OPA_COVER;
    F->awake = theme_box(parent, 0, 0, THEME_W, THEME_W);

    /* the ring: the cache's image (alm_cache, R140), and the plain ring (the night band) until one lands */
    alm_cache_start();
    uint8_t night[3];
    omni_alm_sky_rgb(-90.0, night);
    const uint32_t night_rgb = (uint32_t)night[0] << 16 | (uint32_t)night[1] << 8 | night[2];
    F->plain = disc_create(F->awake, 2 * PLAIN_R, 0, PLAIN_W, night_rgb, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_obj_set_pos(F->plain, CX - PLAIN_R, CY - PLAIN_R);
    F->ring = lv_image_create(F->awake);
    lv_obj_remove_flag(F->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(F->ring, OMNI_ALM_BOX_XY, OMNI_ALM_BOX_XY);
    lv_obj_add_flag(F->ring, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 8; i++)   /* 500 11px FM, rgba(163,154,140,.85) */
        F->num[i] = theme_label(F->awake, &font_plex_mono_400_11, THEME_INK_DIM, THEME_OPA(0.85), 0);
    for (int i = 0; i < 2; i++) {
        F->rs_time[i] = theme_label(F->awake, &font_plex_mono_500_13, THEME_INK, LV_OPA_COVER, 0);   /* 500 12.5px FM */
        F->rs_cap[i] = theme_label(F->awake, &font_cormorant_700_13, THEME_BRASS, LV_OPA_COVER, 2); /* 600 11px FD, 1.5 */
        lv_label_set_text(F->rs_cap[i], i ? "SUNSET" : "SUNRISE");
    }
    F->needle = line_create(F->awake, NEEDLE_RGB, THEME_OPA(0.45), 1);
    F->glow = lv_image_create(F->awake);
    lv_obj_remove_flag(F->glow, LV_OBJ_FLAG_CLICKABLE);
    if (assets_image(ASSET_SUN_GLOW, &F->glow_dsc)) lv_image_set_src(F->glow, &F->glow_dsc);
    lv_image_set_pivot(F->glow, ASSET_SUN_GLOW_PX, ASSET_SUN_GLOW_PY);
    lv_image_set_scale(F->glow, GLOW_SCALE);
    F->dot_up = disc_create(F->awake, DOT_UP, 0xfff0c4, 0, 0, LV_OPA_TRANSP, LV_OPA_COVER);
    F->dot_down = disc_create(F->awake, DOT_DOWN, 0x000000, 2, THEME_BRASS_HI, LV_OPA_COVER, LV_OPA_COVER);

    F->daylight = theme_label(F->awake, &font_cormorant_700_13, THEME_BRASS, LV_OPA_COVER, 2);   /* 600 13px FD, 2.5 (D4) */
    text_at(F->daylight, "DAYLIGHT", CX, CY - 88);
    F->daylen = theme_label(F->awake, &font_plex_sans_500_30, THEME_INK, LV_OPA_COVER, 0);       /* 500 30px FS */
    F->delta = theme_label(F->awake, &font_plex_mono_500_13, DELTA_GREEN, LV_OPA_COVER, 0);      /* 500 12px FM */

    F->moon = lv_image_create(F->awake);   /* the cache's moon (alm_cache) */
    lv_obj_remove_flag(F->moon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(F->moon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(F->moon, CX - MOON_D / 2, CY - 26);
    F->moon_name = theme_label(F->awake, &font_cormorant_700_15, THEME_INK, LV_OPA_COVER, 1);    /* 600 15px FD, 1.2 */
    F->moon_txt = theme_label(F->awake, &font_plex_mono_400_11, THEME_INK_DIM, LV_OPA_COVER, 0); /* 500 11.5px FM */
    F->alt = theme_label(F->awake, &font_plex_sans_500_40, THEME_INK, LV_OPA_COVER, 0);          /* 500 40px FS */
    F->alt_cap = theme_label(F->awake, &font_cormorant_700_13, THEME_BRASS, LV_OPA_COVER, 2);    /* 600 12px FD, 2 */
    text_at(F->alt_cap, "SUN ALTITUDE", CX, CY - 12);
    for (int i = 0; i < 3; i++)   /* 500 12px FM */
        F->line[i] = theme_label(F->awake, &font_plex_mono_500_13, THEME_INK_DIM, LV_OPA_COVER, 0);

    F->gl = theme_box(parent, 0, 0, THEME_W, THEME_W);
    lv_obj_add_flag(F->gl, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; i++) F->gl_tick[i] = line_create(F->gl, THEME_BRASS, GL_OPA, 2);
    F->gl_ring = disc_create(F->gl, GL_RING, 0, 2, THEME_BRASS, GL_OPA, LV_OPA_TRANSP);
    F->glance = glance_create(parent);

    F->timer = lv_timer_create(tick, TICK_MS, NULL);
    if (F->timer) lv_timer_pause(F->timer);
    F->poll = lv_timer_create(poll_tick, POLL_MS, NULL);
    if (F->poll) lv_timer_pause(F->poll);
    state_update((int64_t)time(NULL), true);   /* asks the worker for what the cache lacks; renders nothing itself */
}

static void face_destroy(void)
{
    if (F == NULL) return;
    if (F->timer) lv_timer_delete(F->timer);
    if (F->poll) lv_timer_delete(F->poll);
    lv_obj_delete(F->awake);   /* before the worker stops: a render already landed is kept, shown by nothing */
    alm_cache_stop();
    glance_free(F->glance);
    heap_caps_free(F);
    F = NULL;
}

/* ------------------------------------------------------------ hooks */
static void face_wake(bool reveal)
{
    if (F == NULL) return;
    F->running = true;
    F->revealing = reveal && F->timer != NULL;
    F->t0_ms = now_ms();
    state_update((int64_t)time(NULL), false);
    if (!F->revealing) {
        reveal_end();
        return;
    }
    opa_apply(LV_OPA_TRANSP);
    sun_place(sweep_k(F->t0_ms));
    lv_timer_resume(F->timer);
}

static void face_sleep(void)
{
    if (F == NULL) return;
    F->running = false;
    reveal_end();
}

static void face_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    (void)t_ms;
    if (F == NULL || !F->running || g->kind != G_TAP) return;
    if (F->revealing) {   /* a tap anywhere during a reveal ends it (spec §8) */
        reveal_end();
        return;
    }
    const int dx = g->x0 - CX, dy = g->y0 - CY;
    if (dx * dx + dy * dy >= TAP_R * TAP_R) return;
    s_panel ^= 1;
    state_update((int64_t)time(NULL), false);
}

static void face_update(int64_t now, bool glance)
{
    if (F == NULL) return;
    if (glance) {
        lv_obj_add_flag(F->awake, LV_OBJ_FLAG_HIDDEN);
        glance_marks(now);
        lv_obj_remove_flag(F->gl, LV_OBJ_FLAG_HIDDEN);
        char sub[40] = "";
        const omni_city_t *c = globe_face_home();
        if (c && omni_time_is_set(now)) omni_sun_glance_text(now, c->lat, c->lon, settings()->h24, sub, sizeof sub);
        glance_show(F->glance, true);
        glance_update(F->glance, now, settings()->h24, sub);
        return;
    }
    glance_show(F->glance, false);
    lv_obj_add_flag(F->gl, LV_OBJ_FLAG_HIDDEN);
    F->gl_ok = false;
    lv_obj_remove_flag(F->awake, LV_OBJ_FLAG_HIDDEN);
    state_update(now, false);
}

const omni_face_vt_t FACE_ALMANAC_VT = {
    .name = "almanac",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
    .wake = face_wake,
    .sleep = face_sleep,
    .gesture = face_gesture,
};
