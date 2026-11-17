#pragma once
/* A face as pure code: what the app hands a face each frame (fk_face_in_t) and what a face gives back (fk_face_ops_t),
 * the runtime's small rules, and the still frames' CRC lookup. Pure: no esp_*, freertos or LVGL; no allocation in a
 * draw path. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/clock.h"
#include "omni/geom.h"

struct fk_reveal_ops;   /* a full-screen reveal's ops (fk/reveal.h, from Task 2); opaque here */

/* Every field is a theme's to read or leave: a theme's runtime fills it, a still leaves it 0 unless the theme's pinned
 * inputs set it. The comments name the first theme's uses as examples. */
typedef struct fk_face_in {
    fk_clock_t clk; bool h24;
    uint32_t wake_ms;          /* since the wake's entry began; UINT32_MAX settled */
    float chime;               /* the chime picture's envelope now (the first theme's one channel); 0 silent */
    int8_t odx, ody;           /* the glance orbit (omni_orbit_offset) */
    uint8_t page; bool alt;    /* a pushed screen's page; FK_TAP_ALT's state (held alt_len_ms, or a toggle) */
    int8_t battery; bool charging;   /* -1 unknown (shell_status) */
    int64_t sun_next_s; uint8_t sun_kind;   /* the next sunrise (1) or sunset (2); 0 none */
    bool glance;               /* the frame is the glance's (the view sets it), so dirty() can report only what the
                                * glance draws */
    uint8_t sun_h, sun_m;      /* the next sun event's local time (the app's zone); read only when sun_kind is not 0 */
    bool pulsing;              /* a FK_TAP_PULSE is playing: pulse_ms since the tap (for the face's pulse_len_ms) */
    uint16_t pulse_ms;
    bool reduced;              /* reduced motion */
    uint32_t mono_ms;          /* a free-running ms count (esp_timer, wrapping), for motion that reads no clock. 0 in
                                * every still */
    /* a revealing wake came fresh_ms ago (for 60 s, whatever the motion setting), so what a wake shows as information
     * shows under reduced motion too. false in every still */
    bool fresh;
    uint16_t fresh_ms;
    /* the reveal is drawing over the interior (the view sets it while its frames run): a face whose reveal leaves
     * part of itself on screen (a keep kind's leaves) leaves that part out, since the reveal draws it. false during a
     * reduced cross-fade, so that part fades in with the face; false in every still and at rest */
    bool reveal_live;
    /* the local minutes (0..1439, the app's zone) of today's sunrise and sunset for the home city (ops->uses_sun only),
     * -1 none (polar day or night, no city, an unset clock; or set <= rise, as an input zeroed by memset has) */
    int16_t sun_rise_m, sun_set_m;
    /* the face's own variant, the runtime's per-face counter (RAM only: it survives the view and the sleep, and is 0
     * again after a reboot); a FK_TAP_CYCLE moves it on (fk_variant_next), a tap_var hook may set it. 0 in every still.
     * uint16_t (rd wave 1 Task 1; the first theme's was uint8_t), still wrapped modulo 255 by FK_TAP_CYCLE */
    uint16_t variant;
    /* appended (rd wave 1 Task 1; 0 in the first theme): the chime picture's three channels (the rd chime envelope's) */
    float thrum, beep, click;
    /* appended (rd wave 1 Task 1): the last completed sleep in s, -1 none (0 in the first theme) */
    int32_t slept_s;
    /* appended (rd wave 2 Task 6, RW5's deferred field): the seconds since the last good sync (fk_sync_age of the
     * shell's sync.last_ok), -1 never; fk_face's fill_in sets it for every theme, and the first theme's faces never
     * read it */
    int32_t sync_age_s;
} fk_face_in_t;

typedef const void *(*fk_asset_fn)(const char *name, size_t *size);   /* device: assets_raw; host: the files */
/* A tap's action. FK_TAP_SCREEN: push the theme's screen (the first theme's numeral key); FK_TAP_ALT: in->alt;
 * FK_TAP_PULSE: a pulse (in->pulsing); FK_TAP_NEXT, FK_TAP_BACK: a pushed screen's page turn and its back button;
 * FK_TAP_CYCLE: the face's variant moves on (in->variant, fk_variant_next) and its pulse starts. FK_TAP_CHIME
 * (appended, rd wave 1 Task 1): the runtime asks the theme's hook for a chime preview. */
