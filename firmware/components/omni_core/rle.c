#include "omni/rle.h"
#include <stdbool.h>
#include <string.h>
#include "omni/blend565.h"

/* Run-length frames (rle.h). */

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v & 255);
    p[1] = (uint8_t)(v >> 8);
}
static unsigned get16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

size_t omni_rle_encode(const uint8_t *rgba, int w, int h, int ox, int oy, uint8_t *out, size_t cap)
{
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535 || cap < OMNI_RLE_HDR) return 0;
    put16(out, (unsigned)w);
    put16(out + 2, (unsigned)h);
    put16(out + 4, (unsigned)ox & 0xffff);
    put16(out + 6, (unsigned)oy & 0xffff);
    size_t o = OMNI_RLE_HDR;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = rgba + (size_t)y * w * 4;
        if (o + 2 > cap) return 0;
        const size_t nruns_at = o;
        o += 2;
        unsigned nruns = 0;
        int x = 0, end = 0;   /* end: the previous run's end */
        while (x < w) {
            if (row[x * 4 + 3] == 0) {
                x++;
                continue;
            }
            int len = 0;
            while (x + len < w && row[(x + len) * 4 + 3] != 0) len++;
            if (o + 4 + (size_t)len * 3 > cap) return 0;
            put16(out + o, (unsigned)(x - end));
            put16(out + o + 2, (unsigned)len);
            o += 4;
            for (int i = 0; i < len; i++, o += 3) {
                const uint8_t *p = row + (x + i) * 4;
                put16(out + o, (unsigned)((p[0] * 31 + 127) / 255) << 11 | (unsigned)((p[1] * 63 + 127) / 255) << 5 |
                                   (unsigned)((p[2] * 31 + 127) / 255));
                out[o + 2] = p[3];
            }
            nruns++;
            x += len;
            end = x;
        }
        put16(out + nruns_at, nruns);
    }
    return o;
}

size_t omni_rle_size(const uint8_t *frame, size_t size)
{
    if (size < OMNI_RLE_HDR) return 0;
    const unsigned h = get16(frame + 2);
    size_t o = OMNI_RLE_HDR;
    for (unsigned y = 0; y < h; y++) {
        if (o + 2 > size) return 0;
        unsigned nruns = get16(frame + o);
        o += 2;
        while (nruns--) {
            if (o + 4 > size) return 0;
            o += 4 + (size_t)get16(frame + o + 2) * 3;
            if (o > size) return 0;
        }
    }
    return o;
}

void omni_rle_draw(const uint8_t *frame, uint16_t *fb, int stride, const omni_rect_t *clip)
{
    const int w = (int)get16(frame), h = (int)get16(frame + 2);
    const int ox = (int16_t)get16(frame + 4), oy = (int16_t)get16(frame + 6);
    (void)w;
    const int cx0 = clip ? clip->x0 : 0, cy0 = clip ? clip->y0 : 0;
    const int cx1 = clip ? clip->x1 : 32767, cy1 = clip ? clip->y1 : 32767;
    const uint8_t *p = frame + OMNI_RLE_HDR;
    for (int y = 0; y < h; y++) {
        unsigned nruns = get16(p);
        p += 2;
        const int sy = oy + y;
        const bool row_in = sy >= cy0 && sy < cy1;
        uint16_t *row = row_in ? fb + (ptrdiff_t)(sy - cy0) * stride : NULL;   /* row[x - cx0]: screen column x */
        int x = ox;
        while (nruns--) {
            x += (int)get16(p);
            const int len = (int)get16(p + 2);
            const uint8_t *px = p + 4;
            p += 4 + (size_t)len * 3;
            if (row_in) {
                int a = x > cx0 ? x : cx0, b = x + len < cx1 ? x + len : cx1;
                for (int sx = a; sx < b; sx++) {
                    const uint8_t *q = px + (size_t)(sx - x) * 3;
                    const int al = q[2];
                    const uint16_t c = (uint16_t)(q[0] | q[1] << 8);
                    uint16_t *d = row + (sx - cx0);
                    *d = al == 255 ? c : omni_blend565(*d, c, al);
                }
            }
            x += len;
        }
        if (sy >= cy1) break;
    }
}
