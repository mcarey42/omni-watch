/* World time: design/mockups/faces-practical.js, `id: 'world'` (frame, pointer and sleepFrame, lines 886-975). The
 * maths and texts are omni_core's (omni/worldtime.h).
 *
 * Awake: the polar day/night map (globe_face / globe_view "world": a fixed view, polar LUT, planar path; wt_frame is
 * the screen's background and the box's under_name, wt_over its graticule) turns under the fixed 24-hour ring with the
 * real subsolar meridian at the top (noon under the index), north up, hours running anticlockwise. Over it: a dot at
 * each city's true position (13: the mockup's twelve plus home, omni_wt_list), a leader tick and a code on the band for
 * each (drawn glyph by glyph on its arc, omni_wt_glyphs: R141), and the centre cartouche (home time, or a tapped city's
 * for 6 s). Clock unset (B17, D13): the view-centre sun at latitude 90 lights the whole map, the cartouche reads
 * "--:--".
 *
 * The dots follow every frame (shown, with the frame's own lon0, so they never lead or lag the map). The ticks and
 * labels are hidden during the reveal and laid again for the frame on screen once a minute after it (the map turns 0.25
 * deg a minute). Wake (R138, fade-only): the map brightens in place over 300 ms (omni_wt_reveal) under a black disc
 * over the map whose opacity falls to 0, drawn first in the marks object: no globe frame per step, so a reveal frame is
 * one LVGL redraw of the box (D7's 10.6 fps came from re-rendering the planar map every frame of a spin). The fade
 * starts at the first frame shown after the wake (the fresh map), or after FADE_WAIT_MS without one, and the frame
 * outside the disc never darkens (the reason analog's I2 went through gain). A tap during it ends it. Tap: a city on
 * the band selects it for 6 s (omni_wt_hit: r >= 120, within 0.25 rad), r < 60 clears; the last selection is the
 * glance's pinned city (RAM only, kept across the face's destroy; default: the first in the list). A home city change
 * refreshes the list, band and cartouche and never rebuilds the view (A12). Glance: four dim brass squares at r 213 and
 * the shared glance with the pinned city's time as its sub-line, all offset by omni_orbit_offset; the map is stopped.
 *
 * Timing (Task 9), at INFO beside globe_view's own render windows: "world: band layout calls <us> us" (the LVGL
 * property calls of a band layout only; the redraw they cause lands in the next LVGL frame, not in this number), and
 * for each reveal that runs to its end "world: reveal <n> frames in <ms> ms = <fps> fps" (the LVGL refreshes finished
 * from its first step to its end: frames displayed). A reveal ended by a tap logs "world: reveal skipped after <n>
 * frames" (no fps: not an N1 sample); one cut by sleep logs nothing. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
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
#include "disp_stats.h"
#include "omni/cities.h"
#include "omni/globe.h"
#include "omni/globe_motion.h"
#include "omni/power.h"
#include "omni/sun.h"
#include "omni/timefmt.h"
#include "omni/worldtime.h"

#define CX THEME_CX
#define CY THEME_CY
#define LABEL_R 182                /* arcText r; 186 and reversed in the bottom half (omni_wt_flip) */
#define LABEL_R_FLIP 186
#define LABEL_BOX 44               /* each label's object: its glyphs lie within 16 px of the label's centre */
#define LABEL_LS 1                 /* 600 10.5px FS, letter spacing 1 */
#define LABEL_FONT (&font_plex_sans_600_10)
#define TICK_R0 168
#define TICK_R1 172
#define MARKS_XY (CX - TICK_R1 - 2)   /* the dots and ticks object: the ticks reach r 172 */
#define MARKS_WH (2 * (TICK_R1 + 2))
#define CART_R 38
#define GL_R 213
#define GL_SQ 3
#define SEL_MS 6000
#define WHITE 0xffffff
#define FADE_TICK_MS 33            /* the reveal's steps (R138): each one LVGL redraw of the box */
#define FADE_WAIT_MS 1000          /* no frame after a revealing wake for this long: fade over what the box has */
#define MAP_R 166                  /* the map disc (the world desc's diam 332) the reveal's black disc covers */

