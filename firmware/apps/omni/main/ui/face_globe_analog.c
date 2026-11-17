/* The analog globe face: design/mockups/faces-practical.js, `id: 'globe-analog'` (frame, lines 687-726, and its
 * sleepFrame, lines 728-742). Positions, sizes and colours are the mockup's: centre (233, 233), globe R 150.
 *
 * Awake: the dial (ana_dial, full screen, PSRAM copy) with the globe in its bezel (globe_face / globe_view "analog":
 * the dome and the dial's inner edge are its overlay, the dial itself shows outside the disc), the home marker, the
 * date window at 3 o'clock, and the hands: hour and minute (ana_hour, ana_min) over their shadow images at (+3, +4),
 * the seconds hand in 8 beats a second, the cap on top. The hands are drawn once per position into sprites that LVGL
 * only blends (sprite_t). Wake: the globe swings in and brightens (omni_reveal_analog) while the hands sweep from 12
 * to the time (its settle, without the shadows: spec 7.3); the dial and the date show at once (D6). A tap during that skips both to their end (R48/C15); after it, a tap on the date window flips the
 * globe between home and the subsolar view. Unset clock: the disc lit from the view centre (R48/C3), the hands at 12
 * and "--" in the window. Glance: the 12 dots, the hour and minute outlines and the centre ring, dim and orbiting
 * (omni_orbit_offset); the globe is stopped.
 *
 * The home marker is positioned for a frame when the globe shows that frame (as live's): the tick computes it with
 * the frame and globe_face applies it (shown) in the timer run that swaps that frame in.
 *
 * The reveal brightens the globe through its gain alone (0.36 to 0.9 of the look) and submits fade 255 (I2): a fade
 * multiplies the whole box, and the box outside the disc is the dial itself (under_name), so fading it put a dark
 * square on the dial for the first 300 ms of every wake and swipe in. Fading toward the dial instead would keep that
 * look but not the kept-buffer path, which needs fade 255 (globe_render.c: a fade rewrites every pixel); with fade
 * 255 the settle's frames keep the box outside the disc, and the dome appears at once with the dial (D6). A revealing
 * wake starts from the box without its globe (GLOBE_BLANK_EMPTY: the dial, the dome and a black disc). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "shell.h"
#include "globe_face.h"
#include "theme.h"
#include "omni_fonts.h"
#include "omni_faces.h"
#include "assets.h"
#include "assets_meta.h"
#include "store.h"
#include "omni/cities.h"
#include "omni/dial.h"
#include "omni/globe.h"
#include "omni/globe_motion.h"
#include "omni/power.h"
#include "omni/sun.h"
#include "omni/timefmt.h"

#define CX THEME_CX
#define CY THEME_CY
#define R 150                 /* mockup globe radius (G.R for 300 px) */
#define DATE_X (CX + 176)     /* the date window's centre */
#define DATE_Y (CY + 1)
#define DATE_BASE (DATE_Y + 4) /* textBaseline 'middle' at 16 px: the digits' baseline 4 px lower (design/shots) */
#define TAP_R 30              /* a tap this near (DATE_X, CY) flips the view */
#define MARK_SIZE 16          /* home marker object: ring r 4.5, the black ring 2.9 px wide */
#define SEC_MS 125            /* 8 beats a second */
#define SEC_LATE_MS 2         /* each beat's run this long after its boundary (LVGL's 1 ms tick, timer jitter) */
#define LON_EPS 0.05f         /* the view flip eases until this near its target */
#define DT_MAX 0.1f           /* the most one tick eases (a stalled frame must not jump the flip) */
/* the seconds hand: omni/dial.h */
#define SEC_DOT_R 5
#define SEC_CREAM 0xe7cf95
/* glance (sleepFrame) */
#define GL_DOT_R 197
#define GL_PAD 4

enum { H_HOUR, H_MIN, H__N };

/* A moving part drawn once per position into a canvas (ARGB8888, PSRAM) sized to its box there. Each globe frame
 * LVGL then only blends it, instead of rotating the hand images (or stroking the seconds hand) again under every
 * redraw of the box, which held the flip at about 10 fps. */
typedef struct {
    lv_obj_t *canvas;
    lv_draw_buf_t buf;           /* re-initialised for each box; data is mem */
    void *mem;
    uint32_t px;                 /* mem holds this many pixels (the largest box) */
} sprite_t;

