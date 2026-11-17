#pragma once
/* Sound (spec §3, §10; Stage 3 Task 13): short PCM effects from the assets partition (s16le mono 16 kHz, tools/gen_sfx.py
 * synthesises them) through the ES8311 on its own TX-only I2S channel (R71/C9: never bsp_audio_init, whose duplex
 * channel stays enabled for good and holds the I2S PM lock).
 *
 * Lazy (C16): nothing exists until the first play that is not dropped. That play creates the queue (2 deep, the newest
 * wins), an esp_pm NO_LIGHT_SLEEP lock and the sfx task (priority 3, stack in PSRAM); the task then creates the I2S
 * channel (3 x 256-frame DMA buffers) and the codec. Plays are dropped, and nothing is created, while the sound is off,
 * at volume 0 or while blocked (provisioning: no I2S RAM under the QR; a block also deletes the channel and the codec).
 *
 * Playing: the task holds its PM lock and the amp (GPIO 46) on only while it writes, in 512-sample chunks read straight
 * from the flash mapping; a new play cuts the current effect (a 5 ms fade), a stop ends it the same way. At the end it
 * pushes the DMA out with silence, then the amp goes low and the PM lock is released. After 2 s idle the codec is closed,
 * which also disables the I2S channel, so the I2S driver's own PM lock is released and light sleep can run (spec §9). */
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

void sfx_config(bool on, uint8_t volume);   /* boot (watch_main), then the event loop only (settings, R27); off: stop, never
                                             * init the codec */
/* R73: the player is generic over PCM assets, played by name ("sfx_latch.pcm"); a theme maps its own effects to names
 * (the Omni maps its four in its app). Any task, never blocks: posts to the sfx task; a new effect cuts the current
 * one. False if the asset is missing or not PCM (a .pcm of whole samples); true otherwise, also when the play is
 * dropped (off, volume 0, blocked: counted in sfx_debug_stat). */
bool sfx_play_asset(const char *name);
void sfx_stop(void);                        /* any task; also called on the sleep and glance paths (watch_main) */
void sfx_set_blocked(bool blocked);         /* true while provisioning: plays are dropped (no I2S RAM under the QR) */

/* Play sequences (dw wave 1 Task 16, spec section 13: the dw chime; R136 item 8, B3). The Omni calls neither, so
 * its sounds behave as before: the sequence player is reached only through sfx_play_seq, so a build that never calls
 * it links none of it (function sections and the linker's gc). */
typedef struct { const char *name; uint16_t at_ms; } sfx_step_t;
#define SFX_SEQ_MAX 16
/* Any task, never blocks. Plays the steps at their offsets at gain_pct of the set volume. keep: sfx_stop() (the
 * sleep and glance paths) leaves it playing, and sfx_play_asset is dropped (counted) until it ends. Sound off,
 * volume 0 or a block still cut it, and a new sequence replaces it. The amp and the PM lock are held for the whole
 * sequence (no pop per ping; C10), and at the end the codec returns to the *current* volume. False if a name is
 * missing or n is 0 or above SFX_SEQ_MAX.
 * The steps play one after another on one stream: silence until a step's offset, and a step that starts before the
 * last one ended cuts it (no mixing). gain_pct above 100 is 100; 0 is dropped (counted), as sound off is. */
bool sfx_play_seq(const sfx_step_t *steps, uint8_t n, uint8_t gain_pct, bool keep);
/* the mirror sfx_config keeps (written on the event loop), readable from any task (R136 item 8, B3) */
void sfx_config_get(bool *on, uint8_t *volume);
/* Appended (the dw final review, M2): true once sfx_config has run since boot. Before it, sfx_config_get reads the
 * defaults (off, volume 0), not the wearer's setting; a reader that marks a decision on them (the dw chime) waits.
 * An acquire load: true means the mirror's values are the configured ones. */
bool sfx_config_known(void);

/* The console's `sfx stat`. Times are esp_timer us (0: never). */
typedef struct {
    bool on, blocked;
    uint8_t volume;
    bool task, queue, pm_lock, channel, codec;   /* created */
    bool task_failed;                            /* the task's creation failed (latched until a reboot) */
    bool open;                                   /* the codec is open: the I2S channel enabled (its PM lock held) */
    bool playing, pa, pm_held;                   /* pa: GPIO 46's output level */
    uint32_t plays, cuts, stops, dropped, rejected, failures;
    int64_t last_write_us;                       /* the last audio chunk's write returned (the effect's last samples) */
    uint32_t last_write_dur_us;                  /* how long that write call took */
    uint32_t effect_samples, effect_write_us;    /* the last effect (since a cut): samples written, first write to last */
    int64_t pa_low_us, pm_release_us, close_us;  /* the last end: amp low, PM lock released; the last 2 s idle close */
    int32_t ram_task_b, ram_codec_b;             /* internal RAM the task + queue + lock, and the channel + codec took */
    unsigned stack_free_b;
} sfx_stat_t;
void sfx_debug_stat(sfx_stat_t *out);
#if CONFIG_OMNI_DEBUG_CONSOLE
void sfx_debug_post(bool play, const char *name);   /* `sfx race`: a STOP, or a PLAY that skips the block check */
#endif
