#include "omni/migrate.h"
#include "omni/batrec.h"
#include "omni/clockcal.h"
#include "omni/settings.h"
#include "omni/shared.h"
#include <string.h>

_Static_assert(OMNI_SETTINGS_BLOB_SIZE <= OMNI_MIG_READ_MAX, "settings blob over the read buffer");
_Static_assert(OMNI_CLK_BLOB_SIZE <= OMNI_MIG_READ_MAX, "clockcal blob over the read buffer");
_Static_assert(OMNI_BATREC_BLOB_SIZE <= OMNI_MIG_READ_MAX, "batrec blob over the read buffer");
_Static_assert(OMNI_REC_MAX <= OMNI_MIG_READ_MAX, "record cap over the read buffer");

/* Whether a blob exists: OK (present), NOT_FOUND, or ERR. */
static omni_kv_rc_t probe(const omni_kv_t *kv, const char *ns, const char *key, uint8_t *buf)
{
    size_t n = OMNI_MIG_READ_MAX;
    return kv->get_blob(kv->ctx, ns, key, buf, &n);
}

/* omni/settings decoded into *s: OK, NOT_FOUND (absent: *s is the defaults), or ERR, which includes a blob that is
 * present but unusable (ruling: FAILED, so the boot runs read-only and a later firmware can still migrate it). */
static omni_kv_rc_t read_settings(const omni_kv_t *kv, omni_settings_t *s, uint8_t *buf)
{
    omni_settings_defaults(s);
    size_t n = OMNI_MIG_READ_MAX;
    omni_kv_rc_t rc = kv->get_blob(kv->ctx, OMNI_NS_OLD, OMNI_KEY_SETTINGS, buf, &n);
    if (rc != OMNI_KV_OK) return rc;
    return omni_settings_decode(s, buf, n) == OMNI_SETTINGS_UNUSABLE ? OMNI_KV_ERR : OMNI_KV_OK;
}

/* omni/<key> to watch/<key>, byte for byte; the caller has seen it present in omni and absent in watch. It reads
 * omni/<key> again: an error here comes after earlier writes, but before the marker, so the next run converges. */
static bool copy_fact(const omni_kv_t *kv, const char *key, uint8_t *buf)
{
    size_t n = OMNI_MIG_READ_MAX;
    if (kv->get_blob(kv->ctx, OMNI_NS_OLD, key, buf, &n) != OMNI_KV_OK) return false;
    return kv->set_blob(kv->ctx, OMNI_NS_WATCH, key, buf, n) == OMNI_KV_OK;
}

