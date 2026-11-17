/* SCR_FACES: the carousel editor (spec 8.2), design/mockups/faces-setup.js `faces-edit` (frame(), pointer(), feAct()).
 *
 * The picker's grammar on thumbring.c: every face the theme has round the ring at r 182 in omni_carousel_ring's order,
 * the carousel's faces first with their order numerals, then the hidden ones after a gap of 0.75 slot taken from the
 * slot arcs, at 40 % brightness (lifted to full in the window; R207). Turning the rim brings a face under the window;
 * the centre shows its group and place, its title and the keys: Earlier and Later move it within the carousel, "In
 * swipe order" shows or hides it (the last shown face's toggle is dim), and "Restore default" acts on a 1 s hold (its
 * brass ring fills meanwhile; lifting earlier cancels). Each edit runs the pure core (omni_carousel_*), posts the whole
 * order (ui_carousel_post: the event loop validates and saves it) and re-flows the ring at once; the window stays on
 * the face. BOOT backs out (nav). Compiled -Os (R223). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "shell.h"
#include "thumbring.h"
#include "theme.h"
#include "board.h"
#include "store.h"
#include "debug_console.h"
#include "watch_theme.h"
#include "omni/hold_key.h"
#include "esp_log.h"

#define CX THEME_CX
#define CY THEME_CY
#define TAU (2 * (float)M_PI)
#define CENTRE_R 146        /* drags starting outside this turn the ring */
#define HOLD_MS 1000        /* Restore default */
#define DIM_OPA 89          /* globalAlpha .35: a key that cannot act */
#define PAINTER static __attribute__((noinline)) void

enum { K_EARLIER, K_LATER, K_SWIPE, K_RESTORE, K__N, K_NONE = -1 };
/* FED.btn, and each label's baseline (from the key's top) */
static const struct { int16_t x, y; uint8_t w, h, r, ly; } KEY[K__N] = {
    [K_EARLIER] = {161, 174, 64, 48, 14, 38}, [K_LATER] = {241, 174, 64, 48, 14, 38},
    [K_SWIPE] = {161, 232, 144, 48, 14, 29}, [K_RESTORE] = {158, 296, 150, 44, 22, 21},
};
static const char *const LABEL[K__N] = {"Earlier", "Later", "In swipe order", "Restore default"};

static struct {
    lv_obj_t *root, *draw;
    lv_timer_t *anim;
    omni_carousel_t c;                  /* the order as edited (and posted) */
    uint8_t ring[OMNI_CAROUSEL_MAX], n, shown, focus;   /* ring order; focus: the face id the window rests on */
    thumbring_turn_t m;
    bool unsent, built, centre_drag;   /* centre_drag: this drag began inside CENTRE_R (it turns nothing) */
    int8_t press, sel_shown;
    omni_hold_key_t hk;                 /* Restore default's press (omni_hold_key: pure, host-tested) */
    uint32_t sel_t, t_done;
    char txt[56];                       /* the group line: off the LVGL task's stack */
} S;

static int idx_of(uint8_t id)
{
    int i = 0;
    while (i < S.n - 1 && S.ring[i] != id) i++;
    return i;
}
static int cur(void) { return S.m.drag ? thumbring_nearest(S.m.rot) : idx_of(S.focus); }
static bool inside(int k, int16_t x, int16_t y)
{
    return x >= KEY[k].x && x <= KEY[k].x + KEY[k].w && y >= KEY[k].y && y <= KEY[k].y + KEY[k].h;
}
static void inval(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t a = {x1, y1, x2, y2};
    lv_obj_invalidate_area(S.draw, &a);
}
static void inval_centre(void) { inval(CX - 150, 108, CX + 150, 350); }

/* feCan: Earlier / Later within the carousel; the toggle unless it is the last shown face; Restore unless default */
static bool can(int k)
{
    const int i = idx_of(S.focus);
    const bool shown = i < S.shown;
    const omni_theme_limits_t *lim = &watch_theme()->limits;
    switch (k) {
    case K_EARLIER: return shown && i > 0;
    case K_LATER: return shown && i < S.shown - 1;
    case K_SWIPE: return !(shown && S.shown == 1);
    default: return S.c.len != lim->def_len || memcmp(S.c.ids, lim->def_carousel, S.c.len) != 0;
    }
}

/* The ring from the order: numerals on the shown faces, 40 % on the rest (thumbring's wash) */
static void relist(void)
{
    thumbring_item_t it[OMNI_CAROUSEL_MAX];
    S.n = (uint8_t)omni_carousel_ring(&S.c, watch_theme()->face_count, S.ring, &S.shown);
    for (int i = 0; i < S.n; i++)
        it[i] = (thumbring_item_t){.id = S.ring[i], .opa = i < S.shown ? LV_OPA_COVER : LV_OPA_40,
                                   .numeral = (int8_t)(i < S.shown ? i + 1 : -1)};
    if (!S.built) thumbring_build(it, S.n);
    thumbring_order(it, S.n, S.shown);
    S.built = true;
}