/* A hand and its shadow, in one sprite. */
typedef struct {
    sprite_t sp;
    lv_draw_buf_t *img[2];       /* shadow, hand (PSRAM copies) */
    omni_dial_hand_t g;          /* size, pivot and shadow offset */
    int32_t rot;                 /* drawn at, 0.1 deg (-1: not yet) */
    bool shadow;                 /* drawn with its shadow */
} hand_t;

typedef struct {
    bool show;
    int32_t x, y;
} mark_t;

typedef struct {
    int16_t dx, dy, hour, min;   /* orbit offset, hand angles in 0.1 deg */
} glance_key_t;

typedef struct {
    lv_obj_t *awake, *dial, *date, *mark, *cap, *glance;
    sprite_t secs;
    lv_draw_buf_t *dial_buf, *cap_buf;   /* PSRAM copies */
    hand_t hand[H__N];
    globe_face_t *gf;
    bool running, sun_view, toggling;
    float settle;              /* the hands' sweep from 12 (1 outside the reveal) */
    float lon;                 /* the view's longitude (before the reveal's swing); NAN: not yet */
    mark_t pend;               /* the marker for the last submitted frame (globe_face shows it with that frame) */
    int32_t mark_x, mark_y;
    lv_timer_t *sec_timer;
    float sec_s;               /* the seconds hand's beat (0 .. 59.875) */
    float sec_settle;          /* the settle it was drawn with */
    glance_key_t gk;
    bool gk_valid;
} ana_t;

static ana_t *A;
static const char *TAG = "analog";

static void disc(lv_layer_t *layer, int32_t x, int32_t y, int32_t r, int32_t w, uint32_t rgb, lv_opa_t opa)
{
    if (!theme_on_layer(layer, x - r, y - r, x + r, y + r)) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = x;
    d.center.y = y;
    d.radius = (uint16_t)r;
    d.width = w;
    d.start_angle = 0;
    d.end_angle = 360;
    d.color = lv_color_hex(rgb);
    d.opa = opa;
    lv_draw_arc(layer, &d);
}

static void area_add(lv_area_t *a, int32_t x, int32_t y)
{
    if (x < a->x1) a->x1 = x;
    if (y < a->y1) a->y1 = y;
    if (x > a->x2) a->x2 = x;
    if (y > a->y2) a->y2 = y;
}

/* The wall-clock time of day for the hands (false and 0 when the clock is unset: the hands park at 12). */
static bool clock_now(int64_t now, struct tm *tm)
{
    memset(tm, 0, sizeof *tm);
    if (!omni_time_is_set(now)) return false;
    const time_t t = (time_t)now;
    localtime_r(&t, tm);
    return true;
}

/* ------------------------------------------------------------ hands */
static void sprite_create(sprite_t *sp, uint32_t px)
{
    sp->px = px;
    sp->mem = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, (size_t)px * 4, MALLOC_CAP_SPIRAM);
    if (!sp->mem) return;
    sp->canvas = lv_canvas_create(A->awake);
    lv_obj_remove_flag(sp->canvas, LV_OBJ_FLAG_CLICKABLE);
}

/* The sprite cleared and sized to screen box *box, and a layer on it in the box's coordinates; false if it cannot
 * be. */
static bool sprite_begin(sprite_t *sp, const lv_area_t *box, lv_layer_t *layer)
{
    const uint32_t w = (uint32_t)lv_area_get_width(box), h = (uint32_t)lv_area_get_height(box);
    if (!sp->canvas) return false;
    if (w * h > sp->px) {   /* never: mem holds the largest box */
        ESP_LOGE(TAG, "sprite box %ux%u over its %u px", (unsigned)w, (unsigned)h, (unsigned)sp->px);
        return false;
    }
    lv_obj_invalidate(sp->canvas);
    lv_draw_buf_init(&sp->buf, w, h, LV_COLOR_FORMAT_ARGB8888, w * 4, sp->mem, sp->px * 4);
    lv_draw_buf_clear(&sp->buf, NULL);
    lv_canvas_set_draw_buf(sp->canvas, &sp->buf);
    lv_canvas_init_layer(sp->canvas, layer);
    return true;
}

