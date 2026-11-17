/* The thumbnail ring (thumbring.h): design/mockups/faces-setup.js drawThumb() on nav-map's ring, moved from scr_picker.c.
 *
 * Thumbnails are the faces' own mockup frames (the theme's faces[].thumb: WATCH_THUMB_PX RGB565A8 in the assets
 * partition, a plain tile when NULL), scaled into the disc once and drawn into two small PSRAM canvases: at rest size with
 * the ring (drawn as is) and at the largest size (r 27 x 1.18, scaled while an item nears the window). Settings keeps
 * its drawn star. They are static: nothing repaints while the ring turns. Faces without a view yet carry the mockup's
 * "planned" dimming. */
#include <math.h>
#include <string.h>
#include "thumbring_int.h"

ring_t *thumbring_r;

float thumbring_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
float thumbring_smooth(float t) { t = thumbring_clampf(t, 0, 1); return t * t * (3 - 2 * t); }
float thumbring_wrapf(float a)
{
    while (a <= -(float)M_PI) a += TAU;
    while (a > (float)M_PI) a -= TAU;
    return a;
}
#define smooth thumbring_smooth
#define wrapf thumbring_wrapf

float thumbring_step(void) { return R ? step() : 1; }
float thumbring_slot(int i) { return R ? slot(i) * step() : 0; }

/* ------------------------------------------------------------ thumbnails (drawThumb) */
/* The face's thumbnail (or Settings' star) at radius r into buf (side px). rest: also the brass .55 ring and, for a
 * planned face, the wash, as the ring is drawn for every item not under the window. */
static void paint_disc(const item_t *it, const lv_image_dsc_t *img, lv_draw_buf_t *buf, int32_t px, float r, bool rest)
{
    const float c = px / 2.0f;
    const int32_t ci = px / 2;
    memset(buf->data, 0, buf->data_size);
    lv_canvas_set_draw_buf(R->canvas, buf);
    lv_layer_t layer;
    lv_canvas_init_layer(R->canvas, &layer);
    int32_t ri = (int32_t)lroundf(r);
    theme_draw_box(&layer, ci - ri, ci - ri, ci + ri - 1, ci + ri - 1, LV_RADIUS_CIRCLE, 0x060504, LV_OPA_COVER, 0, 0);
    if (img) { /* the 68 px face circle scaled to the disc */
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = img;
        d.scale_x = d.scale_y = (int32_t)lroundf(256 * 2 * r / THUMB_PX);
        d.pivot.x = d.pivot.y = THUMB_PX / 2;
        d.antialias = 1;
        lv_area_t ar = {ci - THUMB_PX / 2, ci - THUMB_PX / 2, ci - THUMB_PX / 2 + THUMB_PX - 1, ci - THUMB_PX / 2 + THUMB_PX - 1};
        lv_draw_image(&layer, &d, &ar);
    }
    if (it->it.settings) {
        /* Settings: the 16-point star: a disc at the valleys (.42 r) plus 8 teeth to .56 r, and the hole */
        int32_t rv = (int32_t)lroundf(r * .42f);
        theme_draw_box(&layer, ci - rv, ci - rv, ci + rv - 1, ci + rv - 1, LV_RADIUS_CIRCLE, THEME_BRASS, LV_OPA_COVER, 0, 0);
        for (int k = 0; k < 8; k++) {
            float at = (2 * k) / 16.0f * TAU, a0 = (2 * k - 1) / 16.0f * TAU, a1 = (2 * k + 1) / 16.0f * TAU;
            lv_draw_triangle_dsc_t t;
            lv_draw_triangle_dsc_init(&t);
            t.color = lv_color_hex(THEME_BRASS);
            t.p[0].x = c + cosf(a0) * r * .42f, t.p[0].y = c + sinf(a0) * r * .42f;
            t.p[1].x = c + cosf(at) * r * .56f, t.p[1].y = c + sinf(at) * r * .56f;
            t.p[2].x = c + cosf(a1) * r * .42f, t.p[2].y = c + sinf(a1) * r * .42f;
            lv_draw_triangle(&layer, &t);
        }
        int32_t rh = (int32_t)lroundf(r * .2f);
        theme_draw_box(&layer, ci - rh, ci - rh, ci + rh - 1, ci + rh - 1, LV_RADIUS_CIRCLE, 0x060504, LV_OPA_COVER, 0, 0);
    }
    if (rest) {
        theme_draw_arc(&layer, ci, ci, (int32_t)lroundf(r + 1), 2, 0, 360, THEME_BRASS, THEME_OPA(0.55), false);
        if (!it->live) {
            int32_t rr = (int32_t)lroundf(r);
            theme_draw_box(&layer, ci - rr, ci - rr, ci + rr - 1, ci + rr - 1, LV_RADIUS_CIRCLE, 0x000000, THEME_OPA(0.45), 0, 0);
        }
    }
    lv_canvas_finish_layer(R->canvas, &layer);
}

