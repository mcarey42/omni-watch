#pragma once
/* A dw face as pure code (dw wave 1): what the app hands a face each frame (fk_face_in_t) and what a face
 * gives back (fk_face_ops_t). face_kit's runtime (fk_face.c, rd wave 1 Task 2; face_generic.c until then) turns one
 * fk_face_ops_t into the shell's omni_face_vt_t, and its fk_view.c draws it into a PSRAM canvas and runs its reveal over
 * it (a doors face sets reveal_ops = &DW_REVEAL_DOORS, dw/doors.h, with its reveal_asset and its door kind as
 * reveal_arg; every other face leaves it NULL). Task 11: the clock helper (dw/clock.h), the
 * raster-polygon hands, face 1 (Crystal Chamber), the registry and the pinned still frames that dw_shot, the host
 * tests and the device's `dw verify` share. Pure: no esp_*, freertos or LVGL; no allocation in a draw path. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "dw/clock.h"
#include "dw/doors.h"
#include "fk/annulus.h"
#include "fk/compose.h"
#include "fk/face.h"     /* the face interface, its runtime helpers and the still CRCs (face_kit, rd wave 1 Task 1) */
#include "fk/shapes.h"   /* the generic shapes the hands build on */
#include "omni/geom.h"
#include "omni/lid.h"

extern const fk_face_ops_t DW_FACE_CRYSTAL, DW_FACE_ROUNDEL, DW_FACE_CALLBOX, DW_FACE_ROTOR, DW_FACE_CORAL;
extern const fk_face_ops_t DW_FACE_CIRCLES, DW_FACE_LEARN, DW_KEY;   /* Task 15; DW_KEY draws the pushed screen */
extern const fk_face_ops_t DW_FACE_ROOM;   /* wave 2 Task 5: #2, after DW_KEY (the wave-2 faces follow it) */
extern const fk_face_ops_t DW_FACE_AMBER;  /* wave 2 Task 6: #10 */
/* wave 2 Task 7: #13, "fob"; cadence_ms 40 (R147 PR4: the lid and the tap run on this timer; at rest dirty() is
 * DW_F13_SUB only) */
extern const fk_face_ops_t DW_FACE_FOB;
/* wave 2 Task 9: #9, "console"; cadence_ms 40 (the seconds dial's tick, the minutes needle, the in-flight lamp), wake_len_ms
 * 1570 (the needles' staggered spring) */
extern const fk_face_ops_t DW_FACE_CONSOLE;
/* wave 2 Task 10: #6, "wall"; cadence_ms 40 (the current minute's roundel breathes), wake_len_ms 800 (the wall powers up
 * from 12) */
extern const fk_face_ops_t DW_FACE_WALL;
/* wave 2 Task 11: #3, "doorway"; cadence_ms 40 (the column's breath), doors (the Modern doors, ASSET_DW_DOORS_M: the
 * build's art set) inward, the leaves kept (DW_DOORS_IN_KEEP) */
extern const fk_face_ops_t DW_FACE_DOORWAY;
/* wave 2 Task 12: #8, "broadcast"; cadence_ms 0 (the ring and the hum bar step once a second, on the shell's update),
 * wake_len_ms 450 (the switch-on; face_generic's 40 ms timer runs only while it does, R147 PR6) */
extern const fk_face_ops_t DW_FACE_BROADCAST;
/* wave 2 Task 14: #12, "vortex"; cadence_ms 50 (the canvas's rate when the direct path is refused), .direct (its tunnel
 * through the direct path at the panel's rate), wake_len_ms 900 (the fade-in), pulse_len_ms 1900 (the palette's name),
 * a tap anywhere FK_TAP_CYCLE (the next palette) */
extern const fk_face_ops_t DW_FACE_VORTEX;
/* Every built face, in face-id order of their tasks (Tasks 13-15 append): the harness's table, dw_shot's --face and
 * the console's `dw verify` all read this. */
extern const fk_face_ops_t *const DW_FACES[];
extern const unsigned DW_FACE_COUNT;
const fk_face_ops_t *dw_face_by_key(const char *key);   /* NULL if none */

/* ---- The still frames every face is checked on (spec section 9: every still frame is exact). Pinned inputs, TZ-free
 * (the clock fields themselves), settled (wake_ms UINT32_MAX): dw_shot --crc writes their CRCs to dw/verify_crc.h,
 * the host tests compare, and the device's `dw verify` draws the same inputs over its mapped assets. Never a frame in
 * motion (doors.h: only still frames are compared across host and device). */
typedef struct { fk_face_in_t in; bool glance; const char *what; } dw_still_t;
/* dw/verify_crc.h's and the overlay's verify_crc_overlay.h's row type (dw_shot --crc writes it): face_kit's row (rd wave
 * 1 Task 1), under the name both generated tables carry, so neither changes. */
typedef fk_verify_t dw_verify_t;
#define DW_STILL_N 8
void dw_still(unsigned i, dw_still_t *out);   /* i < DW_STILL_N */
/* (Task 15) A face's stills: DW_STILL_N per page (ops->pages, at least one). Still i is dw_still(i % DW_STILL_N) on page
 * i / DW_STILL_N; verify_crc.h's rows number them so. */
unsigned dw_still_count(const struct fk_face_ops *ops);
/* The whole 466 x 466 frame of `ops` for still i (< dw_still_count) into fb (stride 466): draw, or glance for a glance
 * still, on page i / DW_STILL_N. */