typedef struct {               /* what the dots show for one frame */
    int n;
    int16_t x[OMNI_WT_MAX], y[OMNI_WT_MAX];
    bool vis[OMNI_WT_MAX];
    float lon_c;               /* the frame's lon0: the meridian at the top */
    float fade;                /* the reveal's brightening (1 outside one) */
    int64_t min;               /* the frame's wall minute */
} marks_t;

typedef struct { lv_point_t p; int32_t rot; uint32_t cp; } glyph_t;   /* lv_draw_letter's point, 0.1 deg, letter */

typedef struct {
    lv_obj_t *awake, *frame, *marks, *label[OMNI_WT_MAX], *cart, *disc, *big, *top, *sub;
    glyph_t glyph[OMNI_WT_MAX][OMNI_WT_GLYPHS];
    uint8_t nglyph[OMNI_WT_MAX];
    uint32_t lab_rgb[OMNI_WT_MAX];
    lv_opa_t lab_opa[OMNI_WT_MAX];
    lv_obj_t *gl, *gl_sq[4];
    glance_t *glance;
    globe_face_t *gf;
    lv_image_dsc_t frame_dsc;
    lv_timer_t *sel_timer;
    omni_wt_city_t list[OMNI_WT_MAX];
    int n, sel;                /* sel: the list index shown in the cartouche, -1: home */
    uint16_t home;             /* the city the list was made for */
    int32_t bx, by;            /* the globe box origin */
    marks_t pend, cur;         /* the last submitted frame's and the shown frame's */
    bool cur_ok;               /* a frame has been shown */
    lv_opa_t cart_opa;
    bool running;
    /* the band */
    bool band_on;
    float band_lon;
    int64_t band_min;
    float tick_a[OMNI_WT_MAX];
    /* the reveal (R138): the fade over the map on screen, its own timer */
    lv_timer_t *fade_timer;
    bool rv_pending, rv_running;   /* armed by a revealing wake; running from the first frame shown after it */
    float fade;                    /* 0 .. 1: the map's brightness, the dots' and the cartouche's opacity */
    int64_t rv_t0;                 /* esp_timer us: the wake (pending) or the first step (running) */
    uint32_t rv_refr0;             /* disp_stats_refr_done at the first step */
    /* the sun, once per wall second */
    int64_t sun_t;
    bool sun_set;
    omni_sun_t sun;
    int64_t gl_min;
    int8_t gl_dx, gl_dy;
    bool gl_ok;
} world_t;

static world_t *W;
static uint16_t s_pinned;          /* the glance's pinned city: the last one tapped (RAM only, outlives the face) */
static bool s_pinned_ok;
static const char *TAG = "world";

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static const omni_wt_city_t *home_entry(void)
{
    for (int i = 0; i < W->n; i++)
        if (W->list[i].home) return &W->list[i];
    return NULL;
}

static const omni_wt_city_t *pinned_entry(void)
{
    if (s_pinned_ok)
        for (int i = 0; i < W->n; i++)
            if (W->list[i].city == s_pinned) return &W->list[i];
    return W->n > 0 ? &W->list[0] : NULL;
}

/* ------------------------------------------------------------ the band (ticks and labels) */
static void label_style(int i)
{
    const bool home = W->list[i].home, sel = i == W->sel;
    const uint32_t rgb = home ? THEME_BRASS_HI : sel ? WHITE : THEME_INK;
    const lv_opa_t opa = home || sel ? LV_OPA_COVER : THEME_OPA(0.72);
    if (rgb == W->lab_rgb[i] && opa == W->lab_opa[i]) return;
    W->lab_rgb[i] = rgb;
    W->lab_opa[i] = opa;
    lv_obj_invalidate(W->label[i]);
}

/* The code of list entry i on its arc at angle a: each glyph's lv_draw_letter point and turn (omni_wt_glyphs, R141).
 * The point is the glyph's baseline centre: LVGL 9.5 moves the glyph by its own pivot (adv / 2, ascent) and turns it
 * about that point, so the face passes the centre as it is (review I1). */
