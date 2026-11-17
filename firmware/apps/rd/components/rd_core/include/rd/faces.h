#pragma once
/* The Mining Ship face ids, the default carousel, the lift rule and the registry of built faces (spec section 6). The
 * ids are theme-local and append-only; a face exists in a build only once its task registers it (rd_theme.c) and
 * appends its ops to RD_FACES (faces.c). Until then its RD_PENDING bit is set and rd_theme.c shows the shared Digital
 * face under the id's own key, title and group (a placeholder). Pure: no esp_*, freertos or LVGL; no allocation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/compose.h"
#include "fk/face.h"
#include "omni/geom.h"
#include "rd/prefs.h"
#include "rd/release.h"   /* rd_ramp_stop_t (Ship Terminal's housing ramp) */

enum rd_face { RD_F_STASIS = 0, RD_F_TERMINAL, RD_F_DECK, RD_F_HULL, RD_F_BUNK, RD_F_CURRY, RD_F_DIGITAL,
               RD_F_BAY = 7, RD_F_DRIFT, RD_F_CONSOLE, RD_F_SCOPE, RD_F_VENDING, RD_F_BOWL, RD_F_MOPBOT, RD_F__ALL };
#define RD_F__COUNT 14                     /* wave 2: ids 0-13 */
#define RD_CAROUSEL_MAX 16                 /* OMNI_CAROUSEL_MAX (rd_prefs_svc.c asserts it) */
_Static_assert(RD_F__COUNT <= RD_F__ALL, "a built face has a named id");
_Static_assert(RD_F__ALL <= 16, "offered has a bit for every id");

/* The ids not built yet (RW11's bitmask, wave 2; ruling I2: here, beside RD_F__COUNT, so the host suite reads it, since
 * rd_theme.c is app code the host never compiles). Each such id shows the shared Digital face under its own key, title
 * and group (a placeholder, rd_theme.c). Task 2 sets bits 7-13; each face task clears its own bit as it registers its
 * face; Task 10 leaves 0. */
#define RD_PENDING 0x0000u   /* Task 3: Bay Mouth (id 7) built; Task 5: Drift Plate (id 8); Task 6: Drive Console
                              * (id 9); Task 7: Nav Scope (id 10); Task 8: Vending Machine (id 11); Task 9: the Bowl
                              * (id 12); Task 10: Mop Bot (id 13): wave 2's seven faces built, none pending */
/* a constant-expression popcount of a 16-bit mask (rd_theme.c's _Static_asserts count RD_PENDING with it) */
#define RD_POPCOUNT16(x)                                                                                              \
    ((((x) >> 0) & 1u) + (((x) >> 1) & 1u) + (((x) >> 2) & 1u) + (((x) >> 3) & 1u) + (((x) >> 4) & 1u) +            \
     (((x) >> 5) & 1u) + (((x) >> 6) & 1u) + (((x) >> 7) & 1u) + (((x) >> 8) & 1u) + (((x) >> 9) & 1u) +             \
     (((x) >> 10) & 1u) + (((x) >> 11) & 1u) + (((x) >> 12) & 1u) + (((x) >> 13) & 1u) + (((x) >> 14) & 1u) +        \
     (((x) >> 15) & 1u))
_Static_assert((RD_PENDING & ~((1u << RD_F__COUNT) - 1u)) == 0, "a pending id is an id of this build");
_Static_assert((RD_PENDING & (1u << RD_F_DIGITAL)) == 0, "Digital is the fallback itself, never pending");

/* The family order (spec section 6): every face but the shared Digital, grouped Signature, Bridge, Quarters: a fresh
 * record's carousel, and the offer's list (an existing carousel gets each id it was never offered appended once, after
 * the owner's own order). */
#define RD_DEF_CAROUSEL_LEN 13
_Static_assert(RD_DEF_CAROUSEL_LEN <= RD_CAROUSEL_MAX, "the default carousel fits");
extern const uint8_t RD_DEF_CAROUSEL[RD_DEF_CAROUSEL_LEN];   /* 0, 7, 8, 9, 10, 2, 1, 3, 11, 5, 12, 4, 13 */

/* Every id's key, title and group (spec section 6): rd_theme.c's descriptor table (its watch_face_info_t rows, the
 * thumbnail rd_th_<key>.bin) and RD_FACE_KEYS both expand this one list, so the host's test_rd_no_placeholders checks
 * the keys the watch registers (RW11). X(id, key, title, group). */
#define RD_FACE_TABLE(X)                                    \
    X(RD_F_STASIS, "stasis", "Stasis Clock", "Signature")   \
    X(RD_F_TERMINAL, "terminal", "Ship Terminal", "Bridge") \
    X(RD_F_DECK, "deck", "Deck Sign", "Bridge")             \
    X(RD_F_HULL, "hull", "Hull Plate", "Bridge")            \
    X(RD_F_BUNK, "bunk", "Bunk Clock", "Quarters")          \
    X(RD_F_CURRY, "curry", "Curry Clock", "Quarters")       \
    X(RD_F_DIGITAL, "digital", "Digital", "Faces")          \
    X(RD_F_BAY, "bay", "Bay Mouth", "Signature")            \
    X(RD_F_DRIFT, "drift", "Drift Plate", "Signature")      \
    X(RD_F_CONSOLE, "console", "Drive Console", "Bridge")   \
    X(RD_F_SCOPE, "scope", "Nav Scope", "Bridge")           \
    X(RD_F_VENDING, "vending", "Vending Machine", "Quarters") \
    X(RD_F_BOWL, "bowl", "Bowl", "Quarters")                \
    X(RD_F_MOPBOT, "mopbot", "Mop Bot", "Quarters")
extern const char *const RD_FACE_KEYS[RD_F__COUNT];   /* RD_FACE_TABLE's keys, by id */

/* The built rd_core faces, in the order their tasks append them (wave 1: Task 9 Stasis Clock ... Task 15 Curry Clock;
 * wave 2: Tasks 3-10, each clearing its RD_PENDING bit): the harness's table and the console's `rd faces` and `rd verify` read this. */
extern const fk_face_ops_t *const RD_FACES[];
extern const unsigned RD_FACE_COUNT;
const fk_face_ops_t *rd_face_by_key(const char *key);   /* NULL if none */

/* The lift rule (spec section 9.5): in Stasis mode, a revealing lift shows the Stasis Clock: its index in the
 * carousel, or -1 when it is not there (nothing jumps). Always on: -1. */
int rd_lift_index(const rd_prefs_t *p, const uint8_t *carousel, uint8_t len);

/* ---- The still frames every face is checked on (spec section 10.0 Stills; RW12): pinned inputs, the year 2026, yday
 * set. 0 10:09:30.500 Sun 1 Nov 24 h (slept_s 12 307); 1 03:47:12.900 Wed 15 Jul 24 h; 2 23:59:59.000 Sat 31 Oct 24 h;
 * 3 as row 0 in 12 h; 4 13:05:07.250 Sun 1 Nov 12 h; 5 glance 10:09 24 h; 6 glance 03:47, orbit (+4, -4); 7 unset;
 * 8 stale 10:09:30.500; 9 unset glance, orbit (-4, +4); 10 12 h glance 13:05. Every row but 0 has slept_s -1. Wave 2
 * Task 6: row 0's sync_age_s is 754 ("SYNC 12 MIN AGO") and row 8's 90 000 ("SYNC 25 H AGO": the stale still never
 * shows a fresh sync beside its marker, M9); every other row's is 0 (the first theme's). A face with
 * a tapped state sets stills_extra 1: still 11 is row 0 with the face's own tapped variant, its pulse playing
 * rd_still_pulse_ms in when that is not 0 (wave 2 Task 9: a tapped state that shows only while its pulse plays). The
 * CRCs of every built face's stills are rd/verify_crc.h (rd_shot --crc), checked by the host harness and the device's
 * `rd verify`. */
#define RD_STILL_N 11
typedef struct { fk_face_in_t in; bool glance; const char *what; } rd_still_t;
void rd_still(unsigned i, rd_still_t *out);             /* i < RD_STILL_N (larger: row 0) */
unsigned rd_still_count(const fk_face_ops_t *ops);      /* RD_STILL_N + ops->stills_extra */
/* The whole 466 x 466 frame of `ops` for still i (< rd_still_count) into fb (stride 466): draw, or glance for a glance
 * still; still RD_STILL_N is row 0 with the face's tapped variant (rd_still_variant), pulsing at rd_still_pulse_ms when
 * that is not 0. */
void rd_still_frame(const fk_face_ops_t *ops, const void *ctx, unsigned i, uint16_t *fb);
/* the tapped still's variant for a face with stills_extra (each face task adds its RD_<KEY>_TAPPED_VARIANT); 0 none */
uint16_t rd_still_variant(const fk_face_ops_t *ops);
/* wave 2 Task 9: the tapped still's pulse, ms since the tap (in->pulsing, pulse_ms), for a face whose tapped state
 * shows only while its pulse plays (Vending Machine's vend, RD_VM_PULSE_STILL_MS; the Bowl's paw, RD_CB_PAW_STILL_MS;
 * Task 10: Mop Bot's startled hop, RD_MB_HOP_STILL_MS); 0: the still at rest (every other face) */
uint16_t rd_still_pulse_ms(const fk_face_ops_t *ops);

/* ---- The faces' glow tables as const data (rd_core/glow_lut.h, generated by rd_shot --glow-lut; lesson 3): the bytes
 * of fk_glow_lut_build(lut, r) for each r of RD_GLOW_LUT_RADII, from the flash mapping, never built at bind. Each face
 * task appends its own radii. */
#define RD_GLOW_LUT_RADII {16, 14, 190, 9}   /* T9: Stasis's seconds slot (16), go-lamp (14), chime breath (190); T11:
                                             * Ship Terminal's PWR lamp (9: lamp()'s r x 2.6 at r 3.5) */
/* Task 12: the linear radial glows (a radial gradient of alpha 1 at the centre to 0 at r, the bridge mockup's
 * createRadialGradient(x, y, 0, x, y, r) with two stops), in the same layout (entry k for N = 8k + 2 about a whole-pixel
 * centre): Deck Sign's beacon (46). rd_shot --glow-lut writes them from rd/soft_lut.h (rd_lin_lut_build, host only). */
#define RD_LIN_LUT_RADII {46, 8, 17}   /* T13: Hull Plate's running light (lamp()'s r x 2.6 at r 3.2: 8.32) and the
                                      * wake's rivet glint (r 8); wave 2 Task 6: Drive Console's lamps (lamp()'s r x
                                      * 2.6 at r 6.5: 16.9) */
/* Task 10: the soft glows, the reveal mockup's softSprite() profile (faces-reveal.js:202-207: alpha 1, .45, 0 at 0,
 * .35, 1 of r; the mist's puffs and the stasis field's shimmer draw it, not glowSprite's), in fk_glow_lut_build's
 * layout so fk_glow_add_lut draws them: Stasis Release's mist (40-88, every 8: RD_MIST_RADII, RW9) and shimmer (120).
 * rd_shot --glow-lut writes them from rd/soft_lut.h (host only). */
#define RD_SOFT_LUT_RADII {40, 48, 56, 64, 72, 80, 88, 120, 18, 255, 16, 24, 26, 32, 10, 180, 200}
/* wave 2 Task 3: Bay Mouth's pip glow (18) and chime glow (255, the coarse layout: W2-D8, the mockup's 260 capped at
 * the tables' 255); Task 4: the airlock's dust (16, 24, 32; 40-64 above) and its lamp's glowAt (26). 16 and 26 are also
 * radii of other lists (another profile), so the soft tables are served by rd_soft_lut, keyed by radius in this list
 * alone; rd_glow_lut keeps the other lists' 16 and 26 (glow_lut.h names those two soft tables RD_SOFT_LUT_16, _26);
 * Task 5: Drift Plate's newest window (10) and its chime's glow over the time (180) and flash on the windows (200,
 * both the coarse layout) */