void dw_still_frame(const fk_face_ops_t *ops, const void *ctx, unsigned i, uint16_t *fb);

/* ---- Hands and the faces' small shapes (hands.c): raster polygons (omni/raster.h) and analytic discs and glows,
 * every angle through the in-tree sine table (omni/trig.h), so host and device draw the same bytes; each shape is a
 * pure function of the pixel (band-exact). Angles are radians clockwise from 12 o'clock; the screen centre is 233. */
/* Raster-polygon hands (spec section 11 [call]); each hand <= OMNI_POLY_MAX points per part. The crystal shard of
 * faces-reveal.js shard(): a 3 px dark outline, the dark facet, the lit left facet, a white ridge; the minute hand
 * also lays its warm lance glow along itself (additive). grow (crystalHands' k, 0 .. about 1.1) scales the length;
 * grow <= 0 draws nothing. a8: the hand's opacity (255; the faces dim by colour, never below 255 on a numeral). */
void dw_hand_shard(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, float grow, uint8_t a8);
void dw_hand_shard_bbox(float a, bool hour, omni_rect_t *out);   /* every pixel it can touch at grow <= 1.2 */
/* #2's hands (wave 2 Task 5; faces-reveal.js classicHands): a baton from its tail (16 / 20 px behind the centre) to
 * 108 / 162 px x grow, 9 / 6.5 px wide, its tip a point; a 3 px black outline (stroked under the fill, so 1.5 px
 * shows), the fill #f4eedf, the dark centre line (1.2 px, #1a1a1a, from 0.9 to 0.4 of the length). grow <= 0 draws
 * nothing. */
void dw_hand_room(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, float grow, uint8_t a8);
void dw_hand_room_bbox(float a, bool hour, omni_rect_t *out);   /* every pixel it can touch at grow <= 1.2 */
/* #10's hands (wave 2 Task 6; faces-modern.js shardHand, the wand hand, caHands). dw_hand_amber: the amber crystal shard,
 * 100 / 150 px long, 16 / 12 px wide, its tail 18 / 22 px behind the centre: the lit facet (left: #A85921, #FFB45C at
 * .55, #FFD38A) and the ember facet (#5E2C1F, #A85921 at .6, #CA742B), each a gradient from the pivot to the tip's
 * height; the hot fracture line, the ridge, the 1 px outline, and the lit facet's shadow (rgba(255, 150, 60, .8),
 * blur 10) as a short additive halo along the hand. a8: its opacity (255). dw_hand_amber_bbox: every pixel it
 * touches. */
void dw_hand_amber(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, uint8_t a8);
void dw_hand_amber_bbox(float a, bool hour, omni_rect_t *out);
/* The wand seconds hand (168 px, its tail 34): the dark grip, the ribbed steel body, the copper collar, the claws and
 * the cyan tip at 151.2 px; under it the tip's glow and its trail: 5 glows of r 26 in (127, 232, 255) at a - k x 0.022
 * rad, each about its whole-pixel centre (rounded; fk_glow_add_lut, R147 PR15), at .9 x .95 x pulse for k 0 and
 * .9 x .22 (1 - k / 5) for the others (glowSprite's .9 times caHands' alphas). dw_hand_wand_rects: every pixel it
 * touches (for any pulse <= 1), as 4 boxes along it, the last holding the tip and every trail glow. */
void dw_hand_wand(uint16_t *fb, int stride, const omni_rect_t *clip, float a, float pulse);
void dw_hand_wand_rects(float a, omni_rect_t out[4]);
/* #13's Breguet hands (wave 2 Task 7; faces-modern.js fwHands breguet, :853-863): the shaft w px wide (butt ends) from
 * 18 px behind the centre to .72 len, the hollow moon (a ring r .07 len, 2 px wide, at .78 len) and the tip (the
 * triangle (-.9 w, -.84 len), (0, -len), (.9 w, -.84 len)); the hour hand 112 / 4.5, the minute hand 170 / 3.2; rgb at
 * a8 (255). dw_hand_breguet_bbox: every pixel it touches. */
void dw_hand_breguet(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, uint16_t rgb,
                     uint8_t a8);
void dw_hand_breguet_bbox(float a, bool hour, omni_rect_t *out);
/* #9's shapes (wave 2 Task 9). dw_dome_lamp: faces-classic.js domeLamp (:149-162) about (x, y) r: when on > 0 the spill
 * (rgb at .35 on falling linearly to 0 at 3 r), the dome (a radial gradient from (x - .35 r, y - .4 r): #fff, rgb at
 * .4 + .6 on, rgb at .25 + .3 on; off: #3a3a36, #1c1c1a, #0b0b0a) and the ring at r + 1.5 (1 px, rgba(184, 200, 176,
 * .45)); every pixel it touches lies within 3 r + 2 of (x, y) lit, r + 4.5 dark. dw_p9_needle: meterNeedle (:922-932) of
 * meter i (DW_P9_XY, r 36) at value v (clamped to -.03 .. 1.03 of the scale's 110 degrees from -145, MET a0 / a1): the
 * pivot at y + r .5, the needle r - 4 long, its 3 px shadow, the 1.5 px needle in rgb, then the pivot cover (the face's
 * r - 4 disc below the pivot less 3 px, #070706, and its edge line); inside DW_P9_METER[i]. */