/* The layer drawn; the sprite shown at the box. */
static void sprite_end(sprite_t *sp, const lv_area_t *box, lv_layer_t *layer)
{
    lv_canvas_finish_layer(sp->canvas, layer);
    lv_obj_set_pos(sp->canvas, box->x1, box->y1);
    lv_obj_invalidate(sp->canvas);
}

/* Draws hand h at deg into its sprite, placed over the box the shadow and hand cover at that angle. */
static void hand_set(hand_t *h, float deg, bool shadow)
{
    const int32_t rot = lroundf(deg * 10) % 3600;
    if (rot == h->rot && shadow == h->shadow) return;
    lv_area_t box;
    lv_layer_t layer;
    int32_t b[4];
    omni_dial_hand_box(&h->g, rot, shadow, CX, CY, b);
    box = (lv_area_t){b[0], b[1], b[2], b[3]};
    if (!sprite_begin(&h->sp, &box, &layer)) return;
    for (int k = shadow ? 0 : 1; k < 2; k++) {
        if (!h->img[k]) continue;
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = h->img[k];
        d.rotation = rot;
        d.pivot.x = h->g.px;
        d.pivot.y = h->g.py;
        d.antialias = shadow;   /* in the settle's sweep (no shadow yet) nearest-pixel sampling: half the cost */
        const int32_t x = CX - h->g.px + (k ? 0 : h->g.sdx) - box.x1, y = CY - h->g.py + (k ? 0 : h->g.sdy) - box.y1;
        const lv_area_t c = {x, y, x + h->g.w - 1, y + h->g.h - 1};
        lv_draw_image(&layer, &d, &c);
    }
    sprite_end(&h->sp, &box, &layer);
    h->rot = rot;
    h->shadow = shadow;
}

/* hour hr/12 * 360 * settle and minute m/60 * 360 * settle, each with its shadow once settled (once a second, and
 * every tick while the hands settle) */
static void hands_apply(int64_t now)
{
    struct tm tm;
    clock_now(now, &tm);
    const float m = (float)tm.tm_min + (float)tm.tm_sec / 60, hr = (float)(tm.tm_hour % 12) + m / 60;
    const float hd = hr / 12 * 360 * A->settle, md = m / 60 * 360 * A->settle;
    /* spec 7.3: the shadows wait for the settle's end (it ran at 11.7 fps with them, 15 is the floor) */
    hand_set(&A->hand[H_HOUR], hd, A->settle >= 1);
    hand_set(&A->hand[H_MIN], md, A->settle >= 1);
}

/* The seconds hand's screen box at angle ang (radians). */
static void sec_box(float ang, lv_area_t *a)
{
    int32_t b[4];
    omni_dial_sec_box(ang, CX, CY, b);
    *a = (lv_area_t){b[0], b[1], b[2], b[3]};
}

/* The beat now: s = sec + floor(ms / 125) / 8 (0 with the clock unset). */
static float sec_beat(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    if (!clock_now((int64_t)tv.tv_sec, &tm)) return 0;
    return (float)tm.tm_sec + (float)(tv.tv_usec / 1000 / SEC_MS) / 8;
}

/* Draws the seconds hand at angle ang (radians) into its sprite; false if it could not. */
static bool sec_render(float ang)
{
    lv_area_t box;
    lv_layer_t layer;
    sec_box(ang, &box);
    if (!sprite_begin(&A->secs, &box, &layer)) return false;
    const int32_t ox = CX - box.x1, oy = CY - box.y1;   /* the centre in the sprite */
    int32_t x0, y0, x1, y1, t0x, t0y, t1x, t1y, dx, dy;
    omni_dial_rot(ang, 0, OMNI_DIAL_SEC_TAIL, ox, oy, &x0, &y0);
    omni_dial_rot(ang, 0, OMNI_DIAL_SEC_TIP, ox, oy, &x1, &y1);
    omni_dial_rot(ang, 0, OMNI_DIAL_SEC_TIP_FROM, ox, oy, &t0x, &t0y);
    omni_dial_rot(ang, 0, OMNI_DIAL_SEC_TIP_TO, ox, oy, &t1x, &t1y);
    omni_dial_rot(ang, 0, OMNI_DIAL_SEC_DOT, ox, oy, &dx, &dy);
    /* the shadow: 3 px black at 0.4, offset (2, 3) */
    const int32_t sx = OMNI_DIAL_SEC_SH_DX, sy = OMNI_DIAL_SEC_SH_DY;
    theme_draw_line(&layer, x0 + sx, y0 + sy, x1 + sx, y1 + sy, 3, 0x000000, THEME_OPA(0.4f), true);
    /* the 1.6 px cream stroke: LVGL lines are whole pixels, so 2 px at 0.8 (the stroke's weight) */
    theme_draw_line(&layer, x0, y0, x1, y1, 2, SEC_CREAM, THEME_OPA(0.8f), true);
    theme_draw_line(&layer, t0x, t0y, t1x, t1y, 2, THEME_RED, LV_OPA_COVER, true);
    disc(&layer, dx, dy, SEC_DOT_R, SEC_DOT_R, THEME_BRASS, LV_OPA_COVER);
    sprite_end(&A->secs, &box, &layer);
    return true;
}