/* Task 14: the quarters mockup's glowSprite() profile (faces-quarters.js:80-84: alpha 1, .4, 0 at 0, .35, 1 of r; not
 * the reveal's softSprite(), whose middle stop is .45), in the same layout: Bunk Clock's lamp (26). rd_shot --glow-lut
 * writes them from rd/soft_lut.h (rd_qtr_lut_build, host only). */
#define RD_QTR_LUT_RADII {26}
const uint8_t *rd_glow_lut(int r);       /* any list's table but the soft 16 and 26; NULL for any other */
const uint8_t *rd_soft_lut(int r);       /* RD_SOFT_LUT_RADII's soft table at r; NULL for any other (Task 4) */

/* ---- The palette (notes' shared language), RGB565 */
#define RD_RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define RD_C_PAINT RD_RGB(0xDC, 0xD3, 0xBE)
#define RD_C_PAINT_DIM RD_RGB(0x8C, 0x84, 0x74)
#define RD_C_STEEL RD_RGB(0x5A, 0x60, 0x68)
#define RD_C_STEEL_HI RD_RGB(0x8A, 0x90, 0x96)
#define RD_C_PHOS RD_RGB(0x7C, 0xFF, 0x9E)
#define RD_C_PHOS_DIM RD_RGB(0x2F, 0x7A, 0x48)
#define RD_C_AMBER RD_RGB(0xFF, 0xA8, 0x3A)
#define RD_C_GO RD_RGB(0x62, 0xE0, 0x70)
#define RD_C_FROST RD_RGB(0xB9, 0xC9, 0xBE)
#define RD_C_HULL RD_RGB(0x8E, 0x2B, 0x1E)
#define RD_C_HULL_LIT RD_RGB(0xC4, 0x49, 0x2F)
#define RD_C_HAZARD RD_RGB(0xE2, 0xA6, 0x2A)
#define RD_C_STAR0 RD_RGB(0xFF, 0xD9, 0xA0)
#define RD_C_HULL_DEEP RD_RGB(0x3E, 0x12, 0x0B)
#define RD_C_HAZARD_DARK RD_RGB(0x17, 0x12, 0x0B)
#define RD_C_STEEL_DEEP RD_RGB(0x1E, 0x21, 0x24)
#define RD_C_LAMP RD_RGB(0xFF, 0x3B, 0x24)
/* the glance tokens: every channel <= 110 */
#define RD_G_PAINT RD_RGB(0x6A, 0x65, 0x55)
#define RD_G_PHOS RD_RGB(0x2C, 0x6E, 0x40)
#define RD_G_AMBER RD_RGB(0x6E, 0x48, 0x18)
#define RD_G_STEEL RD_RGB(0x34, 0x38, 0x3D)
#define RD_G_HULL RD_RGB(0x6E, 0x24, 0x16)

/* The shared stale marker (spec section 10.0; faces-reveal.js staleMark()): a hollow amber lamp (r 4, 1.6 px) at
 * (cx - 30, cy - 4.5) and STALE in Chakra 13 amber (tracked 0.14 em) from x cx - 20 on baseline cy, in place of the
 * face's top label; glance: the same in rd.g.amber. ch13 is the face's Chakra 13 atlas. */
void rd_stale_mark(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_atlas_t *ch13, int cx, int cy,
                   bool glance);

/* ---- Stasis Clock (`stasis`, spec section 10.1; Task 9). Its base (the seal arcs, 60 ticks with a rivet at every
 * fifth, the lamp housing) is drawn procedurally at bind (RW10): 0 B of assets. cadence 40 ms. Its reveal is Stasis
 * Release (rd/release.h, Task 10): under it the face settles and thaws by in->wake_ms (UINT32_MAX settled). */
extern const fk_face_ops_t RD_FACE_STASIS;
/* One seconds-slot box about its slot's centre (the arc, its glow and their edges): slot i's box is this offset by the
 * slot centre's whole pixel (rd_st_slot_rect). Within a minute dirty() returns only slot boxes (at most 7: the new head,
 * the five tail slots that fade one step and the one that leaves). */
extern const omni_rect_t RD_ST_SLOT_BOX;
void rd_st_slot_rect(int i, omni_rect_t *out);   /* slot i (0..59, the second) */
extern const omni_rect_t RD_ST_TIME;     /* the hero time (every 12/24 h string, "--:--"): redrawn at the minute */
extern const omni_rect_t RD_ST_READOUT;  /* the time-asleep readout's two lines */
extern const omni_rect_t RD_ST_STALE;    /* the top label and the stale marker in its place */
extern const omni_rect_t RD_ST_DATE;     /* the date line (12 h: with its AM/PM suffix) */
extern const omni_rect_t RD_ST_LAMP;     /* the go-lamp and its glow */
extern const omni_rect_t RD_ST_TAP;      /* the tap's target: the whole face (FK_TAP_CHIME) */
/* the chime picture's rects (the seal ring's 8 sector boxes, the numerals' breath, the lamp): outside them a chime
 * frame equals the silent one; returns the count (<= RD_ST_CHIME_MAX) */
#define RD_ST_CHIME_MAX 10
unsigned rd_st_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);
/* The tap's chime preview gate (C9; pure): only when Chime is on, the sound is on and not provisioning */
bool rd_chime_preview_allowed(bool chime_on, bool sound_on, bool provisioning);

/* ---- Ship Terminal (`terminal`, spec section 10.7; Task 11; faces-bridge.js:696-801). A green CRT on the void: the
 * housing as edges (rd_tm_house, A8 380 x 321 at (43, 79): an index into its ramp), the PWR lamp, and inside the
 * glass (RD_TM_GLASS, r 20) the header DEEP HAUL CO. . TERM 04 (or the stale marker) with its spinner, a two-line
 * status log that types itself, the time in VT323 132 phosphor (glow plane, then core), the prompt with its block
 * cursor, then the scanlines (every third row from y 98 at x 0.8). cadence 40 ms.
 *   Rest motion: the cursor (530 ms on, 530 off) and the spinner (180 ms a step) from in->mono_ms; the log types at 40
 *   characters a second from a tap's pulse start (pulse_len_ms 1 350 covers the longest page, 52 characters, PR32);
 *   at each minute change the old time lingers 120 ms at 30 % (m:00.000 to m:00.119, from the clock). Under reduced
 *   motion the cursor shows steady, the spinner is '*', the log is typed and nothing lingers.
 *   The wake (wake_len_ms 900): a line opens to a raster in 250 ms inside the glass only, the header appears, the time
 *   blooms in with a brightness overshoot (x 1.5 at 250 ms, to 1 at 650), the log types from 300 ms; from 860 ms (the
 *   wake's last frame at the 40 ms cadence) the page shows whole, so rest begins typed (PR32).
 *   A tap inside the glass (tap_var) moves in->variant to the next of the four pages (0 -> 1 -> 2 -> 3 -> 0) and starts
 *   the pulse (FK_TAP_PULSE): the new page types from it. Unset, the page stays (the log shows its NTP lines).
 *   The chime picture (awake, not reduced): a 40 px brighter band rolls down the glass with thrum (its centre from the
 *   glass's top to its foot over each two seconds of the clock), and the cursor's blink inverts while beep > .5 (a
 *   double blink for the bip and the bop). Its rect: RD_TM_GLASS.
 *   The glance: the time alone, the 132 core plane in rd.g.phosphor (no date, C7), unset dashes in rd.g.steel, the
 *   stale marker in rd.g.amber; all through the orbit. */
extern const fk_face_ops_t RD_FACE_TERMINAL;
#define RD_TM_TAPPED_VARIANT 1               /* still 11: page 1 */
#define RD_TM_PAGES 4
extern const omni_rect_t RD_TM_GLASS;       /* the glass: the tap's target and the chime's rect */
extern const omni_rect_t RD_TM_CURSOR;      /* every place the block cursor can be (its x follows the prompt) */
extern const omni_rect_t RD_TM_SPINNER;     /* the header's spinner and its glow */
extern const omni_rect_t RD_TM_LOG;         /* the two log lines and their glow */
extern const omni_rect_t RD_TM_TIME;        /* the time (each 12/24 h string, the unset dashes, the linger; orbit) */
extern const omni_rect_t RD_TM_STALE;       /* the header line, or the stale marker in its place */
extern const omni_rect_t RD_TM_PROMPT;      /* the prompt line (the date, 12 h with its AM/PM suffix) */
/* page p's (mod RD_TM_PAGES) log line 0 or 1 */
const char *rd_tm_page_line(unsigned page, unsigned line);
/* the log lines in shows (the unset clock's two, or page in->variant's) and how many characters of each are typed */
void rd_tm_shown(const fk_face_in_t *in, const char *lines[2], int n[2]);
unsigned rd_tm_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_TM_GLASS alone: returns 1 */
/* rd_tm_house's ramp (rd/release.h's rule; tools/export/rd_layers.json's row names the same stops): four runs from
 * black, to phosphorDim (0-63), steel (64-127), steelHi (128-191) and paintDim (192-255) */
#define RD_TM_RAMP_STOPS 8
extern const rd_ramp_stop_t RD_TM_RAMP[RD_TM_RAMP_STOPS];

/* ---- Deck Sign (`deck`, spec section 10.6; Task 12; faces-bridge.js:578-691). A corridor junction sign on the void:
 * the frame layer (rd_dk_frame, A8 394 x 301 at (36, 96): an index into RD_DK_RAMP's four runs; the edge-lit frame
 * (36, 116) 394 x 236, the rivet line with its rust, the divider, DECK and SECTION in stencil 30, FR 0233 and FWD/AFT
 * (their triangles drawn), the beacon's base and the notice plate's edge and rivets), the hazard band across its head (rd_dk_haz,
 * RGB565 384 x 12 at (41, 121)), DECK = the hour (paint .92) and SECTION = the minute (hullLit .94) in stencil 132 on
 * the font's own advances, each string centred on its column (x 134 and 332; 12 h's single hour digit too) with its
 * canvas 'middle' at 246.5 (the alphabetic baseline 292), the amber beacon on top at (233, 88) and the notice (Chakra
 * 13 paint, tracked 1.8, centred on 234; its baseline 385).
 *   Rest motion: the beacon turns (its highlight and glow) at low strength (.45) from the clock (one turn each 4 s,
 *   the mockup's speed .6 after its wake); for 5 s after a revealing wake (in->fresh_ms) it runs at full strength and
 *   a turn faster over those 5 s (one more whole turn, so its phase is the clock's again from 5 s), fading to .45 over
 *   5.0-5.6 s. The minute's change sprays the new minute on through the stencil for 0.7 s (m:00.000 to m:00.699, from
 *   the clock; easeOut3): a seeded dissolve mask (RD_DK_SPRAY_LUT, a 128 x 128 toroidal tile of the mockup's spray()
 *   circles' thresholds, offset per number by its seed: 7 the hour's, 8 + the day's minute mod 50 the minute's) over the
 *   atlas glyphs; the hour column too when the hour changes (m == 0). Under reduced motion the beacon stands still (the
 *   mockup's reduced pose: strength .5, phase .25) and a new minute shows at once.
 *   The wake (wake_len_ms 800): the hour sprays on from 50 ms, the minute from 200 ms (700 ms each), and both show
 *   whole from 760 ms (the wake's last frame at the 40 ms cadence), so rest begins settled.
 *   A tap on the notice (RD_DK_TAP; tap_var) moves in->variant on: LIFT OUT OF ORDER SINCE LAUNCH -> NO RUNNING .
 *   ALSO NO JOGGING -> NEXT TOILET 4 KM (and a drawn triangle) -> the date . ALL DECKS (12 h: the date's AM/PM suffix)
 *   -> the first again (RAM only); not while no notice shows (stale: the marker; unset: the amber line; the Task 12
 *   review). It returns FK_TAP_PULSE with no pulse_len_ms: the runtime draws the new variant and starts nothing.
 *   Unset: two short paintDim dashes per column, the beacon dark, the notice TIME NOT SET . AWAITING SIGNAL in amber.
 *   Stale: the shared marker replaces the notice's text (the face has no top label).
 *   The chime picture (awake, set, not reduced): the beacon at full strength while the chime's thrum runs; the rivet
 *   line's 17 heads blink toward paint with the relay's click.
 *   The glance: the two numbers as the 132 stencil's 1.5 px outlines (rd.g.paint, rd.g.hull), DECK and SECTION in
 *   Chakra 13 (rd.g.paint, rd.g.hull), unset dashes in rd.g.steel, the stale marker in rd.g.amber; all through the
 *   orbit. */
