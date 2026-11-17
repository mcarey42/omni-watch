#pragma once
/* The provisioning step art and copy (watch_ui internal): design/mockups/faces-setup.js `wifi-qr` (phoneGlyph,
 * wifiGlyph, checkGlyph, the working comet on the progress ring) and the headings and lines of each step, moved out of
 * scr_provision.c so the keyboard's in-place join result (spec 7.2) draws the same art. Absolute screen coordinates in
 * a draw callback; LVGL task only. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "net.h"

#define STEPART_RING_R 158   /* the progress ring round the centre */

/* The Wi-Fi mark: three arcs (r 10/21/32) over a dot, centred on x; animate: the arcs pulse with t (lv_tick ms). */
void stepart_wifi(lv_layer_t *layer, int32_t x, int32_t y, uint32_t rgb, bool animate, uint32_t t);
/* The green check in its ring, drawn to k (0..1: the ring sweeps, then the tick). */
void stepart_check(lv_layer_t *layer, int32_t x, int32_t y, float k);
/* The phone outline (26 x 44) centred on (x, y). */
void stepart_phone(lv_layer_t *layer, int32_t x, int32_t y);
/* The phone outline at any size (R223(c)): half width and height, corner radius, the bar's half length and its offset
 * below the centre, in rgb (stroke 2). stepart_phone is (13, 22, 5, 4, 17, THEME_SETUP_BRASS_HI). */
void stepart_phone_at(lv_layer_t *layer, int32_t x, int32_t y, int32_t hw, int32_t hh, int32_t r, int32_t bar, int32_t bar_dy,
                      uint32_t rgb);
/* The working comet chasing round the progress ring (STEPART_RING_R) at t (lv_tick ms), in rgb. */
void stepart_ring(lv_layer_t *layer, uint32_t t, uint32_t rgb);
/* A comet (R223(c): stepart_ring's, the network list's listening sweep): n pieces of step radians behind the head a
 * (canvas radians), each overlapping the next by lap, on an arc of radius r and width w, at head_opa (0..1) fading
 * linearly; the first piece that would start before stop ends it. */
void stepart_comet(lv_layer_t *layer, int32_t r, int32_t w, float a, int n, float step, float lap, float head_opa,
                   uint32_t rgb, float stop);

/* The copy of a join attempt's steps, as the phone path (a provisioning session) shows them: head is the heading;
 * line1 the step's guidance line (NULL: none); line2 a second line (NULL: none): for a result that did not join
 * (wrong password, no network, Wi-Fi did not start), the spec 7.6 line: "Nothing was saved" without had_saved, else
 * "Your saved network is unchanged" (saved_kept) or "... was not kept". The network's name is the caller's. */
typedef struct { const char *head, *line1, *line2; uint32_t head_rgb; } stepart_copy_t;
void stepart_copy(omni_net_state_t st, bool saved_kept, bool had_saved, stepart_copy_t *out);
