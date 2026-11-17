/* The brass backgrounds (omni/brassbg.h), moved verbatim from main/ui/theme.c (R112). The only textual changes (B3):
 * bg_yield calls the caller's yield callback and s_rng is file-static, (float)M_PI is BG_PI (the same float), THEME_W/CX/CY
 * are OMNI_BG_W/OMNI_BG_C, the strip loop takes `rows`, and flatten writes a plain RGB565 square. Built on the device
 * with -ffp-contract=fast like theme.c was (R121/B1, components/omni_core/CMakeLists.txt): not one of the
 * contraction-off files, on purpose. Not reentrant (s_rng, the yield callback): the caller serialises renders. */
#include "omni/brassbg.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define BG_PI 3.14159265358979323846

static void (*s_yield)(int y, void *ctx);
static void *s_yield_ctx;
static void bg_yield(int y)
{
    if (s_yield) s_yield(y, s_yield_ctx);
}

typedef omni_bg_stop_t stop_t;

/* Canvas gradients interpolate premultiplied colour: out = premultiplied rgb + alpha. */
static void grad(const stop_t *s, int n, float t, float out[4])
{
    int i = 0;
    while (i < n - 1 && t > s[i + 1].t) i++;
    float u = 0;
    if (t <= s[0].t) i = 0, u = 0;
    else if (i == n - 1) u = 0;
    else u = (t - s[i].t) / (s[i + 1].t - s[i].t);
    const stop_t *a = &s[i], *b = &s[i < n - 1 ? i + 1 : i];
    float aa = a->a + (b->a - a->a) * u;
    out[0] = a->r * a->a + (b->r * b->a - a->r * a->a) * u;
    out[1] = a->g * a->a + (b->g * b->a - a->g * a->a) * u;
    out[2] = a->b * a->a + (b->b * b->a - a->b * a->a) * u;
    out[3] = aa;
}

/* Composite a premultiplied source over a straight-alpha pixel (c, *a). */
static void over(float c[3], float *a, const float s[4])
{
    float oa = s[3] + *a * (1 - s[3]);
    if (oa <= 0) return;
    for (int k = 0; k < 3; k++) c[k] = (s[k] + c[k] * *a * (1 - s[3])) / oa;
    *a = oa;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t s_rng;
static float mulberry(void) /* faces-setup.js mulberry(), same sequence */
{
    s_rng += 0x6D2B79F5u;
    uint32_t t = (s_rng ^ (s_rng >> 15)) * (1u | s_rng);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) / 4294967296.0);
}

static void px_over(uint8_t *p, const float s[4])
{
    float c[3] = {p[2], p[1], p[0]}, a = p[3] / 255.0f;
    over(c, &a, s);
    p[0] = (uint8_t)clampf(c[2] + 0.5f, 0, 255);
    p[1] = (uint8_t)clampf(c[1] + 0.5f, 0, 255);
    p[2] = (uint8_t)clampf(c[0] + 0.5f, 0, 255);
    p[3] = (uint8_t)clampf(a * 255 + 0.5f, 0, 255);
}

/* Composite an rgba (straight, 0..255 colour, alpha 0..1) scaled by coverage k over a pixel. */
static void px_rgba(uint8_t *p, float r, float g, float b, float a, float k)
{
    float al = a * k;
    if (al > 0) px_over(p, (float[4]){r * al, g * al, b * al, al});
}

static float ring_cov(float d, float r, float w) /* a stroked circle of width w through radius r */
{
    float half = w / 2;
    return clampf(fminf(half + 0.5f - fabsf(d - r), 1), 0, 1) * (w < 1 ? w : 1);
}

/* The ARGB8888 working rows of a background: rows [y0, y1) of the OMNI_BG_W square (R110). Every pass below is per
 * pixel, with no pixel reading another, and runs in the same order for every pixel, so rendering the square strip by
 * strip gives the bytes a whole-square render gives (the speckle's seeded sequence is run whole for each strip, its
 * rectangles clipped to the strip's rows). The strip is OMNI_BG_STRIP rows, not the square: the square's 868 KB on top of
 * the cached backgrounds was the PSRAM low at boot. */