static void label_glyphs(int i, float a, bool flip, int32_t r)
{
    const lv_font_t *f = LABEL_FONT;
    const char *code = W->list[i].code;
    float adv[OMNI_WT_GLYPHS];
    int n = 0;
    for (; n < OMNI_WT_GLYPHS && code[n]; n++)
        adv[n] = (float)lv_font_get_glyph_width(f, (uint8_t)code[n], (uint8_t)code[n + 1]);
    omni_wt_glyph_t g[OMNI_WT_GLYPHS];
    const float mid = (float)f->line_height / 2.0f - (float)f->base_line;   /* canvas 'middle' to the baseline */
    n = omni_wt_glyphs(adv, n, LABEL_LS, (float)r, mid, a, flip, CX, CY, g);
    for (int k = 0; k < n; k++)
        W->glyph[i][k] = (glyph_t){.p = {g[k].px, g[k].py}, .rot = (int32_t)lroundf(g[k].rot_deg * 10.0f),
                                   .cp = (uint8_t)code[k]};
    W->nglyph[i] = (uint8_t)n;
}

static void label_draw(lv_event_t *e)
{
    const int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (W == NULL || i >= W->n) return;
    for (int k = 0; k < W->nglyph[i]; k++) {
        lv_draw_letter_dsc_t d;
        lv_draw_letter_dsc_init(&d);
        d.font = LABEL_FONT;
        d.color = lv_color_hex(W->lab_rgb[i]);
        d.opa = W->lab_opa[i];
        d.rotation = W->glyph[i][k].rot;
        d.unicode = W->glyph[i][k].cp;
        lv_draw_letter(layer, &d, &W->glyph[i][k].p);
    }
}

static void band_hide(void)
{
    if (!W->band_on) return;
    W->band_on = false;
    for (int i = 0; i < OMNI_WT_MAX; i++) show(W->label[i], false);
    lv_obj_invalidate(W->marks);   /* the ticks */
}

/* The ticks and labels for the meridian lon_c at the top (the frame on screen). */
static void band_lay(float lon_c, int64_t min)
{
    const int64_t t0 = esp_timer_get_time();
    W->band_on = true;
    W->band_lon = lon_c;
    W->band_min = min;
    for (int i = 0; i < OMNI_WT_MAX; i++) {
        const omni_city_t *c = i < W->n ? omni_city_at(W->list[i].city) : NULL;
        show(W->label[i], c != NULL);
        if (c == NULL) continue;
        const float a = omni_wt_angle(c->lon, lon_c);
        W->tick_a[i] = a;
        const bool flip = omni_wt_flip(a);
        const int32_t r = flip ? LABEL_R_FLIP : LABEL_R;
        const int32_t px = CX + (int32_t)lroundf(cosf(a) * (float)r), py = CY + (int32_t)lroundf(sinf(a) * (float)r);
        lv_obj_t *l = W->label[i];
        lv_obj_invalidate(l);   /* where it was */
        lv_obj_set_pos(l, px - LABEL_BOX / 2, py - LABEL_BOX / 2);
        label_glyphs(i, a, flip, r);
        label_style(i);
        lv_obj_invalidate(l);
    }
    lv_obj_invalidate(W->marks);   /* the ticks */
    ESP_LOGI(TAG, "band layout calls %lld us", (long long)(esp_timer_get_time() - t0));
}

/* ------------------------------------------------------------ the dots and ticks */
static void disc(lv_layer_t *layer, float x, float y, int32_t d, uint32_t rgb, lv_opa_t opa)
{
    const int32_t x1 = (int32_t)lroundf(x - (float)d / 2), y1 = (int32_t)lroundf(y - (float)d / 2);
    theme_draw_box(layer, x1, y1, x1 + d - 1, y1 + d - 1, LV_RADIUS_CIRCLE, rgb, opa, 0, 0);
}

