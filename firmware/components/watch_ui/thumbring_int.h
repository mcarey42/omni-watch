#pragma once
/* thumbring's two halves (watch_ui internal, R228): thumbring.c draws (-O2: the picker's pixel path) and thumbring_ops.c
 * keeps the ring, its order and its turning (-Os). LVGL task only. */
#include <math.h>
#include "thumbring.h"
#include "theme.h"
#include "watch_theme.h"

#define CX THEME_CX
#define CY THEME_CY
#define TAU (2 * (float)M_PI)
#define RING_R THUMBRING_R
#define THUMB_R THUMBRING_TR
#define THUMB_RMAX (THUMB_R * 1.18f)
#define THUMB_PX WATCH_THUMB_PX /* canvas side at THUMB_RMAX, and the exported thumbnails' side (the face circle) */
#define REST_PX 60          /* canvas side at THUMB_R, ring and planned wash included */
#define WIN_X (CX - 41)     /* the magnifier window at 12 */
#define WIN_Y 6
#define WIN_W 82
#define WIN_H 78

typedef struct {
    thumbring_item_t it;     /* as built (the picture: id, settings) */
    bool live;
    lv_draw_buf_t *thumb;    /* at THUMB_RMAX, scaled while it nears the window */
    lv_draw_buf_t *rest;     /* at THUMB_R with its ring (and wash): drawn as is, no transform */
} item_t;

/* PSRAM, from thumbring_build to thumbring_free (no internal RAM for a screen that is mostly closed) */
typedef struct {
    lv_obj_t *canvas;        /* offscreen painter, only while building */
    item_t items[THUMBRING_MAX];
    thumbring_item_t ring[THUMBRING_MAX];   /* the ring's order (opa, numeral) and, in p, each one's pictures */
    uint8_t p[THUMBRING_MAX];
    bool lift;               /* the editor's ring (thumbring_order): a dimmed face lifts to full in the window (R207;
                              * the picker's stay at their opa, R227) */
    int n, built, gap_at;    /* built: pictures painted; items from gap_at on sit THUMBRING_GAP slots further round (gap_at >= n: no gap) */
} ring_t;
extern ring_t *thumbring_r;   /* PSRAM, from thumbring_build to thumbring_free */
#define R thumbring_r

static inline float total(void) { return R->n + (R->gap_at < R->n ? THUMBRING_GAP : 0); }
static inline float step(void) { return TAU / total(); }
static inline float slot(int i) { return i + (i >= R->gap_at ? THUMBRING_GAP : 0); }
/* Both pictures of an item (img: its exported thumbnail, or NULL), into its buffers, on R->canvas. */
void thumbring_paint(const item_t *it, const lv_image_dsc_t *img);
