#pragma once
/* The boot and save policy over the records (spec §5, §6; R124 items 2 and 3), pure so the host runs it over the
 * in-memory kv: store.c runs it over its NVS kv adapter, on the boot task before the event loop starts and then on the
 * event loop only (R27).
 *
 * omni_boot_run, in order:
 *   1. no_write in (nvs_flash_init failed): nothing is read or written; the defaults.
 *   2. watch/theme is read into prev BEFORE any migration (preflight A5/B17: the migration writes it).
 *   3. omni_migrate_omni_local (omni_legacy only), then omni_migrate_shared, neither forced. If the local part
 *      FAILED, the shared part is skipped: a read-only boot writes nothing at all (Task 6 review m2).
 *   4. Either part FAILED: no_write for the rest of the boot (no record, no face, no watch/theme), and the RAM view is
 *      omni/settings decoded (the defaults only if that read fails), so a failed migration never makes a loss
 *      permanent: the next boot migrates again (R124 item 2, preflight B16).
 *   5. Otherwise watch/shared and t_<id>/local are loaded and joined. A record this build cannot own (a version it does
 *      not know, unusable bytes, or a read error) is read-only for the boot: its changes apply in RAM and it is never
 *      written, by a save or by force (Task 1's ruling: a newer record is never written over). An absent
 *      t_<id>/local is the defaults, written once now; an absent watch/shared is the defaults, written by the first
 *      save.
 *   6. watch/theme = id, written only if it differs from what is there now.
 * A save encodes both records and writes each only if its bytes differ from the last ones read or written. A stored
 * city this build does not know is shown as the default city but re-encoded from its stored bytes, so a save (or
 * force) never replaces it with the default (final review I1; omni_shared_t.city_kept). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/local.h"
#include "omni/migrate.h"
#include "omni/record.h"
#include "omni/settings.h"
#include "omni/shared.h"

#define OMNI_BOOT_ID_MAX 16     /* a theme id and its NUL: "t_<id>" must fit NVS's 15 characters */

typedef struct {
    omni_settings_t s;              /* the joined RAM view */
    char prev[OMNI_BOOT_ID_MAX];    /* watch/theme read BEFORE migrating; "" if none */
    omni_mig_t mig_local, mig_shared;
    omni_rec_load_t shared_load, local_load;   /* meaningful when *_found */
    bool no_write;                  /* nothing is written this boot: init error, or a part returned FAILED */
    bool shared_ro;                 /* watch/shared not this build's to write (unknown version, unusable, unreadable) */
    bool local_ro;                  /* the same for t_<id>/local */
    bool nvs_down;                  /* no_write came in: NVS was never read */
    bool legacy;                    /* the local part ran (omni_legacy) */
    bool forced;                    /* mig_* are omni_boot_force's results */
    bool shared_skipped;            /* the local part FAILED, so the shared part never ran (mig_shared unset) */
    bool shared_found, local_found; /* present when loaded (step 5) */
    bool theme_written;             /* step 6 wrote watch/theme */
    omni_kv_rc_t theme_rc;          /* step 6's set, when it ran */
    uint16_t shared_ver, local_ver; /* the version a read-only record carries (0: no header) */
    uint8_t src_ver;                /* omni/settings' version, when this boot migrated from it or runs on it */
    char ns_local[OMNI_BOOT_ID_MAX];               /* "t_<id>" */
    const omni_theme_limits_t *lim;                /* the descriptor's (static lifetime) */
    omni_shared_t sh;               /* the records as last loaded or saved: tails, face_idx */
    omni_local_t lo;
    /* the last bytes read or written per record, for write-only-on-change (0: none) */
    uint8_t shared_b[OMNI_REC_MAX], local_b[OMNI_REC_MAX];
    size_t shared_n, local_n;
} omni_boot_t;

/* no_write in: nvs_flash_init failed. id: the theme's (at most OMNI_BOOT_ID_MAX - 3 characters). */
void omni_boot_run(omni_boot_t *b, const omni_kv_t *kv, const char *id, const omni_theme_limits_t *lim,
                   bool omni_legacy, bool no_write);
/* Takes s as the RAM view (validated), then writes what changed. no_write: 0 sets (OK). ERR: a set failed. */
omni_kv_rc_t omni_boot_save(omni_boot_t *b, const omni_kv_t *kv, const omni_settings_t *s);
/* Both parts with force (spec §6: omni/settings re-imported over the records), then b->s reloaded. Refused (ERR,
 * 0 sets) when no_write or when either record is read-only: force never overwrites a newer record. ERR: a part
 * returned FAILED (running force again repairs it, migrate.h). */
omni_kv_rc_t omni_boot_force(omni_boot_t *b, const omni_kv_t *kv, const char *id, const omni_theme_limits_t *lim,
                             bool omni_legacy);
/* The tail of t_<id>/local belongs to the theme (its extension, spec §5); the core keeps it byte for byte and the
 * next save writes it through the same write-only-on-change path. A theme sets it only through
 * omni_boot_commit_local_tail below (the raw setter is boot.c's own since the dw final review, M4 N1). The RAM view
 * takes it even when local_ro or no_write (then nothing is written). */
#define OMNI_BOOT_TAIL_MAX (OMNI_REC_MAX - OMNI_LOCAL_V1_SIZE)   /* 233: the longest tail after the 23 B prefix */
/* The tail's length. Its bytes are copied into buf only when they all fit (length <= cap); otherwise nothing is
 * copied, so a caller never holds, decodes or re-commits a truncated tail. */
size_t omni_boot_local_tail(const omni_boot_t *b, uint8_t *buf, size_t cap);
typedef enum {
    OMNI_TAIL_OK,         /* replaced in RAM; the next save writes it */
    OMNI_TAIL_BAD_LEN,    /* n > OMNI_BOOT_TAIL_MAX, or n == 0 over a non-empty tail (it would drop the kept bytes):
                             refused, RAM unchanged */
    OMNI_TAIL_READ_ONLY   /* a no-write boot or a read-only t_<id>/local: applied in RAM only, never written (F7) */
} omni_tail_rc_t;
omni_tail_rc_t omni_boot_commit_local_tail(omni_boot_t *b, const uint8_t *buf, size_t n);
/* false on a no-write boot or when t_<id>/local is read-only: a save would not write the record (C2). */
bool omni_boot_local_writable(const omni_boot_t *b);
/* omni_settings_validate, then the carousel and reveal against the theme's limits. false if anything changed. */
bool omni_boot_validate(const omni_boot_t *b, omni_settings_t *s);

/* For the log and `state`. */
bool omni_boot_swapped(const omni_boot_t *b, const char *id);   /* prev non-empty and != id */
const char *omni_boot_theme_line(const omni_boot_t *b, const char *id, char *buf, size_t cap); /* "theme omni (was none)" */
const char *omni_boot_origin(const omni_boot_t *b, char *buf, size_t cap);     /* "watch v1", "migrated from omni v2" */
const char *omni_boot_migration(const omni_boot_t *b, char *buf, size_t cap);  /* "not needed", "local done, shared
                                                                                   done", "failed: shared" */
uint8_t omni_boot_face(const omni_boot_t *b);                   /* t_<id>/local face_idx, clamped to the carousel */
/* The console prompt: "<id>> ", or "watch> " for a NULL or empty id. Truncated to cap, always NUL-terminated (cap 0
 * touches nothing). debug_console_start builds it from the theme descriptor's id, so no image holds the literal. */
const char *omni_boot_prompt(const char *id, char *buf, size_t cap);