extern const fk_face_ops_t RD_FACE_DECK;
#define RD_DK_TAPPED_VARIANT 1               /* still 11: NO RUNNING . ALSO NO JOGGING */
#define RD_DK_NOTES 4
extern const omni_rect_t RD_DK_BEACON;      /* the beacon: its glow (r 46 about (233, 88)), its dome and its cage */
extern const omni_rect_t RD_DK_SPRAY_H;     /* the hour's column (170 x 150 about (134, 246)): every hour string */
extern const omni_rect_t RD_DK_SPRAY_M;     /* the minute's column (170 x 150 about (332, 246)) */
extern const omni_rect_t RD_DK_NOTICE;      /* the notice's text inside the plate (the stale marker included) */
extern const omni_rect_t RD_DK_STALE;       /* the stale marker in the notice's place */
extern const omni_rect_t RD_DK_TAP;         /* the notice's hit rect (the plate, grown to >= 44 px tall) */
extern const omni_rect_t RD_DK_RIVETS;      /* the rivet line's heads (the click's blink) */
/* the notice in shows (the unset text, or note in->variant mod RD_DK_NOTES), into out; true when the drawn triangle
 * follows it; "" when stale (the marker shows) */
bool rd_dk_notice(const fk_face_in_t *in, char out[48]);
/* the spray's mask level at t ms into a 700 ms spray: 0 (nothing) at t <= 0, rising by easeOut3 to 252 at 699 ms,
 * RD_DK_SPRAY_DONE (the whole glyph) from 700 ms; a pixel shows when its RD_DK_SPRAY_LUT entry is at most the level */
#define RD_DK_SPRAY_DONE 254
int rd_dk_spray_level(long t_ms);
unsigned rd_dk_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_DK_BEACON and RD_DK_RIVETS: returns 2 */
/* rd_dk_frame's ramp (rd/release.h's rule; tools/export/rd_layers.json's row names the same stops): four runs from
 * black, to steelHi (0-63), paint (64-127), hull (128-191) and hullLit (192-255) */
#define RD_DK_RAMP_STOPS 8
extern const rd_ramp_stop_t RD_DK_RAMP[RD_DK_RAMP_STOPS];
/* the spray's dissolve mask: a 128 x 128 tile (rd_core/spray_lut.h, generated by rd_shot --spray-lut from
 * rd/soft_lut.h's rd_spray_lut_build) */
#define RD_DK_SPRAY_TILE 128
const uint8_t *rd_dk_spray_lut(void);   /* RD_DK_SPRAY_TILE^2 levels, row by row */

/* ---- Hull Plate (`hull`, spec section 10.8; Task 13; faces-bridge.js:806-1002). The outer hull at night under one work
 * lamp, an analogue dial: the lit plate (rd_hp_plate, A8 278 x 261 at (10, 7): an index into RD_HP_PLATE_RAMP, the
 * hullDeep -> hullLit red and a steel run; the plate shows only inside the lamp's pool, baked in the A8), the marks over
 * it (rd_hp_marks, A8 436 x 435 at (15, 15): each byte a colour and an alpha of RD_HP_MARK_RUNS laid over the plate;
 * the 48-rivet ring, the minute track, 12, 3, 6 and 9 in stencil 64 and the rest in stencil 30, the 4 knocked askew,
 * the strike's cracks), the patch (rd_hp_patch, RGB565 170 x 92 at (222, 238): the crater with the rock still in it
 * and the ticket DENT LOGGED / REPAIR: PENDING), the hazard arc at 12 (r 196-207, +-0.2 rad: 45-degree stripes on
 * hazardDark crawling 1 px a second from the clock), the red running light at 10:30, the readout above 6 (Chakra 18
 * paint), the hands (the shared metal rule: rd_hand_metal) and the amber seconds hand with its hex-nut counterweight
 * (rd_hand_hull_sec), and the hex-bolt hub. cadence 50 ms.
 *   Rest motion: the seconds hand ticks with a heavy 220 ms overshoot (rd_hp_sec_angle); the running light blinks
 *   150 ms each second (s.000 to s.149, from the clock); the hazard arc crawls a pixel each second. The hour and minute
 *   hands step once a minute. Under reduced motion the seconds hand steps with no overshoot, the light shows steady at
 *   .6 and the arc stands still.
 *   The wake (wake_len_ms 1 000): the hands swing in from 12 with a mechanical overshoot (the mockup's spring from
 *   80 ms) and a glint runs once round the rivet ring; full-dial, drawn whole (R145: its rate recorded); from 950 ms
 *   (its last frame at the 50 ms cadence) it shows settled.
 *   A tap on the readout (RD_HP_TAP; tap_var) moves in->variant on: the date (12 h: with its AM/PM suffix) -> RESERVE
 *   and the battery (--% unknown) -> FR 0412 . PLATE 7 -> the date (RAM only); not while the readout is hidden (stale:
 *   the marker; unset: the unset line). FK_TAP_PULSE with no pulse_len_ms: the runtime draws the new variant.
 *   Unset: no hands, only the bolt; paintDim --:-- above it; AWAITING TIME SIGNAL (Chakra 13 paintDim) in the readout's
 *   place; the running light breathes amber (from in->mono_ms; steady at .35 under reduced motion).
 *   Stale: the shared marker replaces the readout (the face's only label); nothing else changes.
 *   The chime picture (awake, not reduced): the work lamp's pool swells and sags with thrum (the plate's ramp x
 *   (1 + .6 thrum), inside RD_HP_POOL), and the running light blinks on with each note (beep > .5).
 *   The glance: 12, 3, 6 and 9 as the stencil 64's 1.5 px outlines (rd.g.paint), both hands as 1.5 px outlines
 *   (rd.g.paint), every fourth rivet and the rock as a dim dot (rd.g.steel), the hub's ring; unset, the --:-- in
 *   rd.g.steel; stale, the marker in rd.g.amber; all through the orbit.
 * Angles: radians clockwise from 12 (fk/shapes.h's rule), the hands about the dial's centre (233, 233). */
extern const fk_face_ops_t RD_FACE_HULL;
#define RD_HP_TAPPED_VARIANT 1               /* still 11: RESERVE --% */
#define RD_HP_PAGES 3
extern const omni_rect_t RD_HP_RUNLIGHT;    /* the running light at 10:30: its glow, its lens and its ring */
extern const omni_rect_t RD_HP_HAZARD;      /* the hazard arc at 12 */
extern const omni_rect_t RD_HP_READOUT;     /* the readout above 6 (every page, 12 h's suffix, the unset line) */
extern const omni_rect_t RD_HP_STALE;       /* the stale marker in the readout's place */
extern const omni_rect_t RD_HP_TAP;         /* the readout's hit rect (>= 44 px) */
extern const omni_rect_t RD_HP_POOL;        /* the work lamp's pool: rd_hp_plate's box (the chime's swell) */
extern const omni_rect_t RD_HP_UNSET;       /* the unset --:-- above the bolt */
/* The shared metal hand (the notes' metal rule; faces-bridge.js:888-899 hand()) at angle a about the centre: which 0
 * the hour hand (112 long, 17 wide, a 22 px tail), 1 the minute hand (174, 12, 26). A steel body shaded across its
 * short axis (steel to steelDeep), a 1 px steelHi edge on its leading side, the paint stripe in two runs with a 4 px
 * stencil bridge at .62 of its length, and (outline) the 1.5 px black outline. */
void rd_hand_metal(uint16_t *fb, int stride, const omni_rect_t *clip, float a, uint8_t which, bool outline);
void rd_hand_metal_bbox(float a, uint8_t which, omni_rect_t *out);   /* every pixel the hand can touch */
/* Hull Plate's seconds hand at angle a (faces-bridge.js:973-981): a 2 px amber line from the nut (40 px back) to 192
 * px over a 4 px black one, the amber hex nut r 8 with its 1.5 px black edge and its black hole r 3.2 */
void rd_hand_hull_sec(uint16_t *fb, int stride, const omni_rect_t *clip, float a);
void rd_hand_hull_sec_rects(float a, omni_rect_t out[2]);   /* the arm's box and the tail's (with the nut) */
/* The seconds hand's angle at rest: (s - 1 + k) / 60 of a turn, k from a const 12-entry table of the mockup's spring()
 * over ms 0..220 (its heavy overshoot: past s from about s.040 to s.100, at its height (+15 % of a step) about s.066),
 * exactly s / 60 from s.220; under reduced motion s / 60 at once. The wake's swing scales it. */
float rd_hp_sec_angle(const fk_face_in_t *in);
/* the hour and minute hands' angles at rest (each steps once a minute: lesson 9) */
void rd_hp_hand_angles(const fk_face_in_t *in, float *hour, float *minute);
/* the readout in shows (page in->variant mod RD_HP_PAGES), into out; "" when stale (the marker shows), the unset line
 * unset */
void rd_hp_readout(const fk_face_in_t *in, char out[32]);
unsigned rd_hp_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_HP_POOL and RD_HP_RUNLIGHT: returns 2 */
/* rd_hp_plate's ramp (rd/release.h's rule; tools/export/rd_layers.json's row names the same stops): black to hullDeep
 * (0-150) to hullLit (151-191), and black to steelHi (192-255) */
#define RD_HP_PLATE_RAMP_STOPS 5
extern const rd_ramp_stop_t RD_HP_PLATE_RAMP[RD_HP_PLATE_RAMP_STOPS];
/* rd_hp_marks' runs (the row's a8.runs): byte i in [i0, i1] is (r, g, b) at alpha (i - i0 + 1) / (i1 - i0 + 1) over the
 * plate; 0 none */
typedef struct { uint8_t i0, i1, r, g, b; } rd_hp_run_t;
#define RD_HP_MARK_RUNS_N 5
extern const rd_hp_run_t RD_HP_MARK_RUNS[RD_HP_MARK_RUNS_N];

/* ---- Bunk Clock (`bunk`, spec section 10.12; Task 14; faces-quarters.js:678-821). A battered bedside flip clock on a
 * bunk shelf: the case layer (rd_bk_case, RGB565 384 x 221 at (42, 116): the steel snooze bar, the cracked hull-red case
 * with its tape and the two card wells) on #060606, the shelf below it (rd_bk_shelf: one column of its rows from y 336,
 * drawn across), the bar's SNOOZE (Chakra 13 hazardDark), the status lamp at (160, 80) (a phosphor
 * lens r 13 in a steelDeep ring r 16, its rolling scanlines and its glow r 26; a glow and a word only, no letter badge
 * and no likeness) labelled ROOMMATE . PROJECTED (Chakra 13 paintDim from x 184), the two split-flap cards (HH and MM,
 * 174 x 150 at (53, 173) and (239, 173): the card blank drawn at bind from cardFace()'s recipe, the digits in stencil
 * 132 paint on the time's fixed advances, centred on the card; 12 h: the single hour digit centred; the split and the
 * hinge pins), date . BUNK 2 (Chakra 13 paintDim; 12 h: the date's AM/PM suffix before . BUNK 2) and SNOOZED
 * 1 095 000 308 TIMES (VT323 28 amber phosphor). cadence 40 ms.
 *   The count (the ruling, C3; lesson 8): rd_bk_count, derived from the clock and the RAM variant, never stored.
 *   The variant (RW13; RAM only, never NVS): bit 15 the roommate is paused; bit 14 the last tap was the lamp (so the
 *   pulse that follows is the lamp's); bits 0-13 the snooze taps, wrapping at 16 384.
 *   Rest motion: the lamp (its 32 px lens and its glow) from the clock: the scanlines roll a pixel every 60 ms, and every
 *   7.3 s the lens tears for 160 ms (four 40 ms steps between .9 and .25, the scanlines shifted); the minute card flips
 *   on each change (m:00.000 to m:00.449, from the clock: the old top folds down to the hinge, then the new bottom falls,
 *   easeIn2 over 450 ms; two half-card scalings through fk_rows_blit inside RD_BK_FLIP_M), the hour card too when the
 *   hour's string changes; the count's old value lingers 120 ms at 30 % when it changes (a snooze, and midnight).
 *   Under reduced motion the lamp is steady (no roll, no tear, no stutter), new digits show at once and nothing lingers.
 *   The wake (wake_len_ms 1 000): every card flips in from -- (the hours over 0-450 ms, the minutes over 250-700 ms)
 *   and the lamp stutters on over its first 400 ms; from 700 ms it shows settled.
 *   A tap on the bar (RD_BK_HIT_BAR; tap_var): one more snooze (bits 0-13 + 1, bit 14 cleared) and the pulse
 *   (pulse_len_ms 1 500): the bar goes down for 200 ms and the count line reads [ REQUEST LOGGED ] (its first 120 ms at
 *   .5 over the old count at .3), then the new count. Unset the bar is inert: FK_TAP_NONE, the variant left as it is.
 *   A tap on the lamp (RD_BK_HIT_LAMP; tap_var): bit 15 toggles and bit 14 is set; paused, the glow cuts and the label
 *   reads ROOMMATE . PAUSED (BLISS) in paint; unpaused, the lamp flickers back on over the pulse's first 400 ms.
 *   Unset: -- -- on the cards in paintDim, --- -- --- . BUNK 2, SNOOZED ? TIMES at .6, the bar's SNOOZE at .5.
 *   Stale: the shared marker in the roommate label's place; nothing else changes.
 *   The chime picture (awake, not reduced): the lamp's glow swells with thrum; the minute card twitches half a flap
 *   (its top folding a quarter of the way) with beep. Its rects: RD_BK_LAMP, RD_BK_FLIP_M.
 *   The glance: the digits as the 132 stencil's 1.5 px outlines (rd.g.paint), the two cards' split lines (rd.g.steel)
 *   and a hollow lamp ring (rd.g.phosphor, with a bar across it while the roommate is projected); the stale marker in
 *   rd.g.amber; all through the orbit. */
