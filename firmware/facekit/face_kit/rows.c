#include "fk/rows.h"
#include <stddef.h>
#include <string.h>
#include "fk/cols.h"   /* fk_panel_row */
#include "omni/blend565.h"

/* The row renderer (rows.h). Single precision in a fixed order (-ffp-contract=off, face_kit's CMakeLists): float per
 * row and per band only; every per-pixel step is integer. The look follows drawLid's flat leaf in the reveal mockup
 * (its rows scaled about the eye's column, the uniform black, the lower edge's thickness and its cold-light line). */

#define N FK_ROWS_N
#define INV_MAX (1u << 20)   /* 1 / scale above 16 (never on screen for a leaf turned toward the eye): not drawn, so
                              * the Q16 column sums below stay inside int32 */

/* ---- the map ---- */

/* The hinge engine's map at theta: omni_hinge_map's (its sinf and cosf), except at theta 0, where it is the exact
 * constants sin 0 = 0 and cos 0 = 1 put through the same expressions (a shut leaf's frames read no libm). The lines'
 * sine (omni_hinge_lines_at's own sinf(theta)) is m->c, the same value. */
static void row_map(const omni_hinge_t *h, float theta, omni_hinge_map_t *m)
{
    if (theta == 0.0f) {
        const float C = (float)h->n * 0.5f, s = 0.0f, c = 1.0f;
        m->a = h->focal;
        m->b = -h->focal * h->pos;
        m->c = s;
        m->d = h->focal * c - C * s;
        return;
    }
    omni_hinge_map(h, theta, m);
}

/* Line yl (screen row y less dy): omni_hinge_lines_at's v and inv_s by its expressions in its order, floorf of a
 * value >= 0 taken as the truncation (test_fk_rows_lines_match_omni pins them equal). A leaf line always has inv > 0
 * (the scale is finite before the vanishing line); the guard only keeps the conversion defined. */
static fk_row_t map_line(const omni_hinge_t *h, const omni_hinge_map_t *m, int yl)
{
    const float n = (float)h->n, st = m->c;
    const float Y = (float)yl + 0.5f, den = m->c * Y + m->d;
    const float v = den > 0 ? (m->a * Y + m->b) / den + h->pos : -1.0f;
    if (!(den > 0) || !(v >= 0) || !(v < n)) return (fk_row_t){ -1, 0 };   /* past the vanishing line, off the leaf */
    const float inv = 1.0f - (v - h->pos) * st / h->focal;
    if (!(inv > 0)) return (fk_row_t){ -1, 0 };
    return (fk_row_t){ (int16_t)(int)v, (uint32_t)(inv * 65536.0f + 0.5f) };
}

void fk_rows_map(const omni_hinge_t *h, const fk_rows_state_t *st, int y0, int y1, fk_row_t *rows)
{
    omni_hinge_map_t m;
    row_map(h, st->theta, &m);
    for (int y = y0; y < y1; y++) rows[y - y0] = map_line(h, &m, y - st->dy);
}

void fk_rows_extent(const omni_hinge_t *h, const fk_rows_state_t *st, int *y0, int *y1)
{
    omni_hinge_map_t m;
    row_map(h, st->theta, &m);
    *y0 = *y1 = 0;
    int y = 0;
    while (y < h->n && map_line(h, &m, y - st->dy).v < 0) y++;   /* the leaf's rows are contiguous */
    if (y >= h->n) return;
    *y0 = y;
    while (y < h->n && map_line(h, &m, y - st->dy).v >= 0) y++;
    *y1 = y;
}

/* ---- the compositor ---- */

/* the leaf on row r: v in [0, lh), 1 / scale drawable */
static bool row_leaf(const fk_row_t *r, int lh) { return r->v >= 0 && r->v < lh && r->inv_s_q16 <= INV_MAX; }

/* The leaf's column for box-local column x of a row scaled by inv about lw / 2, both at pixel centres:
 * u = floor(lw / 2 + (x + .5 - lw / 2) inv) = (lw 2^16 + (2x + 1 - lw) inv) >> 17, in Q17; < 0 or >= lw: off the leaf.
 * |2x + 1 - lw| <= 2 N and inv <= INV_MAX keep every sum inside int32. */
static inline int32_t col_q(int x, int lw, uint32_t inv)
{
    return ((int32_t)lw << 16) + (int32_t)(2 * x + 1 - lw) * (int32_t)inv;
}

