#include "omni/boot.h"
#include <stdio.h>
#include <string.h>

/* Stack: the boot task's stack is small (3.5 KB, store.c), so the record bytes live in the omni_boot_t (the caller's,
 * in PSRAM on the device) and only small values sit here; the migration parts take their own ~1.1 KB per call. */

/* name: a record's bytes into *buf (cap OMNI_REC_MAX). OK, NOT_FOUND (absent key or namespace), or ERR (including a
 * value over the cap: a newer, longer record is not this build's to write). */
static omni_kv_rc_t read_rec(const omni_kv_t *kv, const char *ns, const char *key, uint8_t *buf, size_t *n)
{
    *n = OMNI_REC_MAX;
    omni_kv_rc_t rc = kv->get_blob(kv->ctx, ns, key, buf, n);
    if (rc != OMNI_KV_OK) *n = 0;
    return rc;
}

static uint16_t rec_version(const uint8_t *buf, size_t n)
{
    return n >= 2 ? (uint16_t)(buf[0] | buf[1] << 8) : 0;
}

/* A record this build cannot own: changes stay in RAM, it is never written (Task 1's ruling). */
static bool owns(omni_rec_load_t r)
{
    return r != OMNI_REC_UNKNOWN_VERSION && r != OMNI_REC_UNUSABLE;
}

static void join(omni_boot_t *b)
{
    omni_settings_join(&b->sh, &b->lo, &b->s);
}

/* Step 4: omni/settings decoded into the RAM view (R124 item 2), or the defaults if it cannot be read. */
static void read_only_from_omni(omni_boot_t *b, const omni_kv_t *kv)
{
    uint8_t blob[OMNI_SETTINGS_BLOB_SIZE];
    size_t n = sizeof blob;
    omni_settings_t s;
    if (kv->get_blob(kv->ctx, OMNI_NS_OLD, OMNI_KEY_SETTINGS, blob, &n) == OMNI_KV_OK &&
        omni_settings_decode(&s, blob, n) != OMNI_SETTINGS_UNUSABLE) {
        b->src_ver = (uint8_t)rec_version(blob, n);
        omni_settings_split(&s, &b->sh, &b->lo);
        omni_shared_validate(&b->sh);
        omni_local_validate(&b->lo, b->lim);
    }
    join(b);
}

/* Step 5 (and after force): both records loaded and joined; an absent local record is the defaults, written now. */
static void load_records(omni_boot_t *b, const omni_kv_t *kv)
{
    omni_shared_defaults(&b->sh);
    omni_local_defaults(&b->lo, b->lim);
    b->shared_ro = b->local_ro = b->shared_found = b->local_found = false;
    b->shared_ver = b->local_ver = 0;

    omni_kv_rc_t rc = read_rec(kv, OMNI_NS_WATCH, OMNI_KEY_SHARED, b->shared_b, &b->shared_n);
    if (rc == OMNI_KV_OK) {
        b->shared_found = true;
        b->shared_load = omni_shared_decode(&b->sh, b->shared_b, b->shared_n);
        b->shared_ver = rec_version(b->shared_b, b->shared_n);
        b->shared_ro = !owns(b->shared_load);
    } else if (rc == OMNI_KV_ERR) {
        b->shared_ro = true;   /* unreadable: never written over */
    }

    rc = read_rec(kv, b->ns_local, OMNI_KEY_LOCAL, b->local_b, &b->local_n);
    if (rc == OMNI_KV_OK) {
        b->local_found = true;
        b->local_load = omni_local_decode(&b->lo, b->local_b, b->local_n, b->lim);
        b->local_ver = rec_version(b->local_b, b->local_n);
        b->local_ro = !owns(b->local_load);
    } else if (rc == OMNI_KV_ERR) {
        b->local_ro = true;
    } else {
        size_t n = omni_local_encode(&b->lo, b->local_b, sizeof b->local_b);
        if (n > 0 && kv->set_blob(kv->ctx, b->ns_local, OMNI_KEY_LOCAL, b->local_b, n) == OMNI_KV_OK)
            b->local_n = n;   /* else written by the first save */
    }
    join(b);
}

