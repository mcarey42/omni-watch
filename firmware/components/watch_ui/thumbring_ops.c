/* The thumbnail ring's list, order and turning (thumbring.h; the drawing is thumbring.c's). -Os (R228): nothing here
 * decides a pixel the picker's captures show (positions come from rot, set by the screens, and from thumbring.c). */
#include <string.h>
#include "thumbring_int.h"
#include "shell.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "assets.h"

#define clampf thumbring_clampf
#define wrapf thumbring_wrapf

static const theme_window_t WINDOW = {
    .x = WIN_X, .y = WIN_Y, .w = WIN_W, .h = WIN_H, .border = 6, .r_out = 14, .r_in = 10,
    .gx0 = WIN_X, .gy0 = WIN_Y, .gx1 = WIN_X + WIN_W, .gy1 = WIN_Y + WIN_H,
    .edge_opa = .7f, .glass_opa = .14f, .glass_stop = .4f,
};

void thumbring_screen(lv_obj_t *parent, lv_event_cb_t draw_cb, lv_obj_t **root, lv_obj_t **draw)
{
    *root = theme_box(parent, 0, 0, THEME_W, THEME_W);
    theme_background(*root, THEME_BG_PICKER);
    *draw = theme_box(*root, 0, 0, THEME_W, THEME_W);
    lv_obj_add_event_cb(*draw, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    theme_window(*root, &WINDOW);
}

void thumbring_build(const thumbring_item_t *items, int n)
{
    thumbring_free();
    R = heap_caps_calloc(1, sizeof *R, MALLOC_CAP_SPIRAM);
    if (R == NULL) {   /* no ring: nothing drawn, the screen still works */
        ESP_LOGW("thumbring", "no PSRAM for the ring table (%u B)", (unsigned)sizeof *R);
        return;
    }
    const watch_theme_t *th = watch_theme();
    R->canvas = lv_canvas_create(lv_screen_active()); /* offscreen painter for the thumbnails */
    lv_obj_add_flag(R->canvas, LV_OBJ_FLAG_HIDDEN);
    R->n = R->built = R->gap_at = n < THUMBRING_MAX ? n : THUMBRING_MAX;
    for (int i = 0; i < R->n; i++) {
        item_t *it = &R->items[i];
        R->ring[i] = items[i];
        R->p[i] = (uint8_t)i;
        it->it = items[i];
        it->live = items[i].settings || shell_face_ready(items[i].id);
        const char *name = items[i].settings || items[i].id >= th->face_count ? NULL : th->faces[items[i].id].thumb;
        it->thumb = theme_buf_create(THUMB_PX, THUMB_PX, LV_COLOR_FORMAT_ARGB8888);
        it->rest = theme_buf_create(REST_PX, REST_PX, LV_COLOR_FORMAT_ARGB8888);
        if (it->thumb == NULL || it->rest == NULL) ESP_LOGW("thumbring", "item %d: no PSRAM for its pictures", i);
        lv_image_dsc_t img;
        bool have = name && assets_image(name, &img);
        if (name && !have) ESP_LOGW("picker", "thumbnail %s missing: plain disc", name);
        thumbring_paint(it, have ? &img : NULL);
    }
    lv_obj_delete(R->canvas);
    R->canvas = NULL;
}

void thumbring_order(const thumbring_item_t *items, int n, int gap_at)
{
    if (R == NULL) return;
    int k = 0;
    for (int i = 0; i < n && i < R->built; i++) {   /* each to the pictures built for its face */
        int q = 0;
        while (q < R->built - 1 && (R->items[q].it.id != items[i].id || R->items[q].it.settings != items[i].settings)) q++;
        R->ring[k] = items[i];
        R->p[k++] = (uint8_t)q;
    }
    R->n = k;
    R->gap_at = gap_at;
    R->lift = true;
}

void thumbring_free(void)
{
    for (int i = 0; R && i < THUMBRING_MAX; i++) {
        if (R->items[i].thumb) theme_buf_destroy(R->items[i].thumb);
        if (R->items[i].rest) theme_buf_destroy(R->items[i].rest);
    }
    heap_caps_free(R);
    R = NULL;
}

int thumbring_nearest(float rot)
{
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; R && i < R->n; i++) {
        const float d = fabsf(wrapf(rot + slot(i) * step()));
        if (d < bd) bd = d, best = i;
    }
    return best;
}