omni_mig_t omni_migrate_shared(const omni_kv_t *kv, const char *theme_id, bool force)
{
    if (kv == NULL || theme_id == NULL) return OMNI_MIG_FAILED;
    uint8_t buf[OMNI_MIG_READ_MAX];
    omni_kv_rc_t rc;
    omni_shared_t sh;   /* under force, first the record there now: an unknown city (I1) and an appended tail (O1) are kept */
    omni_shared_defaults(&sh);
    if (!force) {
        rc = probe(kv, OMNI_NS_WATCH, OMNI_KEY_SHARED, buf);
        if (rc == OMNI_KV_OK) return OMNI_MIG_NOT_NEEDED;
        if (rc != OMNI_KV_NOT_FOUND) return OMNI_MIG_FAILED;
    } else {
        size_t n = OMNI_MIG_READ_MAX;
        rc = kv->get_blob(kv->ctx, OMNI_NS_WATCH, OMNI_KEY_SHARED, buf, &n);
        if (rc == OMNI_KV_ERR) return OMNI_MIG_FAILED;
        if (rc == OMNI_KV_OK) {
            omni_rec_load_t lr = omni_shared_decode(&sh, buf, n);
            if (lr != OMNI_REC_LOADED && lr != OMNI_REC_TAIL_KEPT && lr != OMNI_REC_CITY_UNKNOWN) omni_shared_defaults(&sh);
        }
    }

    /* every probe before any write, so a read error there writes nothing (copy_fact re-reads a fact's bytes later) */
    omni_settings_t s;
    omni_kv_rc_t have_s = read_settings(kv, &s, buf);
    if (have_s == OMNI_KV_ERR) return OMNI_MIG_FAILED;
    static const char *const facts[] = { OMNI_KEY_CLOCKCAL, OMNI_KEY_BATREC };
    bool carry[2] = { false, false };
    bool any_fact = false;
    for (int i = 0; i < 2; i++) {
        rc = probe(kv, OMNI_NS_OLD, facts[i], buf);
        if (rc == OMNI_KV_ERR) return OMNI_MIG_FAILED;
        bool in_old = rc == OMNI_KV_OK;
        rc = probe(kv, OMNI_NS_WATCH, facts[i], buf);
        if (rc == OMNI_KV_ERR) return OMNI_MIG_FAILED;
        carry[i] = in_old && rc == OMNI_KV_NOT_FOUND;
        any_fact = any_fact || in_old;
    }
    /* settings absent: defaults only to carry the facts over, and never under force */
    if (have_s != OMNI_KV_OK && (force || !any_fact)) return OMNI_MIG_NO_SOURCE;

    const omni_shared_t was = sh;   /* what the record kept (the city, I1; the tail, O1), restored below */
    omni_shared_defaults(&sh);
    if (have_s == OMNI_KV_OK) {
        omni_local_t unused;
        omni_settings_split(&s, &sh, &unused);
    }
    omni_shared_validate(&sh);
    sh.tail_len = was.tail_len;   /* a newer build's appended fields stay, byte for byte (O1) */
    memcpy(sh.tail, was.tail, sizeof sh.tail);
    if (was.city_kept) {   /* the stored city stays: omni/settings never held it */
        sh.city = was.city_kept_idx;
        sh.city_kept = true;
        sh.city_kept_idx = was.city_kept_idx;
        memcpy(sh.city_raw, was.city_raw, sizeof sh.city_raw);
    }

    for (int i = 0; i < 2; i++)
        if (carry[i] && !copy_fact(kv, facts[i], buf)) return OMNI_MIG_FAILED;
    if (kv->set_str(kv->ctx, OMNI_NS_WATCH, OMNI_KEY_THEME, theme_id) != OMNI_KV_OK) return OMNI_MIG_FAILED;
    size_t n = omni_shared_encode(&sh, buf, sizeof buf);
    if (n == 0 || kv->set_blob(kv->ctx, OMNI_NS_WATCH, OMNI_KEY_SHARED, buf, n) != OMNI_KV_OK) return OMNI_MIG_FAILED;
    return OMNI_MIG_DONE;
}

omni_mig_t omni_migrate_omni_local(const omni_kv_t *kv, const omni_theme_limits_t *lim, bool force)
{
    if (kv == NULL || lim == NULL) return OMNI_MIG_FAILED;
    uint8_t buf[OMNI_MIG_READ_MAX];
    omni_kv_rc_t rc;
    if (!force) {
        rc = probe(kv, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL, buf);
        if (rc == OMNI_KV_OK) return OMNI_MIG_NOT_NEEDED;
        if (rc != OMNI_KV_NOT_FOUND) return OMNI_MIG_FAILED;
    }
    omni_settings_t s;
    rc = read_settings(kv, &s, buf);
    if (rc == OMNI_KV_ERR) return OMNI_MIG_FAILED;
    if (rc == OMNI_KV_NOT_FOUND) return OMNI_MIG_NO_SOURCE;

    omni_shared_t unused;
    omni_shared_defaults(&unused);   /* split reads city_kept_idx */
    omni_local_t lo;
    omni_local_defaults(&lo, lim);
    omni_settings_split(&s, &unused, &lo);
    lo.face_idx = 0;
    omni_local_validate(&lo, lim);
    size_t n = omni_local_encode(&lo, buf, sizeof buf);
    if (n == 0 || kv->set_blob(kv->ctx, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL, buf, n) != OMNI_KV_OK) return OMNI_MIG_FAILED;
    return OMNI_MIG_DONE;
}

bool omni_fact_writable(omni_kv_rc_t rc, const uint8_t *buf, size_t n, size_t size, uint16_t version)
{
    if (rc == OMNI_KV_NOT_FOUND) return true;
    if (rc != OMNI_KV_OK || buf == NULL || n != size || n < 2) return false;
    return (uint16_t)(buf[0] | buf[1] << 8) == version;
}