void dw_dome_lamp(uint16_t *fb, int stride, const omni_rect_t *clip, float x, float y, float r, uint16_t rgb,
                  float on);
void dw_p9_needle(uint16_t *fb, int stride, const omni_rect_t *clip, int i, float v, uint16_t rgb);
/* #6's roundel (wave 2 Task 10, WD27): the unlit well (unlit, 52 px), then the lit sprite (lit, the hour colour's 52 px
 * cell of dw_w6_lit) over it at level / 255, both with their centre pixel (26, 26) on (x, y) rounded: the top-left at
 * (floor(x - 25.5), floor(y - 25.5)). level 0: the well alone. Approximates roundelSprite's 24 baked levels. */
void dw_wall_roundel(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_img_t *unlit, const fk_img_t *lit,
                     float x, float y, uint8_t level);
/* crystalSleep's outline hand: the shard's outline only, stroked width 1.3, about (cx, cy) (the glance orbit). */
void dw_hand_outline(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, float cx, float cy,
                     uint16_t rgb, uint8_t a8);
void dw_hand_outline_bbox(float a, bool hour, float cx, float cy, omni_rect_t *out);
/* The faces' glow tables as const data (dw/glow_lut.h, generated by dw_shot --glow-lut and checked against
 * fk_glow_lut_build by the host tests; Task 13, the Task 12 review's M3): fk_glow_lut_build(lut, r)'s bytes for r in
 * DW_GLOW_LUT_RADII, from the flash mapping, so no face builds one at bind (no RAM, no time on the lift-to-light path).
 * NULL for any other radius. */
#define DW_GLOW_LUT_RADII {120, 22, 12, 60, 18, 70, 40, 26, 233, 14}   /* wave 2 Task 5: #2's rotor (70) and lit
                                                                         * hour (40); Task 6: #10's wand tip and trail
                                                                         * (26); Task 7: #13's escaping light (233);
                                                                         * Task 11: #3's seconds light (14) */
const uint8_t *dw_glow_lut(int r);

/* #4's hands (faces-classic.js anaHand()): the hour hand a tapered sword carrying a hollow roundel near its tip, the
 * minute hand a long baton; a 2.5 px black outline, the cream gradient across the hand, the dark centre line. Their
 * drop shadow is left out (the dirty boxes stay the hands' own). About the screen centre; a8 the hand's opacity. */
void dw_hand_baton(uint16_t *fb, int stride, const omni_rect_t *clip, float a, bool hour, uint8_t a8);
void dw_hand_baton_bbox(float a, bool hour, omni_rect_t *out);   /* every pixel it can touch */
/* #4's seconds hand: an indicator-red line (1.8 px, round caps) from 38 px behind the centre to 206 px, and its
 * hexagon counterweight; no shadow. dw_hand_second_rects: its pixels' boxes, as 4 boxes along the hand (the dirty
 * rects of a tick are the old hand's 4 and the new one's 4). */
void dw_hand_second(uint16_t *fb, int stride, const omni_rect_t *clip, float a);
void dw_hand_second_rects(float a, omni_rect_t out[4]);
/* The seconds hand's angle (radians clockwise from 12): each second it ticks to s with a short damped overshoot,
 * faces-classic.js's ((s - 1) + tick) / 60 turns, tick = 1 + sin(pi ms / 160) x 0.18 x (1 - ms / 160) for ms < 160,
 * then exactly s / 60 turns. 0 when the clock is unset. */
float dw_second_angle(const fk_clock_t *c);
/* dw_second_angle, or exactly s / 60 turns under reduced motion (in->reduced: no overshoot) */
float dw_second_angle_in(const struct fk_face_in *in);
#define DW_SECOND_OVERSHOOT_MS 160
/* The Dymo tape (faces-classic.js tape()): a black strip 19 px tall, its width the text's (tracked 1.6) + 14, centred
 * on (cx, cy), and the letters from at (the atlas's own colour, or its alpha in rgb when tint). Crystal Chamber's stale
 * strip was its first use. */
void dw_tape(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_atlas_t *at, const char *text, float cx,
             float cy, bool tint, uint16_t rgb);
/* staleMark(x, y): the amber dome lamp at x - 52 (its spill from lut12, dw_glow_lut(12)) and the NOT SYNCED tape at
 * x + 8, from at (the amber dymo atlas). */
void dw_stale_mark(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_atlas_t *at, float x, float y);

/* ---- #1 Crystal Chamber (crystal.c; faces-reveal.js buildCrystalBase, buildLances, buildScriptRing,
 * crystalInterior, crystalSleep). */
#define DW_C1_SCRIPT_R0 214            /* the script ring's strip: r 214-231 (rows 214..230), n_theta 2048 */
#define DW_C1_SCRIPT_R1 231
#define DW_C1_SCRIPT_NT 2048
#define DW_C1_EASE_MS 280              /* the script's step each second (D3) */
#define DW_C1_GLINT_MS0 400            /* the glint runs round the crystal's six facets in [400, 900) ms of each second */
#define DW_C1_GLINT_MS1 900
extern const omni_rect_t DW_C1_MONITOR;   /* the holo monitor at 6 (its text: dw_c1_monitor_text) */
extern const omni_rect_t DW_C1_STALE;     /* the stale marker: the amber dome lamp and the NOT SYNCED strip */
extern const omni_rect_t DW_C1_BOX;       /* the crystal's moving box at rest (the glint) */
extern const omni_rect_t DW_C1_LANCES;    /* every pixel the lances' layer can light (the chime's breath, Task 16) */
/* The monitor's text: "SUN 1" (day and date), or "--:--" when the clock is unset. out holds 12 bytes. */
void dw_c1_monitor_text(const fk_face_in_t *in, char out[12]);
/* The script ring's strip offset (fk_annulus_draw's th_off) for in: the second's step eased over DW_C1_EASE_MS, the
 * wake's wind-in; 0 when unset. */
