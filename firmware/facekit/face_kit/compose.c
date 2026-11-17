#include "fk/compose.h"
#include <math.h>
#include <string.h>
#include "omni/blend565.h"

/* The compositor (compose.h): images, glow sprites and glyph atlases. Integer per pixel; every pixel's value is a
 * function of its screen position and the inputs only, so any band split gives the same frame. */

#define LV_MAGIC 0x19
#define LV_FLAG_PREMULTIPLIED 0x0001
#define LV_FLAG_COMPRESSED 0x0008

static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

bool fk_img_from_bin(fk_img_t *img, const void *bin, size_t size)
{
    const uint8_t *p = bin;
    if (!img || !p || size < 12 || p[0] != LV_MAGIC || ((uintptr_t)p & 1)) return false;
    const uint8_t cf = p[1];
    const unsigned flags = rd16(p + 2), w = rd16(p + 4), h = rd16(p + 6), stride = rd16(p + 8);
    unsigned bpp;   /* bytes per pixel of the first plane */
    if (cf == FK_CF_RGB565 || cf == FK_CF_RGB565A8) bpp = 2;
    else if (cf == FK_CF_A8) bpp = 1;
    else return false;
    if (w == 0 || h == 0 || stride != w * bpp || (flags & (LV_FLAG_PREMULTIPLIED | LV_FLAG_COMPRESSED))) return false;
    const size_t px = (size_t)w * h, need = 12 + px * bpp + (cf == FK_CF_RGB565A8 ? px : 0);
    if (size < need) return false;
    img->w = (uint16_t)w;
    img->h = (uint16_t)h;
    img->cf = cf;
    img->rgb = cf == FK_CF_A8 ? NULL : (const uint16_t *)(const void *)(p + 12);
    img->a = cf == FK_CF_A8 ? p + 12 : cf == FK_CF_RGB565A8 ? p + 12 + px * 2 : NULL;
    return true;
}

/* The part of the screen rect (x, y, w, h) inside clip, as [*x0, *x1) x [*y0, *y1); false if empty. */
static bool cut(omni_rect_t clip, int x, int y, int w, int h, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = x > clip.x0 ? x : clip.x0;
    *y0 = y > clip.y0 ? y : clip.y0;
    *x1 = x + w < clip.x1 ? x + w : clip.x1;
    *y1 = y + h < clip.y1 ? y + h : clip.y1;
    return *x0 < *x1 && *y0 < *y1;
}

