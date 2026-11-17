/* The Mining Ship faces (rd/faces.h; rd wave 1 Task 9, spec section 10): one table row per registered fk_face_ops_t
 * (RD_FACES; each face task adds its row and its lines in the per-face tables below), every row checked on wave 1's
 * lessons (the plan's Global Constraints): band exactness, dirty() over skips and back-steps (lesson 1), reduced motion
 * held still (lesson 2), the full-day glance sweep in 24 and 12 h (lesson 4), the void floor (PR10), the 200 px text
 * radius at lesson 10's times, the tap targets probed at their edges (lesson 5), the chime picture inside its declared
 * rects, at most four atlases, and the still-frame CRCs (rd/verify_crc.h). Then each face's own cases.
 *
 * The glyph boxes and the atlases a face draws from come from face_kit's FK_TEST_HOOKS recording (fk/compose.h, host
 * builds only, PR18). The frame, instant, lit scan, orbit and tap-edge helpers are fk_test_util's (R147 PR10). Over the
 * committed rd assets (RD_ASSETS_DIR).
 *
 * Wave 2 Task 6's appended harness fields (rulings R165/I3, R178, R179): RD_TAP_CIRCLES, a circular target probed at r,
 * r + 1 and the 45 degree points at r - 1 (its centre fixed, or from each still's clock); tap_t.except, a declared rect
 * with its face's circles cut out (a nested target: Drive Console's button inside its face-wide strip tap), probed by a
 * point set outside the circles instead of fk_test_tap_edges; tap_var_t.keeps, a line whose tap starts its pulse and
 * leaves the variant (tap_probe asserts FK_TAP_PULSE and no move); static_bright_t.abs_ceil, a face's own absolute
 * static-bright ceiling where a ruling gives one (0: RD_STATIC_BRIGHT_ABS). Wave 2 Task 8's (appended):
 * tap_var_t.shows_ms, for a line whose variant shows only while its pulse plays (Vending Machine's last press and its
 * item): tap_probe derives whether the state hides the tapped text from frames pulsing at shows_ms, not at rest. Wave 2
 * Task 9's (appended): a face whose every RD_TAP_VARS line keeps (its tap only starts its pulse: the Bowl's paw) has
 * no tapped variant; dirty_tapped asserts its still 11 is a pulse frame instead (rd_still_pulse_ms), and band_exact
 * builds the tapped still as rd_still_frame does (its pulse too); a shows_ms line is checked to be shorter than the
 * face's pulse and to leave some probed state not hidden (the Task 8 review's M1). Wave 2 Task 10's (appended; R165/I3,
 * plan W2-D12): tap_var_t.at, a line whose target moves with the clock (Mop Bot's robot): tap_probe taps at at(in), the
 * target's centre at the probed input, instead of its hit rect's centre; and a face whose RD_TAP_CIRCLES circle moves
 * (at) has its off-target grid run through tap_var too, at every awake still, outside that still's circle. */
/* RD_FACES_PART: see the RD_RUN_ROW block above main (0 or undefined: all); parts 1-3 leave the others' rows unused */
#ifndef RD_FACES_PART
#define RD_FACES_PART 0
#endif
#if RD_FACES_PART != 0
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "fk_test_util.h"
#include "rd_assets_host.h"
#include "fk/compose.h"
#include "fk/ease.h"
#include "fk/shapes.h"
#include "fk/text.h"
#include "omni/blend565.h"
#include "rd/airlock.h"
#include "rd/asleep.h"
#include "rd/assets_meta.h"
#include "rd/faces.h"
#include "rd/pass.h"
#include "rd/release.h"
#include "rd/seg7.h"
#include "rd/soft_lut.h"
#include "rd/verify_crc.h"
#include "rd_cu_steam_oracle.h"

void setUp(void) {}
/* wave 2 Task 7 (W2-D11, M11): every test ends with Nav Scope's committed sweep, whatever a test set (a failed assertion
 * included), so the harness and rd_still_frame always run with RD_SC_STEP_MS */
void tearDown(void) { rd_sc_set_step_ms(RD_SC_STEP_MS); }

#define W 466

/* the measured pins (wave 2 Task 8) */
#define VM_VOID_FLOOR 60.0f
#define VM_SB_PIN 31.59f
#define VM_GW24 2.70f
#define VM_GW24S 2.85f
#define VM_GW12 2.17f
#define VM_GW12S 2.32f
#define VM_TEA100_CRC 0x525ce21eu   /* wave 2 Task 10 (the Task 9 review's m1): TEA's press at 100 ms, rendered by frame() */
/* (wave 2 Task 9) */
#define CB_VOID_FLOOR 29.0f
#define CB_SB_PIN 57.74f
#define CB_GW_H 0
#define CB_GW_M 16
#define CB_GW 3.47f
#define CB_GWS 3.62f
/* (wave 2 Task 10) */
#define MB_VOID_FLOOR 64.0f
#define MB_SB_PIN 14.31f
#define MB_SB_CEIL 27.22f
#define MB_GW_H 23
#define MB_GW_M 59
#define MB_GW 3.07f
#define MB_GWS 3.22f
#define MB_GW12 2.76f
#define MB_GW12S 2.91f
#define MB_REACH 209.0f
#define MB_SBW_PIN 28.25f
#define MB_GW12_H 0
#define MB_GW12_M 59
#define WAKE_PROBE_MS 600

static uint16_t s_a[W * W], s_b[W * W], s_c[W * W];

/* ---- the per-face tables: each face task adds its lines with its row */
/* PR10: the share of the disc that is void (every channel <= 40) on stills 0, 1 and 3 is at least the face's floor: the
 * face's mockup measured by this harness's disc rule (rd_shot --void on its settled shot), minus 2 points. The spec's
 * 85 % stays a report line, printed when a face is under it. */
typedef struct { const char *key; float floor; } void_floor_t;
static const void_floor_t RD_VOID_FLOOR[] = {
    /* Stasis: its settled shot measures 86.76 % (rd_shot --void); its lowest still (0) 85.08 % less 2 points (the
     * controller's ruling 1, Task 9 review) */
    {"stasis", 83.0f},
    /* Ship Terminal: its settled shot measures 93.03 % (rd_shot --void on the shot's 466 px frame), within a point of
     * the brief's 93; its lowest still (3, 12 h: the AM suffix) 92.64 % less 2 points (the face-task guide's rule) */
    {"terminal", 90.0f},
    /* Deck Sign: its settled shot measures 82.64 % (rd_shot --void on the shot's 466 px frame: the beacon at full
     * strength, 3 s after its wake), 1.4 points under the brief's 84; its lowest stills (0 and 3) 83.14 % less 2 points
     * (the face-task guide's rule; the brief's would give 80) */
    {"deck", 81.0f},
    /* Hull Plate: its settled shot measures 84.46 % (rd_shot --void on the shot's 466 px frame), 1.5 points under
     * the brief's 86; its lowest still (3, 12 h: the date's AM suffix) 83.77 % less 2 points (the face-task guide's
     * rule, as Deck Sign's; the brief's would give 82) */
    {"hull", 81.0f},
    /* Bunk Clock: its settled shot measures 72.75 % (rd_shot --void on the shot's 466 px frame, 10:42), 3.25 points
     * under the brief's 76; its lowest still (3, 12 h) 70.00 % less 2 points (the face-task guide's rule; the brief's
     * measured-minus-2 would give 70.75, over two of the stills). The case and the cards are its light: the card
     * blank's top rows sit at 39-42 of 255, so RGB565's nearest red step (41) takes them out of the void */
    {"bunk", 68.0f},
    /* Curry Clock: its settled shot measures 46.23 % (rd_shot --void on the shot's 466 px frame), 3.8 points under the
     * brief's about 50 (so 48); its lowest still (1, 03:47: the spoon over the dark rim) 46.32 % less 2 points (the
     * face-task guide's rule, and the brief's measured-minus-2 alike: 44.2). The foil and the curry are its light, low
     * by design (PR10) */
    {"curry", 44.0f},
    /* Bay Mouth (wave 2 Task 3): its settled shot measures 59.19 % (rd_shot --void on the shot's 466 px frame), the
     * brief's about 59.3; its stills 0 58.22 %, 1 58.97 %, 3 58.05 % (12 h: the date's AM suffix), the lowest less 2
     * points (the face-task guide's rule; the brief's expectation was 57). The star band and the hazard ring are its
     * light (PR10) */
    {"bay", 56.0f},
    /* Drift Plate (wave 2 Task 5): its settled shot measures 55.48 % (rd_shot --void on the shot's 466 px frame), the
     * brief's about 55.6 (stale 55.69 %, unset 56.38 %, chime 54.32 %); its stills 0 55.31 %, 1 55.54 %, 3 55.14 % (12 h),
     * the lowest less 2 points (the face-task guide's rule; the brief's expectation, 53). The band is its light */
    {"drift", 53.0f},
    /* Drive Console (wave 2 Task 6): its settled shot measures 85.90 % (rd_shot --void on the shot's 466 px frame;
     * stale 86.62 %, unset 90.49 %), the brief's about 86.0; its stills 0 86.43 %, 1 87.00 %, 3 86.24 % (12 h: the
     * date's AM suffix), the lowest less 2 points (the face-task guide's rule; the brief's expectation, 84) */
    {"console", 84.0f},
    /* Nav Scope (wave 2 Task 7): its settled shot measures 83.97 % (rd_shot --void on the shot's 466 px frame; stale
     * 84.08 %, unset 85.23 %), the brief's about 83.9; its stills 0 84.49 %, 1 84.33 %, 3 84.49 %, the lowest less 2
     * points (the face-task guide's rule; the brief's expectation, 81). The giant, the rims and the phosphor are its
     * light */
    {"scope", 82.0f},
    /* Vending Machine (wave 2 Task 8): its settled shot measures 64.32 % (rd_shot --void on the shot's 466 px frame;
     * stale 64.30 %, unset 67.77 %), the brief's about 65.4; ours by the same rule 63.53 / 63.60 / 66.98 (RGB565 lifts
     * the front's near-40 browns over the line). Its stills 0 62.85 %, 1 63.44 %, 3 62.06 % (12 h: the wider sticker),
     * the lowest less 2 points (the face-task guide's rule; the brief's expectation, 63) */
    {"vending", VM_VOID_FLOOR},
    /* the Bowl (wave 2 Task 9): its settled shot measures 32.86 % (rd_shot --void on the shot's 466 px frame; stale
     * 32.86 %, unset 31.71 %), the brief's about 33.6 (unset 32.4); ours by the same rule 31.43 / 31.43 / 30.24 (RGB565
     * lifts the wall's near-40 greys over the line). Its stills 0 31.46 %, 1 31.39 %, 3 31.46 %, the lowest less 2
     * points (the face-task guide's rule; the brief's expectation, 30) */
    {"bowl", CB_VOID_FLOOR},
    /* Mop Bot (wave 2 Task 10): its settled shot measures 67.34 % (rd_shot --void on the shot's 466 px frame; stale
     * 67.12 %, unset 89.75 %, 05:38 66.91 %), the brief's about 68; ours at the same instants 68.10 / 67.94 / 89.82 /
     * 67.72. Its stills 0 78.29 %, 1 66.24 % (03:47: the clean arc covers 47 minutes of the track), 3 78.21 %, the
     * lowest less 2 points (the face-task guide's rule; the brief's expectation, 66) */
    {"mopbot", MB_VOID_FLOOR},
};
/* lesson 5: each face's declared hit rects and what they tap (>= 44 px each side, inclusive at every edge). except
 * (wave 2 Task 6, ruling R165/I3, appended): the rect with every RD_TAP_CIRCLES circle of its face cut out (a target
 * nested inside it: Drive Console's button inside its face-wide strip tap), probed by a set of points outside the
 * circles instead of fk_test_tap_edges over the whole rect */
typedef struct { const char *key; omni_rect_t r; fk_tap_t act; bool except; } tap_t;
static const tap_t RD_TAPS[] = {
    {"stasis", {0, 0, W, W}, FK_TAP_CHIME, false},   /* the whole face: the chime preview (C9) */
    {"terminal", {64, 98, 402, 360}, FK_TAP_PULSE, false},   /* the glass (RD_TM_GLASS): the next log page, typed */
    {"deck", {80, 359, 386, 404}, FK_TAP_PULSE, false},       /* the notice plate (RD_DK_TAP): the next notice */
    {"hull", {96, 306, 372, 352}, FK_TAP_PULSE, false},       /* the readout above 6 (RD_HP_TAP): the next page */
    {"bunk", {128, 107, 338, 155}, FK_TAP_PULSE, false},      /* the snooze bar (RD_BK_HIT_BAR, 210 x 48): one more snooze */
    {"bunk", {136, 56, 184, 104}, FK_TAP_PULSE, false},       /* the roommate's lamp (RD_BK_HIT_LAMP, 48 x 48): the pause */
    {"curry", {65, 65, 401, 401}, FK_TAP_PULSE, false},       /* the tray (RD_CU_HIT, its box): one more chilli */
    {"bay", {0, 0, W, W}, FK_TAP_CHIME, false},               /* the whole face: the chime preview (C9), as Stasis Clock */
    {"drift", {0, 0, W, W}, FK_TAP_CHIME, false},             /* the whole face: the chime preview (C9), as Stasis Clock */
    /* the whole face but the button's circle (RD_TAP_CIRCLES): the strip's next page (tap_var moves the variant) */
    {"console", {0, 0, W, W}, FK_TAP_NEXT, true},
    {"scope", {0, 0, W, W}, FK_TAP_PULSE, false},   /* the whole face (RD_SC_TAP): the readout's next page */
    /* Vending Machine (wave 2 Task 8): its six buttons (RD_VM_BTN, 84 x 48 each): a press, its lamp, line and flap */
    {"vending", {101, 238, 185, 286}, FK_TAP_PULSE, false},
    {"vending", {191, 238, 275, 286}, FK_TAP_PULSE, false},
    {"vending", {281, 238, 365, 286}, FK_TAP_PULSE, false},
    {"vending", {101, 292, 185, 340}, FK_TAP_PULSE, false},
    {"vending", {191, 292, 275, 340}, FK_TAP_PULSE, false},
    {"vending", {281, 292, 365, 340}, FK_TAP_PULSE, false},
    /* the Bowl (wave 2 Task 9): the bowl's box (RD_CB_HIT, 432 x 432): the paw */
    {"bowl", {17, 17, 449, 449}, FK_TAP_PULSE, false},
};
/* wave 2 Task 6 (ruling R165/I3, appended): a circular target {key, cx, cy, r, act}; at (NULL: fixed at (cx, cy)) gives
 * its centre at a still's input, for a target that moves with the clock (built from each still's clock). Probed at
 * every awake still's input through tap_var (and through tap too while the centre is fixed): the four compass points at
 * exactly r hit (inclusive, M10) and at r + 1 miss (anything but act), and the four 45 degree points at r - 1 hit */
typedef struct {
    const char *key; int cx, cy, r; fk_tap_t act; void (*at)(const fk_face_in_t *in, int *cx, int *cy);
} tap_circle_t;
/* wave 2 Task 10: Mop Bot's robot at the input's clock, its centre to the nearest pixel (tap_var's own rounding) */
static void mb_robot_at(const fk_face_in_t *in, int *cx, int *cy)
{
    float x, y;
    rd_mb_robot_xy(&in->clk, &x, &y);
    *cx = (int)(x + 0.5f), *cy = (int)(y + 0.5f);
}
static const tap_circle_t RD_TAP_CIRCLES[] = {
    {"console", RD_DC_BTN_X, RD_DC_BTN_Y, RD_DC_BTN_R, FK_TAP_PULSE, NULL},   /* the button: the pulse (its messages) */
    /* Mop Bot (wave 2 Task 10, I3, W2-D12): the robot, r 36 (a 72 px target, C17), built at each still's clock and
     * probed through tap_var (its tap() taps nothing); cx, cy unused (at) */
    {"mopbot", 0, 0, RD_MB_HIT_R, FK_TAP_PULSE, mb_robot_at},
};
/* a face with tap_var: the rect its variant's change redraws (the Task 11 review's M2: the tap's two draws, the new
 * variant and then its pulse's start, each stay inside it), and a second one (r2, NULL none); hit: the RD_TAPS rect the
 * line is for (NULL: the face's one hit rect; Task 14: a face with two) */
/* shakes (the Task 15 review): the line's pulse moves what lies outside its boxes (Curry Clock's shudder, the whole
 * frame), so a probe whose pulse in flight draws outside them is checked for dirty completeness alone; every other line's
 * pulse must stay inside its boxes (tap_probe asserts both ways) */
/* keeps (wave 2 Task 6, appended): the line's tap starts its pulse and leaves the variant (Drive Console's button: its
 * messages, the strip's page untouched), so tap_probe asserts FK_TAP_PULSE and the variant unmoved instead of a move */
/* shows_ms (wave 2 Task 8, appended): the line's variant shows only while its pulse plays (Vending Machine: the last
 * press's lamp and line, MYSTERY's item in the tray), so whether a state hides it is derived from frames pulsing at
 * shows_ms; 0: from frames at rest, as before */
/* at (wave 2 Task 10, appended): the line's target moves with the clock (Mop Bot's robot), so a probe taps at at(in),
 * its centre at the probed input, not at its hit rect's centre; NULL: the hit rect's centre, as before */
typedef struct {
    const char *key; const omni_rect_t *r, *r2, *hit; bool shakes, keeps; uint16_t shows_ms;
    void (*at)(const fk_face_in_t *in, int *x, int *y);
} tap_var_t;
static const omni_rect_t BK_HIT_BAR = {128, 107, 338, 155}, BK_HIT_LAMP = {136, 56, 184, 104};
static const omni_rect_t CB_SCREEN = {0, 0, W, W};
/* Mop Bot (wave 2 Task 10): the robot's box at the probes' clock (every tap_pair state but unset is at m 9, s 30) and
 * parked (unset), each at the hop's largest (rd_mb_robot_box): filled in main() from the face, before any row runs */
static omni_rect_t MB_BOX_0930, MB_BOX_PARK;
static const tap_var_t RD_TAP_VARS[] = {
    {"terminal", &RD_TM_LOG, NULL, NULL, false, false, 0, NULL},
    {"deck", &RD_DK_NOTICE, NULL, NULL, false, false, 0, NULL},
    {"hull", &RD_HP_READOUT, NULL, NULL, false, false, 0, NULL},
    {"bunk", &RD_BK_BAR, &RD_BK_COUNT, &BK_HIT_BAR, false, false, 0, NULL},   /* the press and the count line */
    {"bunk", &RD_BK_ROOM, &RD_BK_COUNT, &BK_HIT_LAMP, false, false, 0, NULL},   /* the lamp, its label; a snooze's message cut short */
    {"curry", &RD_CU_STICKER, &RD_CU_STEAM, NULL, true, false, 0, NULL}, /* the chillies; the steam's red at three and back */
    /* Drive Console (wave 2 Task 6): its targets nest (the button's circle inside the strip's face-wide tap), so each
     * line names its own hit: the button's box (its centre is the circle's) and the strip's rect (outside the circle).
     * The button's pulse sinks it and shows its messages on the strip; the strip's page change redraws the strip, and
     * a pulse in flight over it also moves the button (r2) */
    {"console", &RD_DC_BTN_BOX, &RD_DC_STRIP, &RD_DC_BTN_HIT, false, true, 0, NULL},
    {"console", &RD_DC_STRIP, &RD_DC_BTN_BOX, &RD_DC_STRIP, false, false, 0, NULL},
    {"scope", &RD_SC_READOUT, NULL, NULL, false, false, 0, NULL},   /* the readout's page and its fade (RD_SC_TAP: the face) */
    /* Vending Machine (wave 2 Task 8): a press redraws its button, its lamp and the ticker's line (RD_VM_PANEL) and,
     * MYSTERY's, the flap's tray (RD_VM_FLAP); its variant (the last press, MYSTERY's next item) shows only while the
     * pulse plays (shows_ms). Three of the six buttons (one each row's end, and MYSTERY): the other three are the same
     * code at their own rects (RD_TAPS probes all six) */
    {"vending", &RD_VM_PANEL, &RD_VM_FLAP, &RD_VM_BTN[0], false, false, 100, NULL},
    {"vending", &RD_VM_PANEL, &RD_VM_FLAP, &RD_VM_BTN[4], false, false, 100, NULL},
    {"vending", &RD_VM_PANEL, &RD_VM_FLAP, &RD_VM_BTN[RD_VM_MYSTERY_BTN], false, false, 100, NULL},
    /* the Bowl (wave 2 Task 9): a tap summons the paw and leaves the variant (keeps: the face has none). The paw
     * turns with the minute and its run reaches past the screen's edge (272 px out at reach 0), so its box is the
     * screen; test_rd_cb_paw_schedule holds every run's rects inside rd_cb_paw_rect(m) and the floor at each minute */
    {"bowl", &CB_SCREEN, NULL, NULL, false, true, 0, NULL},
    /* Mop Bot (wave 2 Task 10): a tap on the robot (at its place at the input's clock: at) startles it and leaves the
     * variant (keeps: the face has none); its hop and spin stay inside the robot's box (not shakes), at m 9 (the
     * probes' clock) or parked (unset); hit: the screen (any rect: at gives the point) */
    {"mopbot", &MB_BOX_0930, &MB_BOX_PARK, &CB_SCREEN, false, true, 0, mb_robot_at},
};
/* a face with tap_var: its number of variants (a tap moves the variant on modulo n). Whether a state hides the tapped
 * text is not declared: tap_pair derives it from the frames (the Task 13 review) */
typedef struct { const char *key; uint16_t n; } tap_count_t;
static const tap_count_t RD_TAP_COUNTS[] = {
    {"terminal", RD_TM_PAGES},
    {"deck", RD_DK_NOTES},
    {"hull", RD_HP_PAGES},
    {"console", RD_DC_PAGES},   /* the strip's pages: the date, RESERVE, the last sync */
    {"scope", RD_SC_PAGES},     /* the readout's four pages */
};
/* Task 14: a face whose variant is not a page number (Bunk Clock's bits: RW13) lists the variants its taps and the
 * dirty checks run from instead, 0 first; a tap then only has to move it */
#define TAP_VLIST_MAX 8
typedef struct { const char *key; uint16_t v[TAP_VLIST_MAX]; unsigned n; } tap_vlist_t;
static const tap_vlist_t RD_TAP_VLIST[] = {
    {"curry", {0, 1, 2, 3}, 4},   /* the chillies (bits 0-1) */
    /* none, a snooze, the lamp's pulse (projected), still 11 (paused, a snooze), paused by a lamp tap after 5 snoozes */
    {"bunk", {0, 0x0001, 0x4000, 0x8001, 0xC005}, 5},
    /* Vending Machine (wave 2 Task 8: bits 0-2 the last press, 3-5 MYSTERY's next item, 6 the press parity): none yet
     * (COFFEE, item 0), still 11 (MYSTERY, one sock), MYSTERY showing the IOU (next 5) and the flat battery (next 0, the
     * wrap), TEA with the pea next */
    {"vending", {0, RD_VM_TAPPED_VARIANT, 5 | (5 << 3), 5, 1 | (2 << 3) | RD_VM_VAR_PARITY}, 5},
    /* the Bowl (wave 2 Task 9): no variant (its taps keep it): 0 alone */
    {"bowl", {0}, 1},
    /* Mop Bot (wave 2 Task 10): no variant (its taps keep it): 0 alone */
    {"mopbot", {0}, 1},
};
static const tap_vlist_t *tap_vlist_of(const char *key)
{
    for (unsigned i = 0; i < sizeof RD_TAP_VLIST / sizeof RD_TAP_VLIST[0]; i++)
        if (strcmp(RD_TAP_VLIST[i].key, key) == 0) return &RD_TAP_VLIST[i];
    return NULL;
}
static const tap_count_t *tap_count_of(const char *key)
{
    for (unsigned i = 0; i < sizeof RD_TAP_COUNTS / sizeof RD_TAP_COUNTS[0]; i++)
        if (strcmp(RD_TAP_COUNTS[i].key, key) == 0) return &RD_TAP_COUNTS[i];
    TEST_FAIL_MESSAGE("a face with tap_var declares no RD_TAP_COUNTS or RD_TAP_VLIST line");
    return NULL;
}
/* the variants a face's tap checks run over: its RD_TAP_VLIST line, else 0 .. n - 1 of its RD_TAP_COUNTS line */
static unsigned tap_variants(const char *key, uint16_t out[TAP_VLIST_MAX + 8])
{
    const tap_vlist_t *l = tap_vlist_of(key);
    if (l) {
        for (unsigned i = 0; i < l->n; i++) out[i] = l->v[i];
        return l->n;
    }
    const tap_count_t *c = tap_count_of(key);
    unsigned n = 0;
    for (uint16_t q = 0; q < c->n && n < TAP_VLIST_MAX + 8; q++) out[n++] = q;
    return n;
}
/* the chime picture's declared rects, per face */
typedef struct { const char *key; unsigned (*rects)(omni_rect_t out[RD_ST_CHIME_MAX]); } chime_t;
static const chime_t RD_CHIMES[] = {
    {"stasis", rd_st_chime_rects},
    {"terminal", rd_tm_chime_rects},
    {"deck", rd_dk_chime_rects},
    {"hull", rd_hp_chime_rects},
    {"bunk", rd_bk_chime_rects},
    {"curry", rd_cu_chime_rects},
    {"bay", rd_by_chime_rects},
    {"drift", rd_dp_chime_rects},
    {"console", rd_dc_chime_rects},
    {"scope", rd_sc_chime_rects},
    {"vending", rd_vm_chime_rects},
    {"bowl", rd_cb_chime_rects},
    {"mopbot", rd_mb_chime_rects},
};
/* wave 2 Task 3 (lesson 15, I4; ruling R170): a face with a large static-bright risk renders rest frames every step_s
 * over window_s from still 0's instant; the share of the disc whose pixels have a channel above 40 and are equal in
 * every sample stays at or under its pin (ceil: the measurement plus a point) and under the theme's absolute ceiling,
 * RD_STATIC_BRIGHT_ABS (10 %: R170; the owner-approved Stasis Clock measures 9.22 % by this rule, its hero time and
 * labels; over it, the face stops and reports, R146). The measure leaves out the regions that carry one of spec
 * section 10.0's burn-in motions (moving: Bay Mouth's star band, shifted 3 px over 6 min, and its crawling ring):
 * those move by the spec's rule, and a smooth RGB565 gradient moved a pixel reads the same while it wears evenly.
 * The disc stays the denominator. R185 (wave 2 Task 9) re-grounds the ceiling: a face's absolute ceiling is its own
 * mockup's settled shot measured by this same rule (the same moving regions left out) plus 1 point, given as the line's
 * abs_ceil; RD_STATIC_BRIGHT_ABS (10 %) stays the default, for faces whose mockup measures under 9 %. Drive Console's
 * 10.5 % (R178) and Vending Machine's 32 % (R183) predate R185 and stand as ruled (neither is its mockup plus a point);
 * and R185 keeps R169's area mean (a large lit or warm area's mean largest channel <= 115) beside this measure (the
 * Task 9 review's m4) */
#define RD_STATIC_BRIGHT_ABS 10.0f
/* abs_ceil (R178, appended): the face's own absolute ceiling where a ruling gives one; 0 is the theme's
 * RD_STATIC_BRIGHT_ABS */
typedef struct {
    const char *key; int window_s, step_s; float ceil; bool (*moving)(int x, int y); float abs_ceil;
} static_bright_t;
static bool by_moving(int x, int y);
static bool dp_moving(int x, int y);
static bool sc_moving(int x, int y);
static bool mb_moving(int x, int y);
static const static_bright_t RD_STATIC_BRIGHT[] = {
    /* Bay Mouth: measured 8.33 % (14 203 px) with its star band and ring left out (R170): the time 5.89 % (10: and the
     * minutes' shared strokes over 10:09-10:21), the date, caption and label 1.48 %, the rivets and the lip's steel
     * line about 1 %. Pinned at the measurement plus a point. (With the band and the ring counted it was 16.29 %, the
     * band 7.79 %: a smooth RGB565 photosphere reads the same at every shift of 0..3 px) */
    {"bay", 720, 60, 9.33f, by_moving, 0.f},
    /* Drift Plate (wave 2 Task 5): measured 4.10 % (6 992 px over 61 samples, 10 s apart over 600 s) with its band
     * left out (R170: it drifts 1 px every 10 s), the time and WASH ME over it counted: the time, the labels, the
     * date and the caption. Pinned at the measurement plus a point */
    {"drift", 600, 10, 5.10f, dp_moving, 0.f},
    /* Drive Console (wave 2 Task 6; R178): measured 10.10 % (17 226 px over 61
     * samples, 10 s apart over 600 s), over the 10 % ceiling by 168 px. No region carries a spec burn-in motion (the
     * collar's crawl and the lamps change their own pixels between samples). By region: the hour's two digits and
     * their glow 2.78 %, the panel's frame, labels, rivets and gauge faces 2.05 %, the tapes 1.13 %, DRIVE 2 .88 %, the
     * strip's date .88 %, the button .77 %, the lit lamps and rings .65 %, the RESERVE needle and its gauge .61 %, the
     * SEC gauge's hub and face .35 %. The design's own light: the mockup's settled shot is within .05 point of ours by
     * the void rule. R178: the face's own ceiling is 10.5 % (the light matches the design within .05 point; the .1
     * point over the theme's 10 % is mostly the hour's digits; nothing dimmed). The measurement plus a point (11.10)
     * passes that ceiling, so the pin is the ceiling itself */
    {"console", 600, 10, 10.50f, NULL, 10.5f},
    /* Nav Scope (wave 2 Task 7): measured 5.86 % (10 000 px over 61 samples, 10 s apart over 600 s) with the giant
     * left out (R170: its corona breathes +-8 % over 12 s, spec 10.0's burn-in motion for it; its pixels change between
     * samples anyway): the numerals, the five-minute ticks, the glass rim and the readout's two lines (page 0 holds
     * through the window) are its static light; the sweep and its fan pass over the rest of the glass. Pinned at the
     * measurement plus a point */
    {"scope", 600, 10, 6.86f, sc_moving, 0.f},
    /* Vending Machine (wave 2 Task 8; R183): measured 30.59 % (52 181 px over 61 samples, 10 s apart over 600 s). No
     * region carries a spec burn-in motion. The design's own light: the bezel, the six buttons (their faces' top rows
     * over 40, the SOLD OUT lamps and labels at 1), the lightbox and its legend, the hour's digits and the ghost 8s'
     * glow, the sticker, the flap and the coin slot; the mockup's settled shot measures 28.96 % static by the same rule
     * (moving regions left out). R183: the face's own ceiling is 32 % (abs_ceil): the rule counts any channel above
     * 40, and the front is a large dim panel (its R169 area mean 30.5 of 255); and the full face shows for at most 15 s
     * a lift before the glance (2.70 %) takes over. Nothing dimmed. The pin is min(measured + 1, 32) */
    {"vending", 600, 10, VM_SB_PIN, NULL, 32.f},
    /* the Bowl (wave 2 Task 9; R185, R186): measured 56.74 % (96 787 px over 61 samples, 10 s apart over 600 s).
     * No region carries a spec burn-in motion. The design's own light, the bowl itself: the lip 21.88 %, the wall
     * 18.26 %, the floor 16.60 % (R169 layer mean 63.0 of 255, the lip 97.1). The mockup's settled shot measures 59.38 %
     * by the same rule (our 600 s static mask over it), so by R185 the face's ceiling is 60.38 % (abs_ceil); the pin is
     * the measurement plus a point. Nothing dimmed */
    {"bowl", 600, 10, CB_SB_PIN, NULL, 60.38f},
    /* Mop Bot (wave 2 Task 10; R185, R188): measured 13.31 % (22 702 px over 61 samples, 10 s apart over 600 s) from
     * still 0 (10:09:30), the hazard ring left out (R170: it crawls 1 px a second). The light: the hour's stencil digits
     * and the colon, the clean wet arc from 12 to the robot (#20262B: its blue packs to 41, just over the rule's 40),
     * the ticks, the date, the sign and 30. The arc grows with the minute, so this window is the face's least static;
     * R188 measures it instant for instant, and its worst window is RD_STATIC_BRIGHT_WORST's line. Here the ceiling is
     * R185's at the mockup's settled shot (10:42: 26.22 % by this rule, our mask; ours 25.18 % at that instant) plus a
     * point; the pin is the measurement plus a point */
    {"mopbot", 600, 10, MB_SB_PIN, mb_moving, MB_SB_CEIL},
};

/* ---- the rows: every registered face, bound once over the committed assets */
typedef struct { const fk_face_ops_t *ops; void *ctx; } row_t;
static row_t s_rows[16];
static unsigned s_nrows;

static void bind_all(void)
{
    TEST_ASSERT_TRUE(rd_assets_host_open(RD_ASSETS_DIR));
    s_nrows = 0;
    for (unsigned i = 0; i < RD_FACE_COUNT; i++) {
        void *ctx = calloc(1, RD_FACES[i]->ctx_bytes ? RD_FACES[i]->ctx_bytes : 1);
        TEST_ASSERT_NOT_NULL(ctx);
        TEST_ASSERT_TRUE_MESSAGE(RD_FACES[i]->bind(ctx, rd_assets_host_get), RD_FACES[i]->key);
        TEST_ASSERT_TRUE(s_nrows < sizeof s_rows / sizeof s_rows[0]);
        s_rows[s_nrows++] = (row_t){RD_FACES[i], ctx};
    }
}

static const row_t *row_of(const char *key)
{
    for (unsigned i = 0; i < s_nrows; i++)
        if (strcmp(s_rows[i].ops->key, key) == 0) return &s_rows[i];
    TEST_FAIL_MESSAGE("no such face row");
    return NULL;
}

/* ---- inputs */
static fk_face_in_t in_at(int h, int m, int s, int ms)
{
    fk_face_in_t in;
    fk_test_in_at(&in, h, m, s, ms, false, false);
    in.h24 = true;
    in.clk.yday = 305;
    in.slept_s = -1;
    return in;
}

/* in moved by ms, across midnight too: a set clock's date steps a day (a 28-day February: no face reads a year) */
static fk_face_in_t shifted(fk_face_in_t in, long ms)
{
    static const uint8_t DIM[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const long day = 86400000L;
    long t = ((in.clk.h * 60L + in.clk.m) * 60L + in.clk.s) * 1000L + in.clk.ms + ms;
    const int days = t < 0 ? -1 : t >= day ? 1 : 0;
    t -= days * day;
    in.clk.ms = (uint16_t)(t % 1000), t /= 1000;
    in.clk.s = (uint8_t)(t % 60), t /= 60;
    in.clk.m = (uint8_t)(t % 60), t /= 60;
    in.clk.h = (uint8_t)t;
    if (days && in.clk.set && in.clk.mon < 12) {
        in.clk.wday = (uint8_t)((in.clk.wday + 7 + days) % 7);
        in.clk.yday = (uint16_t)(in.clk.yday + days);
        if (days > 0 && ++in.clk.mday > DIM[in.clk.mon]) in.clk.mday = 1, in.clk.mon = (uint8_t)((in.clk.mon + 1) % 12);
        if (days < 0 && --in.clk.mday < 1) in.clk.mon = (uint8_t)((in.clk.mon + 11) % 12), in.clk.mday = DIM[in.clk.mon];
    }
    return in;
}

/* in's free-running channels moved on by dt ms (the Task 9 review's I2: motion that reads no clock): mono_ms (wrapping),
 * fresh_ms while a revealing wake is fresh (it ends at 60 s), a running wake_ms and pulse_ms; dt <= 0 moves nothing */
static fk_face_in_t ran(fk_face_in_t in, long dt)
{
    if (dt <= 0) return in;
    in.mono_ms += (uint32_t)dt;
    if (in.fresh) {
        const long f = (long)in.fresh_ms + dt;
        if (f < 60000) in.fresh_ms = (uint16_t)f;
        else in.fresh = false, in.fresh_ms = 0;
    }
    if (in.wake_ms != UINT32_MAX) in.wake_ms += (uint32_t)dt;
    if (in.pulsing) in.pulse_ms = (uint16_t)(in.pulse_ms + dt > 60000 ? 60000 : in.pulse_ms + dt);
    return in;
}

/* the free-running variants every lesson row is also run on: no fresh wake, a fresh wake just begun, one 3 s old */
typedef struct { bool fresh; uint16_t fresh_ms; uint32_t mono_ms; } run_t;
static const run_t RUNS[] = {{false, 0, 987654u}, {true, 0, 41234u}, {true, 3000, 4294960000u}};
#define N_RUNS (sizeof RUNS / sizeof RUNS[0])
static fk_face_in_t with_run(fk_face_in_t in, const run_t *v)
{
    in.fresh = v->fresh, in.fresh_ms = v->fresh_ms, in.mono_ms = v->mono_ms;
    return in;
}

typedef void (*draw_fn)(const void *, const fk_face_in_t *, omni_rect_t, uint16_t *, int);

static void frame(const row_t *r, const fk_face_in_t *in, uint16_t *fb) { fk_test_frame(r->ops, r->ctx, in, fb); }

static void in_clips(const row_t *r, const fk_face_in_t *in, int rows, int cols, uint16_t *fb)
{
    const draw_fn fn = in->glance ? r->ops->glance : r->ops->draw;
    memset(fb, 0xC3, sizeof s_a);
    for (int y0 = 0; y0 < W; y0 += rows)
        for (int x0 = 0; x0 < W; x0 += cols) {
            const omni_rect_t c = {(int16_t)x0, (int16_t)y0, (int16_t)(x0 + cols < W ? x0 + cols : W),
                                   (int16_t)(y0 + rows < W ? y0 + rows : W)};
            fn(r->ctx, in, c, fb + (size_t)y0 * W + x0, W);
        }
}

static void assert_same(const uint16_t *want, const uint16_t *got, const char *what)
{
    for (int i = 0; i < W * W; i++)
        if (want[i] != got[i]) {
            char m[256];
            snprintf(m, sizeof m, "%s: pixel (%d, %d) want %04x got %04x", what, i % W, i / W, want[i], got[i]);
            TEST_FAIL_MESSAGE(m);
        }
}

static bool inside(const omni_rect_t *r, int x, int y) { return x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1; }

static bool in_any(const omni_rect_t *r, unsigned n, int x, int y)
{
    for (unsigned k = 0; k < n; k++)
        if (inside(&r[k], x, y)) return true;
    return false;
}

static long frames_differ(const uint16_t *a, const uint16_t *b)
{
    long n = 0;
    for (int i = 0; i < W * W; i++) n += a[i] != b[i];
    return n;
}

/* every pixel that differs between the frames for a and b lies inside dirty(a, b) (a's kind: draw or glance); returns
 * the number of rects */
static unsigned dirty_pair(const row_t *r, const fk_face_in_t *a, const fk_face_in_t *b, const char *what,
                           omni_rect_t rects[8])
{
    omni_rect_t own[8];
    if (!rects) rects = own;
    const uint8_t n = r->ops->dirty(r->ctx, a, b, rects);
    TEST_ASSERT_TRUE_MESSAGE(n <= 8, what);
    frame(r, a, s_a);
    fk_face_in_t bb = *b;
    frame(r, &bb, s_b);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (s_a[y * W + x] == s_b[y * W + x] || in_any(rects, n, x, y)) continue;
            char m[200];
            snprintf(m, sizeof m, "%s: pixel (%d, %d) differs outside the %u dirty rects", what, x, y, n);
            TEST_FAIL_MESSAGE(m);
        }
    return n;
}

/* ---- the lessons, on every row */
static void band_exact(const char *key)
{
    const row_t *r = row_of(key);
    for (unsigned i = 0; i < rd_still_count(r->ops); i++) {
        rd_still_t st;
        rd_still(i % RD_STILL_N, &st);
        if (i >= RD_STILL_N) {   /* the tapped still (rd_still_frame's rule; wave 2 Task 9: its pulse too) */
            st.in.variant = rd_still_variant(r->ops);
            if (rd_still_pulse_ms(r->ops)) st.in.pulsing = true, st.in.pulse_ms = rd_still_pulse_ms(r->ops);
        }
        rd_still_frame(r->ops, r->ctx, i, s_a);
        in_clips(r, &st.in, 8, W, s_b);
        assert_same(s_a, s_b, st.what);
        in_clips(r, &st.in, 6, 37, s_b);   /* odd blocks too */
        assert_same(s_a, s_b, st.what);
    }
    fk_face_in_t in = in_at(10, 9, 30, 500);   /* the chime picture, band-exact too */
    in.thrum = 0.6f, in.beep = 0.8f;
    frame(r, &in, s_a);
    in_clips(r, &in, 8, W, s_b);
    assert_same(s_a, s_b, "the chime picture");
    /* frames in motion too (the Task 14 review): the second half of a minute's change (m:00.300: Bunk Clock's flip
     * past its hinge, at the hour too), mid-wake (350 and 600 ms), and a tapped variant's pulse 100 ms in (Curry Clock's
     * shudder); each built in 8-row bands and in odd blocks */
    const uint16_t tv = rd_still_variant(r->ops);
    static const struct { int h, m, s, ms; uint32_t wake; bool pulse; const char *what; } MOV[] = {
        {10, 10, 0, 300, UINT32_MAX, false, "m:00.300"}, {11, 0, 0, 300, UINT32_MAX, false, "the hour's m:00.300"},
        {10, 42, 3, 0, 350, false, "350 ms into the wake"}, {10, 42, 3, 0, 600, false, "600 ms into the wake"},
        {10, 42, 3, 0, UINT32_MAX, true, "a tapped variant's pulse at 100 ms"},
    };
    for (unsigned k = 0; k < sizeof MOV / sizeof MOV[0]; k++) {
        in = in_at(MOV[k].h, MOV[k].m, MOV[k].s, MOV[k].ms);
        in.wake_ms = MOV[k].wake;
        if (MOV[k].pulse) in.variant = tv, in.pulsing = true, in.pulse_ms = 100;
        frame(r, &in, s_a);
        in_clips(r, &in, 8, W, s_b);
        assert_same(s_a, s_b, MOV[k].what);
        in_clips(r, &in, 6, 37, s_b);
        assert_same(s_a, s_b, MOV[k].what);
    }
}

/* Lesson 1: from each still instant and from 00:00:00.000 and 12:00:59.960, every differing pixel lies inside
 * dirty(a, b) for a forward step, skips, back-steps, the hour and midnight; awake and in the glance; in the still's own
 * 12/24 h and the other; with the free-running channels (mono_ms, fresh_ms) moved by one frame (a sync step) or by the
 * step itself (that long went by), from no fresh wake, a fresh one and a 3 s old one (the Task 9 review's I2) */
static void dirty_jumps_v(const char *key, uint16_t variant)
{
    const row_t *r = row_of(key);
    static const long step[] = {40, 1000, 7000, -1000, -3000, 61000, 3600000};
    for (unsigned b = 0; b < RD_STILL_N + 2; b++)
        for (unsigned v = 0; v < N_RUNS; v++) {
            fk_face_in_t base;
            if (b < RD_STILL_N) {
                rd_still_t st;
                rd_still(b, &st);
                base = st.in;
            } else {
                base = b == RD_STILL_N ? in_at(0, 0, 0, 0) : in_at(12, 0, 59, 960);
            }
            base = with_run(base, &RUNS[v]);
            base.variant = variant;
            for (int hm = 0; hm < 2; hm++) {
                if (hm) base.h24 = !base.h24;
                for (int g = 0; g < 2; g++) {
                    base.glance = g != 0;
                    char what[192];
                    for (unsigned k = 0; k < sizeof step / sizeof step[0]; k++)
                        for (int mv = 0; mv < 2; mv++) {
                            const fk_face_in_t to = ran(shifted(base, step[k]), mv ? step[k] : 40);
                            snprintf(what, sizeof what,
                                     "%s %s %s from %02u:%02u:%02u.%03u%s by %+ld ms (run %u, mono +%ld, variant %u)",
                                     key, g ? "glance" : "draw", base.h24 ? "24 h" : "12 h", base.clk.h, base.clk.m,
                                     base.clk.s, base.clk.ms, base.clk.set ? "" : " (unset)", step[k], v,
                                     mv ? step[k] : 40L, (unsigned)variant);
                            dirty_pair(r, &base, &to, what, NULL);
                        }
                    fk_face_in_t a = base;   /* hh:59:59.900 -> hh+1:00:00.000 */
                    a.clk.m = 59, a.clk.s = 59, a.clk.ms = 900;
                    fk_face_in_t to = ran(shifted(a, 100), 100);
                    snprintf(what, sizeof what, "%s %s %s %02u:59:59.900 -> the hour (run %u)", key,
                             g ? "glance" : "draw", base.h24 ? "24 h" : "12 h", a.clk.h, v);
                    dirty_pair(r, &a, &to, what, NULL);
                    a.clk.h = 23, a.clk.ms = 0;   /* 23:59:59 -> 00:00:00 on the next date */
                    to = ran(shifted(a, 1000), 1000);
                    snprintf(what, sizeof what, "%s %s %s 23:59:59 -> 00:00:00 the next day (run %u)", key,
                             g ? "glance" : "draw", base.h24 ? "24 h" : "12 h", v);
                    dirty_pair(r, &a, &to, what, NULL);
                    a.clk.h = 11;   /* 11:59:59 -> 12:00:00: AM to PM */
                    to = ran(shifted(a, 1000), 1000);
                    dirty_pair(r, &a, &to, "11:59:59 -> noon", NULL);
                }
            }
        }
}

static void dirty_jumps(const char *key) { dirty_jumps_v(key, 0); }

/* the clock's state, the readout, the chime and the orbit: still covered; the glance's end redraws the whole frame */
static void dirty_states_v(const char *key, uint16_t variant)
{
    const row_t *r = row_of(key);
    fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
    a.variant = b.variant = variant;
    b.clk.stale = true;
    dirty_pair(r, &a, &b, "stale", NULL);
    b = a, b.clk.set = false;
    dirty_pair(r, &a, &b, "unset", NULL);
    dirty_pair(r, &b, &a, "set", NULL);
    b = a, b.slept_s = 12307;
    dirty_pair(r, &a, &b, "slept", NULL);
    b = a, b.h24 = false;
    dirty_pair(r, &a, &b, "12 h", NULL);
    b = a, b.reduced = true;
    dirty_pair(r, &a, &b, "reduced", NULL);
    dirty_pair(r, &b, &a, "reduced off", NULL);
    /* the free-running channels alone (I2): mono_ms, a fresh wake starting, running and ending, the wake's own time
     * and a pulse */
    static const long DT[] = {40, 100, 960, 1000, 7000};
    for (unsigned k = 0; k < sizeof DT / sizeof DT[0]; k++) {
        b = ran(a, DT[k]);
        dirty_pair(r, &a, &b, "mono_ms", NULL);
        fk_face_in_t f = a;
        f.fresh = true, f.fresh_ms = 0;
        b = ran(f, DT[k]);
        dirty_pair(r, &f, &b, "fresh_ms", NULL);
        f.fresh_ms = 59990;
        b = ran(f, DT[k]);
        dirty_pair(r, &f, &b, "fresh's end", NULL);
        f = a, f.wake_ms = 0;
        b = ran(f, DT[k]);
        dirty_pair(r, &f, &b, "wake_ms", NULL);
        f = a, f.pulsing = true, f.pulse_ms = 0;
        b = ran(f, DT[k]);
        dirty_pair(r, &f, &b, "pulse_ms", NULL);
    }
    b = a, b.fresh = true, b.fresh_ms = 0;
    dirty_pair(r, &a, &b, "a fresh wake", NULL);
    dirty_pair(r, &b, &a, "fresh ends", NULL);
    b = a, b.wake_ms = 600;
    dirty_pair(r, &b, &a, "the wake's end", NULL);
    dirty_pair(r, &a, &b, "a wake", NULL);
    static const float th[] = {0.f, 0.1f, 0.55f, 1.f};
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 4; j++) {
            fk_face_in_t c = a, d = shifted(a, 40);
            c.thrum = th[i], d.thrum = th[j];
            c.beep = th[j], d.beep = th[i];
            dirty_pair(r, &c, &d, "the chime", NULL);
            d.clk.stale = true;
            dirty_pair(r, &c, &d, "the chime, stale", NULL);
        }
    a.glance = b.glance = true;
    b = a, b.clk.stale = true;
    dirty_pair(r, &a, &b, "stale glance", NULL);
    b = a, b.odx = 4, b.ody = -4;
    dirty_pair(r, &a, &b, "the orbit", NULL);
    b = a, b.clk.set = false;
    dirty_pair(r, &a, &b, "unset glance", NULL);
    b = a, b.glance = false;   /* the glance ending: the whole frame */
    omni_rect_t rects[8];
    const unsigned n = dirty_pair(r, &a, &b, "the glance's end", rects);
    TEST_ASSERT_TRUE_MESSAGE(n == 1 && rects[0].x0 <= 0 && rects[0].y0 <= 0 && rects[0].x1 >= W && rects[0].y1 >= W,
                             "the glance's end is not the whole frame");
}

static void dirty_states(const char *key) { dirty_states_v(key, 0); }

/* wave 2 Task 9: every RD_TAP_VARS line of the face keeps (and it has one) */
static bool all_keeps(const char *key)
{
    unsigned n = 0, kept = 0;
    for (unsigned i = 0; i < sizeof RD_TAP_VARS / sizeof RD_TAP_VARS[0]; i++)
        if (strcmp(RD_TAP_VARS[i].key, key) == 0) n++, kept += RD_TAP_VARS[i].keeps;
    return n > 0 && kept == n;
}

/* the Task 11 review's M2, for every face with tap_var: dirty_jumps and dirty_states once more at each of the face's
 * other variants, 1 .. n - 1 (the Task 12 review: not only the tapped still's) */
static void dirty_tapped(const char *key)
{
    const row_t *r = row_of(key);
    if (!r->ops->tap_var) return;
    const uint16_t v = rd_still_variant(r->ops);
    /* wave 2 Task 9 (appended): a face whose every line keeps (its tap only starts its pulse: the Bowl's paw) has no
     * variant to name and no other variant to run from (its RD_TAP_VLIST is {0}); its tapped still plays its pulse */
    if (all_keeps(key)) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, v, "an all-keeps face names a tapped variant");
        TEST_ASSERT_TRUE_MESSAGE(r->ops->stills_extra == 1 && rd_still_pulse_ms(r->ops) != 0 &&
                                     rd_still_pulse_ms(r->ops) < r->ops->pulse_len_ms,
                                 "an all-keeps face's tapped still is no pulse frame (rd_still_pulse_ms)");
        uint16_t vs[TAP_VLIST_MAX + 8];
        TEST_ASSERT_TRUE_MESSAGE(tap_variants(key, vs) == 1 && vs[0] == 0, "an all-keeps face lists a variant");
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(v != 0, "a face with tap_var names its tapped variant (rd_still_variant)");
    uint16_t vs[TAP_VLIST_MAX + 8];
    const unsigned nv = tap_variants(key, vs);
    bool listed = false;
    for (unsigned k = 0; k < nv; k++) listed = listed || vs[k] == v;
    TEST_ASSERT_TRUE_MESSAGE(listed, "the tapped still's variant is one the checks run from");
    for (unsigned k = 0; k < nv; k++) {
        if (!vs[k]) continue;
        dirty_jumps_v(key, vs[k]);
        dirty_states_v(key, vs[k]);
    }
}

/* the Task 11 review's M2: a tap through tap_var draws twice in one handler (fk_face_gesture: the new variant, then the
 * pulse's start when the face has one). Each draw's dirty rects cover what changed and lie inside the face's RD_TAP_VARS
 * rect: from each variant, awake, 24 and 12 h, set and stale, the new variant not yet pulsing, then pulsing at 0.
 * In flight (the Task 14 review, and the controller's ruling for Task 15): a tap also lands while a pulse plays, at half
 * its length (the variant set directly) and 100 ms after a tap on each of the face's hit rects (the pulse its own). The
 * RD_TAP_VARS box rule covers the variant's change; a cross tap in flight (on another rect than the pulse's own: Bunk
 * Clock's lamp during a snooze's press) is checked for dirty completeness alone (dirty_pair), not for the box, and so is
 * a tap on a line declared shakes whose pulse in flight draws outside the line's boxes (frame(x) differs from x at rest
 * there: Curry Clock's shudder, the whole frame). An undeclared line's pulse never may (asserted) */
static long differ_in(const uint16_t *a, const uint16_t *b, const omni_rect_t *box);
static bool rect_within(const omni_rect_t *r, const omni_rect_t *box)
{
    return box && r->x0 >= box->x0 && r->y0 >= box->y0 && r->x1 <= box->x1 && r->y1 <= box->y1;
}
/* the RD_TAPS rect a line taps (its hit, or the face's one) */
static const omni_rect_t *hit_of(const char *key, const tap_var_t *tv)
{
    if (tv->hit) return tv->hit;
    for (unsigned i = 0; i < sizeof RD_TAPS / sizeof RD_TAPS[0]; i++)
        if (strcmp(RD_TAPS[i].key, key) == 0) return &RD_TAPS[i].r;
    return NULL;
}
/* a pulse in flight in x draws outside the line's boxes (x against x at rest) */
static bool pulse_outside(const row_t *r, const tap_var_t *tv, const fk_face_in_t *x)
{
    if (!x->pulsing) return false;
    fk_face_in_t q = *x;
    q.pulsing = false, q.pulse_ms = 0;
    frame(r, x, s_a);
    frame(r, &q, s_b);
    const omni_rect_t ok[2] = {*tv->r, tv->r2 ? *tv->r2 : *tv->r};
    for (int y = 0; y < W; y++)
        for (int xx = 0; xx < W; xx++)
            if (s_a[y * W + xx] != s_b[y * W + xx] && !in_any(ok, 2, xx, y)) return true;
    return false;
}
/* wave 2 Task 9 (the Task 8 review's M1): the probes of a shows_ms line that found the state not hiding the variant */
static unsigned s_shown;
/* one tap on the line's rect from a (its state, its variant, any pulse in flight); boxed: the box rule applies (not a
 * cross tap). Returns whether the line's declared shake exempted it */
static bool tap_probe(const row_t *r, const char *key, const tap_var_t *tv, const fk_face_in_t *a0, bool boxed,
                      const char *how)
{
    const tap_vlist_t *vl = tap_vlist_of(key);
    const tap_count_t *cnt = vl ? NULL : tap_count_of(key);
    uint16_t vs[TAP_VLIST_MAX + 8];
    const unsigned nv = tap_variants(key, vs);
    const omni_rect_t *hit = hit_of(key, tv);
    TEST_ASSERT_NOT_NULL(hit);
    int16_t tx = (int16_t)((hit->x0 + hit->x1) / 2), ty = (int16_t)((hit->y0 + hit->y1) / 2);
    if (tv->at) {   /* wave 2 Task 10: a target that moves with the clock, at the probed input */
        int px, py;
        tv->at(a0, &px, &py);
        tx = (int16_t)px, ty = (int16_t)py;
    }
    fk_face_in_t a = *a0, b = a, c;
    const fk_tap_t act = r->ops->tap_var(r->ctx, &a, tx, ty, &b.variant);
    if (tv->keeps) {   /* wave 2 Task 6: the pulse, the variant unmoved */
        TEST_ASSERT_EQUAL_INT_MESSAGE(FK_TAP_PULSE, act, "a keeps line's tap starts no pulse");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(a.variant, b.variant, "a keeps line's tap moves the variant");
    } else {
        /* the state hides the tapped text when every variant draws the same frame there (the Task 12 review's rule,
         * derived: then a tap leaves the variant, since it would turn a page nobody sees) */
        bool hidden = true;
        fk_face_in_t h = a;
        h.pulsing = tv->shows_ms != 0, h.pulse_ms = tv->shows_ms, h.variant = vs[0];   /* Task 8: shows_ms */
        frame(r, &h, s_c);
        for (unsigned q = 1; q < nv && hidden; q++) {
            h.variant = vs[q];
            frame(r, &h, s_b);
            hidden = differ_in(s_c, s_b, tv->r) == 0 && (!tv->r2 || differ_in(s_c, s_b, tv->r2) == 0);
        }
        if (tv->shows_ms && !hidden) s_shown++;
        /* a hidden state's tap may do nothing at all (Task 14: Bunk Clock's bar is inert while unset) */
        TEST_ASSERT_TRUE_MESSAGE(act != FK_TAP_NONE || hidden, "a tap on its rect does nothing");
        if (hidden)
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(a.variant, b.variant, "a tap moves a hidden variant");
        else
            TEST_ASSERT_TRUE_MESSAGE(b.variant != a.variant && (vl || b.variant < cnt->n),
                                     "a tap on its rect moves the variant");
    }
    c = b;
    if (act == FK_TAP_PULSE && r->ops->pulse_len_ms) c.pulsing = true, c.pulse_ms = 0;
    const bool out = boxed && (pulse_outside(r, tv, &a) || pulse_outside(r, tv, &b));
    char what[160];
    if (out && !tv->shakes) {
        snprintf(what, sizeof what, "%s (%s): a pulse in flight draws outside the line's boxes, and the line does not"
                 " declare shakes", key, how);
        TEST_FAIL_MESSAGE(what);
    }
    const bool box = boxed && !out;
    omni_rect_t rects[8];
    const fk_face_in_t *pair[3][2] = {{&a, &b}, {&b, &c}, {&a, &c}};
    static const char *const NAME[3] = {"the new variant", "its pulse's start", "both draws"};
    for (int k = 0; k < 3; k++) {
        snprintf(what, sizeof what, "%s tap at (%d, %d) from variant %u (%s%s): %s", key, tx, ty, (unsigned)a.variant,
                 how, box ? "" : "; dirty only", NAME[k]);
        const unsigned n = dirty_pair(r, pair[k][0], pair[k][1], what, rects);
        if (box)
            for (unsigned q = 0; q < n; q++)
                TEST_ASSERT_TRUE_MESSAGE(rect_within(&rects[q], tv->r) || rect_within(&rects[q], tv->r2), what);
    }
    return out;
}
static void tap_pair_line(const row_t *r, const char *key, const tap_var_t *tv)
{
    unsigned exempt = 0;
    /* wave 2 Task 9 (M1): a shows_ms instant lies inside the pulse, and some probe finds the variant shown there */
    if (tv->shows_ms)
        TEST_ASSERT_TRUE_MESSAGE(r->ops->pulse_len_ms && tv->shows_ms < r->ops->pulse_len_ms,
                                 "a line's shows_ms is not shorter than its face's pulse");
    s_shown = 0;
    const tap_vlist_t *vl = tap_vlist_of(key);
    uint16_t vs[TAP_VLIST_MAX + 8];
    const unsigned nv = tap_variants(key, vs);
    const omni_rect_t *hit = hit_of(key, tv);
    /* the states: 24 h, 12 h, stale, 22:09 in 12 h, unset (the Task 12 review); from each of the face's variants (a
     * page face: 0 .. 4); at rest, with a pulse in flight at half its length (a face with a pulse: the tap's first draw
     * keeps the old pulse's time, its second restarts it), and 100 ms after a tap on each of the face's rects */
    for (int st = 0; st < 5; st++)
        for (unsigned iv = 0; iv < (vl ? nv : 5u); iv++) {
            fk_face_in_t a = in_at(st == 3 ? 22 : 10, 9, 30, 500);
            a.h24 = st != 1 && st != 3, a.clk.stale = st == 2, a.clk.set = st != 4, a.mono_ms = 5000;
            a.clk.mday = 28, a.clk.mon = 10, a.clk.wday = 3;   /* WED 28 NOV */
            a.variant = vl ? vs[iv] : (uint16_t)iv;
            char how[96];
            snprintf(how, sizeof how, "state %d", st);
            exempt += tap_probe(r, key, tv, &a, true, how);
            if (!r->ops->pulse_len_ms) continue;
            fk_face_in_t f = a;
            f.pulsing = true, f.pulse_ms = (uint16_t)(r->ops->pulse_len_ms / 2);
            snprintf(how, sizeof how, "state %d, a pulse in flight at %u ms", st, (unsigned)f.pulse_ms);
            exempt += tap_probe(r, key, tv, &f, true, how);
            /* 100 ms after a tap on each rect of the face: its own pulse in flight; a cross tap is checked for dirty
             * completeness only (the ruling) */
            for (unsigned o = 0; o < sizeof RD_TAP_VARS / sizeof RD_TAP_VARS[0]; o++) {
                if (strcmp(RD_TAP_VARS[o].key, key) != 0) continue;
                const omni_rect_t *oh = hit_of(key, &RD_TAP_VARS[o]);
                if (o > 0 && strcmp(RD_TAP_VARS[o - 1].key, key) == 0 && hit_of(key, &RD_TAP_VARS[o - 1]) == oh)
                    continue;   /* a rect once */
                fk_face_in_t g = a;
                int ox = (oh->x0 + oh->x1) / 2, oy = (oh->y0 + oh->y1) / 2;
                if (RD_TAP_VARS[o].at) RD_TAP_VARS[o].at(&a, &ox, &oy);   /* wave 2 Task 10: a moving target */
                const fk_tap_t act = r->ops->tap_var(r->ctx, &a, (int16_t)ox, (int16_t)oy, &g.variant);
                if (act != FK_TAP_PULSE) continue;   /* no pulse in flight from it */
                g.pulsing = true, g.pulse_ms = 100;
                const bool cross = oh != hit;
                snprintf(how, sizeof how, "state %d, 100 ms after a tap on (%d, %d)%s", st, ox, oy,
                         cross ? ", a cross tap" : "");
                exempt += tap_probe(r, key, tv, &g, !cross, how);
            }
        }
    /* a declared shake is used: some probe's pulse in flight did draw outside the line's boxes */
    if (tv->shakes) TEST_ASSERT_TRUE_MESSAGE(exempt > 0, "a line declares shakes, and no probe's pulse ever moves the frame");
    if (tv->shows_ms) TEST_ASSERT_TRUE_MESSAGE(s_shown > 0, "a shows_ms line hides its variant in every probed state");
}

static void tap_pair(const char *key)
{
    const row_t *r = row_of(key);
    if (!r->ops->tap_var) return;
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof RD_TAP_VARS / sizeof RD_TAP_VARS[0]; i++)
        if (strcmp(RD_TAP_VARS[i].key, key) == 0) tap_pair_line(r, key, &RD_TAP_VARS[i]), n++;
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "a face with tap_var declares no RD_TAP_VARS line");
}

/* in the glance nothing moves within a minute: dirty() reports nothing (no flush of unchanged black), whatever the
 * chime and the readout do */
static void glance_quiet(const char *key)
{
    const row_t *r = row_of(key);
    for (int st = 0; st < 3; st++)
        for (int ms = 0; ms < 1000; ms += 40) {
            fk_face_in_t t = in_at(10, 9, 21, ms);
            t.glance = true;
            t.clk.stale = st == 1;
            t.clk.set = st != 2;
            fk_face_in_t t1 = shifted(t, 1000), t40 = shifted(t, 40);
            t1.thrum = 0.5f, t40.beep = 1.f, t40.slept_s = 99;
            omni_rect_t rects[8];
            TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &t, &t1, rects));
            TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &t, &t40, rects));
        }
}

/* Lesson 2: under reduced motion nothing ambient moves within a second: at each still instant (moved back so that
 * +300 ms stays in its second) the frames at +40 ms and +300 ms are the instant's, the clock and the free-running
 * channels (mono_ms, and fresh_ms when a revealing wake is fresh) moved on together (the Task 9 review's I2); from no
 * fresh wake and from a fresh one at several fresh_ms */
static void reduced_still(const char *key)
{
    const row_t *r = row_of(key);
    static const struct { bool fresh; uint16_t ms; } FR[] = {{false, 0}, {true, 0}, {true, 700}, {true, 5000},
                                                             {true, 30000}, {true, 59000}};
    for (unsigned i = 0; i < RD_STILL_N; i++)
        for (unsigned f = 0; f < sizeof FR / sizeof FR[0]; f++) {
            rd_still_t st;
            rd_still(i, &st);
            fk_face_in_t a = st.in;
            a.reduced = true;
            a.mono_ms = 123457u + 1000u * f;
            a.fresh = FR[f].fresh, a.fresh_ms = FR[f].ms;
            if (a.clk.ms > 699) a.clk.ms = 699;
            frame(r, &a, s_c);
            static const int plus[2] = {40, 300};
            for (int k = 0; k < 2; k++) {
                fk_face_in_t b = ran(a, plus[k]);
                b.clk.ms = (uint16_t)(a.clk.ms + plus[k]);
                frame(r, &b, s_a);
                char what[160];
                snprintf(what, sizeof what, "%s reduced, %s, +%d ms (fresh %d at %u ms)", key, st.what, plus[k],
                         (int)FR[f].fresh, FR[f].ms);
                assert_same(s_c, s_a, what);
            }
        }
}

/* RD_GLANCE_SWEEP_STEP: the day sweep's step in minutes (1 when unset; the sanitizer configuration sets 5) */
static int sweep_step(void)
{
    const char *e = getenv("RD_GLANCE_SWEEP_STEP");
    const int n = e ? atoi(e) : 1;
    return n >= 1 && n <= 60 ? n : 1;
}

static float glance_pct(const row_t *r, const fk_face_in_t *in)
{
    frame(r, in, s_a);
    float pct;
    uint8_t maxc;
    fk_test_lit_th(s_a, 8, &pct, &maxc);
    return pct;
}

/* the glance at in, at each of the 9 orbit positions: lit < 5.00 % (tools/shot.py --lit: any channel > 8) and no
 * channel above 110 */
static void glance_orbits(const row_t *r, fk_face_in_t in, const char *what)
{
    for (int o = 0; o < 9; o++) {
        in.odx = ORB9[o][0], in.ody = ORB9[o][1];
        frame(r, &in, s_a);
        float pct;
        uint8_t maxc;
        fk_test_lit_th(s_a, 8, &pct, &maxc);
        if (pct >= 5.f || maxc > 110) {
            char m[200];
            snprintf(m, sizeof m, "%s's glance %s (%s), orbit (%d, %d): %.2f %% lit, max channel %u (< 5 %%, <= 110)",
                     r->ops->key, what, in.h24 ? "24 h" : "12 h", ORB9[o][0], ORB9[o][1], (double)pct, maxc);
            TEST_FAIL_MESSAGE(m);
        }
    }
}

/* Lesson 4: every minute of the day (at :30.5, orbit (0, 0)) in 24 h and in 12 h finds the glance's worst lit %; at
 * that minute and at 23:59 every orbit position stays under the bars, stale too, and so does the unset glance */
static void glance_day(const char *key)
{
    const row_t *r = row_of(key);
    const int step = sweep_step();
    for (int hm = 0; hm < 2; hm++) {
        float worst = -1.f;
        int worst_t = 0, n = 0;
        for (int t = 0; t < 1440; t++) {
            if (t % step && t % 60 != 59) continue;
            fk_face_in_t in = in_at(t / 60, t % 60, 30, 500);
            in.glance = true, in.h24 = hm == 0;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date, as text_radius */
            const float pct = glance_pct(r, &in);
            if (pct > worst) worst = pct, worst_t = t;
            n++;
        }
        char what[40];
        fk_face_in_t in = in_at(worst_t / 60, worst_t % 60, 30, 500);
        in.glance = true, in.h24 = hm == 0;
        in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
        snprintf(what, sizeof what, "at %02d:%02d (the worst)", worst_t / 60, worst_t % 60);
        glance_orbits(r, in, what);
        in.clk.stale = true;
        glance_orbits(r, in, "stale, at the worst minute");
        in = in_at(23, 59, 30, 500);
        in.glance = true, in.h24 = hm == 0;
        in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
        glance_orbits(r, in, "at 23:59");
        in.clk.set = false;
        glance_orbits(r, in, "unset");
        printf("%s %s: the glance's worst minute %02d:%02d lights %.2f %% (%d minutes, step %d)\n", key,
               hm ? "12 h" : "24 h", worst_t / 60, worst_t % 60, (double)worst, n, step);
    }
}

/* the share of the disc (fk_test_lit_th's disc: pixel centres within 233 px of the centre) with every channel <= 40 */
static float void_pct(const uint16_t *fb)
{
    long v = 0, disc = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
            if (dx * dx + dy * dy > 233.f * 233.f) continue;
            disc++;
            const uint16_t p = fb[y * W + x];
            const int R = ((p >> 11) & 31) * 255 / 31, G = ((p >> 5) & 63) * 255 / 63, B = (p & 31) * 255 / 31;
            v += R <= 40 && G <= 40 && B <= 40;
        }
    return (float)(100.0 * (double)v / (double)disc);
}

static float floor_of(const char *key)
{
    for (unsigned i = 0; i < sizeof RD_VOID_FLOOR / sizeof RD_VOID_FLOOR[0]; i++)
        if (strcmp(RD_VOID_FLOOR[i].key, key) == 0) return RD_VOID_FLOOR[i].floor;
    char m[96];
    snprintf(m, sizeof m, "%s has no RD_VOID_FLOOR line (its face task adds it, PR10)", key);
    TEST_FAIL_MESSAGE(m);
    return 100.f;
}

/* PR10: stills 0, 1 and 3 keep the face's own void floor */
static void void_floor(const char *key)
{
    const row_t *r = row_of(key);
    const float fl = floor_of(key);
    static const unsigned STILLS[3] = {0, 1, 3};
    for (int k = 0; k < 3; k++) {
        rd_still_frame(r->ops, r->ctx, STILLS[k], s_a);
        const float v = void_pct(s_a);
        rd_still_t st;
        rd_still(STILLS[k], &st);
        printf("%s still %u (%s): %.2f %% void (floor %.0f %%)%s\n", key, STILLS[k], st.what, (double)v, (double)fl,
               v < 85.f ? "; under the spec's 85 % (report)" : "");
        char m[160];
        snprintf(m, sizeof m, "%s still %u: %.2f %% void, under its floor %.0f %%", key, STILLS[k], (double)v,
                 (double)fl);
        TEST_ASSERT_TRUE_MESSAGE(v >= fl, m);
    }
}

/* ---- the glyph recording (FK_TEST_HOOKS) */
#define MAX_GLYPHS 512
typedef struct { const fk_atlas_t *at; const void *plane; int x0, y0, x1, y1; } glyph_t;
static glyph_t s_gl[MAX_GLYPHS];
static unsigned s_ngl;
static void record(const fk_atlas_t *at, int x0, int y0, int x1, int y1)
{
    if (s_ngl < MAX_GLYPHS)
        s_gl[s_ngl] = (glyph_t){at, at->img->a ? (const void *)at->img->a : (const void *)at->img->rgb, x0, y0, x1, y1};
    s_ngl++;
}

/* the farthest pixel centre of every recorded glyph box from the centre, squared */
static float glyph_reach2(unsigned *worst)
{
    float far2 = 0;
    for (unsigned i = 0; i < s_ngl && i < MAX_GLYPHS; i++) {
        const glyph_t *g = &s_gl[i];
        const float xs[2] = {(float)g->x0 + 0.5f, (float)g->x1 - 0.5f}, ys[2] = {(float)g->y0 + 0.5f, (float)g->y1 - 0.5f};
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) {
                const float dx = xs[a] - 233.f, dy = ys[b] - 233.f, d2 = dx * dx + dy * dy;
                if (d2 > far2) far2 = d2, *worst = i;
            }
    }
    return far2;
}

/* Lesson 10's 200 px, unless a face's line here says otherwise (spec section 10.0: "about 200 px"; each line with the
 * reason and the measured reach, pinned to it so nothing more creeps out) */
typedef struct { const char *key; float reach; } reach_t;
static const reach_t RD_TEXT_REACH[] = {
    /* Bunk Clock (Task 14; for the controller's ruling): the mockup sets the roommate's label from x 184 on y 81 and
     * the count line (VT323 28, 302 px wide) on y 384, and both reach past 200 px: PAUSED (BLISS)'s last glyph box
     * 232.7 px (its ink at the panel's edge), PROJECTED's 214, the count line's corners 229. Kept as drawn (no fidelity
     * cut without a ruling, R146); pinned at the measured reach so nothing else creeps out */
    {"bunk", 233.0f},
    /* Curry Clock (Task 15): its twelve dial numerals sit on the mockup's ring at r 201 (drawStencil at 201 px, the
     * ticks at 222-228 outside them), so their stencil 30 glyph boxes reach 219.7 px (1 and 11 o'clock's two digits);
     * every text line (the stale marker, the lid's biro) lies within 200. Pinned at the measured reach */
    {"curry", 220.0f},
    /* Bay Mouth (wave 2 Task 3): the mockup sets its stencil 132 time at y 252 (Stasis Clock's at 278), so the widest
     * hours' glyph boxes (01:00's 0, their cells' corners) reach 206.6 px; every other line lies within 200. Pinned at
     * the measured reach */
    {"bay", 207.0f},
    /* Drift Plate (wave 2 Task 5; for the controller's ruling): the frame numbers (FR 2231 + i, Chakra 13 at y BY + 40)
     * ride the band across the panel's rim, as the mockup draws them (every plate's x + 10, the whole band's width);
     * a glyph is drawn only while it can reach the panel (its cell within 245 px of the centre), so the farthest
     * glyph box corner lies 253.9 px out (its ink at the rim). The time (stencil 132 at y BY + 135) reaches 198.3 px
     * (23:59's cells) and every label lies within 200. Kept as drawn (no fidelity cut without a ruling, R146); pinned
     * at the measured reach */
    {"drift", 254.0f},
    /* Nav Scope (wave 2 Task 7; for the controller's ruling): the glance's 12, 3, 6 and 9 (stencil 30) sit on the
     * mockup's r 196, as its sleepFrame() draws them, so their glyph boxes reach 209.1 px (3 and 9's cells). The awake
     * numerals are the dial layer's (baked, not atlas glyphs: this harness does not see them), their lit ink to
     * 215.8 px (10 o'clock's), where the mockup puts them; every line of text (the readout, the tag, --:--, the marker)
     * lies inside the glass. Kept as drawn (no fidelity cut without a ruling, R146); pinned at the measured reach */
    {"scope", 210.0f},
    /* the Bowl (wave 2 Task 9; for the controller's ruling): the glance's 12, 3, 6 and 9 (stencil 30) sit on the
     * mockup's r 200, as its sleepFrame() draws them, so their glyph boxes reach 213.1 px (12's cells). The awake
     * numerals are the bowl layer's (baked, not atlas glyphs: this harness does not see them), on the same ring in the
     * lip; every line of text (--:--, BOWL EMPTY, the marker) lies on the floor. Kept as drawn (no fidelity cut without
     * a ruling, R146); pinned at the measured reach */
    {"bowl", 214.0f},
    /* Mop Bot (wave 2 Task 10; R187): 30 at the track's bottom (Chakra 13, its middle at the mockup's cy + RR + 1, 439)
     * reaches 208.7 px (its glyph boxes' lower corners); the time (stencil 132 at baseline 277), the date, the marker
     * and Z Z lie within 200. Accepted as drawn (R187, owner glass); pinned at the measured reach */
    {"mopbot", MB_REACH},
};
static float reach_of(const char *key)
{
    for (unsigned i = 0; i < sizeof RD_TEXT_REACH / sizeof RD_TEXT_REACH[0]; i++)
        if (strcmp(RD_TEXT_REACH[i].key, key) == 0) return RD_TEXT_REACH[i].reach;
    return 200.f;
}
static float s_reach_seen;   /* the farthest glyph box corner text_radius met, for its report line */

static void radius_at(const row_t *r, const fk_face_in_t *in, const char *what)
{
    s_ngl = 0;
    fk_test_glyph_hook = record;
    frame(r, in, s_a);
    fk_test_glyph_hook = NULL;
    TEST_ASSERT_TRUE(s_ngl <= MAX_GLYPHS);
    unsigned w = 0;
    const float far2 = glyph_reach2(&w);
    const float reach = reach_of(r->ops->key);
    if (far2 > s_reach_seen * s_reach_seen) s_reach_seen = sqrtf(far2);
    if (far2 > reach * reach) {
        char m[200];
        snprintf(m, sizeof m, "%s %s: a glyph box (%d, %d)-(%d, %d) reaches %.1f px, past %.1f px of the centre",
                 r->ops->key, what, s_gl[w].x0, s_gl[w].y0, s_gl[w].x1, s_gl[w].y1, (double)sqrtf(far2),
                 (double)reach);
        TEST_FAIL_MESSAGE(m);
    }
}

/* Lesson 10 (spec section 10.0, R154): every glyph box lies within 200 px of the centre, at 1:00, 9:59, 10:00, 12:59
 * and 23:59 (and their other halves of the day) in 12 and 24 h; stale, unset, the widest readouts and the glance */
static void text_radius_at(const char *key);
static void text_radius(const char *key)
{
    s_reach_seen = 0;
    text_radius_at(key);
    printf("%s: the farthest glyph box corner lies %.1f px from the centre (its bar %.1f)\n", key, (double)s_reach_seen,
           (double)reach_of(key));
}
static void text_radius_at(const char *key)
{
    const row_t *r = row_of(key);
    static const int HM[][2] = {{1, 0}, {9, 59}, {10, 0}, {12, 59}, {23, 59}, {13, 0}, {21, 59}, {22, 0}, {0, 0},
                                {0, 59}, {20, 8}, {4, 44}};
    static const int32_t SLEPT[] = {-1, 999 * 3600 + 59 * 60 + 59, 1000 * 3600, 245, 12307};
    for (unsigned k = 0; k < sizeof HM / sizeof HM[0]; k++)
        for (int hm = 0; hm < 2; hm++)
            for (int g = 0; g < 2; g++)
                for (unsigned s = 0; s < sizeof SLEPT / sizeof SLEPT[0]; s++) {
                    fk_face_in_t in = in_at(HM[k][0], HM[k][1], 30, 500);
                    in.h24 = hm == 0, in.glance = g != 0, in.slept_s = SLEPT[s];
                    in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: wide letters */
                    char what[64];
                    snprintf(what, sizeof what, "%s at %02d:%02d %s, slept %ld", g ? "glance" : "awake", HM[k][0],
                             HM[k][1], hm ? "12 h" : "24 h", (long)SLEPT[s]);
                    radius_at(r, &in, what);
                    in.clk.stale = true;
                    radius_at(r, &in, "stale");
                }
    fk_face_in_t in = in_at(0, 0, 0, 0);
    in.clk.set = false;
    radius_at(r, &in, "unset");
    in.glance = true;
    radius_at(r, &in, "unset glance");
    /* Task 14: a face's tapped variants too, at rest and early in their pulse (a label or a message the tap shows) */
    if (r->ops->tap_var) {
        uint16_t vs[TAP_VLIST_MAX + 8];
        const unsigned nv = tap_variants(key, vs);
        for (unsigned q = 0; q < nv; q++)
            for (int st = 0; st < 3; st++) {
                in = in_at(20, 8, 30, 500);
                in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
                in.h24 = false, in.variant = vs[q], in.clk.stale = st == 1, in.clk.set = st != 2;
                if (r->ops->pulse_len_ms) in.pulsing = true, in.pulse_ms = 60;
                radius_at(r, &in, "a tapped variant in its pulse");
                in.pulsing = false, in.pulse_ms = 0;
                radius_at(r, &in, "a tapped variant");
            }
    }
}

/* wave 2 Task 6 (R165/I3): (x, y) lies in one of the face's RD_TAP_CIRCLES circles at in (NULL: their fixed centres),
 * grown by grow px */
static bool in_circle(const char *key, const fk_face_in_t *in, int x, int y, int grow)
{
    for (unsigned i = 0; i < sizeof RD_TAP_CIRCLES / sizeof RD_TAP_CIRCLES[0]; i++) {
        const tap_circle_t *c = &RD_TAP_CIRCLES[i];
        if (strcmp(c->key, key) != 0) continue;
        int cx = c->cx, cy = c->cy;
        if (c->at && in) c->at(in, &cx, &cy);
        const int rr = c->r + grow;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= rr * rr) return true;
    }
    return false;
}
/* one tap at (x, y): tap_var's action (a copy of the variant) at in, or tap's when in is NULL */
static fk_tap_t tap_one(const row_t *r, const fk_face_in_t *in, int x, int y)
{
    if (!in) return r->ops->tap(r->ctx, (int16_t)x, (int16_t)y);
    uint16_t v = in->variant;
    return r->ops->tap_var(r->ctx, in, (int16_t)x, (int16_t)y, &v);
}
static void tap_expect(const row_t *r, const fk_face_in_t *in, int x, int y, fk_tap_t act, bool hit, const char *what)
{
    const fk_tap_t got = tap_one(r, in, x, y);
    if ((got == act) != hit) {
        char m[200];
        snprintf(m, sizeof m, "%s tap%s at (%d, %d), %s: got %d, expected %s%d", r->ops->key, in ? "_var" : "", x, y,
                 what, (int)got, hit ? "" : "anything but ", (int)act);
        TEST_FAIL_MESSAGE(m);
    }
}
/* a circle line: at every awake still (and through tap while its centre is fixed) */
static void circle_probe(const row_t *r, const tap_circle_t *c)
{
    TEST_ASSERT_TRUE_MESSAGE(2 * c->r >= 44, "a circular tap target under 44 px across");
    int d = (int)((float)(c->r - 1) * 0.70710678f + 0.5f);   /* the 45 degree points at r - 1 */
    if (2 * d * d > (c->r - 1) * (c->r - 1)) d--;   /* wave 2 Task 10: r 36 rounds (24.7) past the circle; one in */
    TEST_ASSERT_TRUE(2 * d * d <= (c->r - 1) * (c->r - 1));
    for (unsigned i = 0; i <= RD_STILL_N; i++) {
        rd_still_t st;
        const fk_face_in_t *in = NULL;
        if (i < RD_STILL_N) {
            rd_still(i, &st);
            if (st.glance || !r->ops->tap_var) continue;
            in = &st.in;
        } else if (!r->ops->tap || c->at) {
            continue;
        }
        int cx = c->cx, cy = c->cy;
        if (c->at && in) c->at(in, &cx, &cy);
        static const int CP[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (int k = 0; k < 4; k++) {
            tap_expect(r, in, cx + CP[k][0] * c->r, cy + CP[k][1] * c->r, c->act, true, "on the circle (r)");
            tap_expect(r, in, cx + CP[k][0] * (c->r + 1), cy + CP[k][1] * (c->r + 1), c->act, false, "at r + 1");
            tap_expect(r, in, cx + (k < 2 ? d : -d), cy + (k % 2 ? d : -d), c->act, true, "at r - 1, 45 degrees");
        }
        tap_expect(r, in, cx, cy, c->act, true, "the centre");
    }
}
/* an except rect: its corners, edge middles, centre and a 13 px grid inside it, each outside the face's circles (and the
 * circles' compass points at r + 1), tap the rect's act; one px outside each edge does not */
static void except_probe(const row_t *r, const char *key, omni_rect_t t, fk_tap_t act)
{
    TEST_ASSERT_NOT_NULL(r->ops->tap);
    const int xs[3] = {t.x0, (t.x0 + t.x1 - 1) / 2, t.x1 - 1}, ys[3] = {t.y0, (t.y0 + t.y1 - 1) / 2, t.y1 - 1};
    unsigned n = 0;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            if (!in_circle(key, NULL, xs[i], ys[j], 0)) tap_expect(r, NULL, xs[i], ys[j], act, true, "a corner, an edge or the middle"), n++;
    for (int y = t.y0; y < t.y1; y += 13)
        for (int x = t.x0; x < t.x1; x += 13)
            if (!in_circle(key, NULL, x, y, 0)) tap_expect(r, NULL, x, y, act, true, "inside, outside the circles"), n++;
    for (unsigned i = 0; i < sizeof RD_TAP_CIRCLES / sizeof RD_TAP_CIRCLES[0]; i++) {
        const tap_circle_t *c = &RD_TAP_CIRCLES[i];
        if (strcmp(c->key, key) != 0 || c->at) continue;
        static const int CP[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (int k = 0; k < 4; k++) {
            const int x = c->cx + CP[k][0] * (c->r + 1), y = c->cy + CP[k][1] * (c->r + 1);
            if (inside(&t, x, y)) tap_expect(r, NULL, x, y, act, true, "just outside a circle (r + 1)"), n++;
        }
    }
    tap_expect(r, NULL, t.x0 - 1, ys[1], act, false, "one px left of the rect");
    tap_expect(r, NULL, t.x1, ys[1], act, false, "one px right of the rect");
    tap_expect(r, NULL, xs[1], t.y0 - 1, act, false, "one px above the rect");
    tap_expect(r, NULL, xs[1], t.y1, act, false, "one px below the rect");
    TEST_ASSERT_TRUE(n > 100);
}

/* Lesson 5: each declared hit rect through fk_test_tap_edges, >= 44 px each side (an except rect and the circles
 * through their own probes, R165/I3) */
static void tap_edges(const char *key)
{
    const row_t *r = row_of(key);
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof RD_TAPS / sizeof RD_TAPS[0]; i++) {
        if (strcmp(RD_TAPS[i].key, key) != 0) continue;
        const omni_rect_t t = RD_TAPS[i].r;
        TEST_ASSERT_TRUE_MESSAGE(t.x1 - t.x0 >= 44 && t.y1 - t.y0 >= 44, "a tap target under 44 px");
        if (RD_TAPS[i].except) except_probe(r, key, t, RD_TAPS[i].act);
        else fk_test_tap_edges(r->ops, r->ctx, t, RD_TAPS[i].act);
        n++;
    }
    for (unsigned i = 0; i < sizeof RD_TAP_CIRCLES / sizeof RD_TAP_CIRCLES[0]; i++)
        if (strcmp(RD_TAP_CIRCLES[i].key, key) == 0) circle_probe(r, &RD_TAP_CIRCLES[i]), n++;
    if (r->ops->tap || r->ops->tap_var) TEST_ASSERT_TRUE_MESSAGE(n > 0, "a face that taps declares no RD_TAPS line");
    /* wave 2 Task 10 (appended; I3, W2-D12): a face whose circle moves with the clock (at) taps through tap_var, so the
     * grid of off-target points runs through tap_var too, at every awake still, outside that still's circles and the
     * face's declared rects */
    bool moving = false;
    for (unsigned i = 0; i < sizeof RD_TAP_CIRCLES / sizeof RD_TAP_CIRCLES[0]; i++)
        moving = moving || (strcmp(RD_TAP_CIRCLES[i].key, key) == 0 && RD_TAP_CIRCLES[i].at);
    if (moving) {
        TEST_ASSERT_NOT_NULL(r->ops->tap_var);
        unsigned vprobed = 0;
        for (unsigned s = 0; s < RD_STILL_N; s++) {
            rd_still_t st;
            rd_still(s, &st);
            if (st.glance) continue;
            for (int y = -60; y < W + 60; y += 13)
                for (int x = -60; x < W + 60; x += 13) {
                    bool in = in_circle(key, &st.in, x, y, 0);
                    for (unsigned i = 0; i < sizeof RD_TAPS / sizeof RD_TAPS[0] && !in; i++)
                        in = strcmp(RD_TAPS[i].key, key) == 0 && inside(&RD_TAPS[i].r, x, y);
                    if (in) continue;
                    char m[128];
                    snprintf(m, sizeof m, "%s tap_var taps at (%d, %d) at still %u, outside its circles and rects", key,
                             x, y, s);
                    uint16_t v = st.in.variant;
                    TEST_ASSERT_EQUAL_MESSAGE(FK_TAP_NONE, r->ops->tap_var(r->ctx, &st.in, (int16_t)x, (int16_t)y, &v), m);
                    vprobed++;
                }
        }
        TEST_ASSERT_TRUE(vprobed > 100);
    }
    /* a grid of points outside every declared rect and circle (on and off the screen) taps nothing */
    if (!r->ops->tap) return;
    unsigned probed = 0;
    for (int y = -60; y < W + 60; y += 13)
        for (int x = -60; x < W + 60; x += 13) {
            bool in = in_circle(key, NULL, x, y, 0);
            for (unsigned i = 0; i < sizeof RD_TAPS / sizeof RD_TAPS[0] && !in; i++)
                in = strcmp(RD_TAPS[i].key, key) == 0 && inside(&RD_TAPS[i].r, x, y);
            if (in) continue;
            char m[96];
            snprintf(m, sizeof m, "%s taps at (%d, %d), outside its declared rects", key, x, y);
            TEST_ASSERT_EQUAL_MESSAGE(FK_TAP_NONE, r->ops->tap(r->ctx, (int16_t)x, (int16_t)y), m);
            probed++;
        }
    TEST_ASSERT_TRUE(probed > 100);
}

/* the chime picture (spec section 10.0): with thrum 0.7 and beep 1 the frame differs from the silent frame only inside
 * the face's declared chime rects, and somewhere; under reduced motion it differs nowhere */
static void chime_regions(const char *key)
{
    const row_t *r = row_of(key);
    omni_rect_t ok[RD_ST_CHIME_MAX];
    unsigned n = 0;
    for (unsigned i = 0; i < sizeof RD_CHIMES / sizeof RD_CHIMES[0]; i++)
        if (strcmp(RD_CHIMES[i].key, key) == 0) n = RD_CHIMES[i].rects(ok);
    TEST_ASSERT_TRUE_MESSAGE(n > 0 && n <= RD_ST_CHIME_MAX, "the face declares no chime rects (RD_CHIMES)");
    static const int phase[] = {0, 333, 500, 940};
    for (unsigned k = 0; k < sizeof phase / sizeof phase[0]; k++)
        for (int st = 0; st < 2; st++) {
            fk_face_in_t a = in_at(10, 9, 21, phase[k]), b;
            a.clk.stale = st == 1;
            a.slept_s = 12307;
            b = a, b.thrum = 0.7f, b.beep = 1.f;
            frame(r, &a, s_a);
            frame(r, &b, s_b);
            long d = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++) {
                    if (s_a[y * W + x] == s_b[y * W + x]) continue;
                    if (!in_any(ok, n, x, y)) {
                        char m[128];
                        snprintf(m, sizeof m, "%s: the chime changes pixel (%d, %d), outside its rects", key, x, y);
                        TEST_FAIL_MESSAGE(m);
                    }
                    d++;
                }
            TEST_ASSERT_TRUE_MESSAGE(d > 1000, "the chime changes nothing");
            a.reduced = b.reduced = true;
            frame(r, &a, s_a);
            frame(r, &b, s_b);
            TEST_ASSERT_EQUAL_INT32_MESSAGE(0, frames_differ(s_a, s_b), "the chime under reduced motion");
        }
}

/* the atlases a face draws from: the committed one- and two-plane atlases, by file (the type set counts a family and a
 * size: the 132 stencil's outline, rd_f_s132o, is the same atlas as rd_f_s132, as the designers count 8) */
static const char *const ATLAS_FILES[] = {ASSET_RD_F_S132, ASSET_RD_F_S132O, ASSET_RD_F_S64, ASSET_RD_F_S30,
                                          ASSET_RD_F_VT28, ASSET_RD_F_VT20,  ASSET_RD_F_VT132, ASSET_RD_F_CH18,
                                          ASSET_RD_F_CH13};
#define N_ATLAS_FILES (sizeof ATLAS_FILES / sizeof ATLAS_FILES[0])
static int atlas_of(const void *plane)
{
    for (unsigned i = 0; i < N_ATLAS_FILES; i++) {
        size_t n = 0;
        const uint8_t *f = rd_assets_host_get(ATLAS_FILES[i], &n);
        if (f && (const uint8_t *)plane >= f && (const uint8_t *)plane < f + n)
            return i == 1 ? 0 : (int)i;   /* the outline folds into its stencil */
    }
    return -1;
}

static void atlases(const char *key)
{
    const row_t *r = row_of(key);
    bool used[N_ATLAS_FILES] = {false};
    fk_test_glyph_hook = record;
    for (unsigned i = 0; i < rd_still_count(r->ops) + 3; i++) {
        s_ngl = 0;
        if (i < rd_still_count(r->ops)) {
            rd_still_frame(r->ops, r->ctx, i, s_a);
        } else {   /* the chime, the stale glance, a 12 h stale frame */
            fk_face_in_t in = in_at(13, 5, 7, 250);
            in.thrum = i == rd_still_count(r->ops) ? 1.f : 0.f;
            in.clk.stale = i > rd_still_count(r->ops);
            in.glance = i == rd_still_count(r->ops) + 1;
            in.h24 = false;
            frame(r, &in, s_a);
        }
        TEST_ASSERT_TRUE(s_ngl <= MAX_GLYPHS);
        for (unsigned g = 0; g < s_ngl; g++) {
            const int a = atlas_of(s_gl[g].plane);
            TEST_ASSERT_TRUE_MESSAGE(a >= 0, "a glyph from an atlas that is not a committed rd_f_* file");
            used[a] = true;
        }
    }
    fk_test_glyph_hook = NULL;
    unsigned n = 0;
    printf("%s draws from:", key);
    for (unsigned i = 0; i < N_ATLAS_FILES; i++)
        if (used[i]) printf(" %s", ATLAS_FILES[i]), n++;
    printf(" (%u atlases)\n", n);
    TEST_ASSERT_TRUE_MESSAGE(n >= 1 && n <= 4, "a face draws from more than four atlases (spec section 10.0)");
}

/* wave 2 Task 3 (lesson 15, I4): the static-bright share. From still 0's instant, rest frames every step_s over
 * window_s (the clock and the free-running channels moved on together); a disc pixel (fk_test_lit_th's disc) counts when
 * it has a channel above 40 (widened as void_pct widens) in the first frame and is equal in every sample. The share
 * stays at or under the line's pin and its absolute ceiling */
static const static_bright_t *static_bright_of(const char *key)
{
    for (unsigned i = 0; i < sizeof RD_STATIC_BRIGHT / sizeof RD_STATIC_BRIGHT[0]; i++)
        if (strcmp(RD_STATIC_BRIGHT[i].key, key) == 0) return &RD_STATIC_BRIGHT[i];
    return NULL;
}
static uint8_t s_same[W * W];
static void static_bright(const char *key)
{
    const row_t *r = row_of(key);
    const static_bright_t *sb = static_bright_of(key);
    TEST_ASSERT_NOT_NULL_MESSAGE(sb, "the face has no RD_STATIC_BRIGHT line");
    TEST_ASSERT_TRUE(sb->window_s > 0 && sb->step_s > 0);
    rd_still_t st;
    rd_still(0, &st);
    frame(r, &st.in, s_c);
    memset(s_same, 1, sizeof s_same);
    unsigned n = 1;
    for (int t = sb->step_s; t <= sb->window_s; t += sb->step_s, n++) {
        const fk_face_in_t in = ran(shifted(st.in, t * 1000L), t * 1000L);
        frame(r, &in, s_a);
        for (int i = 0; i < W * W; i++) s_same[i] &= s_a[i] == s_c[i];
    }
    long disc = 0, stat = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
            if (dx * dx + dy * dy > 233.f * 233.f) continue;
            disc++;
            const uint16_t p = s_c[y * W + x];
            const int R = ((p >> 11) & 31) * 255 / 31, G = ((p >> 5) & 63) * 255 / 63, B = (p & 31) * 255 / 31;
            stat += s_same[y * W + x] && (R > 40 || G > 40 || B > 40) && !(sb->moving && sb->moving(x, y));
        }
    const float pct = (float)(100.0 * (double)stat / (double)disc);
    const float abs_ceil = sb->abs_ceil > 0 ? sb->abs_ceil : RD_STATIC_BRIGHT_ABS;   /* R178: a face's own ruling */
    printf("%s: %.2f %% of the disc is static and bright (%ld px over %u samples, %d s apart over %d s; pin %.2f %%,"
           " ceiling %.2f %%)\n", key, (double)pct, stat, n, sb->step_s, sb->window_s, (double)sb->ceil,
           (double)abs_ceil);
    char m[160];
    snprintf(m, sizeof m, "%s: %.2f %% static bright, over the absolute ceiling %.2f %% (R146: stop and report)", key,
             (double)pct, (double)abs_ceil);
    TEST_ASSERT_TRUE_MESSAGE(sb->ceil <= abs_ceil, "a pin above the absolute ceiling");
    TEST_ASSERT_TRUE_MESSAGE(pct <= abs_ceil, m);
    snprintf(m, sizeof m, "%s: %.2f %% static bright, over its pin %.2f %%", key, (double)pct, (double)sb->ceil);
    TEST_ASSERT_TRUE_MESSAGE(pct <= sb->ceil, m);
}

/* wave 2 Task 10 (R188, appended): a face whose static-bright share varies with the clock is also measured at its worst
 * window, from the instant start (h:m:s, still 0's date and channels) over window_s every step_s, by static_bright()'s
 * rule; its ceiling is the mockup's settled frame rendered at that same instant, measured by the same rule (the same
 * mask, the same moving regions left out) plus a point; the pin is the measurement plus a point */
typedef struct {
    const char *key; int h, m, s, window_s, step_s; float ceil; bool (*moving)(int x, int y); float abs_ceil;
} static_bright_worst_t;
static const static_bright_worst_t RD_STATIC_BRIGHT_WORST[] = {
    /* Mop Bot (R188): the clean arc is static from 12 to the robot's place at the window's start and grows after it,
     * so the worst window ends as the hour does: from 10:49:59 to 10:59:59 (a second later, the last sample is past the
     * hour and the arc starts over). Measured 27.25 % (46 486 px; 27.36 % from rd_shot's frames at mono 0); the mockup's settled frame at 10:49:59 (its face driven at
     * 3 000 ms by tools/rd_export.mjs, as its shots, the hour pinned there) measures 28.33 % by the same rule and mask,
     * so the ceiling is 29.33 % and the build is under the design at that instant. (The method checked at 10:42:05:
     * the rendered frame 26.13 %, the committed shot 26.22 %). The pin is the measurement plus a point */
    {"mopbot", 10, 49, 59, 600, 10, MB_SBW_PIN, mb_moving, 29.33f},
};
static void static_bright_worst(const char *key)
{
    const static_bright_worst_t *sb = NULL;
    for (unsigned i = 0; i < sizeof RD_STATIC_BRIGHT_WORST / sizeof RD_STATIC_BRIGHT_WORST[0]; i++)
        if (strcmp(RD_STATIC_BRIGHT_WORST[i].key, key) == 0) sb = &RD_STATIC_BRIGHT_WORST[i];
    TEST_ASSERT_NOT_NULL_MESSAGE(sb, "the face has no RD_STATIC_BRIGHT_WORST line");
    const row_t *r = row_of(key);
    rd_still_t st;
    rd_still(0, &st);
    st.in.clk.h = (uint8_t)sb->h, st.in.clk.m = (uint8_t)sb->m, st.in.clk.s = (uint8_t)sb->s, st.in.clk.ms = 0;
    frame(r, &st.in, s_c);
    memset(s_same, 1, sizeof s_same);
    unsigned n = 1;
    for (int t = sb->step_s; t <= sb->window_s; t += sb->step_s, n++) {
        const fk_face_in_t in = ran(shifted(st.in, t * 1000L), t * 1000L);
        frame(r, &in, s_a);
        for (int i = 0; i < W * W; i++) s_same[i] &= s_a[i] == s_c[i];
    }
    long disc = 0, stat = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
            if (dx * dx + dy * dy > 233.f * 233.f) continue;
            disc++;
            const uint16_t p = s_c[y * W + x];
            const int R = ((p >> 11) & 31) * 255 / 31, G = ((p >> 5) & 63) * 255 / 63, B = (p & 31) * 255 / 31;
            stat += s_same[y * W + x] && (R > 40 || G > 40 || B > 40) && !(sb->moving && sb->moving(x, y));
        }
    const float pct = (float)(100.0 * (double)stat / (double)disc);
    printf("%s from %02d:%02d:%02d (the worst window): %.2f %% static and bright (%ld px over %u samples; pin %.2f %%,"
           " ceiling %.2f %%)\n", key, sb->h, sb->m, sb->s, (double)pct, stat, n, (double)sb->ceil, (double)sb->abs_ceil);
    char m[160];
    TEST_ASSERT_TRUE_MESSAGE(sb->ceil <= sb->abs_ceil, "a worst window's pin above its ceiling");
    snprintf(m, sizeof m, "%s: %.2f %% static bright in its worst window, over the mockup's at that instant + 1 (%.2f %%)",
             key, (double)pct, (double)sb->abs_ceil);
    TEST_ASSERT_TRUE_MESSAGE(pct <= sb->abs_ceil, m);
    snprintf(m, sizeof m, "%s: %.2f %% static bright in its worst window, over its pin %.2f %%", key, (double)pct,
             (double)sb->ceil);
    TEST_ASSERT_TRUE_MESSAGE(pct <= sb->ceil, m);
}
static void test_rd_face_static_bright_worst_mopbot(void) { static_bright_worst("mopbot"); }

/* ---- the CRC table: every still of every row against rd/verify_crc.h (rd_shot --crc writes it) */
static void test_verify_crc_current(void)
{
    const fk_verify_tab_t tab = {RD_VERIFY, sizeof RD_VERIFY / sizeof RD_VERIFY[0]};
    unsigned rows = 0;
    for (unsigned i = 0; i < s_nrows; i++)
        for (unsigned k = 0; k < rd_still_count(s_rows[i].ops); k++) {
            rd_still_frame(s_rows[i].ops, s_rows[i].ctx, k, s_a);
            const uint32_t crc = fk_crc32(0, s_a, sizeof s_a);
            unsigned dups = 0;
            const fk_verify_t *v = fk_verify_lookup(&tab, 1, s_rows[i].ops->key, k, &dups);
            char m[128];
            snprintf(m, sizeof m, "%s still %u: %08x, the table says %08x (rd_shot --crc)", s_rows[i].ops->key, k,
                     (unsigned)crc, v ? (unsigned)v->crc : 0u);
            TEST_ASSERT_TRUE_MESSAGE(v != NULL && dups == 1, "a still without exactly one row");
            TEST_ASSERT_TRUE_MESSAGE(crc == v->crc, m);
            rows++;
        }
    TEST_ASSERT_EQUAL_UINT(rows, (unsigned)(sizeof RD_VERIFY / sizeof RD_VERIFY[0]));   /* no stray rows */
}

/* ---- the shared pieces */
static void test_rd_stills_table(void)
{
    TEST_ASSERT_EQUAL_INT(11, RD_STILL_N);
    for (unsigned i = 0; i < RD_STILL_N; i++) {
        rd_still_t st;
        rd_still(i, &st);
        TEST_ASSERT_NOT_NULL(st.what);
        TEST_ASSERT_EQUAL(st.glance, st.in.glance);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, st.in.wake_ms);
        TEST_ASSERT_EQUAL_INT32(i == 0 ? 12307 : -1, st.in.slept_s);
        TEST_ASSERT_EQUAL(i != 3 && i != 4 && i != 10, st.in.h24);
        TEST_ASSERT_TRUE(st.in.thrum == 0.f && st.in.beep == 0.f && st.in.click == 0.f && st.in.chime == 0.f);
    }
    rd_still_t st;
    rd_still(4, &st);
    char t[6];
    fk_time_str(&st.in.clk, st.in.h24, t);
    TEST_ASSERT_EQUAL_STRING("1:05", t);
    rd_still(0, &st);
    TEST_ASSERT_EQUAL_UINT16(305, st.in.clk.yday);
    TEST_ASSERT_TRUE(st.in.clk.set && !st.in.clk.stale && st.in.clk.h == 10 && st.in.clk.m == 9 && st.in.clk.s == 30 &&
                     st.in.clk.ms == 500 && st.in.clk.mday == 1 && st.in.clk.mon == 10 && st.in.clk.wday == 0);
    rd_still(1, &st);
    TEST_ASSERT_EQUAL_UINT16(196, st.in.clk.yday);
    rd_still(2, &st);
    TEST_ASSERT_EQUAL_UINT16(304, st.in.clk.yday);
    rd_still(6, &st);
    TEST_ASSERT_TRUE(st.glance && st.in.odx == 4 && st.in.ody == -4);
    rd_still(7, &st);
    TEST_ASSERT_FALSE(st.in.clk.set);
    rd_still(8, &st);
    TEST_ASSERT_TRUE(st.in.clk.set && st.in.clk.stale);
    rd_still(9, &st);
    TEST_ASSERT_TRUE(st.glance && !st.in.clk.set && st.in.odx == -4 && st.in.ody == 4);
    rd_still(10, &st);
    TEST_ASSERT_TRUE(st.glance && !st.in.h24 && st.in.clk.h == 13 && st.in.clk.m == 5);
    for (unsigned i = 0; i < s_nrows; i++)
        TEST_ASSERT_EQUAL_UINT(RD_STILL_N + s_rows[i].ops->stills_extra, rd_still_count(s_rows[i].ops));
}

static void test_rd_glow_lut_tables(void)
{
    static uint8_t lut[FK_GLOW_LUT_MAX];
    static const int radii[] = RD_GLOW_LUT_RADII;
    for (unsigned i = 0; i < sizeof radii / sizeof radii[0]; i++) {
        TEST_ASSERT_TRUE(radii[i] <= 255);
        const uint8_t *t = rd_glow_lut(radii[i]);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_TRUE(FK_GLOW_LUT_N(radii[i]) <= sizeof lut);
        fk_glow_lut_build(lut, radii[i]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(lut, t, FK_GLOW_LUT_N(radii[i]));
    }
    TEST_ASSERT_NULL(rd_glow_lut(13));
}

/* the stale marker draws only inside its own box, and in the glance only glance colours */
static void test_rd_stale_mark(void)
{
    size_t n = 0;
    const void *bin = rd_assets_host_get(ASSET_RD_F_CH13, &n);
    fk_img_t im;
    TEST_ASSERT_TRUE(bin && fk_img_from_bin(&im, bin, n));
    const fk_atlas_t ch13 = {&im, RD_CELLS_RD_F_CH13, RD_GLYPHS_RD_F_CH13, ASSET_RD_F_CH13_LINE_H};
    for (int g = 0; g < 2; g++) {
        memset(s_a, 0, sizeof s_a);
        rd_stale_mark(s_a, W, NULL, &ch13, 233, 117, g != 0);
        long lit = 0;
        uint8_t maxc = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                if (!s_a[y * W + x]) continue;
                lit++;
                TEST_ASSERT_TRUE_MESSAGE(x >= 196 && x < 272 && y >= 104 && y < 121, "the marker outside its box");
            }
        float pct;
        fk_test_lit_th(s_a, 8, &pct, &maxc);
        TEST_ASSERT_TRUE(lit > 60);
        if (g) TEST_ASSERT_TRUE_MESSAGE(maxc <= 110, "the glance marker above 110");
        else TEST_ASSERT_TRUE(maxc > 200);   /* amber */
    }
}

/* ---- Stasis Clock (spec section 10.1) */
/* through a minute at 40 ms steps, every dirty rect lies inside the slot boxes of the slots drawn in either frame,
 * and covers what changed */
static void test_rd_stasis_slot_confined(void)
{
    const row_t *r = row_of("stasis");
    for (int red = 0; red < 2; red++)
        for (int ms = 0; ms + 40 < 60000; ms += 40) {
            fk_face_in_t a = in_at(10, 9, ms / 1000, ms % 1000), b = shifted(a, 40);
            a.reduced = b.reduced = red != 0;
            omni_rect_t slots[12], rects[8];
            unsigned ns = 0;
            for (int k = 0; k < 6; k++) {
                rd_st_slot_rect((a.clk.s - k + 60) % 60, &slots[ns++]);
                rd_st_slot_rect((b.clk.s - k + 60) % 60, &slots[ns++]);
            }
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            TEST_ASSERT_TRUE(n <= 7);
            for (unsigned k = 0; k < n; k++) {
                const omni_rect_t *q = &rects[k];
                bool in = false;
                for (unsigned s = 0; s < ns && !in; s++)
                    in = q->x0 >= slots[s].x0 && q->y0 >= slots[s].y0 && q->x1 <= slots[s].x1 && q->y1 <= slots[s].y1;
                char m[96];
                snprintf(m, sizeof m, "%s 10:09:%02d.%03d + 40 ms: a dirty rect outside the slot boxes",
                         red ? "reduced" : "awake", ms / 1000, ms % 1000);
                TEST_ASSERT_TRUE_MESSAGE(in, m);
            }
            if (ms % 200 == 0 || ms % 1000 == 960) dirty_pair(r, &a, &b, "the slot's step", NULL);
        }
    /* the minute: the time and the slots */
    fk_face_in_t a = in_at(10, 9, 59, 960), b = shifted(a, 40);
    omni_rect_t rects[8];
    const unsigned n = dirty_pair(r, &a, &b, "the minute", rects);
    bool time = false;
    for (unsigned k = 0; k < n; k++)
        time |= rects[k].x0 == RD_ST_TIME.x0 && rects[k].y0 == RD_ST_TIME.y0 && rects[k].x1 == RD_ST_TIME.x1 &&
                rects[k].y1 == RD_ST_TIME.y1;
    TEST_ASSERT_TRUE_MESSAGE(time, "the minute does not redraw RD_ST_TIME");
}

/* the slot box holds its slot: drawn alone at each second (the rest of the frame subtracted), the head's arc and glow
 * stay inside rd_st_slot_rect */
static void test_rd_stasis_slot_box(void)
{
    const row_t *r = row_of("stasis");
    TEST_ASSERT_TRUE(RD_ST_SLOT_BOX.x1 - RD_ST_SLOT_BOX.x0 <= 40 && RD_ST_SLOT_BOX.y1 - RD_ST_SLOT_BOX.y0 <= 40);
    for (int s = 0; s < 60; s++) {
        fk_face_in_t a = in_at(10, 9, s, 0), u = a;
        u.clk.set = false;   /* no slots at all, and no lamp; the base is the same */
        frame(r, &a, s_a);
        frame(r, &u, s_b);
        omni_rect_t box[6];
        for (int k = 0; k < 6; k++) rd_st_slot_rect((s - k + 60) % 60, &box[k]);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                if (s_a[y * W + x] == s_b[y * W + x] || in_any(box, 6, x, y)) continue;
                /* the lamp and the text (the readout, the date and the label change with the clock's state) */
                if (inside(&RD_ST_LAMP, x, y) || inside(&RD_ST_STALE, x, y) || inside(&RD_ST_DATE, x, y) ||
                    inside(&RD_ST_TIME, x, y) || inside(&RD_ST_READOUT, x, y))
                    continue;
                char m[96];
                snprintf(m, sizeof m, "second %d: pixel (%d, %d) of a slot outside the slot boxes", s, x, y);
                TEST_FAIL_MESSAGE(m);
            }
    }
}

/* still 7 (unset) shows no lit go-lamp pixel, no seconds slot and the readout's unset lines */
static void test_rd_stasis_unset(void)
{
    const row_t *r = row_of("stasis");
    rd_still_frame(r->ops, r->ctx, 7, s_a);
    for (int y = 0; y < 30; y++)
        for (int x = 213; x < 254; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 13.f;
            if (dx * dx + dy * dy < 6.f * 6.f) TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, s_a[y * W + x], "a lit go-lamp pixel");
        }
    rd_still_t st;
    rd_still(7, &st);
    char l1[RD_ASLEEP_LINE];
    const char *l2 = NULL;
    rd_asleep_lines(st.in.slept_s, st.in.clk.set, l1, &l2);
    TEST_ASSERT_EQUAL_STRING("AWAITING TIME SIGNAL", l1);
    TEST_ASSERT_EQUAL_STRING("NOTHING TO THAW YET", l2);
    /* the readout is the unset one whatever slept_s says, and differs from a set clock's */
    fk_face_in_t in = st.in;
    in.slept_s = 12307;
    frame(r, &in, s_b);
    assert_same(s_a, s_b, "unset with a slept time");
    in = st.in, in.clk = (fk_clock_t){true, false, 10, 9, 30, 1, 10, 0, 500, 305};
    frame(r, &in, s_b);
    long readout = 0;
    for (int y = RD_ST_READOUT.y0; y < RD_ST_READOUT.y1; y++)
        for (int x = RD_ST_READOUT.x0; x < RD_ST_READOUT.x1; x++) readout += s_a[y * W + x] != s_b[y * W + x];
    TEST_ASSERT_TRUE_MESSAGE(readout > 200, "the unset readout is the set one");
    /* no slot: an unset frame is the same at every second */
    in = st.in, in.clk.s = 41, in.clk.ms = 300;
    frame(r, &in, s_b);
    assert_same(s_a, s_b, "unset at another second");
}

/* still 8 (stale) differs from still 0 only inside RD_ST_STALE and the lamp's box (with still 8's slept_s: the
 * readout is not the stale marker's) */
static void test_rd_stasis_stale_rect(void)
{
    const row_t *r = row_of("stasis");
    rd_still_frame(r->ops, r->ctx, 8, s_a);
    rd_still_t s0, s8;
    rd_still(0, &s0);
    rd_still(8, &s8);
    /* still 0 carries slept_s 12 307 and still 8 -1 (RW12: only row 0 has a time asleep), so the readouts differ by
     * design; still 0 is drawn with still 8's slept_s, so what is compared is the stale state alone */
    s0.in.slept_s = s8.in.slept_s;
    frame(r, &s0.in, s_b);
    long stale = 0, lamp = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (s_a[y * W + x] == s_b[y * W + x]) continue;
            const bool st = inside(&RD_ST_STALE, x, y), lp = inside(&RD_ST_LAMP, x, y);
            if (!st && !lp) {
                char m[96];
                snprintf(m, sizeof m, "stale changes pixel (%d, %d), outside RD_ST_STALE and the lamp", x, y);
                TEST_FAIL_MESSAGE(m);
            }
            stale += st, lamp += lp;
        }
    TEST_ASSERT_TRUE_MESSAGE(stale > 100, "no stale marker");
    TEST_ASSERT_TRUE_MESSAGE(lamp > 20, "the lamp is not amber");
}

static void test_rd_stasis_tap_preview_gate(void)
{
    const row_t *r = row_of("stasis");
    static const int16_t P[][2] = {{233, 233}, {0, 0}, {465, 465}, {233, 13}, {60, 400}};
    for (unsigned i = 0; i < sizeof P / sizeof P[0]; i++)
        TEST_ASSERT_EQUAL(FK_TAP_CHIME, r->ops->tap(r->ctx, P[i][0], P[i][1]));
    TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap(r->ctx, -1, 200));
    TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap(r->ctx, 200, W));
    for (int m = 0; m < 8; m++)
        TEST_ASSERT_EQUAL_MESSAGE(m == 3, rd_chime_preview_allowed(m & 1, (m >> 1) & 1, (m >> 2) & 1),
                                  "the preview plays only with Chime on, the sound on and not provisioning");
}

/* the time asleep reaches the face: still 0 reads "STASIS 3H 25M 07S", and the readout redraws in RD_ST_READOUT alone */
static void test_rd_stasis_readout(void)
{
    const row_t *r = row_of("stasis");
    fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
    b.slept_s = 12307;
    omni_rect_t rects[8];
    const unsigned n = dirty_pair(r, &a, &b, "the readout", rects);
    TEST_ASSERT_TRUE(n == 1);
    TEST_ASSERT_TRUE(rects[0].x0 == RD_ST_READOUT.x0 && rects[0].y1 == RD_ST_READOUT.y1);
    TEST_ASSERT_TRUE(frames_differ(s_a, s_b) > 200);
    /* the readout's glyphs lie inside RD_ST_READOUT, the date's in RD_ST_DATE, the hero's in RD_ST_TIME, the label's
     * in RD_ST_STALE */
    static const int32_t SL[] = {-1, 12307, 999 * 3600 + 59 * 60 + 59, 1000 * 3600, 245};
    for (unsigned k = 0; k < sizeof SL / sizeof SL[0]; k++)
        for (int st = 0; st < 3; st++) {
            fk_face_in_t in = in_at(st == 2 ? 20 : 10, 8, 30, 500);
            in.slept_s = SL[k], in.clk.stale = st == 1, in.h24 = st != 2;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
            s_ngl = 0;
            fk_test_glyph_hook = record;
            frame(r, &in, s_a);
            fk_test_glyph_hook = NULL;
            for (unsigned g = 0; g < s_ngl && g < MAX_GLYPHS; g++) {
                const glyph_t *q = &s_gl[g];
                const omni_rect_t *box = q->y1 <= 128 ? &RD_ST_STALE : q->y1 <= 160 ? &RD_ST_DATE
                                         : q->y1 <= 290 ? &RD_ST_TIME : &RD_ST_READOUT;
                char m[128];
                snprintf(m, sizeof m, "a glyph box (%d, %d)-(%d, %d) outside its element's rect", q->x0, q->y0, q->x1,
                         q->y1);
                TEST_ASSERT_TRUE_MESSAGE(q->x0 >= box->x0 && q->y0 >= box->y0 && q->x1 <= box->x1 && q->y1 <= box->y1,
                                         m);
            }
        }
}


/* ---- Ship Terminal (spec section 10.7) */
static bool rect_in(const omni_rect_t *q, const omni_rect_t *box)
{
    return q->x0 >= box->x0 && q->y0 >= box->y0 && q->x1 <= box->x1 && q->y1 <= box->y1;
}

/* the frames differ only inside box; returns how many pixels differ */
static long differ_inside(const uint16_t *a, const uint16_t *b, const omni_rect_t *box, const char *what)
{
    long n = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (a[y * W + x] == b[y * W + x]) continue;
            if (!inside(box, x, y)) {
                char m[160];
                snprintf(m, sizeof m, "%s: pixel (%d, %d) differs outside its rect", what, x, y);
                TEST_FAIL_MESSAGE(m);
            }
            n++;
        }
    return n;
}

static fk_face_in_t tm_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.mono_ms = 5000;   /* the cursor on, the spinner's first step */
    return in;
}

/* the ops' wiring: the brief's numbers, the tap's rect, the tapped still */
static void test_rd_tm_face(void)
{
    TEST_ASSERT_EQUAL_STRING("terminal", RD_FACE_TERMINAL.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_TERMINAL.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(900, RD_FACE_TERMINAL.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(1350, RD_FACE_TERMINAL.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_TERMINAL.pulse_guard_ms);   /* every tap turns the page */
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_TERMINAL.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_TERMINAL.reveal);
    TEST_ASSERT_FALSE(RD_FACE_TERMINAL.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_TERMINAL.tap_var);
    TEST_ASSERT_EQUAL_UINT16(1, RD_TM_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_TM_TAPPED_VARIANT, rd_still_variant(&RD_FACE_TERMINAL));
    TEST_ASSERT_EQUAL_UINT16(0, rd_still_variant(&RD_FACE_STASIS));
    const omni_rect_t g = {64, 98, 402, 360};   /* the mockup's glass: (64, 98) 338 x 262 */
    TEST_ASSERT_TRUE(RD_TM_GLASS.x0 == g.x0 && RD_TM_GLASS.y0 == g.y0 && RD_TM_GLASS.x1 == g.x1 &&
                     RD_TM_GLASS.y1 == g.y1);
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(1, rd_tm_chime_rects(c));
    TEST_ASSERT_TRUE(c[0].x0 == g.x0 && c[0].y1 == g.y1);
    /* the motion regions are small (the notes: 12 x 20 each, with their glow; the cursor's x follows the prompt) */
    TEST_ASSERT_TRUE((RD_TM_SPINNER.x1 - RD_TM_SPINNER.x0) * (RD_TM_SPINNER.y1 - RD_TM_SPINNER.y0) <= 40 * 40);
    TEST_ASSERT_TRUE(RD_TM_CURSOR.y1 - RD_TM_CURSOR.y0 <= 30);
    /* still 11 is page 1 */
    rd_still_frame(&RD_FACE_TERMINAL, row_of("terminal")->ctx, RD_STILL_N, s_a);
    rd_still_t st;
    rd_still(0, &st);
    st.in.variant = 1;
    frame(row_of("terminal"), &st.in, s_b);
    assert_same(s_b, s_a, "still 11 is page 1");
    TEST_ASSERT_EQUAL_STRING("CREW ON BOARD ........ 1", rd_tm_page_line(0, 0));
    TEST_ASSERT_EQUAL_STRING("CREW REQUIRED ...... 169", rd_tm_page_line(0, 1));   /* kept by ruling */
    TEST_ASSERT_EQUAL_STRING("LAST BACKUP ...... TUESDAY", rd_tm_page_line(1, 0));
    TEST_ASSERT_EQUAL_STRING("WHICH TUESDAY .... UNKNOWN", rd_tm_page_line(1, 1));
    TEST_ASSERT_EQUAL_STRING("DAYS WITHOUT INCIDENT .. 0", rd_tm_page_line(2, 0));
    TEST_ASSERT_EQUAL_STRING("PREVIOUS RECORD ....... 0", rd_tm_page_line(2, 1));
    TEST_ASSERT_EQUAL_STRING("DRINKS MACHINE ... JAMMED", rd_tm_page_line(3, 0));
    TEST_ASSERT_EQUAL_STRING("ENGINEER NOTIFIED .... NO", rd_tm_page_line(3, 1));
    TEST_ASSERT_EQUAL_STRING(rd_tm_page_line(0, 0), rd_tm_page_line(4, 0));
}

/* the housing's ramp: the table row names the same stops (the export quantises against them) */
static void test_rd_tm_house_ramp(void)
{
    FILE *f = fopen(RD_LAYERS_JSON, "r");
    TEST_ASSERT_NOT_NULL(f);
    static char js[1 << 17];
    const size_t n = fread(js, 1, sizeof js - 1, f);
    fclose(f);
    js[n] = 0;
    const char *row = strstr(js, "\"name\": \"rd_tm_house\"");
    TEST_ASSERT_NOT_NULL(row);
    const char *rp = strstr(row, "\"ramp\": [");
    TEST_ASSERT_NOT_NULL(rp);
    rp += strlen("\"ramp\": [");
    for (int k = 0; k < RD_TM_RAMP_STOPS; k++) {
        unsigned i = 0, c = 0;
        TEST_ASSERT_EQUAL_INT(2, sscanf(rp, " [%u, \"#%6x\"]", &i, &c));
        TEST_ASSERT_EQUAL_UINT(RD_TM_RAMP[k].i, i);
        TEST_ASSERT_EQUAL_HEX32((unsigned)RD_TM_RAMP[k].r << 16 | (unsigned)RD_TM_RAMP[k].g << 8 | RD_TM_RAMP[k].b, c);
        rp = strchr(rp, ']') + 1;
        if (*rp == ',') rp++;
    }
    TEST_ASSERT_EQUAL_CHAR(']', *rp);
    static uint16_t ramp[256];
    rd_ramp565(RD_TM_RAMP, RD_TM_RAMP_STOPS, ramp);
    TEST_ASSERT_EQUAL_HEX16(0, ramp[0]);
    TEST_ASSERT_EQUAL_HEX16(0, ramp[64]);
    TEST_ASSERT_EQUAL_HEX16(RD_RGB(0x5A, 0x60, 0x68), ramp[127]);
}

/* PR32: the pulse covers the longest page typed at 25 ms a character */
static void test_rd_tm_pulse_covers_pages(void)
{
    unsigned longest = 0;
    for (unsigned p = 0; p < RD_TM_PAGES; p++) {
        const unsigned n = (unsigned)(strlen(rd_tm_page_line(p, 0)) + strlen(rd_tm_page_line(p, 1)));
        longest = n > longest ? n : longest;
    }
    TEST_ASSERT_EQUAL_UINT(52, longest);
    TEST_ASSERT_TRUE(RD_FACE_TERMINAL.pulse_len_ms >= 25u * longest);
    /* the pulse's last instant shows every page whole */
    for (unsigned p = 0; p < RD_TM_PAGES; p++) {
        fk_face_in_t in = tm_at(10, 9, 30, 500);
        in.variant = (uint16_t)p, in.pulsing = true, in.pulse_ms = (uint16_t)(RD_FACE_TERMINAL.pulse_len_ms - 1);
        const char *l[2];
        int n[2];
        rd_tm_shown(&in, l, n);
        TEST_ASSERT_EQUAL_INT((int)strlen(l[0]), n[0]);
        TEST_ASSERT_EQUAL_INT((int)strlen(l[1]), n[1]);
    }
}

/* PR32: the wake's last frame (899 ms) is the settled frame, page 0 typed whole; mid-wake it is not */
static void test_rd_tm_wake_typed(void)
{
    const row_t *r = row_of("terminal");
    for (uint16_t v = 0; v < 2; v++) {
        fk_face_in_t a = tm_at(10, 8, 30, 500), b;
        a.variant = v;
        frame(r, &a, s_c);
        b = a, b.wake_ms = 899;
        const char *l[2];
        int n[2];
        rd_tm_shown(&b, l, n);
        TEST_ASSERT_EQUAL_INT((int)strlen(l[0]), n[0]);
        TEST_ASSERT_EQUAL_INT((int)strlen(l[1]), n[1]);
        frame(r, &b, s_a);
        assert_same(s_c, s_a, "the wake's last frame");
        b.wake_ms = 600;   /* line 0 typed to 12 characters, line 1 not begun */
        rd_tm_shown(&b, l, n);
        TEST_ASSERT_EQUAL_INT(12, n[0]);
        TEST_ASSERT_EQUAL_INT(0, n[1]);
        frame(r, &b, s_a);
        TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_TM_GLASS, "mid-wake") > 200);
        b.wake_ms = 299;   /* nothing typed yet */
        rd_tm_shown(&b, l, n);
        TEST_ASSERT_TRUE(n[0] == 0 && n[1] == 0);
    }
}

/* the CRT power-on: before 250 ms only the raster line, inside the glass; the housing as at rest */
static void test_rd_tm_wake_open(void)
{
    const row_t *r = row_of("terminal");
    fk_face_in_t a = tm_at(10, 8, 30, 500), b = a;
    frame(r, &a, s_c);
    static const uint32_t T[] = {0, 60, 120, 249};
    long lit_prev = -1;
    for (unsigned k = 0; k < 4; k++) {
        b.wake_ms = T[k];
        frame(r, &b, s_a);
        differ_inside(s_c, s_a, &RD_TM_GLASS, "the raster line");
        long lit = 0;
        for (int y = RD_TM_GLASS.y0; y < RD_TM_GLASS.y1; y++)
            for (int x = RD_TM_GLASS.x0; x < RD_TM_GLASS.x1; x++) lit += s_a[y * W + x] != s_c[y * W + x];
        TEST_ASSERT_TRUE_MESSAGE(lit > lit_prev, "the raster opens");
        lit_prev = lit;
        /* no text yet: the raster's rows are all one colour across the glass's middle */
        for (int y = 200; y < 260; y++) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + 120], s_a[y * W + 340]);
    }
    /* reduced: no wake is drawn (the runtime never runs the own wake under reduced motion; the face shows settled) */
    a.reduced = b.reduced = true;
    frame(r, &a, s_c);
    b.wake_ms = 120;
    frame(r, &b, s_a);
    assert_same(s_c, s_a, "reduced: the wake shows settled");
}

/* from variant 0, four taps give pages 1, 2, 3, 0; the hit rect is the whole glass (edges inclusive), nothing outside */
static void test_rd_tm_pages(void)
{
    const row_t *r = row_of("terminal");
    const fk_face_in_t now = tm_at(10, 8, 30, 500);
    uint16_t v = 0;
    for (int k = 1; k <= 4; k++) {
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &now, 233, 229, &v));
        TEST_ASSERT_EQUAL_UINT16(k % 4, v);
    }
    fk_test_tap_edges(r->ops, r->ctx, RD_TM_GLASS, FK_TAP_PULSE);   /* tap(): the same test as tap_var's */
    const int xs[3] = {RD_TM_GLASS.x0, 233, RD_TM_GLASS.x1 - 1}, ys[3] = {RD_TM_GLASS.y0, 229, RD_TM_GLASS.y1 - 1};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            v = 2;
            TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &now, (int16_t)xs[i], (int16_t)ys[j], &v));
            TEST_ASSERT_EQUAL_UINT16(3, v);
        }
    static const int16_t OUT[][2] = {{63, 229}, {402, 229}, {233, 97}, {233, 360}, {10, 10}, {233, 420}};
    for (unsigned i = 0; i < sizeof OUT / sizeof OUT[0]; i++) {
        v = 1;
        TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap_var(r->ctx, &now, OUT[i][0], OUT[i][1], &v));
        TEST_ASSERT_EQUAL_UINT16(1, v);
        TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap(r->ctx, OUT[i][0], OUT[i][1]));
    }
    /* unset the log shows its NTP lines whatever the page: the tap pulses but leaves the page; stale it still turns */
    fk_face_in_t u = now;
    u.clk.set = false;
    v = 2;
    TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &u, 233, 229, &v));
    TEST_ASSERT_EQUAL_UINT16(2, v);
    u = now, u.clk.stale = true;
    TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &u, 233, 229, &v));
    TEST_ASSERT_EQUAL_UINT16(3, v);
    /* each page differs from the others inside RD_TM_LOG only; variant 4 is page 0 again */
    fk_face_in_t a = tm_at(10, 9, 30, 500);
    frame(r, &a, s_c);
    for (uint16_t p = 1; p < RD_TM_PAGES; p++) {
        fk_face_in_t b = a;
        b.variant = p;
        frame(r, &b, s_a);
        TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_TM_LOG, "a page") > 300);
        omni_rect_t d[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, d);
        TEST_ASSERT_TRUE(n >= 1);
        for (unsigned k = 0; k < n; k++) TEST_ASSERT_TRUE(rect_in(&d[k], &RD_TM_LOG));
    }
    fk_face_in_t b = a;
    b.variant = 4;
    frame(r, &b, s_a);
    assert_same(s_c, s_a, "variant 4 is page 0");
}

/* 10 ms into a tap's pulse no page character is drawn; at 25 ms x n the first n are; the pulse's end is the page whole;
 * under reduced motion the page shows typed */
static void test_rd_tm_typing(void)
{
    const row_t *r = row_of("terminal");
    for (uint16_t p = 0; p < RD_TM_PAGES; p++) {
        fk_face_in_t a = tm_at(10, 9, 30, 500), b, q;
        a.variant = p;
        frame(r, &a, s_c);   /* the page typed */
        b = a, b.pulsing = true, b.pulse_ms = 10;
        const char *l[2];
        int n[2];
        rd_tm_shown(&b, l, n);
        TEST_ASSERT_TRUE(n[0] == 0 && n[1] == 0);
        frame(r, &b, s_a);
        q = b, q.variant = (uint16_t)((p + 1) % RD_TM_PAGES);   /* another page, also untyped: the same frame */
        frame(r, &q, s_b);
        assert_same(s_a, s_b, "10 ms into the pulse: no character of either page");
        TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_TM_LOG, "the untyped page") > 300);
        const int len0 = (int)strlen(l[0]), len1 = (int)strlen(l[1]);
        memcpy(s_b, s_a, sizeof s_a);   /* the frame with n - 1 characters */
        for (int k = 1; k <= len0 + len1; k++) {
            b.pulse_ms = (uint16_t)(25 * k);
            rd_tm_shown(&b, l, n);
            TEST_ASSERT_EQUAL_INT(k < len0 ? k : len0, n[0]);
            TEST_ASSERT_EQUAL_INT(k < len0 ? 0 : k - len0, n[1]);
            fk_face_in_t e = b;
            e.pulse_ms = (uint16_t)(25 * k + 24);   /* the same count until the next character's 25 ms */
            int m[2];
            rd_tm_shown(&e, l, m);
            TEST_ASSERT_TRUE(m[0] == n[0] && m[1] == n[1]);
            frame(r, &b, s_a);
            const char ch = k <= len0 ? l[0][k - 1] : l[1][k - 1 - len0];
            const long d = differ_inside(s_b, s_a, &RD_TM_LOG, "one more character");
            if (ch != ' ') TEST_ASSERT_TRUE_MESSAGE(d > 0, "a typed character draws");
            memcpy(s_b, s_a, sizeof s_a);
        }
        assert_same(s_c, s_a, "every character typed: the page at rest");
        b.reduced = true, b.pulse_ms = 10;
        a.reduced = true;
        frame(r, &a, s_c);
        frame(r, &b, s_a);
        assert_same(s_c, s_a, "reduced: the page shows typed");
    }
}

/* within a minute at rest every dirty rect lies inside RD_TM_CURSOR or RD_TM_SPINNER, and covers what changed; both
 * move within the minute */
static void test_rd_tm_rest_dirty(void)
{
    const row_t *r = row_of("terminal");
    for (int st = 0; st < 4; st++) {   /* 24 h, 12 h, stale, and 22:09 in 12 h on WED 28 NOV (the Task 11 review's M1:
                                         * the widest prompt, > WED 28 NOV . PM) */
        bool cur = false, spin = false;
        for (int ms = 120; ms + 40 < 60000; ms += 40) {
            fk_face_in_t a = in_at(st == 3 ? 22 : 10, 9, ms / 1000, ms % 1000);
            a.h24 = st != 1 && st != 3, a.clk.stale = st == 2;
            if (st == 3) a.clk.mday = 28, a.clk.mon = 10, a.clk.wday = 3;
            a.mono_ms = 777000u + (uint32_t)ms;
            fk_face_in_t b = ran(shifted(a, 40), 40);
            omni_rect_t rects[8];
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            for (unsigned k = 0; k < n; k++) {
                const bool c = rect_in(&rects[k], &RD_TM_CURSOR), s = rect_in(&rects[k], &RD_TM_SPINNER);
                char m[96];
                snprintf(m, sizeof m, "10:09:%02d.%03d + 40 ms: a dirty rect outside the cursor and the spinner",
                         ms / 1000, ms % 1000);
                TEST_ASSERT_TRUE_MESSAGE(c || s, m);
                cur |= c, spin |= s;
            }
            if (ms % 520 == 0) dirty_pair(r, &a, &b, "rest", NULL);
        }
        TEST_ASSERT_TRUE_MESSAGE(cur && spin, "the cursor and the spinner move at rest");
    }
    /* under reduced motion nothing moves within the minute */
    for (int ms = 120; ms + 40 < 60000; ms += 360) {
        fk_face_in_t a = in_at(10, 9, ms / 1000, ms % 1000);
        a.reduced = true, a.mono_ms = 777000u + (uint32_t)ms;
        fk_face_in_t b = ran(shifted(a, 40), 40);
        omni_rect_t rects[8];
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    }
}

/* on the minute's change the old time lingers 120 ms at 30 % (persistence): shown at m:00.060, gone at m:00.130; not
 * under reduced motion or unset */
static void test_rd_tm_linger(void)
{
    const row_t *r = row_of("terminal");
    for (int hm = 0; hm < 2; hm++) {
        fk_face_in_t a = tm_at(10, 0, 0, 60), b = tm_at(10, 0, 0, 130), c = tm_at(10, 0, 5, 0);
        a.h24 = b.h24 = c.h24 = hm == 0;
        frame(r, &b, s_b);
        frame(r, &c, s_c);
        assert_same(s_c, s_b, "the linger is gone at m:00.130");
        frame(r, &a, s_a);
        TEST_ASSERT_TRUE(differ_inside(s_b, s_a, &RD_TM_TIME, "the old time") > 200);
        /* where the new time is dark the old one shows at about 30 % of the phosphor */
        int maxg = 0;
        for (int y = RD_TM_TIME.y0; y < RD_TM_TIME.y1; y++)
            for (int x = RD_TM_TIME.x0; x < RD_TM_TIME.x1; x++)
                if (s_b[y * W + x] == 0 || ((s_b[y * W + x] >> 5) & 63) < 2) {
                    const int g = (s_a[y * W + x] >> 5) & 63;
                    maxg = g > maxg ? g : maxg;
                }
        TEST_ASSERT_TRUE_MESSAGE(maxg >= 12 && maxg <= 24, "the old time is not at about 30 %");
        omni_rect_t d[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, d);
        TEST_ASSERT_TRUE(n >= 1);
        for (unsigned k = 0; k < n; k++) TEST_ASSERT_TRUE(rect_in(&d[k], &RD_TM_TIME));
        fk_face_in_t ra = a, rb = b;
        ra.reduced = rb.reduced = true;
        frame(r, &ra, s_a);
        frame(r, &rb, s_b);
        assert_same(s_b, s_a, "reduced: no linger");
        ra = a, rb = b;
        ra.clk.set = rb.clk.set = false;
        frame(r, &ra, s_a);
        frame(r, &rb, s_b);
        assert_same(s_b, s_a, "unset: no linger");
    }
}

/* C7: the glance lights nothing outside RD_TM_TIME (no date line), at every orbit, 12 and 24 h; stale adds only the
 * marker (in RD_TM_STALE, moved by the orbit) */
static void test_rd_tm_glance_no_date(void)
{
    const row_t *r = row_of("terminal");
    static const int HM[][2] = {{10, 8}, {0, 0}, {23, 59}, {1, 5}, {12, 59}};
    for (unsigned k = 0; k < sizeof HM / sizeof HM[0]; k++)
        for (int hm = 0; hm < 2; hm++)
            for (int st = 0; st < 3; st++)
                for (int o = 0; o < 9; o++) {
                    fk_face_in_t in = in_at(HM[k][0], HM[k][1], 30, 500);
                    in.glance = true, in.h24 = hm == 0, in.odx = ORB9[o][0], in.ody = ORB9[o][1];
                    in.clk.stale = st == 1, in.clk.set = st != 2;
                    in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
                    frame(r, &in, s_a);
                    const omni_rect_t sm = {(int16_t)(RD_TM_STALE.x0 + in.odx), (int16_t)(RD_TM_STALE.y0 + in.ody),
                                            (int16_t)(RD_TM_STALE.x1 + in.odx), (int16_t)(RD_TM_STALE.y1 + in.ody)};
                    long lit = 0;
                    for (int y = 0; y < W; y++)
                        for (int x = 0; x < W; x++) {
                            if (!s_a[y * W + x]) continue;
                            lit++;
                            if (inside(&RD_TM_TIME, x, y) || (st == 1 && inside(&sm, x, y))) continue;
                            char m[128];
                            snprintf(m, sizeof m, "glance %02d:%02d: a lit pixel (%d, %d) outside the time", HM[k][0],
                                     HM[k][1], x, y);
                            TEST_FAIL_MESSAGE(m);
                        }
                    TEST_ASSERT_TRUE(lit > 500);
                }
}

/* stale changes only the header line (the marker replaces it) */
static void test_rd_tm_stale_rect(void)
{
    const row_t *r = row_of("terminal");
    fk_face_in_t a = tm_at(10, 9, 30, 500), b = a;
    b.clk.stale = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_TM_STALE, "stale") > 200);
    /* the header's glyphs lie inside RD_TM_STALE, the log's in RD_TM_LOG, the time's in RD_TM_TIME, the prompt's in
     * RD_TM_PROMPT, whatever the page, the clock's state and the 12/24 h setting */
    for (int st = 0; st < 4; st++)
        for (uint16_t p = 0; p < RD_TM_PAGES; p++) {
            fk_face_in_t in = tm_at(st == 3 ? 20 : 10, 8, 30, 500);
            in.variant = p, in.clk.stale = st == 1, in.clk.set = st != 2, in.h24 = st != 3;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
            s_ngl = 0;
            fk_test_glyph_hook = record;
            frame(r, &in, s_a);
            fk_test_glyph_hook = NULL;
            TEST_ASSERT_TRUE(s_ngl > 20 && s_ngl <= MAX_GLYPHS);
            for (unsigned g = 0; g < s_ngl; g++) {
                const glyph_t *q = &s_gl[g];
                const omni_rect_t bx = {(int16_t)q->x0, (int16_t)q->y0, (int16_t)q->x1, (int16_t)q->y1};
                const omni_rect_t *box = q->y1 <= 142 ? &RD_TM_STALE : q->y1 <= 204 ? &RD_TM_LOG
                                         : q->y0 < 300 ? &RD_TM_TIME : &RD_TM_PROMPT;
                char m[128];
                snprintf(m, sizeof m, "a glyph box (%d, %d)-(%d, %d) outside its element's rect", q->x0, q->y0, q->x1,
                         q->y1);
                TEST_ASSERT_TRUE_MESSAGE(rect_in(&bx, box) || (q->y1 <= 142 && rect_in(&bx, &RD_TM_SPINNER)), m);
            }
        }
}

/* the stale marker's rows (the Task 13 review: no still covers the stale glance), awake and in the glance at orbit
 * (0, 0) and (0, +4): its amber ink (red above 24) lies on rows 120 to 129 from its 130 baseline (126 middle + .263 em;
 * the stale shots, awake and the glance at the mockup's own orbit (0, +4), measure 120-129 and 124-133), the awake
 * ring's soft foot on row 130 too; moved by the orbit */
static void test_rd_tm_stale_rows(void)
{
    const row_t *r = row_of("terminal");
    for (int k = 0; k < 3; k++) {
        fk_face_in_t a = tm_at(10, 8, 30, 0);
        a.clk.stale = true, a.glance = k > 0, a.ody = (int8_t)(k == 2 ? 4 : 0);
        frame(r, &a, s_a);
        const int x0 = k ? 196 : 92, x1 = k ? 272 : 168;   /* the glance's marker at 235, the awake one's at 130 */
        int top = -1, bot = -1;
        for (int y = 100; y < 160; y++)
            for (int x = x0; x < x1; x++) {
                const uint16_t p = s_a[y * W + x];
                const int R = (p >> 11) * 255 / 31, G = ((p >> 5) & 63) * 255 / 63;
                if (R > 24 && R > G) {   /* amber, not the header's or the time's phosphor */
                    if (top < 0) top = y;
                    bot = y;
                }
            }
        char m[96];
        snprintf(m, sizeof m, "%s stale marker rows %d-%d", k ? "the glance's" : "the awake", top, bot);
        TEST_ASSERT_EQUAL_INT_MESSAGE(120 + a.ody, top, m);
        TEST_ASSERT_EQUAL_INT_MESSAGE((k ? 129 : 130) + a.ody, bot, m);   /* awake, the ring's soft foot clears 24 */
    }
}

/* still 7 (unset): no phosphor time (dashes only), the NTP lines, the unset prompt; the same frame at any second */
static void test_rd_tm_unset(void)
{
    const row_t *r = row_of("terminal");
    rd_still_frame(r->ops, r->ctx, 7, s_a);
    rd_still_t st;
    rd_still(7, &st);
    const char *l[2];
    int n[2];
    rd_tm_shown(&st.in, l, n);
    TEST_ASSERT_EQUAL_STRING("NTP .......... NO CARRIER", l[0]);
    TEST_ASSERT_EQUAL_STRING("[ AWAITING TIME SIGNAL ]", l[1]);
    /* the time's region (above the prompt's glow) holds no green-dominant pixel: the dashes are rd.paintDim */
    for (int y = RD_TM_TIME.y0; y < 300; y++)
        for (int x = RD_TM_TIME.x0; x < RD_TM_TIME.x1; x++) {
            const uint16_t p = s_a[y * W + x];
            const int R = (p >> 11) * 255 / 31, G = ((p >> 5) & 63) * 255 / 63;
            TEST_ASSERT_TRUE_MESSAGE(G <= R + 24, "a phosphor pixel in the unset time");
        }
    fk_face_in_t in = st.in;
    in.clk.s = 41, in.clk.ms = 300, in.slept_s = 99;
    frame(r, &in, s_b);
    assert_same(s_a, s_b, "unset at another second");
    in.variant = 2;   /* the page does not show while unset */
    frame(r, &in, s_b);
    assert_same(s_a, s_b, "unset on another page");
}


/* ---- Deck Sign (spec section 10.6) */
static fk_face_in_t dk_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3;   /* WED 04 NOV (the mockup's shots) */
    return in;
}

/* the frames' pixels inside box differ (how many), and outside it they are equal */
static long differ_in(const uint16_t *a, const uint16_t *b, const omni_rect_t *box)
{
    long n = 0;
    for (int y = box->y0; y < box->y1; y++)
        for (int x = box->x0; x < box->x1; x++) n += a[y * W + x] != b[y * W + x];
    return n;
}

/* a layer row's ramp: the table row names the stops (the export quantises against them) */
static void ramp_row(const char *name, const rd_ramp_stop_t *stops, int ns)
{
    FILE *f = fopen(RD_LAYERS_JSON, "r");
    TEST_ASSERT_NOT_NULL(f);
    static char js[1 << 17];
    const size_t n = fread(js, 1, sizeof js - 1, f);
    fclose(f);
    js[n] = 0;
    char key[64];
    snprintf(key, sizeof key, "\"name\": \"%s\"", name);
    const char *row = strstr(js, key);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, name);
    const char *rp = strstr(row, "\"ramp\": [");
    TEST_ASSERT_NOT_NULL(rp);
    rp += strlen("\"ramp\": [");
    for (int k = 0; k < ns; k++) {
        unsigned i = 0, c = 0;
        TEST_ASSERT_EQUAL_INT(2, sscanf(rp, " [%u, \"#%6x\"]", &i, &c));
        TEST_ASSERT_EQUAL_UINT(stops[k].i, i);
        TEST_ASSERT_EQUAL_HEX32((unsigned)stops[k].r << 16 | (unsigned)stops[k].g << 8 | stops[k].b, c);
        rp = strchr(rp, ']') + 1;
        if (*rp == ',') rp++;
    }
    TEST_ASSERT_EQUAL_CHAR(']', *rp);
}

/* the ops' wiring: the brief's numbers, the rects, the tapped still */
static void test_rd_dk_face(void)
{
    TEST_ASSERT_EQUAL_STRING("deck", RD_FACE_DECK.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_DECK.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(800, RD_FACE_DECK.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_DECK.pulse_len_ms);   /* the tap starts nothing: the variant draws */
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_DECK.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_DECK.reveal);
    TEST_ASSERT_FALSE(RD_FACE_DECK.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_DECK.tap_var);
    TEST_ASSERT_NOT_NULL(RD_FACE_DECK.tap);
    TEST_ASSERT_EQUAL_UINT16(1, RD_DK_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_DK_TAPPED_VARIANT, rd_still_variant(&RD_FACE_DECK));
    /* the motion regions: the beacon's glow (r 46 about (233, 88)) and the two 170 x 150 columns */
    TEST_ASSERT_TRUE(RD_DK_BEACON.x0 <= 233 - 46 && RD_DK_BEACON.x1 >= 233 + 46 && RD_DK_BEACON.y0 <= 88 - 46 &&
                     RD_DK_BEACON.y1 >= 88 + 46);
    TEST_ASSERT_TRUE((RD_DK_BEACON.x1 - RD_DK_BEACON.x0) * (RD_DK_BEACON.y1 - RD_DK_BEACON.y0) <= 94 * 94);
    TEST_ASSERT_EQUAL_INT(174, RD_DK_SPRAY_H.x1 - RD_DK_SPRAY_H.x0);   /* the widest strings on their own advances */
    TEST_ASSERT_EQUAL_INT(150, RD_DK_SPRAY_H.y1 - RD_DK_SPRAY_H.y0);
    TEST_ASSERT_EQUAL_INT(174, RD_DK_SPRAY_M.x1 - RD_DK_SPRAY_M.x0);
    TEST_ASSERT_EQUAL_INT(150, RD_DK_SPRAY_M.y1 - RD_DK_SPRAY_M.y0);
    TEST_ASSERT_EQUAL_INT(134 * 2, RD_DK_SPRAY_H.x0 + RD_DK_SPRAY_H.x1);   /* centred on the columns */
    TEST_ASSERT_EQUAL_INT(332 * 2, RD_DK_SPRAY_M.x0 + RD_DK_SPRAY_M.x1);
    TEST_ASSERT_TRUE(rect_in(&RD_DK_STALE, &RD_DK_NOTICE));
    TEST_ASSERT_TRUE(rect_in(&RD_DK_NOTICE, &RD_DK_TAP));
    TEST_ASSERT_TRUE(RD_DK_TAP.x1 - RD_DK_TAP.x0 >= 44 && RD_DK_TAP.y1 - RD_DK_TAP.y0 >= 44);
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(2, rd_dk_chime_rects(c));
    TEST_ASSERT_TRUE(c[0].x0 == RD_DK_BEACON.x0 && c[0].y1 == RD_DK_BEACON.y1);
    TEST_ASSERT_TRUE(c[1].x0 == RD_DK_RIVETS.x0 && c[1].y1 == RD_DK_RIVETS.y1);
    /* still 11 is the second notice */
    const row_t *r = row_of("deck");
    rd_still_frame(&RD_FACE_DECK, r->ctx, RD_STILL_N, s_a);
    rd_still_t st;
    rd_still(0, &st);
    st.in.variant = 1;
    frame(r, &st.in, s_b);
    assert_same(s_b, s_a, "still 11 is the second notice");
    ramp_row("rd_dk_frame", RD_DK_RAMP, RD_DK_RAMP_STOPS);
    static uint16_t ramp[256];
    rd_ramp565(RD_DK_RAMP, RD_DK_RAMP_STOPS, ramp);
#define R565(r, g, b) \
    (uint16_t)((((r) * 31 + 127) / 255) << 11 | (((g) * 63 + 127) / 255) << 5 | (((b) * 31 + 127) / 255))
    TEST_ASSERT_EQUAL_HEX16(0, ramp[0]);   /* each run's colour at its end (rd_ramp565 rounds to RGB565) */
    TEST_ASSERT_EQUAL_HEX16(R565(0x8A, 0x90, 0x96), ramp[63]);
    TEST_ASSERT_EQUAL_HEX16(0, ramp[64]);
    TEST_ASSERT_EQUAL_HEX16(R565(0xDC, 0xD3, 0xBE), ramp[127]);
    TEST_ASSERT_EQUAL_HEX16(R565(0x8E, 0x2B, 0x1E), ramp[191]);
    TEST_ASSERT_EQUAL_HEX16(R565(0xC4, 0x49, 0x2F), ramp[255]);
#undef R565
}

/* the const tables (lesson 3): the beacon's linear glow and the spray's mask are their builders' bytes */
static void test_rd_dk_tables(void)
{
    static uint8_t lut[FK_GLOW_LUT_MAX];
    static const int LR[] = RD_LIN_LUT_RADII;
    for (unsigned i = 0; i < sizeof LR / sizeof LR[0]; i++) {
        rd_lin_lut_build(lut, LR[i]);
        TEST_ASSERT_NOT_NULL(rd_glow_lut(LR[i]));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(lut, rd_glow_lut(LR[i]), FK_GLOW_LUT_N(LR[i]));
    }
    TEST_ASSERT_TRUE(rd_glow_lut(46)[0] >= 250 && rd_glow_lut(46)[FK_GLOW_LUT_N(46) - 1] <= 1);   /* 1 to 0 */
    static uint8_t sp[RD_DK_SPRAY_TILE * RD_DK_SPRAY_TILE];
    rd_spray_lut_build(sp);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sp, rd_dk_spray_lut(), sizeof sp);
    unsigned last = 0, hist[256] = {0};
    for (unsigned k = 0; k < sizeof sp; k++) last += sp[k] == RD_DK_SPRAY_DONE, hist[sp[k]]++;
    TEST_ASSERT_TRUE(last >= 64);          /* the last specks */
    TEST_ASSERT_EQUAL_UINT(0, hist[0]);    /* nothing shows at level 0 */
    TEST_ASSERT_EQUAL_UINT(0, hist[255]);
    /* the levels: 0 before and at the start, rising, 252 at 699 ms, done from 700 */
    TEST_ASSERT_EQUAL_INT(0, rd_dk_spray_level(-5));
    TEST_ASSERT_EQUAL_INT(0, rd_dk_spray_level(0));
    TEST_ASSERT_EQUAL_INT(252, rd_dk_spray_level(699));
    TEST_ASSERT_EQUAL_INT(RD_DK_SPRAY_DONE, rd_dk_spray_level(700));
    TEST_ASSERT_EQUAL_INT(RD_DK_SPRAY_DONE, rd_dk_spray_level(100000));
    for (long t = 1; t < 700; t++) TEST_ASSERT_TRUE(rd_dk_spray_level(t) >= rd_dk_spray_level(t - 1));
    TEST_ASSERT_TRUE(rd_dk_spray_level(233) > 150 && rd_dk_spray_level(233) < 200);   /* easeOut3(1/3) = .70 */
}

/* the spray: from m:00.000 to m:00.699 the minute's column differs from the settled frame and at m:00.700 it equals
 * it; the hour's column is untouched unless the hour changed; nothing else changes but the beacon; under reduced
 * motion the new minute shows at once */
static void test_rd_dk_spray_window(void)
{
    const row_t *r = row_of("deck");
    for (int hm = 0; hm < 2; hm++)
        for (int hc = 0; hc < 2; hc++) {
            const int h = hc ? 11 : 10, m = hc ? 0 : 9;
            fk_face_in_t z = dk_at(h, m, 0, 700);
            z.h24 = hm == 0;
            frame(r, &z, s_c);   /* settled */
            fk_face_in_t q = dk_at(h, m, 31, 700);   /* settled again, mid-minute: the columns are the same */
            q.h24 = z.h24;
            frame(r, &q, s_b);
            TEST_ASSERT_EQUAL_INT32(0, differ_in(s_c, s_b, &RD_DK_SPRAY_M));
            TEST_ASSERT_EQUAL_INT32(0, differ_in(s_c, s_b, &RD_DK_SPRAY_H));
            long prev = -1;
            static const int T[] = {0, 40, 120, 300, 500, 660, 699};
            for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) {
                fk_face_in_t a = dk_at(h, m, 0, T[k]);
                a.h24 = z.h24;
                frame(r, &a, s_a);
                const long dm = differ_in(s_c, s_a, &RD_DK_SPRAY_M), dh = differ_in(s_c, s_a, &RD_DK_SPRAY_H);
                char what[96];
                snprintf(what, sizeof what, "%02d:%02d:00.%03d (%s): the minute's column", h, m, T[k],
                         hm ? "12 h" : "24 h");
                TEST_ASSERT_TRUE_MESSAGE(dm > 0, what);
                if (prev >= 0) TEST_ASSERT_TRUE_MESSAGE(dm <= prev, what);   /* it fills in */
                prev = dm;
                snprintf(what, sizeof what, "%02d:%02d:00.%03d: the hour's column", h, m, T[k]);
                if (hc) TEST_ASSERT_TRUE_MESSAGE(dh > 0, what);
                else TEST_ASSERT_EQUAL_INT32_MESSAGE(0, dh, what);
                for (int y = 0; y < W; y++)   /* nothing outside the columns but the beacon */
                    for (int x = 0; x < W; x++)
                        if (s_a[y * W + x] != s_c[y * W + x] && !inside(&RD_DK_SPRAY_M, x, y) &&
                            !inside(&RD_DK_SPRAY_H, x, y) && !inside(&RD_DK_BEACON, x, y))
                            TEST_FAIL_MESSAGE("the spray changes a pixel outside its columns");
                fk_face_in_t red = a;   /* reduced: the new minute at once */
                red.reduced = true;
                fk_face_in_t rz = z;
                rz.reduced = true;
                frame(r, &red, s_a);
                frame(r, &rz, s_b);
                assert_same(s_b, s_a, "reduced: no spray");
            }
            TEST_ASSERT_TRUE_MESSAGE(prev < 400, "699 ms: only the last specks are left");
            fk_face_in_t e = dk_at(h, m, 0, 700);
            e.h24 = z.h24;
            frame(r, &e, s_a);
            assert_same(s_c, s_a, "m:00.700 is the settled frame");
        }
    /* the dissolve differs from minute to minute (a seed per number): 10:09 and 11:09 show the same settled minute
     * column, but spray it on through different specks */
    fk_face_in_t a = dk_at(10, 9, 0, 300), b = dk_at(11, 9, 0, 300);
    fk_face_in_t a1 = dk_at(10, 9, 0, 700), b1 = dk_at(11, 9, 0, 700);
    frame(r, &a1, s_a);
    frame(r, &b1, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_DK_SPRAY_M));
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE_MESSAGE(differ_in(s_a, s_b, &RD_DK_SPRAY_M) > 100, "two minutes spray through the same specks");
}

/* four taps on the notice return to the first; the hit rect is the plate (>= 44 px), edges probed; nothing outside */
static void test_rd_dk_notice_cycle(void)
{
    const row_t *r = row_of("deck");
    fk_face_in_t a = dk_at(10, 8, 30, 500);
    char first[48], t[48];
    rd_dk_notice(&a, first);
    TEST_ASSERT_EQUAL_STRING("LIFT OUT OF ORDER SINCE LAUNCH", first);
    static const char *const WANT[4] = {"NO RUNNING \267 ALSO NO JOGGING", "NEXT TOILET 4 KM",
                                        "WED 04 NOV \267 ALL DECKS", "LIFT OUT OF ORDER SINCE LAUNCH"};
    frame(r, &a, s_c);
    uint16_t v = 0;
    for (int k = 0; k < 4; k++) {
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &a, 233, 381, &v));
        fk_face_in_t b = a;
        b.variant = v;
        const bool arrow = rd_dk_notice(&b, t);
        TEST_ASSERT_EQUAL_STRING(WANT[k], t);
        TEST_ASSERT_EQUAL(k == 1, arrow);
        frame(r, &b, s_a);
        if (k < 3) TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_DK_NOTICE, "a notice") > 100);
        else assert_same(s_c, s_a, "four taps: the first notice");
    }
    fk_face_in_t p = a;   /* 12 h: the date page's AM/PM suffix */
    p.variant = 3, p.h24 = false, p.clk.h = 22;
    rd_dk_notice(&p, t);
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV \267 PM \267 ALL DECKS", t);
    p.variant = 2;   /* the triangle is drawn after the text, inside the notice */
    frame(r, &p, s_a);
    fk_face_in_t p0 = p;
    p0.variant = 1;
    frame(r, &p0, s_b);
    differ_inside(s_b, s_a, &RD_DK_NOTICE, "the triangle");
    fk_test_tap_edges(r->ops, r->ctx, RD_DK_TAP, FK_TAP_PULSE);
    static const int16_t OUT[][2] = {{79, 381}, {386, 381}, {233, 358}, {233, 404}, {233, 233}, {233, 88}};
    for (unsigned i = 0; i < sizeof OUT / sizeof OUT[0]; i++) {
        v = 1;
        TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap_var(r->ctx, &a, OUT[i][0], OUT[i][1], &v));
        TEST_ASSERT_EQUAL_UINT16(1, v);
    }
    /* the Task 12 review: a tap while no notice shows (stale: the marker; unset: the amber line) leaves the variant */
    for (int st = 0; st < 2; st++) {
        fk_face_in_t h = a;
        h.clk.stale = st == 0, h.clk.set = st != 1;
        v = 2;
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &h, 233, 381, &v));
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, v, st ? "a tap while unset turns the notice" : "a tap while stale turns it");
    }
    /* unset: the amber notice whatever the variant; stale: the marker, whatever the variant */
    fk_face_in_t u = a;
    u.clk.set = false, u.variant = 2;
    rd_dk_notice(&u, t);
    TEST_ASSERT_EQUAL_STRING("TIME NOT SET \267 AWAITING SIGNAL", t);
    u = a, u.clk.stale = true, u.variant = 3;
    TEST_ASSERT_FALSE(rd_dk_notice(&u, t));
    TEST_ASSERT_EQUAL_STRING("", t);
}

/* 12 h: a single hour digit is centred in its column (the ink's left and right margins within 1 px); 24 h's two too */
static void test_rd_dk_12h_centre(void)
{
    const row_t *r = row_of("deck");
    static const int H[][2] = {{9, 0}, {21, 0}, {10, 1}, {9, 1}};   /* {hour, 24 h} */
    for (unsigned k = 0; k < sizeof H / sizeof H[0]; k++) {
        fk_face_in_t a = dk_at(H[k][0], 5, 30, 500);
        a.h24 = H[k][1] != 0;
        frame(r, &a, s_a);
        int x0 = W, x1 = -1;
        for (int y = 190; y < 300; y++)   /* the numbers' rows (the labels above, FWD below, are the frame's) */
            for (int x = RD_DK_SPRAY_H.x0; x < RD_DK_SPRAY_H.x1; x++) {
                const uint16_t p = s_a[y * W + x];
                const int R = (p >> 11) * 255 / 31;
                if (R > 40) x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
            }
        const int left = x0 - RD_DK_SPRAY_H.x0, right = RD_DK_SPRAY_H.x1 - 1 - x1;
        char m[96];
        snprintf(m, sizeof m, "%d:05 %s: the hour's ink margins %d and %d", H[k][0], H[k][1] ? "24 h" : "12 h", left,
                 right);
        TEST_ASSERT_TRUE_MESSAGE(x1 > x0, m);
        if (k < 2) TEST_ASSERT_TRUE_MESSAGE(left - right <= 1 && right - left <= 1, m);   /* one digit */
        else TEST_ASSERT_TRUE_MESSAGE(left >= 0 && right >= 0, m);                      /* two: inside */
    }
}

/* mid-minute every dirty rect lies inside RD_DK_BEACON, covers what changed, and the beacon moves; under reduced
 * motion nothing moves */
static void test_rd_dk_rest_dirty(void)
{
    const row_t *r = row_of("deck");
    for (int st = 0; st < 4; st++) {
        bool moved = false;
        for (int ms = 800; ms + 40 < 60000; ms += 40) {
            fk_face_in_t a = dk_at(st == 3 ? 22 : 10, 9, ms / 1000, ms % 1000);
            a.h24 = st != 1 && st != 3, a.clk.stale = st == 2, a.variant = (uint16_t)st;
            a.mono_ms = 777000u + (uint32_t)ms;
            fk_face_in_t b = ran(shifted(a, 40), 40);
            omni_rect_t rects[8];
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            for (unsigned k = 0; k < n; k++) {
                char m[96];
                snprintf(m, sizeof m, "10:09:%02d.%03d + 40 ms: a dirty rect outside the beacon", ms / 1000, ms % 1000);
                TEST_ASSERT_TRUE_MESSAGE(rect_in(&rects[k], &RD_DK_BEACON), m);
                moved = true;
            }
            if (ms % 1960 == 0) dirty_pair(r, &a, &b, "rest", NULL);
        }
        TEST_ASSERT_TRUE_MESSAGE(moved, "the beacon turns at rest");
    }
    for (int f = 0; f < 2; f++)
        for (int ms = 800; ms + 40 < 60000; ms += 360) {
            fk_face_in_t a = dk_at(10, 9, ms / 1000, ms % 1000);
            a.reduced = true, a.mono_ms = 777000u + (uint32_t)ms;
            a.fresh = f != 0, a.fresh_ms = (uint16_t)(ms % 6000);
            fk_face_in_t b = ran(shifted(a, 40), 40);
            omni_rect_t rects[8];
            TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
        }
}

/* the beacon: low strength at rest; full for 5 s after a revealing wake, fading to rest by 5.6 s, its phase the clock's
 * again from 5 s (no jump); dark when unset; the reduced pose whatever the clock and the wake */
static void test_rd_dk_beacon(void)
{
    const row_t *r = row_of("deck");
    fk_face_in_t a = dk_at(10, 8, 30, 0), f = a;
    frame(r, &a, s_c);
    long lit_rest = 0, lit_full = 0;
    f.fresh = true, f.fresh_ms = 3000;
    frame(r, &f, s_a);
    for (int y = RD_DK_BEACON.y0; y < RD_DK_BEACON.y1; y++)
        for (int x = RD_DK_BEACON.x0; x < RD_DK_BEACON.x1; x++) {
            lit_rest += (s_c[y * W + x] >> 11) & 31;
            lit_full += (s_a[y * W + x] >> 11) & 31;
        }
    TEST_ASSERT_TRUE_MESSAGE(lit_full > lit_rest + lit_rest / 2, "the wake's beacon is brighter");
    differ_inside(s_c, s_a, &RD_DK_BEACON, "the wake's beacon");
    for (int ms = 5000; ms <= 5600; ms += 600) {   /* from 5.6 s it is the rest's beacon again, at the clock's phase */
        f.fresh_ms = (uint16_t)(ms + 100);
        frame(r, &f, s_a);
        if (ms == 5600) assert_same(s_c, s_a, "5.7 s after the wake: the rest's beacon");
    }
    /* the phase runs on across 5 s: the wake's extra turn is a whole one, so at 5.000 s (still full strength) the
     * beacon is the one at the wake's start for the same clock, and one ms either side of it is all but that */
    fk_face_in_t p = a, q = a;
    p.fresh = q.fresh = true, p.fresh_ms = 0, q.fresh_ms = 5000;
    frame(r, &p, s_a);
    frame(r, &q, s_b);
    assert_same(s_a, s_b, "the beacon at 5 s: the clock's phase again, at full strength");
    q.fresh_ms = 4999;
    frame(r, &q, s_b);
    TEST_ASSERT_TRUE_MESSAGE(frames_differ(s_a, s_b) < 200, "the beacon jumps at 5 s");
    fk_face_in_t u = a;   /* unset: dark (no glow) */
    u.clk.set = false;
    frame(r, &u, s_a);
    long glow = 0;
    for (int y = RD_DK_BEACON.y0; y < 76; y++)
        for (int x = RD_DK_BEACON.x0; x < RD_DK_BEACON.x1; x++) glow += s_a[y * W + x] != 0;
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, glow, "unset: no glow above the dome");
    fk_face_in_t m = a, n2 = a;   /* reduced: one pose */
    m.reduced = n2.reduced = true;
    n2.clk.s = 41, n2.clk.ms = 777, n2.fresh = true, n2.fresh_ms = 1000;
    frame(r, &m, s_a);
    frame(r, &n2, s_b);
    assert_same(s_a, s_b, "reduced: the beacon stands still");
}

/* the wake (800 ms): the hour sprays on from 50 ms, the minute from 200 ms; from 760 ms both show whole */
static void test_rd_dk_wake(void)
{
    const row_t *r = row_of("deck");
    fk_face_in_t a = dk_at(10, 8, 30, 500), b;
    a.fresh = true, a.fresh_ms = 0;
    frame(r, &a, s_c);
    b = a;
    b.wake_ms = 40;   /* nothing of either number yet */
    b.fresh_ms = 40;
    frame(r, &b, s_a);
    fk_face_in_t settled = a;
    settled.fresh_ms = 40;
    frame(r, &settled, s_c);
    TEST_ASSERT_TRUE(differ_in(s_c, s_a, &RD_DK_SPRAY_H) > 2000);
    TEST_ASSERT_TRUE(differ_in(s_c, s_a, &RD_DK_SPRAY_M) > 2000);
    b.wake_ms = 150, b.fresh_ms = 150;   /* the hour begun, the minute not */
    settled.fresh_ms = 150;
    frame(r, &b, s_a);
    frame(r, &settled, s_c);
    const long dh = differ_in(s_c, s_a, &RD_DK_SPRAY_H), dm = differ_in(s_c, s_a, &RD_DK_SPRAY_M);
    TEST_ASSERT_TRUE(dh > 0 && dm > dh);
    for (uint32_t t = 760; t < 800; t += 39) {
        b.wake_ms = t, b.fresh_ms = (uint16_t)t, settled.fresh_ms = (uint16_t)t;
        frame(r, &b, s_a);
        frame(r, &settled, s_c);
        assert_same(s_c, s_a, "the wake's last frame is settled");
    }
    b.wake_ms = 750, b.fresh_ms = 750, settled.fresh_ms = 750;
    frame(r, &b, s_a);
    frame(r, &settled, s_c);
    TEST_ASSERT_TRUE(differ_in(s_c, s_a, &RD_DK_SPRAY_M) > 0);   /* the minute still spraying */
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_c, s_a, &RD_DK_SPRAY_H));   /* the hour's done at 750 */
    b.wake_ms = 740, b.fresh_ms = 740, settled.fresh_ms = 740;
    frame(r, &b, s_a);
    frame(r, &settled, s_c);
    TEST_ASSERT_TRUE(differ_in(s_c, s_a, &RD_DK_SPRAY_H) > 0);   /* but not at 740 */
}

/* stale: only the notice changes (the marker in RD_DK_STALE); unset: dashes, the beacon dark, the amber notice; the
 * glyphs of every state lie in their element's rect; every notice inside the 200 px radius */
static void test_rd_dk_states(void)
{
    const row_t *r = row_of("deck");
    fk_face_in_t a = dk_at(10, 9, 30, 500), b = a;
    b.clk.stale = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_DK_NOTICE, "stale") > 200);
    fk_face_in_t g = a;   /* the glance's stale marker too: only in its rect, moved by the orbit */
    g.glance = true;
    frame(r, &g, s_a);
    g.clk.stale = true;
    frame(r, &g, s_b);
    differ_inside(s_a, s_b, &RD_DK_STALE, "the stale glance");
    rd_still_frame(r->ops, r->ctx, 7, s_a);   /* unset */
    rd_still_t st;
    rd_still(7, &st);
    fk_face_in_t u = st.in;
    u.clk.s = 41, u.clk.ms = 300, u.variant = 2, u.fresh = true, u.fresh_ms = 100;
    frame(r, &u, s_b);
    assert_same(s_a, s_b, "unset at another second, notice and wake");
    for (int y = 190; y < 300; y++)   /* the numbers' rows: the dashes only, one band of rows */
        for (int x = RD_DK_SPRAY_H.x0; x < RD_DK_SPRAY_H.x1; x++)
            if (s_a[y * W + x]) TEST_ASSERT_TRUE_MESSAGE(y >= 236 && y < 256, "unset: ink in the column off the dashes");
    for (int v = 0; v < 4; v++)
        for (int st2 = 0; st2 < 4; st2++)
            for (int hm = 0; hm < 2; hm++) {
                fk_face_in_t in = dk_at(st2 == 3 ? 22 : 10, 8, 30, 500);
                in.variant = (uint16_t)v, in.clk.stale = st2 == 1, in.clk.set = st2 != 2, in.h24 = hm == 0;
                in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
                s_ngl = 0;
                fk_test_glyph_hook = record;
                frame(r, &in, s_a);
                fk_test_glyph_hook = NULL;
                TEST_ASSERT_TRUE(s_ngl <= MAX_GLYPHS);
                unsigned w = 0;
                const float far2 = glyph_reach2(&w);
                char m[128];
                snprintf(m, sizeof m, "notice %d state %d: a glyph reaches %.0f px^2 from the centre", v, st2,
                         (double)far2);
                TEST_ASSERT_TRUE_MESSAGE(far2 <= 200.f * 200.f, m);
                for (unsigned k = 0; k < s_ngl; k++) {
                    const glyph_t *q = &s_gl[k];
                    const omni_rect_t bx = {(int16_t)q->x0, (int16_t)q->y0, (int16_t)q->x1, (int16_t)q->y1};
                    const omni_rect_t *box = q->y0 >= 360  ? &RD_DK_NOTICE
                                             : q->x0 < 233 ? &RD_DK_SPRAY_H
                                                           : &RD_DK_SPRAY_M;
                    snprintf(m, sizeof m, "a glyph box (%d, %d)-(%d, %d) outside its element's rect", q->x0, q->y0,
                             q->x1, q->y1);
                    TEST_ASSERT_TRUE_MESSAGE(rect_in(&bx, box), m);
                }
            }
}

/* the chime: the beacon at full strength while the thrum runs; the click blinks the rivet line, inside RD_DK_RIVETS */
static void test_rd_dk_chime(void)
{
    const row_t *r = row_of("deck");
    fk_face_in_t a = dk_at(10, 9, 21, 300), b = a;
    frame(r, &a, s_c);
    b.click = 1.f;
    frame(r, &b, s_a);
    TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_DK_RIVETS, "the click") >= 17 * 4);
    omni_rect_t d[8];
    const uint8_t n = r->ops->dirty(r->ctx, &a, &b, d);
    TEST_ASSERT_TRUE(n >= 1);
    for (unsigned k = 0; k < n; k++) TEST_ASSERT_TRUE(rect_in(&d[k], &RD_DK_RIVETS));
    dirty_pair(r, &a, &b, "the click", NULL);
    dirty_pair(r, &b, &a, "the click's end", NULL);
    b = a, b.thrum = 0.7f;
    frame(r, &b, s_a);
    fk_face_in_t f = a;   /* the wake's full strength (at its start: the same phase) is the chime's */
    f.fresh = true, f.fresh_ms = 0;
    frame(r, &f, s_b);
    assert_same(s_b, s_a, "the chime's beacon is at full strength");
    b.reduced = true, b.click = 1.f;   /* not under reduced motion */
    a.reduced = true;
    frame(r, &a, s_c);
    frame(r, &b, s_a);
    assert_same(s_c, s_a, "reduced: no chime");
}


/* ---- Hull Plate (spec section 10.8) */
static fk_face_in_t hp_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3;   /* WED 04 NOV (the mockup's shots) */
    in.mono_ms = 5000;
    return in;
}

#define HP_TAU 6.28318530718f
static float hp_norm(float a)   /* into [0, 2 pi) */
{
    while (a < 0) a += HP_TAU;
    while (a >= HP_TAU) a -= HP_TAU;
    return a;
}

/* a run's colour, as the runs and the ramp's stops are named in the layer table */
static void runs_row(const char *name, const rd_hp_run_t *runs, int n)
{
    FILE *f = fopen(RD_LAYERS_JSON, "r");
    TEST_ASSERT_NOT_NULL(f);
    static char js[1 << 17];
    const size_t got = fread(js, 1, sizeof js - 1, f);
    fclose(f);
    TEST_ASSERT_TRUE(got < sizeof js - 1);
    js[got] = 0;
    char key[64];
    snprintf(key, sizeof key, "\"name\": \"%s\"", name);
    const char *row = strstr(js, key);
    TEST_ASSERT_NOT_NULL_MESSAGE(row, name);
    const char *rp = strstr(row, "\"runs\": [");
    TEST_ASSERT_NOT_NULL(rp);
    rp += strlen("\"runs\": [");
    for (int k = 0; k < n; k++) {
        unsigned i0 = 0, i1 = 0, c = 0;
        TEST_ASSERT_EQUAL_INT(3, sscanf(rp, " [%u, %u, \"#%6x\"]", &i0, &i1, &c));
        TEST_ASSERT_EQUAL_UINT(runs[k].i0, i0);
        TEST_ASSERT_EQUAL_UINT(runs[k].i1, i1);
        TEST_ASSERT_EQUAL_HEX32((unsigned)runs[k].r << 16 | (unsigned)runs[k].g << 8 | runs[k].b, c);
        rp = strchr(rp, ']') + 1;
        if (*rp == ',') rp++;
    }
    TEST_ASSERT_EQUAL_CHAR(']', *rp);
}

/* the ops' wiring: the brief's numbers, the rects, the tapped still, the tables against the layer table */
static void test_rd_hp_face(void)
{
    TEST_ASSERT_EQUAL_STRING("hull", RD_FACE_HULL.key);
    TEST_ASSERT_EQUAL_UINT16(50, RD_FACE_HULL.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(1000, RD_FACE_HULL.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_HULL.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_HULL.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_HULL.reveal);
    TEST_ASSERT_FALSE(RD_FACE_HULL.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_HULL.tap_var);
    TEST_ASSERT_NOT_NULL(RD_FACE_HULL.tap);
    TEST_ASSERT_EQUAL_UINT16(1, RD_HP_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_HP_TAPPED_VARIANT, rd_still_variant(&RD_FACE_HULL));
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_HULL, rd_face_by_key("hull"));
    /* the regions: the running light (16 x 16 about (94.4, 94.4), its ring r 4.7), the arc's 140 x 24 at most */
    TEST_ASSERT_TRUE(RD_HP_RUNLIGHT.x0 <= 86 && RD_HP_RUNLIGHT.x1 >= 103 && RD_HP_RUNLIGHT.y0 <= 86 &&
                     RD_HP_RUNLIGHT.y1 >= 103);
    TEST_ASSERT_TRUE(RD_HP_RUNLIGHT.x1 - RD_HP_RUNLIGHT.x0 <= 20 && RD_HP_RUNLIGHT.y1 - RD_HP_RUNLIGHT.y0 <= 20);
    TEST_ASSERT_TRUE(RD_HP_HAZARD.x1 - RD_HP_HAZARD.x0 <= 140 && RD_HP_HAZARD.y1 - RD_HP_HAZARD.y0 <= 24);
    TEST_ASSERT_TRUE(RD_HP_HAZARD.x0 <= 233 - 41 && RD_HP_HAZARD.x1 >= 233 + 42 && RD_HP_HAZARD.y0 <= 26 &&
                     RD_HP_HAZARD.y1 >= 41);
    TEST_ASSERT_TRUE(rect_in(&RD_HP_STALE, &RD_HP_READOUT));
    TEST_ASSERT_TRUE(rect_in(&RD_HP_READOUT, &RD_HP_TAP));
    TEST_ASSERT_TRUE(RD_HP_TAP.x1 - RD_HP_TAP.x0 >= 44 && RD_HP_TAP.y1 - RD_HP_TAP.y0 >= 44);
    TEST_ASSERT_TRUE(RD_HP_POOL.x0 == 10 && RD_HP_POOL.y0 == 7 && RD_HP_POOL.x1 == 10 + 278 && RD_HP_POOL.y1 == 7 + 261);
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(2, rd_hp_chime_rects(c));
    TEST_ASSERT_TRUE(c[0].x0 == RD_HP_POOL.x0 && c[0].y1 == RD_HP_POOL.y1);
    TEST_ASSERT_TRUE(c[1].x0 == RD_HP_RUNLIGHT.x0 && c[1].y1 == RD_HP_RUNLIGHT.y1);
    /* still 11 is the reserve page */
    const row_t *r = row_of("hull");
    rd_still_frame(&RD_FACE_HULL, r->ctx, RD_STILL_N, s_a);
    rd_still_t st;
    rd_still(0, &st);
    st.in.variant = 1;
    frame(r, &st.in, s_b);
    assert_same(s_b, s_a, "still 11 is the reserve page");
    ramp_row("rd_hp_plate", RD_HP_PLATE_RAMP, RD_HP_PLATE_RAMP_STOPS);
    runs_row("rd_hp_marks", RD_HP_MARK_RUNS, RD_HP_MARK_RUNS_N);
    for (int k = 0; k < RD_HP_MARK_RUNS_N; k++) {   /* the runs rise, apart, within 1..255 */
        TEST_ASSERT_TRUE(RD_HP_MARK_RUNS[k].i0 >= 1 && RD_HP_MARK_RUNS[k].i1 >= RD_HP_MARK_RUNS[k].i0);
        if (k) TEST_ASSERT_TRUE(RD_HP_MARK_RUNS[k].i0 > RD_HP_MARK_RUNS[k - 1].i1);
    }
    /* the brief's layers: the face's own assets within the cap (500 000 B) */
    size_t n0 = 0, n1 = 0, n2 = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_HP_PLATE, &n0));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_HP_MARKS, &n1));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_HP_PATCH, &n2));
    TEST_ASSERT_TRUE(n0 + n1 + n2 <= 500000u);
    TEST_ASSERT_TRUE(RD_FACE_HULL.ctx_bytes <= 1500000u - 434000u);
}

/* the tick: at s.000 the seconds hand shows the second before; it passes s within the first 110 ms (its overshoot,
 * at least a tenth of a step past), and from s.220 it shows s exactly; under reduced motion it shows s at once */
static void test_rd_hp_tick_overshoot(void)
{
    const float step = HP_TAU / 60.f;
    static const int S[] = {0, 1, 29, 30, 59};
    for (unsigned k = 0; k < sizeof S / sizeof S[0]; k++) {
        const int s = S[k];
        fk_face_in_t a = hp_at(10, 9, s, 0);
        const float prev = hp_norm((float)(s - 1) * step), at = hp_norm((float)s * step);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, prev, hp_norm(rd_hp_sec_angle(&a)));
        float peak = -1.f;
        int peak_ms = -1, past_ms = -1;
        for (int ms = 0; ms < 220; ms += 2) {
            a.clk.ms = (uint16_t)ms;
            const float d = hp_norm(rd_hp_sec_angle(&a) - prev);   /* the way from the second before */
            if (d > step && past_ms < 0) past_ms = ms;
            if (d > peak) peak = d, peak_ms = ms;
        }
        char m[96];
        snprintf(m, sizeof m, "second %d: past s at %d ms, the peak %.3f steps at %d ms", s, past_ms,
                 (double)(peak / step), peak_ms);
        TEST_ASSERT_TRUE_MESSAGE(past_ms >= 0 && past_ms <= 110, m);
        TEST_ASSERT_TRUE_MESSAGE(peak >= 1.1f * step && peak_ms <= 110, m);
        a.clk.ms = 110;   /* about s.110 the overshoot is over: within 3 % of a step of s */
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.03f * step, step, hp_norm(rd_hp_sec_angle(&a) - prev), m);
        for (int ms = 220; ms < 1000; ms += 13) {
            a.clk.ms = (uint16_t)ms;
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, at, hp_norm(rd_hp_sec_angle(&a)));
        }
        a.reduced = true;
        for (int ms = 0; ms < 1000; ms += 7) {
            a.clk.ms = (uint16_t)ms;
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, at, hp_norm(rd_hp_sec_angle(&a)));
        }
    }
}

/* within a minute: between ticks (s.300 to s.999) every dirty rect lies inside RD_HP_RUNLIGHT or RD_HP_HAZARD; during
 * a tick they also include the old and new seconds hand's rects and nothing else */
static void test_rd_hp_rest_dirty(void)
{
    const row_t *r = row_of("hull");
    bool light = false, haz = false, sec = false;
    for (int st = 0; st < 3; st++)
        for (int ms = 50; ms + 50 < 60000; ms += 50) {
            fk_face_in_t a = hp_at(10, 9, ms / 1000, ms % 1000);
            a.h24 = st != 1, a.clk.stale = st == 2;
            fk_face_in_t b = ran(shifted(a, 50), 50);
            omni_rect_t rects[8], sa[2], sb[2];
            rd_hand_hull_sec_rects(rd_hp_sec_angle(&a), sa);
            rd_hand_hull_sec_rects(rd_hp_sec_angle(&b), sb);
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            const int bms = b.clk.ms;
            for (unsigned k = 0; k < n; k++) {
                const bool l = rect_in(&rects[k], &RD_HP_RUNLIGHT), h = rect_in(&rects[k], &RD_HP_HAZARD);
                bool s = false;
                for (int q = 0; q < 2; q++)
                    s |= memcmp(&rects[k], &sa[q], sizeof sa[q]) == 0 || memcmp(&rects[k], &sb[q], sizeof sb[q]) == 0;
                char m[112];
                snprintf(m, sizeof m, "10:09:%02d.%03d + 50 ms: a dirty rect (%d, %d)-(%d, %d) outside its regions",
                         ms / 1000, ms % 1000, rects[k].x0, rects[k].y0, rects[k].x1, rects[k].y1);
                if (bms >= 300 || (b.clk.ms > a.clk.ms && a.clk.ms >= 250))
                    TEST_ASSERT_TRUE_MESSAGE(l || h, m);   /* between ticks */
                else
                    TEST_ASSERT_TRUE_MESSAGE(l || h || s, m);
                light |= l, haz |= h, sec |= s && !l && !h;
            }
            if (ms % 350 == 0 || ms % 1000 < 250) dirty_pair(r, &a, &b, "rest", NULL);
        }
    TEST_ASSERT_TRUE_MESSAGE(light && haz && sec, "the light, the arc and the seconds hand move at rest");
    /* the tick redraws the old and the new hand: both of their rects */
    fk_face_in_t a = hp_at(10, 9, 30, 950), b = ran(shifted(a, 100), 100);
    omni_rect_t rects[8], sa[2], sb[2];
    rd_hand_hull_sec_rects(rd_hp_sec_angle(&a), sa);
    rd_hand_hull_sec_rects(rd_hp_sec_angle(&b), sb);
    const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
    for (int q = 0; q < 2; q++) {
        bool fa = false, fb = false;
        for (unsigned k = 0; k < n; k++)
            fa |= memcmp(&rects[k], &sa[q], sizeof sa[q]) == 0, fb |= memcmp(&rects[k], &sb[q], sizeof sb[q]) == 0;
        TEST_ASSERT_TRUE_MESSAGE(fa && fb, "a tick misses a seconds hand's rect");
    }
    /* under reduced motion, across whole seconds only the seconds hand steps: the light and the arc stand */
    for (int sec = 0; sec < 60; sec += 7) {
        fk_face_in_t x = hp_at(10, 9, sec, 500);
        x.reduced = true;
        fk_face_in_t y = ran(shifted(x, 1000), 1000);
        omni_rect_t sx[2], sy[2], box[4];
        rd_hand_hull_sec_rects(rd_hp_sec_angle(&x), sx);
        rd_hand_hull_sec_rects(rd_hp_sec_angle(&y), sy);
        box[0] = sx[0], box[1] = sx[1], box[2] = sy[0], box[3] = sy[1];
        frame(r, &x, s_a);
        frame(r, &y, s_b);
        for (int py = 0; py < W; py++)
            for (int px = 0; px < W; px++)
                if (s_a[py * W + px] != s_b[py * W + px] && !in_any(box, 4, px, py))
                    TEST_FAIL_MESSAGE("reduced: a pixel off the seconds hand changes with the second");
    }
    /* under reduced motion: between whole seconds nothing moves */
    for (int ms = 0; ms + 50 < 1000; ms += 50) {
        fk_face_in_t x = hp_at(10, 9, 21, ms);
        x.reduced = true;
        fk_face_in_t y = ran(shifted(x, 50), 50);
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &x, &y, rects));
    }
}

/* lesson 9: the hour and minute hands step once a minute: their boxes are equal at :00 and :59 */
static void test_rd_hp_hands_per_minute(void)
{
    const row_t *r = row_of("hull");
    static const int HM[][2] = {{10, 9}, {0, 0}, {3, 47}, {11, 59}, {23, 30}, {6, 0}};
    for (unsigned k = 0; k < sizeof HM / sizeof HM[0]; k++) {
        fk_face_in_t a = hp_at(HM[k][0], HM[k][1], 0, 500), b = hp_at(HM[k][0], HM[k][1], 59, 500);
        float ha, ma, hb, mb;
        rd_hp_hand_angles(&a, &ha, &ma);
        rd_hp_hand_angles(&b, &hb, &mb);
        TEST_ASSERT_EQUAL_FLOAT(ha, hb);
        TEST_ASSERT_EQUAL_FLOAT(ma, mb);
        for (uint8_t w = 0; w < 2; w++) {
            omni_rect_t x, y;
            rd_hand_metal_bbox(w ? ma : ha, w, &x);
            rd_hand_metal_bbox(w ? mb : hb, w, &y);
            TEST_ASSERT_EQUAL_MEMORY(&x, &y, sizeof x);
        }
        /* the angles: (h mod 12 + m / 60) / 12 and m / 60 of a turn */
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, hp_norm(((float)(HM[k][0] % 12) + (float)HM[k][1] / 60.f) / 12.f * HP_TAU),
                                 hp_norm(ha));
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, hp_norm((float)HM[k][1] / 60.f * HP_TAU), hp_norm(ma));
        /* the frames: only the seconds hand, the running light and the arc differ in the minute */
        frame(r, &a, s_a);
        frame(r, &b, s_b);
        omni_rect_t sa[2], sb[2], box[6];
        rd_hand_hull_sec_rects(rd_hp_sec_angle(&a), sa);
        rd_hand_hull_sec_rects(rd_hp_sec_angle(&b), sb);
        box[0] = sa[0], box[1] = sa[1], box[2] = sb[0], box[3] = sb[1], box[4] = RD_HP_RUNLIGHT, box[5] = RD_HP_HAZARD;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (s_a[y * W + x] != s_b[y * W + x] && !in_any(box, 6, x, y))
                    TEST_FAIL_MESSAGE("a pixel off the seconds hand changes within the minute");
    }
    /* the metal hand stays inside its box, at every angle; so does the seconds hand inside its two: a 1-degree sweep
     * (offset off the axes) and every angle the tick's overshoot passes through (the spring table's 12 values at
     * every fifth second) */
    static const float SPRING12[12] = {0.000000f, 0.645434f, 1.080718f, 1.170249f, 1.090908f, 1.009338f,
                                       0.979909f, 0.984954f, 0.996054f, 1.000979f, 1.000912f, 1.000000f};
    for (int i = 0; i < 360 + 12 * 12; i++) {
        const float ang = i < 360 ? (float)i * HP_TAU / 360.f + 0.0013f
                                  : ((float)((i - 360) / 12 * 5) - 1.f + SPRING12[(i - 360) % 12]) * HP_TAU / 60.f;
        for (uint8_t w = 0; w < 3; w++) {
            omni_rect_t b[2];
            memset(s_a, 0, sizeof s_a);
            unsigned nb = 1;
            if (w < 2) {
                rd_hand_metal(s_a, W, NULL, ang, w, true);
                rd_hand_metal_bbox(ang, w, &b[0]);
            } else {
                rd_hand_hull_sec(s_a, W, NULL, ang);
                rd_hand_hull_sec_rects(ang, b);
                nb = 2;
            }
            long lit = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++) {
                    if (!s_a[y * W + x]) continue;
                    lit++;
                    if (!in_any(b, nb, x, y)) TEST_FAIL_MESSAGE("a hand's pixel outside its box");
                }
            TEST_ASSERT_TRUE(lit > 300);
        }
    }
}

/* three taps on the readout cycle date -> reserve -> frame and plate -> the date; the hit rect is >= 44 px, its edges
 * probed; the battery unknown shows --%; a tap while the readout is hidden (stale, unset) leaves it */
static void test_rd_hp_readout_cycle(void)
{
    const row_t *r = row_of("hull");
    fk_face_in_t a = hp_at(10, 8, 30, 500);
    a.battery = 76;
    char t[32];
    static const char *const WANT[4] = {"WED 04 NOV", "RESERVE 76%", "FR 0412 \267 PLATE 7", "WED 04 NOV"};
    rd_hp_readout(&a, t);
    TEST_ASSERT_EQUAL_STRING(WANT[0], t);
    frame(r, &a, s_c);
    uint16_t v = 0;
    for (int k = 1; k <= 3; k++) {
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &a, 233, 330, &v));
        fk_face_in_t b = a;
        b.variant = v;
        rd_hp_readout(&b, t);
        TEST_ASSERT_EQUAL_STRING(WANT[k], t);
        frame(r, &b, s_a);
        if (k < 3) TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_HP_READOUT, "a page") > 100);
        else assert_same(s_c, s_a, "three taps: the date again");
    }
    TEST_ASSERT_EQUAL_UINT16(0, v);
    a.battery = -1, a.variant = 1;
    rd_hp_readout(&a, t);
    TEST_ASSERT_EQUAL_STRING("RESERVE --%", t);
    a.battery = 100;
    rd_hp_readout(&a, t);
    TEST_ASSERT_EQUAL_STRING("RESERVE 100%", t);
    a.battery = 0;
    rd_hp_readout(&a, t);
    TEST_ASSERT_EQUAL_STRING("RESERVE 0%", t);
    fk_face_in_t p = a;   /* 12 h: the date page's AM/PM suffix */
    p.variant = 0, p.h24 = false, p.clk.h = 22;
    rd_hp_readout(&p, t);
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV \267 PM", t);
    p.variant = 2;   /* the other pages have none */
    rd_hp_readout(&p, t);
    TEST_ASSERT_EQUAL_STRING("FR 0412 \267 PLATE 7", t);
    fk_test_tap_edges(r->ops, r->ctx, RD_HP_TAP, FK_TAP_PULSE);
    static const int16_t OUT[][2] = {{95, 330}, {372, 330}, {233, 305}, {233, 352}, {233, 233}, {94, 94}};
    for (unsigned i = 0; i < sizeof OUT / sizeof OUT[0]; i++) {
        v = 1;
        TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap_var(r->ctx, &a, OUT[i][0], OUT[i][1], &v));
        TEST_ASSERT_EQUAL_UINT16(1, v);
    }
    for (int st = 0; st < 2; st++) {   /* hidden: stale (the marker), unset (the unset line) */
        fk_face_in_t h = a;
        h.clk.stale = st == 0, h.clk.set = st != 1;
        v = 2;
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &h, 233, 330, &v));
        TEST_ASSERT_EQUAL_UINT16(2, v);
        rd_hp_readout(&h, t);
        TEST_ASSERT_EQUAL_STRING(st ? "AWAITING TIME SIGNAL" : "", t);
    }
}

/* the wake: the hands start at 12 and swing in (with an overshoot past their place), a glint runs round the rivet ring;
 * from 950 ms the frame is settled; reduced motion never wakes (fk_face) and a settled input is the still */
static void test_rd_hp_wake(void)
{
    const row_t *r = row_of("hull");
    fk_face_in_t a = hp_at(4, 40, 30, 500), b = a;
    frame(r, &a, s_c);
    b.wake_ms = 0;   /* every hand at 12 */
    frame(r, &b, s_a);
    fk_face_in_t z = hp_at(0, 0, 0, 500);   /* 00:00:00: every hand at 12 at rest too (the seconds hand at :00) */
    frame(r, &z, s_b);
    long d = 0;
    for (int i = 0; i < W * W; i++) d += s_a[i] != s_b[i];
    TEST_ASSERT_TRUE_MESSAGE(d < 4000, "the wake's first frame is not every hand at 12");   /* the arc and the light */
    TEST_ASSERT_TRUE(frames_differ(s_a, s_c) > 3000);
    /* the glint: half way it lights rivets near 6 o'clock (the ring's (233, 449)), none by 950 */
    b.wake_ms = 580;
    frame(r, &b, s_a);
    fk_face_in_t sw = b;
    long glint = 0;
    for (int y = 430; y < 466; y++)
        for (int x = 200; x < 266; x++) glint += s_a[y * W + x] != s_c[y * W + x];
    TEST_ASSERT_TRUE_MESSAGE(glint > 40, "no glint at the ring's foot half way through the wake");
    (void)sw;
    for (uint32_t t = 950; t < 1000; t += 49) {
        b.wake_ms = t;
        frame(r, &b, s_a);
        assert_same(s_c, s_a, "the wake's last frame is settled");
    }
    /* the swing overshoots: some frame shows the minute hand past its place (the spring above 1) */
    float ha, ma;
    rd_hp_hand_angles(&a, &ha, &ma);
    omni_rect_t mb;
    rd_hand_metal_bbox(ma, 1, &mb);
    bool differs = false;
    for (uint32_t t = 300; t < 600; t += 50) {
        b.wake_ms = t;
        frame(r, &b, s_a);
        differs |= differ_in(s_c, s_a, &mb) > 0;
    }
    TEST_ASSERT_TRUE(differs);
    /* reduced: the input's wake_ms is never set by fk_face; a settled reduced frame equals the still's own */
    fk_face_in_t rr = a;
    rr.reduced = true;
    frame(r, &rr, s_a);
    TEST_ASSERT_TRUE(frames_differ(s_c, s_a) > 0);   /* the light steady at .6, the arc standing */
}

/* unset: no hands, only the bolt, --:-- above it, AWAITING TIME SIGNAL, the light breathing amber; stale: only the
 * readout's place changes; the glyphs inside their element's rects */
static void test_rd_hp_states(void)
{
    const row_t *r = row_of("hull");
    rd_still_frame(r->ops, r->ctx, 7, s_a);
    rd_still_t st;
    rd_still(7, &st);
    fk_face_in_t u = st.in;
    u.clk.s = 41, u.clk.ms = 300, u.variant = 2;   /* no seconds hand, no page */
    frame(r, &u, s_b);
    assert_same(s_a, s_b, "unset at another second and page");
    u.mono_ms = 450;   /* a quarter of the 1.8 s breath: the light at its brightest, and nothing else */
    frame(r, &u, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_HP_RUNLIGHT, "the breath") > 0);
    u.reduced = true, u.mono_ms = 0;   /* reduced: still */
    frame(r, &u, s_a);
    u.mono_ms = 450;
    frame(r, &u, s_b);
    assert_same(s_a, s_b, "reduced: the breath stands");
    /* no hand: along the 9 o'clock line, from the bolt out, the unset frame is the base (no steel) */
    fk_face_in_t a = hp_at(9, 15, 45, 500), b = a;   /* every hand on the 9 o'clock line (3 for the minute) */
    b.clk.stale = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_HP_READOUT, "stale") > 200);
    fk_face_in_t g = a;   /* the glance's marker: only in its rect */
    g.glance = true;
    frame(r, &g, s_a);
    g.clk.stale = true;
    frame(r, &g, s_b);
    differ_inside(s_a, s_b, &RD_HP_STALE, "the stale glance");
    fk_face_in_t un = a;
    un.clk.set = false;
    frame(r, &a, s_a);
    frame(r, &un, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_HP_UNSET) > 100);   /* the dashes */
    long hand = 0;   /* the hour hand's middle, out at 9 o'clock: set it is steel, unset the base */
    for (int x = 233 - 90; x < 233 - 40; x++) hand += s_a[233 * W + x] != s_b[233 * W + x];
    TEST_ASSERT_TRUE_MESSAGE(hand > 40, "unset still shows a hand");
    for (int v = 0; v < 3; v++)
        for (int k = 0; k < 4; k++)
            for (int hm = 0; hm < 2; hm++) {
                fk_face_in_t in = hp_at(k == 3 ? 22 : 10, 8, 30, 500);
                in.variant = (uint16_t)v, in.clk.stale = k == 1, in.clk.set = k != 2, in.h24 = hm == 0;
                in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3, in.battery = 100;
                s_ngl = 0;
                fk_test_glyph_hook = record;
                frame(r, &in, s_a);
                fk_test_glyph_hook = NULL;
                TEST_ASSERT_TRUE(s_ngl <= MAX_GLYPHS);
                for (unsigned q = 0; q < s_ngl; q++) {
                    const glyph_t *gl = &s_gl[q];
                    const omni_rect_t bx = {(int16_t)gl->x0, (int16_t)gl->y0, (int16_t)gl->x1, (int16_t)gl->y1};
                    char m[128];
                    snprintf(m, sizeof m, "page %d state %d: a glyph box (%d, %d)-(%d, %d) outside the readout", v, k,
                             gl->x0, gl->y0, gl->x1, gl->y1);
                    TEST_ASSERT_TRUE_MESSAGE(rect_in(&bx, &RD_HP_READOUT), m);
                }
            }
}

/* the chime: the pool swells with the thrum (the plate brighter, inside RD_HP_POOL only) and the light blinks on with
 * the notes; nothing under reduced motion */
static void test_rd_hp_chime(void)
{
    const row_t *r = row_of("hull");
    fk_face_in_t a = hp_at(10, 9, 21, 600), b = a;   /* the light off (s.600) */
    frame(r, &a, s_c);
    b.thrum = 0.7f;
    frame(r, &b, s_a);
    TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_HP_POOL, "the swell") > 5000);
    long brighter = 0, darker = 0;
    for (int y = RD_HP_POOL.y0; y < RD_HP_POOL.y1; y++)
        for (int x = RD_HP_POOL.x0; x < RD_HP_POOL.x1; x++) {
            const uint16_t p = s_c[y * W + x], q = s_a[y * W + x];
            brighter += (q >> 11) > (p >> 11), darker += (q >> 11) < (p >> 11);
        }
    TEST_ASSERT_TRUE(brighter > 5000 && darker == 0);
    b = a, b.beep = 1.f;
    frame(r, &b, s_a);
    TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_HP_RUNLIGHT, "the note's blink") > 20);
    fk_face_in_t on = hp_at(10, 9, 21, 100);   /* the blink's on is the light's own once-a-second on */
    frame(r, &on, s_b);
    for (int y = RD_HP_RUNLIGHT.y0; y < RD_HP_RUNLIGHT.y1; y++)
        for (int x = RD_HP_RUNLIGHT.x0; x < RD_HP_RUNLIGHT.x1; x++)
            TEST_ASSERT_EQUAL_HEX16(s_b[y * W + x], s_a[y * W + x]);
    b.reduced = true, b.thrum = 0.7f;
    a.reduced = true;
    frame(r, &a, s_c);
    frame(r, &b, s_a);
    assert_same(s_c, s_a, "reduced: no chime");
}

/* the glance: the rock's dim dot, every fourth rivet (12), the cardinal outlines, nothing past 110 */
static void test_rd_hp_glance(void)
{
    const row_t *r = row_of("hull");
    fk_face_in_t a = hp_at(10, 9, 30, 500);
    a.glance = true;
    frame(r, &a, s_a);
    const uint16_t rock = s_a[305 * W + 367];   /* the strike (233 + 152 cos .4936, 233 + 152 sin .4936) */
    TEST_ASSERT_EQUAL_HEX16(RD_G_STEEL, rock);
    unsigned rivets = 0;
    for (int i = 0; i < 48; i++) {
        float sn, cs;
        fk_sincos((float)i * HP_TAU / 48.f, &sn, &cs);
        const int x = (int)(233.f + sn * 216.f + 0.5f), y = (int)(233.f - cs * 216.f + 0.5f);
        long n = 0;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) n += s_a[(y + dy) * W + x + dx] != 0;
        if (i % 4 == 0) TEST_ASSERT_TRUE_MESSAGE(n >= 6, "a fourth rivet is missing");
        else TEST_ASSERT_EQUAL_INT32_MESSAGE(0, n, "a rivet between the fourths");
        rivets += n > 0;
    }
    TEST_ASSERT_EQUAL_UINT(12, rivets);
    float pct;
    uint8_t maxc;
    fk_test_lit_th(s_a, 8, &pct, &maxc);
    TEST_ASSERT_TRUE(pct > 1.f && pct < 5.f && maxc <= 110);
    printf("hull glance 10:09: %.2f %% lit, max channel %u\n", (double)pct, maxc);
}

/* ---- Bunk Clock (spec section 10.12) */
/* pixels that differ outside every rect of r[0 .. n) */
static long differ_outside(const uint16_t *a, const uint16_t *b, const omni_rect_t *r, unsigned n)
{
    long d = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (a[y * W + x] != b[y * W + x] && !in_any(r, n, x, y)) d++;
    return d;
}

static bool meets_rect(const omni_rect_t *a, const omni_rect_t *b)
{
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

static fk_face_in_t bk_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3, in.clk.yday = 308;   /* WED 04 NOV 2026 */
    return in;
}

static void test_rd_bk_face(void)
{
    TEST_ASSERT_EQUAL_STRING("bunk", RD_FACE_BUNK.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_BUNK.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(1000, RD_FACE_BUNK.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(1500, RD_FACE_BUNK.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_BUNK.stills_extra);
    /* draw() composes its leaves into the context (bunk.c's header): a canvas face only */
    TEST_ASSERT_FALSE(RD_FACE_BUNK.reveal);
    TEST_ASSERT_FALSE(RD_FACE_BUNK.direct);
    TEST_ASSERT_NULL(RD_FACE_BUNK.reveal_ops);
    TEST_ASSERT_NOT_NULL(RD_FACE_BUNK.tap_var);
    TEST_ASSERT_NOT_NULL(RD_FACE_BUNK.tap);
    TEST_ASSERT_EQUAL_HEX16(0x8001, RD_BK_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_BK_TAPPED_VARIANT, rd_still_variant(&RD_FACE_BUNK));
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_BUNK, rd_face_by_key("bunk"));
    /* the rects */
    TEST_ASSERT_TRUE(rect_in(&RD_BK_LENS, &RD_BK_LAMP) && rect_in(&RD_BK_LAMP, &RD_BK_ROOM));
    TEST_ASSERT_TRUE(rect_in(&RD_BK_LABEL, &RD_BK_ROOM) && rect_in(&RD_BK_STALE, &RD_BK_LABEL));
    TEST_ASSERT_TRUE(RD_BK_LENS.x1 - RD_BK_LENS.x0 == 32 && RD_BK_LENS.y1 - RD_BK_LENS.y0 == 32);
    TEST_ASSERT_TRUE(RD_BK_FLIP_M.x0 == 239 && RD_BK_FLIP_M.y0 == 173 && RD_BK_FLIP_M.x1 == 239 + 174 &&
                     RD_BK_FLIP_M.y1 == 173 + 150);
    TEST_ASSERT_TRUE(RD_BK_FLIP_H.x0 == 53 && RD_BK_FLIP_H.x1 == 53 + 174 && !meets_rect(&RD_BK_FLIP_H, &RD_BK_FLIP_M));
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(2, rd_bk_chime_rects(c));
    TEST_ASSERT_TRUE(memcmp(&c[0], &RD_BK_LAMP, sizeof c[0]) == 0 && memcmp(&c[1], &RD_BK_FLIP_M, sizeof c[1]) == 0);
    /* still 11 (paused, one snooze) differs from still 0 only in the roommate and the count */
    const row_t *r = row_of("bunk");
    rd_still_frame(&RD_FACE_BUNK, r->ctx, 0, s_a);
    rd_still_frame(&RD_FACE_BUNK, r->ctx, RD_STILL_N, s_b);
    const omni_rect_t tapped[2] = {RD_BK_ROOM, RD_BK_COUNT};
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, tapped, 2));
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_ROOM) > 300 && differ_in(s_a, s_b, &RD_BK_COUNT) > 20);
    /* the glow table: the quarters profile, const data */
    static uint8_t lut[FK_GLOW_LUT_MAX];
    static const int QR[] = RD_QTR_LUT_RADII;
    for (unsigned i = 0; i < sizeof QR / sizeof QR[0]; i++) {
        rd_qtr_lut_build(lut, QR[i]);
        TEST_ASSERT_NOT_NULL(rd_glow_lut(QR[i]));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(lut, rd_glow_lut(QR[i]), FK_GLOW_LUT_N(QR[i]));
    }
    /* the face's own files (rd_bk_*): within the 500 000 B cap */
    size_t n0 = 0, n1 = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_BK_CASE, &n0));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_BK_SHELF, &n1));
    TEST_ASSERT_TRUE(n0 + n1 <= 500000u);
}

/* PR24: the context holds the two composed RGB565 leaves (2 x 174 x 150 x 2 B) and stays within the Budgets row */
static void test_rd_bk_ctx_bytes(void)
{
    TEST_ASSERT_TRUE(RD_FACE_BUNK.ctx_bytes >= 2u * 174u * 150u * 2u);
    TEST_ASSERT_TRUE(RD_FACE_BUNK.ctx_bytes <= 1572864u - 434312u);
    printf("bunk ctx_bytes %u\n", (unsigned)RD_FACE_BUNK.ctx_bytes);
}

/* days from 1970-01-01 to y-m-d (the proleptic Gregorian calendar) */
static int64_t bk_days(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* the count from the clock: 1 095 000 000 + the day of the year + the snooze taps; 0 unset */
static void test_rd_bk_count_days(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    static const struct { int y, m, d; uint32_t want; } D[] = {
        {2026, 1, 1, 1095000001u}, {2026, 12, 31, 1095000365u}, {2028, 12, 31, 1095000366u}, {2026, 11, 4, 1095000308u},
        {2028, 3, 1, 1095000061u}};
    for (unsigned i = 0; i < sizeof D / sizeof D[0]; i++) {
        fk_clock_t c;
        fk_clock_from(bk_days(D[i].y, D[i].m, D[i].d) * 86400 + 12 * 3600, 0, false, &c);
        TEST_ASSERT_TRUE(c.set);
        TEST_ASSERT_EQUAL_UINT32(D[i].want, rd_bk_count(&c, 0));
        TEST_ASSERT_EQUAL_UINT32(D[i].want + 3u, rd_bk_count(&c, 3));
        TEST_ASSERT_EQUAL_UINT32(D[i].want + 3u, rd_bk_count(&c, 0xC003));   /* the pause and the lamp bit: no count */
        TEST_ASSERT_EQUAL_UINT32(D[i].want + 16383u, rd_bk_count(&c, 0x3FFF));
    }
    fk_clock_t u;
    fk_clock_from(0, 0, false, &u);
    TEST_ASSERT_FALSE(u.set);
    TEST_ASSERT_EQUAL_UINT32(0, rd_bk_count(&u, 0));
    TEST_ASSERT_EQUAL_UINT32(0, rd_bk_count(&u, 9));
    char t[32];
    rd_bk_count_str(1095000308u, t);
    TEST_ASSERT_EQUAL_STRING("SNOOZED 1 095 000 308 TIMES", t);
    rd_bk_count_str(1095016749u, t);
    TEST_ASSERT_EQUAL_STRING("SNOOZED 1 095 016 749 TIMES", t);
    rd_bk_count_str(0, t);
    TEST_ASSERT_EQUAL_STRING("SNOOZED ? TIMES", t);
    /* drawn: the count line of 4 Nov with 3 snoozes is the one of the count + 3 (the pixels of its text) */
    const row_t *r = row_of("bunk");
    fk_face_in_t a = bk_at(10, 42, 3, 0), b = a;
    b.variant = 3;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_COUNT) > 20);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_BK_COUNT, 1));
    b.clk.yday = 311, b.variant = 0;   /* three days on and no snoozes: the same count, the same line */
    frame(r, &b, s_c);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_b, s_c, &RD_BK_COUNT));
}

/* lesson 5: the two hit boxes, at least 44 px and apart, probed at every edge; what each tap does to the variant */
static void test_rd_bk_hit_boxes(void)
{
    const row_t *r = row_of("bunk");
    TEST_ASSERT_TRUE(RD_BK_HIT_BAR.x1 - RD_BK_HIT_BAR.x0 == 210 && RD_BK_HIT_BAR.y1 - RD_BK_HIT_BAR.y0 == 48);
    TEST_ASSERT_TRUE(RD_BK_HIT_LAMP.x1 - RD_BK_HIT_LAMP.x0 == 48 && RD_BK_HIT_LAMP.y1 - RD_BK_HIT_LAMP.y0 == 48);
    TEST_ASSERT_FALSE(meets_rect(&RD_BK_HIT_BAR, &RD_BK_HIT_LAMP));
    TEST_ASSERT_TRUE(memcmp(&RD_BK_HIT_BAR, &BK_HIT_BAR, sizeof BK_HIT_BAR) == 0);
    TEST_ASSERT_TRUE(memcmp(&RD_BK_HIT_LAMP, &BK_HIT_LAMP, sizeof BK_HIT_LAMP) == 0);
    fk_test_tap_edges(r->ops, r->ctx, RD_BK_HIT_BAR, FK_TAP_PULSE);
    fk_test_tap_edges(r->ops, r->ctx, RD_BK_HIT_LAMP, FK_TAP_PULSE);
    /* the lamp's box lies about the lamp, the bar's about the bar */
    TEST_ASSERT_TRUE(RD_BK_HIT_LAMP.x0 + RD_BK_HIT_LAMP.x1 == 2 * 160 && RD_BK_HIT_LAMP.y0 + RD_BK_HIT_LAMP.y1 == 2 * 80);
    TEST_ASSERT_TRUE(RD_BK_HIT_BAR.x0 == 128 && RD_BK_HIT_BAR.y0 <= 116 && RD_BK_HIT_BAR.y1 >= 146);
    const fk_face_in_t in = bk_at(10, 42, 3, 0);
    static const struct { uint16_t v, bar, lamp; } V[] = {
        {0x0000, 0x0001, 0xC000}, {0x8003, 0x8004, 0x4003}, {0x4005, 0x0006, 0xC005}, {0xC005, 0x8006, 0x4005},
        {0x3FFF, 0x0000, 0xFFFF}, {0xFFFF, 0x8000, 0x7FFF}, {0x0002, 0x0003, 0xC002}};
    const int16_t bx = 233, by = 131, lx = 160, ly = 80;
    for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
        uint16_t v = V[i].v;
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, bx, by, &v));
        TEST_ASSERT_EQUAL_HEX16(V[i].bar, v);   /* bits 0-13 + 1 (wrapping), bit 14 cleared, bit 15 kept */
        v = V[i].v;
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, lx, ly, &v));
        TEST_ASSERT_EQUAL_HEX16(V[i].lamp, v);   /* bit 15 toggled, bit 14 set, the taps kept */
        v = V[i].v;
        TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 233, 300, &v));   /* the cards: nothing */
        TEST_ASSERT_EQUAL_HEX16(V[i].v, v);
    }
    /* the boxes' corners, inclusive */
    uint16_t v = 0;
    TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, RD_BK_HIT_BAR.x1 - 1, RD_BK_HIT_BAR.y1 - 1, &v));
    TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, RD_BK_HIT_LAMP.x0, RD_BK_HIT_LAMP.y0, &v));
    TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, RD_BK_HIT_LAMP.x1, RD_BK_HIT_LAMP.y0, &v));
    TEST_ASSERT_EQUAL_HEX16(0xC001, v);
}

/* unset: the bar is inert (FK_TAP_NONE, the variant left), the count reads SNOOZED ? TIMES whatever the taps; the lamp
 * still pauses */
static void test_rd_bk_unset_bar_inert(void)
{
    const row_t *r = row_of("bunk");
    fk_face_in_t in = bk_at(10, 42, 3, 0);
    in.clk.set = false;
    static const uint16_t V[] = {0, 1, 0x4007, 0x8001, 0x3FFF};
    for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
        uint16_t v = V[i];
        TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 233, 131, &v));
        TEST_ASSERT_EQUAL_HEX16(V[i], v);
        TEST_ASSERT_EQUAL(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 128, 107, &v));
        TEST_ASSERT_EQUAL_HEX16(V[i], v);
        TEST_ASSERT_EQUAL(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, 160, 80, &v));
        TEST_ASSERT_EQUAL_HEX16((uint16_t)((V[i] ^ 0x8000) | 0x4000), v);
    }
    fk_face_in_t a = in, b = in;
    b.variant = 12;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));   /* the taps show nowhere while unset */
    b.pulsing = true, b.pulse_ms = 50;   /* nor does a snooze's pulse: no press, no message */
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* the minute card flips m:00.000 to m:00.449 from the clock, inside RD_BK_FLIP_M (the lamp's own motion aside), and
 * shows settled from m:00.450 */
static void test_rd_bk_flip_window(void)
{
    const row_t *r = row_of("bunk");
    const fk_face_in_t settled = bk_at(10, 43, 30, 0);
    frame(r, &settled, s_c);
    const omni_rect_t ok[2] = {RD_BK_FLIP_M, RD_BK_LAMP};
    long moved = 0;
    for (int ms = 0; ms < 450; ms += 10) {
        const fk_face_in_t in = bk_at(10, 43, 0, ms);
        frame(r, &in, s_a);
        char m[96];
        snprintf(m, sizeof m, "10:43:00.%03d: the flip outside the minute card", ms);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(0, differ_outside(s_a, s_c, ok, 2), m);
        const long d = differ_in(s_a, s_c, &RD_BK_FLIP_M);
        if (ms <= 400) {
            snprintf(m, sizeof m, "10:43:00.%03d: the minute card does not flip", ms);
            TEST_ASSERT_TRUE_MESSAGE(d > 200, m);
        }
        moved += d;
    }
    TEST_ASSERT_TRUE(moved > 100000);
    for (int ms = 450; ms < 1000; ms += 50) {
        const fk_face_in_t in = bk_at(10, 43, 0, ms);
        frame(r, &in, s_a);
        TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_c, &RD_BK_FLIP_M));
        TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_c, &RD_BK_LAMP, 1));
    }
    /* at the hour the hour card flips too, in its own box; under reduced motion nothing flips */
    const fk_face_in_t hs = bk_at(11, 0, 30, 0);
    frame(r, &hs, s_c);
    fk_face_in_t in = bk_at(11, 0, 0, 100);
    frame(r, &in, s_a);
    const omni_rect_t ok2[3] = {RD_BK_FLIP_M, RD_BK_FLIP_H, RD_BK_LAMP};
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_c, ok2, 3));
    TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_BK_FLIP_H) > 200);
    in.reduced = true;
    fk_face_in_t rs = hs;
    rs.reduced = true;
    frame(r, &in, s_a);
    frame(r, &rs, s_c);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_c));
    /* 12 h: 12:59 -> 1:00 flips the hour card's 12 to its 1 */
    in = bk_at(13, 0, 0, 100), in.h24 = false;
    fk_face_in_t h12 = bk_at(13, 0, 30, 0);
    h12.h24 = false;
    frame(r, &in, s_a);
    frame(r, &h12, s_c);
    TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_BK_FLIP_H) > 200);
}

/* the wake: the hours flip in from -- over 0-450 ms, the minutes over 250-700 ms, the lamp stutters on over 400 ms;
 * settled from 700 ms; under reduced motion no wake runs (fk_face's rule), so the settled frame shows */
static void test_rd_bk_wake(void)
{
    const row_t *r = row_of("bunk");
    fk_face_in_t in = bk_at(10, 42, 3, 0);
    frame(r, &in, s_c);
    const omni_rect_t ok[3] = {RD_BK_FLIP_H, RD_BK_FLIP_M, RD_BK_LAMP};
    static const uint32_t T[] = {0, 150, 300, 450, 600, 699};
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        in.wake_ms = T[i];
        frame(r, &in, s_a);
        TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_c, ok, 3));
        if (T[i] < 450) TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_BK_FLIP_H) > 200);
        else TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_c, &RD_BK_FLIP_H));
        if (T[i] <= 600) TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_BK_FLIP_M) > 200);
        if (T[i] < 400) TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_BK_LAMP) > 50);
        else TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_c, &RD_BK_LAMP));
    }
    in.wake_ms = 0;   /* at its start both cards show the dim dashes, the lamp dark */
    frame(r, &in, s_a);
    fk_face_in_t u = bk_at(10, 42, 3, 0);
    u.clk.set = false;
    frame(r, &u, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_BK_FLIP_M));
    for (uint32_t t = 700; t < 1000; t += 40) {
        in.wake_ms = t;
        frame(r, &in, s_a);
        TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_c));
    }
}

/* a snooze: the bar goes down for 200 ms; [ REQUEST LOGGED ] for the pulse's 1.5 s, its first 120 ms at .5 over the old
 * count at .3; then the new count. Reduced: the message at once, no linger */
static void test_rd_bk_snooze(void)
{
    const row_t *r = row_of("bunk");
    fk_face_in_t in = bk_at(10, 42, 3, 0);
    in.variant = 1;
    frame(r, &in, s_c);   /* after it: the count + 1 */
    fk_face_in_t p = in;
    p.pulsing = true;
    const omni_rect_t ok[2] = {RD_BK_BAR, RD_BK_COUNT};
    static const uint16_t T[] = {0, 60, 119, 120, 199, 200, 800, 1499};
    long bar[8], cnt[8];
    for (unsigned i = 0; i < 8; i++) {
        p.pulse_ms = T[i];
        frame(r, &p, s_a);
        TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_c, ok, 2));
        bar[i] = differ_in(s_a, s_c, &RD_BK_BAR), cnt[i] = differ_in(s_a, s_c, &RD_BK_COUNT);
        TEST_ASSERT_TRUE(cnt[i] > 100);
    }
    TEST_ASSERT_TRUE(bar[0] > 200 && bar[4] > 200 && bar[5] == 0 && bar[6] == 0);
    p.pulse_ms = 60;   /* the linger: the message's first 120 ms differ from its rest */
    frame(r, &p, s_a);
    p.pulse_ms = 800;
    frame(r, &p, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_COUNT) > 50);
    p.pulse_ms = 130;
    frame(r, &p, s_a);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_BK_COUNT));
    p.reduced = true, p.pulse_ms = 60;   /* reduced: the message at full, nothing lingering */
    frame(r, &p, s_a);
    p.pulse_ms = 800;
    frame(r, &p, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_BK_COUNT));
    /* a lamp pulse shows no message */
    fk_face_in_t l = in;
    l.variant = 0x4001, l.pulsing = true, l.pulse_ms = 800;
    frame(r, &l, s_a);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_c, &RD_BK_COUNT));
    /* midnight: the day's count lingers 120 ms at 30 % (the clock's), not on 1 January */
    fk_face_in_t m0 = bk_at(0, 0, 0, 60), m1 = bk_at(0, 0, 0, 130);
    frame(r, &m0, s_a);
    frame(r, &m1, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_COUNT) > 50);
    m0.clk.yday = m1.clk.yday = 1;
    frame(r, &m0, s_a);
    frame(r, &m1, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_BK_COUNT));
}

/* the roommate: paused, the glow cuts and the label changes, inside RD_BK_ROOM; unpaused by a tap, the lamp flickers
 * back over the pulse's first 400 ms; the scanlines roll every 60 ms inside RD_BK_LENS; every 7.3 s the lens tears for
 * 160 ms; under reduced motion it is steady; no letter badge: nothing lit inside the lens is paint or amber */
static void test_rd_bk_lamp(void)
{
    const row_t *r = row_of("bunk");
    fk_face_in_t a = bk_at(10, 42, 3, 0), b = a;
    b.variant = RD_BK_PAUSED;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_BK_ROOM, 1));
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_LAMP) > 800 && differ_in(s_a, s_b, &RD_BK_LABEL) > 200);
    long glow = 0, lit = 0;   /* paused: past the ring r 16 (and left of the label) the void, #060606; lit, the glow */
    for (int y = RD_BK_LAMP.y0; y < RD_BK_LAMP.y1; y++)
        for (int x = RD_BK_LAMP.x0; x < 182; x++) {
            const int dx = 2 * (x - 160) + 1, dy = 2 * (y - 80) + 1;
            if (dx * dx + dy * dy <= 4 * 17 * 17) continue;
            glow += s_b[y * W + x] != 0x0821;
            lit += s_a[y * W + x] != 0x0821;
        }
    TEST_ASSERT_EQUAL_INT32(0, glow);
    TEST_ASSERT_TRUE(lit > 300);
    /* the flicker back on */
    fk_face_in_t f = a;
    f.variant = RD_BK_LAMP_LAST, f.pulsing = true;
    for (uint16_t t = 0; t < 400; t += 100) {
        f.pulse_ms = t;
        frame(r, &f, s_b);
        TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_LAMP) > 50);
        TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_BK_LAMP, 1));
    }
    f.pulse_ms = 400;
    frame(r, &f, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
    /* the roll: 60 ms on, only the lens */
    fk_face_in_t g = bk_at(10, 42, 3, 60);
    frame(r, &g, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_LENS) > 20);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_BK_LENS, 1));
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &a, &g, rects));
    TEST_ASSERT_TRUE(memcmp(&rects[0], &RD_BK_LENS, sizeof rects[0]) == 0);
    /* the tear: at T mod 7300 < 160 the lamp's level and its scanlines' shift change */
    const uint32_t T0 = (10u * 3600u + 42u * 60u + 3u) * 1000u, z = T0 % 7300u, t0 = T0 - z + 7300u;   /* the next tear */
    fk_face_in_t q = bk_at((int)(t0 / 3600000u), (int)(t0 / 60000u % 60u), (int)(t0 / 1000u % 60u), (int)(t0 % 1000u));
    fk_face_in_t q2 = shifted(q, 180);   /* past it, the same roll */
    frame(r, &q, s_a);
    frame(r, &q2, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_LAMP) > 100);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_BK_LAMP, 1));
    q.reduced = q2.reduced = true;   /* reduced: steady */
    frame(r, &q, s_a);
    frame(r, &q2, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
    /* a glow and a word only: the lens holds no paint-, amber- or hull-coloured pixel (no letter badge) */
    frame(r, &a, s_a);
    for (int y = RD_BK_LENS.y0; y < RD_BK_LENS.y1; y++)
        for (int x = RD_BK_LENS.x0; x < RD_BK_LENS.x1; x++) {
            const uint16_t p = s_a[y * W + x];
            const int R = omni_ex_r(p), G = omni_ex_g(p);
            TEST_ASSERT_TRUE_MESSAGE(G >= R, "a non-phosphor pixel in the lamp's lens");
        }
}

/* unset and stale and 12 h: the cards' dim dashes, --- -- --- . BUNK 2 and SNOOZED ? TIMES; stale: the marker in the
 * label's place, nothing else; 12 h: a single hour digit centred on its card, AM/PM on the date line */
static void test_rd_bk_states(void)
{
    const row_t *r = row_of("bunk");
    fk_face_in_t a = bk_at(10, 42, 3, 0), b = a;
    b.clk.stale = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_BK_LABEL, "stale") > 200);
    b = a, b.clk.stale = true, b.variant = RD_BK_PAUSED;   /* stale and paused: the glow cut, the marker kept */
    frame(r, &b, s_b);
    a.variant = RD_BK_PAUSED;
    frame(r, &a, s_a);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_BK_LABEL, 1));
    /* the marker's amber ink lies in RD_BK_STALE */
    a.variant = 0, b.variant = 0;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    for (int y = RD_BK_LABEL.y0; y < RD_BK_LABEL.y1; y++)
        for (int x = RD_BK_LABEL.x0; x < RD_BK_LABEL.x1; x++)
            if (s_b[y * W + x] != s_a[y * W + x] && omni_ex_r(s_b[y * W + x]) > omni_ex_g(s_b[y * W + x]) + 40)
                TEST_ASSERT_TRUE_MESSAGE(inside(&RD_BK_STALE, x, y), "the stale marker outside RD_BK_STALE");
    /* unset */
    fk_face_in_t u = a;
    u.clk.set = false;
    frame(r, &u, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_FLIP_H) > 1000 && differ_in(s_a, s_b, &RD_BK_FLIP_M) > 1000);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BK_DATE) > 50 && differ_in(s_a, s_b, &RD_BK_COUNT) > 200);
    /* 12 h: 9:05 centres its 9 on the hour card (the ink's middle within 2 px of the card's), AM on the date line */
    fk_face_in_t h = bk_at(9, 5, 3, 0);
    h.h24 = false;
    frame(r, &h, s_b);
    long sx = 0, n = 0;
    for (int y = RD_BK_FLIP_H.y0 + 20; y < RD_BK_FLIP_H.y1 - 20; y++)
        for (int x = RD_BK_FLIP_H.x0 + 4; x < RD_BK_FLIP_H.x1 - 4; x++)
            if (omni_ex_r(s_b[y * W + x]) > 150) sx += x, n++;
    TEST_ASSERT_TRUE(n > 500);
    const long mid2 = 2 * sx / n, want2 = RD_BK_FLIP_H.x0 + RD_BK_FLIP_H.x1;
    TEST_ASSERT_TRUE_MESSAGE(mid2 >= want2 - 4 && mid2 <= want2 + 4, "the 12 h hour is not centred on its card");
    fk_face_in_t h24 = h;
    h24.h24 = true;
    frame(r, &h24, s_c);
    TEST_ASSERT_TRUE(differ_in(s_b, s_c, &RD_BK_DATE) > 50);   /* the suffix */
}

/* the chime: the lamp's glow swells with thrum (RD_BK_LAMP), the minute card twitches with beep (RD_BK_FLIP_M) */
static void test_rd_bk_chime(void)
{
    const row_t *r = row_of("bunk");
    fk_face_in_t a = bk_at(10, 42, 3, 0), b = a;
    frame(r, &a, s_a);
    b.thrum = 1.f;
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_BK_LAMP, "the swell") > 800);
    b = a, b.beep = 1.f;
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_BK_FLIP_M, "the twitch") > 1000);
    b.variant = RD_BK_PAUSED, b.beep = 0.f, b.thrum = 1.f;   /* paused: no swell */
    a.variant = RD_BK_PAUSED;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* the glance: the outlined digits, the split lines and the hollow lamp ring (its bar while projected), glance colours,
 * through the orbit; the worst case 20:08 */
static void test_rd_bk_glance(void)
{
    const row_t *r = row_of("bunk");
    fk_face_in_t g = bk_at(20, 8, 30, 500), p;
    g.glance = true;
    frame(r, &g, s_a);
    float pct;
    uint8_t maxc;
    fk_test_lit_th(s_a, 8, &pct, &maxc);
    TEST_ASSERT_TRUE(pct > 1.f && pct < 5.f && maxc <= 110);
    printf("bunk glance 20:08: %.2f %% lit, max channel %u\n", (double)pct, maxc);
    p = g, p.variant = RD_BK_PAUSED;   /* paused: the ring alone */
    frame(r, &p, s_b);
    const omni_rect_t ring = {144, 64, 177, 97};
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &ring, "the lamp's bar") > 10);
    p = g, p.odx = 4, p.ody = -4;
    frame(r, &p, s_b);
    long lit_a = 0, lit_b = 0;
    for (int i = 0; i < W * W; i++) lit_a += s_a[i] != 0, lit_b += s_b[i] != 0;
    TEST_ASSERT_TRUE(lit_a > 1000 && lit_a == lit_b);   /* the same ink, moved */
    TEST_ASSERT_TRUE(s_a[(173 + 75) * W + 120] != 0 && s_b[(173 + 75 - 4) * W + 124] != 0);   /* a split line */
}

/* ---- Curry Clock (Task 15) */
static fk_face_in_t cu_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3, in.clk.yday = 308;   /* WED 04 NOV 2026 */
    return in;
}
static void px8(uint16_t p, int *r, int *g, int *b)
{
    *r = ((p >> 11) & 31) * 255 / 31, *g = ((p >> 5) & 63) * 255 / 63, *b = (p & 31) * 255 / 31;
}

/* the ops' wiring: the brief's numbers, the rects, the ramp, the tapped still, the face's files */
static void test_rd_cu_face(void)
{
    TEST_ASSERT_EQUAL_STRING("curry", RD_FACE_CURRY.key);
    TEST_ASSERT_EQUAL_UINT16(50, RD_FACE_CURRY.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(1000, RD_FACE_CURRY.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(400, RD_FACE_CURRY.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_CURRY.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_CURRY.reveal);
    TEST_ASSERT_FALSE(RD_FACE_CURRY.direct);
    TEST_ASSERT_NULL(RD_FACE_CURRY.reveal_ops);
    TEST_ASSERT_NOT_NULL(RD_FACE_CURRY.tap_var);
    TEST_ASSERT_NOT_NULL(RD_FACE_CURRY.tap);
    TEST_ASSERT_EQUAL_UINT16(3, RD_CU_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_CU_TAPPED_VARIANT, rd_still_variant(&RD_FACE_CURRY));
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_CURRY, rd_face_by_key("curry"));
    /* the rects: the lid's box 336 square on the tray, the hinge on its top edge, the steam and the sticker inside the
     * disc, the stale marker inside the steam (it is drawn after it) and the hub inside the box */
    TEST_ASSERT_TRUE(RD_CU_LIDBOX.x0 == 65 && RD_CU_LIDBOX.y0 == 65 && RD_CU_LIDBOX.x1 == 401 && RD_CU_LIDBOX.y1 == 401);
    TEST_ASSERT_TRUE(RD_CU_LID_HINGE.axis == OMNI_HINGE_HORIZONTAL && RD_CU_LID_HINGE.pos == 0.f &&
                     RD_CU_LID_HINGE.focal == 560.f && RD_CU_LID_HINGE.n == 336 && RD_CU_LID_HINGE.dome_r == 0.f);
    TEST_ASSERT_TRUE(rect_in(&RD_CU_STALE, &RD_CU_STEAM) && rect_in(&RD_CU_HUB, &RD_CU_LIDBOX));
    TEST_ASSERT_TRUE(rect_in(&RD_CU_HUB, &RD_CU_STEAM));
    TEST_ASSERT_TRUE(memcmp(&RD_CU_HIT, &RD_CU_LIDBOX, sizeof RD_CU_HIT) == 0);
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(2, rd_cu_chime_rects(c));
    TEST_ASSERT_TRUE(memcmp(&c[0], &RD_CU_STEAM, sizeof c[0]) == 0 && memcmp(&c[1], &RD_CU_HUB, sizeof c[1]) == 0);
    ramp_row("rd_cu_lid", RD_CU_LID_RAMP, RD_CU_LID_RAMP_STOPS);
    /* still 11 (three chillies) differs from still 0 only on the sticker and in the steam (its red) */
    const row_t *r = row_of("curry");
    rd_still_frame(&RD_FACE_CURRY, r->ctx, 0, s_a);
    rd_still_frame(&RD_FACE_CURRY, r->ctx, RD_STILL_N, s_b);
    const omni_rect_t tapped[2] = {RD_CU_STICKER, RD_CU_STEAM};
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, tapped, 2));
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_CU_STICKER) > 100 && differ_in(s_a, s_b, &RD_CU_STEAM) > 1000);
    /* the face's own files (rd_cu_*): the brief's sizes, within the 500 000 B cap */
    size_t n0 = 0, n1 = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_CU_TRAY, &n0));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_CU_LID, &n1));
    TEST_ASSERT_TRUE(ASSET_RD_CU_TRAY_W * ASSET_RD_CU_TRAY_H * 2 == 225792 && ASSET_RD_CU_LID_W * ASSET_RD_CU_LID_H == 112896);
    TEST_ASSERT_TRUE(n0 + n1 <= 500000u);
    /* wave 2 Task 12: the steam's loop (ARW1, rd/faces.h), its rows and extents, and all four within the cap */
    size_t n3 = 0;
    const uint8_t *wisp = rd_assets_host_get(ASSET_RD_CU_WISP, &n3);
    TEST_ASSERT_NOT_NULL(wisp);
    TEST_ASSERT_TRUE(n3 > 8 + 2 * RD_CU_WISP_ROWS && memcmp(wisp, "ARW1", 4) == 0);
    TEST_ASSERT_TRUE(ASSET_RD_CU_WISP_W == RD_CU_WISP_HW && ASSET_RD_CU_WISP_H == RD_CU_WISP_ROWS);
    TEST_ASSERT_TRUE(n0 + n1 + n3 <= 500000u);
    printf("curry's files %u B (the loop %u B)\n", (unsigned)(n0 + n1 + n3), (unsigned)n3);
}

/* PR24: the context holds the composed lid leaf (336 x 336 RGB565) and stays within the Budgets row */
static void test_rd_cu_ctx_bytes(void)
{
    TEST_ASSERT_TRUE(RD_FACE_CURRY.ctx_bytes >= 225792u);
    TEST_ASSERT_TRUE(RD_FACE_CURRY.ctx_bytes <= 1572864u - 434312u);
    printf("curry ctx_bytes %u\n", (unsigned)RD_FACE_CURRY.ctx_bytes);
}

/* PR23: the biro is graphite. On still 7 (unset: the lid on, --:-- and READY SOON in biro) no pixel of the lid's box
 * has its blue above both its red and its green by more than RGB565's rounding (one 5-bit step), and the biro is there
 * (dark R = G = B ink where the --:-- is) */
static void no_blue(const uint16_t *fb, const char *what)
{
    for (int y = RD_CU_LIDBOX.y0; y < RD_CU_LIDBOX.y1; y++)
        for (int x = RD_CU_LIDBOX.x0; x < RD_CU_LIDBOX.x1; x++) {
            int R, G, B;
            px8(fb[y * W + x], &R, &G, &B);
            if (B > R + 8 && B > G + 8) {
                char m[128];
                snprintf(m, sizeof m, "%s: a blue pixel (%d, %d): %d %d %d", what, x, y, R, G, B);
                TEST_FAIL_MESSAGE(m);
            }
        }
}
static void test_rd_cu_biro_graphite(void)
{
    const row_t *r = row_of("curry");
    /* the wake's first frame too: the lid shut with its printed label in biro (the Task 15 review) */
    fk_face_in_t w = cu_at(10, 42, 3, 0);
    w.wake_ms = 0;
    frame(r, &w, s_a);
    no_blue(s_a, "the wake at 0 ms");
    rd_still_frame(&RD_FACE_CURRY, r->ctx, 7, s_a);
    no_blue(s_a, "still 7");
    long ink = 0;
    for (int y = RD_CU_LIDBOX.y0; y < RD_CU_LIDBOX.y1; y++)
        for (int x = RD_CU_LIDBOX.x0; x < RD_CU_LIDBOX.x1; x++) {
            int R, G, B;
            px8(s_a[y * W + x], &R, &G, &B);
            if (B > R + 8 && B > G + 8) {
                char m[96];
                snprintf(m, sizeof m, "a blue pixel (%d, %d): %d %d %d", x, y, R, G, B);
                TEST_FAIL_MESSAGE(m);
            }
            ink += y >= 225 && y < 252 && x > 160 && x < 306 && R < 100 && G < 100 && B < 100 && R - B < 12 && B - R < 12;
        }
    TEST_ASSERT_TRUE_MESSAGE(ink > 300, "no graphite --:-- on the lid");
}

/* four taps give 1, 2, 3, 0 (the other bits kept); unset a tap does nothing; the tray's rect at its edges */
static void test_rd_cu_chilli_cycle(void)
{
    const row_t *r = row_of("curry");
    fk_face_in_t in = cu_at(10, 42, 3, 0);
    uint16_t v = 0;
    static const uint16_t WANT[4] = {1, 2, 3, 0};
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, RD_FACE_CURRY.tap_var(r->ctx, &in, 233, 150, &v));
        TEST_ASSERT_EQUAL_UINT16(WANT[i], v);
    }
    v = 0x0103;
    RD_FACE_CURRY.tap_var(r->ctx, &in, 70, 396, &v);   /* a corner of the tray's box */
    TEST_ASSERT_EQUAL_HEX16(0x0100, v);
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, RD_FACE_CURRY.tap_var(r->ctx, &in, 30, 233, &v));   /* off the tray */
    TEST_ASSERT_EQUAL_HEX16(0x0100, v);
    in.clk.set = false;   /* the lid is on */
    v = 2;
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, RD_FACE_CURRY.tap_var(r->ctx, &in, 233, 150, &v));
    TEST_ASSERT_EQUAL_UINT16(2, v);
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, RD_FACE_CURRY.tap_var(r->ctx, NULL, 233, 150, &v));   /* no input: set */
    TEST_ASSERT_EQUAL_UINT16(3, v);
    fk_test_tap_edges(&RD_FACE_CURRY, r->ctx, RD_CU_HIT, FK_TAP_PULSE);
    /* the chillies show: each tap's frame differs from the last on the sticker */
    fk_face_in_t a = cu_at(10, 42, 3, 0), b = a;
    for (uint16_t k = 0; k < 3; k++) {
        a.variant = k, b.variant = (uint16_t)(k + 1);
        frame(r, &a, s_a);
        frame(r, &b, s_b);
        TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_CU_STICKER) > 40);
    }
}

/* mid-minute every dirty rect lies inside the steam's box (and the sticker's after a tap), and the steam moves */
static void test_rd_cu_steam_confined(void)
{
    const row_t *r = row_of("curry");
    static const long STEP[] = {50, 100, 1000, 7000, -1000, 20000};
    long moved = 0;
    for (unsigned v = 0; v < 4; v++)
        for (unsigned k = 0; k < sizeof STEP / sizeof STEP[0]; k++) {
            fk_face_in_t a = cu_at(10, 42, 30, 250), b;
            a.variant = (uint16_t)v;
            b = ran(shifted(a, STEP[k]), STEP[k] > 0 ? STEP[k] : 50);
            omni_rect_t rects[8];
            const unsigned n = dirty_pair(r, &a, &b, "curry mid-minute", rects);
            for (unsigned q = 0; q < n; q++) TEST_ASSERT_TRUE_MESSAGE(rect_in(&rects[q], &RD_CU_STEAM), "mid-minute");
            moved += frames_differ(s_a, s_b);
            /* a tap: the next chilli, then its pulse's start (not three: the shudder moves the whole frame) */
            if (v == 3) continue;
            fk_face_in_t c = a;
            c.variant = (uint16_t)(v + 1);
            const unsigned m = dirty_pair(r, &a, &c, "curry tap", rects);
            for (unsigned q = 0; q < m; q++)
                TEST_ASSERT_TRUE_MESSAGE(rect_in(&rects[q], &RD_CU_STEAM) || rect_in(&rects[q], &RD_CU_STICKER), "tap");
        }
    TEST_ASSERT_TRUE_MESSAGE(moved > 10000, "the steam does not move");
}

/* still 7: the lid's card covers the tray (every pixel within r 160 is the card's warm grey or the biro), and no hand
 * is drawn (the unset frame is the same at any time) */
static void test_rd_cu_unset_lid(void)
{
    const row_t *r = row_of("curry");
    rd_still_frame(&RD_FACE_CURRY, r->ctx, 7, s_a);
    long lid = 0;
    for (int y = RD_CU_LIDBOX.y0; y < RD_CU_LIDBOX.y1; y++)
        for (int x = RD_CU_LIDBOX.x0; x < RD_CU_LIDBOX.x1; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
            if (dx * dx + dy * dy > 160.f * 160.f) continue;
            int R, G, B;
            px8(s_a[y * W + x], &R, &G, &B);
            const bool card = R >= 50 && R - G <= 34 && G - R <= 8 && B <= R + 8;   /* the stain at 22 + its dither */
            if (!card) {
                char m[96];
                snprintf(m, sizeof m, "unset: not the lid at (%d, %d): %d %d %d", x, y, R, G, B);
                TEST_FAIL_MESSAGE(m);
            }
            lid++;
        }
    TEST_ASSERT_TRUE(lid > 75000);
    static const int HM[3][2] = {{3, 47}, {12, 0}, {21, 15}};
    for (int k = 0; k < 3; k++) {
        fk_face_in_t u = cu_at(HM[k][0], HM[k][1], 12, 900);
        u.clk.set = false, u.variant = (uint16_t)k, u.wake_ms = 300;
        frame(r, &u, s_b);
        assert_same(s_a, s_b, "an unset frame at another time, variant or wake");
    }
    /* the steel's light (B above R: #eef0f1, the hands' highlight) appears nowhere */
    for (int i = 0; i < W * W; i++) {
        int R, G, B;
        px8(s_a[i], &R, &G, &B);
        TEST_ASSERT_FALSE_MESSAGE(R > 200 && B > R, "a hand's steel on the unset frame");
    }
}

/* the wake: no pixel outside the lid's box differs from the settled frame but in the steam's; the lid shows inside the
 * box until it has swung past the top edge (swing .909, about 716 ms: cos(.55 pi swing) reaches 0), and from there only
 * the steam's puff differs */
static void test_rd_cu_lid_clip(void)
{
    const row_t *r = row_of("curry");
    const omni_rect_t ok[2] = {RD_CU_LIDBOX, RD_CU_STEAM};
    static const uint32_t T[] = {0, 50, 100, 250, 400, 500, 600, 700, 720, 800, 850, 900, 950};
    for (int st = 0; st < 2; st++)
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            fk_face_in_t in = cu_at(10, 42, 3, 0);
            in.clk.stale = st == 1;
            frame(r, &in, s_c);
            in.wake_ms = T[i];
            frame(r, &in, s_a);
            char m[64];
            snprintf(m, sizeof m, "the wake at %u ms (stale %d)", (unsigned)T[i], st);
            TEST_ASSERT_EQUAL_INT32_MESSAGE(0, differ_outside(s_a, s_c, ok, 2), m);
            const omni_rect_t top = {RD_CU_LIDBOX.x0, RD_CU_LIDBOX.y0, RD_CU_LIDBOX.x1, (int16_t)(RD_CU_LIDBOX.y0 + 12)};
            if (T[i] <= 700) TEST_ASSERT_TRUE_MESSAGE(differ_in(s_a, s_c, &top) > 1000, m);   /* the card at the hinge */
            else TEST_ASSERT_EQUAL_INT32_MESSAGE(0, differ_outside(s_a, s_c, &RD_CU_STEAM, 1), m);   /* gone */
            /* the box's corners past the card's rim keep the face (its ticks and numerals) while the card swings:
             * at the start, before the black over its rows has begun (swing .004 at 100 ms) */
            if (T[i] <= 100)
                for (int y = RD_CU_LIDBOX.y0; y < RD_CU_LIDBOX.y1; y++)
                    for (int x = RD_CU_LIDBOX.x0; x < RD_CU_LIDBOX.x1; x++) {
                        const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
                        if (dx * dx + dy * dy > 171.f * 171.f && !inside(&RD_CU_STEAM, x, y) &&   /* the puff aside */
                            s_a[y * W + x] != s_c[y * W + x])
                            TEST_FAIL_MESSAGE("the lid's leaf drawn past the card's rim, over the box's corners");
                    }
            /* mid-swing the black .3 swing lies over the lid's rows of the box, its corners too (500 ms: swing .5,
             * the card squashed to cos(.275 pi) = .649 of its height, 218 rows): the ticks there, off the squashed
             * card (an ellipse 168 x 109 about (233, 174)), dimmed, none brightened */
            if (T[i] == 500) {
                long dimmed = 0;
                for (int y = RD_CU_LIDBOX.y0; y < RD_CU_LIDBOX.y0 + 210; y++)
                    for (int x = RD_CU_LIDBOX.x0; x < RD_CU_LIDBOX.x1; x++) {
                        const float dx = ((float)x + 0.5f - 233.f) / 171.f, dy = ((float)y + 0.5f - 174.1f) / 112.f;
                        if (dx * dx + dy * dy <= 1.f || inside(&RD_CU_STEAM, x, y)) continue;
                        int R0, G0, B0, R1, G1, B1;
                        px8(s_c[y * W + x], &R0, &G0, &B0);
                        px8(s_a[y * W + x], &R1, &G1, &B1);
                        TEST_ASSERT_TRUE_MESSAGE(R1 <= R0 && G1 <= G0 && B1 <= B0, "the swing's black brightens");
                        dimmed += R1 + G1 + B1 < R0 + G0 + B0;
                    }
                TEST_ASSERT_TRUE_MESSAGE(dimmed > 20, "no black over the lid's rows of the box");
            }
            /* its start: the card whole across the box (the shut lid with the order's label) */
            if (T[i] == 0) {
                fk_face_in_t u = in;
                u.clk.set = false;
                frame(r, &u, s_b);
                const omni_rect_t mid = {120, 120, 346, 200};   /* above the label and the unset biro */
                TEST_ASSERT_EQUAL_INT32_MESSAGE(0, differ_in(s_a, s_b, &mid), "the lid at 0 ms is the shut card");
            }
        }
    /* reduced motion: no own wake (the face settled) */
    fk_face_in_t in = cu_at(10, 42, 3, 0);
    in.reduced = true, in.wake_ms = 300;
    frame(r, &in, s_a);
    in.wake_ms = UINT32_MAX;
    frame(r, &in, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* the hands step once a minute (lesson 9): the same through the minute, moved at the next */
static void test_rd_cu_hands_per_minute(void)
{
    fk_face_in_t a = cu_at(10, 42, 0, 0), b = cu_at(10, 42, 59, 950), c = cu_at(10, 43, 0, 0);
    float ha, ma, hb, mb, hc, mc;
    rd_cu_hand_angles(&a, &ha, &ma);
    rd_cu_hand_angles(&b, &hb, &mb);
    rd_cu_hand_angles(&c, &hc, &mc);
    TEST_ASSERT_TRUE(ha == hb && ma == mb && hc != hb && mc != mb);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 6.28318530718f * 43.f / 60.f, mc);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 6.28318530718f * (10.f + 43.f / 60.f) / 12.f, hc);
    a = cu_at(22, 42, 0, 0);   /* 22:42 is 10:42 on the dial */
    rd_cu_hand_angles(&a, &ha, &ma);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, hb, ha);
    /* the bboxes hold their passes: a frame with each hand drawn alone differs from black only inside its box */
    static const float A[] = {0.f, 0.7f, 1.6f, 3.14159f, 4.4f, 5.9f};
    for (unsigned i = 0; i < sizeof A / sizeof A[0]; i++)
        for (uint8_t which = 0; which < 3; which++)
            for (int h = 0; h < 2; h++) {
                memset(s_a, 0, sizeof s_a);
                omni_rect_t bb;
                if (h) rd_hand_spoon(s_a, W, NULL, A[i], which, true), rd_hand_spoon_bbox(A[i], which, &bb);
                else rd_hand_fork(s_a, W, NULL, A[i], which, true), rd_hand_fork_bbox(A[i], which, &bb);
                long lit = 0, out = 0;
                for (int y = 0; y < W; y++)
                    for (int x = 0; x < W; x++) {
                        if (!s_a[y * W + x] && which != RD_CU_PASS_SHADOW) continue;
                        lit += s_a[y * W + x] != 0;
                        out += s_a[y * W + x] != 0 && !inside(&bb, x, y);
                    }
                if (which != RD_CU_PASS_SHADOW) TEST_ASSERT_TRUE(lit > 300);
                TEST_ASSERT_EQUAL_INT32(0, out);
            }
}

/* the shudder: three chillies and the pulse move the whole frame across (sin(t / 18) x 3 (1 - t / 420)); not at two,
 * not under reduced motion, not at the pulse's start */
static void test_rd_cu_shudder(void)
{
    const row_t *r = row_of("curry");
    fk_face_in_t a = cu_at(10, 42, 3, 0);
    a.variant = 3;
    frame(r, &a, s_c);
    a.pulsing = true;
    a.pulse_ms = 0;
    frame(r, &a, s_a);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_c));
    a.pulse_ms = 30;   /* sin(30 / 18) x 3 x (1 - 30 / 420) = 2.78: 3 px to the right */
    frame(r, &a, s_a);
    for (int y = 0; y < W; y++)
        for (int x = 3; x < W; x++) TEST_ASSERT_EQUAL_HEX16(s_c[y * W + x - 3], s_a[y * W + x]);
    a.pulse_ms = 80;   /* sin(80 / 18) x 3 x .81 = -2.36: 2 px to the left */
    frame(r, &a, s_a);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W - 2; x++) TEST_ASSERT_EQUAL_HEX16(s_c[y * W + x + 2], s_a[y * W + x]);
    omni_rect_t rects[8];
    fk_face_in_t b = a;
    b.pulse_ms = 30;
    TEST_ASSERT_EQUAL_UINT8(1, dirty_pair(r, &a, &b, "the shudder", rects));
    TEST_ASSERT_TRUE(rects[0].x0 == 0 && rects[0].x1 == W && rects[0].y0 == 0 && rects[0].y1 == W);
    a.pulse_ms = 399;
    b.pulsing = false, b.pulse_ms = 0;
    dirty_pair(r, &a, &b, "the shudder's end", rects);
    a.pulse_ms = 30;
    a.variant = 2;
    frame(r, &a, s_a);
    b = a, b.pulsing = false, b.pulse_ms = 0;
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
    a.variant = 3, a.reduced = true;
    frame(r, &a, s_a);
    b = a, b.pulsing = false, b.pulse_ms = 0;
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* the steam: red at three chillies; frozen under reduced motion at any clock; thicker with the thrum; the wake's puff */
static void test_rd_cu_steam(void)
{
    const row_t *r = row_of("curry");
    fk_face_in_t a = cu_at(10, 42, 3, 0), b = a;
    b.variant = 3;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    long red = 0, cream = 0;
    for (int y = RD_CU_STEAM.y0; y < RD_CU_STEAM.y1; y++)
        for (int x = RD_CU_STEAM.x0; x < RD_CU_STEAM.x1; x++) {
            int R0, G0, B0, R1, G1, B1;
            px8(s_a[y * W + x], &R0, &G0, &B0);
            px8(s_b[y * W + x], &R1, &G1, &B1);
            cream += G0 + B0, red += G1 + B1;
        }
    TEST_ASSERT_TRUE_MESSAGE(red < cream, "three chillies' steam is not redder");
    /* reduced: the frozen pose at any time (the hands aside: the same minute) */
    a.reduced = true;
    frame(r, &a, s_a);
    b = cu_at(10, 42, 51, 777);
    b.reduced = true;
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
    /* the thrum thickens it, inside the steam's box */
    a = cu_at(10, 42, 3, 0), b = a;
    b.thrum = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    long sa = 0, sb = 0;
    for (int i = 0; i < W * W; i++) sa += s_a[i] & 0x1F, sb += s_b[i] & 0x1F;
    TEST_ASSERT_TRUE(sb > sa + 2000);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_CU_STEAM, 1));
    /* the puff: stronger at the wake's end and early in a fresh wake than settled; none from 1.5 s */
    b = a, b.fresh = true, b.fresh_ms = 100;
    frame(r, &b, s_b);
    sb = 0;
    for (int i = 0; i < W * W; i++) sb += s_b[i] & 0x1F;
    TEST_ASSERT_TRUE(sb > sa + 2000);
    b.fresh_ms = 1500;
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* the chime picture: the steam with thrum (above), the peppercorn's glint with beep, inside the hub's box */
static void test_rd_cu_chime(void)
{
    const row_t *r = row_of("curry");
    fk_face_in_t a = cu_at(10, 42, 3, 0), b = a;
    b.beep = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_CU_HUB) > 30);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_CU_HUB, 1));
    b.reduced = a.reduced = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* the stale marker sits above the hub inside its rect; nothing else changes */
static void test_rd_cu_stale(void)
{
    const row_t *r = row_of("curry");
    rd_still_frame(&RD_FACE_CURRY, r->ctx, 0, s_a);
    rd_still_frame(&RD_FACE_CURRY, r->ctx, 8, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_CU_STALE, 1));
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_CU_STALE) > 100);
}

/* the glance: the rim, four numerals, the cutlery; the unset --:-- as outlines; all moved by the orbit */
static void test_rd_cu_glance(void)
{
    const row_t *r = row_of("curry");
    fk_face_in_t g = cu_at(10, 42, 3, 0);
    g.glance = true;
    frame(r, &g, s_a);
    g.odx = 3, g.ody = -2;
    frame(r, &g, s_b);
    long n = 0;
    for (int y = 10; y < W - 10; y++)
        for (int x = 10; x < W - 10; x++) {
            TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 2) * W + x + 3]);
            n += s_a[y * W + x] != 0;
        }
    TEST_ASSERT_TRUE(n > 2000);
    /* no 1, 2, 4 ... 11: the 1 o'clock numeral's place is dark; the rim's hairline is at r 168 */
    const omni_rect_t one = {320, 45, 350, 80};
    for (int y = one.y0; y < one.y1; y++)
        for (int x = one.x0; x < one.x1; x++) TEST_ASSERT_EQUAL_HEX16(0, s_a[y * W + x]);
    TEST_ASSERT_TRUE(s_a[233 * W + 233 + 168] != 0 && s_a[233 * W + 233 + 160] == 0);
    g.odx = g.ody = 0, g.clk.set = false;
    frame(r, &g, s_a);
    long dash = 0;
    for (int y = 225; y < 262; y++)
        for (int x = 165; x < 300; x++) dash += s_a[y * W + x] != 0;
    TEST_ASSERT_TRUE(dash > 150);
}

/* ---- Task 12: the steam as a pre-rendered per-wisp loop (rd/faces.h's RD_CU_WISP_*), held to wave 1's procedural
 * steam: rd_cu_steam_oracle.c, BASE's puffs_of and steam_draw verbatim (R165/I5), drawn in the face's place through the
 * host hook rd_cu_steam_ref with the face's own state */
#define CU_STEAM_SEQ 40                   /* the 2 s sequence at 50 ms, from still 0's instant */
static uint32_t cu_ref_T;                 /* the state the oracle last drew (its sample count's input) */
static float cu_ref_amt;
static void cu_oracle(uint32_t T, float amt, bool red, uint16_t *fb, int stride, omni_rect_t clip)
{
    cu_ref_T = T, cu_ref_amt = amt;
    rd_cu_steam_oracle(T, amt, red, fb, stride, clip);
}
/* in's frame with wave 1's steam (BASE's frame, R165/I5) */
static void cu_ref_frame(const row_t *r, const fk_face_in_t *in, uint16_t *fb)
{
    rd_cu_steam_ref = cu_oracle;
    frame(r, in, fb);
    rd_cu_steam_ref = NULL;
}
/* the look set: stills 0, 1, 2, 3, 4, 8 and 11, then the 2 s sequence (still 0 moved on 50 ms a frame) */
#define CU_LOOK_STILLS 7
static const unsigned CU_LOOK_STILL[CU_LOOK_STILLS] = {0, 1, 2, 3, 4, 8, RD_STILL_N};
static fk_face_in_t cu_look_in(unsigned i, char *what, size_t n)
{
    rd_still_t st;
    if (i < CU_LOOK_STILLS) {
        const unsigned k = CU_LOOK_STILL[i];
        rd_still(k < RD_STILL_N ? k : 0, &st);
        if (k >= RD_STILL_N) st.in.variant = RD_CU_TAPPED_VARIANT;
        snprintf(what, n, "still %u", k);
        return st.in;
    }
    rd_still(0, &st);
    snprintf(what, n, "still 0 + %u ms", 50 * (i - CU_LOOK_STILLS));
    return shifted(st.in, 50L * (long)(i - CU_LOOK_STILLS));
}
/* BASE's still CRCs (rd/verify_crc.h at 0d2ba75, rows curry 0-11) */
static const uint32_t CU_BASE_CRC[RD_STILL_N + 1] = {0x892758b4u, 0x7b3a9c0eu, 0x3cd8ed3au, 0x892758b4u,
                                                     0xe666b227u, 0xfbbce5bbu, 0x6d9a1705u, 0xfd89a0b3u,
                                                     0xc570cf3fu, 0xfa7e9424u, 0xd9d76c66u, 0x4c9d3f96u};

/* the loop's light: theta + pi is the mirror image about the wisp's axis; 2 pi and the lattice's 1/30 are the same
 * light; the last theta frame runs into the first, mirrored, with no jump */
static uint32_t cu_wl[RD_CU_WISP_W], cu_wm[RD_CU_WISP_W];
static uint32_t cu_row_d(int32_t ta, int32_t pa, int32_t tb, int32_t pb)   /* the largest difference over the rows */
{
    const row_t *r = row_of("curry");
    uint32_t d = 0;
    for (int u = 0; u < RD_CU_WISP_H; u++) {
        rd_cu_wisp_row(r->ctx, ta, pa, u, cu_wl);
        rd_cu_wisp_row(r->ctx, tb, pb, u, cu_wm);
        for (int x = 0; x < RD_CU_WISP_W; x++) {
            const uint32_t e = cu_wl[x] > cu_wm[x] ? cu_wl[x] - cu_wm[x] : cu_wm[x] - cu_wl[x];
            if (e > d) d = e;
        }
    }
    return d;
}
static void test_rd_cu_loop_mirror(void)
{
    const row_t *r = row_of("curry");
#define CU_HALF (RD_CU_WISP_N * 256)
    const int32_t HALF = CU_HALF, TURN = 2 * CU_HALF;
    static const int32_t TH[] = {0, 1, 37, 128, 255, 256, 1000, 3000, 5888, CU_HALF - 1, CU_HALF + 300, 2 * CU_HALF - 1};
    static const int32_t PH[] = {0, 77, 128, 256, 400, 511};
    double light = 0;
    for (unsigned a = 0; a < sizeof TH / sizeof TH[0]; a++)
        for (unsigned b = 0; b < sizeof PH / sizeof PH[0]; b++)
            for (int u = 0; u < RD_CU_WISP_H; u += 3) {
                TEST_ASSERT_TRUE(rd_cu_wisp_row(r->ctx, TH[a], PH[b], u, cu_wl));
                TEST_ASSERT_TRUE(rd_cu_wisp_row(r->ctx, TH[a] + HALF, PH[b], u, cu_wm));
                for (int x = 0; x < RD_CU_WISP_W; x++) {
                    light += cu_wl[x];
                    if (cu_wm[x] != cu_wl[RD_CU_WISP_W - 1 - x]) {
                        char m[160];
                        snprintf(m, sizeof m, "theta_q %d phi_q %d row %d col %d: %u, mirrored %u", (int)TH[a],
                                 (int)PH[b], u, x, (unsigned)cu_wm[x], (unsigned)cu_wl[RD_CU_WISP_W - 1 - x]);
                        TEST_FAIL_MESSAGE(m);
                    }
                }
                TEST_ASSERT_TRUE(rd_cu_wisp_row(r->ctx, TH[a] + TURN, PH[b] + 512, u, cu_wm));
                TEST_ASSERT_EQUAL_MEMORY(cu_wl, cu_wm, sizeof cu_wl);   /* 2 pi, and phi + 1/30 */
                TEST_ASSERT_TRUE(rd_cu_wisp_row(r->ctx, TH[a] - TURN, PH[b] - 512, u, cu_wm));
                TEST_ASSERT_EQUAL_MEMORY(cu_wl, cu_wm, sizeof cu_wl);
            }
    TEST_ASSERT_TRUE_MESSAGE(light > 1e9, "the loop has no light");
    TEST_ASSERT_FALSE(rd_cu_wisp_row(r->ctx, 0, 0, -1, cu_wl));
    TEST_ASSERT_FALSE(rd_cu_wisp_row(r->ctx, 0, 0, RD_CU_WISP_H, cu_wl));
    /* no jump: frame N - 1 to its successor (frame 0 mirrored) and frame 2N - 1 to frame 0 move the light no more than
     * the steps inside a half turn do (x 1.5), and one step of theta_q across either seam moves it a 256th of that */
    for (int32_t p = 0; p < 512; p += 256) {
        uint32_t inner = 0;
        for (int t = 0; t + 1 < RD_CU_WISP_N; t++) {
            const uint32_t d = cu_row_d(t * 256, p, (t + 1) * 256, p);
            if (d > inner) inner = d;
        }
        const uint32_t seam = cu_row_d(HALF - 256, p, HALF, p), wrap = cu_row_d(TURN - 256, p, TURN, p);
        const uint32_t tick = cu_row_d(HALF - 1, p, HALF, p), tick2 = cu_row_d(TURN - 1, p, 0, p);
        printf("curry loop phi_q %d: the largest step inside %u, at the seam %u, at the wrap %u; one theta_q %u, %u\n",
               (int)p, (unsigned)inner, (unsigned)seam, (unsigned)wrap, (unsigned)tick, (unsigned)tick2);
        TEST_ASSERT_TRUE(inner > 0);
        TEST_ASSERT_TRUE_MESSAGE(2 * seam <= 3 * inner, "a jump at the mirror seam (frame N - 1 to frame 0 mirrored)");
        TEST_ASSERT_TRUE_MESSAGE(2 * wrap <= 3 * inner, "a jump at the wrap (frame 2N - 1 to frame 0)");
        TEST_ASSERT_TRUE(tick <= seam / 128 + 1 && tick2 <= wrap / 128 + 1);
    }
    /* phi: the half step to the next stored frame and the lattice's wrap (frame 2 is frame 0) move it as little */
    uint32_t inner = 0;
    for (int t = 0; t < RD_CU_WISP_N; t += 5) {
        const uint32_t d = cu_row_d(t * 256, 0, t * 256, 256), e = cu_row_d(t * 256, 256, t * 256, 512);
        if (d > inner) inner = d;
        if (e > inner) inner = e;
    }
    TEST_ASSERT_TRUE(inner > 0 && cu_row_d(1000, 511, 1000, 512) <= inner / 64 + 1);
}

/* mid-minute every dirty rect lies inside RD_CU_STEAM, as before: 2 s at the cadence, skips both ways, the puff, a
 * thrum; the loop's frame differs from wave 1's only inside it */
static void test_rd_cu_loop_confined(void)
{
    const row_t *r = row_of("curry");
    static const long STEP[] = {50, 50, 50, 100, 250, 1000, 3000, -50, -1000};
    fk_face_in_t a = cu_at(10, 42, 30, 250);
    long moved = 0;
    for (unsigned k = 0; k < 40; k++) {
        const long st = STEP[k % (sizeof STEP / sizeof STEP[0])];
        fk_face_in_t b = ran(shifted(a, st), st > 0 ? st : 50);
        b.thrum = k % 7 == 3 ? 0.6f : 0.f;
        if (k % 11 == 5) b.fresh = true, b.fresh_ms = (uint16_t)(100 * k);
        omni_rect_t rects[8];
        const unsigned n = dirty_pair(r, &a, &b, "curry loop mid-minute", rects);
        for (unsigned q = 0; q < n; q++) TEST_ASSERT_TRUE_MESSAGE(rect_in(&rects[q], &RD_CU_STEAM), "mid-minute");
        moved += frames_differ(s_a, s_b);
        cu_ref_frame(r, &b, s_c);
        TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_b, s_c, &RD_CU_STEAM, 1));
        a = b;
    }
    TEST_ASSERT_TRUE_MESSAGE(moved > 40000, "the steam does not move");
}

/* the look (R165/I5): against the oracle on stills 0, 1, 2, 3, 4, 8 and 11 and the 2 s sequence: outside RD_CU_STEAM
 * byte for byte; over the lit mask (a pixel where either side has a channel above 8, 8-bit as px8 widens RGB565) the
 * mean absolute difference a channel <= 2 levels, 99.5 % of the pixels within 8 levels, none beyond 24; every row's
 * luminance sum over the lit mask within 5 % of the oracle's. The stills without steam (7 unset; 5, 6, 9 and 10 the
 * glances) keep BASE's CRCs, and the oracle reproduces BASE's frame of every still */
static void cu_look_rows(const uint16_t *a, const uint16_t *b, bool box, double *mean, double *within, int *worst,
                         double *row, int *row_y)
{
    long n = 0, ok = 0;
    double sad = 0;
    *worst = 0, *row = 0, *row_y = -1;
    for (int y = 0; y < W; y++) {
        double la = 0, lb = 0;
        for (int x = 0; x < W; x++) {
            if (box && !inside(&RD_CU_STEAM, x, y)) continue;
            int ca[3], cb[3];
            px8(a[y * W + x], &ca[0], &ca[1], &ca[2]);
            px8(b[y * W + x], &cb[0], &cb[1], &cb[2]);
            if (!(ca[0] > 8 || ca[1] > 8 || ca[2] > 8 || cb[0] > 8 || cb[1] > 8 || cb[2] > 8)) continue;
            int m = 0;
            for (int c = 0; c < 3; c++) {
                const int d = ca[c] > cb[c] ? ca[c] - cb[c] : cb[c] - ca[c];
                sad += d;
                if (d > m) m = d;
            }
            n++, ok += m <= 8;
            if (m > *worst) *worst = m;
            la += 0.299 * ca[0] + 0.587 * ca[1] + 0.114 * ca[2];
            lb += 0.299 * cb[0] + 0.587 * cb[1] + 0.114 * cb[2];
        }
        const double e = la > 0 ? (lb > la ? lb - la : la - lb) / la : lb > 0 ? 1.0 : 0.0;
        if (e > *row) *row = e, *row_y = y;
    }
    *mean = n ? sad / (3.0 * (double)n) : 0, *within = n ? (double)ok / (double)n : 1;
}
static void test_rd_cu_loop_look(void)
{
    const row_t *r = row_of("curry");
    /* the oracle is BASE's steam: its frame of every still is BASE's, byte for byte */
    for (unsigned k = 0; k <= RD_STILL_N; k++) {
        rd_still_t st;
        rd_still(k < RD_STILL_N ? k : 0, &st);
        if (k >= RD_STILL_N) st.in.variant = RD_CU_TAPPED_VARIANT;
        cu_ref_frame(r, &st.in, s_c);
        char m[64];
        snprintf(m, sizeof m, "the oracle's still %u is not BASE's", k);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(CU_BASE_CRC[k], fk_crc32(0, s_c, sizeof s_c), m);
    }
    /* no steam: 7 (unset), 5, 6, 9 and 10 (the glances) keep BASE's CRC */
    static const unsigned SAME[] = {5, 6, 7, 9, 10};
    for (unsigned i = 0; i < sizeof SAME / sizeof SAME[0]; i++) {
        rd_still_frame(&RD_FACE_CURRY, r->ctx, SAME[i], s_a);
        TEST_ASSERT_EQUAL_HEX32(CU_BASE_CRC[SAME[i]], fk_crc32(0, s_a, sizeof s_a));
    }
    double wm = 0, ww = 1, wr = 0, bm = 0, bw = 1;
    int wx = 0;
    for (unsigned i = 0; i < CU_LOOK_STILLS + CU_STEAM_SEQ; i++) {
        char what[48];
        const fk_face_in_t in = cu_look_in(i, what, sizeof what);
        frame(r, &in, s_a);
        cu_ref_frame(r, &in, s_b);
        if (i < CU_LOOK_STILLS) {   /* the stills are rd_still_frame's frames */
            rd_still_frame(&RD_FACE_CURRY, r->ctx, CU_LOOK_STILL[i], s_c);
            assert_same(s_c, s_a, what);
        }
        TEST_ASSERT_EQUAL_INT32_MESSAGE(0, differ_outside(s_a, s_b, &RD_CU_STEAM, 1), what);
        double mean, within, row, fm, fw, fr;
        int worst, fx, ry, fy;
        cu_look_rows(s_b, s_a, true, &mean, &within, &worst, &row, &ry);     /* the steam's box */
        cu_look_rows(s_b, s_a, false, &fm, &fw, &fx, &fr, &fy);              /* the frame (the same outside) */
        if (i < CU_LOOK_STILLS || i % 10 == 0)
            printf("curry loop look %-18s box: mean %.3f, within 8 %.4f, worst %d, rows %.4f (y %d); frame: mean %.3f, "
                   "within 8 %.4f, rows %.4f (y %d)\n", what, mean, within, worst, row, ry, fm, fw, fr, fy);
        char m[160];
        snprintf(m, sizeof m, "%s: mean %.3f, within 8 levels %.4f, worst %d, a row's luminance %.4f off (y %d)", what,
                 mean, within, worst, row, ry);
        TEST_ASSERT_TRUE_MESSAGE(mean <= 2.0, m);
        TEST_ASSERT_TRUE_MESSAGE(within >= 0.995, m);
        TEST_ASSERT_TRUE_MESSAGE(worst <= 24, m);
        TEST_ASSERT_TRUE_MESSAGE(row <= 0.05, m);
        if (mean > wm) wm = mean;
        if (within < ww) ww = within;
        if (worst > wx) wx = worst;
        if (row > wr) wr = row;
        if (fm > bm) bm = fm;
        if (fw < bw) bw = fw;
    }
    printf("curry loop look, worst over the set: box mean %.3f, within 8 %.4f, worst %d, rows %.4f; frame mean %.3f, "
           "within 8 %.4f\n", wm, ww, wx, wr, bm, bw);
}

/* the cost (R165/I6): on the same frames the loop's samples (a frame row pair at one stored column) are at most half
 * the oracle's (its bilinear blob samples over RD_CU_STEAM) */
static void test_rd_cu_loop_cost(void)
{
    const row_t *r = row_of("curry");
    unsigned long so = 0, sl = 0;
    for (unsigned i = 0; i < CU_LOOK_STILLS + CU_STEAM_SEQ; i++) {
        char what[48];
        const fk_face_in_t in = cu_look_in(i, what, sizeof what);
        cu_ref_frame(r, &in, s_b);   /* the oracle's state for this frame (cu_ref_T, cu_ref_amt) */
        const unsigned long o = rd_cu_steam_oracle_samples(cu_ref_T, cu_ref_amt);
        frame(r, &in, s_a);
        const unsigned long l = rd_cu_wisp_samples(r->ctx);
        char m[96];
        snprintf(m, sizeof m, "%s: the loop's samples %lu, the oracle's %lu", what, l, o);
        TEST_ASSERT_TRUE_MESSAGE(o > 50000 && l > 0, m);
        TEST_ASSERT_TRUE_MESSAGE(2 * l <= o, m);
        so += o, sl += l;
    }
    printf("curry loop cost: %lu samples a frame against the oracle's %lu (%.3f)\n",
           sl / (CU_LOOK_STILLS + CU_STEAM_SEQ), so / (CU_LOOK_STILLS + CU_STEAM_SEQ), (double)sl / (double)so);
}

/* ---- Bay Mouth (`bay`, spec section 10.2; wave 2 Task 3; faces-reveal.js:612-687, the glance :731-741) */
static fk_face_in_t by_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3, in.clk.yday = 308;   /* WED 04 NOV 2026 */
    return in;
}
static float by_out_back(float s) { const float u = s - 1.f; return 1.f + 2.5f * u * u * u + 1.5f * u * u; }
/* the pip's box at in (rd_by_pip_rect at the face's own turn: the eased one, or under reduced motion the second's
 * settled place) */
static omni_rect_t by_pip_box(const fk_face_in_t *in)
{
    omni_rect_t r;
    const float turn = in->reduced ? (float)(in->clk.s % 60 + 1) / 60.f : rd_by_pip_turn(&in->clk);
    rd_by_pip_rect(turn, &r);
    return r;
}
static bool rect_eq(const omni_rect_t *a, const omni_rect_t *b) { return memcmp(a, b, sizeof *a) == 0; }
/* R170: Bay Mouth's regions with a spec burn-in motion: the star band (RD_BY_STAR, where the band shows: inside the lip
 * and past the ring, the caption and the time over it excluded, as they do not move) and the crawling ring (r 216.5 to
 * 236.5, its coverage) */
static bool by_moving(int x, int y)
{
    const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f, d2 = dx * dx + dy * dy;
    if (d2 >= 216.5f * 216.5f && d2 <= 236.5f * 236.5f) return true;
    if (!inside(&RD_BY_STAR, x, y) || inside(&RD_BY_CAPTION, x, y) || inside(&RD_BY_TIME, x, y)) return false;
    return d2 < 206.5f * 206.5f || d2 > 236.5f * 236.5f;
}
static bool by_lists(const omni_rect_t *rects, unsigned n, const omni_rect_t *want);
/* the four rim bands are all among n rects */
static bool by_bands(const omni_rect_t *rects, unsigned n)
{
    omni_rect_t b[4];
    rd_by_ring_rects(b);
    for (int i = 0; i < 4; i++)
        if (!by_lists(rects, n, &b[i])) return false;
    return true;
}
static bool by_lists(const omni_rect_t *rects, unsigned n, const omni_rect_t *want)
{
    for (unsigned k = 0; k < n; k++)
        if (rect_eq(&rects[k], want)) return true;
    return false;
}
/* a pixel the ring, the lip, its steel line or its rivets can touch (the annulus from r 205.5, past their AA) */
static bool by_annulus(int x, int y)
{
    const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
    return dx * dx + dy * dy >= 205.5f * 205.5f;
}

/* the ops' wiring: the brief's numbers, the rects, the glow tables (softSprite()'s profile), the face's files, and every
 * glyph inside its text rect */
static void test_rd_by_face(void)
{
    TEST_ASSERT_EQUAL_STRING("bay", RD_FACE_BAY.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_BAY.cadence_ms);
    TEST_ASSERT_TRUE(RD_FACE_BAY.reveal);   /* the airlock (Task 4; test_rd_airlock) */
    TEST_ASSERT_EQUAL_PTR(&RD_REVEAL_AIRLOCK, RD_FACE_BAY.reveal_ops);
    TEST_ASSERT_NULL(RD_FACE_BAY.reveal_asset);
    TEST_ASSERT_FALSE(RD_FACE_BAY.direct);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_BAY.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_BAY.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT8(0, RD_FACE_BAY.stills_extra);
    TEST_ASSERT_NOT_NULL(RD_FACE_BAY.tap);
    TEST_ASSERT_NULL(RD_FACE_BAY.tap_var);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_BAY, rd_face_by_key("bay"));
    TEST_ASSERT_TRUE(RD_FACE_BAY.ctx_bytes <= 1572864u - 434312u);
    printf("bay ctx_bytes %u\n", (unsigned)RD_FACE_BAY.ctx_bytes);
    const omni_rect_t star = {0, 266, W, W}, screen = {0, 0, W, W};
    TEST_ASSERT_TRUE(rect_eq(&RD_BY_STAR, &star));
    TEST_ASSERT_TRUE(rect_eq(&RD_BY_RING, &screen));   /* r 236 + AA about 233: the ring's box is the screen */
    const omni_rect_t *texts[4] = {&RD_BY_STALE, &RD_BY_DATE, &RD_BY_TIME, &RD_BY_CAPTION};
    for (int k = 0; k < 4; k++) TEST_ASSERT_TRUE(rect_in(texts[k], &screen));
    TEST_ASSERT_TRUE(RD_BY_PIP.x0 < 0 && RD_BY_PIP.y0 < 0 && RD_BY_PIP.x1 > 0 && RD_BY_PIP.y1 > 0);   /* a template */
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(3, rd_by_chime_rects(c));
    TEST_ASSERT_TRUE(rect_eq(&c[0], &RD_BY_RING));
    for (int k = 0; k < 3; k++) TEST_ASSERT_TRUE(rect_in(&c[k], &screen));
    TEST_ASSERT_EQUAL_HEX16(RD_RGB(0x6E, 0x52, 0x14), RD_G_HAZARD);
    int R, G, B;
    px8(RD_G_HAZARD, &R, &G, &B);
    TEST_ASSERT_TRUE(R <= 110 && G <= 110 && B <= 110);
    /* the pip's glow r 18 and the chime's r 255 (coarse) in the profile glowAt() draws (softSprite: rd/soft_lut.h) */
    static uint8_t lut[FK_GLOW_LUT_MAX];
    static const int GR[2] = {18, 255};
    static const int SR[] = RD_SOFT_LUT_RADII;
    for (int k = 0; k < 2; k++) {
        bool listed = false;
        for (unsigned i = 0; i < sizeof SR / sizeof SR[0]; i++) listed = listed || SR[i] == GR[k];
        TEST_ASSERT_TRUE_MESSAGE(listed, "a Bay Mouth glow is not in RD_SOFT_LUT_RADII");
        TEST_ASSERT_NOT_NULL(rd_glow_lut(GR[k]));
        rd_soft_lut_build(lut, GR[k]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(lut, rd_glow_lut(GR[k]), FK_GLOW_LUT_N(GR[k]));
    }
    TEST_ASSERT_EQUAL_UINT(4066, FK_GLOW_LUT_N(255));   /* the coarse table: (255^2 x 4 - 1) / 64 + 2 */
    /* the face's own files (rd_by_*): the brief's sizes (each with its 12-byte header), within the 500 000 B cap */
    size_t n0 = 0, n1 = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_BY_STAR, &n0));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_BY_RING, &n1));
    TEST_ASSERT_TRUE(ASSET_RD_BY_STAR_W * ASSET_RD_BY_STAR_H * 2 == 186400 && ASSET_RD_BY_STAR_W == 466);
    TEST_ASSERT_TRUE(ASSET_RD_BY_RING_W * ASSET_RD_BY_RING_H == 26980 && ASSET_RD_BY_RING_W == 1420 &&
                     ASSET_RD_BY_RING_H == 19);
    TEST_ASSERT_EQUAL_UINT(186412u + 26992u, (unsigned)(n0 + n1));
    TEST_ASSERT_TRUE(n0 + n1 <= 500000u);
    printf("bay's files %u B (cap 500 000)\n", (unsigned)(n0 + n1));
    /* every glyph lies inside one of the text rects: awake, 12 and 24 h at lesson 10's times and the widest date, stale,
     * unset, mid-wake (its drop: the time's rect holds the settled place only, the wake redraws the whole frame) */
    const row_t *r = row_of("bay");
    static const int HM[][2] = {{1, 0}, {9, 59}, {10, 0}, {12, 59}, {23, 59}, {0, 0}, {20, 8}};
    for (unsigned k = 0; k < sizeof HM / sizeof HM[0]; k++)
        for (int st = 0; st < 6; st++) {
            fk_face_in_t in = by_at(HM[k][0], HM[k][1], 30, 500);
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP */
            in.h24 = st % 2 == 0, in.clk.stale = st >= 2 && st < 4, in.clk.set = st < 4;
            s_ngl = 0;
            fk_test_glyph_hook = record;
            frame(r, &in, s_a);
            fk_test_glyph_hook = NULL;
            TEST_ASSERT_TRUE(s_ngl > 3 && s_ngl <= MAX_GLYPHS);
            for (unsigned g = 0; g < s_ngl; g++) {
                const omni_rect_t b = {(int16_t)s_gl[g].x0, (int16_t)s_gl[g].y0, (int16_t)s_gl[g].x1, (int16_t)s_gl[g].y1};
                bool in_one = false;
                for (int q = 0; q < 4 && !in_one; q++) in_one = rect_in(&b, texts[q]);
                char m[160];
                snprintf(m, sizeof m, "bay at %02d:%02d state %d: a glyph box (%d, %d)-(%d, %d) outside the text rects",
                         HM[k][0], HM[k][1], st, b.x0, b.y0, b.x1, b.y1);
                TEST_ASSERT_TRUE_MESSAGE(in_one, m);
            }
        }
}

/* C18: the star band's shift, 0..3 px, a pixel every 2 min, a 12 min triangle; 0 unset. dirty() names RD_BY_STAR
 * exactly when the shift changes; between changes nothing inside the band changes but the ring, the pip and the text */
static void test_rd_by_star_shift(void)
{
    static const int SOD[] = {0, 120, 240, 360, 480, 600, 720, 119, 121, 719, 86399};
    static const int DY[] = {0, 1, 2, 3, 2, 1, 0, 0, 1, 1, 1};
    for (unsigned k = 0; k < sizeof SOD / sizeof SOD[0]; k++) {
        const fk_face_in_t in = by_at(SOD[k] / 3600, SOD[k] / 60 % 60, SOD[k] % 60, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(DY[k], rd_by_star_dy(&in.clk), "the star's shift");
    }
    fk_face_in_t u = by_at(0, 6, 0, 0);
    u.clk.set = false;
    TEST_ASSERT_EQUAL_INT(0, rd_by_star_dy(&u.clk));
    const row_t *r = row_of("bay");
    omni_rect_t rects[8];
    static const long STEP[] = {1000, 40, 7000, -1000, 61000};
    for (long t = 0; t < 86400; t++)
        for (unsigned k = 0; k < sizeof STEP / sizeof STEP[0]; k++) {
            const fk_face_in_t a = by_at((int)(t / 3600), (int)(t / 60 % 60), (int)(t % 60), 980);
            const fk_face_in_t b = shifted(a, STEP[k]);
            const unsigned n = r->ops->dirty(r->ctx, &a, &b, rects);
            const bool want = rd_by_star_dy(&a.clk) != rd_by_star_dy(&b.clk);
            /* more than 8 rects (the four rim bands, the star, the time, the date, the pips at midnight) collapse to
             * their bounding box, the screen: that names the band too */
            const omni_rect_t screen = {0, 0, W, W};
            const bool named = by_lists(rects, n, &RD_BY_STAR) || (n == 1 && rect_eq(&rects[0], &screen));
            if (named != want) {
                char m[96];
                snprintf(m, sizeof m, "sod %ld %+ld ms: RD_BY_STAR %s", t, STEP[k], want ? "missing" : "named unchanged");
                TEST_FAIL_MESSAGE(m);
            }
        }
    fk_face_in_t a = by_at(10, 5, 59, 960), b = shifted(a, 40);   /* sod 36 359 -> 36 360: dy 2 -> 3 */
    TEST_ASSERT_TRUE(rd_by_star_dy(&a.clk) == 2 && rd_by_star_dy(&b.clk) == 3);
    dirty_pair(r, &a, &b, "the star's step", NULL);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BY_STAR) > 5000);
    /* between changes: only the ring's annulus, the pips and the text change inside the band */
    for (int s = 0; s < 59; s += 7) {
        a = by_at(10, 9, s, 500), b = by_at(10, 9, s + 1, 500);
        TEST_ASSERT_EQUAL_INT(rd_by_star_dy(&a.clk), rd_by_star_dy(&b.clk));
        frame(r, &a, s_a);
        frame(r, &b, s_b);
        const omni_rect_t pa = by_pip_box(&a), pb = by_pip_box(&b);
        for (int y = RD_BY_STAR.y0; y < RD_BY_STAR.y1; y++)
            for (int x = 0; x < W; x++) {
                if (s_a[y * W + x] == s_b[y * W + x] || by_annulus(x, y) || inside(&pa, x, y) || inside(&pb, x, y) ||
                    inside(&RD_BY_CAPTION, x, y) || inside(&RD_BY_TIME, x, y))
                    continue;
                char m[96];
                snprintf(m, sizeof m, "10:09:%02d -> +1 s: band pixel (%d, %d) changes", s, x, y);
                TEST_FAIL_MESSAGE(m);
            }
    }
}

/* the ring crawls one strip column a second (sod mod 1 420); dirty() names the four rim bands at each second's change
 * (R173), which hold every ring pixel, and within a second only the pip's boxes */
static void test_rd_by_ring_crawl(void)
{
    static const long SOD[] = {0, 1, 1419, 1420, 1421, 36570, 86399};
    for (unsigned k = 0; k < sizeof SOD / sizeof SOD[0]; k++) {
        const fk_face_in_t in = by_at((int)(SOD[k] / 3600), (int)(SOD[k] / 60 % 60), (int)(SOD[k] % 60), 700);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(SOD[k] % 1420), rd_by_ring_col(&in.clk));
    }
    fk_face_in_t u = by_at(3, 4, 5, 0);
    u.clk.set = false;
    TEST_ASSERT_EQUAL_UINT16(0, rd_by_ring_col(&u.clk));
    const row_t *r = row_of("bay");
    omni_rect_t rects[8], band[4];
    TEST_ASSERT_EQUAL_UINT(4, rd_by_ring_rects(band));
    for (int y = 0; y < W; y++)   /* the bands hold the ring (r 216 to 237, past its edges) */
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f, d2 = dx * dx + dy * dy;
            if (d2 >= 216.f * 216.f && d2 <= 237.f * 237.f) TEST_ASSERT_TRUE_MESSAGE(in_any(band, 4, x, y), "a ring pixel outside the rim bands");
        }
    long area = 0;
    for (int i = 0; i < 4; i++) area += (long)(band[i].x1 - band[i].x0) * (band[i].y1 - band[i].y0);
    printf("bay's rim bands: %ld px (the screen %d)\n", area, W * W);
    TEST_ASSERT_TRUE(area < W * W * 6L / 10);
    for (int red = 0; red < 2; red++)
        for (int s = 0; s < 60; s++) {
            fk_face_in_t a = by_at(10, 9, s, 960), b = shifted(a, 40);
            a.reduced = b.reduced = red != 0;
            unsigned n = r->ops->dirty(r->ctx, &a, &b, rects);
            /* reduced motion (R171): the ring steps only with the minute (s 59 -> the next minute) */
            TEST_ASSERT_TRUE_MESSAGE(by_bands(rects, n) == (!red || s == 59), "the rim bands at a second's change");
            for (int ms = 0; ms + 40 < 1000; ms += 40) {
                a = by_at(10, 9, s, ms), b = shifted(a, 40);
                a.reduced = b.reduced = red != 0;
                n = r->ops->dirty(r->ctx, &a, &b, rects);
                const omni_rect_t pa = by_pip_box(&a), pb = by_pip_box(&b);
                for (unsigned k = 0; k < n; k++) {
                    char m[96];
                    snprintf(m, sizeof m, "10:09:%02d.%03d + 40 ms%s: a dirty rect outside the pip's boxes", s, ms,
                             red ? " (reduced)" : "");
                    TEST_ASSERT_TRUE_MESSAGE(rect_in(&rects[k], &pa) || rect_in(&rects[k], &pb), m);
                }
            }
        }
    /* the frames: a second moves the ring (and nothing outside the annulus, the pips and the text); a column a second */
    fk_face_in_t a = by_at(10, 9, 41, 500), b = by_at(10, 9, 42, 500);
    dirty_pair(r, &a, &b, "the crawl", NULL);
    long ring = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) ring += s_a[y * W + x] != s_b[y * W + x] && by_annulus(x, y);
    TEST_ASSERT_TRUE_MESSAGE(ring > 3000, "a second does not move the ring");
    for (int k = 0; k < 3; k++) {
        static const uint16_t COL[3] = {0, 709, 1419};
        memset(s_a, 0, sizeof s_a);
        memset(s_b, 0, sizeof s_b);
        const omni_rect_t all = {0, 0, W, W};
        rd_by_ring_draw(r->ctx, COL[k], all, s_a, W);
        rd_by_ring_draw(r->ctx, (uint16_t)((COL[k] + 1) % 1420), all, s_b, W);
        TEST_ASSERT_TRUE(frames_differ(s_a, s_b) > 3000);
        memset(s_b, 0, sizeof s_b);
        rd_by_ring_draw(r->ctx, (uint16_t)(COL[k] + 1420), all, s_b, W);   /* a whole turn is the same ring */
        TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
    }
}

/* R171: under reduced motion the ring steps a column once a minute, with the time ((h x 60 + m) mod 1 420); the star's
 * shift keeps running. Within a minute the frames differ only at the pip; across it the ring moves */
static void test_rd_by_reduced_crawl(void)
{
    const row_t *r = row_of("bay");
    fk_face_in_t a = by_at(10, 9, 5, 500), b = by_at(10, 9, 41, 500);
    a.reduced = b.reduced = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    const omni_rect_t pa = by_pip_box(&a), pb = by_pip_box(&b);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (s_a[y * W + x] != s_b[y * W + x])
                TEST_ASSERT_TRUE_MESSAGE(inside(&pa, x, y) || inside(&pb, x, y), "the reduced ring moves within a minute");
    fk_face_in_t c = by_at(10, 10, 5, 500);
    c.reduced = true;
    frame(r, &c, s_b);
    long ring = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f, d2 = dx * dx + dy * dy;
            ring += s_a[y * W + x] != s_b[y * W + x] && d2 >= 216.5f * 216.5f;
        }
    TEST_ASSERT_TRUE_MESSAGE(ring > 3000, "the reduced ring does not step with the minute");
    /* its step is one column: the frame at 10:10 under reduced motion is the frame of a column offset 610 (10 x 60 + 10)
     * mod 1 420 = 610, as the awake crawl's at sod 610 */
    fk_face_in_t d = by_at(0, 10, 10, 500), e = by_at(10, 10, 5, 500);
    e.reduced = true;
    memset(s_a, 0, sizeof s_a);
    memset(s_b, 0, sizeof s_b);
    rd_by_ring_draw(r->ctx, rd_by_ring_col(&d.clk), (omni_rect_t){0, 0, W, W}, s_a, W);
    rd_by_ring_draw(r->ctx, (uint16_t)((e.clk.h * 60 + e.clk.m) % 1420), (omni_rect_t){0, 0, W, W}, s_b, W);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
    /* the star's shift runs under reduced motion: 10:05:59 -> 10:06:00 moves the band (dy 2 -> 3) */
    fk_face_in_t f = by_at(10, 5, 59, 500), g = shifted(f, 1000);
    f.reduced = g.reduced = true;
    TEST_ASSERT_TRUE(rd_by_star_dy(&f.clk) != rd_by_star_dy(&g.clk));
    dirty_pair(r, &f, &g, "the reduced star's step", NULL);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BY_STAR) > 5000);
}

/* the pip: (s + outCubic(min(ms / 250, 1))) / 60 of a turn; held from 250 ms; within a second dirty() names only its
 * boxes, and none while it holds */
static void test_rd_by_pip(void)
{
    static const struct { int ms; float turn; } T[] = {
        {0, 30.f / 60.f}, {125, (30.f + 0.875f) / 60.f}, {250, 31.f / 60.f}, {600, 31.f / 60.f}, {999, 31.f / 60.f}};
    for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) {
        const fk_face_in_t in = by_at(10, 9, 30, T[k].ms);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, T[k].turn, rd_by_pip_turn(&in.clk));
    }
    const row_t *r = row_of("bay");
    omni_rect_t rects[8];
    for (int ms = 260; ms + 40 < 1000; ms += 40) {
        const fk_face_in_t a = by_at(10, 9, 30, ms), b = shifted(a, 40);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, r->ops->dirty(r->ctx, &a, &b, rects), "the held pip redraws");
    }
    for (int ms = 0; ms < 240; ms += 40) {
        const fk_face_in_t a = by_at(10, 9, 30, ms), b = shifted(a, 40);
        const unsigned n = dirty_pair(r, &a, &b, "the pip's ease", rects);
        TEST_ASSERT_TRUE(n >= 1 && frames_differ(s_a, s_b) > 10);
        const omni_rect_t pa = by_pip_box(&a), pb = by_pip_box(&b);
        for (unsigned k = 0; k < n; k++) TEST_ASSERT_TRUE(rect_in(&rects[k], &pa) || rect_in(&rects[k], &pb));
    }
    /* its box holds it: two frames of one second, the pip at two places of its ease, differ only inside its two boxes */
    for (int s = 0; s < 60; s += 3) {
        const fk_face_in_t a = by_at(10, 9, s, 120);
        frame(r, &a, s_a);
        const omni_rect_t pa = by_pip_box(&a);
        fk_face_in_t c = by_at(10, 9, s, 120);
        c.clk.ms = (uint16_t)((s * 37) % 120);   /* the pip elsewhere in its ease */
        frame(r, &c, s_b);
        const omni_rect_t pc = by_pip_box(&c);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (s_a[y * W + x] != s_b[y * W + x])
                    TEST_ASSERT_TRUE_MESSAGE(inside(&pa, x, y) || inside(&pc, x, y), "the pip outside its box");
    }
}

/* spec 10.2's "at most 45 %" (R169: an area measure) and the face's own levels */
static void test_rd_by_dimming(void)
{
    size_t n = 0;
    const uint8_t *bin = rd_assets_host_get(ASSET_RD_BY_STAR, &n);
    TEST_ASSERT_NOT_NULL(bin);
    TEST_ASSERT_EQUAL_UINT(12u + 466u * 200u * 2u, (unsigned)n);
    const uint16_t *lay = (const uint16_t *)(const void *)(bin + 12);
    long lit = 0;
    double sum = 0;
    int peak = 0;
    for (int i = 0; i < 466 * 200; i++) {
        int R, G, B;
        px8(lay[i], &R, &G, &B);
        const int m = R > G ? (R > B ? R : B) : (G > B ? G : B);
        peak = m > peak ? m : peak;
        if (m > 8) lit++, sum += m;
    }
    const double mean = sum / (double)lit;
    /* R169: the mean of each lit pixel's largest channel (largest > 8) is at most round(.45 x 255) = 115, spec 10.2's
     * "at most 45 %" read as the area's level (burn-in is an area effect; the mockup's own photosphere core is #B4501F,
     * 180 in red, so a 45 % peak ceiling would not hold even for the mockup's art); the peak channel is reported, not
     * gated (a small hot core, a star, or the thin limb line is no burn-in area). Over the ceiling: stop and report
     * (R146), never dim the art to fit */
    printf("bay's star band: %ld lit px, mean largest channel %.1f (ceiling 115), peak %d (ASSET_RD_BY_STAR_MAX %d)\n",
           lit, mean, peak, ASSET_RD_BY_STAR_MAX);
    TEST_ASSERT_TRUE_MESSAGE(mean <= 115.0, "the star band's area level is over 45 % (R169)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(peak, ASSET_RD_BY_STAR_MAX, "rd_pack's max is not the layer's largest channel");
    /* the face adds nothing to the band outside the chime: inside RD_BY_STAR, away from the annulus, the pip and the
     * text, still 0's pixels are the layer's, moved down by the shift (the rows it uncovers black) */
    const row_t *r = row_of("bay");
    for (int k = 0; k < 4; k++) {
        static const int SOD[4] = {36570, 120, 240, 360};
        fk_face_in_t in = by_at(SOD[k] / 3600, SOD[k] / 60 % 60, SOD[k] % 60, 500);
        const int dy = rd_by_star_dy(&in.clk);
        frame(r, &in, s_a);
        const omni_rect_t pa = by_pip_box(&in);
        long same = 0;
        for (int y = RD_BY_STAR.y0; y < RD_BY_STAR.y1; y++)
            for (int x = 0; x < W; x++) {
                if (by_annulus(x, y) || inside(&pa, x, y) || inside(&RD_BY_CAPTION, x, y) || inside(&RD_BY_TIME, x, y))
                    continue;
                const int sy = y - 266 - dy;
                const uint16_t want = sy < 0 ? 0 : lay[sy * 466 + x];
                if (s_a[y * W + x] != want) {
                    char m[96];
                    snprintf(m, sizeof m, "dy %d: band pixel (%d, %d) is %04x, the layer's %04x", dy, x, y, s_a[y * W + x],
                             want);
                    TEST_FAIL_MESSAGE(m);
                }
                same++;
            }
        TEST_ASSERT_TRUE(same > 40000);   /* the band's segment inside r 205.5, less the caption, the time and the pip */
    }
    /* the ring's own levels (drawn alone over black, outside the chime): the strip carries the mockup's .72
     * (round(.72 x 255) = 184 at most), and every pixel's largest channel is at most 173 (R172): .72 x the RGB565
     * rd.hazard's red, 231 (226 packs to 28 of 31, widened by bit replication), is 166.3, rounded up to the next
     * RGB565 red level, 21 of 31 = 173 (the hazardDark pass under it adds a level at most). The brief's 164 came from
     * the token's 8-bit 226, before quantisation */
    const uint8_t *ring = rd_assets_host_get(ASSET_RD_BY_RING, &n);
    TEST_ASSERT_NOT_NULL(ring);
    int smax = 0;
    for (size_t i = 12; i < n; i++) smax = ring[i] > smax ? ring[i] : smax;
    TEST_ASSERT_TRUE_MESSAGE(smax <= 184, "the ring's strip is above the mockup's .72");
    const uint16_t full = omni_blend565(omni_blend565(0, RD_C_HAZARD_DARK, 184), RD_C_HAZARD, 184);
    int fr, fg, fb;
    px8(full, &fr, &fg, &fb);
    const int bound = fr > fg ? (fr > fb ? fr : fb) : (fg > fb ? fg : fb);
    int got = 0;
    long lit_ring = 0;
    memset(s_a, 0, sizeof s_a);
    rd_by_ring_draw(r->ctx, 777, (omni_rect_t){0, 0, W, W}, s_a, W);
    for (int i = 0; i < W * W; i++) {
        int R, G, B;
        px8(s_a[i], &R, &G, &B);
        const int m = R > G ? (R > B ? R : B) : (G > B ? G : B);
        got = m > got ? m : got;
        lit_ring += m > 8;
        if (m > 0) TEST_ASSERT_TRUE_MESSAGE(by_annulus(i % W, i / W), "a ring pixel inside r 205.5");
    }
    printf("bay's ring: strip max %d (184 = .72), drawn max channel %d (the .72 composite %d; R172's ceiling 173)\n",
           smax, got, bound);
    TEST_ASSERT_TRUE(got <= 173 && got <= bound && lit_ring > 10000);   /* lit: the yellow (the dark pass alone widens to 8) */
}

/* still 7 (unset): no pip, the unset caption, paintDim dashes */
static void test_rd_by_unset(void)
{
    const row_t *r = row_of("bay");
    rd_still_frame(r->ops, r->ctx, 7, s_c);
    /* no pip: at each second's place the unset frame shows no amber, where a set frame does */
    for (int s = 0; s < 60; s++) {
        const fk_face_in_t in = by_at(10, 9, s, 400);
        frame(r, &in, s_a);
        float x, y, sn, cs;
        fk_sincos(rd_by_pip_turn(&in.clk) * 6.28318530718f, &sn, &cs);
        x = 233.f + 200.f * sn, y = 233.f - 200.f * cs;
        const int i = (int)(y + 0.5f) * W + (int)(x + 0.5f);
        int R, G, B;
        px8(s_a[i], &R, &G, &B);
        TEST_ASSERT_TRUE_MESSAGE(R > 200 && G >= 140, "the set frame's pip is not amber");
        px8(s_c[i], &R, &G, &B);
        TEST_ASSERT_TRUE_MESSAGE(G < 120, "an unset frame's pip");
    }
    /* the caption differs from the set one, and so does the time: dashes in paintDim at .8 (no paint) */
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_BY_CAPTION) > 100);
    /* where the two times differ, the unset frame's lit pixels are the dashes (paintDim at .8: 112, 105, 93, and their
     * edges) but for the few upper stars a digit of the set time covered: 95 % of them at most 150 in every channel,
     * where the set time's paint reaches past 200 */
    long dash = 0, dim = 0;
    int set_max = 0;
    for (int y = RD_BY_TIME.y0; y < RD_BY_TIME.y1; y++)
        for (int x = RD_BY_TIME.x0; x < RD_BY_TIME.x1; x++) {
            if (s_a[y * W + x] == s_c[y * W + x]) continue;
            int R, G, B;
            px8(s_c[y * W + x], &R, &G, &B);
            const int mu = R > G ? (R > B ? R : B) : (G > B ? G : B);
            px8(s_a[y * W + x], &R, &G, &B);
            const int ms = R > G ? (R > B ? R : B) : (G > B ? G : B);
            set_max = ms > set_max ? ms : set_max;
            if (mu >= 60) dash++, dim += mu <= 150;
        }
    printf("bay unset: %ld dash px, %ld of them paintDim-dim (the set time's largest channel %d)\n", dash, dim, set_max);
    TEST_ASSERT_TRUE(dash > 300 && dim * 100 >= dash * 95 && set_max >= 200);
    /* nothing moves with the clock's fields while unset */
    fk_face_in_t in = by_at(17, 33, 41, 300);
    in.clk.set = false;
    frame(r, &in, s_a);
    assert_same(s_c, s_a, "unset at another time");
}

/* still 8 (stale) differs from still 0 only inside RD_BY_STALE */
static void test_rd_by_stale_rect(void)
{
    const row_t *r = row_of("bay");
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, 8, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_BY_STALE, "stale") > 100);
}

/* the airlock's interior (Task 4 drives it; settle outCubic over 1200-1950 ms, the time's outBack drop over 1230-1700):
 * at wake 0 no label, time or pip shows; from 1950 the frame is the settled one; between 1230 and 1700 the time moves
 * by the drop */
static void time_top(const row_t *r, const fk_face_in_t *in, int *y0, unsigned *n)
{
    s_ngl = 0;
    fk_test_glyph_hook = record;
    frame(r, in, s_a);
    fk_test_glyph_hook = NULL;
    *y0 = 9999, *n = 0;
    for (unsigned g = 0; g < s_ngl && g < MAX_GLYPHS; g++)
        if (s_gl[g].y1 - s_gl[g].y0 > 60) {   /* the 132 stencil's */
            *y0 = s_gl[g].y0 < *y0 ? s_gl[g].y0 : *y0;
            (*n)++;
        }
}
static void test_rd_by_wake_interior(void)
{
    const row_t *r = row_of("bay");
    fk_face_in_t in = by_at(10, 9, 30, 500);
    in.wake_ms = 0;
    s_ngl = 0;
    fk_test_glyph_hook = record;
    frame(r, &in, s_a);
    fk_test_glyph_hook = NULL;
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, s_ngl, "a glyph at wake 0");
    float sn, cs;
    fk_sincos(rd_by_pip_turn(&in.clk) * 6.28318530718f, &sn, &cs);
    int R, G, B;
    px8(s_a[(int)(233.f - 200.f * cs + 0.5f) * W + (int)(233.f + 200.f * sn + 0.5f)], &R, &G, &B);
    TEST_ASSERT_TRUE_MESSAGE(G < 120, "the pip at wake 0");
    fk_face_in_t set = by_at(10, 9, 30, 500);
    frame(r, &set, s_c);
    static const uint32_t DONE[] = {1950, 2000, 2400};
    for (unsigned k = 0; k < 3; k++) {
        in.wake_ms = DONE[k];
        frame(r, &in, s_a);
        assert_same(s_c, s_a, "the wake's end is the settled frame");
    }
    int ys, y;
    unsigned ns, ng;
    time_top(r, &set, &ys, &ns);
    TEST_ASSERT_TRUE(ns == 5);
    for (uint32_t t = 1230; t <= 1700; t += 47) {
        in.wake_ms = t;
        time_top(r, &in, &y, &ng);
        const float ph = ((float)t - 1230.f) / 470.f;
        const float dy = (1.f - by_out_back(ph < 0 ? 0 : ph > 1 ? 1 : ph)) * -16.f;
        const int want = (int)(dy < 0 ? dy - 0.5f : dy + 0.5f);
        char m[96];
        snprintf(m, sizeof m, "wake %u ms: the time's top at %d, settled %d, the drop %+d", (unsigned)t, y, ys, want);
        if (ph * 1.6f > 0.003f) TEST_ASSERT_EQUAL_INT_MESSAGE(ys + want, y, m);
    }
}

/* the glance: the dashed hazard arc at the bottom, the star's limb, the date and the time outline; all moved by the
 * orbit; unset, the dashes as outlines */
static void test_rd_by_glance(void)
{
    const row_t *r = row_of("bay");
    fk_face_in_t g = by_at(10, 42, 3, 0);
    g.glance = true;
    frame(r, &g, s_a);
    g.odx = 3, g.ody = -2;
    frame(r, &g, s_b);
    long n = 0;
    for (int y = 10; y < W - 10; y++)
        for (int x = 10; x < W - 10; x++) {
            TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 2) * W + x + 3]);
            n += s_a[y * W + x] != 0;
        }
    TEST_ASSERT_TRUE(n > 2000);
    long arc = 0, limb = 0, top = 0;
    for (int y = 440; y < 462; y++)
        for (int x = 160; x < 306; x++) arc += s_a[y * W + x] != 0;   /* r 226 at 6 o'clock: dashes, 8 on 8 off */
    for (int y = 290; y < 302; y++)
        for (int x = 200; x < 266; x++) limb += s_a[y * W + x] != 0;  /* the limb's top at (233, 296) */
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < W; x++) top += s_a[y * W + x] != 0;       /* the arc spans .28 pi to .72 pi only */
    TEST_ASSERT_TRUE(arc > 120 && limb > 40 && top == 0);
    g.odx = g.ody = 0, g.clk.set = false;
    frame(r, &g, s_a);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_BY_TIME) > 100);
}

/* Lesson 4's worst case pinned (the Task 3 review's M1: the sanitizer runs' sweep step of 5 skips :56): Bay's glance at
 * its worst minutes, 06:56 in 24 h and 00:56 in 12 h, set and stale, at the widest date, through every orbit position,
 * with its lit share at the centre position inside the measured range */
static void test_rd_by_glance_worst(void)
{
    const row_t *r = row_of("bay");
    /* measured (set, stale): 06:56 24 h 4.31 %, 4.47 %; 00:56 12 h 3.99 %, 4.14 %; each pinned within .1 point */
    static const struct { int h, m; bool h24; float lo[2], hi[2]; } WORST[] = {
        {6, 56, true, {4.21f, 4.37f}, {4.41f, 4.57f}},
        {0, 56, false, {3.89f, 4.04f}, {4.09f, 4.24f}},
    };
    for (unsigned i = 0; i < 2; i++)
        for (int stale = 0; stale < 2; stale++) {
            fk_face_in_t in = in_at(WORST[i].h, WORST[i].m, 30, 500);
            in.glance = true, in.h24 = WORST[i].h24;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date */
            in.clk.stale = stale != 0;
            char what[48];
            snprintf(what, sizeof what, "at %02d:%02d%s (pinned worst)", WORST[i].h, WORST[i].m,
                     stale ? ", stale" : "");
            glance_orbits(r, in, what);
            const float pct = glance_pct(r, &in);
            printf("bay glance %s %s: %.2f %% lit\n", what, WORST[i].h24 ? "24 h" : "12 h", (double)pct);
            TEST_ASSERT_TRUE_MESSAGE(pct >= WORST[i].lo[stale] && pct <= WORST[i].hi[stale], what);
        }
}

/* the chime picture: the ring brightens with thrum, the star's limb warms, the pip flashes with beep */
static void test_rd_by_chime(void)
{
    const row_t *r = row_of("bay");
    fk_face_in_t a = by_at(10, 9, 30, 500), b = a;
    b.thrum = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    long ring = 0, low = 0, up = 0;
    const omni_rect_t pip = by_pip_box(&a);   /* the pip dims to .7 while the chime plays without a beep (the mockup's) */
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (s_a[y * W + x] == s_b[y * W + x] || inside(&pip, x, y)) continue;
            int R0, G0, B0, R1, G1, B1;
            px8(s_a[y * W + x], &R0, &G0, &B0);
            px8(s_b[y * W + x], &R1, &G1, &B1);
            TEST_ASSERT_TRUE_MESSAGE(R1 >= R0 && G1 >= G0 && B1 >= B0, "the chime darkens a pixel (it only adds)");
            if (by_annulus(x, y)) ring++;
            else if (y >= 215) low++;
            else up++;
        }
    TEST_ASSERT_TRUE(ring > 10000 && low > 5000 && up == 0);
    a.thrum = b.thrum = 0.2f;   /* the pip flashes on beep */
    b.beep = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    const omni_rect_t pa = by_pip_box(&a);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &pa) > 20);
    TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &pa, 1));
}

/* ---- Drift Plate (wave 2 Task 5) */
static fk_face_in_t dp_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3, in.clk.yday = 308;   /* WED 04 NOV 2026 */
    return in;
}
/* R170: Drift Plate's region with a spec burn-in motion: the band (RD_DP_BAND, drifting 1 px every 10 s), the time and
 * WASH ME over it excluded, as they do not drift */
static bool dp_moving(int x, int y)
{
    return inside(&RD_DP_BAND, x, y) && !inside(&RD_DP_TIME, x, y) && !inside(&RD_DP_WASH, x, y);
}
/* a window's centre pixel (window i: 3.4 x 5 at x 56 + 6 i, y 320) */
static int dp_win_px(int i) { return 322 * W + 56 + 6 * i + 1; }
static int max8(uint16_t p)
{
    int R, G, B;
    px8(p, &R, &G, &B);
    return R > G ? (R > B ? R : B) : (G > B ? G : B);
}

/* the ops' wiring: the brief's numbers, the rects, the glow tables (softSprite()'s profile), the face's files, and every
 * glyph inside its text rect or the band (the frame numbers ride the band) */
static void test_rd_dp_face(void)
{
    TEST_ASSERT_EQUAL_STRING("drift", RD_FACE_DRIFT.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_DRIFT.cadence_ms);
    TEST_ASSERT_TRUE(RD_FACE_DRIFT.reveal);   /* the hull pass (test_rd_pass) */
    TEST_ASSERT_EQUAL_PTR(&RD_REVEAL_PASS, RD_FACE_DRIFT.reveal_ops);
    TEST_ASSERT_FALSE(RD_FACE_DRIFT.direct);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_DRIFT.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_DRIFT.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT8(0, RD_FACE_DRIFT.stills_extra);
    TEST_ASSERT_NOT_NULL(RD_FACE_DRIFT.tap);
    TEST_ASSERT_NULL(RD_FACE_DRIFT.tap_var);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_DRIFT, rd_face_by_key("drift"));
    TEST_ASSERT_TRUE(RD_FACE_DRIFT.ctx_bytes <= 1572864u - 434312u);
    printf("drift ctx_bytes %u\n", (unsigned)RD_FACE_DRIFT.ctx_bytes);
    TEST_ASSERT_EQUAL_INT(146, RD_DP_BY);
    TEST_ASSERT_EQUAL_INT(186, RD_DP_BH);
    const omni_rect_t band = {0, 146, W, 332}, screen = {0, 0, W, W};
    TEST_ASSERT_TRUE(rect_eq(&RD_DP_BAND, &band));
    TEST_ASSERT_TRUE(rect_in(&RD_DP_TIME, &RD_DP_BAND) && rect_in(&RD_DP_WASH, &RD_DP_BAND));
    const omni_rect_t *texts[4] = {&RD_DP_STALE, &RD_DP_DATE, &RD_DP_BAND, &RD_DP_CAPTION};
    for (int k = 0; k < 4; k++) TEST_ASSERT_TRUE(rect_in(texts[k], &screen));
    TEST_ASSERT_TRUE(rect_in(&RD_DP_WINDOWS, &screen));
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(3, rd_dp_chime_rects(c));
    TEST_ASSERT_TRUE(rect_eq(&c[0], &RD_DP_WINDOWS));
    for (int k = 0; k < 3; k++) TEST_ASSERT_TRUE(rect_in(&c[k], &screen));
    /* the newest window's glow r 10 and the chime's r 180 and r 200 (coarse) in glowAt()'s profile (softSprite) */
    static uint8_t lut[FK_GLOW_LUT_MAX];
    static const int GR[3] = {10, 180, 200};
    static const int SR[] = RD_SOFT_LUT_RADII;
    for (int k = 0; k < 3; k++) {
        bool listed = false;
        for (unsigned i = 0; i < sizeof SR / sizeof SR[0]; i++) listed = listed || SR[i] == GR[k];
        TEST_ASSERT_TRUE_MESSAGE(listed, "a Drift Plate glow is not in RD_SOFT_LUT_RADII");
        TEST_ASSERT_NOT_NULL(rd_soft_lut(GR[k]));
        rd_soft_lut_build(lut, GR[k]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(lut, rd_soft_lut(GR[k]), FK_GLOW_LUT_N(GR[k]));
    }
    /* the face's own files (rd_dp_*): the brief's sizes (each with its 12-byte header), within the 500 000 B cap */
    size_t n0 = 0, n1 = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_DP_TILE, &n0));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_DP_WASH, &n1));
    TEST_ASSERT_TRUE(ASSET_RD_DP_TILE_W == 466 && ASSET_RD_DP_TILE_H == 186);
    TEST_ASSERT_TRUE(ASSET_RD_DP_WASH_W == 130 && ASSET_RD_DP_WASH_H == 34);
    TEST_ASSERT_EQUAL_UINT(173352u + 12u, (unsigned)n0);
    TEST_ASSERT_EQUAL_UINT(4420u + 12u, (unsigned)n1);
    TEST_ASSERT_EQUAL_UINT(177772u + 24u, (unsigned)(n0 + n1));
    TEST_ASSERT_TRUE(n0 + n1 <= 500000u);
    printf("drift's files %u B (cap 500 000)\n", (unsigned)(n0 + n1));
    /* every glyph lies inside one of the text rects (the band holds the frame numbers and the time): awake, 12 and 24 h
     * at lesson 10's times and the widest date, stale, unset */
    const row_t *r = row_of("drift");
    static const int HM[][2] = {{1, 0}, {9, 59}, {10, 0}, {12, 59}, {23, 59}, {0, 0}, {20, 8}};
    for (unsigned k = 0; k < sizeof HM / sizeof HM[0]; k++)
        for (int st = 0; st < 6; st++) {
            fk_face_in_t in = dp_at(HM[k][0], HM[k][1], 30, 500);
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP */
            in.h24 = st % 2 == 0, in.clk.stale = st >= 2 && st < 4, in.clk.set = st < 4;
            s_ngl = 0;
            fk_test_glyph_hook = record;
            frame(r, &in, s_a);
            fk_test_glyph_hook = NULL;
            TEST_ASSERT_TRUE(s_ngl > 3 && s_ngl <= MAX_GLYPHS);
            unsigned big = 0;
            for (unsigned g = 0; g < s_ngl; g++) {
                const omni_rect_t b = {(int16_t)s_gl[g].x0, (int16_t)s_gl[g].y0, (int16_t)s_gl[g].x1, (int16_t)s_gl[g].y1};
                const omni_rect_t bc = {(int16_t)(b.x0 < 0 ? 0 : b.x0), b.y0, (int16_t)(b.x1 > W ? W : b.x1), b.y1};
                bool in_one = false;
                for (int q = 0; q < 4 && !in_one; q++) in_one = rect_in(&bc, texts[q]);
                if (b.y1 - b.y0 > 60) {   /* the 132 stencil's: inside the time's own rect */
                    big++;
                    in_one = rect_in(&b, &RD_DP_TIME);
                }
                char m[160];
                snprintf(m, sizeof m, "drift at %02d:%02d state %d: a glyph box (%d, %d)-(%d, %d) outside the text rects",
                         HM[k][0], HM[k][1], st, b.x0, b.y0, b.x1, b.y1);
                TEST_ASSERT_TRUE_MESSAGE(in_one, m);
            }
            if (in.clk.set) TEST_ASSERT_TRUE(big >= 4);   /* the digits (the unset dashes are short) */
        }
}

/* the band's drift: (sec_of_day / 10) % 466, 1 px at each 10 s boundary (dirty names RD_DP_BAND then); within 10 s only
 * the windows change (and the time at the minute, which is a 10 s boundary too); the windows lit are s + 1, the newest
 * #FFE2A8 (the brightest: its glow only adds) */
static void test_rd_dp_drift(void)
{
    static const struct { int sod, want; } D[] = {{0, 0}, {9, 0}, {10, 1}, {4659, 465}, {4660, 0}, {36570, 3657 % 466},
                                                   {86399, 8639 % 466}};
    for (unsigned k = 0; k < sizeof D / sizeof D[0]; k++) {
        const fk_face_in_t in = dp_at(D[k].sod / 3600, D[k].sod / 60 % 60, D[k].sod % 60, 999);
        TEST_ASSERT_EQUAL_INT(D[k].want, rd_dp_drift(&in.clk));
    }
    fk_face_in_t u = dp_at(10, 9, 30, 0);
    u.clk.set = false;
    TEST_ASSERT_EQUAL_INT(0, rd_dp_drift(&u.clk));
    const row_t *r = row_of("drift");
    /* the step: 10:09:39.900 -> 10:09:40.000 moves the band's tile rows a pixel left (b(x) = a(x + 1)) */
    for (int red = 0; red < 2; red++) {
        fk_face_in_t a = dp_at(10, 9, 39, 900), b = shifted(a, 100);
        a.reduced = b.reduced = red != 0;
        TEST_ASSERT_EQUAL_INT(rd_dp_drift(&a.clk) + 1, rd_dp_drift(&b.clk));
        omni_rect_t rects[8];
        const unsigned n = dirty_pair(r, &a, &b, "the drift's step", rects);
        TEST_ASSERT_TRUE_MESSAGE(by_lists(rects, n, &RD_DP_BAND) || (n == 1 && rects[0].y1 - rects[0].y0 == W),
                                 "the drift's step without RD_DP_BAND");
        frame(r, &a, s_a);
        frame(r, &b, s_b);
        long cmp = 0;
        for (int y = RD_DP_BY + 6; y < RD_DP_BY + RD_DP_BH; y++)
            for (int x = 0; x + 1 < W; x++) {
                const int ax = 2 * x + 1 - W, bx = 2 * x + 3 - W, ay = 2 * y + 1 - W;
                if (inside(&RD_DP_TIME, x, y) || inside(&RD_DP_WASH, x, y) || inside(&RD_DP_WINDOWS, x, y) ||
                    inside(&RD_DP_TIME, x + 1, y) || inside(&RD_DP_WASH, x + 1, y) || inside(&RD_DP_WINDOWS, x + 1, y) ||
                    ax * ax + ay * ay > W * W || bx * bx + ay * ay > W * W)
                    continue;   /* on the panel only: a frame number's glyph off it is never drawn */
                if (s_b[y * W + x] != s_a[y * W + x + 1]) {
                    char m[96];
                    snprintf(m, sizeof m, "band pixel (%d, %d) did not move a pixel left", x, y);
                    TEST_FAIL_MESSAGE(m);
                }
                cmp++;
            }
        TEST_ASSERT_TRUE(cmp > 25000);
    }
    /* within 10 s: only the windows, and dirty names them alone */
    for (int s = 40; s < 49; s++) {
        const fk_face_in_t a = dp_at(10, 9, s, 500), b = shifted(a, 1000);
        omni_rect_t rects[8];
        const unsigned n = dirty_pair(r, &a, &b, "a second", rects);
        TEST_ASSERT_TRUE(n == 1 && rect_eq(&rects[0], &RD_DP_WINDOWS));
        frame(r, &a, s_a);
        frame(r, &b, s_b);
        TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_DP_WINDOWS) > 0);
        TEST_ASSERT_EQUAL_INT32(0, differ_outside(s_a, s_b, &RD_DP_WINDOWS, 1));
    }
    /* the minute: the band, the windows and the time */
    {
        const fk_face_in_t a = dp_at(10, 9, 59, 500), b = shifted(a, 1000);
        omni_rect_t rects[8];
        const unsigned n = dirty_pair(r, &a, &b, "the minute", rects);
        TEST_ASSERT_TRUE(by_lists(rects, n, &RD_DP_TIME) && by_lists(rects, n, &RD_DP_BAND));
    }
    /* the windows lit: s + 1, and the newest the brightest (#FFE2A8 under its glow) */
    for (int s = 0; s < 60; s += 7) {
        const fk_face_in_t in = dp_at(10, 9, s, 500);
        frame(r, &in, s_a);
        int lit = 0, newest = -1;
        for (int i = 0; i < 60; i++) {
            int R, G, B;
            px8(s_a[dp_win_px(i)], &R, &G, &B);
            if (R > 150) lit++, newest = i;
        }
        TEST_ASSERT_EQUAL_INT(s + 1, lit);
        int R, G, B;
        px8(s_a[dp_win_px(newest)], &R, &G, &B);
        TEST_ASSERT_TRUE_MESSAGE(R >= 0xF8 && G >= 0xE0 && B >= 0xA0, "the newest window is not #FFE2A8");
        if (newest > 0) {
            int r2, g2, b2;
            px8(s_a[dp_win_px(newest - 1)], &r2, &g2, &b2);
            TEST_ASSERT_TRUE(G > g2 && B > b2);
        }
    }
}

/* R169 (I4): the tile's area level. The notes' hull-deep token (rd.hullDeep #3E120B: "large red areas sit at this
 * level, not at rd.hull") sets the ceiling: the mean of each lit pixel's largest channel (largest > 8) over the tile is
 * at most 0x3E + 8 = 70 (hullDeep's red, plus 8 for the grime's highlights and the RGB565 widening). The peak channel
 * is reported, not gated (the top lip's #9A3524 and the portholes are thin lines, no burn-in area). Over the ceiling:
 * stop and report (R146), never dim the art to fit. The face adds nothing to the band outside the chime: away from the
 * frame numbers, the time, WASH ME and the windows, the band's rows are the tile's, drifted */
static void test_rd_dp_dimming(void)
{
    size_t n = 0;
    const uint8_t *bin = rd_assets_host_get(ASSET_RD_DP_TILE, &n);
    TEST_ASSERT_NOT_NULL(bin);
    TEST_ASSERT_EQUAL_UINT(12u + 466u * 186u * 2u, (unsigned)n);
    const uint16_t *lay = (const uint16_t *)(const void *)(bin + 12);
    long lit = 0;
    double sum = 0;
    int peak = 0;
    for (int i = 0; i < 466 * 186; i++) {
        const int m = max8(lay[i]);
        peak = m > peak ? m : peak;
        if (m > 8) lit++, sum += m;
    }
    const double mean = sum / (double)lit;
    printf("drift's tile: %ld lit px, mean largest channel %.1f (ceiling 70 = hullDeep 0x3E + 8), peak %d "
           "(ASSET_RD_DP_TILE_MAX %d)\n", lit, mean, peak, ASSET_RD_DP_TILE_MAX);
    TEST_ASSERT_TRUE_MESSAGE(mean <= 70.0, "the tile's area level is over hullDeep + 8 (R169)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(peak, ASSET_RD_DP_TILE_MAX, "rd_pack's max is not the layer's largest channel");
    for (int i = 0; i < 466 * 6; i++) TEST_ASSERT_EQUAL_HEX16(0, lay[i]);   /* the builder's transparent margin */
    const row_t *r = row_of("drift");
    static const int SOD[4] = {36570, 4655, 120, 86399};
    for (int k = 0; k < 4; k++) {
        const fk_face_in_t in = dp_at(SOD[k] / 3600, SOD[k] / 60 % 60, SOD[k] % 60, 500);
        const int d = rd_dp_drift(&in.clk);
        frame(r, &in, s_a);
        long same = 0;
        for (int y = RD_DP_BY + 6; y < RD_DP_BY + RD_DP_BH; y++)
            for (int x = 0; x < W; x++) {
                if ((y >= RD_DP_BY + 26 && y < RD_DP_BY + 46) || inside(&RD_DP_TIME, x, y) ||
                    inside(&RD_DP_WASH, x, y) || inside(&RD_DP_WINDOWS, x, y))
                    continue;
                const uint16_t want = lay[(y - RD_DP_BY) * 466 + (x + d) % 466];
                if (s_a[y * W + x] != want) {
                    char m[96];
                    snprintf(m, sizeof m, "drift %d: band pixel (%d, %d) is %04x, the tile's %04x", d, x, y,
                             s_a[y * W + x], want);
                    TEST_FAIL_MESSAGE(m);
                }
                same++;
            }
        TEST_ASSERT_TRUE(same > 20000);
    }
}

/* still 7 (unset): no window lit, the unset caption, the dashes in paintDim at .75; nothing moves with the clock */
static void test_rd_dp_unset(void)
{
    const row_t *r = row_of("drift");
    rd_still_frame(r->ops, r->ctx, 7, s_c);
    for (int i = 0; i < 60; i++) TEST_ASSERT_EQUAL_HEX16(RD_RGB(0x2A, 0x0E, 0x08), s_c[dp_win_px(i)]);
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_DP_CAPTION) > 100);
    /* the time: the set frame's paint reaches past 200 in green; the dashes (paintDim .75 over the plate) stay at
     * paintDim's green (132) at most */
    int gs = 0, gu = 0;
    long dash = 0;
    for (int y = RD_DP_TIME.y0; y < RD_DP_TIME.y1; y++)
        for (int x = RD_DP_TIME.x0; x < RD_DP_TIME.x1; x++) {
            int R, G, B;
            px8(s_a[y * W + x], &R, &G, &B);
            gs = G > gs ? G : gs;
            px8(s_c[y * W + x], &R, &G, &B);
            gu = G > gu ? G : gu;
            dash += G >= 80;
        }
    printf("drift unset: the time's greenest %d (set %d), %ld dash px\n", gu, gs, dash);
    TEST_ASSERT_TRUE(gs >= 190 && gu <= 136 && dash > 300);
    fk_face_in_t in = dp_at(17, 33, 41, 300);
    in.clk.set = false;
    frame(r, &in, s_a);
    assert_same(s_c, s_a, "unset at another time");
}

/* still 8 (stale) differs from still 0 only inside RD_DP_STALE */
static void test_rd_dp_stale_rect(void)
{
    const row_t *r = row_of("drift");
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, 8, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_DP_STALE, "stale") > 100);
}

/* the pass's interior (test_rd_pass drives it): from 2000 the frame is the settled one; mid-pass the time rides the
 * rounded offset; at the wake's start the labels and the windows are dark; no chime picture while the pass runs */
static void test_rd_dp_wake_interior(void)
{
    const row_t *r = row_of("drift");
    fk_face_in_t set = dp_at(10, 9, 30, 500), in = set;
    frame(r, &set, s_c);
    static const uint32_t DONE[] = {2000, 2050, 2400};
    for (unsigned k = 0; k < 3; k++) {
        in.wake_ms = DONE[k];
        frame(r, &in, s_a);
        assert_same(s_c, s_a, "the pass's end is the settled frame");
    }
    in.wake_ms = 0;
    frame(r, &in, s_a);
    for (int i = 0; i < 60; i++) TEST_ASSERT_EQUAL_HEX16(RD_RGB(0x2A, 0x0E, 0x08), s_a[dp_win_px(i)]);
    long cap = 0;   /* no caption pixel: where wake 0 differs from the settled frame there, it is dark */
    for (int y = RD_DP_CAPTION.y0; y < RD_DP_CAPTION.y1; y++)
        for (int x = RD_DP_CAPTION.x0; x < RD_DP_CAPTION.x1; x++)
            cap += s_a[y * W + x] != s_c[y * W + x] && max8(s_a[y * W + x]) > 40;
    TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_DP_CAPTION) > 100);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, cap, "the caption at wake 0");
    /* the time's glyphs at x + the rounded offset */
    float x_set = 1e9f;
    s_ngl = 0;
    fk_test_glyph_hook = record;
    frame(r, &set, s_a);
    fk_test_glyph_hook = NULL;
    for (unsigned g = 0; g < s_ngl; g++)
        if (s_gl[g].y1 - s_gl[g].y0 > 60 && s_gl[g].x0 < x_set) x_set = (float)s_gl[g].x0;
    static const uint32_t T[] = {1200, 1400, 1500, 1600, 1700};
    for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) {
        rd_pass_state_t ps;
        rd_pass_at(T[k], false, &ps);
        const int off = (int)lroundf(ps.off);
        in.wake_ms = T[k];
        float x0 = 1e9f;
        s_ngl = 0;
        fk_test_glyph_hook = record;
        frame(r, &in, s_a);
        fk_test_glyph_hook = NULL;
        for (unsigned g = 0; g < s_ngl; g++)
            if (s_gl[g].y1 - s_gl[g].y0 > 60 && s_gl[g].x0 < x0) x0 = (float)s_gl[g].x0;
        char m[80];
        snprintf(m, sizeof m, "wake %u: the time's left edge %.0f, settled %.0f, offset %d", (unsigned)T[k],
                 (double)x0, (double)x_set, off);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)x_set + off, (int)x0, m);
        fk_face_in_t ch = in;   /* no chime picture mid-pass */
        ch.thrum = 0.8f, ch.beep = 1.f;
        frame(r, &ch, s_b);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(0, frames_differ(s_a, s_b), "a chime picture while the pass runs");
    }
}

/* the glance: the two hairlines, the 12 ticks (none unset), the date and the time outline; all moved by the orbit */
static void test_rd_dp_glance(void)
{
    const row_t *r = row_of("drift");
    fk_face_in_t g = dp_at(10, 42, 3, 0);
    g.glance = true;
    frame(r, &g, s_a);
    TEST_ASSERT_EQUAL_HEX16(0, s_a[200 * W + 8]);                 /* off the hairline's ends */
    TEST_ASSERT_TRUE(s_a[163 * W + 233] != 0 && s_a[316 * W + 233] != 0);   /* the hairlines at BY + 17, BY + BH - 16 */
    for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL_HEX16(RD_G_AMBER, s_a[321 * W + 57 + 30 * i]);
    for (int i = 0; i < W * W; i++) TEST_ASSERT_TRUE(max8(s_a[i]) <= 110);
    g.odx = 3, g.ody = -2;
    frame(r, &g, s_b);
    for (int y = 10; y < W - 10; y++)
        for (int x = 10; x < W - 10; x++) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 2) * W + x + 3]);
    g.odx = g.ody = 0, g.clk.set = false;
    frame(r, &g, s_b);
    for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL_HEX16(0, s_b[321 * W + 57 + 30 * i]);
}

/* the glance's worst minutes, pinned (the Task 3 review's M1: the sanitizer sweep's step skips most minutes) */
static void test_rd_dp_glance_worst(void)
{
    const row_t *r = row_of("drift");
    /* measured (set, stale): 06:56 24 h 4.59 %, 4.75 %; 00:56 12 h 4.27 %, 4.42 %; each pinned within .1 point */
    static const struct { int h, m; bool h24; float lo[2], hi[2]; } WORST[] = {
        {6, 56, true, {4.49f, 4.65f}, {4.69f, 4.85f}},
        {0, 56, false, {4.17f, 4.32f}, {4.37f, 4.52f}},
    };
    for (unsigned i = 0; i < 2; i++)
        for (int stale = 0; stale < 2; stale++) {
            fk_face_in_t in = in_at(WORST[i].h, WORST[i].m, 30, 500);
            in.glance = true, in.h24 = WORST[i].h24;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date */
            in.clk.stale = stale != 0;
            char what[48];
            snprintf(what, sizeof what, "at %02d:%02d%s (pinned worst)", WORST[i].h, WORST[i].m,
                     stale ? ", stale" : "");
            glance_orbits(r, in, what);
            const float pct = glance_pct(r, &in);
            printf("drift glance %s %s: %.2f %% lit\n", what, WORST[i].h24 ? "24 h" : "12 h", (double)pct);
            TEST_ASSERT_TRUE_MESSAGE(pct >= WORST[i].lo[stale] && pct <= WORST[i].hi[stale], what);
        }
}

/* the chime picture: with thrum .7 the unlit windows light outward from the centre (.7 clamp(1.4 thrum - |i - 30| /
 * 40)); with beep 1 the warm flash (r 200 about (233, BY + BH - 9)) shows; the time's glow with thrum; it only adds or
 * lights; none under reduced motion (chime_regions) */
static void test_rd_dp_chime(void)
{
    const row_t *r = row_of("drift");
    fk_face_in_t a = dp_at(10, 9, 5, 500), b = a;   /* windows 0-5 lit */
    b.thrum = 0.7f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_HEX16(s_a[dp_win_px(i)], s_b[dp_win_px(i)]);
    int prev = 999;
    for (int i = 30; i < 60; i += 5) {   /* from the centre outward: dimmer each step */
        const int m = max8(s_b[dp_win_px(i)]);
        TEST_ASSERT_TRUE_MESSAGE(m < prev, "the chime's windows do not fall off from the centre");
        TEST_ASSERT_TRUE(m > max8(s_a[dp_win_px(i)]));
        prev = m;
    }
    TEST_ASSERT_TRUE(max8(s_b[dp_win_px(30)]) > max8(s_b[dp_win_px(10)]));
    /* the time's glow: (233, 236) brightens */
    TEST_ASSERT_TRUE(max8(s_b[236 * W + 120]) >= max8(s_a[236 * W + 120]));
    const long up = differ_in(s_a, s_b, &(omni_rect_t){53, 56, 414, 146});   /* above the band: the glow's rim */
    printf("drift chime: the time's glow changes %ld px above the band\n", up);
    TEST_ASSERT_TRUE(up > 50);
    /* the flash on beep: below the band, under the strip */
    a.thrum = b.thrum = 0.f;
    b.beep = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    const omni_rect_t below = {120, 336, 346, 420};
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &below) > 2000);
    for (int i = 0; i < W * W; i++) {
        int R0, G0, B0, R1, G1, B1;
        px8(s_a[i], &R0, &G0, &B0);
        px8(s_b[i], &R1, &G1, &B1);
        TEST_ASSERT_TRUE_MESSAGE(R1 >= R0 && G1 >= G0 && B1 >= B0, "the flash darkens a pixel (it only adds)");
    }
}

/* ---- Drive Console (wave 2 Task 6) */
static fk_face_in_t dc_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3, in.clk.yday = 308;   /* WED 04 NOV 2026 */
    in.battery = 72, in.sync_age_s = 754;
    return in;
}
static bool dc_in_any(const omni_rect_t *q, const omni_rect_t *const *boxes, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (rect_in(q, boxes[i])) return true;
    return false;
}

/* the strip's two lines (shared with Nav Scope's page 4) */
static void test_rd_sync_str(void)
{
    static const struct { int32_t age; const char *want; } T[] = {
        {-1, "SYNC NEVER"},         {0, "SYNC 0 MIN AGO"},       {754, "SYNC 12 MIN AGO"},
        {5999, "SYNC 99 MIN AGO"},  {6000, "SYNC 1 H AGO"},      {359999, "SYNC 99 H AGO"},
        {360000, "SYNC 4 D AGO"},   {90000, "SYNC 25 H AGO"},    {INT32_MAX, "SYNC 24855 D AGO"},
        {-7, "SYNC NEVER"},
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        char out[24];
        memset(out, 'x', sizeof out);
        rd_sync_str(T[i].age, out);
        TEST_ASSERT_EQUAL_STRING(T[i].want, out);
    }
}

static void test_rd_reserve_str(void)
{
    static const struct { int8_t b; const char *want; } T[] = {
        {72, "RESERVE 72%"}, {-1, "RESERVE --%"}, {0, "RESERVE 0%"}, {100, "RESERVE 100%"}, {-100, "RESERVE --%"},
        {127, "RESERVE 100%"},
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        char out[16];
        memset(out, 'x', sizeof out);
        rd_reserve_str(T[i].b, out);
        TEST_ASSERT_EQUAL_STRING(T[i].want, out);
    }
}

/* lesson 10 / R154 on the seven-segment readout: 9:59 in 12 h has a blank leading digit that keeps its ghost 8; 10:00
 * lights it; 24 h keeps the leading zero; unset is "----" */
static void test_rd_seg7_12h(void)
{
    float xs[4];
    rd_seg7_xs(233.f, 96.f, xs);
    TEST_ASSERT_EQUAL_FLOAT(101.f, xs[0]);   /* the mockup's digitXs() */
    TEST_ASSERT_EQUAL_FLOAT(165.f, xs[1]);
    TEST_ASSERT_EQUAL_FLOAT(251.f, xs[2]);
    TEST_ASSERT_EQUAL_FLOAT(315.f, xs[3]);
    char t[5];
    fk_face_in_t in = in_at(9, 59, 30, 500);
    in.h24 = false;
    rd_seg7_str(&in, t);
    TEST_ASSERT_EQUAL_STRING(" 959", t);
    in.h24 = true;
    rd_seg7_str(&in, t);
    TEST_ASSERT_EQUAL_STRING("0959", t);
    in.clk.h = 21, in.h24 = false;
    rd_seg7_str(&in, t);
    TEST_ASSERT_EQUAL_STRING(" 959", t);
    in.clk.h = 0;
    rd_seg7_str(&in, t);
    TEST_ASSERT_EQUAL_STRING("1259", t);
    in.clk.set = false;
    rd_seg7_str(&in, t);
    TEST_ASSERT_EQUAL_STRING("----", t);
    TEST_ASSERT_EQUAL_HEX(0x00, rd_seg7_segs(' '));
    TEST_ASSERT_EQUAL_HEX(RD_SEG7_G, rd_seg7_segs('-'));
    TEST_ASSERT_EQUAL_HEX(RD_SEG7_ALL, rd_seg7_segs('8'));
    TEST_ASSERT_EQUAL_HEX(0x06, rd_seg7_segs('1'));   /* b and c */
    const uint16_t ON = RD_C_AMBER, GH = RD_RGB(0x40, 0x30, 0x10);
    for (int k = 0; k < 2; k++) {
        fk_face_in_t q = in_at(k ? 10 : 9, k ? 0 : 59, 30, 500);
        q.h24 = false;
        memset(s_a, 0, sizeof s_a);
        rd_seg7_time(s_a, W, NULL, &q, 233.f, 161.f, 96.f, ON, GH, false, false);
        const unsigned lit = rd_seg7_segs(k ? '1' : ' ');
        for (int sg = 0; sg < 7; sg++) {   /* each segment's middle pixel: on where lit, the ghost where not */
            omni_pt_t p[6];
            rd_seg7_poly(sg, xs[0], 161.f, 96.f, p);
            float cx = 0, cy = 0;
            for (int i = 0; i < 6; i++) cx += p[i].x / 6.f, cy += p[i].y / 6.f;
            const uint16_t px = s_a[(int)cy * W + (int)cx];
            TEST_ASSERT_EQUAL_HEX16_MESSAGE((lit >> sg) & 1u ? ON : GH, px, k ? "10:00's leading 1" : "9:59's ghost 8");
        }
        long on = 0;   /* no lit pixel in the blank digit's box */
        const int x0 = (int)xs[0] - 2, x1 = (int)(xs[0] + rd_seg7_width(96.f) + rd_seg7_slant(96.f)) + 2;
        for (int y = 158; y < 260; y++)
            for (int x = x0; x < x1; x++) on += s_a[y * W + x] == ON;
        if (!k) TEST_ASSERT_EQUAL_INT32_MESSAGE(0, on, "9:59 in 12 h lights a pixel of its blank leading digit");
        else TEST_ASSERT_TRUE(on > 300);
    }
}

/* the ops' wiring: the brief's numbers, the rects, the lamps' glow table, the face's files, the panel's ramp, and every
 * glyph inside a text rect */
static void test_rd_dc_face(void)
{
    TEST_ASSERT_EQUAL_STRING("console", RD_FACE_CONSOLE.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_CONSOLE.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(900, RD_FACE_CONSOLE.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(3200, RD_FACE_CONSOLE.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_CONSOLE.pulse_guard_ms);   /* every press restarts the messages */
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_CONSOLE.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_CONSOLE.reveal);
    TEST_ASSERT_FALSE(RD_FACE_CONSOLE.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_CONSOLE.tap);
    TEST_ASSERT_NOT_NULL(RD_FACE_CONSOLE.tap_var);
    TEST_ASSERT_EQUAL_UINT16(1, RD_DC_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_DC_TAPPED_VARIANT, rd_still_variant(&RD_FACE_CONSOLE));
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_CONSOLE, rd_face_by_key("console"));
    TEST_ASSERT_TRUE(RD_FACE_CONSOLE.ctx_bytes <= 1572864u - 434312u);
    printf("console ctx_bytes %u\n", (unsigned)RD_FACE_CONSOLE.ctx_bytes);
    /* the rects: the brief's sizes where it gives them, each about its element's centre */
    TEST_ASSERT_TRUE(RD_DC_SECG.x1 - RD_DC_SECG.x0 == 112 && RD_DC_SECG.y1 - RD_DC_SECG.y0 == 112);
    TEST_ASSERT_TRUE(RD_DC_SECG.x0 + 56 == 134 && RD_DC_SECG.y0 + 56 == 352);
    TEST_ASSERT_TRUE(RD_DC_RESG.x1 - RD_DC_RESG.x0 == 112 && RD_DC_RESG.x0 + 56 == 332 && RD_DC_RESG.y0 + 56 == 352);
    TEST_ASSERT_TRUE(RD_DC_COLLAR.x1 - RD_DC_COLLAR.x0 == 64 && RD_DC_COLLAR.y1 - RD_DC_COLLAR.y0 == 64);
    TEST_ASSERT_TRUE(RD_DC_COLLAR.x0 + 32 == RD_DC_BTN_X && RD_DC_COLLAR.y0 + 32 == RD_DC_BTN_Y);
    TEST_ASSERT_TRUE(RD_DC_BTN_HIT.x1 - RD_DC_BTN_HIT.x0 == 2 * RD_DC_BTN_R);
    TEST_ASSERT_TRUE(RD_DC_BTN_HIT.y1 - RD_DC_BTN_HIT.y0 == 2 * RD_DC_BTN_R);
    TEST_ASSERT_TRUE((RD_DC_BTN_HIT.x0 + RD_DC_BTN_HIT.x1) / 2 == RD_DC_BTN_X);
    TEST_ASSERT_TRUE((RD_DC_BTN_HIT.y0 + RD_DC_BTN_HIT.y1) / 2 == RD_DC_BTN_Y);
    TEST_ASSERT_TRUE(rect_in(&RD_DC_BTN_BOX, &RD_DC_COLLAR) && rect_in(&RD_DC_COLON, &RD_DC_TIME));
    const int sx = (RD_DC_STRIP.x0 + RD_DC_STRIP.x1) / 2, sy = (RD_DC_STRIP.y0 + RD_DC_STRIP.y1) / 2;
    TEST_ASSERT_TRUE_MESSAGE((sx - RD_DC_BTN_X) * (sx - RD_DC_BTN_X) + (sy - RD_DC_BTN_Y) * (sy - RD_DC_BTN_Y) >
                                 RD_DC_BTN_R * RD_DC_BTN_R, "the strip's centre lies in the button's circle");
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(2, rd_dc_chime_rects(c));
    TEST_ASSERT_TRUE(rect_eq(&c[0], &RD_DC_TIME) && rect_eq(&c[1], &RD_DC_LAMPS));
    /* the lamps' glow: lamp()'s r x 2.6 at r 6.5, the linear table r 17 */
    static const int LR[] = RD_LIN_LUT_RADII;
    bool listed = false;
    for (unsigned i = 0; i < sizeof LR / sizeof LR[0]; i++) listed = listed || LR[i] == 17;
    TEST_ASSERT_TRUE_MESSAGE(listed, "the lamps' glow (17) is not in RD_LIN_LUT_RADII");
    static uint8_t lut[FK_GLOW_LUT_MAX];
    rd_lin_lut_build(lut, 17);
    TEST_ASSERT_NOT_NULL(rd_glow_lut(17));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(lut, rd_glow_lut(17), FK_GLOW_LUT_N(17));
    /* the panel's ramp is the table row's */
    ramp_row("rd_dc_panel", RD_DC_RAMP, RD_DC_RAMP_STOPS);
    /* the face's own files (rd_dc_*), each with its 12-byte header, within the 500 000 B cap */
    size_t n0 = 0, n1 = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_DC_PANEL, &n0));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_DC_CUTS, &n1));
    TEST_ASSERT_TRUE(ASSET_RD_DC_PANEL_W == 406 && ASSET_RD_DC_PANEL_H == 390);
    TEST_ASSERT_TRUE(ASSET_RD_DC_CUTS_W == 124 && ASSET_RD_DC_CUTS_H == 40);
    TEST_ASSERT_EQUAL_UINT(406u * 390u + 12u, (unsigned)n0);
    TEST_ASSERT_EQUAL_UINT(124u * 40u * 2u + 12u, (unsigned)n1);
    TEST_ASSERT_TRUE(n0 + n1 <= 500000u);
    printf("console's files %u B (cap 500 000)\n", (unsigned)(n0 + n1));
    /* every glyph lies inside one of the text rects: awake, 12 and 24 h at lesson 10's times and the widest date, each
     * page, the messages, stale, unset, and the glance's date */
    const row_t *r = row_of("console");
    const omni_rect_t glance_date = {150, 280, 316, 302};
    const omni_rect_t *texts[3] = {&RD_DC_STALE, &RD_DC_STRIP, &glance_date};
    static const int HM[][2] = {{1, 0}, {9, 59}, {10, 0}, {12, 59}, {23, 59}, {0, 0}, {20, 8}};
    for (unsigned k = 0; k < sizeof HM / sizeof HM[0]; k++)
        for (int st = 0; st < 6; st++)
            for (int v = 0; v < 5; v++) {
                fk_face_in_t in = dc_at(HM[k][0], HM[k][1], 30, 500);
                in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP */
                in.h24 = st % 2 == 0, in.clk.stale = st >= 2 && st < 4, in.clk.set = st < 4;
                in.variant = (uint16_t)(v % 3), in.battery = 100, in.sync_age_s = 359999;
                if (v == 3) in.pulsing = true, in.pulse_ms = 100;
                if (v == 4) in.glance = true;
                s_ngl = 0;
                fk_test_glyph_hook = record;
                frame(r, &in, s_a);
                fk_test_glyph_hook = NULL;
                TEST_ASSERT_TRUE(s_ngl <= MAX_GLYPHS);
                for (unsigned g = 0; g < s_ngl; g++) {
                    const omni_rect_t b = {(int16_t)s_gl[g].x0, (int16_t)s_gl[g].y0, (int16_t)s_gl[g].x1,
                                           (int16_t)s_gl[g].y1};
                    char m[160];
                    snprintf(m, sizeof m, "console at %02d:%02d state %d v %d: a glyph box (%d, %d)-(%d, %d) outside "
                             "the text rects", HM[k][0], HM[k][1], st, v, b.x0, b.y0, b.x1, b.y1);
                    TEST_ASSERT_TRUE_MESSAGE(dc_in_any(&b, texts, v == 4 ? 3 : 2), m);
                }
            }
}

/* the button (M10, I3): a tap inside the r 34 circle starts the pulse and leaves the page; the strip reads LOGGED at
 * 100 ms, IGNORED at 1 600 ms and the page again at 3 300 ms (the pulse ended); the button's box changes during the
 * press; exactly r 34 starts it, r 35 turns the page instead */
static void test_rd_dc_button(void)
{
    const row_t *r = row_of("console");
    fk_face_in_t in = dc_at(10, 9, 30, 500);
    uint16_t v = 0;
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, RD_DC_BTN_X - 10, RD_DC_BTN_Y + 12, &v));
    TEST_ASSERT_EQUAL_UINT16(0, v);
    char t[32];
    in.pulsing = true, in.pulse_ms = 0;
    fk_face_in_t p = in;
    fk_face_in_advance(r->ops, &p, 100);
    TEST_ASSERT_TRUE(rd_dc_strip(&p, t));
    TEST_ASSERT_EQUAL_STRING("[ REQUEST LOGGED ]", t);
    p = in;
    fk_face_in_advance(r->ops, &p, 1600);
    TEST_ASSERT_TRUE(rd_dc_strip(&p, t));
    TEST_ASSERT_EQUAL_STRING("[ REQUEST IGNORED ]", t);
    p = in;
    fk_face_in_advance(r->ops, &p, 3300);
    TEST_ASSERT_FALSE(p.pulsing);
    TEST_ASSERT_FALSE(rd_dc_strip(&p, t));
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV", t);
    /* the press: the button's box and the strip change, nothing else */
    fk_face_in_t rest = dc_at(10, 9, 30, 500), press = rest;
    press.pulsing = true, press.pulse_ms = 100;
    frame(r, &rest, s_a);
    frame(r, &press, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_DC_BTN_BOX) > 200);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_DC_STRIP) > 200);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (s_a[y * W + x] != s_b[y * W + x])
                TEST_ASSERT_TRUE_MESSAGE(inside(&RD_DC_BTN_BOX, x, y) || inside(&RD_DC_STRIP, x, y),
                                         "the press changes a pixel outside the button and the strip");
    press.pulse_ms = 700;   /* risen again by 550 ms */
    frame(r, &press, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_DC_BTN_BOX));
    /* the edge: exactly r 34 (each compass point) starts the pulse; r 35 turns the page */
    static const int CP[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (int k = 0; k < 4; k++) {
        v = 0;
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &rest, (int16_t)(RD_DC_BTN_X + 34 * CP[k][0]),
                                                            (int16_t)(RD_DC_BTN_Y + 34 * CP[k][1]), &v));
        TEST_ASSERT_EQUAL_UINT16(0, v);
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap(r->ctx, (int16_t)(RD_DC_BTN_X + 34 * CP[k][0]),
                                                        (int16_t)(RD_DC_BTN_Y + 34 * CP[k][1])));
        TEST_ASSERT_EQUAL_INT(FK_TAP_NEXT, r->ops->tap_var(r->ctx, &rest, (int16_t)(RD_DC_BTN_X + 35 * CP[k][0]),
                                                           (int16_t)(RD_DC_BTN_Y + 35 * CP[k][1]), &v));
        TEST_ASSERT_EQUAL_UINT16(1, v);
    }
    /* the pages cycle 0 -> 1 -> 2 -> 0; unset, between the two lines */
    v = 2;
    TEST_ASSERT_EQUAL_INT(FK_TAP_NEXT, r->ops->tap_var(r->ctx, &rest, 40, 233, &v));
    TEST_ASSERT_EQUAL_UINT16(0, v);
    fk_face_in_t un = rest;
    un.clk.set = false;
    v = 0;
    r->ops->tap_var(r->ctx, &un, 40, 233, &v);
    TEST_ASSERT_EQUAL_UINT16(1, v);
    r->ops->tap_var(r->ctx, &un, 40, 233, &v);
    TEST_ASSERT_EQUAL_UINT16(0, v);
    /* a NULL in (a caller outside the runtime, fk/face.h): a set clock */
    v = 0;
    TEST_ASSERT_EQUAL_INT(FK_TAP_NEXT, r->ops->tap_var(r->ctx, NULL, 40, 233, &v));
    TEST_ASSERT_EQUAL_UINT16(1, v);
}

/* the strip's pages: the date (12 h: its AM/PM suffix), RESERVE, the last sync; stale shows them too */
static void test_rd_dc_strip(void)
{
    char t[32];
    fk_face_in_t in = dc_at(10, 9, 30, 500);
    TEST_ASSERT_FALSE(rd_dc_strip(&in, t));
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV", t);
    in.h24 = false;
    rd_dc_strip(&in, t);
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV \267 AM", t);
    in.variant = 1;
    rd_dc_strip(&in, t);
    TEST_ASSERT_EQUAL_STRING("RESERVE 72%", t);
    in.variant = 2;
    rd_dc_strip(&in, t);
    TEST_ASSERT_EQUAL_STRING("SYNC 12 MIN AGO", t);
    in.clk.stale = true, in.sync_age_s = 90000;
    rd_dc_strip(&in, t);
    TEST_ASSERT_EQUAL_STRING("SYNC 25 H AGO", t);
    in.variant = 5;   /* mod 3 */
    rd_dc_strip(&in, t);
    TEST_ASSERT_EQUAL_STRING("SYNC 25 H AGO", t);
    in.clk.set = false;   /* unset: never "SYNC 0 MIN AGO" (M9): the face's unset lines */
    in.sync_age_s = 0;
    for (uint16_t v = 0; v < 6; v++) {
        in.variant = v;
        rd_dc_strip(&in, t);
        TEST_ASSERT_EQUAL_STRING(v % 2 ? "--- -- ---" : "[ AWAITING TIME SIGNAL ]", t);
    }
    /* the stills: row 0's sync on the tapped still's next page; the stale still's (M9) */
    rd_still_t st;
    rd_still(0, &st);
    TEST_ASSERT_EQUAL_INT32(754, st.in.sync_age_s);
    rd_still(8, &st);
    TEST_ASSERT_EQUAL_INT32(90000, st.in.sync_age_s);
    st.in.variant = 2;
    rd_dc_strip(&st.in, t);
    TEST_ASSERT_EQUAL_STRING("SYNC 25 H AGO", t);
    for (unsigned i = 1; i < RD_STILL_N; i++) {
        if (i == 8) continue;
        rd_still(i, &st);
        TEST_ASSERT_EQUAL_INT32(0, st.in.sync_age_s);
    }
}

/* lesson 1 within a second: at rest, every dirty rect lies inside the lamps, the SEC gauge, the colon or the collar */
static void test_rd_dc_rest_dirty(void)
{
    const row_t *r = row_of("console");
    const omni_rect_t *const ok[4] = {&RD_DC_LAMPS, &RD_DC_SECG, &RD_DC_COLON, &RD_DC_COLLAR};
    static const int S[] = {0, 1, 7, 30, 59};
    unsigned seen_sec = 0, seen_colon = 0, seen_lamps = 0;
    for (unsigned k = 0; k < sizeof S / sizeof S[0]; k++)
        for (int st = 0; st < 4; st++)
            for (int ms = 0; ms + 40 < 1000; ms += 40) {
                fk_face_in_t a = dc_at(10, 9, S[k], ms);
                a.h24 = st != 1, a.clk.stale = st == 2, a.reduced = st == 3, a.variant = (uint16_t)(k % 3);
                a.mono_ms = 77000u + (uint32_t)ms;
                const fk_face_in_t b = ran(shifted(a, 40), 40);
                omni_rect_t rects[8];
                const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
                for (unsigned q = 0; q < n; q++) {
                    char m[128];
                    snprintf(m, sizeof m, "console at :%02d.%03d state %d: a dirty rect (%d, %d)-(%d, %d) outside the "
                             "rest regions", S[k], ms, st, rects[q].x0, rects[q].y0, rects[q].x1, rects[q].y1);
                    TEST_ASSERT_TRUE_MESSAGE(dc_in_any(&rects[q], ok, 4), m);
                    seen_sec += rect_eq(&rects[q], &RD_DC_SECG);
                    seen_colon += rect_eq(&rects[q], &RD_DC_COLON);
                    seen_lamps += rect_eq(&rects[q], &RD_DC_LAMPS);
                }
                dirty_pair(r, &a, &b, "console at rest", NULL);
            }
    TEST_ASSERT_TRUE(seen_sec > 0 && seen_colon > 0 && seen_lamps > 0);
    /* the second's boundary moves the collar a stripe pixel (awake, not reduced); reduced, only the minute does */
    fk_face_in_t a = dc_at(10, 9, 30, 960), b = ran(shifted(a, 80), 80);
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_DC_COLLAR) > 20);
    a.reduced = b.reduced = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_DC_COLLAR));
    a = dc_at(10, 9, 59, 960), a.reduced = true, b = ran(shifted(a, 80), 80);
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_DC_COLLAR) > 20);
}

/* unset (still 7): --:-- on the middle segments in paintDim, a static colon, the strip's two lines by the variant, only
 * NAV breathing (over mono_ms), the SEC needle dark at 0 */
static void test_rd_dc_unset(void)
{
    const row_t *r = row_of("console");
    rd_still_t st;
    rd_still(7, &st);
    char t[32];
    rd_dc_digits(&st.in, t);
    TEST_ASSERT_EQUAL_STRING("----", t);
    rd_dc_strip(&st.in, t);
    TEST_ASSERT_EQUAL_STRING("[ AWAITING TIME SIGNAL ]", t);
    TEST_ASSERT_EQUAL_FLOAT(0.f, rd_dc_sec(&st.in));
    float xs[4];
    rd_seg7_xs(233.f, 96.f, xs);
    rd_still_frame(r->ops, r->ctx, 7, s_a);
    for (int i = 0; i < 4; i++) {   /* g in paintDim; a shows the panel's ghost */
        omni_pt_t p[6];
        rd_seg7_poly(6, xs[i], 161.f, 96.f, p);
        float cx = 0, cy = 0;
        for (int j = 0; j < 6; j++) cx += p[j].x / 6.f, cy += p[j].y / 6.f;
        TEST_ASSERT_EQUAL_HEX16(RD_C_PAINT_DIM, s_a[(int)cy * W + (int)cx]);
        rd_seg7_poly(0, xs[i], 161.f, 96.f, p);
        cx = cy = 0;
        for (int j = 0; j < 6; j++) cx += p[j].x / 6.f, cy += p[j].y / 6.f;
        TEST_ASSERT_TRUE(max8(s_a[(int)cy * W + (int)cx]) < 40);
    }
    float nav_lo = 2.f, nav_hi = -1.f;
    for (uint32_t mono = 0; mono < 3000; mono += 50) {
        fk_face_in_t in = st.in;
        in.mono_ms = mono;
        for (int i = 0; i < 6; i++) {
            const float l = rd_dc_lamp(&in, i);
            if (i != 2) TEST_ASSERT_EQUAL_FLOAT(0.f, l);
        }
        const float n = rd_dc_lamp(&in, 2);
        nav_lo = n < nav_lo ? n : nav_lo, nav_hi = n > nav_hi ? n : nav_hi;
        TEST_ASSERT_EQUAL_FLOAT(0.f, rd_dc_sec(&in));
        frame(r, &in, s_b);   /* the colon static, the needle at rest, the digits held */
        TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_DC_TIME));
        TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_DC_SECG));
    }
    TEST_ASSERT_TRUE(nav_lo < 0.1f && nav_hi > 0.9f);
    st.in.variant = 1;
    rd_dc_strip(&st.in, t);
    TEST_ASSERT_EQUAL_STRING("--- -- ---", t);
    /* the needle's colour: steel, not amber, at rest on 0 (GA0: down-left of the hub) */
    long amber = 0;
    for (int y = RD_DC_SECG.y0; y < RD_DC_SECG.y1; y++)
        for (int x = RD_DC_SECG.x0; x < RD_DC_SECG.x1; x++) amber += s_a[y * W + x] == RD_C_AMBER;
    TEST_ASSERT_EQUAL_INT32(0, amber);
}

/* the lamps: COMMS is never lit outside the chime; under reduced motion NAV and AFT hold still; awake NAV breathes and
 * AFT flickers; the wake pops them left to right */
static void test_rd_dc_lamps(void)
{
    float nav_lo = 2.f, nav_hi = -1.f;
    unsigned aft_low = 0, n = 0;
    for (int st = 0; st < 4; st++)
        for (long t = 0; t < 120000; t += 37) {
            fk_face_in_t in = shifted(dc_at(10, 9, 0, 0), t);
            in.clk.stale = st == 1, in.clk.set = st != 2, in.mono_ms = (uint32_t)t;
            if (st == 3) in.wake_ms = (uint32_t)(t % 900);
            TEST_ASSERT_EQUAL_FLOAT(0.f, rd_dc_lamp(&in, 5));
            in.reduced = true;
            TEST_ASSERT_EQUAL_FLOAT(0.f, rd_dc_lamp(&in, 5));
            if (st < 2) {
                TEST_ASSERT_EQUAL_FLOAT(0.55f, rd_dc_lamp(&in, 2));
                TEST_ASSERT_EQUAL_FLOAT(0.95f, rd_dc_lamp(&in, 4));
            }
            in.reduced = false;
            if (st < 2) {
                const float nv = rd_dc_lamp(&in, 2), af = rd_dc_lamp(&in, 4);
                nav_lo = nv < nav_lo ? nv : nav_lo, nav_hi = nv > nav_hi ? nv : nav_hi;
                aft_low += af < 0.5f;
                n++;
            }
            in.beep = 1.f;   /* the chime's beep lights every lamp, COMMS too */
            if (st != 2) TEST_ASSERT_EQUAL_FLOAT(1.f, rd_dc_lamp(&in, 5));
        }
    TEST_ASSERT_TRUE(nav_lo < 0.15f && nav_hi > 0.95f);
    TEST_ASSERT_TRUE(aft_low > 0 && aft_low < n / 10);   /* a loose filament: rarely low */
    fk_face_in_t w = dc_at(10, 9, 30, 500);
    w.wake_ms = 100;   /* lamp 0 from 60 ms, lamp 1 from 130 */
    TEST_ASSERT_TRUE(rd_dc_lamp(&w, 0) > 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.f, rd_dc_lamp(&w, 1));
    w.wake_ms = 500;
    for (int i = 0; i < 5; i++) TEST_ASSERT_TRUE(rd_dc_lamp(&w, i) > 0.f);
}

/* the wake: blanks to 150 ms, 88:88 to 450, then the time; both needles sweep up and fall back; settled at 900 */
static void test_rd_dc_wake(void)
{
    const row_t *r = row_of("console");
    fk_face_in_t in = dc_at(10, 9, 30, 500);
    char t[5];
    static const struct { uint32_t w; const char *d; } D[] = {
        {0, "    "}, {150, "    "}, {151, "8888"}, {449, "8888"}, {450, "1009"}, {899, "1009"}};
    for (unsigned k = 0; k < sizeof D / sizeof D[0]; k++) {
        in.wake_ms = D[k].w;
        rd_dc_digits(&in, t);
        TEST_ASSERT_EQUAL_STRING(D[k].d, t);
    }
    in.wake_ms = UINT32_MAX;
    const float sec0 = rd_dc_sec(&in), res0 = rd_dc_res(&in);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.72f, res0);
    in.wake_ms = 450;
    TEST_ASSERT_TRUE(rd_dc_sec(&in) > 0.9f && rd_dc_res(&in) > 1.f);
    TEST_ASSERT_TRUE(rd_dc_res(&in) <= 1.02f);
    in.wake_ms = UINT32_MAX;
    frame(r, &in, s_a);
    fk_face_in_t e = in;
    e.wake_ms = 899;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sec0, rd_dc_sec(&e));
    in.reduced = true;   /* reduced: the runtime gives no wake; the face's frame never swings */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 30.f / 60.f, rd_dc_sec(&in));
}

/* the chime picture: the digits breathe down to 60 % with the thrum, every lamp lights on the beep */
static void test_rd_dc_chime(void)
{
    const row_t *r = row_of("console");
    fk_face_in_t a = dc_at(10, 9, 30, 500), b = a;
    b.thrum = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    float xs[4];
    rd_seg7_xs(233.f, 96.f, xs);
    omni_pt_t p[6];
    rd_seg7_poly(1, xs[1], 161.f, 96.f, p);   /* the 0's b segment */
    float cx = 0, cy = 0;
    for (int j = 0; j < 6; j++) cx += p[j].x / 6.f, cy += p[j].y / 6.f;
    const int i0 = (int)cy * W + (int)cx;
    TEST_ASSERT_EQUAL_HEX16(RD_C_AMBER, s_a[i0]);
    int R, G, B;
    px8(s_b[i0], &R, &G, &B);
    TEST_ASSERT_INT_WITHIN(10, 153, R);   /* .6 x 255 */
    b = a, b.beep = 1.f;
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &(omni_rect_t){346, 89, 381, 124}) > 100);   /* COMMS lights */
}

/* still 8 (stale) differs from still 0 only inside RD_DC_STALE */
static void test_rd_dc_stale_rect(void)
{
    const row_t *r = row_of("console");
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, 8, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_DC_STALE, "stale") > 100);
}

/* the glance: the outlines in rd.g.amber, the colon, the date; every channel <= 110; the orbit moves it whole; unset in
 * rd.g.steel */
static void test_rd_dc_glance(void)
{
    const row_t *r = row_of("console");
    fk_face_in_t g = dc_at(10, 42, 3, 0);
    g.glance = true;
    frame(r, &g, s_a);
    long amber = 0;
    for (int i = 0; i < W * W; i++) {
        TEST_ASSERT_TRUE(max8(s_a[i]) <= 110);
        amber += s_a[i] == RD_G_AMBER;
    }
    TEST_ASSERT_TRUE(amber > 500);
    g.odx = 3, g.ody = -2;
    frame(r, &g, s_b);
    for (int y = 10; y < W - 10; y++)
        for (int x = 10; x < W - 10; x++) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 2) * W + x + 3]);
    g.odx = g.ody = 0, g.clk.set = false;
    frame(r, &g, s_b);
    for (int i = 0; i < W * W; i++) TEST_ASSERT_TRUE(s_b[i] != RD_G_AMBER);
}

/* the glance's worst minutes, pinned (the wave-2 guide: the sanitizer sweep's step skips most minutes) */
static void test_rd_dc_glance_worst(void)
{
    const row_t *r = row_of("console");
    /* measured (set, stale): 08:08 24 h 3.69 %, 3.85 %; 10:08 12 h 3.06 %, 3.21 %; each pinned within .1 point (the
     * mockup's notes: 3.3 %, stale 3.5 %, at its 12 h 10:08) */
    static const struct { int h, m; bool h24; float lo[2], hi[2]; } WORST[] = {
        {8, 8, true, {3.59f, 3.75f}, {3.79f, 3.95f}},
        {10, 8, false, {2.96f, 3.11f}, {3.16f, 3.31f}},
    };
    for (unsigned i = 0; i < 2; i++)
        for (int stale = 0; stale < 2; stale++) {
            fk_face_in_t in = in_at(WORST[i].h, WORST[i].m, 30, 500);
            in.glance = true, in.h24 = WORST[i].h24;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date */
            in.clk.stale = stale != 0;
            char what[48];
            snprintf(what, sizeof what, "at %02d:%02d%s (pinned worst)", WORST[i].h, WORST[i].m,
                     stale ? ", stale" : "");
            glance_orbits(r, in, what);
            const float pct = glance_pct(r, &in);
            printf("console glance %s %s: %.2f %% lit\n", what, WORST[i].h24 ? "24 h" : "12 h", (double)pct);
            TEST_ASSERT_TRUE_MESSAGE(pct >= WORST[i].lo[stale] && pct <= WORST[i].hi[stale], what);
        }
}

/* ---- Nav Scope (wave 2 Task 7) */
static fk_face_in_t sc_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.sync_age_s = 840;
    return in;
}

/* R170: Nav Scope's region with a spec burn-in motion: the giant, whose corona breathes +-8 % over 12 s (spec 10.0),
 * where it shows: inside the glass (r 150) and its sprite's disc (r 146 about (262, 418)) */
static bool sc_moving(int x, int y)
{
    const float gx = (float)x + 0.5f - 233.f, gy = (float)y + 0.5f - 233.f;
    const float mx = (float)x + 0.5f - 262.f, my = (float)y + 0.5f - 418.f;
    return gx * gx + gy * gy < 150.5f * 150.5f && mx * mx + my * my < 146.5f * 146.5f;
}

/* the canvas angle of a turn from 12 (as the face draws it) and a point at r along it */
static void sc_point(float turn, float r, int *x, int *y)
{
    const double a = (double)turn * 6.283185307179586 - 1.5707963267948966;
    *x = (int)(233.0 + cos(a) * r), *y = (int)(233.0 + sin(a) * r);
}

/* the ops' wiring: the brief's numbers, the rects, the dial's runs against the layer table, the face's files, and every
 * glyph inside the glass (the readout's inside RD_SC_READOUT, the marker's inside RD_SC_STALE) */
static void test_rd_sc_face(void)
{
    TEST_ASSERT_EQUAL_STRING("scope", RD_FACE_SCOPE.key);
    TEST_ASSERT_EQUAL_UINT16(70, RD_FACE_SCOPE.cadence_ms);   /* spec section 18: within 66-80 */
    TEST_ASSERT_TRUE(RD_FACE_SCOPE.cadence_ms >= 66 && RD_FACE_SCOPE.cadence_ms <= 80);
    TEST_ASSERT_EQUAL_UINT16(1000, RD_FACE_SCOPE.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(250, RD_FACE_SCOPE.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_SCOPE.pulse_guard_ms);
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_SCOPE.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_SCOPE.reveal);
    TEST_ASSERT_FALSE(RD_FACE_SCOPE.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_SCOPE.tap);
    TEST_ASSERT_NOT_NULL(RD_FACE_SCOPE.tap_var);
    TEST_ASSERT_EQUAL_UINT16(2, RD_SC_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_SC_TAPPED_VARIANT, rd_still_variant(&RD_FACE_SCOPE));
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_SCOPE, rd_face_by_key("scope"));
    TEST_ASSERT_TRUE(RD_FACE_SCOPE.ctx_bytes <= 1572864u - 434312u);
    printf("scope ctx_bytes %u\n", (unsigned)RD_FACE_SCOPE.ctx_bytes);
    /* the glass: the box of r 151 (the wake's black fill) about the centre; the readout and the marker inside it */
    TEST_ASSERT_TRUE(RD_SC_GLASS.x0 + RD_SC_GLASS.x1 == W && RD_SC_GLASS.y0 + RD_SC_GLASS.y1 == W);
    TEST_ASSERT_EQUAL_INT(302, RD_SC_GLASS.x1 - RD_SC_GLASS.x0);
    TEST_ASSERT_EQUAL_INT(302, RD_SC_GLASS.y1 - RD_SC_GLASS.y0);
    TEST_ASSERT_TRUE(rect_in(&RD_SC_READOUT, &RD_SC_GLASS) && rect_in(&RD_SC_STALE, &RD_SC_READOUT));
    const omni_rect_t face = {0, 0, W, W};
    TEST_ASSERT_TRUE(rect_eq(&RD_SC_TAP, &face));
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(1, rd_sc_chime_rects(c));
    TEST_ASSERT_TRUE(rect_eq(&c[0], &RD_SC_GLASS));
    TEST_ASSERT_EQUAL_UINT16(0, RD_SC_STEP_MS);   /* committed: the continuous sweep until W2-S1 rules (Task 11) */
    /* the dial's runs are the table row's, rising, apart, within 1..255 */
    runs_row("rd_sc_dial", RD_SC_DIAL_RUNS, RD_SC_DIAL_RUNS_N);
    for (int k = 0; k < RD_SC_DIAL_RUNS_N; k++) {
        TEST_ASSERT_TRUE(RD_SC_DIAL_RUNS[k].i0 >= 1 && RD_SC_DIAL_RUNS[k].i1 >= RD_SC_DIAL_RUNS[k].i0);
        if (k) TEST_ASSERT_TRUE(RD_SC_DIAL_RUNS[k].i0 == RD_SC_DIAL_RUNS[k - 1].i1 + 1);
    }
    TEST_ASSERT_EQUAL_UINT(255, RD_SC_DIAL_RUNS[RD_SC_DIAL_RUNS_N - 1].i1);
    /* the face's own files (rd_sc_*), each with its 12-byte header, within the 500 000 B cap */
    size_t n0 = 0, n1 = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_SC_GIANT, &n0));
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_SC_DIAL, &n1));
    TEST_ASSERT_TRUE(ASSET_RD_SC_GIANT_W == 284 && ASSET_RD_SC_GIANT_H == 150);
    TEST_ASSERT_TRUE(ASSET_RD_SC_DIAL_W == 466 && ASSET_RD_SC_DIAL_H == 466);
    TEST_ASSERT_EQUAL_UINT(284u * 150u * 2u + 12u, (unsigned)n0);
    TEST_ASSERT_EQUAL_UINT(466u * 466u + 12u, (unsigned)n1);
    TEST_ASSERT_TRUE(n0 + n1 <= 500000u);
    printf("scope's files %u B (cap 500 000)\n", (unsigned)(n0 + n1));
    /* every glyph awake lies inside the glass; above y 200 (the readout's lines) inside RD_SC_READOUT; stale, the
     * marker's inside RD_SC_STALE: 12 and 24 h at lesson 10's times and the widest date, each page, unset */
    const row_t *r = row_of("scope");
    static const int HM[][2] = {{1, 0}, {9, 59}, {10, 0}, {12, 59}, {23, 59}, {0, 0}, {20, 8}};
    for (unsigned k = 0; k < sizeof HM / sizeof HM[0]; k++)
        for (int st = 0; st < 6; st++)
            for (int v = 0; v < 4; v++) {
                fk_face_in_t in = sc_at(HM[k][0], HM[k][1], 30, 500);
                in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP */
                in.h24 = st % 2 == 0, in.clk.stale = st >= 2 && st < 4, in.clk.set = st < 4;
                in.variant = (uint16_t)v, in.sync_age_s = INT32_MAX;   /* SYNC 24855 D AGO: the widest */
                s_ngl = 0;
                fk_test_glyph_hook = record;
                frame(r, &in, s_a);
                fk_test_glyph_hook = NULL;
                TEST_ASSERT_TRUE(s_ngl > 0 && s_ngl <= MAX_GLYPHS);
                for (unsigned g = 0; g < s_ngl; g++) {
                    const omni_rect_t b = {(int16_t)s_gl[g].x0, (int16_t)s_gl[g].y0, (int16_t)s_gl[g].x1,
                                           (int16_t)s_gl[g].y1};
                    char m[160];
                    snprintf(m, sizeof m, "scope at %02d:%02d state %d page %d: a glyph box (%d, %d)-(%d, %d) outside "
                             "its rect", HM[k][0], HM[k][1], st, v, b.x0, b.y0, b.x1, b.y1);
                    TEST_ASSERT_TRUE_MESSAGE(rect_in(&b, &RD_SC_GLASS), m);
                    if (b.y1 <= 200) TEST_ASSERT_TRUE_MESSAGE(rect_in(&b, &RD_SC_READOUT), m);
                    if (in.clk.stale && in.clk.set && b.y1 <= 152) TEST_ASSERT_TRUE_MESSAGE(rect_in(&b, &RD_SC_STALE), m);
                }
            }
}

/* the sweep's two modes (C8, W2-D11): continuous, the turn advances with the ms; stepped at 250 it holds within each
 * quarter second, dirty() returns nothing between steps (the corona's breath too steps with it), and each still in the
 * two modes differs only inside the glass */
static void test_rd_sc_step_modes(void)
{
    const row_t *r = row_of("scope");
    fk_clock_t c = sc_at(10, 9, 10, 0).clk;
    float last = -1.f;
    for (int ms = 0; ms < 1000; ms += 10) {   /* continuous: every ms moves it */
        c.ms = (uint16_t)ms;
        const float t = rd_sc_sweep_turn(&c, 0);
        TEST_ASSERT_TRUE(t > last);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (10.f + (float)ms / 1000.f) / 60.f, t);
        last = t;
    }
    for (int ms = 0; ms < 1000; ms++) {   /* stepped: (s + ms' / 1000) / 60, ms' floored to 250 */
        c.ms = (uint16_t)ms;
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, (10.f + (float)(ms - ms % 250) / 1000.f) / 60.f, rd_sc_sweep_turn(&c, 250));
    }
    rd_sc_set_step_ms(250);
    TEST_ASSERT_EQUAL_UINT16(250, rd_sc_step_ms());
    unsigned quiet = 0, steps = 0;
    for (int ms = 0; ms < 60000; ms += 10) {   /* a minute at a 10 ms grain: dirty only across a quarter second */
        fk_face_in_t a = sc_at(10, 9, 0, 0), b;
        a = shifted(a, ms), b = shifted(a, 10);
        a.mono_ms = (uint32_t)ms, b.mono_ms = (uint32_t)ms + 10u;
        omni_rect_t rects[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        const bool cross = (a.clk.ms / 250) != (b.clk.ms / 250) || a.clk.s != b.clk.s;
        char m[96];
        snprintf(m, sizeof m, "stepped at :%02u.%03u -> +10 ms: %u rects", a.clk.s, a.clk.ms, n);
        if (!cross) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, n, m);
            quiet++;
        } else {
            TEST_ASSERT_TRUE_MESSAGE(n == 1 && rect_eq(&rects[0], &RD_SC_GLASS), m);
            steps++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(240, steps);
    fk_face_in_t a = sc_at(10, 9, 7, 260), b = sc_at(10, 9, 7, 490);   /* and the frames agree inside a step */
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    assert_same(s_a, s_b, "stepped: :07.260 and :07.490");
    /* the stills: stepped against continuous, inside the glass only, and some differ (still 1's .900) */
    long differ = 0;
    for (unsigned i = 0; i < rd_still_count(r->ops); i++) {
        rd_sc_set_step_ms(250);
        rd_still_frame(r->ops, r->ctx, i, s_a);
        rd_sc_set_step_ms(0);
        rd_still_frame(r->ops, r->ctx, i, s_b);
        char m[48];
        snprintf(m, sizeof m, "still %u in the two modes", i);
        differ += differ_inside(s_a, s_b, &RD_SC_GLASS, m);
    }
    TEST_ASSERT_TRUE(differ > 100);
    rd_sc_set_step_ms(RD_SC_STEP_MS);
    printf("scope stepped: %u quiet pairs, %u steps a minute\n", quiet, steps);
}

/* the Task 7 review's M1: the harness's skip, back-step and state dirty checks once more in the 4 Hz stepped mode (Task 11
 * may commit it); tearDown restores the committed mode */
static void test_rd_sc_step_dirty(void)
{
    rd_sc_set_step_ms(250);
    dirty_jumps("scope");
    dirty_states("scope");
}

/* M11 (no symbol-absence test): the harness and rd_still_frame always run with the committed mode (tearDown restores it
 * after every test); the override takes 0 or 250 only; a still drawn with the other mode does not match its committed
 * row, which is why `rd verify` refuses while the override differs */
static void test_rd_sc_override_cleared(void)
{
    TEST_ASSERT_EQUAL_UINT16(RD_SC_STEP_MS, rd_sc_step_ms());
    rd_sc_set_step_ms(250);
    TEST_ASSERT_EQUAL_UINT16(250, rd_sc_step_ms());
    rd_sc_set_step_ms(7);   /* ignored */
    TEST_ASSERT_EQUAL_UINT16(250, rd_sc_step_ms());
    rd_sc_set_step_ms(0);
    TEST_ASSERT_EQUAL_UINT16(0, rd_sc_step_ms());
    const row_t *r = row_of("scope");
    const fk_verify_tab_t tab = {RD_VERIFY, sizeof RD_VERIFY / sizeof RD_VERIFY[0]};
    unsigned dups = 0;
    const fk_verify_t *v = fk_verify_lookup(&tab, 1, "scope", 1, &dups);
    TEST_ASSERT_NOT_NULL(v);
    rd_sc_set_step_ms(RD_SC_STEP_MS ? 0 : 250);
    rd_still_frame(r->ops, r->ctx, 1, s_a);
    TEST_ASSERT_TRUE_MESSAGE(fk_crc32(0, s_a, sizeof s_a) != v->crc, "still 1 in the other mode matches its row");
    rd_sc_set_step_ms(RD_SC_STEP_MS);
    rd_still_frame(r->ops, r->ctx, 1, s_a);
    TEST_ASSERT_EQUAL_HEX32(v->crc, fk_crc32(0, s_a, sizeof s_a));
}

/* the afterglow: 8 wedges of .08 rad trail the sweep, phosphor at .09 (1 - i / 8) over black (sampled at r 60, clear of
 * the graticule and the readout), nothing ahead of it or past the eighth; a contact lights as the sweep passes it
 * (L = 1) and fades within the fan (to e^(-.64 / 1.8) at its end), its dot brightest at the pass */
static void test_rd_sc_afterglow(void)
{
    const row_t *r = row_of("scope");
    fk_face_in_t in = sc_at(4, 20, 52, 0);   /* the sweep past 10 o'clock, both cursors at 4 */
    frame(r, &in, s_a);
    float turn;
    TEST_ASSERT_TRUE(rd_sc_sweep(&in, &turn));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 52.f / 60.f, turn);
    int lastg = 256;
    for (int i = 0; i < 8; i++) {
        const float t = turn - ((float)i + 0.5f) * 0.08f / 6.2831853f;
        int x, y;
        sc_point(t, 60.f, &x, &y);
        const uint16_t want = omni_blend565(0x0000, RD_C_PHOS, fk_ease_to8(0.09f * (1.f - (float)i / 8.f)));
        int R, G, B, r2, g2, b2;
        px8(s_a[y * W + x], &R, &G, &B);
        px8(want, &r2, &g2, &b2);
        char m[64];
        snprintf(m, sizeof m, "wedge %d at (%d, %d)", i, x, y);
        TEST_ASSERT_INT_WITHIN_MESSAGE(5, g2, G, m);
        TEST_ASSERT_INT_WITHIN_MESSAGE(9, r2, R, m);
        TEST_ASSERT_TRUE_MESSAGE(G <= lastg, m);   /* fading behind the sweep */
        if (i < 7) TEST_ASSERT_TRUE_MESSAGE(G > 0, m);   /* the eighth, .011, is under one 565 step of green */
        lastg = G;
    }
    int x, y;
    sc_point(turn + 0.04f / 6.2831853f, 60.f, &x, &y);   /* ahead of the sweep */
    TEST_ASSERT_EQUAL_HEX16(0x0000, s_a[y * W + x]);
    sc_point(turn - 0.70f / 6.2831853f, 60.f, &x, &y);   /* past the eighth wedge (.64 rad) */
    TEST_ASSERT_EQUAL_HEX16(0x0000, s_a[y * W + x]);
    /* contact 0 (CONTACTS[0]: .95 rad from 3 o'clock less a quarter, r 118: the turn .95 / 2 pi) as the sweep passes */
    const float pass = 0.95f / 6.2831853f * 60.f;   /* s */
    fk_face_in_t p = sc_at(4, 20, (int)pass, (int)((pass - (float)(int)pass) * 1000.f) + 1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, rd_sc_contact(&p, 0));
    const float L1 = rd_sc_contact(&p, 0);
    fk_face_in_t q = shifted(p, 3000), e = shifted(p, 6100), before = shifted(p, -200);
    TEST_ASSERT_TRUE(rd_sc_contact(&q, 0) < L1 && rd_sc_contact(&e, 0) < rd_sc_contact(&q, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.70f, rd_sc_contact(&e, 0));   /* e^(-.64 / 1.8) at the fan's end */
    TEST_ASSERT_TRUE(rd_sc_contact(&before, 0) < 0.05f);             /* not yet passed: nearly a turn since */
    frame(r, &p, s_a);
    frame(r, &before, s_b);
    sc_point(0.95f / 6.2831853f, 118.f, &x, &y);
    int Ra, Ga, Ba, Rb, Gb, Bb;
    px8(s_a[y * W + x], &Ra, &Ga, &Ba);
    px8(s_b[y * W + x], &Rb, &Gb, &Bb);
    TEST_ASSERT_TRUE_MESSAGE(Ga > Gb + 100, "the contact does not light as the sweep passes it");
}

/* within a minute, every dirty rect lies inside RD_SC_GLASS (at the cadence, at 40 ms and at a second), through the
 * wake, a tap's fade and the chime; and the frame outside it never changes */
static void test_rd_sc_glass_confined(void)
{
    const row_t *r = row_of("scope");
    static const long STEP[] = {70, 40, 1000};
    for (int st = 0; st < 3; st++)
        for (unsigned k = 0; k < 3; k++)
            for (long t = 0; t + STEP[k] < 60000; t += STEP[k]) {
                fk_face_in_t a = shifted(sc_at(10, 9, 0, 0), t);
                a.clk.stale = st == 1, a.clk.set = st != 2, a.mono_ms = (uint32_t)t;
                if (t < 1000) a.wake_ms = (uint32_t)t;
                if (t >= 2000 && t < 2400) a.pulsing = true, a.pulse_ms = (uint16_t)(t - 2000);
                if (t >= 5000 && t < 7400) a.thrum = 0.6f, a.beep = t < 5300 ? 1.f : 0.f;
                const fk_face_in_t b = ran(shifted(a, STEP[k]), STEP[k]);
                omni_rect_t rects[8];
                const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
                for (uint8_t q = 0; q < n; q++) {
                    char m[96];
                    snprintf(m, sizeof m, "state %d at +%ld ms by %ld: a dirty rect outside the glass", st, t, STEP[k]);
                    TEST_ASSERT_TRUE_MESSAGE(rect_in(&rects[q], &RD_SC_GLASS), m);
                }
            }
    rd_still_frame(r->ops, r->ctx, 0, s_c);
    static const long AT[] = {0, 333, 4321, 17000, 33950, 59990};
    for (unsigned k = 0; k < sizeof AT / sizeof AT[0]; k++) {
        fk_face_in_t a = shifted(sc_at(10, 9, 0, 0), AT[k]);
        a.wake_ms = k == 1 ? 333u : UINT32_MAX;
        a.thrum = k == 3 ? 1.f : 0.f;
        frame(r, &a, s_a);
        differ_inside(s_c, s_a, &RD_SC_GLASS, "the frame outside the glass");
    }
}

/* the corona: the giant at .92 + .08 sin(2 pi t / 12 000) of the clock (its added alpha +-8 % over 12 s), the period
 * 12 s, .92 under reduced motion; the giant's pixels follow it */
static void test_rd_sc_corona(void)
{
    const row_t *r = row_of("scope");
    float lo = 2.f, hi = -1.f;
    for (long t = 0; t < 24000; t += 125) {
        fk_face_in_t in = shifted(sc_at(10, 0, 0, 0), t);
        const float k = rd_sc_corona(&in);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.92f + 0.08f * (float)sin(6.283185307179586 * (double)(t % 12000) / 12000.0), k);
        const fk_face_in_t p = shifted(in, 12000);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, k, rd_sc_corona(&p));
        lo = k < lo ? k : lo, hi = k > hi ? k : hi;
        in.reduced = true;
        TEST_ASSERT_EQUAL_FLOAT(0.92f, rd_sc_corona(&in));
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.84f, lo);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.00f, hi);
    /* 10:00:03.000 the peak (1.00), :09.000 the trough (.84): a giant pixel's red scales by .84 */
    fk_face_in_t a = sc_at(10, 0, 3, 0), b = sc_at(10, 0, 9, 0);
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    long n = 0;
    double ra = 0, rb = 0;
    for (int y = 300; y < 370; y++)
        for (int x = 180; x < 300; x++) {
            if (!sc_moving(x, y)) continue;
            int R, G, B;
            px8(s_a[y * W + x], &R, &G, &B);
            ra += R;
            px8(s_b[y * W + x], &R, &G, &B);
            rb += R;
            n++;
        }
    TEST_ASSERT_TRUE(n > 5000);
    printf("scope corona: the giant's mean red %.1f at the peak, %.1f at the trough (%.3f)\n", ra / n, rb / n, rb / ra);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.84f, (float)(rb / ra));
}

/* unset: no cursor or sweep pixel (nothing in the glass greener than the graticule's scale outside the hub, the
 * readout, the dashes and the contacts), the sweep dark, the contacts at .1, --:-- at the hub in paintDim, the caption */
static void test_rd_sc_unset(void)
{
    const row_t *r = row_of("scope");
    fk_face_in_t in = sc_at(10, 9, 30, 500), set;
    in.clk.set = false, in.mono_ms = 7777;
    set = sc_at(10, 9, 30, 500);
    float turn;
    TEST_ASSERT_FALSE(rd_sc_sweep(&in, &turn));
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(0.1f, rd_sc_contact(&in, i));
    char l1[24], l2[24];
    rd_sc_readout(&in, l1, l2);
    TEST_ASSERT_EQUAL_STRING("NO TIME FIX", l1);
    TEST_ASSERT_EQUAL_STRING("AWAITING SIGNAL", l2);
    const omni_rect_t hub = {222, 227, 244, 239}, dashes = {195, 268, 272, 298}, tag = {150, 238, 260, 270};
    for (int pass = 0; pass < 2; pass++) {
        frame(r, pass ? &set : &in, s_a);
        int green = 0;
        for (int y = RD_SC_GLASS.y0; y < RD_SC_GLASS.y1; y++)
            for (int x = RD_SC_GLASS.x0; x < RD_SC_GLASS.x1; x++) {
                if (inside(&hub, x, y) || inside(&RD_SC_READOUT, x, y) || inside(&dashes, x, y) || inside(&tag, x, y))
                    continue;
                bool near = false;   /* the contacts' dots */
                for (int i = 0; i < 4; i++) {
                    static const float A[4] = {0.95f, 4.3f, 5.55f, 2.0f}, R[4] = {118.f, 74.f, 122.f, 56.f};
                    const double cx = 233.0 + cos((double)A[i] - 1.5707963) * R[i], cy = 233.0 + sin((double)A[i] - 1.5707963) * R[i];
                    near = near || ((double)x + 0.5 - cx) * ((double)x + 0.5 - cx) + ((double)y + 0.5 - cy) * ((double)y + 0.5 - cy) < 49.0;
                }
                if (near) continue;
                int R, G, B;
                px8(s_a[y * W + x], &R, &G, &B);
                if (G > R + 40) green = G > green ? G : green;   /* phosphor, not the paint ticks in the box's corners */
            }
        if (!pass) TEST_ASSERT_TRUE_MESSAGE(green <= 125, "unset: a cursor or a sweep pixel in the glass");
        else TEST_ASSERT_TRUE_MESSAGE(green > 200, "set: the discriminator finds no cursor");
    }
    frame(r, &in, s_a);
    long dim = 0;
    for (int y = dashes.y0; y < dashes.y1; y++)
        for (int x = dashes.x0; x < dashes.x1; x++) dim += s_a[y * W + x] == RD_C_PAINT_DIM;
    TEST_ASSERT_TRUE_MESSAGE(dim > 30, "unset: no paintDim --:-- at the hub");
}

/* reduced motion: the sweep steps once a second (frames equal within a second, another the next), no fan pixel, the
 * contacts at .6, the corona at .92 */
static void test_rd_sc_reduced(void)
{
    const row_t *r = row_of("scope");
    fk_face_in_t a = sc_at(4, 20, 52, 0), b = a, c = a;
    a.reduced = b.reduced = c.reduced = true;
    b.clk.ms = 400, b.mono_ms = 400;
    c.clk.ms = 999, c.mono_ms = 999;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    assert_same(s_a, s_b, "reduced at :52.000 and :52.400");
    frame(r, &c, s_b);
    assert_same(s_a, s_b, "reduced at :52.000 and :52.999");
    fk_face_in_t d = shifted(a, 1000);
    frame(r, &d, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_SC_GLASS, "reduced, the next second") > 0);
    float turn;
    TEST_ASSERT_TRUE(rd_sc_sweep(&b, &turn));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 52.f / 60.f, turn);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_FLOAT(0.6f, rd_sc_contact(&b, i));
    TEST_ASSERT_EQUAL_FLOAT(0.92f, rd_sc_corona(&b));
    for (int i = 0; i < 8; i++) {   /* where the fan lies awake, black under reduced motion */
        int x, y;
        sc_point(52.f / 60.f - ((float)i + 0.5f) * 0.08f / 6.2831853f, 60.f, &x, &y);
        char m[48];
        snprintf(m, sizeof m, "reduced: a fan pixel at (%d, %d)", x, y);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0000, s_a[y * W + x], m);
    }
}

/* the wake (1.0 s): the glass warms outward (black past rr = 150 easeOut3(1.25 wk)), the sweep makes one decelerating
 * turn onto the clock, the cursors grow out from 250 ms, the giant fades in and rises 24 px, the readout from 500 ms */
static void test_rd_sc_wake(void)
{
    const row_t *r = row_of("scope");
    fk_face_in_t rest = sc_at(4, 20, 52, 0), w = rest;
    float t0, t;
    rd_sc_sweep(&rest, &t0);
    static const struct { uint32_t ms; float back; } TURN[] = {{0, 1.f}, {500, 0.125f}, {900, 0.001f}};
    for (unsigned k = 0; k < 3; k++) {
        w.wake_ms = TURN[k].ms;
        rd_sc_sweep(&w, &t);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, t0 - TURN[k].back, t);
    }
    w.wake_ms = 500;   /* rr = 150 easeOut3(.625) = 141.8: black from 143 out, the dial within 140 */
    frame(r, &w, s_b);
    frame(r, &rest, s_c);
    int x, y;
    sc_point(0.25f, 145.5f, &x, &y);   /* 3 o'clock, past rr: black */
    TEST_ASSERT_EQUAL_HEX16(0x0000, s_b[y * W + x]);
    TEST_ASSERT_TRUE(s_c[y * W + x] != 0x0000);   /* the graticule's crosshair at rest */
    sc_point(0.25f, 100.f, &x, &y);   /* the ring r 100 at 3 o'clock, inside rr: as at rest */
    TEST_ASSERT_EQUAL_HEX16(s_c[y * W + x], s_b[y * W + x]);
    char l1[24], l2[24];
    rd_sc_readout(&w, l1, l2);
    TEST_ASSERT_EQUAL_STRING("ETA: EVENTUALLY", l2);
    w.wake_ms = 450;   /* the readout's fade has not begun: another page draws the same frame */
    frame(r, &w, s_a);
    w.variant = 1;
    frame(r, &w, s_b);
    assert_same(s_a, s_b, "the readout before 500 ms of the wake");
    w.wake_ms = 650, w.variant = 0;   /* and half faded in at 650: the pages differ */
    frame(r, &w, s_a);
    w.variant = 1;
    frame(r, &w, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_SC_READOUT, "the readout at 650 ms of the wake") > 100);
    w.variant = 0;
    /* the giant: none at 0, rising 24 px at 100 ms, in place from 1 000 */
    w.wake_ms = 0;
    frame(r, &w, s_a);
    long lit = 0;
    for (int yy = 330; yy < 380; yy++)
        for (int xx = 200; xx < 300; xx++) lit += max8(s_a[yy * W + xx]) > 40 && (s_a[yy * W + xx] >> 11) > 8;
    TEST_ASSERT_TRUE_MESSAGE(lit < 200, "the giant shows at the wake's start");
    w.wake_ms = 999;
    frame(r, &w, s_a);
    w.wake_ms = UINT32_MAX;
    frame(r, &w, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_SC_GLASS) < 2000);   /* nearly settled at its last frame */
    /* under reduced motion the runtime gives no own wake: the face shows settled (as Drive Console's) */
    w.reduced = true, w.wake_ms = 100;
    frame(r, &w, s_a);
    w.wake_ms = UINT32_MAX;
    frame(r, &w, s_b);
    assert_same(s_a, s_b, "reduced: the wake shows settled");
}

/* the readout's pages: a tap anywhere turns them 0 -> 1 -> 2 -> 3 -> 0 (FK_TAP_PULSE: the new page fades in over
 * 250 ms); page 4 is the date (12 h: its AM/PM suffix) and the last sync; stale hides the top line only; unset the tap
 * does nothing */
static void test_rd_sc_pages(void)
{
    const row_t *r = row_of("scope");
    static const char *const P[4][2] = {{"HDG  HOME", "ETA: EVENTUALLY"}, {"RED GIANT", "STILL RED"},
                                        {"NEAREST TAKEAWAY", "9 742 LY"}, {"SUN 01 NOV \267 AM", "SYNC 14 MIN AGO"}};
    fk_face_in_t in = sc_at(10, 9, 30, 500);
    in.h24 = false;
    uint16_t v = 0;
    for (int k = 0; k < 8; k++) {
        in.variant = v;
        char l1[24], l2[24];
        rd_sc_readout(&in, l1, l2);
        TEST_ASSERT_EQUAL_STRING(P[v][0], l1);
        TEST_ASSERT_EQUAL_STRING(P[v][1], l2);
        const uint16_t was = v;
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, (int16_t)(13 * k + 5), (int16_t)(400 - k), &v));
        TEST_ASSERT_EQUAL_UINT16((was + 1) % 4, v);
    }
    in.h24 = true, in.variant = 3;
    char l1[24], l2[24];
    rd_sc_readout(&in, l1, l2);
    TEST_ASSERT_EQUAL_STRING("SUN 01 NOV", l1);
    in.clk.stale = true;
    rd_sc_readout(&in, l1, l2);
    TEST_ASSERT_EQUAL_STRING("", l1);
    TEST_ASSERT_EQUAL_STRING("SYNC 14 MIN AGO", l2);
    in.clk.set = false, v = 2;
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 233, 233, &v));
    TEST_ASSERT_EQUAL_UINT16(2, v);
    /* the fade: at 0 ms of the pulse the readout is gone, at 125 half, from 250 whole */
    fk_face_in_t f = sc_at(10, 9, 30, 500);
    f.variant = 1, f.pulsing = true, f.pulse_ms = 0;
    frame(r, &f, s_a);
    f.pulse_ms = 250;
    frame(r, &f, s_b);
    f.pulsing = false, f.pulse_ms = 0;
    frame(r, &f, s_c);
    assert_same(s_c, s_b, "the readout at the fade's end");
    TEST_ASSERT_TRUE(differ_inside(s_a, s_c, &RD_SC_READOUT, "the readout's fade") > 300);
}

/* the cursors step once a minute (lesson 9): every pixel outside the sweep's reach is the same through a minute */
static void test_rd_sc_hands_per_minute(void)
{
    float h0, m0, h1, m1;
    fk_face_in_t a = sc_at(10, 9, 0, 0), b = sc_at(10, 9, 59, 999), c = sc_at(10, 10, 0, 0);
    rd_sc_hand_turns(&a, &h0, &m0);
    rd_sc_hand_turns(&b, &h1, &m1);
    TEST_ASSERT_TRUE(h0 == h1 && m0 == m1);
    rd_sc_hand_turns(&c, &h1, &m1);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.f / 60.f, m1);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, (10.f + 10.f / 60.f) / 12.f, h1);
    fk_face_in_t d = sc_at(22, 9, 0, 0);
    rd_sc_hand_turns(&d, &h1, &m1);
    TEST_ASSERT_EQUAL_FLOAT(h0, h1);   /* 22:09 is 10:09 */
}

/* the chime (awake, set): the sweep holds at 12, the graticule pulses with the thrum (its pixels brighter, inside the
 * glass), the TAKEAWAY? contact lights with each note; under reduced motion nothing */
static void test_rd_sc_chime(void)
{
    const row_t *r = row_of("scope");
    fk_face_in_t a = sc_at(10, 0, 1, 200), b = a;
    b.thrum = 1.f;
    float t;
    rd_sc_sweep(&b, &t);
    TEST_ASSERT_EQUAL_FLOAT(0.f, t);
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    long brighter = 0;
    int x, y;
    for (int k = 0; k < 360; k++) {   /* the ring r 50 */
        sc_point((float)k / 360.f, 50.f, &x, &y);
        int R, G, B, R2, G2, B2;
        px8(s_a[y * W + x], &R, &G, &B);
        px8(s_b[y * W + x], &R2, &G2, &B2);
        brighter += G2 > G + 8;
    }
    TEST_ASSERT_TRUE_MESSAGE(brighter > 100, "the graticule does not pulse with the thrum");
    b.thrum = 0, b.beep = 1.f;
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_sc_contact(&b, 1));
    TEST_ASSERT_TRUE(rd_sc_contact(&b, 0) < 1.f);
    b.reduced = true;
    TEST_ASSERT_EQUAL_FLOAT(0.6f, rd_sc_contact(&b, 1));
}

/* still 8 (stale) differs from still 0 only inside RD_SC_STALE */
static void test_rd_sc_stale_rect(void)
{
    const row_t *r = row_of("scope");
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, 8, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_SC_STALE, "stale") > 100);
}

/* R169 (the dispatch's dimming measure): the mean of each lit pixel's largest channel (> 8) over the giant's layer, and
 * over the part of it the glass shows at rest, at the breath's peak (1.00) and its mean (.92) */
static void test_rd_sc_dimming(void)
{
    size_t n = 0;
    const uint8_t *bin = rd_assets_host_get(ASSET_RD_SC_GIANT, &n);
    TEST_ASSERT_NOT_NULL(bin);
    const uint16_t *lay = (const uint16_t *)(const void *)(bin + 12);
    long lit = 0, vlit = 0;
    double sum = 0, vsum = 0;
    int peak = 0;
    for (int y = 0; y < 150; y++)
        for (int x = 0; x < 284; x++) {
            const int m = max8(lay[y * 284 + x]);
            peak = m > peak ? m : peak;
            if (m <= 8) continue;
            lit++, sum += m;
            const float sx = 116.f + (float)x + 0.5f - 233.f, sy = 272.f + (float)y + 0.5f - 233.f;
            if (sx * sx + sy * sy < 150.f * 150.f) vlit++, vsum += m;
        }
    const double mean = sum / (double)lit, vmean = vsum / (double)vlit;
    /* the face as drawn: still 0's giant pixels (inside the glass, off the graticule) at its own breath */
    const row_t *r = row_of("scope");
    fk_face_in_t pk = sc_at(10, 0, 3, 0);   /* the breath's peak */
    frame(r, &pk, s_a);
    long flit = 0;
    double fsum = 0;
    for (int y = 272; y < 384; y++)
        for (int x = 83; x < 383; x++) {
            if (!sc_moving(x, y)) continue;
            const int m = max8(s_a[y * W + x]);
            if (m > 8) flit++, fsum += m;
        }
    printf("scope's giant (R169): the layer %ld lit px, mean largest channel %.1f, peak %d (ASSET_RD_SC_GIANT_MAX %d);"
           " the glass's part %ld px, mean %.1f at the breath's peak, %.1f at its mean (x .92); the frame at the peak"
           " %ld px, mean %.1f\n", lit, mean, peak, ASSET_RD_SC_GIANT_MAX, vlit, vmean, vmean * 0.92, flit,
           fsum / (double)flit);
    TEST_ASSERT_EQUAL_INT_MESSAGE(peak, ASSET_RD_SC_GIANT_MAX, "rd_pack's max is not the layer's largest channel");
    /* R181: the burn-in level is the breath's mean (its +-8 % over 12 s is the spec's burn-in motion for this giant), so
     * Bay Mouth's area ceiling (R169, round(.45 x 255) = 115) applies to the part of the layer the glass shows at rest,
     * at the breath's mean (.92, scope.c's CORONA_MEAN): measured 108.4. Report lines only, not gated: the same part at
     * the breath's peak (117.9) and the whole layer (126.2; its rows 112-149 never show). Over the ceiling: stop and
     * report (R146), never dim the art to fit */
    TEST_ASSERT_TRUE_MESSAGE(vmean * 0.92 <= 115.0, "the glass's part of the giant at the breath's mean is over 115 (R181)");
}

/* the glance: the rim, the limb, 12 3 6 9, the cursors as lines; every channel <= 110; the orbit moves it whole; unset
 * no phosphor line and --:-- in rd.g.steel; stale the marker in rd.g.amber */
static void test_rd_sc_glance(void)
{
    const row_t *r = row_of("scope");
    fk_face_in_t g = sc_at(10, 42, 3, 0);
    g.glance = true;
    frame(r, &g, s_a);
    long phos = 0, hull = 0;
    for (int i = 0; i < W * W; i++) {
        TEST_ASSERT_TRUE(max8(s_a[i]) <= 110);
        phos += s_a[i] == RD_G_PHOS;
        hull += s_a[i] == RD_G_HULL;
    }
    TEST_ASSERT_TRUE(phos > 200);
    TEST_ASSERT_TRUE(hull > 50);
    g.odx = 3, g.ody = -2;
    frame(r, &g, s_b);
    for (int y = 10; y < W - 10; y++)
        for (int x = 10; x < W - 10; x++) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 2) * W + x + 3]);
    g.odx = g.ody = 0, g.clk.set = false;
    frame(r, &g, s_b);
    long steel = 0;
    for (int i = 0; i < W * W; i++) {
        TEST_ASSERT_TRUE(s_b[i] != RD_G_PHOS);
        steel += s_b[i] == RD_G_STEEL;
    }
    TEST_ASSERT_TRUE(steel > 30);
    g.clk.set = true, g.clk.stale = true;
    frame(r, &g, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_SC_STALE, "the stale glance") > 20);
}

/* the glance's worst minutes, pinned (the wave-2 guide: the sanitizer sweep's step skips most minutes) */
static void test_rd_sc_glance_worst(void)
{
    const row_t *r = row_of("scope");
    /* measured (set, stale): 00:28 2.49 %, 2.64 % in 24 h and in 12 h alike (an analogue glance: no date, no AM/PM),
     * the day sweep's worst minute; each pinned within .1 point (the mockup's notes: 2.8 %, stale 3.0 %) */
    static const struct { int h, m; bool h24; float lo[2], hi[2]; } WORST[] = {
        {0, 28, true, {2.39f, 2.54f}, {2.59f, 2.74f}},
        {0, 28, false, {2.39f, 2.54f}, {2.59f, 2.74f}},
    };
    for (unsigned i = 0; i < sizeof WORST / sizeof WORST[0]; i++)
        for (int stale = 0; stale < 2; stale++) {
            fk_face_in_t in = in_at(WORST[i].h, WORST[i].m, 30, 500);
            in.glance = true, in.h24 = WORST[i].h24;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date */
            in.clk.stale = stale != 0;
            char what[48];
            snprintf(what, sizeof what, "at %02d:%02d%s (pinned worst)", WORST[i].h, WORST[i].m,
                     stale ? ", stale" : "");
            glance_orbits(r, in, what);
            const float pct = glance_pct(r, &in);
            printf("scope glance %s %s: %.2f %% lit\n", what, WORST[i].h24 ? "24 h" : "12 h", (double)pct);
            TEST_ASSERT_TRUE_MESSAGE(pct >= WORST[i].lo[stale] && pct <= WORST[i].hi[stale], what);
        }
}

/* ---- Vending Machine (wave 2 Task 8) */
/* the ops' wiring: the brief's numbers, the rects, the face's file, its pending bit cleared */
static void test_rd_vm_face(void)
{
    TEST_ASSERT_EQUAL_STRING("vending", RD_FACE_VENDING.key);
    TEST_ASSERT_EQUAL_UINT16(50, RD_FACE_VENDING.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(1200, RD_FACE_VENDING.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(3600, RD_FACE_VENDING.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_FACE_VENDING.pulse_guard_ms);   /* every press restarts its pulse */
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_VENDING.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_VENDING.reveal);
    TEST_ASSERT_FALSE(RD_FACE_VENDING.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_VENDING.tap);
    TEST_ASSERT_NOT_NULL(RD_FACE_VENDING.tap_var);
    TEST_ASSERT_EQUAL_UINT16(RD_VM_MYSTERY_BTN | (1u << 3) | RD_VM_VAR_PARITY, RD_VM_TAPPED_VARIANT);
    TEST_ASSERT_EQUAL_UINT16(RD_VM_TAPPED_VARIANT, rd_still_variant(&RD_FACE_VENDING));
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_VENDING, rd_face_by_key("vending"));
    TEST_ASSERT_EQUAL_UINT(0, (RD_PENDING >> RD_F_VENDING) & 1u);
    TEST_ASSERT_TRUE(RD_FACE_VENDING.ctx_bytes <= 1572864u - 434312u);
    printf("vending ctx_bytes %u\n", (unsigned)RD_FACE_VENDING.ctx_bytes);
    /* the rects: the brief's sizes where it gives them (the ticker is its clip's 258 x 26) */
    for (int i = 0; i < RD_VM_BTN_N; i++) {
        TEST_ASSERT_TRUE(RD_VM_BTN[i].x1 - RD_VM_BTN[i].x0 == 84 && RD_VM_BTN[i].y1 - RD_VM_BTN[i].y0 == 48);
        TEST_ASSERT_TRUE(RD_VM_BTN[i].x0 == 101 + 90 * (i % 3) && RD_VM_BTN[i].y0 == 238 + 54 * (i / 3));
        TEST_ASSERT_TRUE(rect_in(&RD_VM_BTN[i], &RD_VM_PANEL));
    }
    TEST_ASSERT_TRUE(RD_VM_FLAP.x1 - RD_VM_FLAP.x0 == 142 && RD_VM_FLAP.y1 - RD_VM_FLAP.y0 == 38);
    TEST_ASSERT_TRUE(RD_VM_LIGHTBOX.x1 - RD_VM_LIGHTBOX.x0 == 250 && RD_VM_LIGHTBOX.y1 - RD_VM_LIGHTBOX.y0 == 34);
    TEST_ASSERT_TRUE(RD_VM_TICKER.x1 - RD_VM_TICKER.x0 == 258 && RD_VM_TICKER.y1 - RD_VM_TICKER.y0 == 26);
    TEST_ASSERT_TRUE(rect_in(&RD_VM_STALE, &RD_VM_LIGHTBOX) && rect_in(&RD_VM_COLON, &RD_VM_TIME));
    TEST_ASSERT_TRUE(rect_in(&RD_VM_MYSTERY, &RD_VM_BTN[RD_VM_MYSTERY_BTN]) && rect_in(&RD_VM_TICKER, &RD_VM_PANEL));
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(3, rd_vm_chime_rects(c));
    TEST_ASSERT_TRUE(rect_eq(&c[0], &RD_VM_TIME) && rect_eq(&c[1], &RD_VM_LIGHTBOX) && rect_eq(&c[2], &RD_VM_MYSTERY));
    /* the face's own file (rd_vm_*): 340 x 390 RGB565, 265 200 B and its 12-byte header, within the 500 000 B cap */
    size_t n = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_VM_FRONT, &n));
    TEST_ASSERT_TRUE(ASSET_RD_VM_FRONT_W == 340 && ASSET_RD_VM_FRONT_H == 390);
    TEST_ASSERT_EQUAL_UINT(265200u + 12u, (unsigned)n);
    printf("vending's files %u B (cap 500 000)\n", (unsigned)n);
    /* every glyph lies inside a text rect: the lightbox, the ticker, the buttons, the tray; awake, 12 and 24 h, stale,
     * unset, each button's pulse and each item (the sticker's are sampled, not placed: test_rd_vm_12h) */
    const row_t *r = row_of("vending");
    const omni_rect_t *texts[9] = {&RD_VM_STALE, &RD_VM_TICKER, &RD_VM_FLAP, &RD_VM_BTN[0], &RD_VM_BTN[1],
                                   &RD_VM_BTN[2], &RD_VM_BTN[3], &RD_VM_BTN[4], &RD_VM_BTN[5]};
    for (int st = 0; st < 4; st++)
        for (int v = 0; v < 13; v++) {
            fk_face_in_t in = in_at(20, 8, 30, 500);
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;
            in.h24 = st != 1, in.clk.stale = st == 2, in.clk.set = st != 3;
            if (v > 0) {
                in.pulsing = true, in.pulse_ms = 1000;
                in.variant = (uint16_t)(v <= 6 ? (unsigned)(v - 1) : RD_VM_MYSTERY_BTN | (unsigned)((v - 6) % 6) << 3);
            }
            s_ngl = 0;
            fk_test_glyph_hook = record;
            frame(r, &in, s_a);
            fk_test_glyph_hook = NULL;
            TEST_ASSERT_TRUE(s_ngl <= MAX_GLYPHS);
            for (unsigned g = 0; g < s_ngl; g++) {
                const omni_rect_t b = {(int16_t)s_gl[g].x0, (int16_t)s_gl[g].y0, (int16_t)s_gl[g].x1, (int16_t)s_gl[g].y1};
                const omni_rect_t tk = {RD_VM_TICKER.x0 - 30, RD_VM_TICKER.y0 - 12, RD_VM_TICKER.x1 + 30,
                                        RD_VM_TICKER.y1 + 12};   /* a ticker glyph's box may run past its clip */
                char m[160];
                snprintf(m, sizeof m, "vending state %d v %d: a glyph box (%d, %d)-(%d, %d) outside the text rects", st,
                         v, b.x0, b.y0, b.x1, b.y1);
                TEST_ASSERT_TRUE_MESSAGE(dc_in_any(&b, texts, 9) || rect_in(&b, &tk), m);
            }
        }
}

/* the buzz: sod 9 000 with ms 0-99, not at ms 100 nor sod 9 001; unset never. Under reduced motion the lightbox holds */
static void test_rd_vm_buzz(void)
{
    fk_face_in_t in = in_at(2, 30, 0, 0);   /* sod 9 000 */
    for (int ms = 0; ms < 100; ms++) {
        in.clk.ms = (uint16_t)ms;
        TEST_ASSERT_TRUE(rd_vm_buzz(&in.clk));
    }
    in.clk.ms = 100;
    TEST_ASSERT_FALSE(rd_vm_buzz(&in.clk));
    in.clk.s = 1, in.clk.ms = 0;
    TEST_ASSERT_FALSE(rd_vm_buzz(&in.clk));
    in = in_at(23, 59, 51, 50);   /* 86 391 = 9 x 9 599 */
    TEST_ASSERT_TRUE(rd_vm_buzz(&in.clk));
    in.clk.set = false;
    TEST_ASSERT_FALSE(rd_vm_buzz(&in.clk));
    /* the flicker inside it: the mockup's 30 ms slots (4 000 + ms), .45 in the odd ones */
    in = in_at(2, 30, 0, 10);
    TEST_ASSERT_EQUAL_FLOAT(0.45f, rd_vm_lit(&in));
    in.clk.ms = 30;
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lit(&in));
    in.clk.ms = 60;
    TEST_ASSERT_EQUAL_FLOAT(0.45f, rd_vm_lit(&in));
    in.clk.ms = 120;
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lit(&in));
    const row_t *r = row_of("vending");
    fk_face_in_t a = in_at(2, 30, 0, 10), b = in_at(2, 30, 0, 150);
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_VM_LIGHTBOX) > 500);
    a.reduced = b.reduced = true;
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lit(&a));
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, differ_in(s_a, s_b, &RD_VM_LIGHTBOX));
}

/* the ticker's glyph boxes in its band, whole inside [lo, hi) */
typedef struct { int x0, y0, w, h; } vm_box_t;
static unsigned vm_ticker_boxes(const row_t *r, const fk_face_in_t *in, int lo, int hi, vm_box_t *out)
{
    s_ngl = 0;
    fk_test_glyph_hook = record;
    frame(r, in, s_a);
    fk_test_glyph_hook = NULL;
    unsigned n = 0;
    for (unsigned g = 0; g < s_ngl && g < MAX_GLYPHS; g++)
        if (s_gl[g].y0 >= RD_VM_TICKER.y0 - 14 && s_gl[g].y1 <= RD_VM_TICKER.y1 + 14 && s_gl[g].x0 >= lo &&
            s_gl[g].x1 <= hi)
            out[n++] = (vm_box_t){s_gl[g].x0, s_gl[g].y0, s_gl[g].x1 - s_gl[g].x0, s_gl[g].y1 - s_gl[g].y0};
    return n;
}

/* the ticker: its offset moves 2.25 px a 50 ms frame (45 px/s of the clock's ms), modulo its message's width; the glyphs
 * 50 ms on are the same glyphs 2 or 3 px further left, across the wrap too (seamless); still under reduced motion; and
 * within a second at rest every dirty rect lies inside the ticker, the colon, MYSTERY or the lightbox (the buzz) */
static void test_rd_vm_ticker(void)
{
    const row_t *r = row_of("vending");
    const float mw = rd_vm_ticker_mw(true);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 98.f * 11.2f, mw);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 45.f * 11.2f, rd_vm_ticker_mw(false));
    long wraps = 0;
    for (long t = 0; t < 86400000L - 50; t += 997L * 13L) {
        fk_face_in_t a = in_at((int)(t / 3600000), (int)(t / 60000 % 60), (int)(t / 1000 % 60), (int)(t % 1000));
        const fk_face_in_t b = shifted(a, 50);
        const float oa = rd_vm_ticker_off(&a), ob = rd_vm_ticker_off(&b);
        TEST_ASSERT_TRUE(oa >= 0.f && oa < mw && ob >= 0.f && ob < mw);
        float d = ob - oa;
        if (d < 0) d += mw, wraps++;
        TEST_ASSERT_FLOAT_WITHIN(2e-3f, 2.25f, d);
    }
    TEST_ASSERT_TRUE(wraps > 0);
    /* seamless: the wrap between t and t + 50 ms (the offset falls back), and an ordinary step; every glyph whole inside
     * the clip 50 ms on has its twin 2 or 3 px to the right before */
    static vm_box_t A[MAX_GLYPHS], B[MAX_GLYPHS];
    long wrap_t = -1;
    for (long t = 36000000L; t < 36000000L + 30000L && wrap_t < 0; t++) {
        fk_face_in_t a = in_at(10, 0, (int)(t / 1000 % 60), (int)(t % 1000));
        const fk_face_in_t b = shifted(a, 50);
        if (rd_vm_ticker_off(&b) < rd_vm_ticker_off(&a)) wrap_t = t;
    }
    TEST_ASSERT_TRUE(wrap_t >= 0);
    const long T[2] = {wrap_t, wrap_t + 4321};
    for (int k = 0; k < 2; k++) {
        fk_face_in_t a = in_at(10, 0, (int)(T[k] / 1000 % 60), (int)(T[k] % 1000));
        a.clk.m = (uint8_t)(T[k] / 60000 % 60);
        const fk_face_in_t b = shifted(a, 50);
        const unsigned na = vm_ticker_boxes(r, &a, RD_VM_TICKER.x0 - 30, RD_VM_TICKER.x1 + 30, A);
        const unsigned nb = vm_ticker_boxes(r, &b, RD_VM_TICKER.x0 + 4, RD_VM_TICKER.x1 - 30, B);
        TEST_ASSERT_TRUE(nb > 20);
        for (unsigned i = 0; i < nb; i++) {
            bool twin = false;
            for (unsigned j = 0; j < na && !twin; j++)
                twin = A[j].y0 == B[i].y0 && A[j].w == B[i].w && A[j].h == B[i].h &&
                       (A[j].x0 - B[i].x0 == 2 || A[j].x0 - B[i].x0 == 3);
            char m[128];
            snprintf(m, sizeof m, "the ticker's glyph at x %d (%s) has no twin 50 ms before", B[i].x0,
                     k ? "a step" : "the wrap");
            TEST_ASSERT_TRUE_MESSAGE(twin, m);
        }
    }
    /* reduced: the still line; the ticker's pixels hold over a second and the next */
    fk_face_in_t a = in_at(10, 9, 30, 0);
    a.reduced = true;
    char t[48];
    uint8_t al;
    TEST_ASSERT_EQUAL_INT(RD_VM_TK_STILL, rd_vm_ticker(&a, t, &al));
    TEST_ASSERT_EQUAL_STRING("ALL ITEMS SOLD OUT", t);
    frame(r, &a, s_c);
    for (int ms = 50; ms < 3000; ms += 250) {
        const fk_face_in_t b = ran(shifted(a, ms), ms);
        frame(r, &b, s_b);
        TEST_ASSERT_EQUAL_INT32(0, differ_in(s_c, s_b, &RD_VM_TICKER));
    }
    /* within a second at rest (a buzz second, 9 x 4 001, and an ordinary one), set, stale and 12 h */
    const omni_rect_t *const ok[4] = {&RD_VM_TICKER, &RD_VM_COLON, &RD_VM_MYSTERY, &RD_VM_LIGHTBOX};
    static const long SOD[2] = {36009L, 36013L};
    unsigned seen[4] = {0};
    for (int k = 0; k < 2; k++)
        for (int st = 0; st < 3; st++)
            for (int ms = 0; ms + 50 <= 1000; ms += 10) {
                fk_face_in_t p = in_at((int)(SOD[k] / 3600), (int)(SOD[k] / 60 % 60), (int)(SOD[k] % 60), ms);
                p.h24 = st != 2, p.clk.stale = st == 1, p.mono_ms = 5000u + (uint32_t)ms;
                const fk_face_in_t q = ran(shifted(p, 50), 50);
                omni_rect_t rects[8];
                const uint8_t n = r->ops->dirty(r->ctx, &p, &q, rects);
                for (unsigned i = 0; i < n; i++) {
                    char m[128];
                    snprintf(m, sizeof m, "vending at %ld.%03d state %d: a dirty rect (%d, %d)-(%d, %d) outside the rest "
                             "regions", SOD[k], ms, st, rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1);
                    TEST_ASSERT_TRUE_MESSAGE(dc_in_any(&rects[i], ok, 4), m);
                    for (int o = 0; o < 4; o++) seen[o] += rect_eq(&rects[i], ok[o]);
                }
                if (ms % 50 == 0) dirty_pair(r, &p, &q, "vending at rest", NULL);
            }
    TEST_ASSERT_TRUE(seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[3] > 0);
}

/* MYSTERY: six taps give the six items in order, each opening the flap for 3.6 s; a sold-out tap shows its line and
 * leaves the order alone; unset, MYSTERY says PLEASE WAIT and leaves it too; every press flips the parity */
static void test_rd_vm_mystery(void)
{
    static const char *const ENJOY[6] = {"ENJOY YOUR SOCK", "ENJOY YOUR SPANNER", "ENJOY YOUR PEA", "ENJOY YOUR FISH",
                                         "ENJOY YOUR IOU", "ENJOY YOUR BATTERY"};
    const row_t *r = row_of("vending");
    const omni_rect_t *m = &RD_VM_BTN[RD_VM_MYSTERY_BTN], *tea = &RD_VM_BTN[1];
    const int mx = (m->x0 + m->x1) / 2, my = (m->y0 + m->y1) / 2, tx = (tea->x0 + tea->x1) / 2,
              ty = (tea->y0 + tea->y1) / 2;
    fk_face_in_t in = in_at(10, 9, 30, 500);
    frame(r, &in, s_c);   /* at rest */
    uint16_t v = 0;
    char t[48];
    uint8_t al;
    for (int k = 0; k < 7; k++) {
        const uint16_t v0 = v;
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, (int16_t)mx, (int16_t)my, &v));
        TEST_ASSERT_EQUAL_UINT16((v0 ^ RD_VM_VAR_PARITY) & RD_VM_VAR_PARITY, v & RD_VM_VAR_PARITY);
        fk_face_in_t p = in;
        p.variant = v, p.pulsing = true;
        for (int ms = 0; ms < 3600; ms += 100) {
            p.pulse_ms = (uint16_t)ms;
            TEST_ASSERT_EQUAL_INT(k % 6, rd_vm_item(&p));
        }
        p.pulse_ms = 0;
        TEST_ASSERT_EQUAL_FLOAT(0.f, rd_vm_flap_open(&p));
        p.pulse_ms = 125;
        TEST_ASSERT_TRUE(rd_vm_flap_open(&p) > 0.8f && rd_vm_flap_open(&p) < 1.f);
        p.pulse_ms = 1000;
        TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_flap_open(&p));
        TEST_ASSERT_EQUAL_INT(RD_VM_TK_MSG, rd_vm_ticker(&p, t, &al));
        TEST_ASSERT_EQUAL_STRING(ENJOY[k % 6], t);
        frame(r, &p, s_a);   /* the open tray, the press over, the message on the ticker */
        TEST_ASSERT_TRUE(differ_in(s_c, s_a, &RD_VM_FLAP) > 1000);
        p.pulse_ms = 3400;
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, rd_vm_flap_open(&p));
        p.pulse_ms = 3599;
        TEST_ASSERT_TRUE(rd_vm_flap_open(&p) < 0.01f);
        fk_face_in_t e = p;
        fk_face_in_advance(r->ops, &e, 1);   /* 3 600: the pulse is over, the tray shut */
        TEST_ASSERT_FALSE(e.pulsing);
        TEST_ASSERT_EQUAL_INT(-1, rd_vm_item(&e));
        frame(r, &e, s_a);
        TEST_ASSERT_EQUAL_INT32(0, differ_in(s_c, s_a, &RD_VM_FLAP));
        /* under reduced motion the flap is open from the tap, no swing */
        p.reduced = true, p.pulse_ms = 10;
        TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_flap_open(&p));
        /* a sold-out press between: its line, its flash, and the order untouched */
        if (k == 2) {
            uint16_t w = v;
            TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, (int16_t)tx, (int16_t)ty, &w));
            TEST_ASSERT_EQUAL_UINT16((v >> 3) & 7u, (w >> 3) & 7u);
            TEST_ASSERT_EQUAL_UINT16(1, w & 7u);
            fk_face_in_t q = in;
            q.variant = w, q.pulsing = true, q.pulse_ms = 100;
            TEST_ASSERT_EQUAL_INT(RD_VM_TK_MSG, rd_vm_ticker(&q, t, &al));
            TEST_ASSERT_EQUAL_STRING("TEA: SOLD OUT", t);
            TEST_ASSERT_EQUAL_INT(-1, rd_vm_item(&q));
            TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lamp(&q, 1));
            q.pulse_ms = 130;
            TEST_ASSERT_EQUAL_FLOAT(0.25f, rd_vm_lamp(&q, 1));
            q.pulse_ms = 1250;
            TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lamp(&q, 1));
            q.pulse_ms = 130, q.reduced = true;   /* no flash under reduced motion */
            TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lamp(&q, 1));
            q.reduced = false, q.pulse_ms = 100;
            frame(r, &q, s_a);
            differ_inside(s_c, s_a, &RD_VM_PANEL, "a sold-out press");
            v = w;
        }
    }
    /* unset: PLEASE WAIT, the item and the order unmoved, the parity flipped */
    in.clk.set = false;
    const uint16_t u0 = v;
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, (int16_t)mx, (int16_t)my, &v));
    TEST_ASSERT_EQUAL_UINT16((u0 >> 3) & 7u, (v >> 3) & 7u);
    TEST_ASSERT_TRUE(v != u0);
    in.variant = v, in.pulsing = true, in.pulse_ms = 100;
    TEST_ASSERT_EQUAL_INT(RD_VM_TK_MSG, rd_vm_ticker(&in, t, &al));
    TEST_ASSERT_EQUAL_STRING("PLEASE WAIT", t);
    TEST_ASSERT_EQUAL_INT(-1, rd_vm_item(&in));
    /* outside the buttons nothing taps; the gaps between them neither */
    uint16_t w = 0;
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 188, 260, &w));
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 150, 289, &w));
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 233, 408, &w));
    TEST_ASSERT_EQUAL_UINT16(0, w);
}

/* 12 h: the blank leading digit keeps its ghost (and no lit segment); AM/PM on the sticker, never on the display or in
 * the glance */
static void test_rd_vm_12h(void)
{
    const row_t *r = row_of("vending");
    char d[5];
    fk_face_in_t in = in_at(9, 59, 30, 500);
    in.h24 = false;
    rd_vm_digits(&in, d);
    TEST_ASSERT_EQUAL_STRING(" 959", d);
    in = in_at(13, 5, 30, 500);
    in.h24 = false;
    rd_vm_digits(&in, d);
    TEST_ASSERT_EQUAL_STRING(" 105", d);
    in.h24 = true;
    rd_vm_digits(&in, d);
    TEST_ASSERT_EQUAL_STRING("1305", d);
    /* the first cell (x 107-160, y 101-191 before the skew): 12 h at 9:59 shows its ghost (#1d1206 at full cover) and no
     * amber; 24 h's 0 lights it */
    const omni_rect_t cell0 = {104, 100, 162, 192};
    fk_face_in_t a = in_at(9, 59, 30, 500), b = a;
    b.h24 = false;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    long ghost = 0, amber_a = 0, amber_b = 0;
    for (int y = cell0.y0; y < cell0.y1; y++)
        for (int x = cell0.x0; x < cell0.x1; x++) {
            ghost += s_b[y * W + x] == RD_RGB(0x1D, 0x12, 0x06);
            amber_a += s_a[y * W + x] == RD_C_AMBER;
            amber_b += s_b[y * W + x] == RD_C_AMBER;
        }
    TEST_ASSERT_TRUE_MESSAGE(ghost > 200, "the blank leading digit lost its ghost");
    TEST_ASSERT_EQUAL_INT32(0, amber_b);
    TEST_ASSERT_TRUE(amber_a > 200);
    /* the sticker: the date and its suffix in 12 h, none in 24 h; the 12 h sticker is wider (it holds the suffix) */
    char s[24];
    a = in_at(10, 9, 30, 500), a.clk.mday = 4, a.clk.wday = 3;
    rd_vm_sticker_date(&a, s);
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV", s);
    a.h24 = false;
    rd_vm_sticker_date(&a, s);
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV \267 AM", s);
    a.clk.h = 22;
    rd_vm_sticker_date(&a, s);
    TEST_ASSERT_EQUAL_STRING("WED 04 NOV \267 PM", s);
    a.clk.set = false;
    rd_vm_sticker_date(&a, s);
    TEST_ASSERT_EQUAL_STRING("--- -- ---", s);
    a = in_at(10, 9, 30, 500), b = a, b.h24 = false;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    differ_inside(s_a, s_b, &RD_VM_STICKER, "12 h's sticker");
    long right = 0;   /* the suffix's ink: the 12 h sticker's own stretch, right of the 24 h one (u past 64) */
    for (int y = RD_VM_STICKER.y0; y < RD_VM_STICKER.y1; y++)
        for (int x = 292; x < RD_VM_STICKER.x1; x++) right += s_a[y * W + x] != s_b[y * W + x];
    TEST_ASSERT_TRUE(right > 300);
    /* the glance: no AM/PM (the glance draws no date); 12 h at 10:09 is 24 h's frame */
    a.glance = b.glance = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* the wake: the lightbox stutters (.9 / .08 by 65 ms to 520), the buttons are dark to 250 ms, the display blank (its
 * ghosts) to 250 and 88:88 to 650, the ticker from 650, the flap's empty drop over 880-1 200; settled after */
static void test_rd_vm_wake(void)
{
    const row_t *r = row_of("vending");
    static const uint8_t ST[8] = {0, 1, 0, 0, 1, 0, 1, 1};
    fk_face_in_t in = in_at(10, 9, 30, 500);
    char d[5], t[48];
    uint8_t al;
    for (uint32_t w = 0; w < 1200; w += 5) {
        in.wake_ms = w;
        const float lit = rd_vm_lit(&in);
        if (w < 520) TEST_ASSERT_EQUAL_FLOAT(ST[w / 65] ? 0.9f : 0.08f, lit);
        else TEST_ASSERT_EQUAL_FLOAT(1.f, lit);
        rd_vm_digits(&in, d);
        TEST_ASSERT_EQUAL_STRING(w < 250 ? "    " : w < 650 ? "8888" : "1009", d);
        TEST_ASSERT_TRUE(rd_vm_colon(&in) == (w >= 250));
        for (int i = 0; i < RD_VM_BTN_N; i++)
            if (w < 250) TEST_ASSERT_EQUAL_FLOAT(0.08f, rd_vm_lamp(&in, i));
        TEST_ASSERT_EQUAL_INT(w <= 650 ? RD_VM_TK_NONE : RD_VM_TK_SCROLL, rd_vm_ticker(&in, t, &al));
        const float o = rd_vm_flap_open(&in);
        if (w <= 880) TEST_ASSERT_EQUAL_FLOAT(0.f, o);
        else TEST_ASSERT_TRUE(o > 0.f && o <= 0.25f);
    }
    in.wake_ms = 1065;   /* the drop's top */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.25f, rd_vm_flap_open(&in));
    /* the frames: blank, the lamp test, landed; the display's ghosts glow from the start */
    fk_face_in_t a = in, b = in, s = in;
    a.wake_ms = 100, b.wake_ms = 400, s.wake_ms = UINT32_MAX;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_VM_TIME) > 2000);
    long amber = 0;
    for (int y = RD_VM_TIME.y0; y < RD_VM_TIME.y1; y++)
        for (int x = RD_VM_TIME.x0; x < RD_VM_TIME.x1; x++) amber += s_a[y * W + x] == RD_C_AMBER;
    TEST_ASSERT_EQUAL_INT32(0, amber);
    frame(r, &s, s_c);
    in.wake_ms = 1199;
    frame(r, &in, s_a);
    differ_inside(s_c, s_a, &RD_VM_FLAP, "the wake's last frame");
    /* reduced motion: the runtime gives no own wake (R179); a wake_ms reads settled */
    a.reduced = s.reduced = true;
    frame(r, &a, s_a);
    frame(r, &s, s_b);
    TEST_ASSERT_EQUAL_INT32(0, frames_differ(s_a, s_b));
}

/* unset (still 7): --:-- (amber dashes, the colon steady), MYSTERY at .25, the unset scroll, --- -- --- */
static void test_rd_vm_unset(void)
{
    const row_t *r = row_of("vending");
    fk_face_in_t in = in_at(0, 0, 0, 700);
    in.clk.set = false;
    char d[5], t[48];
    uint8_t al;
    rd_vm_digits(&in, d);
    TEST_ASSERT_EQUAL_STRING("----", d);
    TEST_ASSERT_TRUE(rd_vm_colon(&in));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, rd_vm_lamp(&in, RD_VM_MYSTERY_BTN));
    TEST_ASSERT_EQUAL_INT(RD_VM_TK_SCROLL, rd_vm_ticker(&in, t, &al));
    in.mono_ms = 1000;
    const float o0 = rd_vm_ticker_off(&in);
    in.mono_ms = 1050;
    TEST_ASSERT_FLOAT_WITHIN(2e-3f, 2.25f, rd_vm_ticker_off(&in) - o0);   /* scrolled from mono_ms */
    in.reduced = true;
    TEST_ASSERT_EQUAL_INT(RD_VM_TK_STILL, rd_vm_ticker(&in, t, &al));
    TEST_ASSERT_EQUAL_STRING("AWAITING TIME SIGNAL", t);
    /* the still: the colon's pixels lit at ms 700 (the set clock's would be off) */
    rd_still_frame(r->ops, r->ctx, 7, s_a);
    long amber = 0;
    for (int y = RD_VM_COLON.y0; y < RD_VM_COLON.y1; y++)
        for (int x = RD_VM_COLON.x0; x < RD_VM_COLON.x1; x++) amber += s_a[y * W + x] == RD_C_AMBER;
    TEST_ASSERT_TRUE(amber > 100);
}

/* stale (still 8): the marker replaces the lightbox's legend, nothing else changes */
static void test_rd_vm_stale_rect(void)
{
    const row_t *r = row_of("vending");
    fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
    b.clk.stale = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_VM_STALE, "vending stale") > 100);
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, 8, s_c);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_c, &RD_VM_STALE, "vending's stale still") > 100);
}

/* the chime (awake, set): the display's glow swells with the thrum (inside RD_VM_TIME), the lightbox too, MYSTERY lights
 * on the notes; nothing under reduced motion or in the glance */
static void test_rd_vm_chime(void)
{
    const row_t *r = row_of("vending");
    fk_face_in_t a = in_at(10, 9, 21, 900), b = a;   /* MYSTERY dark in its blink (870-1 581 of 1 634) */
    TEST_ASSERT_EQUAL_FLOAT(0.3f, rd_vm_lamp(&a, RD_VM_MYSTERY_BTN));
    b.thrum = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_VM_TIME) > 3000);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_VM_LIGHTBOX) > 2000);
    b.thrum = 0.f, b.beep = 1.f;
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lamp(&b, RD_VM_MYSTERY_BTN));
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_VM_MYSTERY, "the chime's note") > 100);
    b.reduced = true;
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lamp(&b, RD_VM_MYSTERY_BTN));   /* steady: no blink, no note */
    b.beep = 0.f;
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_lamp(&b, RD_VM_MYSTERY_BTN));
}

/* the glance: every channel <= 110, the digits in rd.g.amber through the orbit, the MYSTERY dot, unset dashes, the
 * stale marker inside the lightbox's place */
static void test_rd_vm_glance(void)
{
    const row_t *r = row_of("vending");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    in.glance = true;
    frame(r, &in, s_a);
    long ink = 0;
    for (int i = 0; i < W * W; i++) {
        int R, G, B;
        px8(s_a[i], &R, &G, &B);
        TEST_ASSERT_TRUE(R <= 110 && G <= 110 && B <= 110);
        ink += s_a[i] == RD_G_AMBER;
    }
    TEST_ASSERT_TRUE(ink > 1000);
    TEST_ASSERT_EQUAL_HEX16(RD_G_AMBER, s_a[286 * W + 331]);   /* the MYSTERY dot */
    in.odx = 4, in.ody = -4;
    frame(r, &in, s_b);
    TEST_ASSERT_EQUAL_HEX16(RD_G_AMBER, s_b[282 * W + 335]);
    for (int y = 4; y < W - 4; y++)
        for (int x = 4; x < W - 4; x++) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 4) * W + x + 4]);
    in.odx = in.ody = 0, in.clk.stale = true;
    frame(r, &in, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &(omni_rect_t){150, 55, 316, 85}, "the glance's stale marker") > 50);
    in.clk.set = false, in.clk.stale = false;
    frame(r, &in, s_b);
    long dash = 0;
    for (int i = 0; i < W * W; i++) dash += s_b[i] == RD_G_AMBER;
    TEST_ASSERT_TRUE(dash > 100 && dash < ink);
}

/* the glance's worst minutes, pinned (the wave-2 guide): 08:08 in 24 h (26 segments), 10:08 in 12 h (21) */
static void test_rd_vm_glance_worst(void)
{
    const row_t *r = row_of("vending");
    static const struct { int h, m; bool h24; float lo[2], hi[2]; } WORST[] = {
        {8, 8, true, {VM_GW24 - 0.1f, VM_GW24S - 0.1f}, {VM_GW24 + 0.1f, VM_GW24S + 0.1f}},
        {10, 8, false, {VM_GW12 - 0.1f, VM_GW12S - 0.1f}, {VM_GW12 + 0.1f, VM_GW12S + 0.1f}},
    };
    for (unsigned i = 0; i < 2; i++)
        for (int stale = 0; stale < 2; stale++) {
            fk_face_in_t in = in_at(WORST[i].h, WORST[i].m, 30, 500);
            in.glance = true, in.h24 = WORST[i].h24;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date */
            in.clk.stale = stale != 0;
            char what[48];
            snprintf(what, sizeof what, "at %02d:%02d%s (pinned worst)", WORST[i].h, WORST[i].m,
                     stale ? ", stale" : "");
            glance_orbits(r, in, what);
            const float pct = glance_pct(r, &in);
            printf("vending glance %s %s: %.2f %% lit\n", what, WORST[i].h24 ? "24 h" : "12 h", (double)pct);
            TEST_ASSERT_TRUE_MESSAGE(pct >= WORST[i].lo[stale] && pct <= WORST[i].hi[stale], what);
        }
}

/* R169 (the controller's rulings for Task 8): the front is a large lit area, so its area level is measured: the mean
 * of each lit pixel's largest channel (largest > 8) against Bay Mouth's ceiling 115 (as R181 applied it to Nav Scope's
 * giant); the peak is reported, not gated. Over it: stop and report (R146), never dim the art */
static void test_rd_vm_dimming(void)
{
    size_t n = 0;
    const uint8_t *bin = rd_assets_host_get(ASSET_RD_VM_FRONT, &n);
    TEST_ASSERT_NOT_NULL(bin);
    const uint16_t *lay = (const uint16_t *)(const void *)(bin + 12);
    long lit = 0;
    double sum = 0;
    int peak = 0;
    for (int i = 0; i < 340 * 390; i++) {
        int R, G, B;
        px8(lay[i], &R, &G, &B);
        const int m = R > G ? (R > B ? R : B) : (G > B ? G : B);
        peak = m > peak ? m : peak;
        if (m > 8) lit++, sum += m;
    }
    const double mean = sum / (double)lit;
    printf("vending's front: %ld lit px of %d, mean largest channel %.1f (ceiling 115), peak %d\n", lit, 340 * 390,
           mean, peak);
    TEST_ASSERT_TRUE_MESSAGE(mean <= 115.0, "the front's area level is over 45 % (R169)");
}

/* ---- the Bowl (wave 2 Task 9, spec section 10.11) */
/* the mockup's rng(99) (mulberry32), worked out here on its own: kibble i's place (kpos()) */
static void cb_kpos(int i, float *x, float *y, float *s)
{
    uint32_t seed = 99u;
    double v[7] = {0};
    for (int k = 0; k <= i; k++)
        for (int j = 0; j < 7; j++) {
            seed += 0x6D2B79F5u;
            uint32_t t = seed;
            t = (t ^ (t >> 15)) * (t | 1u);
            t ^= t + (t ^ (t >> 7)) * (t | 61u);
            v[j] = (double)(t ^ (t >> 14)) / 4294967296.0;
        }
    const double dr = (v[0] - 0.5) * 10.0, da = (v[1] - 0.5) * 0.04, a = i / 60.0 * 6.283185307179586 - 1.5707963267948966 + da;
    *x = (float)(233.0 + cos(a) * (152.0 + dr));
    *y = (float)(233.0 + sin(a) * (152.0 + dr));
    *s = (float)(0.85 + v[3] * 0.35);
}
/* a kibble's pixel: its browns (#8a5a2b, #6d4420, #a06a34, the highlight over them), never the wall's greys, the lip's
 * red, the kipper's hull, the bones' paint or a crumb (half over the wall) */
static bool cb_kib_px(uint16_t p)
{
    int R, G, B;
    px8(p, &R, &G, &B);
    return R >= 100 && G >= 60 && G <= R - 25 && B <= G - 20 && B <= 75;
}
static long cb_kib_count(const uint16_t *fb, float x, float y, int r)
{
    long n = 0;
    for (int py = (int)y - r; py <= (int)y + r; py++)
        for (int px = (int)x - r; px <= (int)x + r; px++) n += cb_kib_px(fb[py * W + px]);
    return n;
}
static fk_face_in_t cb_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3, in.clk.yday = 308;   /* WED 04 NOV 2026 */
    return in;
}

/* the ops' wiring: the brief's numbers, the rects, the tapped still (a paw frame), the face's file */
static void test_rd_cb_face(void)
{
    TEST_ASSERT_EQUAL_STRING("bowl", RD_FACE_BOWL.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_BOWL.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(1000, RD_FACE_BOWL.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(1600, RD_FACE_BOWL.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(1600, RD_FACE_BOWL.pulse_guard_ms);   /* a tap during a run leaves it (the mockup) */
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_BOWL.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_BOWL.reveal);
    TEST_ASSERT_FALSE(RD_FACE_BOWL.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_BOWL.tap);
    TEST_ASSERT_NOT_NULL(RD_FACE_BOWL.tap_var);
    TEST_ASSERT_EQUAL_UINT16(0, rd_still_variant(&RD_FACE_BOWL));   /* no variant: a tap only summons the paw */
    TEST_ASSERT_EQUAL_UINT16(RD_CB_PAW_STILL_MS, rd_still_pulse_ms(&RD_FACE_BOWL));
    TEST_ASSERT_EQUAL_UINT16(900, RD_CB_PAW_STILL_MS);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_BOWL, rd_face_by_key("bowl"));
    TEST_ASSERT_EQUAL_UINT(0, (RD_PENDING >> RD_F_BOWL) & 1u);
    TEST_ASSERT_EQUAL_HEX32(0x0000u, RD_PENDING);   /* wave 2 Task 10: Mop Bot (id 13) built, none pending */
    TEST_ASSERT_TRUE(RD_FACE_BOWL.ctx_bytes <= 1572864u - 434312u);
    printf("bowl ctx_bytes %u\n", (unsigned)RD_FACE_BOWL.ctx_bytes);
    /* the rects */
    TEST_ASSERT_TRUE(RD_CB_BOWL.x0 == 17 && RD_CB_BOWL.y0 == 17 && RD_CB_BOWL.x1 == 449 && RD_CB_BOWL.y1 == 449);
    TEST_ASSERT_TRUE(rect_eq(&RD_CB_HIT, &RD_CB_BOWL));   /* the tap covers the bowl's box */
    TEST_ASSERT_TRUE(rect_in(&RD_CB_FLOOR, &RD_CB_BOWL) && rect_in(&RD_CB_RING, &RD_CB_BOWL));
    TEST_ASSERT_TRUE(rect_in(&RD_CB_STALE, &RD_CB_FLOOR) && rect_in(&RD_CB_SHEEN, &RD_CB_FLOOR));
    TEST_ASSERT_TRUE(rect_in(&RD_CB_HUB, &RD_CB_FLOOR));
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(2, rd_cb_chime_rects(c));
    TEST_ASSERT_TRUE(rect_eq(&c[0], &RD_CB_SHEEN) && rect_eq(&c[1], &RD_CB_RING));
    /* every kibble (at rest, hopped 6 px, its crumb) inside RD_CB_RING: its reach 8.02 x s + 1 about kpos */
    for (int i = 0; i < 60; i++) {
        float x, y, s;
        cb_kpos(i, &x, &y, &s);
        const float r = 8.02f * s + 1.f;
        TEST_ASSERT_TRUE(x - r >= RD_CB_RING.x0 && x + r + 1 <= RD_CB_RING.x1 && y - r - 6.f >= RD_CB_RING.y0 &&
                         y + r + 1 <= RD_CB_RING.y1);
    }
    /* the face's own file (rd_cb_*): 432 x 432 RGB565, 373 248 B and its 12-byte header, within the 500 000 B cap */
    size_t n = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_CB_BOWL, &n));
    TEST_ASSERT_TRUE(ASSET_RD_CB_BOWL_W == 432 && ASSET_RD_CB_BOWL_H == 432);
    TEST_ASSERT_EQUAL_UINT(373260u, (unsigned)n);
    TEST_ASSERT_TRUE(n <= 500000u);
    printf("bowl's files %u B (cap 500 000)\n", (unsigned)n);
    /* taps: the bowl's box summons the paw (tap_var: FK_TAP_PULSE, the variant left); under reduced motion nothing;
     * unset the pulse plays (and draws nothing: test_rd_cb_unset) */
    const row_t *r = row_of("bowl");
    fk_face_in_t in = cb_at(10, 9, 30, 500);
    uint16_t v = 0x1234;
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, 233, 233, &v));
    TEST_ASSERT_EQUAL_HEX16(0x1234, v);
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, NULL, 17, 448, &v));
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 16, 233, &v));
    in.reduced = true;
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &in, 233, 233, &v));
    in.reduced = false, in.clk.set = false;
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, 233, 233, &v));
    TEST_ASSERT_EQUAL_HEX16(0x1234, v);
    /* still 11: still 0 with the paw 900 ms into a tap's run (full reach, claws out, the kibble in flight), inside the
     * paw's box for the still's minute and the floor */
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, RD_STILL_N, s_b);
    omni_rect_t pr;
    rd_cb_paw_rect(9, &pr);
    long d = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (s_a[y * W + x] == s_b[y * W + x]) continue;
            d++;
            TEST_ASSERT_TRUE_MESSAGE(inside(&pr, x, y) || inside(&RD_CB_FLOOR, x, y), "still 11 differs outside the paw");
        }
    TEST_ASSERT_TRUE(d > 3000);
}

/* 60 - m kibbles left at m 0, 1, 37 and 59, starting where the minute hand points; the eaten ones crumbs; unset none.
 * Kibble i's place is worked out here from the mockup's rng(99) on its own (cb_kpos) */
static void test_rd_cb_kibbles(void)
{
    const row_t *r = row_of("bowl");
    static const int M[4] = {0, 1, 37, 59};
    for (int k = 0; k < 4; k++) {
        fk_face_in_t in = cb_at(3, M[k], 5, 0);   /* :05: no paw */
        TEST_ASSERT_EQUAL_INT(-1, rd_cb_paw_ms(&in));
        TEST_ASSERT_EQUAL_UINT(60u - (unsigned)M[k], rd_cb_kibbles_left(&in.clk));
        frame(r, &in, s_a);
        float ha, ma;
        rd_cb_hand_angles(&in, &ha, &ma);
        float sn = sinf(ma), cs = cosf(ma);
        unsigned seen = 0;
        for (int i = 0; i < 60; i++) {
            float x, y, s;
            cb_kpos(i, &x, &y, &s);
            const long n = cb_kib_count(s_a, x, y, 4);
            char m[96];
            snprintf(m, sizeof m, "m %d: kibble %d shows %ld kibble pixels", M[k], i, n);
            if (i < M[k]) {
                TEST_ASSERT_TRUE_MESSAGE(n == 0, m);   /* eaten: a crumb */
            } else if (i == M[k]) {   /* under the skeleton's head (r 130-152 along it): its edge peeks out */
                TEST_ASSERT_TRUE_MESSAGE(cb_kib_count(s_a, x, y, 7) >= 1, m);
                seen++;
            } else {
                TEST_ASSERT_TRUE_MESSAGE(n >= 8, m);
                seen++;
            }
        }
        TEST_ASSERT_EQUAL_UINT(60u - (unsigned)M[k], seen);
        /* the first one left (m) lies along the minute hand: within its da (.02 rad) of the hand's angle */
        float x, y, s;
        cb_kpos(M[k], &x, &y, &s);
        const float dx = x - 233.f, dy = y - 233.f, rr = sqrtf(dx * dx + dy * dy);
        const float cross = (dx * -cs - dy * sn) / rr, dot = (dx * sn - dy * cs) / rr;
        TEST_ASSERT_TRUE(dot > 0.99f && fabsf(cross) < 0.021f);
    }
    fk_face_in_t in = cb_at(3, 37, 5, 0);
    in.clk.set = false;
    TEST_ASSERT_EQUAL_UINT(0, rd_cb_kibbles_left(&in.clk));
}

/* 12:00:59 -> 12:01:00: kibble 0 is eaten (its own box, beside the hands'); 3 s back over the minute it is back, and
 * the rects cover both ways */
static void test_rd_cb_kibbles_jump(void)
{
    const row_t *r = row_of("bowl");
    /* a fresh wake 10 s old: the paw runs on its fresh schedule (none now), not on the clock's grid, whose run starts at
     * every :00 too (sec_of_day % 20 == 0) and would join the minute's rects */
    fk_face_in_t a = cb_at(12, 0, 59, 0), b = shifted(a, 1000);
    a.mono_ms = 5000, b.mono_ms = 6000;
    a.fresh = b.fresh = true, a.fresh_ms = 10000, b.fresh_ms = 11000;
    omni_rect_t rects[8];
    unsigned n = dirty_pair(r, &a, &b, "bowl 12:00:59 -> 12:01:00", rects);
    TEST_ASSERT_EQUAL_UINT(2, n);   /* the hands, and the kibble */
    float x, y, s;
    cb_kpos(0, &x, &y, &s);
    bool kib = false;
    for (unsigned i = 0; i < n; i++) {
        const int w = rects[i].x1 - rects[i].x0, h = rects[i].y1 - rects[i].y0;
        if (!inside(&rects[i], (int)x, (int)y) || w > 24 || h > 24) continue;
        kib = true;
        printf("bowl: the eaten kibble's rect %d x %d (its reach 8.02 x %.2f + 1 about its place)\n", w, h, (double)s);
        TEST_ASSERT_TRUE(w >= 14 && h >= 14);
    }
    TEST_ASSERT_TRUE_MESSAGE(kib, "no rect of the eaten kibble's own");
    frame(r, &a, s_c);
    TEST_ASSERT_TRUE(cb_kib_count(s_c, x, y, 4) >= 8);
    TEST_ASSERT_EQUAL_INT32(0, cb_kib_count(s_b, x, y, 4));   /* dirty_pair left b's frame in s_b */
    fk_face_in_t c = shifted(b, -3000);   /* 12:00:57: back */
    c.fresh_ms = 8000;
    n = dirty_pair(r, &b, &c, "bowl 12:01:00 -> 12:00:57", rects);
    TEST_ASSERT_TRUE(n >= 2);
    TEST_ASSERT_TRUE(cb_kib_count(s_b, x, y, 4) >= 8);
    dirty_pair(r, &c, &b, "bowl 12:00:57 -> 12:01:00", NULL);
    /* on the clock's grid the paw starts with the minute: its rects join the kibble's and the hands' */
    a.fresh = b.fresh = false;
    dirty_pair(r, &a, &b, "bowl 12:00:59 -> 12:01:00, the paw's start", NULL);
    /* the hour's refill (12:59 -> 13:00) and a long skip: the ring */
    a = cb_at(12, 59, 59, 900), b = shifted(a, 100);
    dirty_pair(r, &a, &b, "bowl 12:59:59.900 -> 13:00", NULL);
    dirty_pair(r, &b, &a, "bowl 13:00 -> 12:59:59.900", NULL);
    a = cb_at(12, 10, 5, 0), b = cb_at(12, 31, 5, 0);
    dirty_pair(r, &a, &b, "bowl 12:10 -> 12:31", NULL);
}

/* the paw's schedule (W2-D6): fresh, 2.5 s after the wake then every 20 s; after the fresh minute on the clock's 20 s
 * grid; a tap's pulse at once; never under reduced motion, unset, at m 59 or in the glance. Every dirty rect of a run
 * lies inside the paw's box for its minute and the floor */
static void test_rd_cb_paw_schedule(void)
{
    const row_t *r = row_of("bowl");
    fk_face_in_t in = cb_at(10, 9, 13, 0);   /* off the clock's grid: only the fresh schedule runs */
    in.fresh = true;
    for (int f = 0; f < 60000; f += 50) {
        in.fresh_ms = (uint16_t)f;
        const int want = f >= 2500 && f < 4100 ? f - 2500 : f >= 22500 && f < 24100 ? f - 22500
                       : f >= 42500 && f < 44100 ? f - 42500 : -1;
        TEST_ASSERT_EQUAL_INT(want, rd_cb_paw_ms(&in));
    }
    in.fresh = false, in.fresh_ms = 0;
    for (long t = 0; t < 86400000L; t += 250) {   /* the whole day on the clock */
        fk_face_in_t q = shifted(cb_at(0, 0, 0, 0), t);
        const long ph = t % 20000;
        const int want = q.clk.m < 59 && ph < 1600 ? (int)ph : -1;
        if (rd_cb_paw_ms(&q) != want) {
            char m[96];
            snprintf(m, sizeof m, "bowl paw at %02u:%02u:%02u.%03u: %d, want %d", q.clk.h, q.clk.m, q.clk.s, q.clk.ms,
                     rd_cb_paw_ms(&q), want);
            TEST_FAIL_MESSAGE(m);
        }
    }
    in = cb_at(10, 9, 13, 0);
    in.pulsing = true, in.pulse_ms = 0;   /* a tap: at once, over any schedule */
    TEST_ASSERT_EQUAL_INT(0, rd_cb_paw_ms(&in));
    in.pulse_ms = 1599;
    TEST_ASSERT_EQUAL_INT(1599, rd_cb_paw_ms(&in));
    in.fresh = true, in.fresh_ms = 3000, in.pulse_ms = 200;
    TEST_ASSERT_EQUAL_INT(200, rd_cb_paw_ms(&in));
    in.pulse_ms = 1600;   /* the pulse over: the fresh schedule's */
    TEST_ASSERT_EQUAL_INT(500, rd_cb_paw_ms(&in));
    fk_face_in_t q = in;
    q.reduced = true;
    TEST_ASSERT_EQUAL_INT(-1, rd_cb_paw_ms(&q));
    q.pulse_ms = 100;
    TEST_ASSERT_EQUAL_INT(-1, rd_cb_paw_ms(&q));
    q = in, q.clk.set = false;
    TEST_ASSERT_EQUAL_INT(-1, rd_cb_paw_ms(&q));
    q = in, q.glance = true;
    TEST_ASSERT_EQUAL_INT(-1, rd_cb_paw_ms(&q));
    q = in, q.clk.m = 59, q.pulse_ms = 100;
    TEST_ASSERT_EQUAL_INT(-1, rd_cb_paw_ms(&q));
    /* under reduced motion the frame at a paw's time is the frame without it */
    q = cb_at(10, 9, 40, 500), q.reduced = true;
    fk_face_in_t q2 = cb_at(10, 9, 42, 500);
    q2.reduced = true;
    frame(r, &q, s_a);
    frame(r, &q2, s_b);
    assert_same(s_a, s_b, "bowl reduced: no paw on the 20 s grid");
    q.pulsing = true, q.pulse_ms = 900, q.fresh = true, q.fresh_ms = 3400;
    frame(r, &q, s_b);
    assert_same(s_a, s_b, "bowl reduced: no paw for a pulse or the fresh run");
    /* the runs: every dirty rect inside rd_cb_paw_rect(m) and the floor, frame to frame and over skips */
    int wmin = W, wmax = 0, hmin = W, hmax = 0;
    for (int m = 0; m < 59; m += 1) {
        omni_rect_t pr;
        rd_cb_paw_rect((unsigned)m, &pr);
        const int w = pr.x1 - pr.x0, h = pr.y1 - pr.y0;
        wmin = w < wmin ? w : wmin, wmax = w > wmax ? w : wmax, hmin = h < hmin ? h : hmin, hmax = h > hmax ? h : hmax;
        /* the Task 9 review's m5: every minute's rects held to its box (dirty() alone, cheap); fifteen minutes' runs
         * also drawn (dirty_pair: every quarter's angles) */
        const bool drawn = m % 4 == 1 || m == 58;
        fk_face_in_t p = cb_at(10, m, 13, 0);
        p.mono_ms = 1000;
        p.pulsing = true;
        static const int STEP[] = {40, 40, 100, 1000, -40};
        for (int pt = 0; pt <= 1640; pt += 40)
            for (unsigned k = 0; k < sizeof STEP / sizeof STEP[0]; k++) {
                if (k && pt % 200) continue;
                fk_face_in_t a = p, b;
                a.pulse_ms = (uint16_t)pt;
                b = a;
                const int to = pt + STEP[k];
                if (to < 0) continue;
                b.pulsing = to < 1600, b.pulse_ms = (uint16_t)(to < 1600 ? to : 0);
                char what[80];
                snprintf(what, sizeof what, "bowl's paw at m %d: %d -> %d ms", m, pt, to);
                omni_rect_t rects[8];
                const unsigned n = r->ops->dirty(r->ctx, &a, &b, rects);
                for (unsigned i = 0; i < n; i++)
                    for (int y = rects[i].y0; y < rects[i].y1; y++)
                        for (int x = rects[i].x0; x < rects[i].x1; x++)
                            if (!inside(&pr, x, y) && !inside(&RD_CB_FLOOR, x, y)) {
                                char mm[160];
                                snprintf(mm, sizeof mm, "%s: rect (%d, %d)-(%d, %d) past the paw's box and the floor",
                                         what, rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1);
                                TEST_FAIL_MESSAGE(mm);
                            }
                if (drawn && (k == 0 || pt % 400 == 0)) dirty_pair(r, &a, &b, what, NULL);
            }
    }
    printf("bowl: the paw's box per minute %d-%d x %d-%d px\n", wmin, wmax, hmin, hmax);
    /* and a run from the clock's grid and the fresh one draws the paw, not only its pulse */
    fk_face_in_t g = cb_at(10, 9, 40, 900), z = cb_at(10, 9, 42, 900);
    frame(r, &g, s_a);
    frame(r, &z, s_b);
    omni_rect_t pr;
    rd_cb_paw_rect(9, &pr);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &pr) > 3000);
}

/* still 7 (unset): the empty bowl, the layer itself but for --:-- and BOWL EMPTY on the floor (no fish, hub or kibble) */
static void test_rd_cb_unset(void)
{
    const row_t *r = row_of("bowl");
    rd_still_frame(r->ops, r->ctx, 7, s_a);
    size_t n = 0;
    const uint8_t *bin = rd_assets_host_get(ASSET_RD_CB_BOWL, &n);
    TEST_ASSERT_NOT_NULL(bin);
    const uint16_t *lay = (const uint16_t *)(const void *)(bin + 12);
    const omni_rect_t text = {150, 195, 317, 285};   /* --:-- (stencil 64, baseline 247) and BOWL EMPTY (276) */
    TEST_ASSERT_TRUE(rect_in(&text, &RD_CB_FLOOR));
    long inked = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const uint16_t want = inside(&RD_CB_BOWL, x, y) ? lay[(y - 17) * 432 + (x - 17)] : 0;
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
            if (dx * dx + dy * dy < 186.f * 186.f) TEST_ASSERT_FALSE(cb_kib_px(s_a[y * W + x]));   /* the wall */
            if (s_a[y * W + x] == want) continue;
            TEST_ASSERT_TRUE_MESSAGE(inside(&text, x, y), "the unset bowl differs from its layer outside the text");
            inked++;
        }
    TEST_ASSERT_TRUE(inked > 400);
    /* the text: paintDim glyphs of the stencil 64 (--:--) and Chakra 13 (BOWL EMPTY) atlases */
    s_ngl = 0;
    fk_test_glyph_hook = record;
    rd_still_frame(r->ops, r->ctx, 7, s_b);
    fk_test_glyph_hook = NULL;
    TEST_ASSERT_EQUAL_UINT(5 + 9, s_ngl);   /* --:-- and BOWL EMPTY's nine letters (the space draws nothing) */
    long dim = 0;
    for (int i = 0; i < W * W; i++) dim += s_a[i] == RD_C_PAINT_DIM;
    TEST_ASSERT_TRUE(dim > 100);
    /* a pulse in flight changes nothing unset (no paw), nor does the chime */
    fk_face_in_t in = cb_at(10, 9, 30, 500);
    in.clk.set = false, in.pulsing = true, in.pulse_ms = 900, in.thrum = 1.f, in.beep = 1.f;
    frame(r, &in, s_b);
    assert_same(s_a, s_b, "the unset bowl with a pulse and the chime");
}

/* the wake: the kibbles rain in (none at 0 ms, all landed by 1 000) and the fish swing from 12; the bowl's box; under
 * reduced motion the settled frame */
static void test_rd_cb_wake(void)
{
    const row_t *r = row_of("bowl");
    fk_face_in_t in = cb_at(10, 42, 5, 0), st = in;
    frame(r, &st, s_c);
    in.wake_ms = 0;
    frame(r, &in, s_a);
    for (int i = 42; i < 60; i++) {
        float x, y, s;
        cb_kpos(i, &x, &y, &s);
        TEST_ASSERT_EQUAL_INT32(0, cb_kib_count(s_a, x, y, 4));
    }
    TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &RD_CB_BOWL, "the wake at 0 ms") > 2000);
    in.wake_ms = 300;   /* the first ones have landed, the last not yet in */
    frame(r, &in, s_a);
    float x, y, s;
    cb_kpos(42, &x, &y, &s);
    TEST_ASSERT_TRUE(cb_kib_count(s_a, x, y, 4) >= 8);
    cb_kpos(59, &x, &y, &s);
    TEST_ASSERT_EQUAL_INT32(0, cb_kib_count(s_a, x, y, 4));
    in.wake_ms = 999;
    frame(r, &in, s_a);
    assert_same(s_c, s_a, "the wake's last frame is the settled one");
    in.wake_ms = 1000;
    frame(r, &in, s_a);
    assert_same(s_c, s_a, "past the wake");
    omni_rect_t rects[8];
    fk_face_in_t a = cb_at(10, 42, 5, 0), b;
    a.wake_ms = 400, b = ran(a, 40);
    TEST_ASSERT_EQUAL_UINT(1, dirty_pair(r, &a, &b, "bowl's wake", rects));
    TEST_ASSERT_TRUE(rect_eq(&rects[0], &RD_CB_BOWL));
    in.wake_ms = 300, in.reduced = true;
    frame(r, &in, s_a);
    st.reduced = true;
    frame(r, &st, s_b);
    assert_same(s_b, s_a, "a wake under reduced motion");
    /* the wake shots' frames are in the bowl's box: the rain and the swing (the device records its rate, R145) */
    a.wake_ms = 0, b = a, b.wake_ms = UINT32_MAX;
    dirty_pair(r, &a, &b, "the wake's whole run", rects);
    /* the Task 9 review's m3: one wake time, two minutes (the rain's d reads i - m): the bowl's box all the same */
    static const long DM[] = {60000, -60000, 1260000, 3600000};
    for (unsigned k = 0; k < sizeof DM / sizeof DM[0]; k++) {
        a = cb_at(10, 42, 5, 0), a.wake_ms = 400;
        b = shifted(a, DM[k]);
        const unsigned n = dirty_pair(r, &a, &b, "bowl's wake: one wake time, another minute", rects);
        bool bowl = false;
        for (unsigned i = 0; i < n; i++) bowl = bowl || rect_in(&RD_CB_BOWL, &rects[i]);
        TEST_ASSERT_TRUE_MESSAGE(bowl, "bowl's wake at one wake time over a minute's change leaves out the bowl's box");
    }
}

/* stale: the marker at the top of the floor, nothing else */
static void test_rd_cb_stale(void)
{
    const row_t *r = row_of("bowl");
    fk_face_in_t a = cb_at(10, 9, 30, 500), b = a;
    b.clk.stale = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_CB_STALE, "bowl stale") > 100);
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, 8, s_c);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_c, &RD_CB_STALE, "bowl's stale still") > 100);
}

/* the chime: the sheen brightens with the thrum (RD_CB_SHEEN), the next kibble hops with the beep; nothing under reduced
 * motion */
static void test_rd_cb_chime(void)
{
    const row_t *r = row_of("bowl");
    fk_face_in_t a = cb_at(10, 9, 21, 900), b = a;
    b.thrum = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_CB_SHEEN, "the sheen") > 2000);
    b.thrum = 0.f, b.beep = 1.f;
    frame(r, &b, s_b);
    float x, y, s;
    cb_kpos(9, &x, &y, &s);
    const float rr = 8.02f * s + 1.f;
    const omni_rect_t hop = {(int16_t)(x - rr - 7.f), (int16_t)(y - rr - 7.f), (int16_t)(x + rr + 2.f),
                             (int16_t)(y + rr + 2.f)};
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &hop, "the next kibble's hop") > 20);
    b.reduced = true, b.thrum = 1.f, a.reduced = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    assert_same(s_a, s_b, "the chime under reduced motion");
}

/* the glance: every channel <= 110, the lip and the four numerals, the fish outlines through the orbit, the stale
 * marker, unset dashes */
static void test_rd_cb_glance(void)
{
    const row_t *r = row_of("bowl");
    fk_face_in_t in = cb_at(10, 9, 30, 500);
    in.glance = true;
    frame(r, &in, s_a);
    long hull = 0, paint = 0;
    for (int i = 0; i < W * W; i++) {
        int R, G, B;
        px8(s_a[i], &R, &G, &B);
        TEST_ASSERT_TRUE(R <= 110 && G <= 110 && B <= 110);
        hull += s_a[i] == RD_G_HULL;
        paint += s_a[i] == RD_G_PAINT;
    }
    printf("bowl glance: %ld px at rd.g.hull, %ld at rd.g.paint\n", hull, paint);
    TEST_ASSERT_TRUE(hull > 300 && paint > 100);
    in.odx = 4, in.ody = -4;
    frame(r, &in, s_b);
    for (int y = 4; y < W - 4; y++)
        for (int x = 4; x < W - 4; x++) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 4) * W + x + 4]);
    in.odx = in.ody = 0, in.clk.stale = true;
    frame(r, &in, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_CB_STALE, "the glance's stale marker") > 50);
    in.clk.set = false, in.clk.stale = false;
    frame(r, &in, s_b);
    long dash = 0;
    for (int i = 0; i < W * W; i++) dash += s_b[i] == RD_G_PAINT;
    TEST_ASSERT_TRUE(dash > 100);
    /* 12 h and 24 h draw the same glance (no text but the numerals) */
    in = cb_at(22, 9, 30, 500), in.glance = true, in.h24 = false;
    frame(r, &in, s_b);
    assert_same(s_a, s_b, "the glance in 12 h");
}

/* the glance's worst minutes, pinned (the wave-2 guide): 24 h and 12 h, set and stale, at the widest date */
static void test_rd_cb_glance_worst(void)
{
    const row_t *r = row_of("bowl");
    static const struct { int h, m; bool h24; float pct[2]; } WORST[] = {
        {CB_GW_H, CB_GW_M, true, {CB_GW, CB_GWS}},
        {CB_GW_H + 12, CB_GW_M, false, {CB_GW, CB_GWS}},
    };
    for (unsigned i = 0; i < 2; i++)
        for (int stale = 0; stale < 2; stale++) {
            fk_face_in_t in = in_at(WORST[i].h, WORST[i].m, 30, 500);
            in.glance = true, in.h24 = WORST[i].h24;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date */
            in.clk.stale = stale != 0;
            char what[48];
            snprintf(what, sizeof what, "at %02d:%02d%s (pinned worst)", WORST[i].h, WORST[i].m,
                     stale ? ", stale" : "");
            glance_orbits(r, in, what);
            const float pct = glance_pct(r, &in);
            printf("bowl glance %s %s: %.2f %% lit\n", what, WORST[i].h24 ? "24 h" : "12 h", (double)pct);
            TEST_ASSERT_TRUE_MESSAGE(pct >= WORST[i].pct[stale] - 0.1f && pct <= WORST[i].pct[stale] + 0.1f, what);
        }
}

/* R169: the bowl is a large lit area (the lip's warm enamel among it), so its area level is measured: the mean of each
 * lit pixel's largest channel (largest > 8) against Bay Mouth's ceiling 115 (as R181 and Task 8 applied it); the warm
 * lip alone too; the peak is reported, not gated. Over it: stop and report (R146), never dim the art */
static void test_rd_cb_dimming(void)
{
    size_t n = 0;
    const uint8_t *bin = rd_assets_host_get(ASSET_RD_CB_BOWL, &n);
    TEST_ASSERT_NOT_NULL(bin);
    const uint16_t *lay = (const uint16_t *)(const void *)(bin + 12);
    long lit = 0, wlit = 0;
    double sum = 0, wsum = 0;
    int peak = 0;
    for (int y = 0; y < 432; y++)
        for (int x = 0; x < 432; x++) {
            int R, G, B;
            px8(lay[y * 432 + x], &R, &G, &B);
            const int m = R > G ? (R > B ? R : B) : (G > B ? G : B);
            peak = m > peak ? m : peak;
            if (m <= 8) continue;
            lit++, sum += m;
            const float dx = (float)x + 0.5f - 216.f, dy = (float)y + 0.5f - 216.f;
            if (dx * dx + dy * dy >= 186.f * 186.f) wlit++, wsum += m;   /* the lip: outside the wall's r 186 */
        }
    const double mean = sum / (double)lit, wmean = wsum / (double)wlit;
    printf("bowl's layer: %ld lit px of %d, mean largest channel %.1f (ceiling 115), the lip's %ld px %.1f, peak %d\n",
           lit, 432 * 432, mean, wlit, wmean, peak);
    TEST_ASSERT_TRUE_MESSAGE(mean <= 115.0, "the bowl's area level is over 45 % (R169)");
    TEST_ASSERT_TRUE_MESSAGE(wmean <= 115.0, "the lip's area level is over 45 % (R169)");
}

/* R179 (the controller's rulings for Task 9): no reduced-motion wake fade (the runtime gives no own wake under reduced
 * motion), but the ambient motion is stilled: under reduced motion the paw never runs (its grid, the fresh run, a tap),
 * the chime moves nothing, and a minute's frames hold still between the minute's changes */
static void test_rd_cb_reduced(void)
{
    const row_t *r = row_of("bowl");
    fk_face_in_t a = cb_at(10, 9, 0, 0);
    a.reduced = true;
    frame(r, &a, s_c);
    for (int t = 0; t < 60000; t += 250) {
        fk_face_in_t b = ran(shifted(a, t), t);
        b.fresh = t < 30000, b.fresh_ms = (uint16_t)(t < 30000 ? t : 0);
        b.pulsing = t % 2000 < 1600, b.pulse_ms = (uint16_t)(t % 2000);
        b.thrum = 0.7f, b.beep = 1.f;
        TEST_ASSERT_EQUAL_INT(-1, rd_cb_paw_ms(&b));
        frame(r, &b, s_a);
        char what[64];
        snprintf(what, sizeof what, "bowl reduced at 10:09 + %d ms", t);
        assert_same(s_c, s_a, what);
    }
    uint16_t v = 0;
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &a, 233, 233, &v));
}

/* the Task 8 review's M5: Vending Machine's tapped still is a vend (the press's pulse 1 000 ms in), not still 0 */
static void test_rd_vm_still_vend(void)
{
    const row_t *r = row_of("vending");
    TEST_ASSERT_EQUAL_UINT16(1000, rd_still_pulse_ms(&RD_FACE_VENDING));
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, RD_STILL_N, s_b);
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_VM_FLAP) > 1000);    /* the flap open on the sock */
    TEST_ASSERT_TRUE(differ_in(s_a, s_b, &RD_VM_TICKER) > 200);   /* ENJOY YOUR SOCK */
    fk_face_in_t in;
    rd_still_t st;
    rd_still(0, &st);
    in = st.in;
    in.variant = RD_VM_TAPPED_VARIANT, in.pulsing = true, in.pulse_ms = 1000;
    TEST_ASSERT_EQUAL_INT(0, rd_vm_item(&in));
    TEST_ASSERT_EQUAL_FLOAT(1.f, rd_vm_flap_open(&in));
    frame(r, &in, s_c);
    assert_same(s_b, s_c, "vending's still 11 is the vend at 1 000 ms");
    /* every other face's tapped still stays at rest (its CRC unchanged) */
    for (unsigned i = 0; i < s_nrows; i++)
        if (s_rows[i].ops != &RD_FACE_VENDING && s_rows[i].ops != &RD_FACE_BOWL && s_rows[i].ops != &RD_FACE_MOPBOT)
            TEST_ASSERT_EQUAL_UINT16(0, rd_still_pulse_ms(s_rows[i].ops));
    TEST_ASSERT_EQUAL_UINT16(RD_MB_HOP_STILL_MS, rd_still_pulse_ms(&RD_FACE_MOPBOT));   /* wave 2 Task 10 */
    /* the Task 9 review's m1: the sold-out press too, TEA's pulse 100 ms in (its lamp's flash and its line), pinned by
     * its CRC (rd_still_frame's own input: still 0 with TEA last pressed, the parity flipped by that press) */
    in = st.in;
    in.variant = 1u | RD_VM_VAR_PARITY, in.pulsing = true, in.pulse_ms = 100;
    frame(r, &in, s_c);
    TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_VM_BTN[1]) > 50);      /* TEA darkened, its lamp flashing */
    TEST_ASSERT_TRUE(differ_in(s_a, s_c, &RD_VM_TICKER) > 200);     /* TEA: SOLD OUT */
    char crc[64];
    snprintf(crc, sizeof crc, "vending's TEA at 100 ms: %08x", (unsigned)fk_crc32(0, s_c, sizeof s_c));
    printf("%s\n", crc);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(VM_TEA100_CRC, fk_crc32(0, s_c, sizeof s_c), crc);
}

/* ---- Mop Bot (wave 2 Task 10, spec section 10.13) */
/* R170: the hazard ring (r 190, 2 px and its AA) carries spec 10.0's burn-in motion (a crawl of 1 px a second) */
static bool mb_moving(int x, int y)
{
    const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f, d2 = dx * dx + dy * dy;
    return d2 >= 187.5f * 187.5f && d2 <= 192.5f * 192.5f;
}
static fk_face_in_t mb_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.clk.mday = 4, in.clk.mon = 10, in.clk.wday = 3, in.clk.yday = 308;   /* WED 04 NOV 2026 */
    return in;
}
/* the robot's box at rest (bot()'s scale 1.25, no hop) about its place at in's clock, a pixel inside the face's own */
static omni_rect_t mb_rest_box(const fk_face_in_t *in)
{
    float x, y;
    rd_mb_robot_xy(&in->clk, &x, &y);
    const float h = 44.f * 1.25f;
    const float x0 = x - h < 0 ? 0 : x - h, y0 = y - h < 0 ? 0 : y - h;   /* on the screen (parked: near its top) */
    const float x1 = x + h + 1.f > W ? W : x + h + 1.f, y1 = y + h + 1.f > W ? W : y + h + 1.f;
    return (omni_rect_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}
/* every pixel of box lies in one of the n rects */
static bool mb_covered(const omni_rect_t *box, const omni_rect_t *rects, unsigned n)
{
    for (int y = box->y0 < 0 ? 0 : box->y0; y < box->y1 && y < W; y++)
        for (int x = box->x0 < 0 ? 0 : box->x0; x < box->x1 && x < W; x++)
            if (!in_any(rects, n, x, y)) return false;
    return true;
}

/* the ops' wiring: the brief's numbers, the rects, the tile, the robot's line, the tapped still (a hop) */
static void test_rd_mb_face(void)
{
    const row_t *r = row_of("mopbot");
    TEST_ASSERT_EQUAL_STRING("mopbot", RD_FACE_MOPBOT.key);
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_MOPBOT.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(1200, RD_FACE_MOPBOT.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(900, RD_FACE_MOPBOT.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(800, RD_FACE_MOPBOT.pulse_guard_ms);   /* a tap during the spin leaves it (the mockup) */
    TEST_ASSERT_EQUAL_UINT8(1, RD_FACE_MOPBOT.stills_extra);
    TEST_ASSERT_FALSE(RD_FACE_MOPBOT.reveal);
    TEST_ASSERT_FALSE(RD_FACE_MOPBOT.direct);
    TEST_ASSERT_NOT_NULL(RD_FACE_MOPBOT.tap);
    TEST_ASSERT_NOT_NULL(RD_FACE_MOPBOT.tap_var);
    TEST_ASSERT_EQUAL_INT(36, RD_MB_HIT_R);
    TEST_ASSERT_EQUAL_UINT16(0, rd_still_variant(&RD_FACE_MOPBOT));   /* no variant: a tap only startles it */
    TEST_ASSERT_EQUAL_UINT16(RD_MB_HOP_STILL_MS, rd_still_pulse_ms(&RD_FACE_MOPBOT));
    TEST_ASSERT_TRUE(RD_MB_HOP_STILL_MS > 0 && RD_MB_HOP_STILL_MS < 800);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_MOPBOT, rd_face_by_key("mopbot"));
    TEST_ASSERT_EQUAL_UINT(0, (RD_PENDING >> RD_F_MOPBOT) & 1u);
    TEST_ASSERT_TRUE(RD_FACE_MOPBOT.ctx_bytes <= 1572864u - 434312u);
    printf("mopbot ctx_bytes %u\n", (unsigned)RD_FACE_MOPBOT.ctx_bytes);
    /* the rects: the marker above the time, the date below it, all on the screen */
    TEST_ASSERT_TRUE(RD_MB_STALE.y1 <= RD_MB_TIME.y0 && RD_MB_TIME.y1 <= RD_MB_DATE.y0);
    omni_rect_t c[RD_ST_CHIME_MAX];
    TEST_ASSERT_EQUAL_UINT(2, rd_mb_chime_rects(c));
    TEST_ASSERT_TRUE(rect_eq(&c[0], &RD_MB_SIGN) && rect_eq(&c[1], &RD_MB_TRACK));
    /* the face's own file (rd_mb_*): the tread tile, 44 x 44 RGB565 (3 872 B and its 12-byte header) */
    size_t n = 0;
    TEST_ASSERT_NOT_NULL(rd_assets_host_get(ASSET_RD_MB_TREAD, &n));
    TEST_ASSERT_TRUE(ASSET_RD_MB_TREAD_W == 44 && ASSET_RD_MB_TREAD_H == 44);
    TEST_ASSERT_EQUAL_UINT(44 * 44 * 2 + 12, n);
    TEST_ASSERT_TRUE(n <= 500000u);
    /* the robot's line: (m + s / 60) / 60 of a turn on r 207 from 12; unset parked .11 rad past 12 */
    fk_face_in_t in = mb_at(10, 9, 30, 500);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 9.5f / 60.f, rd_mb_robot_turn(&in.clk));
    float x, y;
    for (int m = 0; m < 60; m += 5) {
        in = mb_at(3, m, 0, 0);
        rd_mb_robot_xy(&in.clk, &x, &y);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 207.f, sqrtf((x - 233.f) * (x - 233.f) + (y - 233.f) * (y - 233.f)));
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 233.f + 207.f * sinf((float)m / 60.f * 6.2831853f), x);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 233.f - 207.f * cosf((float)m / 60.f * 6.2831853f), y);
    }
    in.clk.set = false;
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.11f / 6.2831853f, rd_mb_robot_turn(&in.clk));
    /* still 11 is the robot mid-hop (a tap 300 ms ago): it differs from still 0 only inside the robot's box */
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, RD_STILL_N, s_b);
    rd_still_t st;
    rd_still(0, &st);
    omni_rect_t box;
    rd_mb_robot_box(&st.in, &box);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &box, "mopbot's tapped still") > 1000);
}

/* the tap follows the robot (I3): at m 0, 15, 30 and 45 a tap at its centre +- 36 px hits (inclusive, M10), +- 37
 * misses; the robot's place an hour-quarter on misses; tap() taps nothing anywhere */
static void test_rd_mb_hit_moves(void)
{
    const row_t *r = row_of("mopbot");
    static const int CP[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (int m = 0; m < 60; m += 15) {
        fk_face_in_t in = mb_at(10, m, 0, 0);
        int cx, cy;
        mb_robot_at(&in, &cx, &cy);
        uint16_t v = 0;
        for (int k = 0; k < 4; k++) {
            char what[64];
            snprintf(what, sizeof what, "mopbot m %d: +-36 along (%d, %d)", m, CP[k][0], CP[k][1]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(FK_TAP_PULSE,
                                          r->ops->tap_var(r->ctx, &in, (int16_t)(cx + 36 * CP[k][0]),
                                                          (int16_t)(cy + 36 * CP[k][1]), &v), what);
            TEST_ASSERT_EQUAL_INT_MESSAGE(FK_TAP_NONE,
                                          r->ops->tap_var(r->ctx, &in, (int16_t)(cx + 37 * CP[k][0]),
                                                          (int16_t)(cy + 37 * CP[k][1]), &v), what);
            TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap(r->ctx, (int16_t)(cx + 36 * CP[k][0]),
                                                           (int16_t)(cy + 36 * CP[k][1])));
        }
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, (int16_t)cx, (int16_t)cy, &v));
        TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap(r->ctx, (int16_t)cx, (int16_t)cy));
        TEST_ASSERT_EQUAL_UINT16(0, v);   /* the variant left */
        fk_face_in_t later = mb_at(10, (m + 15) % 60, 0, 0);
        TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &later, (int16_t)cx, (int16_t)cy, &v));
        in.reduced = true;   /* reduced motion: the tap still lands (its eye flashes) */
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &in, (int16_t)cx, (int16_t)cy, &v));
    }
    for (int y = -30; y < W + 30; y += 7)
        for (int x = -30; x < W + 30; x += 7) TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap(r->ctx, (int16_t)x, (int16_t)y));
    fk_face_in_t u = mb_at(0, 0, 0, 0);   /* unset: parked by the sign */
    u.clk.set = false;
    int px, py;
    mb_robot_at(&u, &px, &py);
    uint16_t v = 0;
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &u, (int16_t)px, (int16_t)(py + 36), &v));
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, &u, (int16_t)px, (int16_t)(py + 37), &v));
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap_var(r->ctx, NULL, (int16_t)px, (int16_t)py, &v));
}

/* the robot across a -3 s step, +61 s and midnight: its old and new boxes both in dirty(), nothing of it left behind */
static void test_rd_mb_robot_jump(void)
{
    const row_t *r = row_of("mopbot");
    static const struct { int h, m, s, ms; long step; } J[] = {
        {10, 9, 30, 500, -3000}, {10, 9, 1, 0, -3000},  {10, 0, 1, 500, -3000}, {10, 9, 30, 500, 61000},
        {10, 59, 30, 0, 61000},  {23, 59, 59, 500, 1000}, {23, 59, 30, 0, 61000}, {0, 0, 0, 500, -1000},
    };
    for (unsigned j = 0; j < sizeof J / sizeof J[0]; j++)
        for (int w = 0; w < 2; w++) {
            fk_face_in_t a = mb_at(J[j].h, J[j].m, J[j].s, J[j].ms), b = shifted(a, J[j].step);
            a.mono_ms = b.mono_ms = w ? 4321u : 0u;   /* the wobble at 0 (mono 0) and mid-swing */
            omni_rect_t rects[8];
            char what[96];
            snprintf(what, sizeof what, "mopbot's robot from %02d:%02d:%02d.%03d by %+ld ms (mono %u)", J[j].h, J[j].m,
                     J[j].s, J[j].ms, J[j].step, (unsigned)a.mono_ms);
            const unsigned n = dirty_pair(r, &a, &b, what, rects);
            omni_rect_t ba = mb_rest_box(&a), bb = mb_rest_box(&b);
            if (w) {   /* mid-swing the robot sits up to 2.5 px off its line's point */
                ba.x0 += 3, ba.y0 += 3, ba.x1 -= 3, ba.y1 -= 3;
                bb.x0 += 3, bb.y0 += 3, bb.x1 -= 3, bb.y1 -= 3;
            }
            TEST_ASSERT_TRUE_MESSAGE(mb_covered(&ba, rects, n), what);
            TEST_ASSERT_TRUE_MESSAGE(mb_covered(&bb, rects, n), what);
        }
}

/* the deck is the tile repeated under any clip: a frame in 7 x 13 px clips (and 13 x 7) is the frame drawn whole; the
 * tread repeats every 44 px each way (and not every 22: its plates' tilt flips there, the mockup's checker), and every
 * uncovered pixel is the deck's (the tile with the tinted grime) */
static void test_rd_mb_deck_tiles(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t ins[7];
    ins[0] = mb_at(10, 42, 5, 0);
    ins[1] = ins[0], ins[1].clk.set = false;
    ins[2] = ins[0], ins[2].clk.stale = true, ins[2].h24 = false;
    ins[3] = ins[0], ins[3].wake_ms = 600;
    ins[4] = ins[0], ins[4].pulsing = true, ins[4].pulse_ms = 300, ins[4].mono_ms = 777;
    ins[5] = ins[0], ins[5].thrum = 0.8f, ins[5].beep = 1.f, ins[5].click = 1.f;
    ins[6] = ins[0], ins[6].glance = true, ins[6].odx = 3, ins[6].ody = -2;
    for (unsigned i = 0; i < 7; i++) {
        frame(r, &ins[i], s_a);
        in_clips(r, &ins[i], 13, 7, s_b);
        assert_same(s_a, s_b, "mopbot in 7 x 13 clips");
        in_clips(r, &ins[i], 7, 13, s_b);
        assert_same(s_a, s_b, "mopbot in 13 x 7 clips");
    }
    long flips = 0;
    for (int y = 0; y + 44 < W; y++)
        for (int x = 0; x + 44 < W; x++) {
            const uint16_t t = rd_mb_tread_px(r->ctx, x, y);
            TEST_ASSERT_EQUAL_HEX16(t, rd_mb_tread_px(r->ctx, x + 44, y));
            TEST_ASSERT_EQUAL_HEX16(t, rd_mb_tread_px(r->ctx, x, y + 44));
            TEST_ASSERT_EQUAL_HEX16(t, rd_mb_tread_px(r->ctx, x + 44, y + 44));
            if (x + 22 < W) flips += t != rd_mb_tread_px(r->ctx, x + 22, y + 44 < W ? y + 44 : y);
        }
    printf("mopbot's tread: %ld px differ a (22, 44) step away (the tilt's checker)\n", flips);
    TEST_ASSERT_TRUE(flips > 1000);
    /* uncovered: inside the hazard ring, off the time, the date, the marker and the prints, the frame is the deck */
    frame(r, &ins[0], s_a);
    omni_rect_t pb;
    rd_mb_prints_box(42, &pb);
    long seen = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
            if (dx * dx + dy * dy > 185.f * 185.f) continue;
            if (inside(&RD_MB_TIME, x, y) || inside(&RD_MB_DATE, x, y) || inside(&RD_MB_STALE, x, y) || inside(&pb, x, y))
                continue;
            TEST_ASSERT_EQUAL_HEX16(rd_mb_deck_px(r->ctx, x, y), s_a[y * W + x]);
            seen++;
        }
    TEST_ASSERT_TRUE(seen > 40000);
}

/* within a minute the rest's dirty rects lie inside the robot's box, the sheen's tail and the ring's rim bands */
static void test_rd_mb_rest_dirty(void)
{
    const row_t *r = row_of("mopbot");
    omni_rect_t bands[4];
    rd_mb_ring_bands(bands);
    long area = 0, frames = 0;
    for (int run = 0; run < 2; run++) {
        fk_face_in_t a = mb_at(run ? 3 : 10, run ? 47 : 9, 0, 0);
        a.mono_ms = run ? 4000000000u : 1000u;
        for (int t = 0; t < 59960; t += 40) {
            const fk_face_in_t p = ran(shifted(a, t), t), q = ran(shifted(a, t + 40), t + 40);
            omni_rect_t ok[7], rects[8];
            rd_mb_robot_box(&p, &ok[0]);
            rd_mb_robot_box(&q, &ok[1]);
            rd_mb_sheen_box(&p, &ok[2]);
            for (int k = 0; k < 4; k++) ok[3 + k] = bands[k];
            omni_rect_t sq;
            rd_mb_sheen_box(&q, &sq);
            ok[2] = (omni_rect_t){ok[2].x0 < sq.x0 ? ok[2].x0 : sq.x0, ok[2].y0 < sq.y0 ? ok[2].y0 : sq.y0,
                                  ok[2].x1 > sq.x1 ? ok[2].x1 : sq.x1, ok[2].y1 > sq.y1 ? ok[2].y1 : sq.y1};
            const unsigned n = r->ops->dirty(r->ctx, &p, &q, rects);
            for (unsigned i = 0; i < n; i++) {
                bool in = false;
                for (int k = 0; k < 7 && !in; k++) in = rect_in(&rects[i], &ok[k]);
                if (!in) {
                    char m[160];
                    snprintf(m, sizeof m, "mopbot rest +%d ms: rect (%d, %d)-(%d, %d) past the robot, the sheen and the ring",
                             t, rects[i].x0, rects[i].y0, rects[i].x1, rects[i].y1);
                    TEST_FAIL_MESSAGE(m);
                }
                area += (long)(rects[i].x1 - rects[i].x0) * (rects[i].y1 - rects[i].y0);
            }
            frames++;
            if (t % 1000 == 960 || t % 7000 == 0) dirty_pair(r, &p, &q, "mopbot rest", NULL);
        }
    }
    printf("mopbot rest: %.0f px a frame on average (the screen %d)\n", (double)area / (double)frames, W * W);
}

/* unset: the robot parked by the sign (its eye dark), Z Z beside it, the track all grime, --:--; nothing moves but the
 * ring's crawl (mono_ms); a tap hops it in its box */
static void test_rd_mb_unset(void)
{
    const row_t *r = row_of("mopbot");
    rd_still_frame(r->ops, r->ctx, 7, s_a);
    rd_still_t st;
    rd_still(7, &st);
    float x, y;
    rd_mb_robot_xy(&st.in.clk, &x, &y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 233.f + 207.f * sinf(0.11f), x);
    /* the eye: robot frame (11, 0) x 1.25, turned .11 rad, in #2A1410 (off: no glow) */
    const int ex = (int)(x + 13.75f * cosf(0.11f)), ey = (int)(y + 13.75f * sinf(0.11f));
    TEST_ASSERT_EQUAL_HEX16(RD_RGB(0x2A, 0x14, 0x10), s_a[ey * W + ex]);
    /* Z Z beside it (Chakra 13 glyphs right of and below the robot) */
    s_ngl = 0;
    fk_test_glyph_hook = record;
    frame(r, &st.in, s_b);
    fk_test_glyph_hook = NULL;
    int zz = 0;
    for (unsigned i = 0; i < s_ngl && i < MAX_GLYPHS; i++)
        if (s_gl[i].x0 >= (int)x + 12 && s_gl[i].x1 <= (int)x + 50 && s_gl[i].y0 >= (int)y + 15 && s_gl[i].y1 <= (int)y + 36)
            zz++;
    TEST_ASSERT_EQUAL_INT(2, zz);
    /* the track all grime: no arc, no prints (the deck itself on the track's middle, away from the robot and sign) */
    for (float th = 1.f; th < 6.f; th += 0.25f) {
        const int px = (int)(233.f + 207.f * sinf(th)), py = (int)(233.f - 207.f * cosf(th));
        TEST_ASSERT_EQUAL_HEX16(rd_mb_deck_px(r->ctx, px, py), s_a[py * W + px]);
    }
    /* --:-- (the stencil's painted body over the deck) and AWAITING TIME SIGNAL in paintDim: no hazard paint */
    long dim = 0, haz = 0;
    for (int y = RD_MB_TIME.y0; y < RD_MB_DATE.y1; y++)
        for (int x = RD_MB_TIME.x0; x < RD_MB_TIME.x1; x++) {
            int R, G, B;
            px8(s_a[y * W + x], &R, &G, &B);
            dim += R > 110 && R < 150 && G > 110 && G < 145 && B > 95 && B < 125;
            haz += R > 180 && G > 130 && B < 80;
        }
    printf("mopbot unset: %ld paintDim px in the time and the line, %ld hazard\n", dim, haz);
    TEST_ASSERT_TRUE(dim > 1000);
    TEST_ASSERT_EQUAL_INT32(0, haz);
    /* still: only the ring crawls (by mono_ms); the chime and the free-running channels move nothing else */
    fk_face_in_t u = st.in, v = st.in;
    v.mono_ms = 7777, v.thrum = 1.f, v.beep = 1.f;
    frame(r, &u, s_a);
    frame(r, &v, s_b);
    omni_rect_t bands[4];
    rd_mb_ring_bands(bands);
    for (int i = 0; i < W * W; i++)
        if (s_a[i] != s_b[i]) TEST_ASSERT_TRUE_MESSAGE(in_any(bands, 4, i % W, i / W), "unset: a pixel moves off the ring");
    v = u, v.pulsing = true, v.pulse_ms = 300;   /* a tap: it hops where it is parked */
    frame(r, &v, s_b);
    omni_rect_t pk;
    rd_mb_robot_box(&u, &pk);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &pk, "unset's hop") > 300);
}

/* the wake: the robot races from the sign (parked) to the minute, the arc growing; settled from 1 200 ms; under reduced
 * motion and unset no race */
static void test_rd_mb_wake(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t st = mb_at(10, 42, 5, 0), in = st;
    frame(r, &st, s_c);
    in.wake_ms = 0;
    frame(r, &in, s_a);
    fk_face_in_t park = st;
    park.clk.set = false;
    omni_rect_t pb = mb_rest_box(&park), sb = mb_rest_box(&st);
    TEST_ASSERT_TRUE(differ_in(s_c, s_a, &pb) > 1000);   /* the robot by the sign */
    TEST_ASSERT_TRUE(differ_in(s_c, s_a, &sb) > 1000);   /* not yet at the minute */
    in.wake_ms = WAKE_PROBE_MS;
    frame(r, &in, s_b);
    TEST_ASSERT_TRUE(frames_differ(s_a, s_b) > 1000 && frames_differ(s_c, s_b) > 1000);
    in.wake_ms = 1200;   /* the wake over */
    frame(r, &in, s_a);
    assert_same(s_c, s_a, "mopbot at 1 200 ms is settled");
    /* every frame of the race, 40 ms apart, and its end: nothing left behind */
    for (uint32_t t = 0; t <= 1240; t += 40) {
        fk_face_in_t a = st, b;
        a.wake_ms = t;
        b = ran(a, 40);
        if (b.wake_ms >= 1200) b.wake_ms = UINT32_MAX;
        dirty_pair(r, &a, &b, "mopbot's race", NULL);
    }
    fk_face_in_t a = st, b = st;
    a.wake_ms = 0;
    dirty_pair(r, &a, &b, "mopbot's whole race", NULL);
    in = st, in.wake_ms = 300, in.reduced = true;
    frame(r, &in, s_a);
    st.reduced = true;
    frame(r, &st, s_b);
    assert_same(s_b, s_a, "a wake under reduced motion");
    in = park, in.wake_ms = 300;
    frame(r, &in, s_a);
    frame(r, &park, s_b);
    assert_same(s_b, s_a, "an unset wake: parked");
}

/* R179: no reduced-motion fade (the runtime gives no own wake under reduced motion), but the ambient motion is stilled:
 * within a second nothing moves whatever mono_ms, the chime or a tap's spin do (a tap only flashes the eye); over the
 * minute only the robot creeps (a step a second) and the ring steps once, at the minute */
static void test_rd_mb_reduced(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t a = mb_at(10, 9, 30, 0);
    a.reduced = true, a.mono_ms = 1000;
    frame(r, &a, s_c);
    for (int t = 0; t < 1000; t += 40) {
        fk_face_in_t b = ran(shifted(a, t), t * 37);
        b.thrum = 0.7f, b.beep = 1.f, b.click = 1.f;
        b.fresh = true, b.fresh_ms = (uint16_t)t;
        frame(r, &b, s_a);
        assert_same(s_c, s_a, "mopbot reduced within a second");
    }
    /* a tap: the eye flashes amber (no spin, no hop): the frame differs only at the eye */
    fk_face_in_t p = a;
    p.pulsing = true, p.pulse_ms = 300;
    frame(r, &p, s_a);
    float x, y;
    rd_mb_robot_xy(&a.clk, &x, &y);
    const float th = 9.5f / 60.f * 6.2831853f;
    const float ex = x + 13.75f * cosf(th), ey = y + 13.75f * sinf(th);
    const omni_rect_t eye = {(int16_t)(ex - 12.f), (int16_t)(ey - 12.f), (int16_t)(ex + 13.f), (int16_t)(ey + 13.f)};
    TEST_ASSERT_TRUE(differ_inside(s_c, s_a, &eye, "mopbot reduced: the tap's eye flash") > 20);
    uint16_t v = 0;
    int cx, cy;
    mb_robot_at(&a, &cx, &cy);
    TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap_var(r->ctx, &a, (int16_t)cx, (int16_t)cy, &v));
    /* the minute: the ring holds until it turns (R171), the robot creeping inside its box and the sheen's */
    fk_face_in_t m0 = mb_at(10, 9, 0, 500), m1 = mb_at(10, 9, 59, 500);
    m0.reduced = m1.reduced = true;
    frame(r, &m0, s_a);
    frame(r, &m1, s_b);
    omni_rect_t ok[4];
    rd_mb_robot_box(&m0, &ok[0]);
    rd_mb_robot_box(&m1, &ok[1]);
    rd_mb_sheen_box(&m0, &ok[2]);
    rd_mb_sheen_box(&m1, &ok[3]);
    for (int i = 0; i < W * W; i++)
        if (s_a[i] != s_b[i])
            TEST_ASSERT_TRUE_MESSAGE(in_any(ok, 4, i % W, i / W), "mopbot reduced: a pixel moves off the robot and the sheen");
    fk_face_in_t m2 = mb_at(10, 10, 0, 500);
    m2.reduced = true;
    frame(r, &m2, s_c);
    omni_rect_t bands[4];
    rd_mb_ring_bands(bands);
    long ring = 0;
    for (int i = 0; i < W * W; i++) {
        const float dx = (float)(i % W) + 0.5f - 233.f, dy = (float)(i / W) + 0.5f - 233.f, d2 = dx * dx + dy * dy;
        ring += s_b[i] != s_c[i] && d2 > 186.f * 186.f && d2 < 194.f * 194.f;
    }
    TEST_ASSERT_TRUE_MESSAGE(ring > 50, "mopbot reduced: the ring steps at the minute");
}

/* the ring crawls 1 px a second from the clock (a step at each second, the rim bands named); nothing within a second */
static void test_rd_mb_ring_crawl(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t a = mb_at(10, 9, 30, 0), b = mb_at(10, 9, 30, 960), c = mb_at(10, 9, 31, 0);
    a.mono_ms = b.mono_ms = c.mono_ms = 0;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    frame(r, &c, s_c);
    long same = 0, moved = 0;
    for (int i = 0; i < W * W; i++) {
        const float dx = (float)(i % W) + 0.5f - 233.f, dy = (float)(i / W) + 0.5f - 233.f, d2 = dx * dx + dy * dy;
        if (d2 < 186.f * 186.f || d2 > 194.f * 194.f) continue;
        omni_rect_t rb;
        rd_mb_robot_box(&a, &rb);
        if (inside(&rb, i % W, i / W)) continue;
        same += s_a[i] != s_b[i];
        moved += s_a[i] != s_c[i];
    }
    TEST_ASSERT_EQUAL_INT32(0, same);
    TEST_ASSERT_TRUE(moved > 100);
    omni_rect_t rects[8], bands[4];
    const unsigned n = dirty_pair(r, &b, &c, "mopbot's ring at a second", rects);
    rd_mb_ring_bands(bands);
    for (int k = 0; k < 4; k++) {
        bool listed = false;
        for (unsigned i = 0; i < n; i++) listed = listed || rect_in(&bands[k], &rects[i]);
        TEST_ASSERT_TRUE_MESSAGE(listed, "a rim band missing at the ring's step");
    }
}

/* stale: the marker above the time, nothing else */
static void test_rd_mb_stale(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t a = mb_at(10, 9, 30, 500), b = a;
    b.clk.stale = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_MB_STALE, "mopbot stale") > 100);
    rd_still_frame(r->ops, r->ctx, 0, s_a);
    rd_still_frame(r->ops, r->ctx, 8, s_c);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_c, &RD_MB_STALE, "mopbot's stale still") > 100);
}

/* the chime: the eye swells with the thrum, the antenna's tip blinks with the beep (both in the robot's box), the sign
 * tips on the click (RD_MB_SIGN); nothing under reduced motion or unset */
static void test_rd_mb_chime(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t a = mb_at(10, 0, 3, 0), b = a;   /* the hour: the robot by the sign */
    a.mono_ms = b.mono_ms = 0;
    omni_rect_t box;
    rd_mb_robot_box(&a, &box);
    b.thrum = 1.f;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &box, "the eye's swell") > 1000);
    b.thrum = 0.f, b.beep = 1.f;
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &box, "the antenna's blink") > 10);
    b.beep = 0.f, b.click = 1.f;
    frame(r, &b, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_MB_SIGN, "the sign's bump") > 50);
    b.thrum = b.beep = 1.f;
    a.reduced = b.reduced = true;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    assert_same(s_a, s_b, "the chime under reduced motion");
    a.reduced = b.reduced = false, a.clk.set = b.clk.set = false;
    frame(r, &a, s_a);
    frame(r, &b, s_b);
    assert_same(s_a, s_b, "the chime unset (the robot asleep)");
}

/* the glance: every channel <= 110, the outlined time and the robot's dot in rd.g.hazard (by the minute), the orbit,
 * the stale marker, the unset dashes, 12 h without the leading zero */
static void test_rd_mb_glance(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t in = mb_at(10, 42, 30, 500);
    in.glance = true;
    frame(r, &in, s_a);
    long haz = 0;
    for (int i = 0; i < W * W; i++) {
        int R, G, B;
        px8(s_a[i], &R, &G, &B);
        TEST_ASSERT_TRUE(R <= 110 && G <= 110 && B <= 110);
        haz += s_a[i] == RD_G_HAZARD;
    }
    printf("mopbot glance: %ld px at rd.g.hazard\n", haz);
    TEST_ASSERT_TRUE(haz > 300);
    const float th = 42.f / 60.f * 6.2831853f;
    const int dx = (int)(233.f + 207.f * sinf(th)), dy = (int)(233.f - 207.f * cosf(th));
    TEST_ASSERT_EQUAL_HEX16(RD_G_HAZARD, s_a[dy * W + dx]);   /* the dot, at the minute (not its seconds) */
    fk_face_in_t q = in;
    q.clk.s = 5;
    frame(r, &q, s_b);
    assert_same(s_a, s_b, "the glance within a minute");
    in.odx = 4, in.ody = -4;
    frame(r, &in, s_b);
    for (int y = 4; y < W - 4; y++)
        for (int x = 4; x < W - 4; x++) TEST_ASSERT_EQUAL_HEX16(s_a[y * W + x], s_b[(y - 4) * W + x + 4]);
    in.odx = in.ody = 0, in.clk.stale = true;
    frame(r, &in, s_b);
    TEST_ASSERT_TRUE(differ_inside(s_a, s_b, &RD_MB_STALE, "the glance's stale marker") > 50);
    in.clk.set = false, in.clk.stale = false;
    frame(r, &in, s_b);
    long dash = 0;
    for (int i = 0; i < W * W; i++) dash += s_b[i] == RD_G_HAZARD;
    TEST_ASSERT_TRUE(dash > 100);
    /* 12 h: 1:05 (no leading zero) differs from 13:05 in 24 h, and no AM/PM is drawn */
    fk_face_in_t h12 = mb_at(13, 5, 0, 0), h24 = h12;
    h12.glance = h24.glance = true, h12.h24 = false;
    s_ngl = 0;
    fk_test_glyph_hook = record;
    frame(r, &h12, s_a);
    fk_test_glyph_hook = NULL;
    TEST_ASSERT_EQUAL_UINT(4, s_ngl);   /* 1 : 0 5 */
    frame(r, &h24, s_b);
    TEST_ASSERT_TRUE(frames_differ(s_a, s_b) > 100);
}

/* the glance's worst minutes, pinned (the wave-2 guide): 24 h and 12 h, set and stale, at the widest date */
static void test_rd_mb_glance_worst(void)
{
    const row_t *r = row_of("mopbot");
    static const struct { int h, m; bool h24; float pct[2]; } WORST[] = {
        {MB_GW_H, MB_GW_M, true, {MB_GW, MB_GWS}},
        {MB_GW12_H, MB_GW12_M, false, {MB_GW12, MB_GW12S}},
    };
    for (unsigned i = 0; i < 2; i++)
        for (int stale = 0; stale < 2; stale++) {
            fk_face_in_t in = in_at(WORST[i].h, WORST[i].m, 30, 500);
            in.glance = true, in.h24 = WORST[i].h24;
            in.clk.mday = 28, in.clk.mon = 8, in.clk.wday = 3;   /* WED 28 SEP: the widest date */
            in.clk.stale = stale != 0;
            char what[48];
            snprintf(what, sizeof what, "at %02d:%02d%s (pinned worst)", WORST[i].h, WORST[i].m,
                     stale ? ", stale" : "");
            glance_orbits(r, in, what);
            const float pct = glance_pct(r, &in);
            printf("mopbot glance %s %s: %.2f %% lit\n", what, WORST[i].h24 ? "24 h" : "12 h", (double)pct);
            TEST_ASSERT_TRUE_MESSAGE(pct >= WORST[i].pct[stale] - 0.1f && pct <= WORST[i].pct[stale] + 0.1f, what);
        }
}

/* R185 keeps R169's area mean for a large lit area: Mop Bot's only one is the clean wet arc (at 10:59:30 nearly the whole
 * track), a dim #20262B whose blue rounds just over the static-bright rule's 40; the mean largest channel of its lit
 * pixels (> 8) and of the whole frame's stays far under 115; the peak (the hazard paint) is reported, not gated */
static void test_rd_mb_dimming(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t in = mb_at(10, 59, 30, 0);
    frame(r, &in, s_a);
    long lit = 0, alit = 0;
    double sum = 0, asum = 0;
    int peak = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f, d2 = dx * dx + dy * dy;
            if (d2 > 233.f * 233.f) continue;
            int R, G, B;
            px8(s_a[y * W + x], &R, &G, &B);
            const int m = R > G ? (R > B ? R : B) : (G > B ? G : B);
            peak = m > peak ? m : peak;
            if (m <= 8) continue;
            lit++, sum += m;
            if (d2 >= 193.f * 193.f && d2 <= 221.f * 221.f && dx < 0) alit++, asum += m;   /* the arc's west half */
        }
    printf("mopbot at 10:59:30: %ld lit px, mean largest channel %.1f; the arc's west half %ld px %.1f (ceiling 115);"
           " peak %d\n", lit, sum / (double)lit, alit, asum / (double)alit, peak);
    TEST_ASSERT_TRUE(sum / (double)lit <= 115.0);
    TEST_ASSERT_TRUE(asum / (double)alit <= 115.0);
}

/* a tap: the hop and the spin stay inside the robot's box (its largest), the eye amber; over by 800 ms */
static void test_rd_mb_tap_hop(void)
{
    const row_t *r = row_of("mopbot");
    fk_face_in_t a = mb_at(10, 23, 40, 0);
    a.mono_ms = 500;
    frame(r, &a, s_c);
    omni_rect_t box;
    rd_mb_robot_box(&a, &box);
    for (int t = 0; t < 900; t += 20) {
        fk_face_in_t p = a;
        p.pulsing = true, p.pulse_ms = (uint16_t)t;
        frame(r, &p, s_a);
        char what[48];
        snprintf(what, sizeof what, "mopbot's hop at %d ms", t);
        const long d = differ_inside(s_c, s_a, &box, what);
        if (t == 200) TEST_ASSERT_TRUE(d > 1500);   /* mid-hop, a quarter turned */
    }
}

/* RW11 (wave 2): every id but the shared Digital is either a built face (an RD_FACES entry with that id's key, the keys
 * the descriptor registers: RD_FACE_TABLE) or pending (its RD_PENDING bit: the shared Digital face under its key, a
 * placeholder), never both and never neither. Each face task clears its bit; Task 10 leaves RD_PENDING 0. */
static void test_rd_no_placeholders(void)
{
    /* wave 2 Task 10: wave 2's seven faces built, so nothing is pending and every id but Digital is an rd_core face */
    TEST_ASSERT_EQUAL_HEX32(0, RD_PENDING);
    TEST_ASSERT_EQUAL_UINT(RD_F__COUNT - 1u, RD_FACE_COUNT);
    TEST_ASSERT_EQUAL_UINT(RD_F__COUNT - 1u - (unsigned)__builtin_popcount(RD_PENDING), RD_FACE_COUNT);
    TEST_ASSERT_EQUAL_HEX32(0, RD_PENDING & ~((1u << RD_F__COUNT) - 1u));   /* only this build's ids */
    TEST_ASSERT_EQUAL_HEX32(0, RD_PENDING & (1u << RD_F_DIGITAL));         /* Digital is never pending */
    TEST_ASSERT_EQUAL_UINT((unsigned)__builtin_popcount(RD_PENDING), RD_POPCOUNT16(RD_PENDING));
    for (unsigned id = 0; id < RD_F__COUNT; id++) {
        TEST_ASSERT_NOT_NULL(RD_FACE_KEYS[id]);
        const fk_face_ops_t *ops = rd_face_by_key(RD_FACE_KEYS[id]);
        const bool pending = (RD_PENDING >> id) & 1u;
        char m[64];
        snprintf(m, sizeof m, "id %u (%s)", id, RD_FACE_KEYS[id]);
        if (id == RD_F_DIGITAL || pending) {
            TEST_ASSERT_NULL_MESSAGE(ops, m);   /* the shared face, not an rd_core one: built and pending never both */
            continue;
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(ops, m);   /* not pending: built */
        TEST_ASSERT_EQUAL_STRING_MESSAGE(RD_FACE_KEYS[id], ops->key, m);
    }
    for (unsigned i = 0; i < RD_FACE_COUNT; i++) {   /* and every built face has an id, not a pending one */
        unsigned hits = 0;
        for (unsigned id = 0; id < RD_F__COUNT; id++)
            if (strcmp(RD_FACE_KEYS[id], RD_FACES[i]->key) == 0) {
                hits++;
                TEST_ASSERT_FALSE_MESSAGE((RD_PENDING >> id) & 1u, RD_FACES[i]->key);
            }
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1, hits, RD_FACES[i]->key);
    }
    static const char *const KEYS[] = {"stasis", "terminal", "deck",    "hull",  "bunk",    "curry", "digital",
                                       "bay",    "drift",    "console", "scope", "vending", "bowl",  "mopbot"};
    TEST_ASSERT_EQUAL_UINT(sizeof KEYS / sizeof KEYS[0], RD_F__COUNT);
    for (unsigned id = 0; id < RD_F__COUNT; id++) TEST_ASSERT_EQUAL_STRING(KEYS[id], RD_FACE_KEYS[id]);
}

/* the Task 14 review: Bunk Clock's four leaf slots. At the hour's flip (both cards) and mid-wake, a frame drawn once
 * more composes no leaf, whole or in bands */
static void test_rd_bk_leaf_cache(void)
{
    /* each case on a freshly bound context, so its first frame composes its leaves (the Task 15 review): the hour's flip
     * four (10 and 11, 59 and 00), the wake at 350 ms three (-- shared, the hours, the minutes), at 600 ms two (the hours
     * have landed: the minutes' -- and 42) */
    static const struct { int h, m; uint32_t wake; unsigned first; } AT[] = {
        {11, 0, UINT32_MAX, 4}, {10, 42, 350, 3}, {10, 42, 600, 2}};
    for (unsigned k = 0; k < sizeof AT / sizeof AT[0]; k++) {
        row_t fresh = {&RD_FACE_BUNK, calloc(1, RD_FACE_BUNK.ctx_bytes)};
        TEST_ASSERT_NOT_NULL(fresh.ctx);
        TEST_ASSERT_TRUE(RD_FACE_BUNK.bind(fresh.ctx, rd_assets_host_get));
        const row_t *r = &fresh;
        fk_face_in_t in = bk_at(AT[k].h, AT[k].m, 0, 300);
        in.wake_ms = AT[k].wake;
        TEST_ASSERT_EQUAL_UINT32(0, rd_bk_leaf_composes(r->ctx));
        frame(r, &in, s_a);
        const uint32_t n0 = rd_bk_leaf_composes(r->ctx);
        TEST_ASSERT_TRUE_MESSAGE(n0 >= AT[k].first, "the first frame composed fewer leaves than it shows");
        frame(r, &in, s_b);
        in_clips(r, &in, 8, W, s_b);
        in_clips(r, &in, 6, 37, s_b);
        assert_same(s_a, s_b, "bunk's frame from its held leaves");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(n0, rd_bk_leaf_composes(r->ctx), "a held frame composed a leaf again");
        /* each card's own clip (the view's dirty rects): none either */
        draw_fn fn = r->ops->draw;
        fn(r->ctx, &in, RD_BK_FLIP_H, s_b + RD_BK_FLIP_H.y0 * W + RD_BK_FLIP_H.x0, W);
        fn(r->ctx, &in, RD_BK_FLIP_M, s_b + RD_BK_FLIP_M.y0 * W + RD_BK_FLIP_M.x0, W);
        TEST_ASSERT_EQUAL_UINT32(n0, rd_bk_leaf_composes(r->ctx));
        free(fresh.ctx);
    }
}

/* the rows (one line each per face) */
#define RD_FACE_ROW(key)                                                                \
    static void test_rd_face_band_exact_##key(void) { band_exact(#key); }             \
    static void test_rd_face_dirty_jumps_##key(void) { dirty_jumps(#key); }           \
    static void test_rd_face_dirty_states_##key(void) { dirty_states(#key); }         \
    static void test_rd_face_glance_quiet_##key(void) { glance_quiet(#key); }         \
    static void test_rd_face_reduced_still_##key(void) { reduced_still(#key); }       \
    static void test_rd_face_glance_day_##key(void) { glance_day(#key); }             \
    static void test_rd_face_void_##key(void) { void_floor(#key); }                   \
    static void test_rd_face_text_radius_##key(void) { text_radius(#key); }           \
    static void test_rd_face_tap_edges_##key(void) { tap_edges(#key); }               \
    static void test_rd_face_chime_regions_##key(void) { chime_regions(#key); }       \
    static void test_rd_face_atlases_##key(void) { atlases(#key); }                   \
    static void test_rd_face_tap_pair_##key(void) { tap_pair(#key); }                 \
    static void test_rd_face_dirty_tapped_##key(void) { dirty_tapped(#key); }
/* RD_FACES_PART (0 or undefined: all): the slow rows split over three binaries, so ctest runs them in parallel. 1: the
   dirty_jumps / dirty_states / dirty_tapped rows; 2: the glance_day rows; 3: everything else. A part leaves the other
   parts' row functions (and their helpers) unused. */
#define RD_RUN_P1(key)                                \
    RUN_TEST(test_rd_face_dirty_jumps_##key);         \
    RUN_TEST(test_rd_face_dirty_states_##key);        \
    RUN_TEST(test_rd_face_dirty_tapped_##key)
#define RD_RUN_P2(key) RUN_TEST(test_rd_face_glance_day_##key)
#define RD_RUN_P3(key)                                \
    RUN_TEST(test_rd_face_band_exact_##key);          \
    RUN_TEST(test_rd_face_glance_quiet_##key);        \
    RUN_TEST(test_rd_face_reduced_still_##key);       \
    RUN_TEST(test_rd_face_void_##key);                \
    RUN_TEST(test_rd_face_text_radius_##key);         \
    RUN_TEST(test_rd_face_tap_edges_##key);           \
    RUN_TEST(test_rd_face_chime_regions_##key);       \
    RUN_TEST(test_rd_face_atlases_##key);             \
    RUN_TEST(test_rd_face_tap_pair_##key)
#if RD_FACES_PART == 1
#define RD_RUN_ROW(key) RD_RUN_P1(key)
#elif RD_FACES_PART == 2
#define RD_RUN_ROW(key) RD_RUN_P2(key)
#elif RD_FACES_PART == 3
#define RD_RUN_ROW(key) RD_RUN_P3(key)
#else
#define RD_RUN_ROW(key) RD_RUN_P3(key); RD_RUN_P1(key); RD_RUN_P2(key)
#endif
/* every other RUN_TEST in main runs in part 3 (or all) only */
#if RD_FACES_PART == 0 || RD_FACES_PART == 3
#define RD_RUN_REST(t) RUN_TEST(t)
#else
#define RD_RUN_REST(t) ((void)0)
#endif

RD_FACE_ROW(stasis)
RD_FACE_ROW(terminal)
RD_FACE_ROW(deck)
RD_FACE_ROW(hull)
RD_FACE_ROW(bunk)
RD_FACE_ROW(curry)
RD_FACE_ROW(bay)
RD_FACE_ROW(drift)
RD_FACE_ROW(console)
RD_FACE_ROW(scope)
RD_FACE_ROW(vending)
RD_FACE_ROW(bowl)
RD_FACE_ROW(mopbot)
/* the static-bright row (wave 2 Task 3): each face with an RD_STATIC_BRIGHT line */
#define RD_STATIC_BRIGHT_ROW(key) \
    static void test_rd_face_static_bright_##key(void) { static_bright(#key); }
RD_STATIC_BRIGHT_ROW(bay)
RD_STATIC_BRIGHT_ROW(drift)
RD_STATIC_BRIGHT_ROW(console)
RD_STATIC_BRIGHT_ROW(scope)
RD_STATIC_BRIGHT_ROW(vending)
RD_STATIC_BRIGHT_ROW(bowl)
RD_STATIC_BRIGHT_ROW(mopbot)

/* every registered face has a row: the table above lists each, so a face task cannot add a face without its row */
static void test_rd_faces_all_rowed(void)
{
    static const char *const ROWED[] = {"stasis", "terminal", "deck", "hull", "bunk", "curry", "bay", "drift",
                                        "console", "scope", "vending", "bowl", "mopbot"};
    TEST_ASSERT_EQUAL_UINT(sizeof ROWED / sizeof ROWED[0], RD_FACE_COUNT);
    for (unsigned i = 0; i < RD_FACE_COUNT; i++) TEST_ASSERT_EQUAL_STRING(ROWED[i], RD_FACES[i]->key);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_STASIS, rd_face_by_key("stasis"));
    TEST_ASSERT_EQUAL_UINT16(40, RD_FACE_STASIS.cadence_ms);
    TEST_ASSERT_TRUE(RD_FACE_STASIS.reveal);   /* Stasis Release (Task 10; test_rd_release) */
    TEST_ASSERT_EQUAL_PTR(&RD_REVEAL_STASIS, RD_FACE_STASIS.reveal_ops);
    TEST_ASSERT_TRUE(RD_FACE_STASIS.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_TERMINAL, rd_face_by_key("terminal"));
    TEST_ASSERT_TRUE(RD_FACE_TERMINAL.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_DECK, rd_face_by_key("deck"));
    TEST_ASSERT_TRUE(RD_FACE_DECK.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_HULL, rd_face_by_key("hull"));
    TEST_ASSERT_TRUE(RD_FACE_HULL.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_BUNK, rd_face_by_key("bunk"));
    TEST_ASSERT_TRUE(RD_FACE_BUNK.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_CURRY, rd_face_by_key("curry"));
    TEST_ASSERT_TRUE(RD_FACE_CURRY.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_BAY, rd_face_by_key("bay"));
    TEST_ASSERT_TRUE(RD_FACE_BAY.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_NOT_NULL_MESSAGE(static_bright_of("bay"), "Bay Mouth's static-bright line (lesson 15)");
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_DRIFT, rd_face_by_key("drift"));
    TEST_ASSERT_TRUE(RD_FACE_DRIFT.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_NOT_NULL_MESSAGE(static_bright_of("drift"), "Drift Plate's static-bright line (lesson 15)");
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_CONSOLE, rd_face_by_key("console"));
    TEST_ASSERT_TRUE(RD_FACE_CONSOLE.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_NOT_NULL_MESSAGE(static_bright_of("console"), "Drive Console's static-bright line (lesson 15)");
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_SCOPE, rd_face_by_key("scope"));
    TEST_ASSERT_TRUE(RD_FACE_SCOPE.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_NOT_NULL_MESSAGE(static_bright_of("scope"), "Nav Scope's static-bright line (lesson 15)");
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_VENDING, rd_face_by_key("vending"));
    TEST_ASSERT_TRUE(RD_FACE_VENDING.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_NOT_NULL_MESSAGE(static_bright_of("vending"), "Vending Machine's static-bright line (lesson 15)");
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_BOWL, rd_face_by_key("bowl"));
    TEST_ASSERT_TRUE(RD_FACE_BOWL.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_NOT_NULL_MESSAGE(static_bright_of("bowl"), "the Bowl's static-bright line (lesson 15)");
    TEST_ASSERT_EQUAL_PTR(&RD_FACE_MOPBOT, rd_face_by_key("mopbot"));
    TEST_ASSERT_TRUE(RD_FACE_MOPBOT.ctx_bytes <= 1500000u - 434000u);
    TEST_ASSERT_NOT_NULL_MESSAGE(static_bright_of("mopbot"), "Mop Bot's static-bright line (lesson 15)");
}

int main(void)
{
    UNITY_BEGIN();
    bind_all();
    {   /* wave 2 Task 10: Mop Bot's RD_TAP_VARS boxes, at the probes' clock and parked */
        fk_face_in_t q = in_at(10, 9, 30, 500);
        rd_mb_robot_box(&q, &MB_BOX_0930);
        q.clk.set = false;
        rd_mb_robot_box(&q, &MB_BOX_PARK);
    }
    RD_RUN_REST(test_rd_faces_all_rowed);
    RD_RUN_REST(test_rd_stills_table);
    RD_RUN_REST(test_rd_glow_lut_tables);
    RD_RUN_REST(test_rd_stale_mark);
    RD_RUN_ROW(stasis);
    RD_RUN_REST(test_rd_stasis_slot_confined);
    RD_RUN_REST(test_rd_stasis_slot_box);
    RD_RUN_REST(test_rd_stasis_unset);
    RD_RUN_REST(test_rd_stasis_stale_rect);
    RD_RUN_REST(test_rd_stasis_tap_preview_gate);
    RD_RUN_REST(test_rd_stasis_readout);
    RD_RUN_ROW(terminal);
    RD_RUN_REST(test_rd_tm_face);
    RD_RUN_REST(test_rd_tm_house_ramp);
    RD_RUN_REST(test_rd_tm_pulse_covers_pages);
    RD_RUN_REST(test_rd_tm_wake_typed);
    RD_RUN_REST(test_rd_tm_wake_open);
    RD_RUN_REST(test_rd_tm_pages);
    RD_RUN_REST(test_rd_tm_typing);
    RD_RUN_REST(test_rd_tm_rest_dirty);
    RD_RUN_REST(test_rd_tm_linger);
    RD_RUN_REST(test_rd_tm_glance_no_date);
    RD_RUN_REST(test_rd_tm_stale_rect);
    RD_RUN_REST(test_rd_tm_stale_rows);
    RD_RUN_REST(test_rd_tm_unset);
    RD_RUN_ROW(deck);
    RD_RUN_REST(test_rd_dk_face);
    RD_RUN_REST(test_rd_dk_tables);
    RD_RUN_REST(test_rd_dk_spray_window);
    RD_RUN_REST(test_rd_dk_notice_cycle);
    RD_RUN_REST(test_rd_dk_12h_centre);
    RD_RUN_REST(test_rd_dk_rest_dirty);
    RD_RUN_REST(test_rd_dk_beacon);
    RD_RUN_REST(test_rd_dk_wake);
    RD_RUN_REST(test_rd_dk_states);
    RD_RUN_REST(test_rd_dk_chime);
    RD_RUN_ROW(hull);
    RD_RUN_REST(test_rd_hp_face);
    RD_RUN_REST(test_rd_hp_tick_overshoot);
    RD_RUN_REST(test_rd_hp_rest_dirty);
    RD_RUN_REST(test_rd_hp_hands_per_minute);
    RD_RUN_REST(test_rd_hp_readout_cycle);
    RD_RUN_REST(test_rd_hp_wake);
    RD_RUN_REST(test_rd_hp_states);
    RD_RUN_REST(test_rd_hp_chime);
    RD_RUN_REST(test_rd_hp_glance);
    RD_RUN_ROW(bunk);
    RD_RUN_REST(test_rd_bk_face);
    RD_RUN_REST(test_rd_bk_ctx_bytes);
    RD_RUN_REST(test_rd_bk_count_days);
    RD_RUN_REST(test_rd_bk_hit_boxes);
    RD_RUN_REST(test_rd_bk_unset_bar_inert);
    RD_RUN_REST(test_rd_bk_flip_window);
    RD_RUN_REST(test_rd_bk_wake);
    RD_RUN_REST(test_rd_bk_snooze);
    RD_RUN_REST(test_rd_bk_lamp);
    RD_RUN_REST(test_rd_bk_states);
    RD_RUN_REST(test_rd_bk_chime);
    RD_RUN_REST(test_rd_bk_glance);
    RD_RUN_REST(test_rd_bk_leaf_cache);
    RD_RUN_ROW(curry);
    RD_RUN_REST(test_rd_cu_face);
    RD_RUN_REST(test_rd_cu_ctx_bytes);
    RD_RUN_REST(test_rd_cu_biro_graphite);
    RD_RUN_REST(test_rd_cu_chilli_cycle);
    RD_RUN_REST(test_rd_cu_steam_confined);
    RD_RUN_REST(test_rd_cu_unset_lid);
    RD_RUN_REST(test_rd_cu_lid_clip);
    RD_RUN_REST(test_rd_cu_hands_per_minute);
    RD_RUN_REST(test_rd_cu_shudder);
    RD_RUN_REST(test_rd_cu_steam);
    RD_RUN_REST(test_rd_cu_chime);
    RD_RUN_REST(test_rd_cu_stale);
    RD_RUN_REST(test_rd_cu_glance);
    RD_RUN_REST(test_rd_cu_loop_mirror);
    RD_RUN_REST(test_rd_cu_loop_confined);
    RD_RUN_REST(test_rd_cu_loop_look);
    RD_RUN_REST(test_rd_cu_loop_cost);
    RD_RUN_ROW(bay);
    RD_RUN_REST(test_rd_face_static_bright_bay);
    RD_RUN_REST(test_rd_by_face);
    RD_RUN_REST(test_rd_by_star_shift);
    RD_RUN_REST(test_rd_by_ring_crawl);
    RD_RUN_REST(test_rd_by_reduced_crawl);
    RD_RUN_REST(test_rd_by_pip);
    RD_RUN_REST(test_rd_by_dimming);
    RD_RUN_REST(test_rd_by_unset);
    RD_RUN_REST(test_rd_by_stale_rect);
    RD_RUN_REST(test_rd_by_wake_interior);
    RD_RUN_REST(test_rd_by_glance);
    RD_RUN_REST(test_rd_by_glance_worst);
    RD_RUN_REST(test_rd_by_chime);
    RD_RUN_ROW(drift);
    RD_RUN_REST(test_rd_face_static_bright_drift);
    RD_RUN_REST(test_rd_dp_face);
    RD_RUN_REST(test_rd_dp_drift);
    RD_RUN_REST(test_rd_dp_dimming);
    RD_RUN_REST(test_rd_dp_unset);
    RD_RUN_REST(test_rd_dp_stale_rect);
    RD_RUN_REST(test_rd_dp_wake_interior);
    RD_RUN_REST(test_rd_dp_glance);
    RD_RUN_REST(test_rd_dp_glance_worst);
    RD_RUN_REST(test_rd_dp_chime);
    RD_RUN_ROW(console);
    RD_RUN_REST(test_rd_face_static_bright_console);
    RD_RUN_REST(test_rd_sync_str);
    RD_RUN_REST(test_rd_reserve_str);
    RD_RUN_REST(test_rd_seg7_12h);
    RD_RUN_REST(test_rd_dc_face);
    RD_RUN_REST(test_rd_dc_button);
    RD_RUN_REST(test_rd_dc_strip);
    RD_RUN_REST(test_rd_dc_rest_dirty);
    RD_RUN_REST(test_rd_dc_unset);
    RD_RUN_REST(test_rd_dc_lamps);
    RD_RUN_REST(test_rd_dc_wake);
    RD_RUN_REST(test_rd_dc_chime);
    RD_RUN_REST(test_rd_dc_stale_rect);
    RD_RUN_REST(test_rd_dc_glance);
    RD_RUN_REST(test_rd_dc_glance_worst);
    RD_RUN_ROW(scope);
    RD_RUN_REST(test_rd_face_static_bright_scope);
    RD_RUN_REST(test_rd_sc_face);
    RD_RUN_REST(test_rd_sc_step_modes);
    RD_RUN_REST(test_rd_sc_step_dirty);
    RD_RUN_REST(test_rd_sc_override_cleared);
    RD_RUN_REST(test_rd_sc_afterglow);
    RD_RUN_REST(test_rd_sc_glass_confined);
    RD_RUN_REST(test_rd_sc_corona);
    RD_RUN_REST(test_rd_sc_unset);
    RD_RUN_REST(test_rd_sc_reduced);
    RD_RUN_REST(test_rd_sc_wake);
    RD_RUN_REST(test_rd_sc_pages);
    RD_RUN_REST(test_rd_sc_hands_per_minute);
    RD_RUN_REST(test_rd_sc_chime);
    RD_RUN_REST(test_rd_sc_stale_rect);
    RD_RUN_REST(test_rd_sc_dimming);
    RD_RUN_REST(test_rd_sc_glance);
    RD_RUN_REST(test_rd_sc_glance_worst);
    RD_RUN_ROW(vending);
    RD_RUN_REST(test_rd_face_static_bright_vending);
    RD_RUN_REST(test_rd_vm_face);
    RD_RUN_REST(test_rd_vm_buzz);
    RD_RUN_REST(test_rd_vm_ticker);
    RD_RUN_REST(test_rd_vm_mystery);
    RD_RUN_REST(test_rd_vm_12h);
    RD_RUN_REST(test_rd_vm_wake);
    RD_RUN_REST(test_rd_vm_unset);
    RD_RUN_REST(test_rd_vm_stale_rect);
    RD_RUN_REST(test_rd_vm_chime);
    RD_RUN_REST(test_rd_vm_glance);
    RD_RUN_REST(test_rd_vm_glance_worst);
    RD_RUN_REST(test_rd_vm_dimming);
    RD_RUN_REST(test_rd_vm_still_vend);
    RD_RUN_ROW(bowl);
    RD_RUN_REST(test_rd_face_static_bright_bowl);
    RD_RUN_REST(test_rd_cb_face);
    RD_RUN_REST(test_rd_cb_kibbles);
    RD_RUN_REST(test_rd_cb_kibbles_jump);
    RD_RUN_REST(test_rd_cb_paw_schedule);
    RD_RUN_REST(test_rd_cb_unset);
    RD_RUN_REST(test_rd_cb_wake);
    RD_RUN_REST(test_rd_cb_stale);
    RD_RUN_REST(test_rd_cb_chime);
    RD_RUN_REST(test_rd_cb_glance);
    RD_RUN_REST(test_rd_cb_glance_worst);
    RD_RUN_REST(test_rd_cb_dimming);
    RD_RUN_REST(test_rd_cb_reduced);
    RD_RUN_ROW(mopbot);
    RD_RUN_REST(test_rd_face_static_bright_mopbot);
    RD_RUN_REST(test_rd_face_static_bright_worst_mopbot);
    RD_RUN_REST(test_rd_mb_face);
    RD_RUN_REST(test_rd_mb_hit_moves);
    RD_RUN_REST(test_rd_mb_robot_jump);
    RD_RUN_REST(test_rd_mb_deck_tiles);
    RD_RUN_REST(test_rd_mb_rest_dirty);
    RD_RUN_REST(test_rd_mb_unset);
    RD_RUN_REST(test_rd_mb_wake);
    RD_RUN_REST(test_rd_mb_reduced);
    RD_RUN_REST(test_rd_mb_ring_crawl);
    RD_RUN_REST(test_rd_mb_stale);
    RD_RUN_REST(test_rd_mb_chime);
    RD_RUN_REST(test_rd_mb_glance);
    RD_RUN_REST(test_rd_mb_glance_worst);
    RD_RUN_REST(test_rd_mb_tap_hop);
    RD_RUN_REST(test_rd_mb_dimming);
    RD_RUN_REST(test_rd_no_placeholders);
    RD_RUN_REST(test_verify_crc_current);
    return UNITY_END();
}
