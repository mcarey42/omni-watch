#pragma once
/* The thumbnail ring (watch_ui internal): design/mockups/faces-setup.js drawThumb() on the picker's ring (nav-map's
 * frame()), moved out of scr_picker.c for the picker and the carousel editor. The bezel screen (the picker's brass
 * background, a full-screen draw object and the magnifier window at 12), the thumbnails painted once per open into
 * two small PSRAM canvases each (at rest size with the ring, and at the largest size, scaled while an item nears the
 * window), their drawing round the ring and their invalidation boxes, and the red V under the window. LVGL task only. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "omni/input.h"
#include "omni/settings.h"

#define THUMBRING_MAX (OMNI_CAROUSEL_MAX + 1)   /* every face, and the picker's Settings star */
#define THUMBRING_R 182                          /* NAV.R: thumbnail centres */
#define THUMBRING_TR 27                          /* NAV.tr: a thumbnail's radius at rest */
#define THUMBRING_GAP 0.75f                      /* the editor's gap before its hidden faces, in slots (FE_GAP; R207) */

/* id: the theme's face id (ignored for Settings); opa: the thumbnail's brightness (a black wash of 255 - opa over it;
 * on the editor's ring, the one thumbring_order set, it lifts as the item reaches the window: R207, R227); numeral: the
 * small brass order numeral on its inner edge, -1 none. A face without a
 * registered view (shell_face_ready) also gets the mockup's "planned" wash. */
typedef struct { uint8_t id; bool settings; lv_opa_t opa; int8_t numeral; } thumbring_item_t;

/* The bezel screen: root (on parent), the picker's background, the full-screen draw object (draw_cb on
 * LV_EVENT_DRAW_MAIN) and the window above it. */
void thumbring_screen(lv_obj_t *parent, lv_event_cb_t draw_cb, lv_obj_t **root, lv_obj_t **draw);
/* The canvases, once per open (PSRAM): the ring is items[0 .. n - 1] in this order, evenly spaced. */
void thumbring_build(const thumbring_item_t *items, int n);
/* The built items again in a new order, with new opa and numerals (each keeps the pictures built for its face): items
 * from gap_at on sit THUMBRING_GAP slots further round, the gap taken from the slot arcs (gap_at >= n: no gap). */
void thumbring_order(const thumbring_item_t *items, int n, int gap_at);
/* One slot's angle (radians), item i's angle past the window's at rot 0, and the item nearest the window at rot. */
float thumbring_step(void);
float thumbring_slot(int i);
int thumbring_nearest(float rot);
/* The screens' small helpers, one copy (clamp; the mockups' smooth step; an angle into (-pi, pi]). */
float thumbring_clampf(float v, float lo, float hi);
float thumbring_smooth(float t);
float thumbring_wrapf(float a);
/* Every thumbnail at ring angle rot (rot = -thumbring_slot(i) puts item i under the window); sel: the landed selection (its
 * brassHi rim, and no numeral), -1 none. */
void thumbring_draw(lv_layer_t *layer, float rot, int sel);
/* The red V index under the window. */
void thumbring_index(lv_layer_t *layer);
/* Item i's box at ring angle rot (what to invalidate when it moves). */
void thumbring_item_box(int i, float rot, lv_area_t *a);
void thumbring_free(void);

/* The engraved scale turning with the ring (16 marks of r 216 .. 220/223/226 per slot run, sub per slot): a major at each
 * of the first majors slots (opa major_opa), a mid at each half slot. tick: one such mark (or any radial line) at a. */
void thumbring_scale(lv_layer_t *layer, float rot, int sub, int majors, lv_opa_t major_opa);
void thumbring_tick(lv_layer_t *layer, float a, float r0, float r1, int32_t w, uint32_t rgb, lv_opa_t opa);

/* Turning (nav-map's pointer()): the ring's angle, where it springs to, the flick's velocity, the angle last drawn. */
typedef struct { float rot, target, vel, drawn_rot, last_a; uint32_t last_t, t_prev; bool drag; } thumbring_turn_t;
/* G_DRAG_START / MOVE / END (a rim drag the screen claimed): the ring follows the finger. 1 on END (drag over: the screen
 * picks its target from thumbring_coast), else 0. t: when the input task saw the point. */
int thumbring_drag(thumbring_turn_t *m, const omni_gesture_t *g, uint32_t t);
float thumbring_coast(const thumbring_turn_t *m);   /* where the flick would carry the ring (at most 2.5 slots) */
/* One anim tick at now: springs toward target (the mockup's 0.18 per 60 Hz frame) unless dragging; true if it moved. */
bool thumbring_spring(thumbring_turn_t *m, uint32_t now);
/* Invalidates what turning moved on draw (each item's box at drawn_rot and at rot, the scale in 16 arc pieces: LVGL keeps
 * 32 invalid areas before it falls back to the whole screen), then drawn_rot = rot. Nothing when they are equal. */
void thumbring_inval(lv_obj_t *draw, thumbring_turn_t *m);
/* The item a tap at (x, y) outside the r 146 centre lands on (within r + 14 of its centre), or -1. */
int thumbring_hit(float rot, int16_t x, int16_t y);
