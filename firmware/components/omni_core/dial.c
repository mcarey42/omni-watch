#include "omni/dial.h"

#include <math.h>

void omni_dial_rot(float a, float lx, float ly, int32_t cx, int32_t cy, int32_t *x, int32_t *y)
{
    const float c = cosf(a), s = sinf(a);
    *x = cx + (int32_t)lroundf(lx * c - ly * s);
    *y = cy + (int32_t)lroundf(lx * s + ly * c);
}

void omni_dial_sec_box(float a, int32_t cx, int32_t cy, int32_t box[4])
{
    const float ends[2] = {OMNI_DIAL_SEC_TAIL, OMNI_DIAL_SEC_TIP_TO};
    box[0] = box[1] = INT32_MAX;
    box[2] = box[3] = INT32_MIN;
    for (int i = 0; i < 2; i++) {
        int32_t x, y;
        omni_dial_rot(a, 0, ends[i], cx, cy, &x, &y);
        const int32_t xs[2] = {x, x + OMNI_DIAL_SEC_SH_DX}, ys[2] = {y, y + OMNI_DIAL_SEC_SH_DY};
        for (int k = 0; k < 2; k++) {
            if (xs[k] < box[0]) box[0] = xs[k];
            if (ys[k] < box[1]) box[1] = ys[k];
            if (xs[k] > box[2]) box[2] = xs[k];
            if (ys[k] > box[3]) box[3] = ys[k];
        }
    }
    box[0] -= OMNI_DIAL_SEC_PAD;
    box[1] -= OMNI_DIAL_SEC_PAD;
    box[2] += OMNI_DIAL_SEC_PAD;
    box[3] += OMNI_DIAL_SEC_PAD;
}

uint32_t omni_dial_hand_box(const omni_dial_hand_t *h, int32_t rot, bool shadow, int32_t cx, int32_t cy, int32_t box[4])
{
    const float a = (float)rot * 3.14159265358979323846f / 1800;
    box[0] = box[1] = INT32_MAX;
    box[2] = box[3] = INT32_MIN;
    for (int k = shadow ? 0 : 1; k < 2; k++) {
        const int32_t ox = k ? 0 : h->sdx, oy = k ? 0 : h->sdy;
        for (int c = 0; c < 4; c++) {
            int32_t x, y;
            const float lx = (float)((c & 1 ? h->w : 0) - h->px), ly = (float)((c & 2 ? h->h : 0) - h->py);
            omni_dial_rot(a, lx, ly, cx + ox, cy + oy, &x, &y);
            if (x < box[0]) box[0] = x;
            if (y < box[1]) box[1] = y;
            if (x > box[2]) box[2] = x;
            if (y > box[3]) box[3] = y;
        }
    }
    box[0] -= 2;
    box[1] -= 2;
    box[2] += 2;
    box[3] += 2;
    return (uint32_t)(box[2] - box[0] + 1) * (uint32_t)(box[3] - box[1] + 1);
}