static void marks_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    if (W->fade < 1.0f)   /* the reveal (R138): the map brightens in place under a fading black disc */
        theme_draw_box(layer, CX - MAP_R, CY - MAP_R, CX + MAP_R - 1, CY + MAP_R - 1, LV_RADIUS_CIRCLE, 0x000000,
                       (lv_opa_t)lroundf((1.0f - W->fade) * 255.0f), 0, 0);
    if (W->band_on)
        for (int i = 0; i < W->n; i++) {   /* leader ticks at the map's edge, 1 px: home brass, the others ink .35 */
            const float a = W->tick_a[i], c = cosf(a), s = sinf(a);
            const bool home = W->list[i].home;
            theme_draw_line(layer, CX + (int32_t)lroundf(c * TICK_R0), CY + (int32_t)lroundf(s * TICK_R0),
                            CX + (int32_t)lroundf(c * TICK_R1), CY + (int32_t)lroundf(s * TICK_R1), 1,
                            home ? THEME_BRASS : THEME_INK, home ? LV_OPA_COVER : THEME_OPA(0.35), false);
        }
    const marks_t *m = &W->cur;
    const float f = W->fade;
    for (int i = 0; i < m->n && i < W->n; i++) {
        if (!m->vis[i]) continue;
        const bool home = W->list[i].home, sel = i == W->sel;
        /* black .7 under: r 4 (home) or 3; then brassHi r 2.6 (home), white (selected) or white .85, r 1.8 */
        disc(layer, m->x[i], m->y[i], home ? 8 : 6, 0x000000, THEME_OPA(0.7 * f));
        disc(layer, m->x[i], m->y[i], home ? 5 : 4, home ? THEME_BRASS_HI : WHITE,
             home || sel ? THEME_OPA(f) : THEME_OPA(0.85 * f));
    }
}

/* The dots move within the globe box, which the frame's swap redraws anyway: this only covers a change between
 * frames (a selection, a list). */
static void marks_dirty(void)
{
    const lv_area_t a = {W->bx, W->by, W->bx + 337, W->by + 337};
    lv_obj_invalidate_area(W->marks, &a);
}

/* ------------------------------------------------------------ the cartouche */
static void cart_update(int64_t now)
{
    char big[8], top[4], sub[24];
    const omni_wt_city_t *h = home_entry(), *s = W->sel >= 0 && W->sel < W->n ? &W->list[W->sel] : NULL;
    omni_wt_cartouche(s, h, now, settings()->h24, big, top, sub);
    /* R141: in the cartouche box's own coordinates (its centre is (CART_R, CART_R)): theme_place sets the position
     * within the parent, and placed at screen coordinates the texts sat ~195 px outside the 76 px box, clipped away:
     * the empty cartouche Task 9 saw */
    theme_set_text(W->big, big);
    theme_place(W->big, THEME_CENTER, CART_R, CART_R + 7);
    theme_set_text(W->top, top);
    theme_place(W->top, THEME_CENTER, CART_R, CART_R - 15);
    theme_set_text(W->sub, sub);
    theme_place(W->sub, THEME_CENTER, CART_R, CART_R + 21);
}

/* ------------------------------------------------------------ the list and the selection */
static void select_city(int i)
{
    W->sel = i;
    if (i >= 0) {
        s_pinned = W->list[i].city;
        s_pinned_ok = true;
        if (W->sel_timer) {
            lv_timer_reset(W->sel_timer);
            lv_timer_resume(W->sel_timer);
        }
    } else if (W->sel_timer) {
        lv_timer_pause(W->sel_timer);
    }
    if (W->band_on)
        for (int k = 0; k < W->n; k++) label_style(k);
    marks_dirty();
    cart_update((int64_t)time(NULL));
}

static void sel_expire(lv_timer_t *t)
{
    (void)t;
    if (W != NULL) select_city(-1);
}

static void list_update(void)
{
    const uint16_t home = settings()->city;
    if (W->n > 0 && home == W->home) return;
    W->home = home;
    W->n = omni_wt_list(home, W->list);
    W->sel = -1;
    if (W->sel_timer) lv_timer_pause(W->sel_timer);
    W->pend.n = W->cur.n = 0;   /* the old list's dots: gone until the next frame (kicked below) */
    marks_dirty();
    if (W->band_on) band_lay(W->band_lon, W->band_min);
    globe_face_kick(W->gf);
}

