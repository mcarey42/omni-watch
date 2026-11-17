#include "theme.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "assets.h"
#include "omni/brassbg.h"

lv_draw_buf_t *theme_buf_create(uint32_t w, uint32_t h, lv_color_format_t cf)
{
    uint32_t stride = lv_draw_buf_width_to_stride(w, cf), size = stride * h;
    if (cf == LV_COLOR_FORMAT_RGB565A8) size += stride / 2 * h; /* the A8 plane after the RGB565 one, as LVGL sizes it */
    lv_draw_buf_t *b = heap_caps_calloc(1, sizeof *b, MALLOC_CAP_SPIRAM);
    void *data = b ? heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_SPIRAM) : NULL;
    if (data == NULL || lv_draw_buf_init(b, w, h, cf, stride, data, size) != LV_RESULT_OK) {
        heap_caps_free(data);
        heap_caps_free(b);
        return NULL;
    }
    return b;
}

/* m11: freed without lv_image_cache_drop, which is right only while LVGL caches no image data or headers (an image
 * shown from a freed buffer would otherwise outlive it in the cache). */
#if !defined(CONFIG_LV_CACHE_DEF_SIZE) || !defined(CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT) || \
    CONFIG_LV_CACHE_DEF_SIZE != 0 || CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT != 0
#error "theme_buf_destroy needs the LVGL image cache and image header cache off"
#endif

void theme_buf_destroy(lv_draw_buf_t *b)
{
    if (b == NULL) return;
    heap_caps_free(b->data);
    heap_caps_free(b);
}

bool theme_on_layer(const lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    const lv_area_t *c = &layer->_clip_area;
    return x2 >= c->x1 && x1 <= c->x2 && y2 >= c->y1 && y1 <= c->y2;
}

lv_obj_t *theme_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

void theme_set_text(lv_obj_t *label, const char *s)
{
    if (strcmp(lv_label_get_text(label), s) != 0) lv_label_set_text(label, s);
}

lv_obj_t *theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, lv_opa_t opa, int32_t letter_space)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_opa(l, opa, 0);
    lv_obj_set_style_text_letter_space(l, letter_space, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return l;
}

void theme_place(lv_obj_t *label, theme_align_t align, int32_t x, int32_t baseline_y)
{
    const lv_font_t *f = lv_obj_get_style_text_font(label, 0);
    int32_t ls = lv_obj_get_style_text_letter_space(label, 0);
    const char *txt = lv_label_get_text(label);
    lv_point_t sz;
    lv_text_get_size(&sz, txt, f, ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t w = sz.x;
    if (txt[0] != '\0') w += ls; /* the canvas counts the spacing after the last glyph */
    int32_t left = align == THEME_LEFT ? x : align == THEME_CENTER ? x - w / 2 : x - w;
    lv_obj_set_pos(label, left, baseline_y - (f->line_height - f->base_line));
}

/* ------------------------------------------------------------ custom-draw helpers */
/* The arc's own box (its end points and the axis extremes it passes, outer and inner radius): a short segment of
 * a big ring touches only a few render bands. Angles in degrees, clockwise from 3 o'clock; a1 < a0 wraps. */
void theme_arc_area(int32_t cx, int32_t cy, int32_t r, int32_t w, int32_t a0, int32_t a1, lv_area_t *out)
{
    a0 = ((a0 % 360) + 360) % 360;
    a1 = ((a1 % 360) + 360) % 360;
    int32_t span = a1 - a0;
    if (span <= 0) span += 360;
    if (span >= 360 || (a0 == a1)) {
        *out = (lv_area_t){cx - r, cy - r, cx + r, cy + r};
        return;
    }
    float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
    float rr[2] = {(float)r, (float)(r - w > 0 ? r - w : 0)};
    int32_t angs[6] = {a0, a1, 0, 90, 180, 270};
    for (int i = 0; i < 6; i++) {
        int32_t d = ((angs[i] - a0) % 360 + 360) % 360;
        if (i >= 2 && d > span) continue; /* an axis extreme outside the arc */
        float a = (float)angs[i] * (float)M_PI / 180;
        for (int k = 0; k < 2; k++) {
            float x = cx + cosf(a) * rr[k], y = cy + sinf(a) * rr[k];
            x1 = fminf(x1, x), y1 = fminf(y1, y), x2 = fmaxf(x2, x), y2 = fmaxf(y2, y);
        }
    }
    *out = (lv_area_t){(int32_t)x1 - w - 1, (int32_t)y1 - w - 1, (int32_t)x2 + w + 1, (int32_t)y2 + w + 1};
}

static bool arc_on_layer(const lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r, int32_t w, int32_t a0, int32_t a1)
{
    lv_area_t a;
    theme_arc_area(cx, cy, r, w, a0, a1, &a);
    return theme_on_layer(layer, a.x1, a.y1, a.x2, a.y2);
}

void theme_draw_arc(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r, int32_t w, int32_t a0, int32_t a1,
                    uint32_t rgb, lv_opa_t opa, bool rounded)
{
    if (!arc_on_layer(layer, cx, cy, r, w, a0, a1)) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)r;
    d.width = w;
    d.start_angle = a0;
    d.end_angle = a1;
    d.color = lv_color_hex(rgb);
    d.opa = opa;
    d.rounded = rounded;
    lv_draw_arc(layer, &d);
}

void theme_draw_line(lv_layer_t *layer, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint32_t rgb,
                     lv_opa_t opa, bool round)
{
    if (!theme_on_layer(layer, LV_MIN(x0, x1) - w, LV_MIN(y0, y1) - w, LV_MAX(x0, x1) + w, LV_MAX(y0, y1) + w)) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x0;
    d.p1.y = y0;
    d.p2.x = x1;
    d.p2.y = y1;
    d.width = w;
    d.color = lv_color_hex(rgb);
    d.opa = opa;
    d.round_start = d.round_end = round;
    lv_draw_line(layer, &d);
}