/* The box-local columns [*a, *b) of [0, w) where row r draws the leaf (u monotonic in x: one run; empty a >= b) */
static void leaf_span_of(const fk_row_t *r, int lw, int w, int *a, int *b)
{
    *a = *b = 0;
    int x = 0;
    while (x < w && (col_q(x, lw, r->inv_s_q16) < 0 || (col_q(x, lw, r->inv_s_q16) >> 17) >= lw)) x++;
    *a = x;
    while (x < w && col_q(x, lw, r->inv_s_q16) >= 0 && (col_q(x, lw, r->inv_s_q16) >> 17) < lw) x++;
    *b = x;
}

/* black at shade over L: omni_blend565(L, 0, shade), whose black source terms are 0 (sq = 256 - omni_amul(shade)) */
static inline uint16_t darken(uint16_t L, uint32_t sq)
{
    const uint32_t rb = ((uint32_t)omni_ex_r(L) << 16 | (uint32_t)omni_ex_b(L)) * sq;
    const uint32_t g = (uint32_t)omni_ex_g(L) * sq;
    return (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
}

/* The leaf's row src (lw pixels) scaled by inv into o[0 .. xz - xa), for box-local columns [xa, xz); sq: the shade's
 * weight (256: none). Off the leaf: left as it is. */
static void leaf_span(uint16_t *o, int xa, int xz, const uint16_t *src, int lw, uint32_t inv, uint32_t sq)
{
    int32_t q = col_q(xa, lw, inv);
    const int32_t step = 2 * (int32_t)inv;
    for (int x = xa; x < xz; x++, o++, q += step) {
        if (q < 0) continue;
        const int32_t u = q >> 17;
        if (u >= lw) continue;
        *o = sq == 256u ? src[u] : darken(src[u], sq);
    }
}

void fk_rows_edge(const fk_rows_state_t *st, const fk_row_t *rows, int nrows, int lw, int lh, fk_rows_edge_t *e)
{
    *e = (fk_rows_edge_t){ -1, 0, 0 };
    if (st->edge_px == 0) return;
    int y = nrows - 1;
    while (y >= 0 && !row_leaf(&rows[y], lh)) y--;
    if (y < 0 || y + 1 >= nrows) return;   /* no leaf, or its free edge off the bottom */
    int a, b;
    leaf_span_of(&rows[y], lw, lw, &a, &b);
    *e = (fk_rows_edge_t){ (int16_t)y, (int16_t)a, (int16_t)b };
}

/* The quad's and the rim's rows for edge row L: quad [L + 1, L + 1 + edge_px); rim the rows whose centres lie in
 * [ye - rim_px / 2, ye + rim_px / 2), ye = L + 1 + edge_px (the quad's lower edge) */
typedef struct { int L, q1; bool rim; float lo, hi; } edge_t;
static edge_t edge_of(const fk_rows_state_t *st, int L)
{
    edge_t e = { L, L < 0 ? L : L + 1 + st->edge_px, false, 0, 0 };
    if (L >= 0 && st->rim_a8 && st->rim_px > 0) {
        const float ye = (float)e.q1, hw = st->rim_px * 0.5f;
        e.rim = true;
        e.lo = ye - hw;
        e.hi = ye + hw;
    }
    return e;
}
static inline bool is_quad(const edge_t *e, int y) { return e->L >= 0 && y > e->L && y < e->q1; }
static inline bool is_rim(const edge_t *e, int y)
{
    const float c = (float)y + 0.5f;
    return e->rim && c >= e->lo && c < e->hi;
}

/* row y of the quad and rim over o (o[0] is box-local column xa) in box-local columns [xa, xz) */
static void edge_row(const fk_rows_state_t *st, const edge_t *e, int y, uint16_t *o, int xa, int xz)
{
    if (xa >= xz) return;
    if (is_quad(e, y))
        for (int x = 0; x < xz - xa; x++) o[x] = st->edge_rgb;
    if (is_rim(e, y))
        for (int x = 0; x < xz - xa; x++) o[x] = omni_blend565(o[x], st->rim_rgb, st->rim_a8);
}

void fk_rows_band(const fk_rows_state_t *st, const fk_row_t *rows, const fk_rows_edge_t *edge, const uint16_t *leaf,
                  const uint16_t *interior, omni_rect_t band, uint16_t *out)
{
    omni_rect_t b = band;   /* clipped to the screen; out keeps band's layout (row 0 = band.y0) */
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > N) b.x1 = N;
    if (b.y1 > N) b.y1 = N;
    if (b.x0 >= b.x1 || b.y0 >= b.y1) return;
    out += (ptrdiff_t)(b.y0 - band.y0) * N;
    if (st->fade == 0) {   /* the fade's first frame: black whatever is under it */
        for (int y = b.y0; y < b.y1; y++) memset(out + (size_t)(y - b.y0) * N + b.x0, 0, (size_t)(b.x1 - b.x0) * 2);
        return;
    }
    const edge_t e = edge_of(st, edge->L);
    const int ea = edge->x0, ez = edge->x1;   /* the quad's and the rim's columns: where the edge row draws the leaf */
    const uint32_t sq = 256u - (uint32_t)omni_amul(st->shade);
    const uint32_t fp = (uint32_t)omni_amul(st->fade);
    for (int y = b.y0; y < b.y1; y++) {
        uint16_t *o = out + (size_t)(y - b.y0) * N;
        int p0, p1;
        fk_panel_row(y, &p0, &p1);   /* off the round panel nothing shows: black, and no work there */
        if (p0 < b.x0) p0 = b.x0;
        if (p0 > b.x1) p0 = b.x1;   /* a narrow band left of the panel's row (Task 3 review I1) */
        if (p1 > b.x1) p1 = b.x1;
        if (p1 < p0) p1 = p0;
        memset(o + b.x0, 0, (size_t)(p0 - b.x0) * 2);
        memset(o + p1, 0, (size_t)(b.x1 - p1) * 2);
        if (p0 >= p1) continue;
        const fk_row_t *r = &rows[y];
        const bool on = row_leaf(r, N);
        /* the interior, except on a row the leaf covers whole (inv_s <= 1: every column is on the leaf); NULL: out
         * holds it already (rd wave 1 Task 10) */
        if (interior && (!on || r->inv_s_q16 > 65536u))
            memcpy(o + p0, interior + (size_t)y * N + p0, (size_t)(p1 - p0) * 2);
        if (on) leaf_span(o + p0, p0, p1, leaf + (size_t)r->v * N, N, r->inv_s_q16, sq);
        const int xa = ea > p0 ? ea : p0, xz = ez < p1 ? ez : p1;
        edge_row(st, &e, y, o + xa, xa, xz);
        if (st->fade != 255)   /* the fade from black: omni_blend565(0, o, fade), the black destination's terms 0 */
            for (int x = p0; x < p1; x++) {
                const uint16_t q = o[x];
                const uint32_t rb = ((uint32_t)omni_ex_r(q) << 16 | (uint32_t)omni_ex_b(q)) * fp;
                const uint32_t g = (uint32_t)omni_ex_g(q) * fp;
                o[x] = (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
            }
    }
}

void fk_rows_blit(const fk_rows_state_t *st, const fk_row_t *rows, const uint16_t *leaf, int lw, int lh,
                  omni_rect_t box, omni_rect_t clip, uint16_t *fb, int stride)
{
    omni_rect_t r = box;   /* box and clip */
    if (r.x0 < clip.x0) r.x0 = clip.x0;
    if (r.y0 < clip.y0) r.y0 = clip.y0;
    if (r.x1 > clip.x1) r.x1 = clip.x1;
    if (r.y1 > clip.y1) r.y1 = clip.y1;
    /* the box is the leaf's (M1): rows holds lh rows, and every box row and column has one */
    if (box.x1 - box.x0 != lw || box.y1 - box.y0 != lh || lw <= 0 || lh <= 0 || lw > N || lh > N) return;
    if (r.x0 >= r.x1 || r.y0 >= r.y1) return;
    fk_rows_edge_t fe;
    fk_rows_edge(st, rows, lh, lw, lh, &fe);
    const edge_t e = edge_of(st, fe.L);
    const int ea = fe.x0, ez = fe.x1;   /* box-local */
    const uint32_t sq = 256u - (uint32_t)omni_amul(st->shade);
    const int xa = r.x0 - box.x0, xz = r.x1 - box.x0;   /* box-local columns written */
    const int qa = ea > xa ? ea : xa, qz = ez < xz ? ez : xz;
    for (int y = r.y0; y < r.y1; y++) {
        const int yl = y - box.y0;
        uint16_t *o = fb + (ptrdiff_t)(y - clip.y0) * stride + (r.x0 - clip.x0);   /* o[0]: pixel (r.x0, y) */
        const fk_row_t *row = &rows[yl];
        if (row_leaf(row, lh)) leaf_span(o, xa, xz, leaf + (size_t)row->v * (size_t)lw, lw, row->inv_s_q16, sq);
        if (qa < qz) edge_row(st, &e, yl, o + (qa - xa), qa, qz);
    }
}
