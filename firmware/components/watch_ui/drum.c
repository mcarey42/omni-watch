/* The drum (drum.h): moved from scr_settings.c, unchanged in its motion and drawing. */
#include <math.h>
#include <string.h>
#include "drum.h"
#include "theme.h"
#include "board.h"

#define CX THEME_CX
#define CY THEME_CY
#define PIPS_Y 384

const theme_window_t DRUM_WINDOW = {
    .x = DRUM_WIN_X, .y = DRUM_WIN_Y, .w = DRUM_WIN_W, .h = DRUM_WIN_H, .border = 4, .r_out = 15, .r_in = 12,
    .gx0 = DRUM_WIN_X, .gy0 = DRUM_WIN_Y, .gx1 = DRUM_WIN_X + DRUM_WIN_W * 0.3f, .gy1 = DRUM_WIN_Y + DRUM_WIN_H * 1.6f,
    .edge_opa = 0, .glass_opa = .08f, .glass_stop = .45f,
};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

void drum_init(drum_t *dr, int n, int start)
{
    memset(dr, 0, sizeof *dr);
    dr->n = n;
    dr->s = dr->target = (float)(start < 0 ? 0 : start >= n ? n - 1 : start);
}

int drum_cur(const drum_t *dr) { return (int)clampf(roundf(dr->s), 0, dr->n - 1); }

float drum_scale(const drum_t *dr, int i)
{
    float d = i - dr->s;
    return 1 - fminf(1, fabsf(d)) * 0.22f - fmaxf(0, fabsf(d) - 1) * 0.1f;
}

void drum_draw(const drum_t *dr, lv_layer_t *layer, drum_row_fn row, void *ctx)
{
    for (int i = 0; row != NULL && i < dr->n; i++) { /* the drum, by position */
        float d = i - dr->s;
        if (fabsf(d) > 2.6f) continue;
        int32_t y = CY + (int32_t)lroundf(sinf(clampf(d / 3.2f, -1, 1) * (float)M_PI / 2) * DRUM_ROW_H * 2.6f);
        float in_win = fmaxf(0, 1 - fabsf(d));
        if (in_win > 0.5f) row(layer, i, y, (lv_opa_t)(255 * smooth((in_win - 0.5f) * 2)), true, ctx);
        else row(layer, i, y, (lv_opa_t)(255 * clampf(1 - fabsf(d) * 0.33f, 0, 1)), false, ctx);
    }
    if (dr->pips_max && dr->n > dr->pips_max) return;
    const int pos = drum_cur(dr);
    for (int i = 0; i < dr->n; i++) { /* position pips */
        int32_t x = CX - (dr->n - 1) * 5 + i * 10, r = i == pos ? 3 : 2;
        theme_draw_box(layer, x - r, PIPS_Y - r, x + r - 1, PIPS_Y + r - 1, LV_RADIUS_CIRCLE,
                       i == pos ? THEME_BRASS : THEME_PIP_OFF, LV_OPA_COVER, 0, 0);
    }
}

bool drum_anim(drum_t *dr, uint32_t dt)
{
    if (dr->drag || dr->s == dr->target) return false;
    float d = dr->target - dr->s; /* the mockup's 0.2 per 60 Hz frame */
    dr->s += d * (1 - powf(0.8f, (float)dt / 16.7f));
    if (fabsf(dr->target - dr->s) < 0.001f) dr->s = dr->target;
    return true;
}

static void drum_to(drum_t *dr, int16_t y, uint32_t now)
{
    float dy = (float)(y - dr->last_y);
    dr->s = clampf(dr->s0 - (float)(y - dr->y0) / DRUM_ROW_H, -0.4f, dr->n - 0.6f);
    dr->v = 0.7f * dr->v + 0.3f * (-dy / DRUM_ROW_H) / fmaxf(1, (float)(now - dr->last_t));
    dr->last_y = y;
    dr->last_t = now;
}