typedef enum {
    FK_TAP_NONE, FK_TAP_SCREEN, FK_TAP_ALT, FK_TAP_PULSE, FK_TAP_NEXT, FK_TAP_BACK, FK_TAP_CYCLE, FK_TAP_CHIME
} fk_tap_t;

/* Every draw call takes fb as the pixel at (clip.x0, clip.y0) with row stride `stride` px (fk/compose.h). A pixel's
 * value depends only on the pixel and the inputs, never on the clip: a frame drawn in clips is the frame drawn whole.
 * dirty(a, b) returns the rects (at most 8) outside which the frames for a and b are equal: draw's frames, or the
 * glance's when b->glance (the view redraws only those, awake or in the glance). */
typedef struct fk_face_ops {
    const char *key;           /* `face <key>` */
    size_t ctx_bytes;          /* the face's assets view and its maps (PSRAM on the device) */
    uint16_t cadence_ms;       /* motion at rest: 0 none, 40 = 25 fps, 50 = 20 fps */
    bool (*bind)(void *ctx, fk_asset_fn get);
    void (*draw)(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride);
    void (*glance)(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride);
    uint8_t (*dirty)(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8]);
    fk_tap_t (*tap)(const void *ctx, int16_t x, int16_t y);  /* NULL: none; targets >= 44 px */
    /* the face opens with a full-screen reveal; its interior under the reveal is draw() at the reveal's elapsed time
     * as wake_ms */
    bool reveal;
    /* a face with a wake of its own (no reveal): after a revealing wake (not a swipe, the picker, a re-lift, Instant or
     * reduced motion: omni_reveal_for's NONE), wake_ms runs from 0 for wake_len_ms, then it is UINT32_MAX (settled).
     * 0: none. pulse_len_ms: how long a FK_TAP_PULSE plays (in->pulsing, pulse_ms). */
    uint16_t wake_len_ms, pulse_len_ms;
    /* pages: a pushed screen's pages (in->page 0 .. pages - 1; 0 means one, every face): the stills are checked on
     * each. alt_len_ms: a FK_TAP_ALT holds in->alt this long, then it clears (a second tap clears it sooner); 0: a
     * toggle that stays. */
    uint8_t pages;
    uint16_t alt_len_ms;
    /* the reveal's asset (NULL: the reveal's default) and its argument (the first theme's door kind; NULL: the
     * reveal's default), passed to the reveal as they are */
    const char *reveal_asset;
    const void *reveal_arg;
    /* a FK_TAP_PULSE is ignored while a pulse is under pulse_guard_ms in (fk_pulse_retap). 0: every tap restarts the
     * pulse */
    uint16_t pulse_guard_ms;
    /* the face's rest motion is full-screen, so the view sends it through the direct path while the face is awake and
     * not reduced (its own frames at the panel's rate; fk_face_in_advance moves each frame's input on); the canvas at
     * cadence_ms whenever the claim is refused or cut. Never with .reveal. */
    bool direct;
    /* the face reads today's sunrise and sunset (in->sun_rise_m, sun_set_m), so the runtime works them out once a
     * minute; every other face gets -1 (none) and skips the work. The next sun event is every face's. */
    bool uses_sun;
    /* appended (rd wave 1 Task 1; NULL or 0 in the first theme): */
    const struct fk_reveal_ops *reveal_ops;   /* the reveal (Task 2) */
    /* used instead of tap when set: it may change *variant (RAM only) and returns the action as tap does. in is the
     * face's input at the tap (rd wave 1 Task 13: a face whose tapped text is hidden while the clock is stale or unset
     * leaves its variant as it is). The runtime (fk_face_gesture) always passes one; a NULL in, which only a caller
     * outside the runtime could pass, is taken as a set, fresh clock (nothing hidden) */
    fk_tap_t (*tap_var)(const void *ctx, const fk_face_in_t *in, int16_t x, int16_t y, uint16_t *variant);
    uint8_t stills_extra;                     /* still rows past the theme's base stills (a tapped state) */
} fk_face_ops_t;
/* The input of a direct frame dt_ms after `in` was filled (the view's rest-motion worker; the shell refills it each
 * cadence): mono_ms moves on by dt (wrapping); a running wake (wake_ms not UINT32_MAX) and pulse
 * (pulsing) move on by dt and end at ops->wake_len_ms (wake_ms UINT32_MAX) and ops->pulse_len_ms (pulsing false,
 * pulse_ms 0), as the runtime's own clock ends them. Nothing else changes: the clock stays the shell's (a minute's text
 * comes with its next refill, at most one cadence late). */