/* ------------------------------------------------------------ globe */
static void tick(globe_face_t *gf, const globe_tick_t *t, globe_view_frame_t *f, bool *animating)
{
    (void)animating;   /* the reveal (preset) is the world map's only motion */
    world_t *w = globe_face_user(gf);
    const int64_t now = (int64_t)time(NULL);
    if (now != w->sun_t) {   /* the subsolar meridian moves 0.004 deg a second */
        w->sun_t = now;
        w->sun_set = omni_sun_light(now, &w->sun);
    }
    const double lon0 = omni_wrap180(w->sun_set ? w->sun.subsolar_lon : 0);
    f->lon0_deg = lon0;
    f->gain = GLOBE_WORLD_GAIN;   /* the reveal is the face's own fade over the box (R138): never a frame's */
    /* latitude 90: with the clock unset the view-centre sun sits over the pole and lights the whole map (B17) */
    globe_face_sun(now, 90, lon0, &f->sun, NULL);

    const omni_globe_cfg_t *cfg = globe_view_cfg(globe_face_view(gf));
    (void)t;
    marks_t m = {.n = w->n, .lon_c = (float)lon0, .min = now / 60};
    const float half = (float)cfg->size / 2, rad = (float)cfg->diam / 2;
    for (int i = 0; i < w->n; i++) {
        const omni_city_t *c = omni_city_at(w->list[i].city);
        float p[3] = {0, 0, -1};
        if (c) omni_globe_project(cfg, c->lat, c->lon, lon0, p);
        m.vis[i] = p[2] > 0;
        m.x[i] = (int16_t)lroundf((float)w->bx + half + p[0] * rad);
        m.y[i] = (int16_t)lroundf((float)w->by + half + p[1] * rad);
    }
    w->pend = m;
}

/* The reveal's step (R138): the fade at its time, applied to the black disc, the dots and the cartouche. */
static void fade_apply(float fade)
{
    if (fade == W->fade) return;
    W->fade = fade;
    marks_dirty();   /* the disc and the dots: inside the box */
    face_fade_children(W->cart, &W->cart_opa, (lv_opa_t)lroundf(fade * 255.0f));   /* R28 */
}

static void reveal_end(bool skipped)
{
    if (W->rv_running) {
        const int64_t ms = (esp_timer_get_time() - W->rv_t0) / 1000;
        const uint32_t frames = disp_stats_refr_done() - W->rv_refr0;
        if (skipped)
            ESP_LOGI(TAG, "reveal skipped after %u frames", (unsigned)frames);
        else
            ESP_LOGI(TAG, "reveal %u frames in %lld ms = %.1f fps", (unsigned)frames, (long long)ms,
                     ms > 0 ? (double)frames * 1000.0 / (double)ms : 0.0);
    }
    W->rv_pending = W->rv_running = false;
    if (W->fade_timer) lv_timer_pause(W->fade_timer);
    fade_apply(1.0f);
    if (W->cur_ok) band_lay(W->cur.lon_c, W->cur.min);   /* for the frame on screen */
}

static void reveal_run(void)
{
    W->rv_pending = false;
    W->rv_running = true;
    W->rv_t0 = esp_timer_get_time();
    W->rv_refr0 = disp_stats_refr_done();
}

static void fade_tick(lv_timer_t *t)
{
    (void)t;
    if (W == NULL || !W->running) return;
    if (W->rv_pending) {   /* waiting for the fresh map; a view that shows nothing is not waited on for long */
        if (esp_timer_get_time() - W->rv_t0 < (int64_t)FADE_WAIT_MS * 1000) return;
        reveal_run();
    }
    if (!W->rv_running) return;
    omni_reveal_t rv;
    omni_wt_reveal((uint32_t)((esp_timer_get_time() - W->rv_t0) / 1000), settings()->reduced_motion, &rv);
    fade_apply(rv.fade);
    if (rv.done) reveal_end(false);
}

static void shown(globe_face_t *gf)
{
    world_t *w = globe_face_user(gf);
    w->cur = w->pend;
    w->cur_ok = true;
    marks_dirty();
    if (w->rv_pending) reveal_run();   /* the fresh map is on screen: it brightens from here */
    if (w->rv_pending || w->rv_running) {
        band_hide();
        return;
    }
    if (!w->band_on || w->cur.min != w->band_min) band_lay(w->cur.lon_c, w->cur.min);
}

/* The view sits over the frame (index 0) and under the dots, labels and cartouche. */
static const globe_face_ops_t OPS = {
    .view = "world",
    .cad = OMNI_CAD_LIVE,
    .z = 1,
    .reveal = NULL,               /* R138: the face fades the map on screen itself (fade_tick): no frame per step */
    .tick = tick,
    .shown = shown,
    .fixed_view = true,           /* A12/B16: a city change refreshes the band and cartouche, never the view */
};