uint16_t dw_c1_script_off(const fk_face_in_t *in);

/* The mockups' reveal time (Designer A's 2.3 s, faces-reveal.js revealState) for a wake time on this build's doors
 * timeline: the swing's start and the doors' end map onto Designer A's (identity on the DW_DOORS_MOCKUP build). #1's
 * and #2's tails (wave 2 Task 5: moved here from crystal.c, unchanged). */
static inline float dw_mockup_te(uint32_t wake_ms)
{
    const float t0 = DW_DOORS_SWING_T0_MS, t1 = DW_DOORS_END_MS;
    return 380.f + ((float)wake_ms - t0) * ((2300.f - 380.f) / (t1 - t0));
}

/* ---- #2 Roundel Room (wave 2 Task 5; room.c; faces-reveal.js buildClassicBase, classicHands, classicInterior,
 * classicSleep): Classic's Flip-open home, behind the Classic doors. */
extern const omni_rect_t DW_R2_ROTOR;  /* the rotor's glow box (r 70 about the centre): all that moves within a minute
                                        * (WD41: spec section 11 says an 80 px box; the designer's r 70 glow breathes
                                        * with the lift at rest) */
extern const omni_rect_t DW_R2_STALE;  /* the stale marker, centred (233, 329): between the rotor and the date at 359 */
extern const omni_rect_t DW_R2_BEAMS;  /* every pixel the beams' layer (dw_r2_beam) lights: the chime's breath */
/* The rotor's lift, 0..1: (sin(t / 2000 turns) + 1) / 2 for t the ms of the day mod 2000; under the chime (not
 * reduced) the envelope itself (the mockup's bob = 2 lift - 1); 0.5 under reduced motion; 0 when the clock is unset. */
float dw_r2_lift(const fk_face_in_t *in);
/* Hour roundel h (1..12, at r 207): every pixel its lit drawing (the r 40 glow, the disc, the ring, the numeral) can
 * touch, cut to the screen. */
void dw_r2_hour_rect(int h, omni_rect_t *out);

/* ---- #10 Crystal (wave 2 Task 6; amber.c; faces-modern.js caBuild, caHands, modern-analog): amber shard hands that
 * step each minute, the wand seconds hand sweeping, the script ring stepping once an hour round. */
#define DW_C10_SCRIPT_R0 198           /* the script ring's A8 strip: r 198-232 (the band's circles at 202 and 228, */
#define DW_C10_SCRIPT_R1 232           /* stroked across them, and the words that cross them), n_theta 2048 */
#define DW_C10_SCRIPT_NT 2048
#define DW_C10_WAKE_MS 900             /* the wake: the hands sweep from 12 (easeOut3 over 900 ms) */
#define DW_C10_FADE_MS 700             /* the dial (the floor, the numerals, the script, the core glow) fades up */
extern const omni_rect_t DW_C10_STALE; /* the stale marker, centred (233, 317) */
/* The script's strip offset (fk_annulus_draw_a8's th_off): 1 degree each 10 s, once round an hour (WD30): k =
 * round(floor(t / 10) n / 360) for t the seconds into the hour, so (n - k) mod n; 0 when the clock is unset, and under
 * reduced motion (held still). */
uint16_t dw_c10_script_off(const fk_face_in_t *in);
/* The wand's angle (radians clockwise from 12): the sweep (s + ms / 1000) / 60 of a turn; s / 60 under reduced motion
 * (a step each second); 0 unset. dw_c10_pulse: the tip glow's pulse .75 + .25 sin(2 pi 1.2 t), t the seconds of the
 * day (taken mod 5 s: six whole periods); 1 under reduced motion or unset. */
float dw_c10_wand_angle(const fk_face_in_t *in);
float dw_c10_pulse(const fk_face_in_t *in);

/* ---- #13 Fob watch (wave 2 Task 7; fob.c; faces-modern.js fwBuild, fwLid, fwHands, fob-watch): the hunter case's lid
 * on the shared horizontal hinge engine (omni/lid.h, unchanged), the escaping light, the Breguet hands and the
 * sub-seconds hand. wake_len_ms = HOLD + OPEN + LIGHT = 2200; pulse_len_ms = CLOSE + OPEN + LIGHT = 2520 (R147 PR5: a
 * tap closes the lid and opens it again with no hold). */
extern const omni_hinge_t DW_F13_HINGE;  /* { OMNI_HINGE_HORIZONTAL, 0, 1000, 466, 0, 0 }: faces-modern.js :836-851, f
                                          * 900 with Z x .9, as F 1000; flat (dome 0) */
#define DW_F13_HOLD_MS 100               /* WD31: the mockup's 450 would break the 0.5 s lid rule; the wake's hold only,
                                          * a tap has none */