void omni_boot_run(omni_boot_t *b, const omni_kv_t *kv, const char *id, const omni_theme_limits_t *lim,
                   bool omni_legacy, bool no_write)
{
    memset(b, 0, sizeof *b);
    b->lim = lim;
    b->legacy = omni_legacy;
    b->mig_local = b->mig_shared = OMNI_MIG_NOT_NEEDED;
    snprintf(b->ns_local, sizeof b->ns_local, "t_%s", id);
    omni_shared_defaults(&b->sh);
    omni_local_defaults(&b->lo, lim);
    if (no_write) {
        b->no_write = b->nvs_down = true;
        join(b);
        return;
    }

    if (kv->get_str(kv->ctx, OMNI_NS_WATCH, OMNI_KEY_THEME, b->prev, sizeof b->prev) != OMNI_KV_OK) b->prev[0] = '\0';

    if (omni_legacy) b->mig_local = omni_migrate_omni_local(kv, lim, false);
    if (b->mig_local == OMNI_MIG_FAILED) b->shared_skipped = true;   /* a read-only boot writes nothing at all */
    else b->mig_shared = omni_migrate_shared(kv, id, false);
    if (b->mig_local == OMNI_MIG_FAILED || b->mig_shared == OMNI_MIG_FAILED) {
        b->no_write = true;
        read_only_from_omni(b, kv);
        return;
    }
    if (b->mig_shared == OMNI_MIG_DONE) {   /* the source's version, for `state` */
        uint8_t hdr[OMNI_SETTINGS_BLOB_SIZE];
        size_t n = sizeof hdr;
        if (kv->get_blob(kv->ctx, OMNI_NS_OLD, OMNI_KEY_SETTINGS, hdr, &n) == OMNI_KV_OK)
            b->src_ver = (uint8_t)rec_version(hdr, n);
    }

    load_records(b, kv);

    char now[OMNI_BOOT_ID_MAX];
    if (kv->get_str(kv->ctx, OMNI_NS_WATCH, OMNI_KEY_THEME, now, sizeof now) != OMNI_KV_OK || strcmp(now, id) != 0) {
        b->theme_rc = kv->set_str(kv->ctx, OMNI_NS_WATCH, OMNI_KEY_THEME, id);
        b->theme_written = true;
    }
}

bool omni_boot_validate(const omni_boot_t *b, omni_settings_t *s)
{
    bool ok = omni_settings_validate(s);
    omni_shared_t sh;
    omni_shared_defaults(&sh);   /* split reads city_kept_idx */
    omni_local_t lo;
    omni_local_defaults(&lo, b->lim);
    omni_settings_split(s, &sh, &lo);
    if (!omni_local_validate(&lo, b->lim)) {
        s->reveal_style = lo.reveal_style;
        memcpy(s->carousel, lo.carousel, OMNI_CAROUSEL_MAX);
        s->carousel_len = lo.carousel_len;
        ok = false;
    }
    return ok;
}

/* The theme's extension: the RAM view only; omni_boot_save writes it when the record is this build's to write. */
_Static_assert(OMNI_BOOT_TAIL_MAX == sizeof ((omni_local_t *)0)->tail, "the tail cap is the local record's");

/* The raw setter (file-static since the dw final review, M4 N1: n == 0 wipes a kept tail, which only the commit's
 * refusals make safe): false when n > the tail's room, nothing changed. */
static bool set_local_tail(omni_boot_t *b, const uint8_t *buf, size_t n)
{
    if (n > sizeof b->lo.tail) return false;
    if (n) memcpy(b->lo.tail, buf, n);
    memset(b->lo.tail + n, 0, sizeof b->lo.tail - n);
    b->lo.tail_len = (uint16_t)n;
    return true;
}

bool omni_boot_local_writable(const omni_boot_t *b)
{
    return !b->no_write && !b->local_ro;
}

size_t omni_boot_local_tail(const omni_boot_t *b, uint8_t *buf, size_t cap)
{
    const size_t n = b->lo.tail_len;
    if (n > 0 && n <= cap) memcpy(buf, b->lo.tail, n);
    return n;
}

omni_tail_rc_t omni_boot_commit_local_tail(omni_boot_t *b, const uint8_t *buf, size_t n)
{
    if (n > OMNI_BOOT_TAIL_MAX || (n == 0 && b->lo.tail_len > 0)) return OMNI_TAIL_BAD_LEN;
    set_local_tail(b, buf, n);
    return omni_boot_local_writable(b) ? OMNI_TAIL_OK : OMNI_TAIL_READ_ONLY;
}

/* One record: encoded, then written only if its bytes differ from the last ones read or written. */
static omni_kv_rc_t save_rec(const omni_kv_t *kv, const char *ns, const char *key, const uint8_t *enc, size_t n,
                             uint8_t *last, size_t *last_n)
{
    if (n == 0) return OMNI_KV_ERR;
    if (n == *last_n && memcmp(enc, last, n) == 0) return OMNI_KV_OK;
    omni_kv_rc_t rc = kv->set_blob(kv->ctx, ns, key, enc, n);
    if (rc == OMNI_KV_OK) {
        memcpy(last, enc, n);
        *last_n = n;
    }
    return rc;
}

omni_kv_rc_t omni_boot_save(omni_boot_t *b, const omni_kv_t *kv, const omni_settings_t *s)
{
    b->s = *s;
    omni_boot_validate(b, &b->s);
    omni_settings_split(&b->s, &b->sh, &b->lo);   /* keeps the tails and face_idx */
    omni_local_validate(&b->lo, b->lim);          /* face_idx within the carousel */
    if (b->no_write) return OMNI_KV_OK;
    omni_kv_rc_t rc = OMNI_KV_OK;
    uint8_t enc[OMNI_REC_MAX];
    if (!b->shared_ro &&
        save_rec(kv, OMNI_NS_WATCH, OMNI_KEY_SHARED, enc, omni_shared_encode(&b->sh, enc, sizeof enc), b->shared_b,
                 &b->shared_n) != OMNI_KV_OK)
        rc = OMNI_KV_ERR;
    if (!b->local_ro &&
        save_rec(kv, b->ns_local, OMNI_KEY_LOCAL, enc, omni_local_encode(&b->lo, enc, sizeof enc), b->local_b,
                 &b->local_n) != OMNI_KV_OK)
        rc = OMNI_KV_ERR;
    return rc;
}

