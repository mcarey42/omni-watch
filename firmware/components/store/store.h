#pragma once
/* Persistence on the Stage 4b layout (spec §5, §6): watch/shared (the settings every theme shares), t_<id>/local (the
 * theme's own: reveal, carousel, face), watch/theme (the theme that last booted), watch/clockcal and watch/batrec. At
 * boot the Stage 3 namespace `omni` is migrated once, by omni/boot.h's policy over an NVS kv adapter; `omni` is only
 * ever read (the adapter refuses any set on it) and NVS is never erased. A failed migration makes the boot read-only
 * (nothing written until a reboot) on omni/settings as decoded.
 *
 * THREADING: settings_commit() must NOT be called from an LVGL callback. It re-applies brightness through
 * power_set_brightness(), which takes the power mutex (the same one power_apply holds across LVGL
 * pause/resume), so calling it with the LVGL lock held can deadlock. From a UI callback post an event and
 * let the event loop call settings_commit(). After boot only the event loop writes NVS (R27): the saves, the face,
 * store_migrate_force. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "omni/settings.h"
#include "omni/local.h"
#include "omni/batrec.h"
#include "omni/clockcal.h"
#include "omni/boot.h"

/* Battery session record (omni_batrec_t) in blob watch/batrec: a version prefix and a size check. Written only on
 * VBUS edges and coarse checkpoints, never on a read-only boot. Absent or mismatched gives an empty record. */
esp_err_t store_batrec_load(omni_batrec_t *r);
esp_err_t store_batrec_save(const omni_batrec_t *r);

/* Clock calibration (omni_clk_saved_t: rate, sync interval, SNTP correction log) in blob watch/clockcal, versioned
 * and size-checked, then omni_clk_restore's range check. Written by the event loop after each sync (R27), never on a
 * read-only boot. Absent or unusable gives the defaults. */
esp_err_t store_clock_load(omni_clockcal_t *c);
esp_err_t store_clock_save(const omni_clockcal_t *c);

/* NVS init (never erases), omni_boot_run, apply TZ + brightness. Plain arguments: store sits below watch_ui. On the
 * boot task, before the event loop starts. Returns nvs_flash_init's result. */
esp_err_t store_init(const char *id, const omni_theme_limits_t *lim, bool omni_legacy);
const omni_settings_t *settings(void);
omni_settings_t *settings_edit(void);            /* mutate, then call settings_commit() */
/* Applies TZ and brightness now and saves to NVS 2 s after the last call (debounced): the timer posts EV_SAVE
 * and the event loop calls store_settings_save_now (R27: the settings are read and written on that task only). */
void settings_commit(void);
void store_settings_save_now(void);

const char *store_settings_origin(void);  /* event loop only (see store_status): "watch v1", "watch v1 (tail kept)", "migrated from omni v2",
                                             "defaults (none saved)", "read-only: watch/shared v<N>",
                                             "read-only: migration failed", ... */
const char *store_migration(void);        /* "not needed" | "local done, shared done" | "failed: <part> <err>" */
/* Event loop only: the two strings above are rewritten there by store_migrate_force. Any other task (the console's
 * `state`) copies them with store_status, under the store's lock. */
void store_status(char *origin, size_t origin_cap, char *migration, size_t migration_cap);
/* This boot writes nothing (nvs_flash_init failed, or a migration part FAILED): every NVS writer checks it. */
bool store_read_only(void);
const char *store_prev_theme(void);       /* watch/theme as found at boot, before migrating; "" if none */
bool store_theme_swapped(void);           /* prev non-empty and != id */
uint8_t store_face(void);                 /* t_<id>/local face_idx, clamped */
void store_face_commit(uint8_t idx);      /* arms the same 2 s debounced save; no apply (no panel command) */

/* The theme's extension: the tail of t_<id>/local after the 23 B common prefix (spec §5, WD3). The core keeps it byte
 * for byte; the theme gives it a meaning (dw/prefs.h). Event loop or boot task only (R27), like the saves. */
#define STORE_LOCAL_EXT_MAX (OMNI_REC_MAX - OMNI_LOCAL_V1_SIZE)   /* 233: size the caller's buffer with this */
/* t_<id>/local's tail into buf, under the store lock; 0 if none. Returns the tail's length; when that is more than
 * cap NOTHING is copied (a caller never holds a truncated tail). */
size_t store_local_ext(uint8_t *buf, size_t cap);
/* Replaces the tail and arms the same 2 s debounced save (store_settings_save_now saves at once).
 *   OMNI_TAIL_OK:        replaced; the save writes it.
 *   OMNI_TAIL_BAD_LEN:   n > STORE_LOCAL_EXT_MAX, or n == 0 over a non-empty tail: refused, RAM unchanged.
 *   OMNI_TAIL_READ_ONLY: a read-only boot or a read-only t_<id>/local: applied in RAM only, never written (F7).
 *                        Also before store_init has run, or when its boot state could not be allocated: then nothing
 *                        is applied, in RAM or NVS (the dw final review, M4 N2). */
omni_tail_rc_t store_local_ext_commit(const uint8_t *buf, size_t n);
bool store_local_writable(void);  /* false on a no-write boot or when t_<id>/local is read-only (C2) */

typedef struct { size_t used_entries, free_entries, total_entries;
                 size_t watch_b, theme_b, old_b; } store_nvs_stats_t;  /* *_b = nvs_get_used_entry_count × 32 */
esp_err_t store_nvs_stats(store_nvs_stats_t *out);
/* EVENT LOOP ONLY (R27): omni_boot_force over the NVS kv; never writes omni. Cancels a pending save first, reloads the
 * RAM view and applies it. ESP_ERR_INVALID_STATE: a read-only boot (refused); ESP_ERR_NOT_ALLOWED: a record is
 * read-only (refused: a newer record is never overwritten); ESP_FAIL: a part failed (force again repairs it). */
esp_err_t store_migrate_force(void);
