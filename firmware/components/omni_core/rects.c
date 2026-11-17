#include "omni/rects.h"

int32_t omni_rect_area(const omni_rect_t *r)
{
    return r->x1 > r->x0 && r->y1 > r->y0 ? (int32_t)(r->x1 - r->x0) * (r->y1 - r->y0) : 0;
}

omni_rect_t omni_rect_unite(omni_rect_t a, omni_rect_t b)
{
    if (!omni_rect_area(&a)) return b;
    if (!omni_rect_area(&b)) return a;
    return (omni_rect_t){ a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1,
                          a.y1 > b.y1 ? a.y1 : b.y1 };
}

void omni_rects_add(omni_rects_t *L, omni_rect_t r)
{
    if (!omni_rect_area(&r)) return;
    for (int i = 0; i < L->n; i++)   /* already covered */
        if (L->r[i].x0 <= r.x0 && L->r[i].y0 <= r.y0 && L->r[i].x1 >= r.x1 && L->r[i].y1 >= r.y1) return;
    if (L->n >= 24 || L->n == OMNI_RECTS_MAX) {
        if (L->n < OMNI_RECTS_MAX) L->r[L->n++] = r;
        else L->r[L->n - 1] = omni_rect_unite(L->r[L->n - 1], r);
        return;
    }
    L->r[L->n++] = r;
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < L->n && !again; i++)
            for (int j = i + 1; j < L->n && !again; j++) {
                const omni_rect_t u = omni_rect_unite(L->r[i], L->r[j]);
                if (omni_rect_area(&u) <= omni_rect_area(&L->r[i]) + omni_rect_area(&L->r[j])) {
                    L->r[i] = u;
                    L->r[j] = L->r[--L->n];
                    again = true;
                }
            }
    }
}

uint32_t omni_rects_px(const omni_rects_t *L)
{
    uint32_t n = 0;
    for (int i = 0; i < L->n; i++) n += (uint32_t)omni_rect_area(&L->r[i]);
    return n;
}

bool omni_rect_minus(const omni_rect_t *a, const omni_rect_t *b, omni_rects_t *out)
{
    if (!omni_rect_area(a)) return true;
    if (!omni_rect_area(b) || b->x1 <= a->x0 || b->x0 >= a->x1 || b->y1 <= a->y0 || b->y0 >= a->y1) {
        if (out->n >= OMNI_RECTS_MAX) return false;
        out->r[out->n++] = *a;
        return true;
    }
    const int16_t y0 = b->y0 > a->y0 ? b->y0 : a->y0, y1 = b->y1 < a->y1 ? b->y1 : a->y1;
    const omni_rect_t p[4] = { { a->x0, a->y0, a->x1, y0 }, { a->x0, y1, a->x1, a->y1 },
                               { a->x0, y0, b->x0 > a->x0 ? b->x0 : a->x0, y1 }, { b->x1 < a->x1 ? b->x1 : a->x1, y0, a->x1, y1 } };
    for (int i = 0; i < 4; i++) {
        if (!omni_rect_area(&p[i])) continue;
        if (out->n >= OMNI_RECTS_MAX) return false;
        out->r[out->n++] = p[i];
    }
    return true;
}

bool omni_rects_minus(const omni_rects_t *A, const omni_rect_t *hole, omni_rects_t *out)
{
    const int n0 = out->n;
    for (int i = 0; i < A->n; i++)
        if (!omni_rect_minus(&A->r[i], hole, out)) {
            out->n = n0;
            return false;
        }
    return true;
}

void omni_rects_disjoint(const omni_rects_t *L, omni_rects_t *out)
{
    out->n = 0;
    for (int i = 0; i < L->n; i++) {
        omni_rects_t pieces = { .n = 0 };
        if (omni_rect_area(&L->r[i])) pieces.r[pieces.n++] = L->r[i];
        for (int k = 0; k < out->n && pieces.n; k++) {
            omni_rects_t next = { .n = 0 };
            for (int j = 0; j < pieces.n; j++)
                if (!omni_rect_minus(&pieces.r[j], &out->r[k], &next)) goto whole;
            pieces = next;
        }
        for (int j = 0; j < pieces.n; j++) {
            if (out->n >= OMNI_RECTS_MAX) goto whole;
            out->r[out->n++] = pieces.r[j];
        }
    }
    return;
whole:
    out->n = 0;
    omni_rect_t u = { 0, 0, 0, 0 };
    for (int i = 0; i < L->n; i++) u = omni_rect_unite(u, L->r[i]);
    if (omni_rect_area(&u)) out->r[out->n++] = u;
}