extern const fk_face_ops_t RD_FACE_BUNK;
#define RD_BK_TAPPED_VARIANT 0x8001u     /* still 11: the roommate paused, one snooze */
#define RD_BK_PAUSED 0x8000u
#define RD_BK_LAMP_LAST 0x4000u
#define RD_BK_TAPS 0x3FFFu
#define RD_BK_COUNT_BASE 1095000000u     /* 3 000 000 years of one snooze a day */
/* 1 095 000 000 + the day of the year (c->yday, 1..366) + the snooze taps (variant & RD_BK_TAPS); 0 when the clock is
 * unset (drawn as SNOOZED ? TIMES) */
uint32_t rd_bk_count(const fk_clock_t *c, uint16_t variant);
/* the count line's text for n: "SNOOZED 1 095 000 308 TIMES" (the digits in threes); n 0: "SNOOZED ? TIMES" */
void rd_bk_count_str(uint32_t n, char out[32]);
extern const omni_rect_t RD_BK_HIT_BAR;     /* the snooze bar's hit box, 210 x 48 (the visual 210 x 30, C17) */
extern const omni_rect_t RD_BK_HIT_LAMP;    /* the lamp's hit box, 48 x 48 about its centre (the visual 32 px) */
extern const omni_rect_t RD_BK_LAMP;        /* the lamp: its glow r 26 about (160, 80), its ring and its lens */
extern const omni_rect_t RD_BK_LENS;        /* the lens alone (32 x 32): the scanlines' roll */
extern const omni_rect_t RD_BK_LABEL;       /* the roommate's label (either text), or the stale marker in its place */
extern const omni_rect_t RD_BK_STALE;       /* the stale marker */
extern const omni_rect_t RD_BK_ROOM;        /* RD_BK_LAMP and RD_BK_LABEL: what a lamp tap redraws */
extern const omni_rect_t RD_BK_BAR;         /* the bar's press and its SNOOZE */
extern const omni_rect_t RD_BK_FLIP_H;      /* the hour card (174 x 150 at (53, 173)) */
extern const omni_rect_t RD_BK_FLIP_M;      /* the minute card (174 x 150 at (239, 173)): its flip and its twitch */
extern const omni_rect_t RD_BK_DATE;        /* the date line (12 h: with its AM/PM suffix) */
extern const omni_rect_t RD_BK_COUNT;       /* the count line (the count, its linger, [ REQUEST LOGGED ], the unset line) */
unsigned rd_bk_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_BK_LAMP and RD_BK_FLIP_M: returns 2 */
/* how many card leaves the face (its bound ctx) has composed in its draws so far: a frame whose leaves are all held
 * composes none (four slots, the Task 14 review) */
uint32_t rd_bk_leaf_composes(const void *ctx);

/* ---- Curry Clock (`curry`, spec section 10.10; Task 15; faces-quarters.js:350-503). Tonight's takeaway seen from above
 * on black: the tray layer (rd_cu_tray, RGB565 336 x 336 at (65, 65): buildBg()'s crimped foil r 168 under its 32 %
 * shade and the curry r 147, cut to its disc), the 48 minute ticks (paintDim 1.5 px, r 222-228) and the twelve numerals
 * in stencil 30 at r 201 (12, 3, 6 and 9 in paint, the rest paint x .75), the heat sticker at 8 o'clock (148 x 64,
 * turned .2 rad about (138, 330): HEAT, MILD / MED / HOT unticked, INADVISABLE ticked in red biro, and the chillies,
 * drawn at bind from sticker()'s recipe into a sprite), the steel fork (the hour) and spoon (the minute) with their
 * shadows, the peppercorn hub, and the steam rising over them. cadence 50 ms.
 *   Rest motion: the steam (steam(): four wisps of 30 puffs of the 32 x 32 blob, their positions from the clock's ms; no
 *   free timer), drawn from a pre-rendered per-wisp loop (rd_cu_wisp, wave 2 Task 12: RD_CU_WISP_* below) inside
 *   RD_CU_STEAM. The hands step
 *   once a minute (lesson 9). Under reduced motion the steam is frozen (the mockup's 4 s pose at .7).
 *   The wake (wake_len_ms 1 000): the card lid (rd_cu_lid, A8 336 x 336: an index into RD_CU_LID_RAMP; the bind
 *   composes it, tinted, with its printed No 42 . EXTRA HOT. SORRY. (the ordinal sign, Latin-1 0xBA) in biro, into one RGB565 leaf, PR24) swings up on
 *   its top edge (RD_CU_LID_HINGE) over easeInOut(t / 1 000), squashed to cos(.55 pi swing) of its height (the mockup's
 *   flat scale: rows of the squash through fk_rows_blit, one call a row, cut to the card's disc), black .3 swing over its
 *   box, all inside RD_CU_LIDBOX; then the steam puffs up (x 1 + 1.5 (1 - t / 1 500), from the wake's and the fresh
 *   wake's clock). Under reduced motion the face shows settled (no own wake).
 *   A tap anywhere on the tray (RD_CU_HIT; tap_var): one more chilli (variant bits 0-1: 0 -> 1 -> 2 -> 3 -> 0, RAM only,
 *   never NVS) and the pulse (pulse_len_ms 400): at three the steam tints red and the tray shudders for the pulse
 *   (sin(t / 18) x 3 (1 - t / 420) px across, the whole frame; none under reduced motion). Unset the tap does nothing
 *   (FK_TAP_NONE, the variant left).
 *   Unset: the lid stays on, --:-- (stencil 64) and READY SOON (Chakra 13) on it in biro, and no hands, sticker or steam.
 *   Stale: the shared marker above the hub (staleMark(233, 155)); nothing else changes.
 *   The biro is graphite #3A3A3A (R = G = B; PR23: the mockup's #2a2a5a breaks spec section 10.0's no blue).
 *   The chime picture (awake, set, not reduced): the steam thickens with thrum (x 1 + thrum); the peppercorn glints
 *   with beep (a paint glow r 9). Its rects: RD_CU_STEAM, RD_CU_HUB.
 *   The glance: the rim's hairline (rd.g.steel, 1.5 px), 12, 3, 6 and 9 (stencil 30, rd.g.paint), the cutlery as 1.4 px
 *   outlines (rd.g.paint); unset, the --:-- as outlines; stale, the marker in rd.g.amber; all through the orbit.
 * Angles: radians clockwise from 12, the hands about the dial's centre (233, 233). */
extern const fk_face_ops_t RD_FACE_CURRY;
#define RD_CU_TAPPED_VARIANT 3u              /* still 11: three chillies (the red steam) */
#define RD_CU_HEAT 0x0003u                   /* the variant's chillies */
extern const omni_hinge_t RD_CU_LID_HINGE;  /* { HORIZONTAL, 0, 560, 336, 0, 0 }: the lid box's top edge, box-local */
extern const omni_rect_t RD_CU_LIDBOX;      /* the lid's box, 336 x 336 at (65, 65): the tray layer and the wake */
extern const omni_rect_t RD_CU_STEAM;       /* every pixel the steam can touch (its wisps' reach, 221 x 255) */
extern const omni_rect_t RD_CU_STICKER;     /* the heat sticker's sprite (162 x 96): a tap's chilli */
extern const omni_rect_t RD_CU_STALE;       /* the stale marker above the hub */
extern const omni_rect_t RD_CU_HUB;         /* the peppercorn and its glint */
extern const omni_rect_t RD_CU_HIT;         /* the tap's target: the tray's box */
/* The lid's ramp (rd_cu_lid's A8 is an index into it; tools/export/rd_layers.json's row names the same stops): 1-32
 * the rim's #a8a396 at its edge coverage k / 32 (laid over the tray), 33-160 the card's gradient #7f796c to #c9c3b4,
 * 161-255 the same under the curry stain (.25 of (140, 70, 20)); 0 no card */
#define RD_CU_LID_RAMP_STOPS 6
extern const rd_ramp_stop_t RD_CU_LID_RAMP[RD_CU_LID_RAMP_STOPS];
#define RD_CU_BIRO RD_RGB(0x3A, 0x3A, 0x3A)  /* the biro: graphite (PR23) */
/* The cutlery (faces-quarters.js:392-408 fork(), spoon(), hands()), rd_hand_metal's signature: which is the pass,
 * RD_CU_PASS_SHADOW the shadow (4, 6) px off in black .55, RD_CU_PASS_STEEL the steel (the screen's linear gradient
 * #eef0f1 / steelHi / #e6e8ea from (173, 173) to (293, 293)) and, with outline, its 1.5 px black outline,
 * RD_CU_PASS_GLANCE the glance's 1.4 px rd.g.paint outline alone ('lighten'); the fork 108 px long, the spoon 152 */
#define RD_CU_PASS_SHADOW 0
#define RD_CU_PASS_STEEL 1
#define RD_CU_PASS_GLANCE 2
void rd_hand_fork(uint16_t *fb, int stride, const omni_rect_t *clip, float a, uint8_t which, bool outline);
void rd_hand_spoon(uint16_t *fb, int stride, const omni_rect_t *clip, float a, uint8_t which, bool outline);
void rd_hand_fork_bbox(float a, uint8_t which, omni_rect_t *out);    /* every pixel the pass can touch */
void rd_hand_spoon_bbox(float a, uint8_t which, omni_rect_t *out);
/* the hour (fork) and minute (spoon) angles: each steps once a minute (lesson 9) */
void rd_cu_hand_angles(const fk_face_in_t *in, float *hour, float *minute);
unsigned rd_cu_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_CU_STEAM and RD_CU_HUB: returns 2 */
/* The steam's structure (W2-D7; wave 2 Task 12): puff k of wisp w sits at life = frac(base_w + k/30 + .23 w), x =
 * 233 + OX[w] + sin(7 life + wob + 1.7 w)(6 + 22 life); so every wisp is the same image G(phi, theta) moved by
 * OX[w], with theta = wob + 1.7 w and phi the puff lattice's phase. G(phi, theta + pi) is G(phi, theta) mirrored
 * about the wisp's axis, and G(phi + 1/30, theta) is G(phi, theta). */