typedef struct { uint8_t *data; uint32_t stride; int y0, y1; } strip_t;
static inline uint8_t *srow(const strip_t *s, int y) { return s->data + (size_t)(y - s->y0) * s->stride; }

/* faces-setup.js brassBand(g, r0, r1, {seed, dark, speck}): linear base + conic brushed sheen + radial bevel
 * (clipped to the annulus), the seeded tarnish speckle, the optional dark wash, then the two inner edge
 * lines - in the mockup's order, composited over what buf already holds (its rows of the OMNI_BG_W square). */
typedef struct { float r0, r1, dark; uint32_t seed; int speck; } band_t;

static float annulus_cov(float d, float r0, float r1) { return clampf(r1 - d + 0.5f, 0, 1) * clampf(d - r0 + 0.5f, 0, 1); }

static void brass_band(const strip_t *buf, const band_t *b)
{
    static const stop_t LIN[] = {{0, 0xdc, 0xc0, 0x7e, 1}, {.32f, 0xa7, 0x83, 0x3f, 1}, {.62f, 0x7a, 0x5c, 0x2a, 1}, {1, 0x46, 0x32, 0x1a, 1}};
    static const float CON[][2] = {{0, .18f}, {.07f, -.14f}, {.17f, .12f}, {.29f, -.16f}, {.4f, .1f}, {.53f, -.12f},
                                   {.66f, .16f}, {.79f, -.14f}, {.9f, .07f}, {1, .18f}};
    static const stop_t RAD[] = {{0, 0, 0, 0, .6f}, {.12f, 255, 238, 200, .32f}, {.3f, 0, 0, 0, 0},
                                 {.75f, 0, 0, 0, .06f}, {.9f, 255, 235, 190, .22f}, {1, 0, 0, 0, .65f}};
    stop_t con[10];
    for (int i = 0; i < 10; i++) {
        float a = CON[i][1];
        con[i] = a > 0 ? (stop_t){CON[i][0], 255, 236, 190, a} : (stop_t){CON[i][0], 25, 12, 0, -a};
    }
    const float W = OMNI_BG_W, cx = OMNI_BG_C, cy = OMNI_BG_C, tau = 2 * (float)BG_PI, r0 = b->r0, r1 = b->r1;
    const float lx0 = cx - r1 * 0.6f, ly0 = cy - r1, lvx = 1.2f * r1, lvy = 2 * r1;
    const float lv2 = lvx * lvx + lvy * lvy;
    for (int y = buf->y0; y < buf->y1; y++) {
        bg_yield(y);
        uint8_t *row = srow(buf, y);
        for (int x = 0; x < (int)W; x++) {
            float px = x + 0.5f, py = y + 0.5f, dx = px - cx, dy = py - cy, d = sqrtf(dx * dx + dy * dy);
            float cov = annulus_cov(d, r0, r1);
            if (cov <= 0) continue;
            float c[3], a = 1, s[4];
            grad(LIN, 4, clampf(((px - lx0) * lvx + (py - ly0) * lvy) / lv2, 0, 1), s);
            c[0] = s[0], c[1] = s[1], c[2] = s[2];
            float ang = atan2f(dy, dx) + 0.7f;
            ang -= tau * floorf(ang / tau);
            grad(con, 10, ang / tau, s);
            over(c, &a, s);
            grad(RAD, 6, (d - r0) / (r1 - r0), s);
            over(c, &a, s);
            px_rgba(row + x * 4, c[0], c[1], c[2], 1, cov);
        }
    }
    /* tarnish speckle: the mockup's seeded rectangles, rasterised with area coverage inside the band */
    s_rng = b->seed;
    int n = b->speck >= 0 ? b->speck : (int)lroundf((r1 * r1 - r0 * r0) / 40);
    for (int i = 0; i < n; i++) {
        float a = mulberry() * tau, r = r0 + mulberry() * (r1 - r0);
        bool dark = mulberry() < 0.72f;
        float al = dark ? 0.05f + mulberry() * 0.14f : 0.03f + mulberry() * 0.08f;
        float x0 = cx + cosf(a) * r, y0 = cy + sinf(a) * r, w = 0.8f + mulberry() * 1.6f, h = 0.8f + mulberry() * 1.6f;
        for (int y = (int)floorf(y0); y < (int)ceilf(y0 + h); y++) {
            for (int x = (int)floorf(x0); x < (int)ceilf(x0 + w); x++) {
                if (x < 0 || y < buf->y0 || x >= (int)W || y >= buf->y1) continue;
                float ox = fminf(x + 1, x0 + w) - fmaxf(x, x0), oy = fminf(y + 1, y0 + h) - fmaxf(y, y0);
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                float k = ox * oy * annulus_cov(sqrtf(dx * dx + dy * dy), r0, r1);
                if (k <= 0) continue;
                uint8_t *p = srow(buf, y) + x * 4;
                if (dark) px_rgba(p, 30, 16, 2, al, k);
                else px_rgba(p, 255, 240, 200, al, k);
            }
        }
    }
    /* the dark wash (inside the band), then the edge lines at r0 + 0.5 and r0 + 1.5 (1 px) */
    for (int y = buf->y0; y < buf->y1; y++) {
        bg_yield(y);
        uint8_t *row = srow(buf, y);
        for (int x = 0; x < (int)W; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            if (d < r0 - 2 || d > r1 + 1) continue;
            uint8_t *p = row + x * 4;
            if (b->dark > 0) px_rgba(p, 10, 6, 2, b->dark, annulus_cov(d, r0, r1));
            px_rgba(p, 0, 0, 0, .8f, ring_cov(d, r0 + 0.5f, 1));
            px_rgba(p, 255, 232, 180, .25f, ring_cov(d, r0 + 1.5f, 1));
        }
    }
}

