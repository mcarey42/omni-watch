#pragma once
/* Rect lists (Stage 3 Task 7): the dial view's dirty and render lists. Integer only. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"

#define OMNI_RECTS_MAX 64
typedef struct { omni_rect_t r[OMNI_RECTS_MAX]; int n; } omni_rects_t;

int32_t omni_rect_area(const omni_rect_t *r);               /* 0 for an empty rect */
omni_rect_t omni_rect_unite(omni_rect_t a, omni_rect_t b);   /* the bounding rect; an empty one adds nothing */
/* Adds r (if not empty and not inside one already there), merging any pair whose union costs no more than the two
 * (omni_dial_dirty's rule). From 24 rects on (the disc's bands of two frames) it appends without merging, which is
 * quadratic; a full list grows its last rect. The list then covers every pixel added, and maybe more. */
void omni_rects_add(omni_rects_t *L, omni_rect_t r);
uint32_t omni_rects_px(const omni_rects_t *L);               /* the rects' areas summed (an overlap counts twice) */
/* a less b, appended to out as up to 4 rects (the rows above and below b, the parts left and right of it); a when they
 * do not meet, nothing when a is empty. false if out is full (out then holds a part). */
bool omni_rect_minus(const omni_rect_t *a, const omni_rect_t *b, omni_rects_t *out);
/* Every rect of A less hole, appended to out (A's order, 4 pieces at most each). false if they do not all fit: out is
 * then left as it was, and the caller renders A whole instead (final review M7: a dropped piece keeps stale pixels). */
bool omni_rects_minus(const omni_rects_t *A, const omni_rect_t *hole, omni_rects_t *out);
/* out: L's pixels as rects no two of which overlap. A list too fragmented to split within OMNI_RECTS_MAX becomes its
 * bounding rect (disjoint still, and a superset). The dial view's two cores render one list's chunks at once, and every
 * step of a render reads the pixels it blends over: two overlapping rects rendered at the same time would blend one
 * pixel twice or from a half-drawn state. out must not be L. */
void omni_rects_disjoint(const omni_rects_t *L, omni_rects_t *out);