/* The seconds hand at the beat now and the settle (8 times a second, and every tick while the hands settle). */
static void sec_apply(void)
{
    const float s = sec_beat();
    if (s == A->sec_s && A->settle == A->sec_settle) return;
    A->sec_s = s;
    A->sec_settle = A->settle;
    sec_render(s / 60 * 2 * (float)M_PI * A->settle);
}

/* ------------------------------------------------------------ home marker */
static void mark_apply(const mark_t *m)
{
    if (m->show) {
        A->mark_x = m->x;
        A->mark_y = m->y;
        lv_obj_set_pos(A->mark, m->x - MARK_SIZE / 2, m->y - MARK_SIZE / 2);
        lv_obj_remove_flag(A->mark, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(A->mark, LV_OBJ_FLAG_HIDDEN);
    }
}

static void mark_shown(globe_face_t *gf)
{
    const ana_t *a = globe_face_user(gf);
    mark_apply(&a->pend);
}

static void mark_hide(globe_face_t *gf)
{
    const ana_t *a = globe_face_user(gf);
    lv_obj_add_flag(a->mark, LV_OBJ_FLAG_HIDDEN);
}

static void mark_draw(lv_event_t *e)
{
    globe_face_home_ring(lv_event_get_layer(e), A->mark_x, A->mark_y, 4.5f, 1.5f, -1);
}

/* 8 beats a second, each run just after a beat boundary of the wall clock. lv_timer counts the period from the start
 * of this callback, so the boundary is taken from the wall ms read there (read after the draw, it put the next run
 * a draw's length before the boundary: a run that found the old beat, then a second one). */
static void sec_tick(lv_timer_t *t)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    const uint32_t ms = (uint32_t)(tv.tv_usec / 1000);
    lv_timer_set_period(t, SEC_MS - ms % SEC_MS + SEC_LATE_MS);
    sec_apply();
}

/* ------------------------------------------------------------ globe */
static void tick(globe_face_t *gf, const globe_tick_t *t, globe_view_frame_t *f, bool *animating)
{
    const float dt = fminf(t->dt, DT_MAX);
    const int64_t wall = (int64_t)time(NULL);
    const omni_city_t *c = globe_face_home();
    const omni_globe_cfg_t *cfg = globe_view_cfg(globe_face_view(gf));
    omni_sun_t raw;
    const bool set = omni_sun_light(wall, &raw);
    const float home_lon = c ? (float)c->lon : 0;
    const float target = A->sun_view && set ? (float)raw.subsolar_lon : home_lon;
    if (isnan(A->lon)) A->lon = target;   /* the first tick (a date tap may come before it): start at the target */
    if (A->toggling) {   /* the flip eases (mockup: 0.08 a 60 Hz frame); dt is 0 for its first tick (from rest) */
        A->lon = omni_ease_lon(A->lon, target, dt);
        if (fabsf(omni_wrap180f(target - A->lon)) < LON_EPS) A->toggling = false;
    }
    if (!A->toggling) A->lon = target;   /* at rest the view follows its target (the subsolar point moves) */
    const float lon0 = omni_wrap180f(A->lon + t->rv.lon_off_deg);
    f->lon0_deg = lon0;
    f->gain = t->rv.gain;   /* fade stays 255 (I2: the header) */
    globe_face_sun(wall, cfg->lat0_deg, lon0, &f->sun, NULL);

    mark_t m = {.show = false};
    if (c) {
        float hp[3];
        omni_globe_project(cfg, c->lat, c->lon, lon0, hp);
        m.show = hp[2] > 0 && t->rv.fade >= 1;   /* hidden while the globe brightens (fixed opacities) */
        m.x = CX + lroundf(hp[0] * R);
        m.y = CY + lroundf(hp[1] * R);
    }
    A->pend = m;

    if (t->rv.settle != A->settle) {   /* the hands sweep in from 12 with the reveal */
        A->settle = t->rv.settle;
        hands_apply(wall);
        sec_apply();
    }
    *animating = *animating || A->toggling;
}