/* Turning repaints each thumbnail's box at its old and new place. */
void thumbring_item_box(int i, float rot, lv_area_t *a)
{
    if (R == NULL) { *a = (lv_area_t){0, 0, 0, 0}; return; }
    float ang = -(float)M_PI / 2 + rot + slot(i) * step();
    int32_t x = CX + (int32_t)lroundf(cosf(ang) * RING_R), y = CY + (int32_t)lroundf(sinf(ang) * RING_R);
    const int32_t m = THUMB_PX / 2 + 1 + (R->ring[i].numeral >= 0 ? 8 : 0);
    *a = (lv_area_t){x - m, y - m, x + m, y + m};
}

/* ------------------------------------------------------------ turning (shared by the picker and the editor) */
int thumbring_drag(thumbring_turn_t *m, const omni_gesture_t *g, uint32_t t)
{
    if (g->kind == G_DRAG_START) {
        m->drag = true;
        m->vel = 0;
        m->last_a = atan2f(g->y0 - CY, g->x0 - CX);
        m->last_t = t;
    } else if (!m->drag || (g->kind != G_DRAG_MOVE && g->kind != G_DRAG_END)) {
        return 0;
    }
    /* t: when the input task saw this point (events can reach here in bunches while a frame renders) */
    float a = atan2f(g->y - CY, g->x - CX), d = wrapf(a - m->last_a);
    m->rot += d;
    float dt = (float)(t - m->last_t);
    m->vel = 0.7f * m->vel + 0.3f * (d / fmaxf(1, dt));
    m->last_a = a;
    m->last_t = t;
    if (g->kind != G_DRAG_END) return 0;
    m->drag = false;
    return 1;
}

float thumbring_coast(const thumbring_turn_t *m)
{
    const float st = thumbring_step();
    return m->rot + clampf(m->vel * 160, -st * 2.5f, st * 2.5f);
}

bool thumbring_spring(thumbring_turn_t *m, uint32_t now)
{
    uint32_t dt = now - m->t_prev;
    m->t_prev = now;
    if (m->drag || m->rot == m->target) return false;
    float d = m->target - m->rot, k = 1 - powf(1 - 0.18f, (float)dt / 16.7f);
    m->rot += d * k;
    if (fabsf(m->target - m->rot) < 0.0005f) m->rot = m->target;
    return true;
}

void thumbring_inval(lv_obj_t *draw, thumbring_turn_t *m)
{
    if (R == NULL || m->rot == m->drawn_rot) return;
    for (int i = 0; i < R->n; i++) {
        lv_area_t a, b;
        thumbring_item_box(i, m->drawn_rot, &a);
        thumbring_item_box(i, m->rot, &b);
        lv_area_t u = {LV_MIN(a.x1, b.x1), LV_MIN(a.y1, b.y1), LV_MAX(a.x2, b.x2), LV_MAX(a.y2, b.y2)};
        lv_obj_invalidate_area(draw, &u);
    }
    for (int k = 0; k < 16; k++) {
        lv_area_t a;
        theme_arc_area(CX, CY, 228, 14, k * 360 / 16, (k + 1) * 360 / 16, &a);
        lv_obj_invalidate_area(draw, &a);
    }
    m->drawn_rot = m->rot;
}

int thumbring_hit(float rot, int16_t x, int16_t y)
{
    float dx = x - CX, dy = y - CY;
    if (R == NULL || dx * dx + dy * dy <= 146 * 146) return -1;
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < R->n; i++) {
        float a = -(float)M_PI / 2 + rot + slot(i) * step();
        float d = hypotf(x - (CX + cosf(a) * RING_R), y - (CY + sinf(a) * RING_R));
        if (d < bd) bd = d, best = i;
    }
    return bd < THUMB_R + 14 ? best : -1;
}

