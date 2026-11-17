#pragma once
/* The Mining Ship chime's picture and plan (rd wave 1 Task 8; spec section 11, "Ship Clears Its Throat"). Pure: no
 * esp_*, freertos or LVGL, no allocation. chime_env.c is the one rd_core file that calls libm (expf, RW8): it runs
 * only while a chime plays, never in a still frame. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/chime.h"

/* spec section 11's picture envelope, host-tested; every channel 0..1, all 0 outside [0, 2400) ms */
#define RD_CHIME_MS 2400
#define RD_CHIME_PCM "rd_chime.pcm"     /* the default (audible) voicing: the one asset the hour plays */
#define RD_CHIME_R_PCM "rd_chime_r.pcm" /* the recipe as written: `rd chime play recipe` only (the owner's check) */
typedef struct { float thrum, bip, bop, click, beep; } rd_chime_env_t;
/* thrum (ms/550)^2 below 550, 1 to 1500, then (1 - ph(ms, 1500, 2350))^2; bip e^-(ms-750)/45 in [750, 860); bop
 * e^-(ms-950)/60 in [950, 1100); click 1 - (ms-2200)/60 in [2200, 2260); beep max(bip, bop). NULL e: nothing. */
void rd_chime_env(uint32_t ms, rd_chime_env_t *e);
/* rd's plan (PR33, pure): one asset, pings ignored, the gain the service passes (100, or 40 in the quiet window);
 * false (nothing to play) for any verdict but PLAY, or a NULL argument */
bool rd_chime_plan(const fk_chime_verdict_t *v, uint8_t vol_pct, const char **asset, uint8_t *gain);
