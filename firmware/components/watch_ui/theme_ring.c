/* theme_draw_pill_ring (theme.h): a hold key's brass ring, shared by Settings' Forget sheet, provisioning's "Forget and
 * re-scan" pill and the carousel editor's Restore default. Its own file so it compiles -Os (R225, Task 14 review M2)
 * while theme.c keeps the build's -O2. */
#include <math.h>
#include "theme.h"
void theme_draw_pill_ring(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, float p, int32_t w,
                          uint32_t rgb, lv_opa_t opa)
{
    const int32_t r = (y2 - y1) / 2, run = x2 - x1 - 2 * r;   /* the half circles' radius; the straight runs' length */
    const float arc = (float)M_PI * r;
    float l = p * (2 * run + 2 * arc);
    int32_t x = (x1 + x2) / 2;
    for (int s = 0; s < 5 && l > 0; s++) {
        if (s % 2 == 0) {   /* the straight runs: the top to the right, the bottom back, the top to the top centre */
            /* the top half-runs end at the arcs' own centres (x2 - r, x1 + r) and sum to run, so an odd run (or an odd
             * width) still meets the arcs and ends exactly where it began (review M3) */
            const int32_t y = s == 2 ? y2 : y1, dir = s == 2 ? -1 : 1,
                          seg = s == 2 ? run : s == 0 ? x2 - r - (x1 + x2) / 2 : (x1 + x2) / 2 - (x1 + r);
            theme_draw_line(layer, x, y, x + dir * (int32_t)lroundf(fminf(l, (float)seg)), y, w, rgb, opa, true);
            x += dir * seg;
            l -= (float)seg;
        } else {            /* the right half circle from 270 degrees, the left one from 90 */
            const int32_t a0 = s == 1 ? 270 : 90, deg = (int32_t)lroundf(fminf(l, arc) * (180 / (float)M_PI) / (float)r);
            if (deg > 0) theme_draw_arc(layer, s == 1 ? x2 - r : x1 + r, y1 + r, r + w / 2, w, a0, (a0 + deg) % 360, rgb, opa, true);
            l -= arc;
        }
    }
}