#define RD_CU_WISP_W 110            /* the wisp's reach: +-(28 + 27) px */
#define RD_CU_WISP_H 255            /* y 16 .. 271 (RD_CU_STEAM's rows) */
#define RD_CU_WISP_N 24             /* theta steps over [0, pi): the mirror gives [pi, 2 pi) */
#define RD_CU_WISP_SCALE 2          /* stored at half resolution, bilinear x 2 at the blit */
/* Step 1's measurement (Task 12): G's largest difference across phi is 6.4 levels (> 2), so each theta step has two
 * phi frames, phi = 0 and 1/60 (the lattice's half step) */
#define RD_CU_WISP_NPHI 2
#define RD_CU_WISP_HW (RD_CU_WISP_W / RD_CU_WISP_SCALE)                             /* 55 columns stored */
#define RD_CU_WISP_HH ((RD_CU_WISP_H + RD_CU_WISP_SCALE - 1) / RD_CU_WISP_SCALE)     /* 128 rows stored */
#define RD_CU_WISP_ROWS (RD_CU_WISP_N * RD_CU_WISP_NPHI * RD_CU_WISP_HH)            /* 6 144 */
#define RD_CU_WISP_Q 6              /* a stored byte is 6 x the wisp's light S (S: the sum of each puff's alpha x its
                                       blob texel, 0..255, so a channel c adds c x S / 256 of 255) */
/* rd_cu_wisp (tools/rd_pack.py kind a8_rows, tools/facepack.py's ARW1): "ARW1", u16 w (RD_CU_WISP_HW), u16 h
 * (RD_CU_WISP_ROWS), then each row's extent [x0, x1) as two bytes, then each row's x1 - x0 bytes in turn, the file
 * padded to an even length. Row ((t NPHI + p) HH + k) is frame (theta t pi / N, phi p / 60)'s row k: the light of local
 * rows 2k, 2k + 1 (y 16 + 2k ...) and columns 2x, 2x + 1 (screen 233 + OX[w] - 55 + 2x ...), worked out so the blit's
 * bilinear x 2 gives the full-resolution wisp back (least squares), each puff's contribution floored as the procedural
 * steam floored it (its 8.4 sums at 255, amt 1).
 * The draw: per wisp, phi' = frac(base_w + .23 w) between its two stored frames p0 = floor(60 phi') and p0 + 1 (the
 * lattice wraps: frame 2 is frame 0) at g = frac(60 phi'); each side moved along the rise by what the lattice moves
 * (G(phi0 + d, theta) is near G(phi0, theta + 7 d) moved up 220 d px: the motion-compensated blend), at theta step
 * floor(theta' / step) and the next (mirrored past pi) blended by the fraction: four frame rows per output row, each
 * pair blended vertically at its own (shifted) fraction, then x 2 across by (1, 3) / 4; times amt and the cream or red
 * colour, added into RD_CU_STEAM's rows (rounded as the 8.4 sums were). The inner loop is integer-only and spans each
 * row's extents only (I6). */
/* the one-wisp light of local row u (0 .. RD_CU_WISP_H - 1) at theta = theta_q / 256 steps of pi / RD_CU_WISP_N
 * (any integer: taken modulo 2 RD_CU_WISP_N steps) and phi = phi_q / 256 sixtieths (modulo 2), as the draw blends it:
 * out[x] for local column x (0 .. RD_CU_WISP_W - 1), in units of 1 / (4 x 65536 x RD_CU_WISP_Q) of S. Returns false
 * (out zeroed) when u is outside the wisp. */
bool rd_cu_wisp_row(const void *ctx, int32_t theta_q, int32_t phi_q, int u, uint32_t out[RD_CU_WISP_W]);
/* the last frame's steam samples: each frame row pair blended at one stored column (two texel reads) */
uint32_t rd_cu_wisp_samples(const void *ctx);
#ifdef RD_TEST_HOOKS
/* Host test builds only (the host CMake defines RD_TEST_HOOKS for rd_core; no device build does): when set, the face
 * draws its steam through this instead of the loop, with its state (T the clock's ms, amt, red) and the scene's
 * clip, so a test renders wave 1's procedural steam (firmware/test/host/rd_cu_steam_oracle.c) in its place */
extern void (*rd_cu_steam_ref)(uint32_t T, float amt, bool red, uint16_t *fb, int stride, omni_rect_t clip);
#endif

/* ---- Bay Mouth (`bay`, spec section 10.2; wave 2 Task 3; faces-reveal.js:612-687 bayBase(), hazardRing() and
 * bayFace() settled, :731-741 sleep_airlock() the glance). Looking out of the open cargo bay on the void: the upper stars
 * (seed 301, 260 of them, none on the giant: the mockup's rng and stars() recipe, drawn above y 266 at bind), the star
 * band (rd_by_star, RGB565 466 x 200 at y 266: the corona, the red giant about (233, 568) r 272 at its 45 % and its limb,
 * and the stars below y 266), moved down 0..3 px by the clock (rd_by_star_dy, C18: the burn-in rule for a large warm
 * area), the door frame's lip (r 212, 9 px #0B0C0D; r 207.5, 1 px steelHi .4) and its 48 rivets at r 212, the hazard
 * ring (r 217-236: hazardDark at .72 by fk_ring_fill's rule, then rd_by_ring's yellow, an A8 polar strip 1 420 x 19 that
 * carries the mockup's .72, in rd.hazard, one column a second clockwise: rd_by_ring_col), the amber beacon pip running
 * round r 200 as the seconds (r 3.6 and its soft glow r 18 at .55), BAY 03 . DOORS OPEN (Chakra 13 paintDim, tracked)
 * at y 98, or the stale marker there; the date (VT323 28 amber; 12 h with its AM/PM suffix) at y 130; the time (stencil
 * 132 paint) at y 252; the caption DOORS: OPEN (EVENTUALLY) (Chakra 13 hazard at .9, tracked) at y 288. cadence 40 ms.
 * The upper stars, the lip, its rivets and the ring's dark pass are procedural, the lip and the ring kept as per-pixel
 * coverages of their annulus at bind (integer: no per-pixel square root in a draw).
 *   The ring's strip is 1 420 columns (one a pixel at r 226), which fk_annulus_draw_a8 cannot take (its n_theta is a
 *   power of two), so the face keeps its own polar map of the annulus with that rule (a pixel centre's r^2 against the
 *   rows' squares, its column from omni_atan2_tab, clockwise from 12): pixel (row, col) reads strip entry (row, (col -
 *   rd_by_ring_col) mod 1 420), so the stripes turn clockwise as the mockup's rotate() does.
 *   Rest motion: the pip eases to the next second over 250 ms (rd_by_pip_turn); the ring steps a column at each second;
 *   the band steps a pixel every 2 min. Under reduced motion the pip stands at the second's settled place ((s + 1) / 60)
 *   and steps once a second; the band's shift stays (R171) and the ring steps a column once a minute, with the time
 *   (R171: (h x 60 + m) mod 1 420), so the burn-in rules still hold with no ambient motion.
 *   While in->wake_ms runs (the airlock's interior, Task 4): settle = outCubic(ph(wake_ms, 1200, 1950)); the label and
 *   the date at outCubic(ph(settle, .15, 1)); the time drops in by (1 - outBack(ph(wake_ms, 1230, 1700))) x -16 px at
 *   clamp(drop x 1.6, 0, 1); the caption at outCubic(ph(settle, .5, 1)) x .9; the pip at settle. Settled (UINT32_MAX),
 *   reduced or glance: all 1. Reveal false here (true from Task 4).
 *   A tap anywhere: FK_TAP_CHIME (the preview only when Chime and the sound are on, C9: rd_chime_preview_allowed).
 *   Unset: --:-- in paintDim at .8, no pip, the date --- -- ---, DOORS: OPEN . CLOCK: NOT YET; the ring at column 0 and
 *   the band unshifted. Stale: the shared marker at y 98 in the label's place.
 *   The chime picture (awake, not reduced): the ring brightens additively (its 19 px annulus, hazard x .35 thrum), the
 *   star's upper limb warms (a soft glow (255, 120, 60) at (233, 470), r 255, x .25 thrum: W2-D8), and the pip flashes
 *   with beep (k = settle x (.4 + .6 beep + .3), clamped to 1).
 *   The glance: the dashed hazard arc (r 226 from .28 pi to .72 pi of the canvas's angles, 3 px, 8 on 8 off, rd.g.hazard),
 *   the star's limb (about (233, 568) r 272 from 1.2 pi to 1.8 pi, 1.2 px, rd.g.hull), the date (VT323 28 core,
 *   rd.g.amber, no AM/PM) at y 130, the time as the 132 stencil's 1.5 px outline (rd.g.paint) at y 252, the stale marker
 *   in rd.g.amber; all through the orbit. */
extern const fk_face_ops_t RD_FACE_BAY;
#define RD_G_HAZARD RD_RGB(0x6E, 0x52, 0x14)   /* rd.g.hazard, the glance token (notes' table) */
extern const omni_rect_t RD_BY_STAR;     /* the star band, {0, 266, 466, 466}: its shift redraws it */
extern const omni_rect_t RD_BY_RING;     /* the ring's box (r 236 and its edge about 233: the screen) */
/* R173: what dirty() names at each step of the crawl: the screen's four rim bands (82 px deep), which hold the ring;
 * returns 4 */
unsigned rd_by_ring_rects(omni_rect_t out[4]);
/* the pip's box about its centre's whole pixel (its glow r 18 and its disc r 3.6): a template, as RD_ST_SLOT_BOX;
 * rd_by_pip_rect places it. Within a second dirty() returns only placed pip boxes (the pip's place before and after) */
extern const omni_rect_t RD_BY_PIP;
void rd_by_pip_rect(float turn, omni_rect_t *out);   /* the pip's box at a turn (0..1 from 12, clockwise) */
extern const omni_rect_t RD_BY_TIME;     /* the time at rest (every 12/24 h string, the unset dashes) */
extern const omni_rect_t RD_BY_DATE;     /* the date line (12 h: with its AM/PM suffix) */
extern const omni_rect_t RD_BY_CAPTION;  /* the caption (either text) */
extern const omni_rect_t RD_BY_STALE;    /* the top label, or the stale marker in its place */
/* the star band's vertical shift (C18): k = (sec_of_day / 120) % 6, dy = k <= 3 ? k : 6 - k (0..3 px down, 1 px every
 * 2 min, a 12 min triangle); 0 when the clock is unset */
int rd_by_star_dy(const fk_clock_t *c);
/* the ring's strip column offset: sec_of_day % 1420 (one column a second, 1 px/s at r 226); 0 unset. Under reduced
 * motion the face uses (h x 60 + m) % 1420 instead (R171) */
uint16_t rd_by_ring_col(const fk_clock_t *c);
/* the pip's turn: (s + outCubic(min(ms / 250, 1))) / 60 */
float rd_by_pip_turn(const fk_clock_t *c);
/* the chime picture's rects: the ring (RD_BY_RING), the lower glow's box and the pip's track; returns 3 */
unsigned rd_by_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);
/* the ring's two passes alone (the hazardDark annulus at .72, then the strip's yellow at column offset col) over fb, the
 * frame's pixel (clip.x0, clip.y0) with row stride `stride`: draw()'s own ring, for the tests' levels (ctx bound) */
void rd_by_ring_draw(const void *ctx, uint16_t col, omni_rect_t clip, uint16_t *fb, int stride);