void drum_drag(drum_t *dr, const omni_gesture_t *g, uint32_t t)
{
    switch (g->kind) {
    case G_DRAG_START:
        dr->drag = true;
        dr->y0 = dr->last_y = g->y0;
        dr->s0 = dr->s;
        dr->v = 0;
        dr->last_t = t;
        drum_to(dr, g->y, t);
        break;
    case G_DRAG_MOVE:
        if (dr->drag) drum_to(dr, g->y, t);
        break;
    case G_DRAG_END:
        if (!dr->drag) break;
        drum_to(dr, g->y, t);
        dr->drag = false;
        dr->target = clampf(roundf(dr->s + clampf(dr->v * 180, -3, 3)), 0, dr->n - 1);
        break;
    default: break;
    }
}

int drum_tap(drum_t *dr, int16_t y)
{
    float off = (float)(y - CY) / DRUM_ROW_H;
    if (fabsf(off) < 0.55f) return -1; /* the windowed row */
    float sgn = off > 0 ? 1 : -1;
    dr->target = clampf(roundf(dr->s + sgn * fmaxf(1, roundf(fabsf(off) * 0.8f))), 0, dr->n - 1);
    return (int)dr->target;
}

/* ------------------------------------------------------------ the drum screen (R223(c)) */
const lv_font_t *drum_dim_font(float px)
{
    return px >= 13.5f ? &font_plex_sans_400_14 : px >= 12.5f ? &font_plex_sans_400_13
         : px >= 11.5f ? &font_plex_sans_400_12 : &font_plex_sans_400_11;
}

void drum_inval(lv_obj_t *draw)
{
    lv_area_t a = {CX - 165, 96, CX + 165, 392};
    lv_obj_invalidate_area(draw, &a);
}

int drum_gesture(drum_t *d, const omni_gesture_t *g, uint32_t t, int32_t rim_r)
{
    const int32_t dx = g->x0 - CX, dy = g->y0 - CY, tx = g->x - CX, ty = g->y - CY;
    switch (g->kind) {
    case G_DRAG_START:   /* from the centre only */
    case G_DRAG_MOVE:
    case G_DRAG_END:
        if (g->kind == G_DRAG_START ? dx * dx + dy * dy < rim_r * rim_r : d->drag) {
            drum_drag(d, g, t);
            return DRUM_G_MOVED;
        }
        break;
    case G_TAP:
    case G_LIFT_STILL:   /* a slow tap, where the screen asks for it */
        if (tx * tx + ty * ty < rim_r * rim_r && drum_tap(d, g->y) < 0) return DRUM_G_WINDOW;
        break;
    default: break;
    }
    return DRUM_G_NONE;
}

void drum_screen_create(drum_screen_t *s, lv_obj_t *parent, lv_event_cb_t draw_cb, lv_timer_cb_t anim_cb,
                        bool (*claim)(int16_t, int16_t, void *), uint8_t want)
{
    s->root = theme_box(parent, 0, 0, THEME_W, THEME_W);
    theme_background(s->root, THEME_BG_SETTINGS);
    s->draw = theme_box(s->root, 0, 0, THEME_W, THEME_W);
    lv_obj_add_event_cb(s->draw, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    s->frame = theme_window(s->root, &DRUM_WINDOW);
    s->anim = lv_timer_create(anim_cb, 16, NULL);
    board_set_touch_claim_w(claim, NULL, want);
}

void drum_screen_destroy(drum_screen_t *s)
{
    board_set_touch_claim(NULL, NULL);
    if (s->anim) lv_timer_delete(s->anim);
    if (s->root) lv_obj_delete(s->root);
    memset(s, 0, sizeof *s);
}

void drum_screen_dim(const drum_screen_t *s, bool on)
{
    if (s->root) lv_obj_set_style_opa(s->root, on ? LV_OPA_40 : LV_OPA_COVER, 0);
}