void theme_draw_box(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius, uint32_t fill,
                    lv_opa_t fill_opa, int32_t border, lv_opa_t border_opa)
{
    theme_draw_rect(layer, x1, y1, x2, y2, radius, fill, fill_opa, border, fill, border_opa);   /* the same descriptor */
}

/* ------------------------------------------------------------ jewel lens */
#define LENS_JEWEL 7

void theme_lens_lit(void *obj, int32_t v)
{
    /* faces-setup.js lens(): k = 0.18 + 0.82 lit, highlight and glow scale with lit */
    static const float BASE[2][3] = {{216, 68, 58}, {79, 208, 106}};
    const float *b = BASE[lv_obj_get_user_data(obj) != NULL];
    float lit = (float)v / 1000.0f, k = 0.18f + 0.82f * lit;
    int r = (int)fminf(255, b[0] * k + 90 * lit), g = (int)fminf(255, b[1] * k + 90 * lit),
        bl = (int)fminf(255, b[2] * k + 70 * lit);
    lv_obj_set_style_bg_color(obj, lv_color_make(r, g, bl), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_make((int)(b[0] * k * .45f), (int)(b[1] * k * .45f), (int)(b[2] * k * .45f)), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)(0.35f * lit * 255), 0);
}

lv_obj_t *theme_lens(lv_obj_t *parent, int32_t x, int32_t y, bool green)
{
    int32_t rr = LENS_JEWEL + 3;
    lv_obj_t *bezel = theme_box(parent, x - rr, y - rr, rr * 2, rr * 2);
    lv_obj_set_style_radius(bezel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(bezel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bezel, lv_color_hex(0xe6c886), 0);
    lv_obj_set_style_bg_grad_color(bezel, lv_color_hex(0x3d2c14), 0);
    lv_obj_set_style_bg_grad_dir(bezel, LV_GRAD_DIR_VER, 0);
    lv_obj_add_flag(bezel, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *jewel = theme_box(bezel, 3, 3, LENS_JEWEL * 2, LENS_JEWEL * 2);
    lv_obj_set_user_data(jewel, green ? (void *)1 : NULL);
    lv_obj_set_style_radius(jewel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(jewel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(jewel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(jewel, lv_color_hex(green ? THEME_GREEN : THEME_RED), 0);
    lv_obj_set_style_shadow_width(jewel, LENS_JEWEL * 3, 0);
    lv_obj_set_style_shadow_spread(jewel, 1, 0);
    theme_lens_lit(jewel, 1000);

    /* The specular glint, upper left: (2, 2) in the jewel, made the jewel's younger sibling at the same screen place
     * (R230). Each level of LVGL nesting costs the LVGL task 640 B of stack while it draws (lv_obj_refr's frame and
     * lv_obj_redraw's), and the glint was the deepest object on the provisioning screens (screen, screen root,
     * provisioning root, bezel, jewel, glint). The pixels do not change. The glint lies inside the jewel, so the
     * jewel's clip never cut it. Nothing draws between the jewel's children and its post-draw, because the jewel has no
     * border, outline or scrollbar. So the glint still draws straight after the jewel's shadow and fill. */
    lv_obj_t *hi = theme_box(bezel, 3 + 2, 3 + 2, 5, 3);
    lv_obj_set_style_radius(hi, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hi, THEME_OPA(0.55), 0);
    lv_obj_set_style_bg_color(hi, lv_color_white(), 0);
    return jewel;
}

/* Two-circle radial gradient position (canvas createRadialGradient(x0, y0, r0, x1, y1, r1)) for a point, padded to
 * 0..1. The start circle lies inside the end circle in every use here, so there is one root with r(t) >= 0. */
float theme_radial_t(float px, float py, float x0, float y0, float r0, float x1, float y1, float r1)
{
    float dcx = x1 - x0, dcy = y1 - y0, dr = r1 - r0, pdx = px - x0, pdy = py - y0;
    float a = dcx * dcx + dcy * dcy - dr * dr, b = -2 * (pdx * dcx + pdy * dcy + r0 * dr), c = pdx * pdx + pdy * pdy - r0 * r0;
    float t;
    if (fabsf(a) < 1e-6f) {
        t = fabsf(b) < 1e-6f ? 0 : -c / b;
    } else {
        float disc = b * b - 4 * a * c;
        if (disc < 0) disc = 0;
        float q = sqrtf(disc), t1 = (-b + q) / (2 * a), t2 = (-b - q) / (2 * a);
        t = fmaxf(t1, t2);
        if (r0 + t * dr < 0) t = fminf(t1, t2);
    }
    return omni_bg_clampf(t, 0, 1);
}

/* ------------------------------------------------------------ opaque brass backgrounds (setup bezel, picker, settings) */
/* The three backgrounds are flash assets since R112 (bg_*.bin, mapped, nothing copied: R70's read-only art). If the
 * assets image is missing or rejected (assets_init failed: a file-count or checksum mismatch unmaps all of it, D13),
 * a background is rendered once on first use by omni_core's brassbg.c (the code that made the assets' bytes on the
 * Stage 3 device, R121/B1) into a PSRAM buffer and kept. Every caller holds the display lock (LVGL, and the console's
 * `theme dump`), which already serialises them; s_bg_lock, created on first use under it, keeps a render single
 * should a caller ever come without it. */
/* Looked up by name: every theme's image carries these three files (the dw stub, copies of the Omni's). */
static const char *const BG_ASSET[THEME_BG__COUNT] = {"bg_bezel.bin", "bg_picker.bin", "bg_settings.bin"};
_Static_assert((int)THEME_BG__COUNT == (int)OMNI_BG__COUNT && (int)THEME_BG_BEZEL == (int)OMNI_BG_BEZEL &&
                   (int)THEME_BG_PICKER == (int)OMNI_BG_PICKER && (int)THEME_BG_SETTINGS == (int)OMNI_BG_SETTINGS &&
                   THEME_W == OMNI_BG_W && THEME_CX == OMNI_BG_C && THEME_CY == OMNI_BG_C,
               "theme_bg_t and the screen geometry match omni/brassbg.h");
static lv_image_dsc_t s_bg[THEME_BG__COUNT];   /* data NULL: not loaded yet */
static SemaphoreHandle_t s_bg_lock;
static bool s_bg_nomem[THEME_BG__COUNT];   /* the fallback's allocation failure, logged once each */

static const lv_image_dsc_t *bg_get(theme_bg_t which)
{
    if (s_bg_lock == NULL) s_bg_lock = xSemaphoreCreateMutex();
    if (s_bg_lock) xSemaphoreTake(s_bg_lock, portMAX_DELAY);
    lv_image_dsc_t *d = &s_bg[which];
    if (d->data == NULL) {
        lv_image_dsc_t a;
        if (assets_image(BG_ASSET[which], &a) && a.header.cf == LV_COLOR_FORMAT_RGB565 && a.header.w == THEME_W &&
            a.header.h == THEME_W && a.header.stride == THEME_W * 2) {
            *d = a;
        } else {
            /* Straight through, as a first-use render always ran (1-2 s on the caller's task, the display lock held:
             * a yield here would only hand the lock's waiters nothing to do). */
            uint8_t *strip = heap_caps_malloc((size_t)THEME_W * 4 * OMNI_BG_STRIP, MALLOC_CAP_SPIRAM);
            lv_draw_buf_t *out = theme_buf_create(THEME_W, THEME_W, LV_COLOR_FORMAT_RGB565);
            if (strip && out && out->header.stride == THEME_W * 2) {
                omni_bg_render((omni_bg_t)which, (uint16_t *)out->data, strip, NULL, NULL);
                d->header = out->header;
                d->data_size = out->data_size;
                d->data = out->data;   /* kept: out's descriptor is never freed either */
                out = NULL;
                ESP_LOGW("theme", "background %d: asset missing, rendered (crc %08" PRIx32 ")", which,
                         esp_rom_crc32_le(0, d->data, d->data_size));
            } else if (!s_bg_nomem[which]) {   /* retried on every use; said once */
                s_bg_nomem[which] = true;
                ESP_LOGE("theme", "background %d: asset missing, and no PSRAM to render it (shown blank)", which);
            }
            heap_caps_free(strip);
            theme_buf_destroy(out);
        }
    }
    if (s_bg_lock) xSemaphoreGive(s_bg_lock);
    return d->data ? d : NULL;
}

lv_obj_t *theme_background(lv_obj_t *parent, theme_bg_t which)
{
    if ((unsigned)which >= THEME_BG__COUNT) return NULL;
    const lv_image_dsc_t *src = bg_get(which);
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(img, 0, 0);
    if (src) lv_image_set_src(img, src);
    return img;
}

lv_obj_t *theme_bezel(lv_obj_t *parent) { return theme_background(parent, THEME_BG_BEZEL); }

#if CONFIG_OMNI_DEBUG_CONSOLE
const uint8_t *theme_bg_pixels(theme_bg_t which)
{
    if ((unsigned)which >= THEME_BG__COUNT) return NULL;
    const lv_image_dsc_t *d = bg_get(which);
    return d ? d->data : NULL;
}
#endif

/* ------------------------------------------------------------ brass window frames */
static float rrect_sd(float px, float py, float x, float y, float w, float h, float r) /* signed distance, <0 inside */
{
    float hx = w / 2, hy = h / 2, qx = fabsf(px - (x + hx)) - (hx - r), qy = fabsf(py - (y + hy)) - (hy - r);
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

lv_obj_t *theme_window(lv_obj_t *parent, const theme_window_t *w)
{
    static struct { const theme_window_t *spec; lv_draw_buf_t *buf; } cache[4];
    lv_draw_buf_t *buf = NULL;
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (cache[i].spec == w) buf = cache[i].buf;
        if (cache[i].spec == NULL && slot < 0) slot = i;
    }
    const int32_t b = w->border, x0 = w->x - b - 1, y0 = w->y - b - 1, bw = w->w + 2 * b + 2, bh = w->h + 2 * b + 2;
    if (buf == NULL && slot >= 0) {
        buf = theme_buf_create(bw, bh, LV_COLOR_FORMAT_ARGB8888);
        if (buf) {
            memset(buf->data, 0, buf->data_size);
            const omni_bg_stop_t st[3] = {{0, 0xf2, 0xd7, 0x97, 1}, {.5f, 0xa0, 0x7b, 0x38, 1}, {1, 0x4a, 0x35, 0x18, 1}};
            const float gvx = w->gx1 - w->gx0, gvy = w->gy1 - w->gy0, gv2 = gvx * gvx + gvy * gvy;
            for (int y = 0; y < bh; y++) {
                uint8_t *row = buf->data + y * buf->header.stride;
                for (int x = 0; x < bw; x++) {
                    float px = x0 + x + 0.5f, py = y0 + y + 0.5f, s[4];
                    float so = rrect_sd(px, py, w->x - b, w->y - b, w->w + 2 * b, w->h + 2 * b, w->r_out);
                    float si = rrect_sd(px, py, w->x, w->y, w->w, w->h, w->r_in);
                    float co = omni_bg_clampf(0.5f - so, 0, 1), ci = omni_bg_clampf(0.5f - si, 0, 1);
                    uint8_t *p = row + x * 4;
                    float ring = co * (1 - ci); /* even-odd: outer minus inner */
                    if (ring > 0) {
                        omni_bg_grad(st, 3, omni_bg_clampf(((px - w->gx0) * gvx + (py - w->gy0) * gvy) / gv2, 0, 1), s);
                        omni_bg_px_rgba(p, s[0], s[1], s[2], 1, ring);
                    }
                    if (w->edge_opa > 0) omni_bg_px_rgba(p, 0, 0, 0, w->edge_opa, omni_bg_clampf(1 - fabsf(si), 0, 1));
                    if (ci > 0) { /* glass: white glass_opa at the top fading to 0 at glass_stop of the height */
                        float t = (py - w->y) / (w->h * w->glass_stop);
                        if (t < 1) omni_bg_px_rgba(p, 255, 255, 255, w->glass_opa * (1 - omni_bg_clampf(t, 0, 1)), ci);
                    }
                }
            }
            cache[slot].spec = w;
            cache[slot].buf = buf;
        }
    }
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(img, x0, y0);
    if (buf) lv_image_set_src(img, buf);
    return img;
}

/* ------------------------------------------------------------ rim knob (settings level rows) */
const lv_draw_buf_t *theme_knob(void)
{
    static lv_draw_buf_t *buf;
    if (buf) return buf;
    buf = theme_buf_create(THEME_KNOB_SIZE, THEME_KNOB_SIZE, LV_COLOR_FORMAT_ARGB8888);
    if (buf == NULL) return NULL;
    memset(buf->data, 0, buf->data_size);
    /* radial (kx - 4, ky - 4, 1) -> (kx, ky, 13): #fff0c8, .5 #c9a35b, 1 #5a4220; disc r 13 */
    const omni_bg_stop_t st[3] = {{0, 0xff, 0xf0, 0xc8, 1}, {.5f, 0xc9, 0xa3, 0x5b, 1}, {1, 0x5a, 0x42, 0x20, 1}};
    const float c = THEME_KNOB_SIZE / 2.0f;
    for (int y = 0; y < THEME_KNOB_SIZE; y++) {
        uint8_t *row = buf->data + y * buf->header.stride;
        for (int x = 0; x < THEME_KNOB_SIZE; x++) {
            float px = x + 0.5f, py = y + 0.5f, d = sqrtf((px - c) * (px - c) + (py - c) * (py - c)), s[4];
            float cov = omni_bg_clampf(13 - d + 0.5f, 0, 1);
            if (cov <= 0) continue;
            omni_bg_grad(st, 3, theme_radial_t(px, py, c - 4, c - 4, 1, c, c, 13), s);
            omni_bg_px_rgba(row + x * 4, s[0], s[1], s[2], 1, cov);
        }
    }
    return buf;
}

/* ------------------------------------------------------------ text in draw callbacks */
static int32_t text_width(const char *txt, const lv_font_t *f, int32_t ls)
{
    lv_point_t sz;
    lv_text_get_size(&sz, txt, f, ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

static __attribute__((noinline)) void draw_text(lv_layer_t *layer, const char *txt, const lv_font_t *font, uint32_t rgb, lv_opa_t opa,
                      int32_t letter_space, theme_align_t align, int32_t x, int32_t baseline, bool local)
{
    if (txt == NULL || txt[0] == '\0' || opa <= LV_OPA_MIN) return;
    int32_t w = text_width(txt, font, letter_space);
    int32_t left = align == THEME_LEFT ? x : align == THEME_CENTER ? x - w / 2 : x - w;
    int32_t top = baseline - (font->line_height - font->base_line);
    if (!theme_on_layer(layer, left, top, left + w + 2, top + font->line_height)) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = txt;
    d.font = font;
    d.color = lv_color_hex(rgb);
    d.opa = opa;
    d.letter_space = letter_space;
    d.text_local = local;
    lv_area_t a = {left, top, left + w + 2, top + font->line_height};
    lv_draw_label(layer, &d, &a);
}

void theme_draw_text(lv_layer_t *layer, const char *txt, const lv_font_t *font, uint32_t rgb, lv_opa_t opa,
                     int32_t letter_space, theme_align_t align, int32_t x, int32_t baseline)
{
    draw_text(layer, txt, font, rgb, opa, letter_space, align, x, baseline, true);
}

void theme_draw_text_ref(lv_layer_t *layer, const char *txt, const lv_font_t *font, uint32_t rgb, int32_t x, int32_t baseline)
{
    draw_text(layer, txt, font, rgb, LV_OPA_COVER, 0, THEME_LEFT, x, baseline, false);
}

int32_t theme_text_width(const char *txt, const lv_font_t *font, int32_t letter_space)
{
    return text_width(txt, font, letter_space);
}

static uint32_t utf8_next(const char *s, uint32_t *i) /* the code point at s[*i]; advances *i */
{
    const uint8_t *p = (const uint8_t *)s + *i;
    int n = p[0] < 0x80 ? 1 : (p[0] & 0xe0) == 0xc0 ? 2 : (p[0] & 0xf0) == 0xe0 ? 3 : 4;
    uint32_t cp = n == 1 ? p[0] : n == 2 ? p[0] & 0x1f : n == 3 ? p[0] & 0x0f : p[0] & 0x07;
    for (int k = 1; k < n && p[k]; k++) cp = cp << 6 | (p[k] & 0x3f);
    *i += (uint32_t)n;
    return cp;
}

/* Arc text is rasterised here, not by LVGL: its rotated letters/images lose the parts of a glyph that cross the
 * 16-line render bands. Each label (text, font, radius, angle, tracking) is drawn once into an A8 strip by
 * bilinear sampling of the font's glyph bitmaps, kept in PSRAM, and blitted unrotated (recoloured) - the same
 * path as plain text. Strips used by the current draw call are never evicted (LVGL finishes a band's draw tasks
 * before it asks for the next band, so older strips are free to go). */
#define GLYPH_CACHE 96
static struct glyph_img {
    const lv_font_t *font;
    uint32_t cp;
    uint8_t *a8;
    uint16_t w, h, stride;
    int16_t ofs_x, ofs_y, adv_w;
} *s_glyphs;
static int s_nglyphs;

static const struct glyph_img *glyph_img(const lv_font_t *font, uint32_t cp)
{
    for (int i = 0; i < s_nglyphs; i++)
        if (s_glyphs[i].font == font && s_glyphs[i].cp == cp) return &s_glyphs[i];
    if (s_glyphs == NULL) s_glyphs = heap_caps_calloc(GLYPH_CACHE, sizeof *s_glyphs, MALLOC_CAP_SPIRAM);
    if (s_glyphs == NULL || s_nglyphs >= GLYPH_CACHE) return NULL;
    struct glyph_img *gi = &s_glyphs[s_nglyphs++];
    gi->font = font;
    gi->cp = cp;
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(font, &g, cp, 0) || g.box_w == 0 || g.box_h == 0) return gi; /* a space: no bitmap */
    gi->ofs_x = g.ofs_x;
    gi->ofs_y = g.ofs_y;
    gi->adv_w = (int16_t)g.adv_w;
    uint32_t stride = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8), size = stride * g.box_h;
    lv_draw_buf_t tmp;
    void *data = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_SPIRAM);
    if (data == NULL) return gi;
    lv_draw_buf_init(&tmp, g.box_w, g.box_h, LV_COLOR_FORMAT_A8, stride, data, size);
    g.req_raw_bitmap = 0;
    if (lv_font_get_glyph_bitmap(&g, &tmp) == NULL) {
        heap_caps_free(data);
        return gi;
    }
    gi->a8 = data;
    gi->w = g.box_w;
    gi->h = g.box_h;
    gi->stride = (uint16_t)stride;
    return gi;
}

static float glyph_at(const struct glyph_img *gi, float u, float v) /* bilinear coverage 0..255, 0 outside */
{
    u -= 0.5f, v -= 0.5f;
    int iu = (int)floorf(u), iv = (int)floorf(v);
    float fu = u - iu, fv = v - iv, acc = 0;
    for (int k = 0; k < 4; k++) {
        int x = iu + (k & 1), y = iv + (k >> 1);
        if (x < 0 || y < 0 || x >= gi->w || y >= gi->h) continue;
        acc += gi->a8[y * gi->stride + x] * ((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv);
    }
    return acc;
}

#define ARC_CACHE 24
#define ARC_TEXT_MAX 32
static struct arc_label {
    char text[ARC_TEXT_MAX];
    const lv_font_t *font;
    float r, a, track;
    lv_draw_buf_t buf;
    int32_t x, y;           /* strip position on screen */
    uint32_t used;          /* draw-call stamp */
    bool ok;
} *s_arcs;
static uint32_t s_arc_stamp;
/* arc_label's glyph placement, in PSRAM beside the cache: 896 B that sat on the LVGL task's stack under lv_draw_image
 * (the Settings draw's deepest path, Task 9; spec 11). arc_label is noinline for the same reason: its frame is gone
 * before theme_draw_arc_text draws. LVGL task (or under the LVGL lock) only, as the cache. */
static struct arc_scratch {
    struct { const struct glyph_img *gi; float bx, by, c, sn; } gl[ARC_TEXT_MAX];
    float ws[ARC_TEXT_MAX];
    uint32_t cps[ARC_TEXT_MAX];
} *s_arc_tmp;

static __attribute__((noinline)) struct arc_label *arc_label(const char *s, const lv_font_t *font, float r, float a,
                                                             float track)
{
    if (s_arcs == NULL) s_arcs = heap_caps_calloc(ARC_CACHE, sizeof *s_arcs, MALLOC_CAP_SPIRAM);
    if (s_arc_tmp == NULL) s_arc_tmp = heap_caps_malloc(sizeof *s_arc_tmp, MALLOC_CAP_SPIRAM);
    if (s_arcs == NULL || s_arc_tmp == NULL || strlen(s) >= ARC_TEXT_MAX) return NULL;
    struct arc_label *victim = NULL;
    for (int i = 0; i < ARC_CACHE; i++) {
        struct arc_label *l = &s_arcs[i];
        if (l->ok && l->font == font && l->r == r && l->a == a && l->track == track && strcmp(l->text, s) == 0) {
            l->used = s_arc_stamp;
            return l;
        }
        if (l->used != s_arc_stamp && (victim == NULL || !l->ok || (victim->ok && l->used < victim->used))) victim = l;
    }
    if (victim == NULL) return NULL; /* all in use by this draw call */
    if (victim->ok) heap_caps_free(victim->buf.data);
    memset(victim, 0, sizeof *victim);

    /* glyph placement (arcText): each glyph's baseline centre B, its rotation, its "up" vector */
    __typeof__(s_arc_tmp->gl[0]) *const gl = s_arc_tmp->gl;
    float *const ws = s_arc_tmp->ws;
    float total = 0;
    uint32_t *const cps = s_arc_tmp->cps;
    int n = 0;
    for (uint32_t i = 0; s[i] && n < ARC_TEXT_MAX;) {
        cps[n] = utf8_next(s, &i);
        ws[n] = lv_font_get_glyph_width(font, cps[n], 0) + track;
        total += ws[n++];
    }
    total -= track;
    const bool bottom = sinf(a) > 0.25f;
    const float mid = (font->line_height - 2.0f * font->base_line) / 2; /* em middle above the baseline */
    float cur = bottom ? a + total / 2 / r : a - total / 2 / r;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        float w = ws[i], gw = w - track, am = bottom ? cur - gw / 2 / r : cur + gw / 2 / r;
        float rot = bottom ? am - (float)M_PI / 2 : am + (float)M_PI / 2;
        float c = cosf(rot), sn = sinf(rot);
        float bx = THEME_CX + cosf(am) * r - mid * sn, by = THEME_CY + sinf(am) * r + mid * c; /* up = (sin, -cos) */
        gl[i].gi = glyph_img(font, cps[i]);
        gl[i].bx = bx, gl[i].by = by, gl[i].c = c, gl[i].sn = sn;
        float ext = font->line_height + 2.0f;
        x0 = fminf(x0, bx - ext), y0 = fminf(y0, by - ext), x1 = fmaxf(x1, bx + ext), y1 = fmaxf(y1, by + ext);
        cur = bottom ? cur - w / r : cur + w / r;
    }
    int32_t ix0 = (int32_t)floorf(x0), iy0 = (int32_t)floorf(y0), bw = (int32_t)ceilf(x1) - ix0 + 1, bh = (int32_t)ceilf(y1) - iy0 + 1;
    uint32_t stride = lv_draw_buf_width_to_stride(bw, LV_COLOR_FORMAT_A8), size = stride * bh;
    uint8_t *data = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_SPIRAM);
    if (data == NULL) return NULL;
    memset(data, 0, size);
    for (int i = 0; i < n; i++) {
        const struct glyph_img *gi = gl[i].gi;
        if (gi == NULL || gi->a8 == NULL) continue;
        /* glyph space: u right along the baseline from the box's left, v down from the box's top; the baseline
         * centre is at (adv/2 - ofs_x, h + ofs_y) */
        float pu = gi->adv_w / 2.0f - gi->ofs_x, pv = (float)gi->h + gi->ofs_y, c = gl[i].c, sn = gl[i].sn;
        float ext = font->line_height + 2.0f;
        for (int y = (int)(gl[i].by - ext); y <= (int)(gl[i].by + ext); y++) {
            for (int x = (int)(gl[i].bx - ext); x <= (int)(gl[i].bx + ext); x++) {
                int lx = x - ix0, ly = y - iy0;
                if (lx < 0 || ly < 0 || lx >= bw || ly >= bh) continue;
                float dx = x + 0.5f - gl[i].bx, dy = y + 0.5f - gl[i].by;
                float u = pu + dx * c + dy * sn, v = pv - dx * sn + dy * c; /* inverse rotation */
                float k = glyph_at(gi, u, v);
                if (k <= 0) continue;
                uint8_t *p = data + ly * stride + lx;
                int sum = *p + (int)(k + 0.5f);
                *p = (uint8_t)(sum > 255 ? 255 : sum);
            }
        }
    }
    lv_draw_buf_init(&victim->buf, bw, bh, LV_COLOR_FORMAT_A8, stride, data, size);
    strcpy(victim->text, s);
    victim->font = font, victim->r = r, victim->a = a, victim->track = track;
    victim->x = ix0, victim->y = iy0;
    victim->used = s_arc_stamp;
    victim->ok = true;
    return victim;
}

void theme_arc_text_frame(void) { s_arc_stamp++; }

/* faces-setup.js arcText(): curved text centred on angle a (canvas radians), reading left to right; glyphs sit
 * with their vertical middle on the circle, upright at the top and along the bottom (sin a > 0.25). */
void theme_draw_arc_text(lv_layer_t *layer, const char *s, const lv_font_t *font, float r, float a, uint32_t rgb,
                         lv_opa_t opa, float track)
{
    if (opa <= LV_OPA_MIN) return;
    struct arc_label *l = arc_label(s, font, r, a, track);
    if (l == NULL || !theme_on_layer(layer, l->x, l->y, l->x + (int32_t)l->buf.header.w - 1, l->y + (int32_t)l->buf.header.h - 1))
        return;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &l->buf;
    d.recolor = lv_color_hex(rgb);
    d.opa = opa;
    lv_area_t ar = {l->x, l->y, l->x + (int32_t)l->buf.header.w - 1, l->y + (int32_t)l->buf.header.h - 1};
    lv_draw_image(layer, &d, &ar);
}

/* ------------------------------------------------------------ keys and the level dial */
void theme_draw_key(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, theme_key_t style)
{
    theme_draw_key_opa(layer, x, y, w, h, style, LV_OPA_COVER);
}

void theme_draw_key_opa(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, theme_key_t style, lv_opa_t opa)
{
    if (!theme_on_layer(layer, x, y, x + w - 1, y + h - 1)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = h / 2;
    d.bg_opa = LV_OPA_COVER;
    d.border_width = 1;
    if (style == THEME_KEY_PRIMARY) {
        d.bg_color = lv_color_hex(0xe3c582);
        d.bg_grad.dir = LV_GRAD_DIR_VER;
        d.bg_grad.stops_count = 2;
        d.bg_grad.stops[0] = (lv_grad_stop_t){.color = lv_color_hex(0xe3c582), .opa = LV_OPA_COVER, .frac = 0};
        d.bg_grad.stops[1] = (lv_grad_stop_t){.color = lv_color_hex(0xa07b38), .opa = LV_OPA_COVER, .frac = 255};
        d.border_color = lv_color_hex(0xfff0c8);
        d.border_opa = THEME_OPA(0.5);
    } else {
        d.bg_color = lv_color_hex(style == THEME_KEY_ON ? THEME_SETUP_KEY_ON : THEME_SETUP_KEY);
        d.border_color = lv_color_hex(style == THEME_KEY_ALARM ? THEME_RED : THEME_BRASS);
        d.border_opa = style == THEME_KEY_DARK ? THEME_OPA(0.38) : THEME_OPA(0.85);
    }
    if (opa < LV_OPA_COVER) {   /* the whole key at opa (fill and hairline); LV_OPA_COVER draws as before */
        d.bg_opa = opa;
        d.border_opa = (lv_opa_t)(d.border_opa * opa / 255);
    }
    lv_area_t a = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

int32_t theme_arc_deg(float rad)
{
    int32_t d = (int32_t)lroundf(rad * 180 / (float)M_PI) % 360;
    return d < 0 ? d + 360 : d;
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, float t)
{
    int r = (int)lroundf(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
    int g = (int)lroundf(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
    int bl = (int)lroundf((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
    return (uint32_t)(r << 16 | g << 8 | bl);
}

float theme_level_at(float a)
{
    const float tau = 2 * (float)M_PI;
    float u = fmodf(a - THEME_LEVEL_A0, tau);
    if (u < 0) u += tau;
    if (u > THEME_LEVEL_SWEEP) u = u > THEME_LEVEL_SWEEP + (tau - THEME_LEVEL_SWEEP) / 2 ? 0 : THEME_LEVEL_SWEEP;
    return u / THEME_LEVEL_SWEEP;
}

bool theme_level_hit(float a, float margin)
{
    const float tau = 2 * (float)M_PI;
    float u = fmodf(a - (THEME_LEVEL_A0 - margin), tau);
    if (u < 0) u += tau;
    return u <= THEME_LEVEL_SWEEP + 2 * margin;
}

void theme_draw_level(lv_layer_t *layer, float v, theme_level_glyph_t glyph)
{
    const int32_t cx = THEME_CX, cy = THEME_CY;
    const float a0 = THEME_LEVEL_A0, ae = a0 + THEME_LEVEL_SWEEP * omni_bg_clampf(v, 0, 1), tau = 2 * (float)M_PI;
    /* track: black .6, 10 px, round */
    theme_draw_arc(layer, cx, cy, 201, 10, theme_arc_deg(a0), theme_arc_deg(a0 + THEME_LEVEL_SWEEP), 0x000000, THEME_OPA(0.6), true);
    /* fill: 6 px, linear gradient #6d5024 -> #f0d595 from (cx - 196, cy + 100) to (cx + 196, cy - 100), round start */
    const float gx = 392, gy = -200, g2 = gx * gx + gy * gy;
    for (float a = a0; v >= 0 && a < ae - 0.001f; a += 0.06f) {
        float b = fminf(a + 0.06f, ae), m = (a + b) / 2;
        float px = cosf(m) * 196 + 196, py = sinf(m) * 196 - 100;
        uint32_t c = mix_rgb(0x6d5024, 0xf0d595, omni_bg_clampf((px * gx + py * gy) / g2, 0, 1));
        theme_draw_arc(layer, cx, cy, 199, 6, theme_arc_deg(a), theme_arc_deg(b + 0.01f), c, LV_OPA_COVER, false);
    }
    int32_t sx = cx + (int32_t)lroundf(cosf(a0) * 196), sy = cy + (int32_t)lroundf(sinf(a0) * 196);
    if (v >= 0) theme_draw_box(layer, sx - 3, sy - 3, sx + 2, sy + 2, LV_RADIUS_CIRCLE, 0x6d5024, LV_OPA_COVER, 0, 0);
    for (int i = 0; i <= 20; i++) { /* ticks r 176..181, every 5th to 185 */
        float a = a0 + THEME_LEVEL_SWEEP * i / 20, r1 = i % 5 ? 181 : 185;
        theme_draw_line(layer, cx + (int32_t)lroundf(cosf(a) * 176), cy + (int32_t)lroundf(sinf(a) * 176),
                        cx + (int32_t)lroundf(cosf(a) * r1), cy + (int32_t)lroundf(sinf(a) * r1), i % 5 ? 1 : 2, THEME_BRASS,
                        THEME_OPA(0.45), false);
    }
    if (glyph == THEME_LEVEL_INFO) return;
    const lv_draw_buf_t *knob = theme_knob();
    int32_t kx = cx + (int32_t)lroundf(cosf(ae) * 196), ky = cy + (int32_t)lroundf(sinf(ae) * 196);
    if (knob && theme_on_layer(layer, kx - THEME_KNOB_SIZE / 2, ky - THEME_KNOB_SIZE / 2, kx + THEME_KNOB_SIZE / 2, ky + THEME_KNOB_SIZE / 2)) {
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = knob;
        lv_area_t a = {kx - THEME_KNOB_SIZE / 2, ky - THEME_KNOB_SIZE / 2, kx + THEME_KNOB_SIZE / 2 - 1, ky + THEME_KNOB_SIZE / 2 - 1};
        lv_draw_image(layer, &d, &a);
    }
    theme_draw_box(layer, kx - 2, ky - 2, kx + 2, ky + 2, LV_RADIUS_CIRCLE, THEME_RED, LV_OPA_COVER, 0, 0);
    for (int e = 0; e < 2; e++) { /* end glyphs just past each end */
        float a = e ? a0 + THEME_LEVEL_SWEEP + 0.17f : a0 - 0.17f, s = e ? 7 : 4;
        int32_t x = cx + (int32_t)lroundf(cosf(a) * 196), y = cy + (int32_t)lroundf(sinf(a) * 196);
        if (glyph == THEME_LEVEL_SUN) { /* ring r .55 s, 8 rays .85 s .. 1.25 s */
            theme_draw_arc(layer, x, y, (int32_t)lroundf(s * 0.55f + 0.65f), 1, 0, 360, THEME_BRASS, LV_OPA_COVER, false);
            for (int i = 0; i < 8; i++) {
                float b = i / 8.0f * tau;
                theme_draw_line(layer, x + (int32_t)lroundf(cosf(b) * s * .85f), y + (int32_t)lroundf(sinf(b) * s * .85f),
                                x + (int32_t)lroundf(cosf(b) * s * 1.25f), y + (int32_t)lroundf(sinf(b) * s * 1.25f), 1,
                                THEME_BRASS, LV_OPA_COVER, false);
            }
        } else { /* a speaker with one (quiet) or three (loud) waves */
            theme_draw_box(layer, x - 5, y - 2, x - 2, y + 2, 0, THEME_BRASS, LV_OPA_COVER, 0, 0);
            for (int i = 0; i < (e ? 3 : 1); i++)
                theme_draw_arc(layer, x - 3, y, 4 + i * 3, 1, 315, 45, THEME_BRASS, LV_OPA_COVER, false);
        }
    }
}

void theme_draw_red_index(lv_layer_t *layer, float a)
{
    int32_t x = THEME_CX + (int32_t)lroundf(cosf(a) * 228), y = THEME_CY + (int32_t)lroundf(sinf(a) * 228);
    if (!theme_on_layer(layer, x - 8, y - 8, x + 8, y + 8)) return;
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = lv_color_hex(THEME_RED);
    t.p[0].x = (int32_t)lroundf(THEME_CX + cosf(a) * 224), t.p[0].y = (int32_t)lroundf(THEME_CY + sinf(a) * 224);
    t.p[1].x = (int32_t)lroundf(THEME_CX + cosf(a - 0.03f) * 233), t.p[1].y = (int32_t)lroundf(THEME_CY + sinf(a - 0.03f) * 233);
    t.p[2].x = (int32_t)lroundf(THEME_CX + cosf(a + 0.03f) * 233), t.p[2].y = (int32_t)lroundf(THEME_CY + sinf(a + 0.03f) * 233);
    lv_draw_triangle(layer, &t);
}

void theme_invalidate_level(lv_obj_t *obj, float v_old, float v_new)
{
    float lo = omni_bg_clampf(fminf(v_old, v_new), 0, 1), hi = omni_bg_clampf(fmaxf(v_old, v_new), 0, 1);
    float a0 = THEME_LEVEL_A0 + THEME_LEVEL_SWEEP * lo, a1 = THEME_LEVEL_A0 + THEME_LEVEL_SWEEP * hi;
    lv_area_t a;
    theme_arc_area(THEME_CX, THEME_CY, 201, 10, theme_arc_deg(a0) - 2, theme_arc_deg(a1) + 2, &a); /* the fill between */
    lv_obj_invalidate_area(obj, &a);
    for (int k = 0; k < 2; k++) { /* the knob, old and new */
        float ae = k ? a1 : a0;
        int32_t kx = THEME_CX + (int32_t)lroundf(cosf(ae) * 196), ky = THEME_CY + (int32_t)lroundf(sinf(ae) * 196);
        lv_area_t b = {kx - THEME_KNOB_SIZE / 2 - 1, ky - THEME_KNOB_SIZE / 2 - 1, kx + THEME_KNOB_SIZE / 2 + 1, ky + THEME_KNOB_SIZE / 2 + 1};
        lv_obj_invalidate_area(obj, &b);
    }
}

/* ------------------------------------------------------------ the Setup screens' shared pieces (R223(c)) */
static int32_t sheet_key_x(const theme_sheet_t *s, int k)
{
    return s->key[1] == NULL ? THEME_CX - THEME_SHEET_KEY_W / 2 : k ? 239 : 99;
}

void theme_draw_sheet(lv_layer_t *layer, const theme_sheet_t *s)
{
    for (int i = 0; i < 4; i++) {
        const theme_line_t *l = &s->line[i];
        if (l->s) theme_draw_text(layer, l->s, l->f, l->rgb, LV_OPA_COVER, l->ls, THEME_CENTER, THEME_CX, l->y);
    }
    for (int k = 0; s->key_y && k < 2 && s->key[k]; k++) {
        const int32_t x = sheet_key_x(s, k);
        const bool primary = s->style[k] == THEME_KEY_PRIMARY;
        theme_draw_key(layer, x, s->key_y, THEME_SHEET_KEY_W, THEME_SHEET_KEY_H, (theme_key_t)s->style[k]);
        theme_draw_text(layer, s->key[k], primary ? &font_cormorant_700_20 : &font_plex_sans_500_14,
                        primary ? 0x1f1606 : s->style[k] == THEME_KEY_ALARM ? THEME_RED : THEME_INK, LV_OPA_COVER, 0,
                        THEME_CENTER, x + THEME_SHEET_KEY_W / 2, s->key_y + (primary ? 30 : 28));
    }
    if (s->footer)
        theme_draw_text(layer, "tap outside to close", &font_plex_mono_400_10, THEME_SETUP_FAINT, LV_OPA_COVER, 0,
                        THEME_CENTER, THEME_CX, 330);
}

int theme_sheet_key(const theme_sheet_t *s, int32_t x, int32_t y)
{
    for (int k = 0; s->key_y && k < 2 && s->key[k]; k++) {
        const int32_t kx = sheet_key_x(s, k);
        if (x >= kx && x <= kx + THEME_SHEET_KEY_W && y >= s->key_y && y <= s->key_y + THEME_SHEET_KEY_H) return k;
    }
    return -1;
}

const char *theme_ellipsis(const lv_font_t *f)
{
    return f == &font_plex_sans_400_14 || f == &font_plex_sans_400_13 ? "..." : "\xe2\x80\xa6";
}

/* out: the first m bytes of b, display-safe; tail (NULL: none) after the trailing spaces */
static void name_cut(char *out, const uint8_t *b, int m, const char *tail)
{
    int k = 0;
    for (int i = 0; i < m; i++) out[k++] = b[i] < 0x20 || b[i] == 0x7f ? '?' : (char)b[i];
    if (tail) {
        while (k > 0 && out[k - 1] == ' ') k--;
        strcpy(out + k, tail);
    } else {
        out[k] = 0;
    }
}

static int utf8_floor(const uint8_t *b, int m)
{
    while (m > 0 && (b[m] & 0xc0) == 0x80) m--;
    return m;
}

void theme_fit_name(char out[THEME_NAME_BUF], const uint8_t *b, int len, const lv_font_t *f, int32_t max)
{
    len = len > 32 ? 32 : len < 0 ? 0 : len;   /* a name is at most 32 bytes (the record's field) */
    name_cut(out, b, len, NULL);
    if (len == 0 || theme_text_width(out, f, 0) <= max) return;
    int lo = 0, hi = len - 1;
    while (lo < hi) {
        const int m = (lo + hi + 1) / 2;
        name_cut(out, b, utf8_floor(b, m), theme_ellipsis(f));
        if (theme_text_width(out, f, 0) <= max) lo = m;
        else hi = m - 1;
    }
    name_cut(out, b, utf8_floor(b, lo), theme_ellipsis(f));
}

void theme_draw_rect(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius, uint32_t fill,
                     lv_opa_t fill_opa, int32_t border, uint32_t border_rgb, lv_opa_t border_opa)
{
    if (!theme_on_layer(layer, x1, y1, x2, y2)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_color = lv_color_hex(fill);
    d.bg_opa = fill_opa;
    d.border_width = border;
    d.border_color = lv_color_hex(border_rgb);
    d.border_opa = border_opa;
    const lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

void theme_draw_grad(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius, bool vertical,
                     uint32_t c0, lv_opa_t o0, uint32_t c1, lv_opa_t o1, lv_opa_t opa)
{
    if (!theme_on_layer(layer, x1, y1, x2, y2)) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_opa = opa;
    d.bg_grad.dir = vertical ? LV_GRAD_DIR_VER : LV_GRAD_DIR_HOR;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0] = (lv_grad_stop_t){.color = lv_color_hex(c0), .opa = o0, .frac = 0};
    d.bg_grad.stops[1] = (lv_grad_stop_t){.color = lv_color_hex(c1), .opa = o1, .frac = 255};
    const lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