/* The source box (sx, sy, w, h) of img (coloured, inside it) with its top-left at screen (x, y). */
static void blit_box(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *img, int sx, int sy, int w, int h,
                     int x, int y, uint8_t opa)
{
    int x0, y0, x1, y1;
    if (!img->rgb || opa == 0 || !cut(clip, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int Y = y0; Y < y1; Y++) {
        const size_t s = (size_t)(sy + Y - y) * img->w + (size_t)(sx + x0 - x);
        const uint16_t *src = img->rgb + s;
        const uint8_t *a = img->a ? img->a + s : NULL;
        uint16_t *d = fb + (Y - clip.y0) * stride + (x0 - clip.x0);
        const int n = x1 - x0;
        if (!a && opa == 255) {
            memcpy(d, src, (size_t)n * 2);
            continue;
        }
        for (int i = 0; i < n; i++) {
            const unsigned ai = (a ? a[i] : 255u) * opa / 255u;   /* opa 255: a exactly */
            if (ai == 255) d[i] = src[i];   /* blend565.h: 255 is the source, 0 the destination, exactly */
            else if (ai) d[i] = omni_blend565(d[i], src[i], (int)ai);
        }
    }
}

void fk_blit(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *img, int x, int y, uint8_t opa)
{
    if (!fb || !img) return;
    blit_box(fb, stride, clip, img, 0, 0, img->w, img->h, x, y, opa);
}

static inline uint16_t add565(uint16_t d, int r, int g, int b)   /* 'lighter', doors.c's */
{
    const int R = omni_ex_r(d) + r, G = omni_ex_g(d) + g, B = omni_ex_b(d) + b;
    return omni_pack565(R < 255 ? R : 255, G < 255 ? G : 255, B < 255 ? B : 255);
}

void fk_add_a8(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *a8, int x, int y, int w, int h,
               uint16_t rgb, uint8_t gain)
{
    int x0, y0, x1, y1;
    if (!fb || !a8 || !a8->a || a8->w == 0 || a8->h == 0 || gain == 0 || w <= 0 || h <= 0 || w > 32767 || h > 32767 || !cut(clip, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    const int R = omni_ex_r(rgb), G = omni_ex_g(rgb), B = omni_ex_b(rgb), gp = omni_amul(gain);
    if (w == a8->w && h == a8->h) {   /* 1:1 (the sampling below then reads (X - x, Y - y)): a zero alpha adds nothing,
                                       * so it is passed over first (the crystal's lances are 89 % zero; Task 12) */
        for (int Y = y0; Y < y1; Y++) {
            const uint8_t *row = a8->a + (size_t)(Y - y) * a8->w + (size_t)(x0 - x);
            uint16_t *d = fb + (Y - clip.y0) * stride + (x0 - clip.x0);
            for (int k = 0; k < x1 - x0; k++) {
                if (!row[k]) continue;
                const int ap = omni_amul(row[k]) * gp >> 8;
                if (ap) d[k] = add565(d[k], (R * ap) >> 8, (G * ap) >> 8, (B * ap) >> 8);
            }
        }
        return;
    }
    /* Nearest at pixel centres: source column floor((2 d + 1) sw / (2 w)) at d = X - x, exactly, by a DDA from x0
     * (the first quotient in 64 bits, then whole steps q and remainder steps r of 2 sw / 2 w): no divide per pixel. */
    const int64_t sw = a8->w, sh = a8->h, den = 2 * (int64_t)w;
    const int64_t num0 = (2 * (int64_t)(x0 - x) + 1) * sw;
    const int32_t i0 = (int32_t)(num0 / den), r0 = (int32_t)(num0 % den);
    const int32_t q = (int32_t)(2 * sw / den), r = (int32_t)(2 * sw % den), D = (int32_t)den;
    for (int Y = y0; Y < y1; Y++) {
        const uint8_t *row = a8->a + (size_t)((2 * (int64_t)(Y - y) + 1) * sh / (2 * (int64_t)h)) * (size_t)sw;
        uint16_t *d = fb + (Y - clip.y0) * stride;   /* the clip's row; column X at d[X - clip.x0] */
        int32_t i = i0, rem = r0;
        for (int X = x0; X < x1; X++) {
            const int ap = omni_amul(row[i]) * gp >> 8;
            uint16_t *px = &d[X - clip.x0];
            if (ap) *px = add565(*px, (R * ap) >> 8, (G * ap) >> 8, (B * ap) >> 8);
            i += q;
            rem += r;
            if (rem >= D) {
                rem -= D;
                i++;
            }
        }
    }
}

void fk_add_a8_2(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *a8, int x, int y, int w, int h,
                 uint16_t rgb, uint8_t gain1, uint8_t gain2)
{
    int x0, y0, x1, y1;
    if (!fb || !a8 || !a8->a || w != a8->w || h != a8->h || gain1 == 0 || gain2 == 0 ||
        !cut(clip, x, y, w, h, &x0, &y0, &x1, &y1)) {   /* not the 1:1 case (or a pass draws nothing): one by one */
        fk_add_a8(fb, stride, clip, a8, x, y, w, h, rgb, gain1);
        fk_add_a8(fb, stride, clip, a8, x, y, w, h, rgb, gain2);
        return;
    }
    const int R = omni_ex_r(rgb), G = omni_ex_g(rgb), B = omni_ex_b(rgb), g1 = omni_amul(gain1), g2 = omni_amul(gain2);
    for (int Y = y0; Y < y1; Y++) {
        const uint8_t *row = a8->a + (size_t)(Y - y) * a8->w + (size_t)(x0 - x);
        uint16_t *d = fb + (Y - clip.y0) * stride + (x0 - clip.x0);
        for (int k = 0; k < x1 - x0; k++) {
            if (!row[k]) continue;
            const int am = omni_amul(row[k]), a1 = am * g1 >> 8, a2 = am * g2 >> 8;
            uint16_t p = d[k];
            if (a1) p = add565(p, (R * a1) >> 8, (G * a1) >> 8, (B * a1) >> 8);
            if (a2) p = add565(p, (R * a2) >> 8, (G * a2) >> 8, (B * a2) >> 8);
            d[k] = p;
        }
    }
}

/* cells[i] for character ch, or NULL. */
static const fk_cell_t *glyph(const fk_atlas_t *at, char ch)
{
    if (ch == '\0' || !at->glyphs || !at->cells) return NULL;
    const char *p = strchr(at->glyphs, ch);
    return p ? &at->cells[p - at->glyphs] : NULL;
}

int fk_atlas_width(const fk_atlas_t *at, const char *text, int fixed_cell)
{
    if (!at || !text) return 0;
    int w = 0;
    for (const char *s = text; *s; s++) {
        if (fixed_cell > 0) w += fixed_cell;
        else {
            const fk_cell_t *c = glyph(at, *s);
            if (c) w += c->adv;
        }
    }
    return w;
}

static int floor_div2(int v) { return v >= 0 ? v / 2 : -((1 - v) / 2); }

#ifdef FK_TEST_HOOKS
fk_test_glyph_fn fk_test_glyph_hook;   /* host test builds only (compose.h) */
#endif

void fk_atlas_text(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *text,
                   int x, int baseline, int fixed_cell, uint8_t opa)
{
    if (!fb || !at || !at->img || !text) return;
    int pen = x;
    for (const char *s = text; *s; s++) {
        const fk_cell_t *c = glyph(at, *s);
        if (c) {
            const int left = fixed_cell > 0 ? pen + floor_div2(fixed_cell - c->adv) : pen;
            /* the box, cut to the atlas so a bad cell never reads outside it */
            int sx = c->x, sy = c->y, w = c->w, h = c->h, dx = left + c->ox, dy = baseline + c->oy;
            if (sx < 0) { w += sx; dx -= sx; sx = 0; }
            if (sy < 0) { h += sy; dy -= sy; sy = 0; }
            if (sx + w > at->img->w) w = at->img->w - sx;
            if (sy + h > at->img->h) h = at->img->h - sy;
            if (w > 0 && h > 0) {
#ifdef FK_TEST_HOOKS
                if (fk_test_glyph_hook) fk_test_glyph_hook(at, dx, dy, dx + w, dy + h);
#endif
                blit_box(fb, stride, clip, at->img, sx, sy, w, h, dx, dy, opa);
            }
        }
        pen += fixed_cell > 0 ? fixed_cell : c ? c->adv : 0;
    }
}

/* ---- Task 13: an image's alpha in a given colour (the tinted atlases, #5's lamp) */

/* blit_box with img's alpha plane alone in rgb, the alpha stretched from (lo, 255] onto (0, 255] */
static void blit_box_rgb(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *img, int sx, int sy, int w, int h,
                         int x, int y, uint16_t rgb, uint8_t opa, uint8_t lo)
{
    int x0, y0, x1, y1;
    if (!img->a || opa == 0 || lo == 255 || !cut(clip, x, y, w, h, &x0, &y0, &x1, &y1)) return;
    for (int Y = y0; Y < y1; Y++) {
        const uint8_t *a = img->a + (size_t)(sy + Y - y) * img->w + (size_t)(sx + x0 - x);
        uint16_t *d = fb + (Y - clip.y0) * stride + (x0 - clip.x0);
        for (int i = 0; i < x1 - x0; i++) {
            if (a[i] <= lo) continue;
            const unsigned al = lo ? (a[i] - lo) * 255u / (255u - lo) : a[i];
            const unsigned ai = al * opa / 255u;
            if (ai == 255) d[i] = rgb;
            else if (ai) d[i] = omni_blend565(d[i], rgb, (int)ai);
        }
    }
}

void fk_blit_rgb(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *img, int x, int y, uint16_t rgb,
                 uint8_t opa)
{
    if (!fb || !img) return;
    blit_box_rgb(fb, stride, clip, img, 0, 0, img->w, img->h, x, y, rgb, opa, 0);
}

/* one glyph's box at pen x (fk_atlas_text's cut to the atlas), in its own colour or tinted */
static void glyph_at(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const fk_cell_t *c, int left,
                     int baseline, bool tint, uint16_t rgb, uint8_t opa, uint8_t lo)
{
    int sx = c->x, sy = c->y, w = c->w, h = c->h, dx = left + c->ox, dy = baseline + c->oy;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > at->img->w) w = at->img->w - sx;
    if (sy + h > at->img->h) h = at->img->h - sy;
    if (w <= 0 || h <= 0) return;
#ifdef FK_TEST_HOOKS
    if (fk_test_glyph_hook) fk_test_glyph_hook(at, dx, dy, dx + w, dy + h);
#endif
    if (tint) blit_box_rgb(fb, stride, clip, at->img, sx, sy, w, h, dx, dy, rgb, opa, lo);
    else blit_box(fb, stride, clip, at->img, sx, sy, w, h, dx, dy, opa);
}

void fk_atlas_text_rgb(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *text, int x,
                       int baseline, int fixed_cell, uint16_t rgb, uint8_t opa, uint8_t lo)
{
    if (!fb || !at || !at->img || !text) return;
    int pen = x;
    for (const char *s = text; *s; s++) {
        const fk_cell_t *c = glyph(at, *s);
        if (c) glyph_at(fb, stride, clip, at, c, fixed_cell > 0 ? pen + floor_div2(fixed_cell - c->adv) : pen, baseline,
                        true, rgb, opa, lo);
        pen += fixed_cell > 0 ? fixed_cell : c ? c->adv : 0;
    }
}

float fk_atlas_width_tracked(const fk_atlas_t *at, const char *text, float track)
{
    if (!at || !text || !*text) return 0;
    float w = 0;
    for (const char *s = text; *s; s++) {
        const fk_cell_t *c = glyph(at, *s);
        w += (float)(c ? c->adv : 0) + (s[1] ? track : 0);
    }
    return w;
}

void fk_atlas_text_tracked(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *text,
                           float cx, int baseline, float track, bool tint, uint16_t rgb, uint8_t opa)
{
    if (!fb || !at || !at->img || !text) return;
    float pen = cx - fk_atlas_width_tracked(at, text, track) / 2;
    for (const char *s = text; *s; s++) {
        const fk_cell_t *c = glyph(at, *s);
        if (c) {
            glyph_at(fb, stride, clip, at, c, (int)floorf(pen + 0.5f), baseline, tint, rgb, opa, 0);
            pen += (float)c->adv;
        }
        pen += track;
    }
}