/* ------------------------------------------------------------ build */
static lv_obj_t *label_create(lv_obj_t *parent, int i)
{
    lv_obj_t *l = theme_box(parent, 0, 0, LABEL_BOX, LABEL_BOX);
    lv_obj_add_event_cb(l, label_draw, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)i);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

static void face_create(lv_obj_t *parent)
{
    W = heap_caps_calloc(1, sizeof *W, MALLOC_CAP_SPIRAM);
    if (W == NULL) return;
    W->sel = -1;
    W->cart_opa = LV_OPA_COVER;
    W->sun_t = -1;
    W->fade = 1.0f;
    W->awake = theme_box(parent, 0, 0, THEME_W, THEME_W);

    W->frame = lv_image_create(W->awake);   /* index 0: the view goes just above it */
    lv_obj_remove_flag(W->frame, LV_OBJ_FLAG_CLICKABLE);
    if (assets_image(ASSET_WT_FRAME, &W->frame_dsc)) lv_image_set_src(W->frame, &W->frame_dsc);
    W->gf = globe_face_create(W->awake, &OPS, W);
    W->bx = W->by = ASSET_WT_OVER_X;
    if (W->gf) globe_face_box(W->gf, &W->bx, &W->by);

    W->marks = theme_box(W->awake, MARKS_XY, MARKS_XY, MARKS_WH, MARKS_WH);
    lv_obj_add_event_cb(W->marks, marks_draw, LV_EVENT_DRAW_MAIN, NULL);
    for (int i = 0; i < OMNI_WT_MAX; i++) W->label[i] = label_create(W->awake, i);

    /* the cartouche: r 38, (4,4,6) at .88, 1 px brass .65 (1.2 in the mockup) */
    W->cart = theme_box(W->awake, CX - CART_R, CY - CART_R, 2 * CART_R, 2 * CART_R);
    W->disc = theme_box(W->cart, 0, 0, 2 * CART_R, 2 * CART_R);
    lv_obj_set_style_radius(W->disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(W->disc, lv_color_make(4, 4, 6), 0);
    lv_obj_set_style_bg_opa(W->disc, THEME_OPA(0.88), 0);
    lv_obj_set_style_border_width(W->disc, 1, 0);
    lv_obj_set_style_border_color(W->disc, lv_color_hex(THEME_BRASS), 0);
    lv_obj_set_style_border_opa(W->disc, THEME_OPA(0.65), 0);
    /* the texts are placed in the cartouche box's own coordinates (cart_update: theme_place is parent-relative) */
    W->big = theme_label(W->cart, &font_plex_sans_500_21, THEME_INK, LV_OPA_COVER, 0);       /* 500 21px FS */
    W->top = theme_label(W->cart, &font_plex_sans_600_10, THEME_BRASS, LV_OPA_COVER, 2);     /* 700 10px FS, 1.5 */
    W->sub = theme_label(W->cart, &font_plex_sans_500_9, THEME_INK_DIM, LV_OPA_COVER, 1);    /* 500 8.5px FS, 0.8 */

    W->gl = theme_box(parent, 0, 0, THEME_W, THEME_W);
    lv_obj_add_flag(W->gl, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 4; i++) {   /* rgba(201,163,91,.35), 3 x 3 */
        W->gl_sq[i] = theme_box(W->gl, 0, 0, GL_SQ, GL_SQ);
        lv_obj_set_style_bg_color(W->gl_sq[i], lv_color_hex(THEME_BRASS), 0);
        lv_obj_set_style_bg_opa(W->gl_sq[i], THEME_OPA(0.35), 0);
    }
    W->glance = glance_create(parent);

    W->sel_timer = lv_timer_create(sel_expire, SEL_MS, NULL);
    if (W->sel_timer) lv_timer_pause(W->sel_timer);
    W->fade_timer = lv_timer_create(fade_tick, FADE_TICK_MS, NULL);
    if (W->fade_timer) lv_timer_pause(W->fade_timer);
    list_update();
    cart_update((int64_t)time(NULL));
}

static void face_destroy(void)
{
    if (W == NULL) return;
    if (W->sel_timer) lv_timer_delete(W->sel_timer);
    if (W->fade_timer) lv_timer_delete(W->fade_timer);
    globe_face_destroy(W->gf);   /* stops the worker before the objects go */
    lv_obj_delete(W->awake);     /* before the image descriptor it shows (in W) */
    glance_free(W->glance);
    heap_caps_free(W);
    W = NULL;
}

/* ------------------------------------------------------------ hooks */
static void face_wake(bool reveal)
{
    if (W == NULL) return;
    W->running = true;
    /* R138: a revealing wake covers the map (from before the sleep) with the black disc at once, and fades it from the
     * first fresh frame shown; the globe itself only gets its usual wake frame */
    if (reveal && W->fade_timer && W->gf && globe_face_view(W->gf) && !globe_view_failed(globe_face_view(W->gf))) {
        W->rv_pending = true;
        W->rv_running = false;
        W->rv_t0 = esp_timer_get_time();
        fade_apply(0.0f);
        band_hide();
        lv_timer_resume(W->fade_timer);
    }
    globe_face_wake(W->gf, false);
}

static void face_sleep(void)
{
    if (W == NULL) return;
    W->running = false;
    W->rv_pending = W->rv_running = false;   /* a reveal cut by sleep is no sample and logs nothing */
    if (W->fade_timer) lv_timer_pause(W->fade_timer);
    fade_apply(1.0f);
    globe_face_sleep(W->gf);
}

static void face_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    (void)t_ms;
    if (W == NULL || !W->running || g->kind != G_TAP) return;
    if (W->rv_pending || W->rv_running) {   /* a tap during the reveal only ends it (spec §8) */
        reveal_end(true);
        return;
    }
    if (!W->band_on) return;              /* nothing laid to hit yet */
    const int i = omni_wt_hit(W->list, W->n, g->x0, g->y0, W->band_lon);
    if (i == -2) select_city(-1);
    else if (i >= 0) select_city(i);
}

