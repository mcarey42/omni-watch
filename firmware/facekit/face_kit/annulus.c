#include "fk/annulus.h"
#include <math.h>
#include "omni/blend565.h"
#include "omni/globe_check.h"   /* omni_fnv1a64 */
#include "omni/trig.h"

/* One polar annulus (compose.h): ring.h's plain-LUT map (variant A) for a single ring of any radius about any centre on
 * the half-pixel grid. The build is single precision in a fixed order (-ffp-contract=off, CMakeLists.txt) with no libm
 * per pixel: membership and the radius row are compares against squares, which are exact on the grid; theta is
 * omni_atan2_tab, which is table arithmetic inside its domain (|X|, |Y| < 256, kept there by an exact power-of-two
 * scale). sqrtf runs once a row and once a run, only as a first guess that the exact compares then settle. So the host
 * and the device build the same map, byte for byte. */

#define TWO_PI_F 6.28318530717958647692f
#define R1_MAX 1024.0f
#define ROWS_MAX 256

typedef struct { int16_t x[2], n[2]; uint32_t lut; } arow_t;   /* a row's runs (n 0: none) and its first LUT entry */
_Static_assert(sizeof(arow_t) == 12, "a 12-byte row record (compose.h)");

struct fk_annulus {
    float cx, cy, r0, r1;
    uint32_t npx;
    int16_t y0;                  /* the first row's screen y */
    uint16_t nrows, n_theta, n_r;
    const arow_t *row;           /* nrows records */
    const uint16_t *th;          /* npx theta indices, row by row, runs left to right */
    const uint8_t *ri;           /* npx radius rows, the same order */
};
_Static_assert(sizeof(struct fk_annulus) <= 64, "the header compose.h's bound allows");

static size_t align8(size_t n) { return (n + 7) & ~(size_t)7; }
static int ifloor(float v) { const int i = (int)v; return i - (v < (float)i); }   /* |v| < 2^31 */
static int iceil(float v) { const int i = (int)v; return i + (v > (float)i); }

static bool valid(float r0, float r1, uint16_t n)
{
    if (!(r0 >= 0 && r1 > r0 && r1 <= R1_MAX)) return false;   /* also refuses NaN */
    if (iceil(r1 - r0) > ROWS_MAX) return false;
    return n >= 4 && n <= 32768 && (n & (n - 1)) == 0;
}
static bool on_grid(float c) { return c > -16384.0f && c < 16384.0f && 2 * c == (float)(int)(2 * c); }
static int rows_for(float r1) { return 2 * iceil(r1) + 2; }   /* covers every row of any centre on the grid */
static int first_row(float cy, float r1) { return ifloor(cy) - iceil(r1) - 1; }

/* Row geometry: X = x + 0.5 - cx, Y (the row's) given; a pixel is inside the disc R2 when X^2 + Y^2 < R2. */
typedef struct { float cx, Y, Y2; } arowg_t;
static inline bool inside(const arowg_t *g, int x, float R2)
{
    const float X = (float)x + 0.5f - g->cx;
    return X * X + g->Y2 < R2;
}
/* [*a, *b): the row's pixels inside the disc R2 (contiguous: r^2 is convex along the row). sqrtf only guesses. */
static void disc_run(const arowg_t *g, float R2, int *a, int *b)
{
    *a = *b = 0;
    if (!(g->Y2 < R2)) return;
    const float h = sqrtf(R2 - g->Y2), xc = g->cx - 0.5f;
    int lo = ifloor(xc - h), hi = ifloor(xc + h);
    while (inside(g, lo - 1, R2)) lo--;
    while (lo <= hi && !inside(g, lo, R2)) lo++;
    while (inside(g, hi + 1, R2)) hi++;
    while (hi >= lo && !inside(g, hi, R2)) hi--;
    if (lo <= hi) {
        *a = lo;
        *b = hi + 1;
    }
}
/* The row's annulus runs r0^2 <= r^2 < r1^2: the outer disc less the inner one, at most two runs. */
static void row_runs(float cx, float cy, int y, float R02, float R12, arow_t *rw)
{
    arowg_t g = { cx, cy - ((float)y + 0.5f), 0 };
    g.Y2 = g.Y * g.Y;
    int a1, b1, a0, b0;
    rw->x[0] = rw->x[1] = 0;
    rw->n[0] = rw->n[1] = 0;
    disc_run(&g, R12, &a1, &b1);
    if (a1 >= b1) return;
    disc_run(&g, R02, &a0, &b0);
    int k = 0;
    if (a0 >= b0) {
        rw->x[k] = (int16_t)a1;
        rw->n[k++] = (int16_t)(b1 - a1);
        return;
    }
    if (a1 < a0) {
        rw->x[k] = (int16_t)a1;
        rw->n[k++] = (int16_t)(a0 - a1);
    }
    if (b0 < b1) {
        rw->x[k] = (int16_t)b0;
        rw->n[k] = (int16_t)(b1 - b0);
    }
}

