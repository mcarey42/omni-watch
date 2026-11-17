/* The procedural seven-segment digit (rd/seg7.h; rd wave 2 Task 6; faces-bridge.js:213-222 segPaths() and segDigit(),
 * and digitXs() of the Drive Console, faces-bridge.js:237-241). The mockup's segPaths(w, h, s, k) at the console's
 * w 54, h 96, s 11, k .07, scaled to the caller's h; its gap gp 1.4 scaled with it. Pure: no esp_*, freertos or LVGL;
 * no libm; no allocation. */
#include "rd/seg7.h"
#include <string.h>
#include "fk/shapes.h"
#include "omni/raster.h"

#define REF_H 96.f
#define REF_W 54.f
#define REF_S 11.f
#define REF_GP 1.4f
#define SLANT .07f

/* bit i: segment 'a' + i (the mockup's SEG table) */
unsigned rd_seg7_segs(int ch)
{
    static const uint8_t D[10] = {
        0x3F,   /* 0 abcdef */
        0x06,   /* 1 bc */
        0x5B,   /* 2 abged */
        0x4F,   /* 3 abgcd */
        0x66,   /* 4 fgbc */
        0x6D,   /* 5 afgcd */
        0x7D,   /* 6 afgedc */
        0x07,   /* 7 abc */
        0x7F,   /* 8 */
        0x6F,   /* 9 abcdfg */
    };
    if (ch >= '0' && ch <= '9') return D[ch - '0'];
    return ch == '-' ? RD_SEG7_G : 0u;
}

float rd_seg7_width(float h) { return h * REF_W / REF_H; }
float rd_seg7_slant(float h) { return h * SLANT; }

void rd_seg7_poly(int seg, float x, float y, float h, omni_pt_t out[6])
{
    const float k = h / REF_H, w = REF_W * k, s = REF_S * k, gp = REF_GP * k;
    float q[6][2];
    if (seg == 0 || seg == 6 || seg == 3) {   /* a, g, d: H(yy) */
        const float yy = seg == 0 ? s / 2.f : seg == 6 ? h / 2.f : h - s / 2.f;
        const float P[6][2] = {{s / 2.f + gp, yy},      {s + gp, yy - s / 2.f}, {w - s - gp, yy - s / 2.f},
                               {w - s / 2.f - gp, yy}, {w - s - gp, yy + s / 2.f}, {s + gp, yy + s / 2.f}};
        memcpy(q, P, sizeof q);
    } else {   /* b, c, e, f: V(xx, y0, y1) */
        const float xx = (seg == 1 || seg == 2) ? w - s / 2.f : s / 2.f;
        const bool top = seg == 1 || seg == 5;
        const float y0 = top ? s / 2.f : h / 2.f, y1 = top ? h / 2.f : h - s / 2.f;
        const float P[6][2] = {{xx, y0 + gp},
                               {xx + s / 2.f, y0 + s / 2.f + gp},
                               {xx + s / 2.f, y1 - s / 2.f - gp},
                               {xx, y1 - gp},
                               {xx - s / 2.f, y1 - s / 2.f - gp},
                               {xx - s / 2.f, y0 + s / 2.f + gp}};
        memcpy(q, P, sizeof q);
    }
    for (int i = 0; i < 6; i++) out[i] = (omni_pt_t){x + q[i][0] + (h - q[i][1]) * SLANT, y + q[i][1]};
}

void rd_seg7_digit(uint16_t *fb, int stride, const omni_rect_t *clip, int ch, float x, float y, float h, uint16_t on,
                   uint16_t ghost, bool outline)
{
    const unsigned lit = rd_seg7_segs(ch);
    for (int sg = 0; sg < 7; sg++) {
        const bool is_on = (lit >> sg) & 1u;
        if (!is_on && (outline || ghost == 0)) continue;
        omni_pt_t p[6];
        rd_seg7_poly(sg, x, y, h, p);
        if (is_on && outline) fk_poly_stroke_max(fb, stride, clip, p, 6, 2.f, on, 255);
        else omni_fill_poly(fb, stride, clip, p, 6, is_on ? on : ghost, 255);
    }
}

/* Math.round of a non-negative x (halves up) */
static float round_pos(float v) { return (float)(int)(v + 0.5f); }

void rd_seg7_xs(float cx, float h, float xs[4])
{
    const float k = h / REF_H, dw = REF_W * k, gap = 10.f * k, colon = 22.f * k;
    const float total = dw * 4.f + gap * 2.f + colon, x0 = cx - total / 2.f - 3.f * k;
    xs[0] = round_pos(x0);
    xs[1] = round_pos(x0 + dw + gap);
    xs[2] = round_pos(x0 + 2.f * dw + gap + colon + gap);
    xs[3] = round_pos(x0 + 3.f * dw + 2.f * gap + colon + gap);
}

void rd_seg7_str(const fk_face_in_t *in, char out[5])
{
    if (!in->clk.set) {
        memcpy(out, "----", 5);
        return;
    }
    int h = in->clk.h % 24;
    const int m = in->clk.m % 60;
    if (!in->h24) h = (h + 11) % 12 + 1;   /* the mockup's h12 */
    out[0] = (!in->h24 && h < 10) ? ' ' : (char)('0' + h / 10);
    out[1] = (char)('0' + h % 10);
    out[2] = (char)('0' + m / 10);
    out[3] = (char)('0' + m % 10);
    out[4] = 0;
}

void rd_seg7_time(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_face_in_t *in, float cx, float y,
                  float h, uint16_t on, uint16_t ghost, bool colon, bool outline)
{
    float xs[4];
    char t[5];
    rd_seg7_xs(cx, h, xs);
    rd_seg7_str(in, t);
    for (int i = 0; i < 4; i++) rd_seg7_digit(fb, stride, clip, t[i], xs[i], y, h, on, ghost, outline);
    if (!colon) return;
    const float k = h / REF_H, cxl = xs[1] + REF_W * k + 24.f * k, d = 10.f * k;
    const float qx[2] = {cxl - 4.f * k, cxl - 7.f * k}, qy[2] = {y + h * .30f - 5.f * k, y + h * .70f - 5.f * k};
    for (int i = 0; i < 2; i++) {
        const omni_pt_t p[4] = {{qx[i], qy[i]}, {qx[i] + d, qy[i]}, {qx[i] + d, qy[i] + d}, {qx[i], qy[i] + d}};
        omni_fill_poly(fb, stride, clip, p, 4, on, 255);
    }
}