omni_kv_rc_t omni_boot_force(omni_boot_t *b, const omni_kv_t *kv, const char *id, const omni_theme_limits_t *lim,
                             bool omni_legacy)
{
    if (b->no_write || b->shared_ro || b->local_ro) return OMNI_KV_ERR;   /* never over a newer record */
    b->lim = lim;
    b->forced = true;
    b->legacy = omni_legacy;
    b->mig_local = omni_legacy ? omni_migrate_omni_local(kv, lim, true) : OMNI_MIG_NOT_NEEDED;
    b->mig_shared = omni_migrate_shared(kv, id, true);
    load_records(b, kv);
    return b->mig_local == OMNI_MIG_FAILED || b->mig_shared == OMNI_MIG_FAILED ? OMNI_KV_ERR : OMNI_KV_OK;
}

bool omni_boot_swapped(const omni_boot_t *b, const char *id)
{
    return b->prev[0] != '\0' && strcmp(b->prev, id) != 0;
}

const char *omni_boot_theme_line(const omni_boot_t *b, const char *id, char *buf, size_t cap)
{
    snprintf(buf, cap, "theme %s (was %s)", id, b->prev[0] ? b->prev : "none");
    return buf;
}

const char *omni_boot_origin(const omni_boot_t *b, char *buf, size_t cap)
{
    if (b->nvs_down) snprintf(buf, cap, "defaults (NVS not read)");
    else if (b->no_write) snprintf(buf, cap, "read-only: migration failed");
    else if (b->shared_ro && b->shared_found && b->shared_load == OMNI_REC_UNKNOWN_VERSION)
        snprintf(buf, cap, "read-only: watch/shared v%u", (unsigned)b->shared_ver);
    else if (b->shared_ro) snprintf(buf, cap, "read-only: watch/shared %s", b->shared_found ? "unusable" : "unreadable");
    else if (b->local_ro && b->local_found && b->local_load == OMNI_REC_UNKNOWN_VERSION)
        snprintf(buf, cap, "read-only: %s/local v%u", b->ns_local, (unsigned)b->local_ver);
    else if (b->local_ro) snprintf(buf, cap, "read-only: %s/local %s", b->ns_local, b->local_found ? "unusable" : "unreadable");
    else if (!b->forced && b->mig_shared == OMNI_MIG_DONE && b->src_ver)
        snprintf(buf, cap, "migrated from omni v%u", (unsigned)b->src_ver);
    else if (!b->shared_found) snprintf(buf, cap, "defaults (none saved)");
    else if (b->shared_load == OMNI_REC_TAIL_KEPT) snprintf(buf, cap, "watch v%u (tail kept)", (unsigned)b->shared_ver);
    else if (b->shared_load == OMNI_REC_CITY_UNKNOWN)
        snprintf(buf, cap, "watch v%u, city unknown: kept, default shown", (unsigned)b->shared_ver);
    else snprintf(buf, cap, "watch v%u", (unsigned)b->shared_ver);
    return buf;
}

static const char *mig_name(omni_mig_t m)
{
    switch (m) {
    case OMNI_MIG_NOT_NEEDED: return "not needed";
    case OMNI_MIG_NO_SOURCE: return "no source";
    case OMNI_MIG_DONE: return "done";
    default: return "failed";
    }
}

const char *omni_boot_migration(const omni_boot_t *b, char *buf, size_t cap)
{
    const bool lf = b->legacy && b->mig_local == OMNI_MIG_FAILED, sf = b->mig_shared == OMNI_MIG_FAILED;
    const char *pre = b->forced ? "forced: " : "";
    if (b->nvs_down) snprintf(buf, cap, "skipped (NVS not read)");
    else if (lf && b->shared_skipped) snprintf(buf, cap, "%sfailed: local (shared skipped)", pre);
    else if (lf || sf) snprintf(buf, cap, "%sfailed: %s", pre, lf && sf ? "local, shared" : lf ? "local" : "shared");
    else if (!b->forced && b->mig_shared == OMNI_MIG_NOT_NEEDED && (!b->legacy || b->mig_local == OMNI_MIG_NOT_NEEDED))
        snprintf(buf, cap, "not needed");
    else if (b->legacy) snprintf(buf, cap, "%slocal %s, shared %s", pre, mig_name(b->mig_local), mig_name(b->mig_shared));
    else snprintf(buf, cap, "%sshared %s", pre, mig_name(b->mig_shared));
    return buf;
}

uint8_t omni_boot_face(const omni_boot_t *b)
{
    return b->lo.face_idx < b->s.carousel_len ? b->lo.face_idx : 0;
}

const char *omni_boot_prompt(const char *id, char *buf, size_t cap)
{
    if (cap == 0) return buf;
    snprintf(buf, cap, "%s> ", (id && id[0]) ? id : "watch");
    return buf;
}