#define DW_F13_OPEN_MS 900               /* easeOut3 to DW_F13_OPEN_RAD */
#define DW_F13_OPEN_RAD (0.62f * 3.14159265f)
#define DW_F13_LIGHT_MS 1200             /* the escaping light after the swing */
#define DW_F13_CLOSE_MS 420              /* a tap: inOut close, then open again at once (no hold, PR5) */
#define DW_F13_FADE_MS 500               /* reduced motion: the lid fades instead */
/* the lid's angle t ms into a wake (0 under reduced): 0 for the hold, then easeOut3 to DW_F13_OPEN_RAD over OPEN */
float dw_f13_theta(uint32_t t_ms, bool reduced);
/* the close then the reopen, after a tap: OPEN_RAD (1 - inOut(p / CLOSE)), then OPEN_RAD easeOut3((p - CLOSE) / OPEN) */
float dw_f13_tap_theta(uint32_t pulse_ms);
/* The lid at in: its angle and its weight (255 opaque, 0 none). A pulse takes precedence over the wake (R147 PR20);
 * under reduced motion a wake shows no lid and a tap's flat lid (theta 0) fades 255 -> 0 over DW_F13_FADE_MS; settled
 * and in the glance, none. Past omni_hinge_gone_rad(&DW_F13_HINGE) a weighted lid draws no row. */
void dw_f13_lid(const fk_face_in_t *in, float *theta, uint8_t *a8);
extern const omni_rect_t DW_F13_SUB, DW_F13_STALE;  /* sub-dial 96 x 96 at (233, 337); stale centred (233, 277) */

/* ---- #9 Console panel (wave 2 Task 9; console.c; faces-classic.js CON, PANEL, meterFace, meterNeedle, conBase,
 * classic-console): the hexagonal console from above, the time in the rotor collar and six instruments on six sloping
 * panels (PANEL, clockwise from 12): 0 HOURS, 1 MINUTES, 2 POWER, 3 SECONDS (the red-hand dial), 4 SIGNAL (the three
 * lamps), 5 SOLAR. The shared A8 text atlases dw_p60, dw_cor16 and dw_cap11 (WD25) come with it. */
#define DW_P9_WAKE_MS 1570             /* needle i springs to its value by springTo(t - 120 - 90 i, 1000) */
extern const float DW_P9_XY[6][2];     /* instAt(p, 156) for p.a -90, -30, 30, 90, 150, 210: each instrument's centre */
extern const omni_rect_t DW_P9_METER[6];  /* each instrument's window: its r 36 circle's box */
extern const omni_rect_t DW_P9_LAMPS;  /* the three signal lamps and their spill, inside DW_P9_METER[4] */
extern const omni_rect_t DW_P9_CENTRE; /* the day, the time and the date line in the collar */
extern const omni_rect_t DW_P9_STALE;  /* dw_stale_mark centred (233, 301), inside the collar */
/* The readings at in, by PANEL index: v[0] hours ((h % 12) + m / 60) / 12, v[1] minutes (m + s / 60) / 60, v[2] power
 * battery / 100 (0 unknown), v[3] the seconds hand's turns (s + tick) / 60, tick = sin(ms / 140 pi) .25 (1 - ms / 140) for
 * ms < 140 (none under reduced motion), v[4] 0 (the lamps), v[5] solar clamp((now - rise) / (set - rise)) in whole
 * minutes (0 with no sun); each times its wake spring (1 settled, under reduced motion and in the glance). Unset: 0 for all
 * but the power. */
void dw_p9_values(const fk_face_in_t *in, float v[6]);
/* The signal lamps at in: green (synced), the middle lamp (amber stale, red unset; off when synced) and the in-flight
 * lamp (P.lamp: on while the ms of the day mod 2000 < 700; steady .6 under reduced motion; off unset): colour and level. */
void dw_p9_lamps(const fk_face_in_t *in, uint16_t rgb[3], float on[3]);

/* ---- #6 Roundel wall (wave 2 Task 10; wall.c; faces-classic.js wallCells, WALL_COLS, roundelSprite and
 * classic-roundel-wall): sixty roundels in a staggered wall, one lit for each minute of the hour in the hour's era
 * colour, the current one breathing; the time in a dark porthole. The shared atlases dw_p60, dw_cor16, dw_cap11. */
#define DW_W6_CELLS 60
#define DW_W6_WAKE_MS 800
/* wallCells (:551-560): pitch 46, columns 46 x .866 apart, odd columns down half a pitch, r 17.5; kept when
 * 104 + 17.5 + 2 < d < 233 + 17.5 x .3; sorted by angle from 12 clockwise, then by distance (a const table) */
void dw_w6_cell(int i, float *x, float *y);
/* cell i's box: its 52 px sprite's (dw_wall_roundel), cut to the screen */
void dw_w6_box(int i, omni_rect_t *out);
extern const uint16_t DW_W6_COLS[6];   /* WALL_COLS (:550): cream, lilac, amber, aqua, rose, celadon (RGB565) */
/* cell i's level at in, 0..255 (:589-595): cells i < m at .82; cell m at clamp(s / .6, 0, 1) (.72 + .28 sin(t / 2400
 * turns)), t the ms of the day, s the seconds into the minute; the wake: only cells below shown = min(m, floor(easeOut3(
 * wake_ms / 800) (m + 1))), and cell m once shown reaches m. Reduced motion: cell m steady at .86 (the breath's mean),
 * no fade-on, no wake. Unset: only cell 0, at .25 + .25 sin(mono_ms / 2400 turns) (.25 steady reduced). A level of .01
 * or less is 0 (the mockup draws no sprite). */
