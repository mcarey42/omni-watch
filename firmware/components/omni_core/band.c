#include "omni/band.h"
#include <stddef.h>
#include "omni/blend565.h"

omni_rect_t omni_band_even(omni_rect_t r, int w, int h)
{
    int x0 = r.x0 & ~1, y0 = r.y0 & ~1, x1 = (r.x1 + 1) & ~1, y1 = (r.y1 + 1) & ~1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    return (omni_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 };
}

int omni_band_chunks(const omni_rects_t *L, int rows, int w, int h, omni_rect_t *out, int max, bool *fallback)
{
    if (fallback) *fallback = false;
    if (rows <= 0 || (rows & 1)) return -1;
    omni_rects_t ev = { .n = 0 }, dj;
    for (int i = 0; i < L->n && ev.n < OMNI_RECTS_MAX; i++) {
        if (omni_rect_area(&L->r[i]) == 0) continue;
        omni_rect_t e = omni_band_even(L->r[i], w, h);
        if (omni_rect_area(&e)) ev.r[ev.n++] = e;
    }
    omni_rects_disjoint(&ev, &dj);
    if (fallback && ev.n > 1 && dj.n == 1) {
        omni_rect_t u = { 0, 0, 0, 0 };
        for (int i = 0; i < ev.n; i++) u = omni_rect_unite(u, ev.r[i]);
        if (dj.r[0].x0 == u.x0 && dj.r[0].y0 == u.y0 && dj.r[0].x1 == u.x1 && dj.r[0].y1 == u.y1) *fallback = true;
    }
    int n = 0;
    for (int i = 0; i < dj.n; i++)
        for (int y = dj.r[i].y0; y < dj.r[i].y1; y += rows) {
            if (n >= max) return -1;
            int y1 = y + rows < dj.r[i].y1 ? y + rows : dj.r[i].y1;
            out[n++] = (omni_rect_t){ dj.r[i].x0, (int16_t)y, dj.r[i].x1, (int16_t)y1 };
        }
    return n;
}

void omni_band_pack(uint16_t *band, int stride, omni_rect_t c)
{
    const int cw = c.x1 - c.x0, ch = c.y1 - c.y0;
    for (int y = 0; y < ch; y++) {
        const uint16_t *s = band + (size_t)y * (size_t)stride + c.x0;
        uint16_t *d = band + (size_t)y * (size_t)cw;
        for (int x = 0; x < cw; x++) {
            const uint16_t v = s[x];
            d[x] = (uint16_t)((v << 8) | (v >> 8));   /* forward copy: d <= s, so it never reads what it wrote */
        }
    }
}

void omni_band_overlay(uint16_t *band, int stride, omni_rect_t c, const omni_band_overlay_t *o, int n)
{
    for (int i = 0; i < n; i++) {
        const int x0 = o[i].r.x0 > c.x0 ? o[i].r.x0 : c.x0, x1 = o[i].r.x1 < c.x1 ? o[i].r.x1 : c.x1;
        const int y0 = o[i].r.y0 > c.y0 ? o[i].r.y0 : c.y0, y1 = o[i].r.y1 < c.y1 ? o[i].r.y1 : c.y1;
        for (int y = y0; y < y1; y++) {
            const uint32_t *s = o[i].argb + (size_t)(y - o[i].r.y0) * (size_t)o[i].stride;
            uint16_t *d = band + (size_t)(y - c.y0) * (size_t)stride;
            for (int x = x0; x < x1; x++) {
                const uint32_t p = s[x - o[i].r.x0];
                const int a = (int)(p >> 24);
                if (!a) continue;
                d[x] = omni_blend565(d[x], omni_pack565((int)((p >> 16) & 255u), (int)((p >> 8) & 255u), (int)(p & 255u)), a);
            }
        }
    }
}

omni_dd_why_t omni_direct_admit(const omni_direct_env_t *e, uint32_t need, uint32_t band_bytes, uint32_t reserve)
{
    if (!e->enabled) return OMNI_DD_OFF;
    if (e->provisioning) return OMNI_DD_PROV;
    if (!e->display_on) return OMNI_DD_DISPLAY;
    if (!e->view_shown) return OMNI_DD_HIDDEN;
    if (e->unknown_overlay) return OMNI_DD_OVERLAY;
    if (need > 0 && e->dma_largest < band_bytes) return OMNI_DD_RAM;
    if ((uint64_t)e->int_free < (uint64_t)need + reserve) return OMNI_DD_RAM;
    return OMNI_DD_OK;
}

void omni_band_drop_keep(omni_rect_t *slots, int *n, int max, omni_rect_t a)
{
    const int32_t aa = omni_rect_area(&a);
    if (aa == 0) return;
    for (int i = 0; i < *n; i++) {
        const omni_rect_t u = omni_rect_unite(slots[i], a);
        if (omni_rect_area(&u) <= omni_rect_area(&slots[i]) + aa) {
            slots[i] = u;
            return;
        }
    }
    if (*n < max) {
        slots[(*n)++] = a;
        return;
    }
    int best = 0;
    int32_t grow_min = INT32_MAX;
    for (int i = 0; i < *n; i++) {
        const omni_rect_t u = omni_rect_unite(slots[i], a);
        const int32_t grow = omni_rect_area(&u) - omni_rect_area(&slots[i]);
        if (grow < grow_min) {
            grow_min = grow;
            best = i;
        }
    }
    slots[best] = omni_rect_unite(slots[best], a);
}