static uint32_t count_px(float cx, float cy, float r0, float r1)
{
    const int R = rows_for(r1), y0 = first_row(cy, r1);
    uint32_t n = 0;
    for (int i = 0; i < R; i++) {
        arow_t rw;
        row_runs(cx, cy, y0 + i, r0 * r0, r1 * r1, &rw);
        n += (uint32_t)rw.n[0] + (uint32_t)rw.n[1];
    }
    return n;
}

size_t fk_annulus_bytes(float r0, float r1, uint16_t n_theta)
{
    if (!valid(r0, r1, n_theta)) return 0;
    uint32_t npx = 0;   /* the count is one of four on the grid: the centre's fractions are 0 or 1/2 */
    for (int f = 0; f < 4; f++) {
        const uint32_t k = count_px((f & 1) ? 0.5f : 0.0f, (f & 2) ? 0.5f : 0.0f, r0, r1);
        if (k > npx) npx = k;
    }
    return 7 + align8(sizeof(struct fk_annulus)) + (size_t)rows_for(r1) * sizeof(arow_t) + (size_t)npx * 3;
}

/* floor(r - r0) by squares: the largest h < n_r with (r0 + h)^2 <= r2, from a guess. */
static int radius_row(float r0, int n_r, float r2, int h)
{
    if (h < 0) h = 0;
    if (h > n_r - 1) h = n_r - 1;
    while (h + 1 < n_r && (r0 + (float)(h + 1)) * (r0 + (float)(h + 1)) <= r2) h++;
    while (h > 0 && (r0 + (float)h) * (r0 + (float)h) > r2) h--;
    return h;
}

fk_annulus_t *fk_annulus_build(void *mem, size_t bytes, float cx, float cy, float r0, float r1, uint16_t n_theta)
{
    if (!mem || !valid(r0, r1, n_theta) || !on_grid(cx) || !on_grid(cy)) return NULL;
    if (bytes < fk_annulus_bytes(r0, r1, n_theta)) return NULL;
    uint8_t *base = (uint8_t *)(((uintptr_t)mem + 7) & ~(uintptr_t)7);
    struct fk_annulus *an = (struct fk_annulus *)(void *)base;
    arow_t *row = (arow_t *)(void *)(base + align8(sizeof *an));
    const int R = rows_for(r1), y0 = first_row(cy, r1), n_r = iceil(r1 - r0);
    if (y0 < -32768 || y0 + R > 32767) return NULL;
    const float R02 = r0 * r0, R12 = r1 * r1, k = (float)n_theta / TWO_PI_F;
    float s = 1.0f;   /* exact: atan2 is scale-free, and the table wants |X|, |Y| < 256 (r < r1) */
    while (r1 * s > 256.0f) s *= 0.5f;
    uint32_t npx = 0;
    for (int i = 0; i < R; i++) {
        row_runs(cx, cy, y0 + i, R02, R12, &row[i]);
        npx += (uint32_t)row[i].n[0] + (uint32_t)row[i].n[1];
    }
    uint16_t *th = (uint16_t *)(void *)(row + R);
    uint8_t *ri = (uint8_t *)(th + npx);
    uint32_t e = 0;
    for (int i = 0; i < R; i++) {
        row[i].lut = e;
        const float Y = cy - ((float)(y0 + i) + 0.5f), Y2 = Y * Y;
        for (int j = 0; j < 2; j++) {
            int h = -1;
            for (int x = row[i].x[j]; x < row[i].x[j] + row[i].n[j]; x++, e++) {
                const float X = (float)x + 0.5f - cx, r2 = X * X + Y2;
                if (h < 0) h = (int)(sqrtf(r2) - r0);   /* the run's first guess; then r moves < 1 px a step */
                h = radius_row(r0, n_r, r2, h);
                ri[e] = (uint8_t)h;
                float t = omni_atan2_tab(X * s, Y * s);   /* clockwise from 12 o'clock */
                if (t < 0) t += TWO_PI_F;
                int ti = (int)(t * k);
                if (ti > n_theta - 1) ti = n_theta - 1;
                th[e] = (uint16_t)ti;
            }
        }
    }
    *an = (struct fk_annulus){ cx, cy, r0, r1, npx, (int16_t)y0, (uint16_t)R, n_theta, (uint16_t)n_r, row, th, ri };
    return an;
}

uint32_t fk_annulus_px(const fk_annulus_t *an) { return an ? an->npx : 0; }

/* FNV-1a 64 over the map, serialised little-endian whatever the host: n_theta, n_r, y0, nrows, npx, each row's
 * x[2], n[2] and lut, then every th (2 B) and every ri (1 B). The centre and radii enter only through the map. */
