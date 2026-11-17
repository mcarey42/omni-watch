#pragma once
/* The one-time move from the Stage 3 namespace `omni` to the Stage 4b layout (spec §5, §6): watch/shared plus the
 * device facts for every theme, t_omni/local for the Omni. Pure C over a key-value interface, so the host runs it over
 * an in-memory store and over golden NVS images (test_migrate, test_nvs_image); the device passes an NVS adapter
 * (Task 6). Namespace `omni` is only ever read: it stays the rollback source for the older firmwares. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/local.h"

typedef enum { OMNI_KV_OK = 0, OMNI_KV_NOT_FOUND = 1, OMNI_KV_ERR = -1 } omni_kv_rc_t;

typedef struct {
    void *ctx;  /* every set is committed before it returns (device: nvs_set_* + nvs_commit). An absent key OR an
                   absent namespace is NOT_FOUND. A set naming OMNI_NS_OLD returns ERR without writing (both kvs).
                   A get whose value does not fit the caller's buffer, or whose key holds another type, is ERR
                   (device: ESP_ERR_NVS_INVALID_LENGTH / a type mismatch). */
    omni_kv_rc_t (*get_blob)(void *ctx, const char *ns, const char *key, uint8_t *buf, size_t *n); /* *n: cap in, size out */
    omni_kv_rc_t (*set_blob)(void *ctx, const char *ns, const char *key, const uint8_t *buf, size_t n);
    omni_kv_rc_t (*get_str)(void *ctx, const char *ns, const char *key, char *buf, size_t cap);
    omni_kv_rc_t (*set_str)(void *ctx, const char *ns, const char *key, const char *s);
} omni_kv_t;

typedef enum { OMNI_MIG_NOT_NEEDED, OMNI_MIG_NO_SOURCE, OMNI_MIG_DONE, OMNI_MIG_FAILED } omni_mig_t;

#define OMNI_NS_OLD "omni"
#define OMNI_NS_WATCH "watch"
#define OMNI_NS_T_OMNI "t_omni"

#define OMNI_KEY_SETTINGS "settings"   /* omni: the Stage 3 settings blob (v1 or v2) */
#define OMNI_KEY_CLOCKCAL "clockcal"   /* omni and watch: OMNI_CLK_BLOB_SIZE, copied verbatim */
#define OMNI_KEY_BATREC "batrec"       /* omni and watch: OMNI_BATREC_BLOB_SIZE, copied verbatim */
#define OMNI_KEY_THEME "theme"         /* watch: the id of the theme that last booted (a string) */
#define OMNI_KEY_SHARED "shared"       /* watch: the shared record; written last, so it is the "migrated" marker */
#define OMNI_KEY_LOCAL "local"         /* t_<id>: the theme's own record */

/* The largest value the migration reads (any larger is a kv ERR, so FAILED). The Stage 3 blobs are 62, 274 and 114 B
 * and a record is capped at OMNI_REC_MAX, so this leaves room for any of them grown. */
#define OMNI_MIG_READ_MAX 512

/* Every theme (core). Runs when watch/shared is absent, or always with force. Reads omni/settings (v1 or v2),
 * omni/clockcal and omni/batrec. Writes, in order: watch/clockcal and watch/batrec (each only if absent in watch and
 * present in omni, copied verbatim, force or not), watch/theme = theme_id, then watch/shared LAST (the marker). Any
 * read error other than NOT_FOUND, or any write error, returns FAILED at once (the next boot runs again). Never
 * writes namespace omni.
 * The source: omni/settings decoded (v1 or v2; an unknown city is the default city). Present but unusable (a size
 * or version no decoder reads): FAILED with 0 sets, force or not, facts or not, so the boot runs read-only and a
 * later firmware can still migrate the blob. Absent: watch/shared is the defaults, written only when omni still holds
 * a clockcal or batrec to carry over and not under force (force re-imports omni/settings and never overwrites the
 * shared record with defaults); otherwise NO_SOURCE with 0 sets.
 * Force and an unknown city: when watch/shared holds a city this build's table does not know (one a newer theme
 * added), force keeps its stored name and country bytes and re-imports the rest (final review I1); a read error on
 * watch/shared under force is FAILED with 0 sets.
 * Interrupted force: the old marker still stands, so a plain run is NOT_NEEDED and leaves the partial rewrite (the
 * new t_omni/local beside the old watch/shared); running force again repairs it, ending exactly as an uninterrupted
 * force (test_migrate_fault_force). Omni itself is never touched either way. */
omni_mig_t omni_migrate_shared(const omni_kv_t *kv, const char *theme_id, bool force);

/* The Omni only. Runs when t_omni/local is absent (or always with force) and omni/settings decodes: writes
 * t_omni/local from its reveal and carousel (validated against lim), face_idx 0. NO_SOURCE (0 sets) when
 * omni/settings is absent; FAILED (0 sets) when it is present but unusable. */
omni_mig_t omni_migrate_omni_local(const omni_kv_t *kv, const omni_theme_limits_t *lim, bool force);

/* The device facts (watch/clockcal, watch/batrec: uint16 version, then a fixed-size body) follow the records' rule
 * that older code never writes over a newer format (final review m3). rc and buf[0..n) are the get's result (on the
 * device the buffer is exactly this build's size, so a longer value is ERR). Writable: absent (NOT_FOUND), or exactly
 * this build's size and version (its body may still be out of range: the fact is re-learnt and written). Not
 * writable, so read-only for the boot: a read error, another size, or another version. */
bool omni_fact_writable(omni_kv_rc_t rc, const uint8_t *buf, size_t n, size_t size, uint16_t version);