/* ------------------------------------------------------------ glance */
static void glance_marks(int64_t now)
{
    int8_t dx, dy;
    omni_orbit_offset(now, &dx, &dy);
    if (W->gl_ok && dx == W->gl_dx && dy == W->gl_dy) return;
    W->gl_ok = true;
    W->gl_dx = dx;
    W->gl_dy = dy;
    for (int i = 0; i < 4; i++) {   /* 0, 90, 180, 270 deg */
        const int32_t ux = i == 0 ? 1 : i == 2 ? -1 : 0, uy = i == 1 ? 1 : i == 3 ? -1 : 0;
        lv_obj_set_pos(W->gl_sq[i], CX + dx + ux * GL_R - 1, CY + dy + uy * GL_R - 1);   /* fillRect(-1.5, 3) */
    }
}

/* ------------------------------------------------------------ update */
static void face_update(int64_t now, bool glance)
{
    if (W == NULL) return;
    globe_face_update(W->gf, now);   /* fixed view: a clock set or step gets a frame now; a city never rebuilds */
    list_update();                   /* a new home city: the list, band and dots (kicked) */
    if (glance) {
        lv_obj_add_flag(W->awake, LV_OBJ_FLAG_HIDDEN);
        glance_marks(now);
        lv_obj_remove_flag(W->gl, LV_OBJ_FLAG_HIDDEN);
        char sub[24];
        omni_wt_glance_text(pinned_entry(), now, settings()->h24, sub);
        glance_show(W->glance, true);
        glance_update(W->glance, now, settings()->h24, sub);
        return;
    }
    glance_show(W->glance, false);
    lv_obj_add_flag(W->gl, LV_OBJ_FLAG_HIDDEN);
    W->gl_ok = false;
    lv_obj_remove_flag(W->awake, LV_OBJ_FLAG_HIDDEN);
    cart_update(now);
    if (W->gf == NULL && !W->band_on) {   /* no view (no PSRAM): the band for the real sun, once */
        omni_sun_t sun;
        band_lay(omni_sun_light(now, &sun) ? (float)sun.subsolar_lon : 0, now / 60);
    }
}

const omni_face_vt_t FACE_WORLD_VT = {
    .name = "world",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
    .wake = face_wake,
    .sleep = face_sleep,
    .gesture = face_gesture,
};