/* ---- Drift Plate (`drift`, spec section 10.3; wave 2 Task 5; faces-reveal.js:743-862 buildHullTile(), washMe(),
 * hullR() and frame_hull() settled, sleep_hull() the glance). A close look along a vast rust hull on the void: the
 * stars (seed 503, 230 of them: the mockup's rng and stars() recipe, drawn at bind and kept as their lit pixels;
 * still, C13), the band (rd_dp_tile, RGB565 466 x 186 at y RD_DP_BY, repeating, its rows 6-185 laid opaque over the
 * stars; its first six rows are the builder's transparent margin), drawn at the band's offset (the pass's offset less
 * rd_dp_drift: the band drifts 1 px every 10 s, the burn-in rule for a large warm area) with the frame numbers FR
 * 2231 + i riding it (Chakra 13 paintDim at .75, tracked 1.8 px, at the plate's x + 10 on y BY + 40); the time
 * (stencil 132 paint at .95, or paintDim at .75 unset) at (233 + the pass's offset, BY + 135) and WASH ME
 * (rd_dp_wash, A8 130 x 34 in (214, 150, 120)) at (time x + 80, BY + 143), which park with the pass and do not drift;
 * the 60 underside windows (3.4 x 5 at x 56 + 6 i, y 320; lit up to floor((s + 1) x lights): the newest #FFE2A8 with
 * its glow (255, 200, 120) r 10 at .6, the others (255, 190, 110) at .85, unlit #2A0E08); HULL . SECTION 7 . PORT
 * (Chakra 13 paintDim, tracked) at y 82, or the stale marker there; the date (VT323 28 amber; 12 h with its AM/PM
 * suffix) at y 116; the caption DRIFT 0.1 M/S . NO ONE IS STEERING (Chakra 13 paintDim at .85, tracked) at y 372.
 * cadence 40 ms.
 *   Rest motion: the window strip once a second (RD_DP_WINDOWS), the band every 10 s (RD_DP_BAND), the time block
 *   once a minute (RD_DP_TIME). Under reduced motion the same: the drift is a burn-in rule, never ambient motion (as
 *   Bay's star shift, R171), and the windows are the seconds (information).
 *   While in->wake_ms runs (the hull pass, rd/pass.h, R166): the band at rd_pass_at(wake_ms)'s offset (rounded to the
 *   pixel) less the drift, the time and WASH ME at the offset, the windows lit to floor((s + 1) x lights8 / 255), the
 *   label and the date at outCubic(ph(settle, .2, 1)), the caption at outCubic(ph(settle, .5, 1)) x .85. No chime
 *   picture while the pass runs (the time's glow rides the offset outside the pass's planned rect: it shows from the
 *   settled frame). Settled (UINT32_MAX), reduced or glance: offset 0, everything at 1. The opening fade is the
 *   reveal's (its band()), never the face's.
 *   A tap anywhere: FK_TAP_CHIME (the preview only when Chime and the sound are on, C9), as Stasis Clock.
 *   Unset: --:-- in paintDim at .75, no window lit, the date --- -- --- at .55, DRIFTING . CLOCK NOT SET; the band
 *   undrifted. Stale: the shared marker at y 82 in the label's place.
 *   The chime picture (awake, settled, not reduced): the windows light outward from the centre with thrum (window i
 *   unlit shows (255, 190, 110) at .7 clamp(1.4 thrum - |i - 30| / 40, 0, 1)); a glow (255, 200, 140) r 180 over the
 *   time (233, BY + 90) at .1 thrum while the clock is set; a warm flash (255, 200, 120) r 200 on the window strip
 *   (233, BY + BH - 9) at .18 beep.
 *   The glance: the band's two hairlines (rd.g.hull, 1.2 px from x 14 to 452 at y BY + 17 and BY + BH - 16), 12 amber
 *   window ticks (3 x 4 at x 56 + 30 i, y 320, rd.g.amber; none unset), the date (VT323 28 core, rd.g.amber, no
 *   AM/PM) at y 116, the time as the 132 stencil's 1.5 px outline (rd.g.paint) at y BY + 135, the stale marker in
 *   rd.g.amber; all through the orbit. */
extern const fk_face_ops_t RD_FACE_DRIFT;
#define RD_DP_BY 146   /* the band's first row */
#define RD_DP_BH 186   /* its rows (rd_dp_tile's) */
extern const omni_rect_t RD_DP_BAND;      /* the band, {0, 146, 466, 332}: the drift's step redraws it */
extern const omni_rect_t RD_DP_WINDOWS;   /* the 60 windows and the newest one's glow: the second redraws it */
extern const omni_rect_t RD_DP_TIME;      /* the time at rest (every 12/24 h string, the unset dashes; in the band) */
extern const omni_rect_t RD_DP_WASH;      /* WASH ME at rest (inside the band; it does not drift) */
extern const omni_rect_t RD_DP_DATE;      /* the date line (12 h: with its AM/PM suffix) */
extern const omni_rect_t RD_DP_CAPTION;   /* the caption (either text) */
extern const omni_rect_t RD_DP_STALE;     /* the top label, or the stale marker in its place */
/* the band's drift: (sec_of_day / 10) % 466 px (1 px every 10 s, a turn of the tile in 77 min 40 s); 0 unset */
int rd_dp_drift(const fk_clock_t *c);
/* the chime picture's rects: the windows (RD_DP_WINDOWS), the time's glow box (r 180 about (233, BY + 90)) and the
 * flash's box (r 200 about (233, BY + BH - 9)); returns 3 */
unsigned rd_dp_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);

/* ---- The last sync's age and the reserve, as the strip lines show them (wave 2 Task 6; faces.c, shared by Drive
 * Console's strip and Nav Scope's page 4). A caller shows its face's unset caption instead while the clock is unset, so
 * an unset clock never reads "SYNC 0 MIN AGO" (M9). */
/* "SYNC NEVER" (age_s < 0); "SYNC {m} MIN AGO" below 6 000 s (m = age / 60); "SYNC {h} H AGO" below 360 000 s (h = age /
 * 3 600); else "SYNC {d} D AGO" (d = age / 86 400): INT32_MAX reads "SYNC 24855 D AGO" */
void rd_sync_str(int32_t age_s, char out[24]);
/* "RESERVE 72%" (battery 0..100, clamped); "RESERVE --%" for -1 (unknown) or any other negative */
void rd_reserve_str(int8_t battery, char out[16]);

/* ---- Drive Console (`console`, spec section 10.4; wave 2 Task 6; faces-bridge.js:225-398 build(), frame() and
 * sleepFrame()). The engineer's station as edge-lit hardware on the void: the panel layer (rd_dc_panel, A8 406 x 390 at
 * (30, 35): the mockup's build() canvas cut to its ink, each byte an index into RD_DC_RAMP's five runs from black: the
 * console deck's edge and its rivets with their rust, DRIVE 2 in stencil 30 hullLit, the lamp labels, the readout's
 * riveted frame and its ghost 88:88 at amber .06, the SEC and RESERVE gauge faces with RESERVE's red zone, the button's
 * steelDeep ring) and the tapes over it (rd_dc_cuts, RGB565 124 x 40 at (171, 381): DO NOT PRESS on its dark tape and
 * SERIOUSLY on its hazard tape, turned -.06), all laid at bind's ramp. Over them, in the mockup's order: the stale marker
 * in DRIVE 2's place (a black (150, 34) 166 x 36 under it); the hazard collar (r 24-30 about the button's centre (233,
 * 344): 10 px stripes at 45 degrees, rd.hazard on rd.hazardDark) crawling 1 px a second; the six filament lamps HYD, O2,
 * NAV, GRAV, AFT, COMMS (r 6.5 at x 103 + 52 i, y 106: the glow r 17 at .30 on, the lens's radial gradient, the steel
 * ring r 8); the readout's four digits (rd/seg7.h at height 96 from x 101, 165, 251, 315 on row 161) in amber with the
 * mockup's glow (shadowBlur 6, amber .45) and the colon's two squares; the strip (VT323 28 phosphor, centred on 233,
 * middle 293); the SEC needle (G1 (134, 352)) and the RESERVE needle (G2 (332, 352)); the red mushroom (r 21) last.
 * cadence 40 ms.
 *   Rest motion: NAV breathes (.55 + .45 sin over 2.6 s of the clock), AFT's loose filament flickers (.2 in one of 23 of
 *   the clock's 90 ms windows, else .95), COMMS is dead; the colon blinks at 1 Hz (on to s.500, then .15); the SEC
 *   needle ticks with the mockup's 220 ms spring (Hull Plate's table); the collar crawls a stripe pixel each second of
 *   the clock. Under reduced motion NAV (.55) and AFT (.95) hold, the colon is steady, the needle steps each second and
 *   the collar steps once a minute (R171's rule).
 *   The wake (wake_len_ms 900): the lamps pop on left to right (lamp i from 60 + 70 i ms, to 600), the digits are blank
 *   to 150 ms and test 88:88 to 450, and both needles sweep full scale and fall back (sin(pi easeInOut(t / 900)) over
 *   easeOut3). Under reduced motion the runtime shows the wake settled (fk_face_wake: no own wake), as every wave-1
 *   face's.
 *   The strip: in->variant % 3 pages the date (12 h: its AM/PM suffix), RESERVE (rd_reserve_str) and the last sync
 *   (rd_sync_str); a pulse (the button) shows [ REQUEST LOGGED ] to 1 500 ms and [ REQUEST IGNORED ] to 3 200 ms in
 *   rd.lamp; unset, it alternates [ AWAITING TIME SIGNAL ] (variant even) and --- -- --- (odd).
 *   A tap inside the button's circle (r 34 about (233, 344), inclusive) starts the pulse (FK_TAP_PULSE; the variant
 *   stays); anywhere else on the panel turns the strip's page (FK_TAP_NEXT through tap_var: the variant moves on, mod 3;
 *   unset, between 0 and 1). The button is sunk 2 px at the tap, held there to 250 ms and risen by 550 (linearly).
 *   Unset: --:-- on the middle segments in rd.paintDim and a static colon; only NAV breathes (over 1.4 s of mono_ms);
 *   the SEC needle in steel at 0. Stale: the marker replaces DRIVE 2, nothing else changes.
 *   The chime picture (awake, set, not reduced): the digits and the colon breathe from 100 % down to 60 % with the thrum
 *   (1 - .4 thrum), and every lamp lights with the beep (on = max(on, beep)). Its rects: RD_DC_TIME and RD_DC_LAMPS.
 *   The glance: the four digits as 2 px segment outlines in rd.g.amber (unset: the dashes in rd.g.steel), the colon's
 *   6 px squares, the date (Chakra 13 rd.g.paint, centred, middle 293) and the stale marker; all through the orbit. */
extern const fk_face_ops_t RD_FACE_CONSOLE;
#define RD_DC_TAPPED_VARIANT 1   /* still 11: the strip's RESERVE page */
#define RD_DC_PAGES 3
#define RD_DC_BTN_X 233          /* the button's centre and its hit circle's radius (inclusive) */
#define RD_DC_BTN_Y 344
#define RD_DC_BTN_R 34
extern const omni_rect_t RD_DC_LAMPS;    /* the six lamps, their glows r 17 and rings */
extern const omni_rect_t RD_DC_SECG;     /* the SEC gauge's 112 px square about (134, 352): its needle and hub */
extern const omni_rect_t RD_DC_RESG;     /* the RESERVE gauge's 112 px square about (332, 352) */
extern const omni_rect_t RD_DC_TIME;     /* the four digits with their glow (and the colon's) */
extern const omni_rect_t RD_DC_COLON;    /* the colon's two squares and their glow */
extern const omni_rect_t RD_DC_STRIP;    /* the strip: every page, both messages, the unset lines (glow and core) */
extern const omni_rect_t RD_DC_COLLAR;   /* the hazard collar's 64 px square about the button's centre */
extern const omni_rect_t RD_DC_BTN_HIT;  /* the hit circle's box (68 px square): the circle r 34 is the target */
extern const omni_rect_t RD_DC_BTN_BOX;  /* the button's pixels while it sinks */
extern const omni_rect_t RD_DC_STALE;    /* DRIVE 2, and the stale marker in its place */
/* the strip's line in shows (a pulse's message, the unset line, or page in->variant's) and its colour; returns true for
 * a pulse's message (rd.lamp) */