static void focus_on(uint8_t id)   /* feFocus */
{
    S.focus = id;
    S.m.target = S.m.rot + thumbring_wrapf(-thumbring_slot(idx_of(id)) - S.m.rot);
}

/* feAct: the core, the post (retried by anim_cb if the queue was full), the ring re-flowed at once (one full frame) */
static void act(int k)
{
    const omni_theme_limits_t *lim = &watch_theme()->limits;
    bool ok = true;
    if (k == K_RESTORE) omni_carousel_restore(&S.c, lim), S.t_done = lv_tick_get();
    else if (k == K_SWIPE) ok = omni_carousel_toggle(&S.c, S.focus, lim);
    else ok = omni_carousel_move(&S.c, S.focus, k == K_EARLIER ? -1 : 1);
    if (!ok) return;
    S.unsent = !ui_carousel_post(&S.c);
    relist();
    focus_on(S.focus);
    lv_obj_invalidate(S.draw);
    S.m.drawn_rot = S.m.rot;
}

/* ------------------------------------------------------------ drawing (spec 11: flat, noinline painters) */
static float gap_angle(float rot)   /* the gap's middle */
{
    return -(float)M_PI / 2 + rot + (S.shown - 1 + (1 + THUMBRING_GAP) / 2) * thumbring_step();
}

/* The engraved scale turning with the ring: a mark every quarter slot, majors at the shown faces' slots and at each
 * hidden one; the gap's engraved double divider. */
PAINTER scale(lv_layer_t *layer)
{
    thumbring_scale(layer, S.m.rot, 4, S.shown, THEME_OPA(0.85));
    for (int i = S.shown; i < S.n; i++)
        thumbring_tick(layer, -(float)M_PI / 2 + S.m.rot + thumbring_slot(i), 216, 226, 2, 0x1e1204, THEME_OPA(0.85));
    if (S.shown < S.n) {
        const float a = gap_angle(S.m.rot);
        for (int s = -1; s <= 1; s += 2) {
            thumbring_tick(layer, a + s * 0.035f, 160, 204, 1, 0x1e1204, THEME_OPA(0.8));
            thumbring_tick(layer, a + s * 0.035f + 0.005f, 160, 204, 1, 0xffe8b4, THEME_OPA(0.16));
        }
    }
}

/* key(): a dark key, its fill and hairline at opa (pressed #2a2317; on #1f1a12 with a brass .85 hairline) */
static void key(lv_layer_t *layer, int k, bool on, lv_opa_t opa)
{
    theme_draw_rect(layer, KEY[k].x, KEY[k].y, KEY[k].x + KEY[k].w - 1, KEY[k].y + KEY[k].h - 1, KEY[k].r,
                    S.press == k ? 0x2a2317 : on ? THEME_SETUP_KEY_ON : THEME_SETUP_KEY, opa, 1, THEME_BRASS,
                    (lv_opa_t)((on ? THEME_OPA(0.85) : THEME_OPA(0.38)) * opa / 255));
}

/* The hold ring round Restore (theme_draw_pill_ring): the pill 4 px out (x 154 .. 312, y 292 .. 344, r 26). */
#define RING_BOX 154, 292, 312, 344

