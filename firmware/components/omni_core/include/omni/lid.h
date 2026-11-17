#pragma once
/* The lid flip's hinge engine (spec §7.4, the owner's raster-line technique; Stage 3 Task 9). A flat leaf hinged on a
 * screen line swings toward the viewer; each output line of the screen is a scaled copy of one line of the leaf's
 * bitmap, found by a reciprocal (linear-fractional) map, plus a per-line shade. No stored frames. Parameterised by axis
 * (R73), so a later theme's double doors or fob lid reuse it; the Omni lid is its only Stage 3 caller. Pure C, single
 * precision in a fixed order (-ffp-contract=off): the device builds the host's tables.
 * The per-line maths is the same for either axis (R98): axis only says which way a renderer lays the lines, and
 * nothing here reads it. Stage 3 renders the horizontal axis only (omni_dial's lid layer); the column renderer the
 * vertical axis needs is a deliverable of the themes stage (the call-box doors), not built yet. */
#include <stdbool.h>
#include <stdint.h>

/* A flat leaf hinged on a screen line, rotated by theta toward the viewer, focal F. axis HORIZONTAL: the hinge is the
 * row y = pos and each output line is a screen row; VERTICAL: the hinge is the column x = pos and each line is a
 * column (the same maths transposed). C = n / 2, the eye's line. For line y (distance along the leaf from the hinge
 * measured as v - pos):
 *   v = (a y + b) / (c y + d) + pos,  a = F, b = -F pos, c = sin(theta), d = F cos(theta) - C sin(theta);
 *   scale across the line s = F / (F - (v - pos) sin(theta)); source u = C + (x - C) / s.
 * This is the exact inverse of the mockup's proj() (drawLid 893) with its dome height Hd = 0 (plan D4: the dome is
 * carried by the shading, not the geometry). dome_r and dome_tilt shape the shading's dome (below): dome_r 0 is a flat
 * leaf (no dome term, for a door). The Omni lid is { HORIZONTAL, pos -6, F 800, n 466, dome_r 226, dome_tilt .55 }. */
typedef enum { OMNI_HINGE_HORIZONTAL, OMNI_HINGE_VERTICAL } omni_hinge_axis_t;
typedef struct { omni_hinge_axis_t axis; float pos, focal; uint16_t n; float dome_r, dome_tilt; } omni_hinge_t;
typedef struct { float a, b, c, d; } omni_hinge_map_t;
void omni_hinge_map(const omni_hinge_t *h, float theta_rad, omni_hinge_map_t *m);

/* One output line: v the source line (< 0: no leaf on this line), inv_s_q16 = 1 / s in Q16 (65536 = 1.0: uint32, as
 * 1.0 itself is a value it takes, at theta 0), and the mockup's per-strip shading x 255. */
typedef struct { int16_t v; uint32_t inv_s_q16; uint8_t dark, spec; } omni_hinge_line_t;
/* Per output line (h->n of them), each at its centre y + 0.5: the map's continuous source coordinate v' (v = floor(v'),
 * no leaf when the line is past the leaf's vanishing line or v' is outside [0, n)), 1 / s at v', and the mockup's
 * per-strip shading at v' (beta = asin((v' - C) / dome_r) * dome_tilt, 0 for dome_r 0; n = theta + beta, dark =
 * clamp(1 - cos(n + .5) / cos(beta + .5), 0, .6), spec = (exp(-((n - .38) / .14)^2) .5 + exp(-((n - 1.05) /
 * .22)^2) .22) clamp(6 theta, 0, 1); each 0 at or below .01, as the mockup skips those draws) x 255, rounded. The same
 * lines for either axis. */
void omni_hinge_lines(const omni_hinge_t *h, float theta_rad, omni_hinge_line_t *lines);
/* Lines [y0, y1) only, into lines[0 .. y1 - y0), each exactly as omni_hinge_lines gives it (a renderer builds a band's
 * lines with the band, so two cores share the cost). */
void omni_hinge_lines_at(const omni_hinge_t *h, float theta_rad, int y0, int y1, omni_hinge_line_t *lines);
extern const omni_hinge_t OMNI_LID_HINGE;   /* { OMNI_HINGE_HORIZONTAL, -6, 800, 466, 226, 0.55 } */
/* The angle past which the leaf leaves the screen: from atan2(F, C - pos) on, every line is past its vanishing line or
 * off the leaf (v - pos >= F / sin(theta) > n). The Omni lid's is 73.37 deg (its flat geometry, D4; the mockup's
 * domed lid lingers to 90). */
float omni_hinge_gone_rad(const omni_hinge_t *h);

/* The timeline as named constants (R72/C15, plan D3): pop up to +4 deg by outCubic over [POP_T0, UP), settle -1.5 deg
 * by inOutCubic over [UP, LATCH), swing +108 deg by inOutCubic over [SWING_T0, SWING_T0 + SWING). The device's: pop
 * from 0, UP 60, LATCH 150, the swing from the latch, SWING 600 (theta reaches 90 deg at about 533 ms and all ends at
 * 750; the flat lid leaves the screen at 73.37 deg, about 485 ms: omni_hinge_gone_rad).
 * Defining OMNI_FLIP_MOCKUP (to 1) selects the mockup's 280-380 / 380-540 / 580-1640 instead, for the owner's A/B
 * (Task 14): CONFIG_OMNI_FLIP_MOCKUP on the device (Kconfig, final review M3), the test_lid_mockup build on the host;
 * OMNI_ENTRY_FLIP follows it (omni_model.h). */
#if defined(OMNI_FLIP_MOCKUP) && OMNI_FLIP_MOCKUP
#define OMNI_FLIP_POP_T0_MS 280
#define OMNI_FLIP_UP_MS 380
#define OMNI_FLIP_LATCH_MS 540
#define OMNI_FLIP_SWING_T0_MS 580
#define OMNI_FLIP_SWING_MS 1060
#else
#define OMNI_FLIP_POP_T0_MS 0
#define OMNI_FLIP_UP_MS 60
#define OMNI_FLIP_LATCH_MS 150
#define OMNI_FLIP_SWING_T0_MS OMNI_FLIP_LATCH_MS
#define OMNI_FLIP_SWING_MS 600
#endif
#define OMNI_FLIP_FADE_MS 300        /* the mockup's fadeIn(0, 300) from black, on the wake's own time (not t x 2.2) */
#define OMNI_FLIP_REDUCED_K 2.2f     /* reduced motion runs the timeline at t x 2.2 (the mockup's tt) */

/* theta (radians) t_ms after the wake: the on-device timeline (decision D3); reduced: at t x 2.2 */
float omni_flip_theta(uint32_t t_ms, bool reduced);
/* the black over the dial as the lid opens: .92 (1 - outCubic(ph(theta, 2 deg, 80 deg))) */
float omni_flip_dim(float theta_rad);
/* the fade from black: 1 - outCubic(ph(t, 0, OMNI_FLIP_FADE_MS)) (the mockup's fadeIn, over the lid and the dial) */
float omni_flip_fade(uint32_t t_ms);
/* the spec gradient (buildLid 888-890) by source column, x 255: its alpha at the column's centre (stops .15 / .8 at
 * .3 / 1 at .42 / .55 at .7 / .1 at 1); the compositor adds it in (255, 240, 205) */
void omni_lid_spec_cols(uint8_t cols[466]);
