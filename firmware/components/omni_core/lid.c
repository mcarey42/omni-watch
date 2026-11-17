#include "omni/lid.h"
#include <math.h>

/* The hinge engine and the flip's timeline (lid.h). Single precision in a fixed order (-ffp-contract=off): the device
 * builds the lines the host tests pin. */

#define D2R 0.017453292519943295f

/* the dome: buildLid's radius LR 226, drawLid's normal tilt asin(.) * .55 */
const omni_hinge_t OMNI_LID_HINGE = { OMNI_HINGE_HORIZONTAL, -6.0f, 800.0f, 466, 226.0f, 0.55f };

float omni_hinge_gone_rad(const omni_hinge_t *h) { return atan2f(h->focal, (float)h->n * 0.5f - h->pos); }

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float out_cubic(float s) { const float u = 1 - s; return 1 - u * u * u; }
static float in_out_cubic(float s)
{
    if (s < 0.5f) return 4 * s * s * s;
    const float u = -2 * s + 2;
    return 1 - u * u * u / 2;
}
/* ph(t, a, b) of the mockup, in float (a < b) */
static float phf(float t, float a, float b) { return clampf((t - a) / (b - a), 0, 1); }

void omni_hinge_map(const omni_hinge_t *h, float theta_rad, omni_hinge_map_t *m)
{
    const float C = (float)h->n * 0.5f, s = sinf(theta_rad), c = cosf(theta_rad);
    m->a = h->focal;
    m->b = -h->focal * h->pos;
    m->c = s;
    m->d = h->focal * c - C * s;
}

static uint8_t to8(float x) { return (uint8_t)floorf(clampf(x, 0, 1) * 255.0f + 0.5f); }

void omni_hinge_lines(const omni_hinge_t *h, float theta_rad, omni_hinge_line_t *lines)
{
    omni_hinge_lines_at(h, theta_rad, 0, h->n, lines);
}

void omni_hinge_lines_at(const omni_hinge_t *h, float theta_rad, int y0, int y1, omni_hinge_line_t *lines)
{
    omni_hinge_map_t m;
    omni_hinge_map(h, theta_rad, &m);
    const float C = (float)h->n * 0.5f, st = sinf(theta_rad), n = (float)h->n;
    const float spec_k = clampf(theta_rad * 6.0f, 0, 1);
    for (int y = y0; y < y1; y++) {
        omni_hinge_line_t *l = &lines[y - y0];
        const float Y = (float)y + 0.5f, den = m.c * Y + m.d;
        const float v = den > 0 ? (m.a * Y + m.b) / den + h->pos : -1.0f;
        if (!(den > 0) || !(v >= 0) || !(v < n)) {   /* past the leaf's vanishing line, or off the leaf */
            *l = (omni_hinge_line_t){ -1, 0, 0, 0 };
            continue;
        }
        l->v = (int16_t)floorf(v);
        const float inv = 1.0f - (v - h->pos) * st / h->focal;
        l->inv_s_q16 = (uint32_t)floorf(inv * 65536.0f + 0.5f);
        /* drawLid's strip shading at v: the dome's normal tilt beta across the lid, the facet's angle nn */
        const float beta = h->dome_r > 0 ? asinf(clampf((v - C) / h->dome_r, -1, 1)) * h->dome_tilt : 0.0f;
        const float nn = theta_rad + beta;
        const float dark = clampf(1.0f - cosf(nn + 0.5f) / cosf(beta + 0.5f), 0, 0.6f);
        const float e1 = (nn - 0.38f) / 0.14f, e2 = (nn - 1.05f) / 0.22f;
        const float spec = (expf(-(e1 * e1)) * 0.5f + expf(-(e2 * e2)) * 0.22f) * spec_k;
        l->dark = dark > 0.01f ? to8(dark) : 0;   /* the mockup skips a draw at .01 or below */
        l->spec = spec > 0.01f ? to8(spec) : 0;
    }
}

float omni_flip_theta(uint32_t t_ms, bool reduced)
{
    const float t = reduced ? (float)t_ms * OMNI_FLIP_REDUCED_K : (float)t_ms;
    const float pop = 4.0f * D2R * out_cubic(phf(t, OMNI_FLIP_POP_T0_MS, OMNI_FLIP_UP_MS)) -
                      1.5f * D2R * in_out_cubic(phf(t, OMNI_FLIP_UP_MS, OMNI_FLIP_LATCH_MS));
    const float s = phf(t, OMNI_FLIP_SWING_T0_MS, OMNI_FLIP_SWING_T0_MS + OMNI_FLIP_SWING_MS);
    return pop + 108.0f * D2R * in_out_cubic(s);
}

float omni_flip_dim(float theta_rad) { return 0.92f * (1.0f - out_cubic(phf(theta_rad, 2.0f * D2R, 80.0f * D2R))); }

float omni_flip_fade(uint32_t t_ms) { return 1.0f - out_cubic(phf((float)t_ms, 0, OMNI_FLIP_FADE_MS)); }

void omni_lid_spec_cols(uint8_t cols[466])
{
    static const float POS[5] = { 0, 0.3f, 0.42f, 0.7f, 1 }, A[5] = { 0.15f, 0.8f, 1, 0.55f, 0.1f };
    for (int x = 0; x < 466; x++) {
        const float t = ((float)x + 0.5f) / 466.0f;
        int i = 1;
        while (i < 4 && t > POS[i]) i++;
        const float u = (t - POS[i - 1]) / (POS[i] - POS[i - 1]);
        cols[x] = to8(A[i - 1] + (A[i] - A[i - 1]) * u);
    }
}