uint8_t dw_w6_level(const fk_face_in_t *in, int i);
/* the lit cells' colour: DW_W6_COLS[h % 6]; cream unset */
uint16_t dw_w6_colour(const fk_face_in_t *in);
extern const omni_rect_t DW_W6_CENTRE; /* the porthole's day, time and date line (and AWAITING SIGNAL) */
extern const omni_rect_t DW_W6_STALE;  /* staleMark(cx, cy + 72): dw_stale_mark centred (233, 305) */

/* ---- #3 Through the Doors (wave 2 Task 11; doorway.c; faces-reveal.js buildDoorwayBase, doorwayInterior, doorwayOver,
 * doorwaySleep): the canon inward swing, its leaves kept at 86 degrees as the bezel (DW_DOORS_IN_KEEP; fk_cols_leaves,
 * unless in->reveal_live), the frontal crystal room, the digital time over a smoke backplate, and the floor ring's
 * perspective dial (the hour and minute marks, the blue seconds light). */
extern const omni_rect_t DW_D3_COLUMN;   /* the column glow's r 120 box about (233, 250): all that moves within a second */
extern const omni_rect_t DW_D3_STALE;    /* dw_stale_mark centred (233, 372) */
extern const omni_rect_t DW_D3_LANCES;   /* every pixel the lances' layer (dw_d3_lance) lights: the chime's breath */
extern const omni_rect_t DW_D3_RIPPLE;   /* the chime's ripple ellipse at its widest */
extern const omni_rect_t DW_D3_TIME;     /* everything the time layer draws (the backplate's lit box) */
/* the seconds light at second s (its r 14 glow and its disc); a floor mark i (of 60) and, for the hour, its r 18 glow */
void dw_d3_sec_rect(int s, omni_rect_t *out);
void dw_d3_mark_rect(int i, bool hour, omni_rect_t *out);
/* the boxes the time's glyphs (their stroke-and-glow cells) and the date line can ink, for in (date: empty unset) */
void dw_d3_text_boxes(const void *ctx, const fk_face_in_t *in, omni_rect_t *digits, omni_rect_t *date);
/* the time layer's level for in (255 settled; the tail's fade, 0 before it starts) (wave 2 Task 12, review M5) */
uint8_t dw_d3_text_level(const fk_face_in_t *in);

/* ---- #8 Broadcast (wave 2 Task 12; broadcast.c; faces-classic.js howlaround, scanlines, bcCentre and
 * classic-broadcast): a 405-line television set: the howlaround ring turning round the rim once in 90 s, stepped each
 * second; the bloomed time with its multipath ghost; the hum bar; scanlines over everything; the switch-on wake. */
#define DW_B8_RING_R0 128              /* dw_b8_ring: the A8 strip over r 128-233 at n_theta 2048 (R147 PR8), the */
#define DW_B8_RING_R1 233              /* tube's vignette baked into its rows */
#define DW_B8_RING_NT 2048
#define DW_B8_WAKE_MS 450              /* the switch-on: the bright line, then the picture opens out from 233 */
#define DW_B8_LINE_MS 120              /* the line's part */
extern const omni_rect_t DW_B8_STALE;  /* dw_stale_mark centred (233, 333) (staleMark(cx, cy + 100)) */
extern const omni_rect_t DW_B8_CENTRE; /* every pixel the centre's text (the day, the time, its ghost and bloom, the
                                        * date line; the unset lines) can touch */
/* The ring's strip offset (fk_annulus_draw_a8's th_off): k = round((t mod 90) 2048 / 90) entries clockwise, t the
 * seconds of the day (mono_ms / 1000 when the clock is unset), so (n - k) mod n: 22 or 23 entries a second, once round
 * in 90 s (D3); 0 under reduced motion (held). */
uint16_t dw_b8_ring_off(const fk_face_in_t *in);
/* The face's frame (draw, or the glance when in->glance) with or without the scanlines (scan false: the test hook;
 * draw and glance pass true). */
void dw_b8_frame(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride, bool scan);

/* ---- #4 Roundel analog (roundel.c; faces-classic.js anaDial, anaHand, classic-analog). */
#define DW_R4_R 172                   /* the hour roundels' radius */
#define DW_R4_WAKE_MS 2100            /* the wake: the wave's lap, its rest on the hour (<= 2025 ms), the hands' sweep */
extern const omni_rect_t DW_R4_STALE; /* the stale marker: the amber lamp and the NOT SYNCED tape above the hub */
/* The lit level of hour roundel h (1..12) for in, 0..255: the wake's wave and its rest on the hour; settled, 255 for
 * the current hour and 0 for the others; 0 for all when the clock is unset. */
uint8_t dw_r4_lit(const fk_face_in_t *in, int h);