bool rd_dc_strip(const fk_face_in_t *in, char out[32]);
/* lamp i's level (0..1) in shows: i 0 HYD, 1 O2, 2 NAV, 3 GRAV, 4 AFT, 5 COMMS */
float rd_dc_lamp(const fk_face_in_t *in, int i);
/* the SEC needle's place (a fraction of full scale, -.02 to 1.02) and the RESERVE needle's */
float rd_dc_sec(const fk_face_in_t *in);
float rd_dc_res(const fk_face_in_t *in);
/* the four characters the readout shows (the wake's blanks and 88:88, the time, unset "----") */
void rd_dc_digits(const fk_face_in_t *in, char out[5]);
/* the chime picture's rects: RD_DC_TIME and RD_DC_LAMPS; returns 2 */
unsigned rd_dc_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);
/* rd_dc_panel's ramp (rd/release.h's rule; tools/export/rd_layers.json's row names the same stops): five runs from
 * black, to steelHi (0-50: steel and steelDeep lie on it), paint (51-101: paintDim), hullLit (102-152: the rust's
 * hullDeep), amber (153-203: the ghost segments) and lamp (204-255: RESERVE's red zone) */
#define RD_DC_RAMP_STOPS 10
extern const rd_ramp_stop_t RD_DC_RAMP[RD_DC_RAMP_STOPS];

/* ---- Nav Scope (`scope`, spec section 10.5; wave 2 Task 7; faces-bridge.js:403-573 giantSprite(), build(), numerals(),
 * cursor(), frame() and sleepFrame()). The navigator's phosphor scope: the dial (rd_sc_dial, A8 466 x 466: build()'s two
 * steel rims, the painted ticks, the rivets and, inside the glass, the phosphorDim graticule and its scale, with
 * numerals()'s twelve stencil 30 numerals at r 196, each byte a colour and an alpha of RD_SC_DIAL_RUNS laid over what is
 * under it) over the red giant (rd_sc_giant, RGB565 284 x 150, giantSprite()'s top-left, at (116, 272) inside the glass,
 * r 150 about the centre). Inside the glass (RD_SC_GLASS) the face draws, in the mockup's order: the wake's warm-up, the
 * sweep (the seconds: rd_sc_sweep) with its 8-wedge afterglow fan, the four contacts it lights (rd_sc_contact; contact 1
 * tagged TAKEAWAY? in VT323 20), the readout (VT323 20 and 28 phosphor: rd_sc_readout) or the stale marker in its top
 * line's place, the hour and minute cursors (phosphor with open chevron heads and their shadowBlur 8 glow) or, unset,
 * --:-- in paintDim, and the hub. cadence 70 ms (spec section 18: within 66-80).
 *   Rest motion, all inside the glass: the sweep (continuous, or the 4 Hz stepped sweep: rd_sc_step_ms), the fan and
 *   the contacts it lights (each L = e^(-since / 1.8) of the angle since the sweep passed it), and the corona's breath
 *   (rd_sc_corona: the giant at .92 + .08 sin over 12 s of the clock, of mono_ms while unset; in the stepped mode at
 *   each step). The cursors step once a minute (lesson 9). Under reduced motion the sweep steps once a second with no
 *   fan, the contacts hold .6 and the corona .92 (no breathing).
 *   The wake (wake_len_ms 1 000): the glass warms outward from the hub (black from rr = 150 easeOut3(1.25 wk) out to
 *   r 151 and a phosphor ring at rr, .6 (1 - wk)); the sweep makes one decelerating turn onto the clock (its turn less
 *   1 - easeOut3(wk)); the cursors grow out (easeOut3((t - 250) / 650)); the giant fades in over 500 ms and rises 24 px
 *   into place (24 (1 - easeOut3((t - 100) / 900)), rounded to the pixel); the readout fades in from 500 to 800 ms.
 *   Under reduced motion the runtime gives no own wake (fk_face_wake), so no 0.3 s fade either, as Drive Console's.
 *   A tap anywhere (RD_SC_TAP; tap_var) turns the readout's page (in->variant mod RD_SC_PAGES: HDG  HOME / ETA:
 *   EVENTUALLY, RED GIANT / STILL RED, NEAREST TAKEAWAY / 9 742 LY, the date (12 h: its AM/PM suffix) / rd_sync_str)
 *   and starts the pulse (FK_TAP_PULSE, pulse_len_ms 250: the new page fades in over it); unset, the tap does nothing
 *   (the caption shows on every page).
 *   Unset: no cursors, the sweep dark, the contacts at .1, --:-- at the hub, NO TIME FIX / AWAITING SIGNAL. Stale: the
 *   marker replaces the readout's top line, nothing else changes.
 *   The chime picture (awake, set, not reduced; its rect RD_SC_GLASS): the sweep holds at 12, where the hour's chime
 *   began; the graticule pulses outward with the thrum (its phosphorDim pixels lit toward phosphor by .7 thrum inside a
 *   front at thrum x 170 px); the TAKEAWAY? contact lights with each note (L = max(L, beep)).
 *   The glance: the glass rim (r 153, 1.2 px rd.g.steel), the giant's limb as one arc (r 120 about (262, 418), 1.5 px
 *   rd.g.hull, inside the glass), 12, 3, 6 and 9 (stencil 30, rd.g.paint), both cursors as thin lines (3 and 2 px,
 *   rd.g.phosphor) or, unset, --:-- (VT323 28, rd.g.steel), and the stale marker; all through the orbit. */
extern const fk_face_ops_t RD_FACE_SCOPE;
#define RD_SC_TAPPED_VARIANT 2   /* still 11: NEAREST TAKEAWAY / 9 742 LY */
#define RD_SC_PAGES 4
/* the sweep: 0 continuous; 250 the 4 Hz stepped sweep (C8). W2-S1 decides (Task 11); the stills always use it (W2-D11) */
#define RD_SC_STEP_MS 0
uint16_t rd_sc_step_ms(void);          /* RD_SC_STEP_MS unless the console's override is set */
/* 0 or 250 (any other value is ignored); RAM, lost at a reboot. Always compiled in rd_core (pure); only the `rd scope
 * step` command is console-gated (R101, M11), and `rd verify` refuses while it differs from RD_SC_STEP_MS */
void rd_sc_set_step_ms(uint16_t ms);
float rd_sc_sweep_turn(const fk_clock_t *c, uint16_t step_ms);   /* (s + ms' / 1000) / 60, ms' = ms floored to step */
extern const omni_rect_t RD_SC_GLASS;     /* the glass (r 151, the wake's black fill, about the centre): every rest,
                                           * wake, tap and chime change lies inside it */
extern const omni_rect_t RD_SC_READOUT;   /* the readout's two lines (glow planes too) and the stale marker */
extern const omni_rect_t RD_SC_STALE;     /* the top line, and the stale marker in its place */
extern const omni_rect_t RD_SC_TAP;       /* the tap's target: the whole face */
/* whether the sweep shows (a set clock) and its turn from 12 (the stepped mode's floor, the reduced second, the chime's
 * hold at 12, the wake's extra turn) */
bool rd_sc_sweep(const fk_face_in_t *in, float *turn);
float rd_sc_contact(const fk_face_in_t *in, int i);   /* contact i's level L (0..1); 1 is TAKEAWAY? */
float rd_sc_corona(const fk_face_in_t *in);           /* the giant's breath: .84 .. 1 (.92 under reduced motion) */
/* the readout's lines in shows: l1 "" when stale (the marker shows); the unset caption unset */
void rd_sc_readout(const fk_face_in_t *in, char l1[24], char l2[24]);
void rd_sc_hand_turns(const fk_face_in_t *in, float *hour, float *minute);   /* turns from 12, once a minute */
unsigned rd_sc_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_SC_GLASS: returns 1 */
/* rd_sc_dial's runs (the row's a8.runs, rd_hp_run_t's rule): paint, paintDim, steel, steelDeep, steelHi, phosphorDim */
#define RD_SC_DIAL_RUNS_N 6
extern const rd_hp_run_t RD_SC_DIAL_RUNS[RD_SC_DIAL_RUNS_N];

/* ---- Vending Machine (`vending`, spec section 10.9; wave 2 Task 8; faces-quarters.js:183-345 buildBg(), drawFlap(),
 * item(), digits(), frame(), pointer() and sleepFrame()). The front of a crew-deck vending machine: the time is its
 * price display, and every button is sold out but MYSTERY. The front (rd_vm_front, RGB565 340 x 390 at (61, 41):
 * buildBg()'s canvas, the bezel, the buttons and their names, the coin slot and the tray) over the shared rd_grime
 * tinted hull-dark; the face draws the lightbox's glow and legend (or the stale marker in its place), the amber
 * seven-segment time (digits(): segPath() at w 42, h 80, th 10 under the mockup's -.07 skew, each digit's ghost 8 and
 * its lit segments casting the canvas's shadowBlur 14 glow), the VT323 28 ticker, the buttons' lamps, the TODAY sticker
 * (turned -.035 rad: the date, 12 h with its AM/PM suffix, and SERVICED: NEVER) and the tray's flap. cadence 50 ms.
 *   Rest motion: the ticker (RD_VM_TICKER: scrolled 45 px/s from the clock's ms of the day, two copies a message's
 *   width apart: rd_vm_ticker_off), the colon at 1 Hz (on to 600 ms of each second), MYSTERY's blink and the
 *   fluorescent buzz (rd_vm_buzz: the lightbox flickers for 0.1 s every 9 s of the clock); a tap's lamp, message and
 *   flap. Under reduced motion: a steady colon, a still ticker line, MYSTERY steady, no buzz, and the flap opens and
 *   shuts without its swing; the runtime gives no own wake under reduced motion (R179), so no 0.5 s fade either.
 *   The wake (wake_len_ms 1 200): the lightbox stutters on (to 520 ms), the buttons are dark to 250 ms, the display
 *   shows its ghosts to 250 ms and the 88:88 lamp test to 650, the ticker starts at 650, and the flap drops open
 *   empty (.25 sin over 880-1 200 ms).
 *   A tap on a button (RD_VM_BTN: tap_var, FK_TAP_PULSE, pulse_len_ms 3 600): it darkens for 160 ms and the ticker shows
 *   its line for 2.6 s; a sold-out one flashes its lamp for 1.2 s (TEA: SOLD OUT); MYSTERY (set) opens the flap for
 *   3.6 s on the next of six items (ENJOY YOUR SOCK), and unset says PLEASE WAIT. The variant (RAM only): bits 0-2 the
 *   last pressed button, bits 3-5 MYSTERY's next item, bit 6 (RD_VM_VAR_PARITY) flipped by every press, so a press of
 *   the same button again still moves it.
 *   Unset: --:-- on the display (amber, the colon steady), MYSTERY at .25, the ticker AWAITING TIME SIGNAL . PRICES MAY
 *   VARY, the sticker's date --- -- ---. Stale: the marker replaces the lightbox's legend, nothing else changes.
 *   The chime picture (awake, set, not reduced; its rects rd_vm_chime_rects): the display's glow swells to
 *   .55 + .35 thrum and the lightbox to .55 + .3 thrum; MYSTERY blinks with the notes (.3 + .7 beep).
 *   The glance: the four digits as filled 5 px segments in rd.g.amber, the colon, the MYSTERY dot and the stale marker,
 *   all through the orbit. */
extern const fk_face_ops_t RD_FACE_VENDING;
#define RD_VM_BTN_N 6
#define RD_VM_MYSTERY_BTN 5
#define RD_VM_ITEMS 6
#define RD_VM_VAR_PARITY 0x40u
#define RD_VM_TAPPED_VARIANT (RD_VM_MYSTERY_BTN | (1u << 3) | RD_VM_VAR_PARITY)   /* MYSTERY's first item (one sock) */
/* wave 2 Task 9 (the Task 8 review's M5): still 11 plays the press's pulse 1 000 ms in, so a CRC pins a vend frame (the
 * flap open on the sock, its glow and lines, ENJOY YOUR SOCK on the ticker); at rest it would equal still 0 */
