#pragma once
/* The drum (watch_ui internal): design/mockups/faces-setup.js `settings` frame(). Rows on a drum round the screen's
 * centre; a centre drag rolls it (with a flick's momentum), a tap above or below steps it, and the row nearest the
 * middle sits in the brass window. Its motion, its drawing by position (each row through the screen's callback) and
 * its position pips. Settings uses it; the network list and the city results reuse it. LVGL task only. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "omni/input.h"
#include "theme.h"

#define DRUM_ROW_H 54   /* px per row at the window */
/* The brass window the windowed row sits in, and its frame (theme_window renders it once per spec: one shared spec,
 * one PSRAM copy for every screen on the drum). */
#define DRUM_WIN_X 76
#define DRUM_WIN_Y (THEME_CY - 28)
#define DRUM_WIN_W 314
#define DRUM_WIN_H 56

/* Position s, target, drag state, velocity; static storage owned by the screen. Read-only outside drum.c. */
typedef struct drum {
    int n;                    /* rows */
    float s, target;          /* drum position in rows (s == i: row i in the window) */
    bool drag;                /* a centre drag is rolling it */
    float s0, v;
    int16_t y0, last_y;
    uint32_t last_t;
    uint8_t pips_max;         /* appended (the network list): no pips when n is above it; 0 (drum_init): always */
} drum_t;

/* One row: i (0 .. n - 1) at baseline-centre y, at opacity opa; in_window: the row is in the window (draw it the
 * window's way), else a dimmed label whose size drum_scale gives. */
typedef void (*drum_row_fn)(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa, bool in_window, void *ctx);

extern const theme_window_t DRUM_WINDOW;

void drum_init(drum_t *d, int n, int start);
/* The rows by position (row NULL: none, as under a sheet), then the pips (unless n > pips_max, when set). */
void drum_draw(const drum_t *d, lv_layer_t *layer, drum_row_fn row, void *ctx);
bool drum_anim(drum_t *d, uint32_t dt_ms);                 /* true while moving (the caller invalidates) */
void drum_drag(drum_t *d, const omni_gesture_t *g, uint32_t t);   /* G_DRAG_START/MOVE/END from the centre */
int drum_tap(drum_t *d, int16_t y);   /* -1: the window (the caller acts); else steps toward y, returning the target */
int drum_cur(const drum_t *d);                             /* the row index in the window */
float drum_scale(const drum_t *d, int i);                  /* row i's size against the window's (1 there) */

/* ---- the drum screen (R223(c), Task 12): what every screen on the drum shares (the network list, the keyboard's city
 * results; Settings keeps its own). */
/* The row label off the window at px (16 x drum_scale): Plex Sans 400 14 / 13 / 12 / 11. */
const lv_font_t *drum_dim_font(float px);
/* The drum's part of a gesture, outside a centre of r rim_r (the rim): a drag that starts inside it rolls the drum (and
 * its MOVE and END while it does), a tap inside it steps toward the tap or hits the window. */
enum { DRUM_G_NONE, DRUM_G_MOVED /* invalidate the drum */, DRUM_G_WINDOW /* a tap on the windowed row: act on drum_cur */ };
int drum_gesture(drum_t *d, const omni_gesture_t *g, uint32_t t, int32_t rim_r);
void drum_inval(lv_obj_t *draw);   /* the drum's rows and pips (CX +-165, 96 .. 392) */

/* The screen's objects: a root on THEME_BG_SETTINGS, the full-screen draw object (draw_cb on LV_EVENT_DRAW_MAIN), the
 * brass window (frame), the anim timer at ANIM_MS (16 ms), and the touch claim (want: OMNI_GW_*). */
typedef struct { lv_obj_t *root, *draw, *frame; lv_timer_t *anim; } drum_screen_t;
void drum_screen_create(drum_screen_t *s, lv_obj_t *parent, lv_event_cb_t draw_cb, lv_timer_cb_t anim_cb,
                        bool (*claim)(int16_t, int16_t, void *), uint8_t want);
void drum_screen_destroy(drum_screen_t *s);   /* the claim cleared, the timer and the objects deleted; s zeroed */
void drum_screen_dim(const drum_screen_t *s, bool on);   /* 40 % (idle, glance) */