void fk_face_in_advance(const struct fk_face_ops *ops, fk_face_in_t *in, uint32_t dt_ms);
/* in->sync_age_s from the shell's clock (now_s) and its last good sync (last_ok_s, unix s; 0 never): -1 when last_ok_s
 * is 0, 0 when now_s is before it (a clock stepped back, or unset), else now_s - last_ok_s capped at INT32_MAX */
int32_t fk_sync_age(int64_t now_s, int64_t last_ok_s);
/* A FK_TAP_CYCLE's next variant: (v + 1) % 255. 255 is a multiple of 3 (a three-palette face's), so the wrap never
 * repeats a palette, as a uint8_t's 255 -> 0 would at the 256th tap. */
uint16_t fk_variant_next(uint16_t v);
/* Whether a FK_TAP_PULSE restarts the pulse, given the one playing (pulsing, pulse_ms since its tap): not pulsing, or
 * pulse_ms >= ops->pulse_guard_ms (the runtime's tap rule). */
bool fk_pulse_retap(const fk_face_ops_t *ops, bool pulsing, uint32_t pulse_ms);
/* The period of the runtime's face timer for ops: its cadence_ms while awake, whatever runs; a
 * face of no cadence (0) with a wake or a pulse of its own gets FK_FACE_TIMER_WAKE_MS only while one runs (running:
 * in->wake_ms or a pulse under its length), so its frames show; 0: no timer (at rest, or a face with neither). The
 * timer is created when this turns non-zero, deleted when it turns 0, and at the sleep and the destroy. */
#define FK_FACE_TIMER_WAKE_MS 40
uint16_t fk_face_timer_ms(const fk_face_ops_t *ops, bool running);

/* ---- The still frames' CRCs: a theme's table of (face, still, CRC) rows, generated by its host tool and compared by
 * its host tests and its device's verify command. */
typedef struct { const char *face; uint8_t still; uint32_t crc; } fk_verify_t;   /* a CRC table's rows */
/* A CRC table and its row count. A generic build has its theme's table alone; a private build's overlay table
 * supplements it, with rows only for the faces whose bind reads a swap asset: a still is looked up there first, then in
 * the generic table. */
typedef struct { const fk_verify_t *rows; unsigned n; } fk_verify_tab_t;
/* The applicable row for (face, still): the first matching row of the first of tabs[0 .. ntabs) that has one; NULL if
 * none has. *dups (if not NULL): that table's number of rows for the still (exactly 1 in a sound table; 0 with NULL). */
const fk_verify_t *fk_verify_lookup(const fk_verify_tab_t *tabs, unsigned ntabs, const char *face, unsigned still,
                                    unsigned *dups);
/* zlib's CRC-32 (esp_rom_crc32_le's), of n bytes, chained from crc (0 to start). */
uint32_t fk_crc32(uint32_t crc, const void *p, size_t n);
/* The whole 466 x 466 frame of `ops` for the input `in` into fb (stride 466): draw, or glance when glance is set (a
 * theme's still frames: it fills `in` from its pinned inputs). */
void fk_still_render(const fk_face_ops_t *ops, const void *ctx, const fk_face_in_t *in, bool glance, uint16_t *fb);