static uint64_t fnv_le(uint64_t h, uint32_t v, int bytes)
{
    uint8_t b[4];
    for (int i = 0; i < bytes; i++) b[i] = (uint8_t)(v >> (8 * i));
    return omni_fnv1a64(h, b, (size_t)bytes);
}
uint64_t fk_annulus_digest(const fk_annulus_t *an)
{
    if (!an) return 0;
    uint64_t h = OMNI_FNV1A_INIT;
    h = fnv_le(h, an->n_theta, 2);
    h = fnv_le(h, an->n_r, 2);
    h = fnv_le(h, (uint16_t)an->y0, 2);
    h = fnv_le(h, an->nrows, 2);
    h = fnv_le(h, an->npx, 4);
    for (int i = 0; i < an->nrows; i++) {
        const arow_t *r = &an->row[i];
        h = fnv_le(h, (uint16_t)r->x[0], 2);
        h = fnv_le(h, (uint16_t)r->x[1], 2);
        h = fnv_le(h, (uint16_t)r->n[0], 2);
        h = fnv_le(h, (uint16_t)r->n[1], 2);
        h = fnv_le(h, r->lut, 4);
    }
    for (uint32_t e = 0; e < an->npx; e++) h = fnv_le(h, an->th[e], 2);
    return omni_fnv1a64(h, an->ri, an->npx);
}

static inline void put(uint16_t *d, uint16_t rgb, unsigned a)
{
    if (a == 255) *d = rgb;   /* blend565.h: 255 is the source, 0 the destination, exactly */
    else if (a) *d = omni_blend565(*d, rgb, (int)a);
}

void fk_annulus_draw(const fk_annulus_t *an, const omni_strip_t *st, uint16_t th_off, uint8_t opa,
                     uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!an || !st || !fb || opa == 0 || !st->rgb || !st->a || st->n_theta != an->n_theta ||
        st->stride < st->n_theta || st->n_r < an->n_r)
        return;
    const uint32_t S = st->stride, mask = st->n_theta - 1u, off = th_off & mask;
    const uint16_t *rgb = st->rgb;
    const uint8_t *alpha = st->a;
    const int ya = clip.y0 > an->y0 ? clip.y0 : an->y0;
    const int yb = clip.y1 < an->y0 + an->nrows ? clip.y1 : an->y0 + an->nrows;
    for (int y = ya; y < yb; y++) {
        const arow_t *rw = &an->row[y - an->y0];
        uint32_t lut = rw->lut;
        for (int j = 0; j < 2; lut += (uint32_t)rw->n[j], j++) {
            const int x0 = rw->x[j], x1 = x0 + rw->n[j];
            const int a = x0 > clip.x0 ? x0 : clip.x0, b = x1 < clip.x1 ? x1 : clip.x1;
            if (a >= b) continue;
            const uint16_t *th = an->th + lut + (a - x0);
            const uint8_t *ri = an->ri + lut + (a - x0);
            uint16_t *d = fb + (y - clip.y0) * stride + (a - clip.x0);
            const int n = b - a;
            if (opa == 255)
                for (int i = 0; i < n; i++) {
                    const uint32_t e = (uint32_t)ri[i] * S + ((th[i] + off) & mask);
                    put(d + i, rgb[e], alpha[e]);
                }
            else
                for (int i = 0; i < n; i++) {
                    const uint32_t e = (uint32_t)ri[i] * S + ((th[i] + off) & mask);
                    put(d + i, rgb[e], alpha[e] * (unsigned)opa / 255u);
                }
        }
    }
}

void fk_annulus_draw_a8(const fk_annulus_t *an, const fk_img_t *a8, uint16_t th_off, uint16_t rgb, uint8_t opa,
                        uint16_t *fb, int stride, omni_rect_t clip)
{
    if (!an || !a8 || !a8->a || !fb || opa == 0 || a8->w != an->n_theta || a8->h < an->n_r) return;
    const uint32_t S = a8->w, mask = an->n_theta - 1u, off = th_off & mask;
    const uint8_t *alpha = a8->a;
    const int ya = clip.y0 > an->y0 ? clip.y0 : an->y0;
    const int yb = clip.y1 < an->y0 + an->nrows ? clip.y1 : an->y0 + an->nrows;
    for (int y = ya; y < yb; y++) {
        const arow_t *rw = &an->row[y - an->y0];
        uint32_t lut = rw->lut;
        for (int j = 0; j < 2; lut += (uint32_t)rw->n[j], j++) {
            const int x0 = rw->x[j], x1 = x0 + rw->n[j];
            const int a = x0 > clip.x0 ? x0 : clip.x0, b = x1 < clip.x1 ? x1 : clip.x1;
            if (a >= b) continue;
            const uint16_t *th = an->th + lut + (a - x0);
            const uint8_t *ri = an->ri + lut + (a - x0);
            uint16_t *d = fb + (y - clip.y0) * stride + (a - clip.x0);
            const int n = b - a;
            for (int i = 0; i < n; i++) {
                const unsigned v = alpha[(uint32_t)ri[i] * S + ((th[i] + off) & mask)];
                if (v) put(d + i, rgb, opa == 255 ? v : v * (unsigned)opa / 255u);
            }
        }
    }
}