/* faces-setup.js thinBezel(): brassBand(222, 233, seed 3), the chapter ring and its 60 engraved ticks. */
#define BZ_RING 214.5f

static void bezel_render(const strip_t *buf)
{
    const float cx = OMNI_BG_C, cy = OMNI_BG_C, tau = 2 * (float)BG_PI;
    brass_band(buf, &(band_t){222, 233, 0, 3, -1});
    for (int y = buf->y0; y < buf->y1; y++) {
        bg_yield(y);
        uint8_t *row = srow(buf, y);
        for (int x = 0; x < OMNI_BG_W; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            if (d < BZ_RING - 7.5f || d > BZ_RING + 1) continue;
            uint8_t *p = row + x * 4;
            px_rgba(p, 0xc9, 0xa3, 0x5b, .22f, ring_cov(d, BZ_RING, 1));
            float ang = atan2f(dy, dx);
            int i = (int)lroundf(ang / tau * 60);
            float ta = (float)i / 60 * tau, along = dx * cosf(ta) + dy * sinf(ta), perp = -dx * sinf(ta) + dy * cosf(ta);
            int ii = ((i % 60) + 60) % 60;
            float l = ii % 5 ? 3 : 6, al = ii % 5 ? .18f : .4f;
            float k = clampf(1 - fabsf(perp), 0, 1) * clampf(along - (BZ_RING - l) + 0.5f, 0, 1) * clampf(BZ_RING - along + 0.5f, 0, 1);
            px_rgba(p, 0xc9, 0xa3, 0x5b, al, k);
        }
    }
}