/* img: the face's exported thumbnail (mapped from flash; only needed while painting), or NULL for Settings, a face
 * without one (a plain tile) or a lost asset */
void thumbring_paint(const item_t *it, const lv_image_dsc_t *img)
{
    if (it->thumb) paint_disc(it, img, it->thumb, THUMB_PX, THUMB_RMAX, false);
    if (it->rest) paint_disc(it, img, it->rest, REST_PX, THUMB_R, true);
}

/* ------------------------------------------------------------ drawing (frame()) */
/* An item off its rest picture (nearing the window, or the landed selection): the large one scaled to r, its rim, and the
 * planned wash. */
static void thumb_moving(lv_layer_t *layer, const item_t *it, float x, float y, float r, bool hi)
{
    if (it->thumb) {
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = it->thumb;
        d.scale_x = d.scale_y = (int32_t)lroundf(256 * r / THUMB_RMAX);
        d.pivot.x = THUMB_PX / 2;
        d.pivot.y = THUMB_PX / 2;
        d.antialias = 1;
        int32_t x0 = (int32_t)lroundf(x) - THUMB_PX / 2, y0 = (int32_t)lroundf(y) - THUMB_PX / 2;
        lv_area_t ar = {x0, y0, x0 + THUMB_PX - 1, y0 + THUMB_PX - 1};
        lv_draw_image(layer, &d, &ar);
    }
    /* rim: brassHi 2.5 on the landed selection, brass .65 1.5 otherwise */
    int32_t w = hi ? 3 : 2;
    theme_draw_arc(layer, (int32_t)lroundf(x), (int32_t)lroundf(y), (int32_t)lroundf(r + w / 2.0f), w, 0, 360,
                   hi ? THEME_SETUP_BRASS_HI : THEME_BRASS, hi ? LV_OPA_COVER : THEME_OPA(0.55), false);
    if (!it->live) { /* planned, not in this build: the mockup's dark wash */
        int32_t rr = (int32_t)lroundf(r);
        theme_draw_box(layer, (int32_t)lroundf(x) - rr, (int32_t)lroundf(y) - rr, (int32_t)lroundf(x) + rr - 1,
                       (int32_t)lroundf(y) + rr - 1, LV_RADIUS_CIRCLE, 0x000000, THEME_OPA(0.45), 0, 0);
    }
}