/* ---- #5 Call box digital (callbox.c; faces-classic.js boxFrame, boxSign, panes, classic-digital). */
#define DW_B5_WAKE_MS 2700            /* the wake: the lamp's three pulses (the sign's flicker, the panes' stagger) */
#define DW_B5_PULSE_MS 2700           /* a lamp tap's three pulses */
extern const omni_rect_t DW_B5_STALE; /* the stale marker: the plaque's bottom line (NOT SYNCED in amber) */
extern const omni_rect_t DW_B5_SIGN;  /* the sign and its bloom: everything the minute's text can touch */
extern const omni_rect_t DW_B5_LAMP;  /* the lamp's glow and its lens */
extern const omni_rect_t DW_B5_PLAQUE;
/* The panes lit at in: pane i lights at i x 5 s, so s / 5 + 1 of them, at most 12 (faces-classic.js:497; B15); 0 when
 * the clock is unset. dw_b5_pane_level: pane i's level 0..255 (its 0.4 s fade in, the wake's stagger). */
uint8_t dw_b5_panes_lit(const fk_face_in_t *in);
uint8_t dw_b5_pane_level(const fk_face_in_t *in, int i);
/* The lamp's level, 0..255 (0.28 at rest, three pulses on the wake and a tap, one on the hour, 0.18 unset). */
uint8_t dw_b5_lamp_level(const fk_face_in_t *in);

/* ---- #7 Rotor (rotor.c; faces-classic.js rotorGlass, rotorFront, classic-rotor). */
#define DW_T7_WAKE_MS 1300            /* the take-off: the stack rises out of the collar, the core brightens (1250 ms) */
#define DW_T7_STACK_W 90              /* dw_t7_stack: the plates, rods and spheres, its base line at row DW_T7_STACK_BASE */
#define DW_T7_STACK_H 170
#define DW_T7_STACK_BASE 164
extern const omni_rect_t DW_T7_COLUMN; /* the glass column's interior: the only region that moves within a second */
extern const omni_rect_t DW_T7_STALE;  /* the stale marker (the amber lamp and NOT SYNCED) where the date tape is */
/* The stack's stroke: 0 at the bottom .. 1 at the top over each 2 s, sinusoidal ((1 - cos) / 2 of the turn), rising on
 * the even second and falling on the odd one (ms_in_2s is taken mod 2000). */
float dw_rotor_lift(uint32_t ms_in_2s);
/* Second s's rim pinpoint (0..59): every pixel it can touch. */
void dw_t7_pin_rect(int s, omni_rect_t *out);

/* ---- #11 Coral (coral.c; faces-modern.js cdBuild, rotorColon, modern-digital). */
#define DW_K11_WAKE_MS 600            /* the wake: the hex ring and struts fade in (easeOut3 over 600 ms) */
#define DW_K11_G_R 10.f               /* the circle script under-line's ring radius (20 px numerals) */
extern const omni_rect_t DW_K11_COLON; /* the rotor colon and its glow: the only region that moves within a second */
extern const omni_rect_t DW_K11_STALE; /* the day line (NOT SYNCED in amber when stale) */
/* The colon slug's bob, -1..1: sin of the turn over 2.2 s (ms_in_2200 taken mod 2200); the slug sits 18 px x bob below
 * the colon's middle. The face reads it at the time of day mod 2.2 s; 0 under reduced motion or unset. */
float dw_coral_slug(uint32_t ms_in_2200);
/* Hex cell i (0..59, i / 60 of a turn from 12 o'clock at r 214): its centre. dw_k11_cell_state: 0 unlit, 1 gold (a
 * second gone this minute), 2 teal (the current second); every cell 0 when the clock is unset. */
void dw_k11_cell_xy(int i, float *x, float *y);
uint8_t dw_k11_cell_state(const fk_face_in_t *in, int i);
#define DW_K11_CELL_R 9.6f            /* every pixel a cell (its outline r 9, stroke 1.2) can touch, about its centre */
/* The under-line: the time again in the numerals, following the shared 12/24 h (spec section 12 [call]); four
 * DW_G_EMPTY when unset. dw_k11_underline_xy: glyph i's centre (rowPos(cx, cy + 112, 10, 4, 12)). */
void dw_k11_underline(const fk_face_in_t *in, int digits[4]);
void dw_k11_underline_xy(int i, float *x, float *y);
/* The two coral struts on each side (cdBuild): cubic Beziers, stroked w px, baked into dw_k11_base. */
typedef struct { float x0, y0, c1x, c1y, c2x, c2y, x1, y1, w; } dw_bezier_t;
#define DW_K11_STRUT_N 4
extern const dw_bezier_t DW_K11_STRUTS[DW_K11_STRUT_N];

/* ---- #14 Circle Script, #15 the numeral key and #16 Learning (gface.c, gkey.c, glearn.c; faces-modern.js circles,
 * circles-key, circles-learn). The numerals are drawn live (dw/circles.h), always 24 h (D5); an empty
 * ring means the time is not known. No decorative line touches a numeral (spec section 12; test_g14_layout_no_touch). */
typedef struct { float cx, cy, r; } dw_slot_t;
/* The 2 x 2 layouts, hours over minutes, each row left to right: #14's 80 px rings (quadPos(cx, cy - 4, 40)) and #16's
 * 68 px ones (quadPos(cx, 214, 34), the minutes 30 px lower, each ring 18 px above its twin's line) */