/* ------------------------------------------------------------ opaque brass backgrounds (picker, settings) */
static uint16_t rgb565(float r, float g, float b)
{
    int R = (int)clampf(r + 0.5f, 0, 255), G = (int)clampf(g + 0.5f, 0, 255), B = (int)clampf(b + 0.5f, 0, 255);
    return (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
}

static void flatten(const strip_t *argb, uint16_t *out) /* the strip's ARGB over true black -> RGB565 */
{
    for (int y = argb->y0; y < argb->y1; y++) {
        bg_yield(y);
        const uint8_t *s = srow(argb, y);
        uint16_t *d = out + (size_t)y * OMNI_BG_W;
        for (int x = 0; x < OMNI_BG_W; x++, s += 4) {
            float a = s[3] / 255.0f;
            d[x] = rgb565(s[2] * a, s[1] * a, s[0] * a);
        }
    }
}

static void picker_bg(const strip_t *buf)
{
    /* navBezel(): brassBand(146, 233, seed 21, dark .35); lineWidth 1.2 rings at 222 (black .6) and 220.8 (light .18) */
    brass_band(buf, &(band_t){146, 233, .35f, 21, -1});
    for (int y = buf->y0; y < buf->y1; y++) {
        bg_yield(y);
        uint8_t *row = srow(buf, y);
        for (int x = 0; x < OMNI_BG_W; x++) {
            float dx = x + 0.5f - OMNI_BG_C, dy = y + 0.5f - OMNI_BG_C, d = sqrtf(dx * dx + dy * dy);
            uint8_t *p = row + x * 4;
            if (d > 218 && d < 225) {
                px_rgba(p, 0, 0, 0, .6f, ring_cov(d, 222, 1.2f));
                px_rgba(p, 255, 232, 180, .18f, ring_cov(d, 220.8f, 1.2f));
            }
            if (d < 146) { /* the centre: radial #000 (r 60) -> #0c0a07 (r 146), filled to r 145 */
                float t = clampf((d - 60) / 86, 0, 1);
                px_rgba(p, 0x0c * t, 0x0a * t, 0x07 * t, 1, clampf(145 - d + 0.5f, 0, 1));
            }
        }
    }
}

static void settings_bg(const strip_t *buf)
{
    /* settingsBezel(): brassBand(222, 233, seed 9) then brassBand(170, 222, seed 13, dark .72, speck 1200) */
    brass_band(buf, &(band_t){222, 233, 0, 9, -1});
    brass_band(buf, &(band_t){170, 222, .72f, 13, 1200});
}

void omni_bg_render_rows(omni_bg_t which, uint16_t *out, uint8_t *strip, int rows,
                         void (*yield)(int y, void *ctx), void *ctx)
{
    if (rows <= 0) return;                    /* y0 += rows would never end */
    if (rows > OMNI_BG_W) rows = OMNI_BG_W;   /* a strip is at most the square: never write past OMNI_BG_W rows */
    const uint32_t stride = OMNI_BG_W * 4;
    s_yield = yield;
    s_yield_ctx = ctx;
    for (int y0 = 0; y0 < OMNI_BG_W; y0 += rows) {
        const strip_t st = { strip, stride, y0, y0 + rows < OMNI_BG_W ? y0 + rows : OMNI_BG_W };
        memset(strip, 0, (size_t)stride * (size_t)(st.y1 - st.y0));
        if (which == OMNI_BG_BEZEL) bezel_render(&st);
        else if (which == OMNI_BG_PICKER) picker_bg(&st);
        else settings_bg(&st);
        flatten(&st, out);
    }
    s_yield = NULL;
    s_yield_ctx = NULL;
}

void omni_bg_render(omni_bg_t which, uint16_t *out, uint8_t *strip, void (*yield)(int y, void *ctx), void *ctx)
{
    omni_bg_render_rows(which, out, strip, OMNI_BG_STRIP, yield, ctx);
}

/* The shared helpers (omni/brassbg.h): exported wrappers, so the statics above, and the code the renderer is built
 * from, stay exactly what made the captured bytes. */
void omni_bg_grad(const omni_bg_stop_t *s, int n, float t, float out[4]) { grad(s, n, t, out); }
float omni_bg_clampf(float v, float lo, float hi) { return clampf(v, lo, hi); }
void omni_bg_px_rgba(uint8_t *p, float r, float g, float b, float a, float k) { px_rgba(p, r, g, b, a, k); }