PAINTER centre(lv_layer_t *layer, int c)
{
    const uint32_t now = lv_tick_get();
    const watch_face_info_t *f = &watch_theme()->faces[S.ring[c]];
    const lv_opa_t fade = (lv_opa_t)(255 * thumbring_smooth((float)(now - S.sel_t) / 220));
    snprintf(S.txt, sizeof S.txt, c < S.shown ? "%.20s \xc2\xb7 %d OF %d" : "%.20s \xc2\xb7 HIDDEN", f->group, c + 1, S.shown);
    for (char *p = S.txt; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    theme_draw_text(layer, S.txt, &font_plex_sans_600_10, THEME_BRASS, fade, 2, THEME_CENTER, CX, 124);
    const size_t tl = strlen(f->title);
    theme_draw_text(layer, f->title, tl > 18 ? &font_cormorant_700_20 : tl > 13 ? &font_cormorant_700_26 : &font_cormorant_700_32,
                    THEME_SETUP_HEAD, fade, 0, THEME_CENTER, CX, 160);

    const bool sw = c < S.shown;   /* In swipe order: on, with the brass switch to the right */
    for (int k = 0; k < K__N; k++) {
        const bool on = can(k) && !(S.m.drag && k < K_SWIPE);   /* Earlier and Later also wait while the ring turns */
        const lv_opa_t opa = on ? LV_OPA_COVER : DIM_OPA;
        const int32_t x = KEY[k].x + KEY[k].w / 2, y = KEY[k].y + 17;
        key(layer, k, k == K_SWIPE && sw, opa);
        theme_draw_text(layer, LABEL[k], k == K_RESTORE ? &font_plex_sans_500_14 : &font_plex_sans_400_12, THEME_INK, opa, 0,
                        k == K_SWIPE ? THEME_LEFT : THEME_CENTER, k == K_SWIPE ? KEY[k].x + 13 : x, KEY[k].y + KEY[k].ly);
        if (k < K_SWIPE) {   /* the triangle above the word (a path, not a glyph) */
            const int32_t dir = k == K_EARLIER ? -1 : 1;
            lv_draw_triangle_dsc_t t;
            lv_draw_triangle_dsc_init(&t);
            t.color = lv_color_hex(on ? THEME_SETUP_BRASS_HI : THEME_SETUP_DIM);
            t.opa = opa;
            t.p[0].x = x + dir * 5, t.p[0].y = y;
            t.p[1].x = x - dir * 4, t.p[1].y = y - 6;
            t.p[2].x = x - dir * 4, t.p[2].y = y + 6;
            if (theme_on_layer(layer, x - 5, y - 6, x + 5, y + 6)) lv_draw_triangle(layer, &t);
        } else if (k == K_SWIPE) {   /* the switch: a 28 x 14 track and its knob */
            const int32_t tx = KEY[k].x + KEY[k].w - 40, kx = sw ? tx + 21 : tx + 7;
            if (sw) theme_draw_grad(layer, tx, y, tx + 27, y + 13, 7, true, 0xe3c582, LV_OPA_COVER, 0xa07b38, LV_OPA_COVER, opa);
            else theme_draw_rect(layer, tx, y, tx + 27, y + 13, 7, 0x080706, opa, 1, THEME_BRASS, (lv_opa_t)(THEME_OPA(0.45) * opa / 255));
            theme_draw_box(layer, kx - 5, y + 2, kx + 4, y + 11, LV_RADIUS_CIRCLE, sw ? 0xfff3d6 : 0x5b5246, opa, 0, 0);
        } else {   /* HOLD 1 S, and the hold ring: a faint track; brassHi (with its glow) fills it over 1 s, then runs back */
            theme_draw_text(layer, "HOLD 1 S", &font_plex_sans_500_9, THEME_SETUP_FAINT, opa, 2, THEME_CENTER, CX, KEY[k].y + 34);
            theme_draw_pill_ring(layer, RING_BOX, 1, 2, THEME_BRASS, (lv_opa_t)(THEME_OPA(0.16) * opa / 255));
            const float hp = S.press == K_RESTORE ? omni_hold_key_fill(&S.hk, now, HOLD_MS) / 1000.0f
                             : now - S.t_done < 500 ? 1 - (float)(now - S.t_done) / 500 : 0;
            if (hp > 0) {
                theme_draw_pill_ring(layer, RING_BOX, hp, 7, THEME_SETUP_BRASS_HI, THEME_OPA(0.2));
                theme_draw_pill_ring(layer, RING_BOX, hp, 3, THEME_SETUP_BRASS_HI, LV_OPA_COVER);
            }
        }
    }
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const int c = cur();
    scale(layer);
    thumbring_draw(layer, S.m.rot, S.m.drag ? -1 : c);
    thumbring_index(layer);
    centre(layer, c);
    ui_stack_note("faces");
}

/* ------------------------------------------------------------ motion */
static void anim_cb(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t now = lv_tick_get();
    if (S.unsent) S.unsent = !ui_carousel_post(&S.c);
    thumbring_spring(&S.m, now);   /* toward the face */
    if (S.m.rot != S.m.drawn_rot && S.shown < S.n) {   /* the divider lies between two thumbnails' boxes: its own */
        for (int k = 0; k < 2; k++) {
            const float a = gap_angle(k ? S.m.rot : S.m.drawn_rot);
            const int32_t x = CX + (int32_t)lroundf(cosf(a) * 182), y = CY + (int32_t)lroundf(sinf(a) * 182);
            inval(x - 30, y - 30, x + 30, y + 30);
        }
    }
    thumbring_inval(S.draw, &S.m);
    const int c = cur();
    if (c != S.sel_shown) {
        S.sel_shown = (int8_t)c;
        S.sel_t = now;
    }
    if (now - S.sel_t < 260) inval_centre();
    if (S.press == K_RESTORE || now - S.t_done < 520) inval(146, 284, 320, 352);   /* the hold ring */
}

static void gesture(const omni_gesture_t *g, uint32_t t)
{
    if (S.root == NULL) return;
    if (g->kind == G_DRAG_START) {   /* every slide is claimed, so it cancels a press or the hold at once (Task 13 carry) */
        const int dx = g->x0 - CX, dy = g->y0 - CY;
        S.centre_drag = dx * dx + dy * dy <= CENTRE_R * CENTRE_R;
    }
    if (g->kind == G_DOWN) {
        S.centre_drag = false;
        S.press = K_NONE;
        for (int k = 0; k < K__N; k++)
            if (inside(k, g->x, g->y) && can(k)) S.press = (int8_t)k;
        if (S.press == K_RESTORE) omni_hold_key_down(&S.hk, lv_tick_get());
        inval_centre();
        return;
    }
    if (S.press == K_RESTORE) {   /* acts at the first G_HOLD at or past 1 s; the lift, a drag or any other gesture cancels */
        const omni_hold_key_rc_t rc = omni_hold_key_gesture(&S.hk, g, HOLD_MS);
        if (rc == OMNI_HOLD_KEY_NONE) return;
        S.press = K_NONE;
        inval_centre();
        if (rc == OMNI_HOLD_KEY_FIRE) act(K_RESTORE);
        return;
    }
    if (g->kind == G_HOLD) return;
    const bool tap = g->kind == G_TAP || g->kind == G_LIFT_STILL;
    if (S.press != K_NONE) {   /* the release ends a press */
        const int k = S.press;
        S.press = K_NONE;
        inval_centre();
        if (tap && inside(k, g->x, g->y)) act(k);
        return;
    }
    if (S.centre_drag && (g->kind == G_DRAG_START || g->kind == G_DRAG_MOVE || g->kind == G_DRAG_END)) {
        S.centre_drag = g->kind != G_DRAG_END;   /* a slide that began in the centre turns nothing */
        return;
    }
    if (g->kind == G_DRAG_START) inval_centre();   /* Earlier and Later wait */
    if (thumbring_drag(&S.m, g, t)) {   /* released: coast, then settle on the nearest face */
        focus_on(S.ring[thumbring_nearest(thumbring_coast(&S.m))]);
        S.m.vel = 0;
        inval_centre();
    }
    const int hit = tap ? thumbring_hit(S.m.rot, g->x, g->y) : -1;   /* a tap on a thumbnail brings it round */
    if (hit >= 0) focus_on(S.ring[hit]);
}

/* Input task, no LVGL lock. Every slide is the screen's (G_DRAG_*), so one that starts on a key cancels its press, and
 * Restore's hold, at once (omni_hold_key); gesture() turns the ring only for a slide that starts outside CENTRE_R. */
static bool claim(int16_t x0, int16_t y0, void *ctx)
{
    (void)x0, (void)y0, (void)ctx;
    return true;
}

/* ------------------------------------------------------------ screen interface */
static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    const omni_settings_t *st = settings();
    const omni_nav_t *nav = shell_nav();
    S.c.len = st->carousel_len;
    memcpy(S.c.ids, st->carousel, sizeof S.c.ids);
    S.c.face = st->carousel[nav && nav->face_idx < st->carousel_len ? nav->face_idx : 0];   /* the saved face */
    S.press = K_NONE;
    thumbring_screen(parent, draw_cb, &S.root, &S.draw);
    relist();
    focus_on(nav ? omni_nav_current_face(nav, st) : S.c.face);   /* the face the wearer sees (a transient one too) */
    S.m.rot = S.m.drawn_rot = S.m.target;
    S.sel_shown = (int8_t)cur();
    S.sel_t = S.t_done = lv_tick_get() - 1000;   /* no fade on opening */
    S.m.t_prev = lv_tick_get();
    S.anim = lv_timer_create(anim_cb, 16, NULL);
    board_set_touch_claim_w(claim, NULL, OMNI_GW_DOWN | OMNI_GW_HOLD | OMNI_GW_LIFT_STILL);
}

static void destroy(void)
{
    board_set_touch_claim(NULL, NULL);
    if (S.anim) lv_timer_delete(S.anim);
    if (S.root) lv_obj_delete(S.root);   /* the images before their buffers */
    if (S.unsent && !ui_carousel_post(&S.c) && !ui_carousel_post(&S.c))   /* a last try, then one more */
        ESP_LOGW("faces", "the last order was not posted (queue full): the edit is lost");
    thumbring_free();
    memset(&S, 0, sizeof S);
}

static void dim(bool on)
{
    if (S.root) lv_obj_set_style_opa(S.root, on ? LV_OPA_40 : LV_OPA_COVER, 0);
}

const omni_screen_vt_t SCR_FACES_VT = {
    .id = SCR_FACES, .create = create, .destroy = destroy, .gesture = gesture, .dim = dim,
};