/* The view goes just over the dial (index 0) and under the marker, date and hands; it is rebuilt there for a new
 * home city (lat0 follows). */
static const globe_face_ops_t OPS = {
    .view = "analog",
    .cad = OMNI_CAD_ANALOG,
    .z = 1,
    .reveal = omni_reveal_analog,
    .blank = GLOBE_BLANK_EMPTY,   /* the dial and the dome over a black disc: the globe brightens in (I2) */
    .tick = tick,
    .shown = mark_shown,
    .blanked = mark_hide,
};

/* ------------------------------------------------------------ glance */
/* sleepFrame's hand outline: (-0.55w, 14), (-w, -len/3), (0, -len), (w, -len/3), (0.55w, 14), rotated by a. */
static void outline_pts(float a, float len, float w, int32_t cx, int32_t cy, int32_t px[5], int32_t py[5])
{
    const float lx[5] = {-0.55f * w, -w, 0, w, 0.55f * w}, ly[5] = {14, -len * 0.33f, -len, -len * 0.33f, 14};
    for (int i = 0; i < 5; i++) omni_dial_rot(a, lx[i], ly[i], cx, cy, &px[i], &py[i]);
}

static float key_rad(int16_t deci) { return (float)deci / 10 * (float)M_PI / 180; }

static void outline_area(float a, float len, float w, int32_t cx, int32_t cy, lv_area_t *ar)
{
    int32_t px[5], py[5];
    outline_pts(a, len, w, cx, cy, px, py);
    for (int i = 0; i < 5; i++) area_add(ar, px[i], py[i]);
}

static void glance_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const int32_t cx = CX + A->gk.dx, cy = CY + A->gk.dy;
    for (int i = 0; i < 12; i++) {   /* the batons as dots, brass 0.40 (r 3.2 at 12, else 2.2) */
        const float a = (float)i / 12 * 2 * (float)M_PI;
        int32_t x, y;
        omni_dial_rot(a, 0, -GL_DOT_R, cx, cy, &x, &y);
        const int32_t r = i == 0 ? 3 : 2;
        disc(layer, x, y, r, r, THEME_BRASS, THEME_OPA(0.40f));
    }
    /* 1.5 px ink 0.38 outlines: 2 px at 0.75 of that (the stroke's weight, as live's chevrons) */
    const lv_opa_t opa = THEME_OPA(0.38f * 0.75f);
    const struct { int16_t a; float len, w; } h[2] = {{A->gk.hour, 112, 7.5f}, {A->gk.min, 192, 6}};
    for (int k = 0; k < 2; k++) {
        int32_t px[5], py[5];
        outline_pts(key_rad(h[k].a), h[k].len, h[k].w, cx, cy, px, py);
        for (int i = 0; i < 5; i++)
            theme_draw_line(layer, px[i], py[i], px[(i + 1) % 5], py[(i + 1) % 5], 2, THEME_INK, opa, true);
    }
    disc(layer, cx, cy, 5, 2, THEME_INK, opa);   /* the centre ring, r 4 (outer edge 4.75) */
}