void thumbring_draw(lv_layer_t *layer, float rot, int sel)
{
    if (R == NULL) return;
    const float st = step();
    for (int i = 0; i < R->n; i++) {
        float a = -(float)M_PI / 2 + rot + slot(i) * st;
        float near = fmaxf(0, 1 - fabsf(wrapf(a + (float)M_PI / 2)) / (st * 0.8f));
        float r = THUMB_R * (1 + 0.18f * smooth(near));
        float x = CX + cosf(a) * RING_R, y = CY + sinf(a) * RING_R;
        const item_t *it = &R->items[R->p[i]];
        const thumbring_item_t *ri = &R->ring[i];
        int32_t xi = (int32_t)lroundf(x), yi = (int32_t)lroundf(y);
        const int32_t m = THUMB_PX / 2 + (ri->numeral >= 0 ? 8 : 0);   /* a numeral reaches past the box inward (to 40.9 px at the window) */
        if (!theme_on_layer(layer, xi - m, yi - m, xi + m, yi + m)) continue;
        bool hi = i == sel;
        if (near <= 0 && !hi && it->rest) { /* the usual case: the pre-drawn resting thumbnail */
            lv_draw_image_dsc_t d;
            lv_draw_image_dsc_init(&d);
            d.src = it->rest;
            lv_area_t ar = {xi - REST_PX / 2, yi - REST_PX / 2, xi + REST_PX / 2 - 1, yi + REST_PX / 2 - 1};
            lv_draw_image(layer, &d, &ar);
        } else {
            thumb_moving(layer, it, x, y, r, hi);
        }
        if (ri->opa < LV_OPA_COVER) { /* dimmed: a brightness wash, ring included; the editor's lift to full in the window (R207) */
            const int32_t rr = (int32_t)lroundf(r + 1.5f);
            theme_draw_box(layer, xi - rr, yi - rr, xi + rr, yi + rr, LV_RADIUS_CIRCLE, 0x000000,
                           (lv_opa_t)((LV_OPA_COVER - ri->opa) * (R->lift ? 1 - smooth(near) : 1)), 0, 0);
        }
        if (ri->numeral >= 0 && !hi) { /* the order numeral on the inner edge: a dark disc, brass hairline, brassHi */
            const float rb = RING_R - r - 1;
            const int32_t bx = (int32_t)lroundf(CX + cosf(a) * rb), by = (int32_t)lroundf(CY + sinf(a) * rb);
            char num[4];
            num[0] = (char)('0' + ri->numeral / 10), num[1] = (char)('0' + ri->numeral % 10), num[2] = 0;
            theme_draw_rect(layer, bx - 8, by - 8, bx + 8, by + 8, LV_RADIUS_CIRCLE, 0x120e08, LV_OPA_COVER, 1, THEME_BRASS,
                            THEME_OPA(0.85));
            theme_draw_text(layer, ri->numeral < 10 ? num + 1 : num, &font_plex_sans_600_10, THEME_SETUP_BRASS_HI,
                            LV_OPA_COVER, 0, THEME_CENTER, bx, by + 4);
        }
    }
}

void thumbring_index(lv_layer_t *layer)
{
    /* red V index under the window */
    if (theme_on_layer(layer, CX - 7, WIN_Y + WIN_H + 8, CX + 7, WIN_Y + WIN_H + 17)) {
        lv_draw_triangle_dsc_t t;
        lv_draw_triangle_dsc_init(&t);
        t.color = lv_color_hex(THEME_RED);
        t.p[0].x = CX - 7, t.p[0].y = WIN_Y + WIN_H + 8;
        t.p[1].x = CX + 7, t.p[1].y = WIN_Y + WIN_H + 8;
        t.p[2].x = CX, t.p[2].y = WIN_Y + WIN_H + 17;
        lv_draw_triangle(layer, &t);
    }
}

/* ------------------------------------------------------------ the scale (shared by the picker and the editor) */
void thumbring_tick(lv_layer_t *layer, float a, float r0, float r1, int32_t w, uint32_t rgb, lv_opa_t opa)
{
    theme_draw_line(layer, (int32_t)lroundf(CX + cosf(a) * r0), (int32_t)lroundf(CY + sinf(a) * r0),
                    (int32_t)lroundf(CX + cosf(a) * r1), (int32_t)lroundf(CY + sinf(a) * r1), w, rgb, opa, false);
}

void thumbring_scale(lv_layer_t *layer, float rot, int sub, int majors, lv_opa_t major_opa)
{
    if (R == NULL) return;
    const float st = step();
    const int n = (int)lroundf(total() * sub);
    for (int i = 0; i < n; i++) {
        float a = -(float)M_PI / 2 + rot + i * st / sub;
        bool major = i % sub == 0 && i / sub < majors, mid = i % (sub / 2) == 0;
        float r1 = major ? 226 : mid ? 223 : 220;
        thumbring_tick(layer, a, 216, r1, major ? 2 : 1, 0x1e1204, major ? major_opa : THEME_OPA(0.55));
    }
}
