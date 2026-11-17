#pragma once
/* What the circle script trio's sources share (gface.c, gkey.c, glearn.c; dw wave 1 Task 15): faces-modern.js's G
 * palette, asset binding and the small fills. Private to dw_core. Pure: no allocation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/compose.h"
#include "dw/faces.h"

#define G_W 466
#define G_C 233.f
#define G_PI 3.14159265358979f
#define G_RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
/* faces-modern.js G: ink #F3D594, inkHi #FFF0C8, deco rgba(214,170,96,.34), decoHi rgba(232,196,128,.55), glance
 * #6E5526, hint rgba(243,213,148,.75). Numerals dim by colour, never by alpha below 255 (owner ruling). */
#define G_INK_R 0xf3
#define G_INK_G 0xd5
#define G_INK_B 0x94
#define G_INK G_RGB(G_INK_R, G_INK_G, G_INK_B)
#define G_INK_HI G_RGB(0xff, 0xf0, 0xc8)
#define G_DECO G_RGB(214, 170, 96)
#define G_DECO_A 87                 /* .34 */
#define G_GLANCE G_RGB(0x6e, 0x55, 0x26)

static inline float g_clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float g_ease_out3(float x)
{
    const float u = 1 - g_clampf(x, 0, 1);
    return 1 - u * u * u;
}

/* G.ink (or any 8-bit colour) at level l (0..255) over black, as RGB565 */
static inline uint16_t g_level(int r, int g, int b, int l)
{
    return G_RGB(r * l / 255, g * l / 255, b * l / 255);
}

static inline bool g_img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static inline void g_fill(uint16_t *fb, int stride, omni_rect_t clip, uint16_t v)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = v;
    }
}

static inline bool g_meets(omni_rect_t clip, omni_rect_t r)
{
    return r.x0 < clip.x1 && clip.x0 < r.x1 && r.y0 < clip.y1 && clip.y0 < r.y1;
}

static inline uint8_t g_full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, G_W, G_W};
    return 1;
}

/* the 2 x 2 reading layout's centres, faces-modern.js quadPos(cx, cy, r): r 1.42 out each way */
static inline void g_quad(float cx, float cy, float r, int i, float *x, float *y)
{
    const float d = r * 1.42f;
    *x = cx + (i & 1 ? d : -d), *y = cy + (i & 2 ? d : -d);
}