extern const dw_slot_t DW_G14_SLOTS[4], DW_G16_SLOTS[4];
#define DW_G14_CIRCLE_R 154.f          /* the inner circle (stroke 1.2), where the seconds bead runs */
#define DW_G14_CIRCLE_W 1.2f
#define DW_G14_DOTS_R 162.f            /* the sixty fine dots (r 0.9, every fifth 1.7) */
#define DW_G14_BAND_R0 170.f           /* the script band's circles (scriptRing(W, 170, 224, ..)) */
#define DW_G14_BAND_R1 224.f
#define DW_G14_SCRIPT_R0 162           /* dw_g14_script: the A8 strip over the rows its ink reaches on the panel */
#define DW_G14_SCRIPT_R1 234
#define DW_G14_SCRIPT_NT 2048
#define DW_G14_DATE_R 10.f             /* the date: 20 px numerals at 6 o'clock on the circle, cx -+ 15 */
#define DW_G14_KNOCK_R 15.f            /* the circle (and what lies under it) knocked out behind each date numeral */
#define DW_G14_WAKE_MS 3300            /* the draw-on (1190 ms), the band's fade (700 ms), the hint (2.5 s + 0.8 s) */
#define DW_G14_HINT_MS 2500
extern const omni_rect_t DW_G14_STALE; /* the stale marker under the minutes (the hint's place, which it replaces) */
extern const omni_rect_t DW_G14_HINT;  /* TAP FOR THE KEY */
/* The four rings' digits: dw_digits24 (21:07 is 2 1 0 7 whatever in->h24); DW_G_EMPTY x 4 when unset. */
void dw_g14_digits(const fk_face_in_t *in, int d[4]);
/* The script band's strip offset (fk_annulus_draw_a8's th_off): k = round(t n / 600) entries clockwise for t the
 * seconds into the current ten minutes (0.6 degrees a second, once round in 10 minutes, D3), so (n - k) mod n; 0 when
 * the clock is unset or under reduced motion (held still). */
uint16_t dw_g14_script_off(const fk_face_in_t *in);
/* Date numeral i's centre (i 0 the tens) */
void dw_g14_date_xy(int i, float *x, float *y);
/* The seconds bead's centre on the inner circle: the sweep (s + ms / 1000) / 60 of a turn, or dw_second_angle_in's
 * whole second under reduced motion */
void dw_g14_bead_xy(const fk_face_in_t *in, float *x, float *y);

/* #15, the key: three pages (in->page; scr_key.c turns them). Page 0: the ten numerals and their twins; 1: the four
 * line shapes and the 5 + n sum (n steps every 1.6 s from the page's opening); 2: the time frozen at the page's opening,
 * read ring by ring. in->wake_ms is the time since the page opened (UINT32_MAX: settled, the page built and read
 * through); in->clk is the clock now. */
#define DW_KEY_PAGES 3
#define DW_KEY_SUM_MS 1600
#define DW_KEY_STEP_MS 1700            /* page 2: each ring's turn; the cycle is 4 steps and a 2.6 s rest */
#define DW_KEY_CYCLE_MS (4 * DW_KEY_STEP_MS + 2600)
/* The glyphs page in->page draws, as slots (centre, ring radius): returns how many (<= 12). */
int dw_key_slots(const fk_face_in_t *in, dw_slot_t out[12]);
/* Page 2's time: the clock less wake_ms (the page's opening; the clock itself when settled), as dw_digits24. */
void dw_key_time(const fk_face_in_t *in, int d[4]);
/* Page 1's sum: n (0..4) and the result's level (0..255) */
void dw_key_sum(const fk_face_in_t *in, int *n, uint8_t *level);
extern const omni_rect_t DW_KEY_BACK;  /* the back button's tap target (>= 44 px) */

/* #16, Learning */
#define DW_G16_ALT_MS 4000             /* a tap hides the twins this long (FK_TAP_ALT, in->alt) */
extern const omni_rect_t DW_G16_HELP;  /* the "?" button's tap target (>= 44 px): FK_TAP_SCREEN */
extern const omni_rect_t DW_G16_STALE; /* the stale marker, where the day line is */
/* Twin i's box: every pixel its Arabic digit (or "?", or "-") can touch */
void dw_g16_twin_box(int i, omni_rect_t *out);

/* ---- #12 Vortex (wave 2 Task 14; vortex.c; faces-modern.js:677-772 "Vortex"): the time on calm black at the eye of a
 * turning, rushing tunnel confined to the ring r 104-236 (dw/vortex.h's kernel at DW_V12_Q), three palettes by
 * in->variant (a tap: the next, its name for 1.4 s, then a 0.5 s fade). */
extern const omni_rect_t DW_V12_TUNNEL[4];   /* every pixel the tunnel can change: the screen less DW_V12_CENTRE */
extern const omni_rect_t DW_V12_CENTRE;      /* [161, 305)^2: every block there has its centre inside r 104 (black) */
extern const omni_rect_t DW_V12_NAME;        /* the palette's name (Cormorant SC 12, tracked 3) about (233, 315) */
extern const omni_rect_t DW_V12_STALE;       /* dw_stale_mark centred (233, 333) */
uint8_t dw_v12_palette(const fk_face_in_t *in);      /* in->variant % 3 */
/* the name's level: 255 for pulse_ms <= 1400, then 255 (1900 - pulse_ms) / 500 rounded, 0 from 1900 or not pulsing */
uint8_t dw_v12_name_level(const fk_face_in_t *in);