static void glance_update(int64_t now)
{
    struct tm tm;
    clock_now(now, &tm);
    const float m = (float)tm.tm_min + (float)tm.tm_sec / 60, hr = (float)(tm.tm_hour % 12) + m / 60;
    int8_t dx, dy;
    omni_orbit_offset(now, &dx, &dy);
    const glance_key_t k = {dx, dy, (int16_t)(lroundf(hr / 12 * 3600) % 3600),
                            (int16_t)(lroundf(m / 60 * 3600) % 3600)};
    if (A->gk_valid && memcmp(&k, &A->gk, sizeof k) == 0) return;
    if (!A->gk_valid || k.dx != A->gk.dx || k.dy != A->gk.dy) {   /* everything moved */
        A->gk = k;
        A->gk_valid = true;
        lv_obj_invalidate(A->glance);
        return;
    }
    /* only the hands that turned: their old and new outlines */
    const int32_t cx = CX + k.dx, cy = CY + k.dy;
    lv_area_t ar = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
    if (k.hour != A->gk.hour) {
        outline_area(key_rad(A->gk.hour), 112, 7.5f, cx, cy, &ar);
        outline_area(key_rad(k.hour), 112, 7.5f, cx, cy, &ar);
    }
    if (k.min != A->gk.min) {
        outline_area(key_rad(A->gk.min), 192, 6, cx, cy, &ar);
        outline_area(key_rad(k.min), 192, 6, cx, cy, &ar);
    }
    A->gk = k;
    ar.x1 -= GL_PAD;
    ar.y1 -= GL_PAD;
    ar.x2 += GL_PAD;
    ar.y2 += GL_PAD;
    lv_obj_invalidate_area(A->glance, &ar);
}

/* ------------------------------------------------------------ build */
/* An image (PSRAM copy) with its pivot on the dial's centre. */
static lv_obj_t *image(lv_obj_t *parent, const char *name, lv_draw_buf_t **buf, int32_t px, int32_t py)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    *buf = assets_image_psram(name);
    if (*buf) lv_image_set_src(img, *buf);
    lv_obj_set_pos(img, CX - px, CY - py);
    return img;
}

/* most: the largest box at any whole 0.1 deg rotation (omni/dial.h, pinned by test_dial: no search at create) */
static void hand_create(hand_t *h, const char *shadow, const char *hand, omni_dial_hand_t g, uint32_t most)
{
    *h = (hand_t){.g = g, .rot = -1};
    h->img[0] = assets_image_psram(shadow);
    h->img[1] = assets_image_psram(hand);
    sprite_create(&h->sp, most);
}

