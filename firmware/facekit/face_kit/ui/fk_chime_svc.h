#pragma once
/* The hourly chime on the device (from the first theme's wave 1 Task 16, its spec section 13; face_kit since rd wave 1
 * Task 2): face_kit's scheduler (fk/chime.h) behind one esp_timer one-shot, the theme's sound and picture by callback
 * (fk_chime_theme_t), and the picture's channels the faces breathe with (fk_chime_svc_fill). */
#include <stdbool.h>
#include <stdint.h>
#include "fk/chime.h"
#include "fk/face.h"
#include "sdkconfig.h"

/* The theme's plan. Every callback but ctx may run on the esp_timer task (play) or any task (env): neither logs or
 * blocks. */
typedef struct {
    void (*ctx)(fk_chime_ctx_t *c);   /* the settings snapshot: chime_on, quiet_from_h, quiet_to_h (the rest is the
                                       * service's) */
    /* play the verdict's sound at vol_pct (the verdict's gain); true if it sounded (the picture starts), false if it
     * did not (an asset missing, the sound refused: counted, `chime` shows it) */
    bool (*play)(const fk_chime_verdict_t *v, uint8_t vol_pct);
    /* the picture's channels (in->chime, thrum, beep, click) at ms after the start; at_play is the verdict the service
     * played */
    void (*env)(uint32_t ms, const fk_chime_verdict_t *at_play, fk_face_in_t *in);
    uint32_t len_ms;                  /* the whole sound: the picture is read until len_ms + 1 s after the start */
} fk_chime_theme_t;
/* Once, before the first sync (the theme's after_store, before its faces register); t is kept. */
void fk_chime_svc_init(const fk_chime_theme_t *t);
/* Boot task (after_store) and then the event loop (the theme's Chime and quiet rows): the timer exists while the
 * theme's chime_on is on, and is deleted when it is off (no chime timer and no chime work). The state is kept. */
void fk_chime_svc_sync(void);
int64_t fk_chime_started_us(void);       /* any task: esp_timer us when the last chime began; 0 none */
void fk_chime_svc_fill(fk_face_in_t *in);   /* any task: the channels now (the theme's env); zeros when silent */
/* Every build (PR4): the theme's sound now through its play (a preview: the theme's tap, its console's `chime play`),
 * as the hour plays it (the hour's pings and quiet gain); false if the service is not started or it did not sound. */
bool fk_chime_svc_play_now(void);
#if CONFIG_OMNI_DEBUG_CONSOLE
void fk_chime_svc_print(const char *tag);   /* the theme's `chime` command; tag starts every line */
bool fk_chime_svc_at(uint32_t n_s);         /* `chime at +N` (N s, 1..FK_CHIME_AT_MAX_S) or `at off` (0) */
#endif
