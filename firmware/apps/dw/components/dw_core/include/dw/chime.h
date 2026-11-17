#pragma once
/* The hourly chime's scheduler and envelope (spec §13, D10, D11; preflight B3, B4, D7; R136 item 8). Pure: the clock
 * is passed in, the local time comes from localtime_r under the caller's TZ (as omni/timefmt.c), and nothing here
 * reads settings or starts a timer. The service (Task 16) owns the sound/volume mirror, the one-shot timer and the
 * state; it calls fk_chime_due at each wake and re-arms with fk_chime_arm_ms. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/chime.h"   /* the scheduler (face_kit, rd wave 1 Task 1) */

#define DW_CHIME_PING_GAP_MS 360
#define DW_CHIME_MAX_MS 7600         /* the whole sound, the last ping's own length included (D7) */
#define DW_CHIME_PING_MAX_MS 200     /* with the 7.6 s bound: the chime itself <= 3 440 ms (D7) */
#define DW_CHIME_CHIME_MAX_MS (DW_CHIME_MAX_MS - 11 * DW_CHIME_PING_GAP_MS - DW_CHIME_PING_MAX_MS)   /* = 3 440 */

/* The bound is enforced, not assumed: dw_chime_plan and dw_chime_env clamp chime_ms to DW_CHIME_CHIME_MAX_MS, so the
 * last ping ends by DW_CHIME_MAX_MS and every at_ms fits a uint16. */
typedef char dw_chime_chk_bound[(DW_CHIME_CHIME_MAX_MS == 3440 && DW_CHIME_CHIME_MAX_MS + 11 * DW_CHIME_PING_GAP_MS
                                 + DW_CHIME_PING_MAX_MS == DW_CHIME_MAX_MS && DW_CHIME_MAX_MS < 65535) ? 1 : -1];

typedef struct { uint8_t what; uint16_t at_ms; } dw_chime_step_t;   /* what 0: the chime, 1: a ping */
/* chime at 0, then `pings` (clamped to 12) pings from chime_ms, DW_CHIME_PING_GAP_MS apart; the step count. */
uint8_t dw_chime_plan(uint8_t pings, uint16_t chime_ms, dw_chime_step_t out[13]);

/* The chime service's play (rd wave 1 Task 2, PR21: face_kit's fk_chime_svc calls the theme's play with the verdict
 * it played): a PLAY verdict's steps, dw_chime_plan(v->pings, chime_ms, ..) (the chime's length is the asset's, which
 * the app reads); any other verdict, or a NULL one, gives none. At most cap steps; the count. vol_pct is the gain the
 * app plays them at (sfx_play_seq's), which the steps do not carry. */
unsigned dw_chime_play_plan(const fk_chime_verdict_t *v, uint8_t vol_pct, uint16_t chime_ms, dw_chime_step_t *steps,
                            unsigned cap);

/* The picture's envelope, 0..1, follows the chime that plays: dw_chime_env_bind reads the chime asset (s16le mono
 * 16 kHz) once and keeps its loudness as one byte per DW_CHIME_WIN_MS window (rms, the loudest window 255; at most
 * DW_CHIME_WIN_MAX windows, the first DW_CHIME_CHIME_MAX_MS). Bind before the first play (the app does so when it reads
 * the asset's length); until then the chime's part is 0. dw_chime_env: the table at t_ms (linear between windows), 0
 * after the chime; from chime_ms on a low glow held to the last ping, a glint on each ping (decaying over
 * DW_CHIME_PING_MAX_MS), and a 0.5 s fade after the last ping, then 0. */
#define DW_CHIME_WIN_MS 20
#define DW_CHIME_WIN_MAX (DW_CHIME_CHIME_MAX_MS / DW_CHIME_WIN_MS + 1)
void dw_chime_env_bind(const uint8_t *pcm, size_t bytes);   /* NULL or 0 bytes: an empty table */
float dw_chime_env(uint32_t t_ms, uint8_t pings, uint16_t chime_ms);