static void face_create(lv_obj_t *parent)
{
    A = heap_caps_calloc(1, sizeof *A, MALLOC_CAP_SPIRAM);
    if (A == NULL) return;
    A->lon = NAN;
    A->settle = 1;
    A->sec_settle = -1;
    A->awake = theme_box(parent, 0, 0, THEME_W, THEME_W);

    A->dial = lv_image_create(A->awake);   /* index 0: the view goes just above it */
    lv_obj_remove_flag(A->dial, LV_OBJ_FLAG_CLICKABLE);
    A->dial_buf = assets_image_psram(ASSET_ANA_DIAL);
    if (A->dial_buf) lv_image_set_src(A->dial, A->dial_buf);

    A->mark = theme_box(A->awake, 0, 0, MARK_SIZE, MARK_SIZE);
    lv_obj_add_flag(A->mark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(A->mark, mark_draw, LV_EVENT_DRAW_MAIN, NULL);

    A->date = theme_label(A->awake, &font_plex_sans_600_16, THEME_BRASS_HI, LV_OPA_COVER, 0);   /* 600 16px FS */

    /* the shadow images share their hand's size and pivot (assets_meta.h) */
    hand_create(&A->hand[H_HOUR], ASSET_ANA_HOUR_SH, ASSET_ANA_HOUR,
                (omni_dial_hand_t){ASSET_ANA_HOUR_W, ASSET_ANA_HOUR_H, ASSET_ANA_HOUR_PX, ASSET_ANA_HOUR_PY,
                                   ASSET_ANA_HOUR_SH_DX, ASSET_ANA_HOUR_SH_DY},
                OMNI_DIAL_HOUR_BOX_PX);
    hand_create(&A->hand[H_MIN], ASSET_ANA_MIN_SH, ASSET_ANA_MIN,
                (omni_dial_hand_t){ASSET_ANA_MIN_W, ASSET_ANA_MIN_H, ASSET_ANA_MIN_PX, ASSET_ANA_MIN_PY,
                                   ASSET_ANA_MIN_SH_DX, ASSET_ANA_MIN_SH_DY},
                OMNI_DIAL_MIN_BOX_PX);
    sprite_create(&A->secs, OMNI_DIAL_SEC_BOX_PX);   /* the largest box at any angle (host test test_dial) */
    A->cap = image(A->awake, ASSET_ANA_CAP, &A->cap_buf, ASSET_ANA_CAP_PX, ASSET_ANA_CAP_PY);
    A->gf = globe_face_create(A->awake, &OPS, A);

    A->sec_timer = lv_timer_create(sec_tick, SEC_MS, NULL);
    if (A->sec_timer) lv_timer_pause(A->sec_timer);

    A->glance = theme_box(parent, 0, 0, THEME_W, THEME_W);
    lv_obj_add_flag(A->glance, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(A->glance, glance_draw, LV_EVENT_DRAW_MAIN, NULL);
}

static void face_destroy(void)
{
    if (A == NULL) return;
    if (A->sec_timer) lv_timer_delete(A->sec_timer);
    globe_face_destroy(A->gf);   /* stops the worker before the objects go with the parent */
    lv_obj_delete(A->awake);     /* before the PSRAM images it shows */
    theme_buf_destroy(A->dial_buf);
    for (int i = 0; i < H__N; i++) {
        heap_caps_free(A->hand[i].sp.mem);
        theme_buf_destroy(A->hand[i].img[0]);
        theme_buf_destroy(A->hand[i].img[1]);
    }
    theme_buf_destroy(A->cap_buf);
    heap_caps_free(A->secs.mem);
    heap_caps_free(A);
    A = NULL;
}

/* ------------------------------------------------------------ hooks */
static void face_wake(bool reveal)
{
    if (A == NULL) return;
    A->running = true;
    A->toggling = false;   /* a flip cut by sleep lands where it was going */
    globe_face_wake(A->gf, reveal);   /* no view (no assets / PSRAM): no reveal, the hands at once */
    A->settle = globe_face_revealing(A->gf) && !settings()->reduced_motion ? 0 : 1;   /* mockup wake: from 12 */
    hands_apply((int64_t)time(NULL));
    sec_apply();
    if (A->sec_timer) {
        lv_timer_resume(A->sec_timer);
        lv_timer_reset(A->sec_timer);
    }
}

static void face_sleep(void)
{
    if (A == NULL) return;
    A->running = false;
    globe_face_sleep(A->gf);
    if (A->sec_timer) lv_timer_pause(A->sec_timer);
}

static void face_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    (void)t_ms;
    if (A == NULL || !A->running || g->kind != G_TAP) return;
    if (globe_face_skip(A->gf) || A->settle < 1) {   /* the reveal (its end frame now) and the settle (R48/C15) */
        A->settle = 1;
        hands_apply((int64_t)time(NULL));
        sec_apply();
        return;
    }
    const float dx = (float)(g->x - DATE_X), dy = (float)(g->y - CY);
    if (A->gf && dx * dx + dy * dy < (float)(TAP_R * TAP_R)) {   /* the date window: home <-> subsolar */
        A->sun_view = !A->sun_view;
        A->toggling = true;   /* the ease starts from here: the first tick after one at rest has dt 0 */
        globe_face_kick(A->gf);
    }
}

/* ------------------------------------------------------------ update */
static void update_awake(int64_t now)
{
    struct tm tm;
    char buf[4] = "--";
    if (clock_now(now, &tm)) snprintf(buf, sizeof buf, "%d", tm.tm_mday);
    theme_set_text(A->date, buf);
    theme_place(A->date, THEME_CENTER, DATE_X, DATE_BASE);
    hands_apply(now);
}

static void face_update(int64_t now, bool glance)
{
    if (A == NULL) return;
    /* A new home city (a new view at its lat0), or a clock set or stepped (a frame now; the hands follow below and
     * on the next beat). */
    globe_face_update(A->gf, now);
    if (glance) {
        lv_obj_add_flag(A->awake, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(A->glance, LV_OBJ_FLAG_HIDDEN);
        glance_update(now);
        return;
    }
    lv_obj_add_flag(A->glance, LV_OBJ_FLAG_HIDDEN);
    A->gk_valid = false;
    lv_obj_remove_flag(A->awake, LV_OBJ_FLAG_HIDDEN);
    update_awake(now);
}

const omni_face_vt_t FACE_GLOBE_ANALOG_VT = {
    .name = "globe-analog",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
    .wake = face_wake,
    .sleep = face_sleep,
    .gesture = face_gesture,
};