#define RD_VM_PULSE_STILL_MS 1000u
extern const omni_rect_t RD_VM_BTN[RD_VM_BTN_N];   /* the six buttons' 84 x 48 boxes: the hit boxes and a press */
extern const omni_rect_t RD_VM_TICKER;    /* the ticker's clip (104, 196) 258 x 26: every line, both planes */
extern const omni_rect_t RD_VM_COLON;     /* the colon's two squares and their glow */
extern const omni_rect_t RD_VM_TIME;      /* the four digits, their ghosts and glow, and the colon */
extern const omni_rect_t RD_VM_MYSTERY;   /* MYSTERY's lamp and ? ? ? */
extern const omni_rect_t RD_VM_FLAP;      /* the tray (162, 388) 142 x 38: the flap, the glow and the item */
extern const omni_rect_t RD_VM_LIGHTBOX;  /* the lightbox (108, 42) 250 x 34: its glow and legend */
extern const omni_rect_t RD_VM_STICKER;   /* the turned sticker's box (12 h's wider one) */
extern const omni_rect_t RD_VM_STALE;     /* the lightbox's glow: its legend, or the stale marker in its place */
extern const omni_rect_t RD_VM_PANEL;     /* the ticker and the six buttons: what a tap redraws but the flap */
/* the 0.1 s fluorescent buzz: a set clock's local second of the day a multiple of 9 and its ms under 100 (local and
 * unix seconds agree mod 9: every zone offset is a multiple of 900 s) */
bool rd_vm_buzz(const fk_clock_t *c);
float rd_vm_lit(const fk_face_in_t *in);          /* the lightbox's level: the wake's stutter, the buzz, else 1 */
void rd_vm_digits(const fk_face_in_t *in, char out[5]);   /* the display's four: the wake's blanks and 88:88, the time */
bool rd_vm_colon(const fk_face_in_t *in);         /* the colon lit */
float rd_vm_lamp(const fk_face_in_t *in, int i);  /* button i's lamp alpha (0..1) */
int rd_vm_item(const fk_face_in_t *in);           /* the item the tray shows (0 sock .. 5 battery), -1 none */
float rd_vm_flap_open(const fk_face_in_t *in);    /* the flap's opening (0 shut .. 1) */
/* the ticker's line: what it shows and its alpha (255: a tap's message; 204 the rest); out "" for the scroll */
#define RD_VM_TK_NONE 0
#define RD_VM_TK_SCROLL 1
#define RD_VM_TK_STILL 2
#define RD_VM_TK_MSG 3
int rd_vm_ticker(const fk_face_in_t *in, char out[48], uint8_t *alpha);
float rd_vm_ticker_off(const fk_face_in_t *in);   /* the scroll's offset, px in [0, rd_vm_ticker_mw) */
float rd_vm_ticker_mw(bool set);                  /* the scroll message's width (a copy's period), px */
void rd_vm_sticker_date(const fk_face_in_t *in, char out[24]);   /* the sticker's date (12 h: its AM/PM suffix) */
unsigned rd_vm_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_VM_TIME, _LIGHTBOX, _MYSTERY: returns 3 */

/* ---- the Bowl (`bowl`, spec section 10.11; wave 2 Task 9; faces-quarters.js:508-673 buildBg(), kibble(), kpos(),
 * fishWhole(), fishBones(), paw(), frame(), pointer() and sleepFrame()). A chipped hull-red enamel cat bowl seen from
 * above: the bowl (rd_cb_bowl, RGB565 432 x 432 at (17, 17): buildBg()'s lip with its painted numerals, the steel wall
 * and the polished floor engraved MINE, cut to its disc) on black; sixty kibbles round the wall, one eaten a minute
 * (60 - m left, starting where the minute hand points; the eaten ones crumbs), the kipper (the hour: hull-red with its
 * paint stripe) and the fish skeleton (the minute), both polygons with their shadows, and the steel hub. cadence 40 ms.
 *   Rest motion: the paw (a generic tabby forearm in a white sock, polygons) reaches over the kibble ring at (m + .5) /
 *   60 of a turn and bats the next kibble onto the floor, 1.6 s a run (rd_cb_paw_ms: a tap's pulse; while the wake is
 *   fresh at fresh_ms 2 500 + 20 000 k; after it whenever the clock's second of the day is a multiple of 20; plan W2-D6),
 *   inside rd_cb_paw_rect(m) and RD_CB_FLOOR; the hands step and one kibble is eaten once a minute (its rect and the
 *   hands'). Under reduced motion no paw runs; the runtime gives no own wake under reduced motion (R179).
 *   The wake (wake_len_ms 1 000): the remaining kibbles rain in round the ring (scale 1.9 to 1 by easeOutBack, from 26
 *   px above, fading in, staggered over 560 ms) and the fish swing to the time (easeOutBack over 900 ms): RD_CB_BOWL.
 *   A tap on the bowl (RD_CB_HIT; tap_var, FK_TAP_PULSE, pulse_len_ms 1 600, guarded 1 600 ms): the paw, the variant
 *   left (the face has none: rd_still_variant 0; its tapped still is a paw frame, rd_still_pulse_ms 900); under reduced
 *   motion a tap does nothing (FK_TAP_NONE). Unset the pulse plays and shows nothing.
 *   Unset: the bowl empty (no kibbles, fish or hub), --:-- (stencil 64) and BOWL EMPTY (Chakra 13) in paintDim on the
 *   floor. Stale: the shared marker at the top of the floor (staleMark(233, 167)); nothing else changes.
 *   The chime picture (awake, set, not reduced; its rects rd_cb_chime_rects): the floor's sheen brightens with the
 *   thrum (steelHi through the soft glow r 56 at (197, 189), .45 thrum); the next kibble hops 6 px with the beep.
 *   The glance: the lip's hairline (rd.g.hull 1.5 px at r 214), 12, 3, 6 and 9 (stencil 30, rd.g.paint), the kipper's
 *   outline (rd.g.hull 1.4 px) and the skeleton (rd.g.paint 1.3 px); unset, the --:-- as outlines; stale, the marker in
 *   rd.g.amber; all through the orbit.
 * Angles: radians clockwise from 12, about the bowl's centre (233, 233). */
extern const fk_face_ops_t RD_FACE_BOWL;
#define RD_CB_PAW_STILL_MS 900u              /* still 11: the paw at full reach, claws out, the kibble in flight */
extern const omni_rect_t RD_CB_BOWL;   /* the bowl's box (17, 17) 432 x 432: the wake's region */
extern const omni_rect_t RD_CB_HIT;    /* the tap: the bowl's box */
extern const omni_rect_t RD_CB_FLOOR;  /* the floor (r 118): where the batted kibble lands */
extern const omni_rect_t RD_CB_STALE;  /* the stale marker at the top of the floor */
extern const omni_rect_t RD_CB_RING;   /* every kibble at rest, its crumb and its hop */
extern const omni_rect_t RD_CB_SHEEN;  /* the chime's sheen glow */
extern const omni_rect_t RD_CB_HUB;    /* the hub */
unsigned rd_cb_kibbles_left(const fk_clock_t *c);   /* 60 - m; 0 when unset (the bowl is empty) */
/* the paw's ms into its 1.6 s run, or -1: a tap's pulse first; then, while in->fresh, fresh_ms in [2500 + 20000 k,
 * + 1600); after the fresh minute, sec_of_day % 20 == 0 starts one (W2-D6). None unset, at m 59 (the last kibble
 * stays: the mockup's m < 59), under reduced motion or in the glance */
int rd_cb_paw_ms(const fk_face_in_t *in);
/* the paw's box for minute m: every pixel its run can touch at that minute (it turns with the minute: the brief's
 * "about 170 x 190 at the rim" is one minute's paw near its full reach, not a fixed rect) */
void rd_cb_paw_rect(unsigned m, omni_rect_t *out);
void rd_cb_hand_angles(const fk_face_in_t *in, float *hour, float *minute);   /* once a minute */
unsigned rd_cb_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_CB_SHEEN, RD_CB_RING: returns 2 */

/* ---- Mop Bot (`mopbot`, spec section 10.13; wave 2 Task 10; faces-quarters.js:826-948). A grimy tread-plate deck (the
 * ruling C5: rd_mb_tread, one RGB565 44 x 44 period of the plate, tiled by x mod 44 and y mod 44 under every clip, the
 * shared rd_grime tinted over it, the ticks drawn) with the time in worn hazard stencil 132 (rd_f_s132 tinted, the
 * mockup's advances tightened to .9), the date . DECK 14 below it; the track (r 192-222), the dashed hazard ring (r 190)
 * crawling 1 px a second (once a minute under reduced motion, R171), the WET FLOOR sign at 12 and 30 at the bottom; the
 * clean wet arc from 12 to the robot with its sheen, and the boot prints across the stretch mopped 2.2 minutes ago (from
 * m 4, stepped once a minute). The robot (ours, polygons at any heading: a squat hazard bucket on treads, one eye, a whip
 * antenna and a trailing mop; no neck, no claw) is the minute hand, rd_mb_robot_turn of a turn on r 207.
 *   Rest motion (mono_ms): the robot's scrub wobble (+-.012 rad), mop swish, cleats and eye blink, about 100 squared
 *   (rd_mb_robot_box holds it at the hop's largest), and the sheen's tail end (40 x 40, rd_mb_sheen_box) as the robot
 *   creeps (a step a second); the ring's four rim bands each second (rd_mb_ring_bands); the prints and the text once a
 *   minute. Under reduced motion, unset and in the glance the robot holds still.
 *   The wake (wake_len_ms 1 200): the robot races from the sign to the minute (easeOutBack), mopping; prints after it.
 *   A tap (tap_var, FK_TAP_PULSE, pulse_len_ms 900, guarded 800 ms) within RD_MB_HIT_R of the robot's centre at the
 *   input's clock, to its nearest pixel (I3): a startled hop and spin with an amber eye; under reduced motion only the
 *   eye flashes. tap() returns FK_TAP_NONE everywhere (no clock, no robot). Unset: the robot parked asleep by the sign
 *   (its eye off), Z Z, a grimy track (no arc, no prints), --:-- and AWAITING TIME SIGNAL in paintDim. Stale: the
 *   marker above the time (RD_MB_STALE).
 *   The chime picture (awake, set, not reduced; rd_mb_chime_rects): the eye swells with the thrum, the antenna's tip
 *   blinks with the beep, the robot bumps the sign on the click.
 *   The glance: the outlined time (rd_f_s132o) in rd.g.hazard and a robot dot by the minute; the stale marker. */
extern const fk_face_ops_t RD_FACE_MOPBOT;
#define RD_MB_HIT_R 36             /* a 72 px target (C17), inclusive at r (M10) */
#define RD_MB_HOP_STILL_MS 300u    /* still 11: the robot mid-hop, a tap 300 ms ago (the mockup's tap-startle shot) */
float rd_mb_robot_turn(const fk_clock_t *c);   /* (m + s / 60) / 60 of a turn from 12; unset .11 rad (parked) */
void rd_mb_robot_xy(const fk_clock_t *c, float *x, float *y);   /* on the track's centre line, r 207 */
extern const omni_rect_t RD_MB_STALE;   /* the stale marker above the time */
extern const omni_rect_t RD_MB_TIME;    /* the hero time (every 12/24 h string, --:--) */
extern const omni_rect_t RD_MB_DATE;    /* the date line (AWAITING TIME SIGNAL unset) */
extern const omni_rect_t RD_MB_SIGN;    /* the WET FLOOR sign, tipped by the chime's click */
extern const omni_rect_t RD_MB_TRACK;   /* every pixel the robot can reach on its line (the chime's eye and antenna) */
/* every pixel the robot settled at in's clock can touch: its wobble, any spin and the hop's largest scale */
void rd_mb_robot_box(const fk_face_in_t *in, omni_rect_t *out);
/* the sheen's tail end (its butt edge, 40 x 40) at in's clock, its wobble included */
void rd_mb_sheen_box(const fk_face_in_t *in, omni_rect_t *out);
void rd_mb_ring_bands(omni_rect_t out[4]);       /* the hazard ring's four rim bands (what its crawl redraws) */
void rd_mb_prints_box(unsigned m, omni_rect_t *out);   /* the boot prints of minute m */
unsigned rd_mb_chime_rects(omni_rect_t out[RD_ST_CHIME_MAX]);   /* RD_MB_SIGN, RD_MB_TRACK: returns 2 */
/* the deck at (x, y) (0..465): the tread tile alone, and with the tinted grime (what any uncovered pixel shows) */
uint16_t rd_mb_tread_px(const void *ctx, int x, int y);
uint16_t rd_mb_deck_px(const void *ctx, int x, int y);
