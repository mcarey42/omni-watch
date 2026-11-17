/* The omni-to-watch migration (omni/migrate.h) over the in-memory kv (kv_mem.h), which behaves as the device adapter
 * must: an absent namespace is NOT_FOUND, sets on `omni` are refused. The values are the golden fixture's (nvs/). */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "kv_mem.h"
#include "omni/batrec.h"
#include "omni/cities.h"
#include "omni/clockcal.h"
#include "omni/local.h"
#include "omni/migrate.h"
#include "omni/settings.h"
#include "omni/shared.h"

void setUp(void) {}
void tearDown(void) {}

/* the Omni theme's limits as Task 5 will declare them: 8 faces, 3 reveals, the default order */
static const uint8_t k_def[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const omni_theme_limits_t k_lim = { 8, 3, 0, sizeof k_def, k_def };

static kv_mem_t s_m, s_ref;   /* static: KV_MEM_MAX entries are too large for the stack under ASan */

static uint8_t s_settings[OMNI_SETTINGS_BLOB_SIZE], s_clockcal[OMNI_CLK_BLOB_SIZE], s_batrec[OMNI_BATREC_BLOB_SIZE];

/* a value file: one line of hex digits */
static size_t read_hex(const char *name, uint8_t *out, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", NVS_FIXTURE_DIR, name);
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t n = 0;
    unsigned v;
    while (fscanf(f, "%2x", &v) == 1) {
        TEST_ASSERT_TRUE_MESSAGE(n < cap, path);
        out[n++] = (uint8_t)v;
    }
    fclose(f);
    return n;
}

static void load_values(void)
{
    TEST_ASSERT_EQUAL(sizeof s_settings, read_hex("settings.hex", s_settings, sizeof s_settings));
    TEST_ASSERT_EQUAL(sizeof s_clockcal, read_hex("clockcal.hex", s_clockcal, sizeof s_clockcal));
    TEST_ASSERT_EQUAL(sizeof s_batrec, read_hex("batrec.hex", s_batrec, sizeof s_batrec));
}

/* the Stage 3 store: omni/settings, omni/clockcal, omni/batrec; no watch, no t_omni namespace */
static void fixture(kv_mem_t *m)
{
    kv_mem_free(m);
    load_values();
    TEST_ASSERT_EQUAL(0, kv_mem_put(m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, s_settings, sizeof s_settings));
    TEST_ASSERT_EQUAL(0, kv_mem_put(m, OMNI_NS_OLD, OMNI_KEY_CLOCKCAL, KV_T_BLOB, s_clockcal, sizeof s_clockcal));
    TEST_ASSERT_EQUAL(0, kv_mem_put(m, OMNI_NS_OLD, OMNI_KEY_BATREC, KV_T_BLOB, s_batrec, sizeof s_batrec));
}

typedef struct { omni_mig_t local, shared; } omni_boot_t;

/* the Omni's boot order (spec §6): local, then shared */
static omni_boot_t omni_boot(kv_mem_t *m, bool force)
{
    omni_kv_t kv = kv_mem_iface(m);
    omni_boot_t r;
    r.local = omni_migrate_omni_local(&kv, &k_lim, force);
    r.shared = omni_migrate_shared(&kv, "omni", force);
    return r;
}

static void assert_blob(const kv_mem_t *m, const char *ns, const char *key, const uint8_t *want, size_t n)
{
    const kv_entry_t *e = kv_mem_find(m, ns, key);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, key);
    TEST_ASSERT_EQUAL_MESSAGE(KV_T_BLOB, e->type, key);
    TEST_ASSERT_EQUAL_MESSAGE(n, e->n, key);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(want, e->val, n, key);
}

static void assert_str(const kv_mem_t *m, const char *ns, const char *key, const char *want)
{
    const kv_entry_t *e = kv_mem_find(m, ns, key);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, key);
    TEST_ASSERT_EQUAL_MESSAGE(KV_T_STR, e->type, key);
    TEST_ASSERT_EQUAL_MESSAGE(strlen(want), e->n, key);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, e->val, e->n, key);
}

static void assert_omni_unchanged(const kv_mem_t *m)
{
    assert_blob(m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, s_settings, sizeof s_settings);
    assert_blob(m, OMNI_NS_OLD, OMNI_KEY_CLOCKCAL, s_clockcal, sizeof s_clockcal);
    assert_blob(m, OMNI_NS_OLD, OMNI_KEY_BATREC, s_batrec, sizeof s_batrec);
    TEST_ASSERT_EQUAL(0, m->old_ns_sets);
}

static void decode_shared(const kv_mem_t *m, omni_shared_t *sh)
{
    const kv_entry_t *e = kv_mem_find(m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_shared_decode(sh, e->val, e->n));
}

static void decode_local(const kv_mem_t *m, omni_local_t *lo)
{
    const kv_entry_t *e = kv_mem_find(m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_local_decode(lo, e->val, e->n, &k_lim));
}

static void assert_owner_shared(const kv_mem_t *m, uint8_t brightness)
{
    omni_shared_t sh;
    decode_shared(m, &sh);
    TEST_ASSERT_EQUAL_STRING("Columbus", omni_city_at(sh.city)->name);
    TEST_ASSERT_EQUAL_STRING("US", omni_city_at(sh.city)->country);
    TEST_ASSERT_EQUAL_UINT8(brightness, sh.brightness);
    TEST_ASSERT_EQUAL_UINT8(15, sh.sleep_s);
    TEST_ASSERT_FALSE(sh.h24);
    TEST_ASSERT_TRUE(sh.sound_on);
    TEST_ASSERT_EQUAL_UINT8(30, sh.volume);
    TEST_ASSERT_FALSE(sh.reduced_motion);
    TEST_ASSERT_EQUAL_UINT16(0, sh.tail_len);
}

static void assert_owner_local(const kv_mem_t *m, uint8_t reveal)
{
    omni_local_t lo;
    decode_local(m, &lo);
    const uint8_t car[OMNI_CAROUSEL_MAX] = { 0, 1, 2, 3, 7 };
    TEST_ASSERT_EQUAL_UINT8(reveal, lo.reveal_style);
    TEST_ASSERT_EQUAL_UINT8(5, lo.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(car, lo.carousel, OMNI_CAROUSEL_MAX);
    TEST_ASSERT_EQUAL_UINT8(0, lo.face_idx);
    TEST_ASSERT_EQUAL_UINT16(0, lo.tail_len);
}

static void test_migrate_omni_full(void)
{
    fixture(&s_m);
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.shared);
    assert_owner_local(&s_m, OMNI_REVEAL_FLIP);
    assert_owner_shared(&s_m, 45);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL, s_clockcal, sizeof s_clockcal);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_BATREC, s_batrec, sizeof s_batrec);
    assert_str(&s_m, OMNI_NS_WATCH, OMNI_KEY_THEME, "omni");
    assert_omni_unchanged(&s_m);
    /* five sets, in spec §6's order: watch/shared, the marker, last */
    TEST_ASSERT_EQUAL(5, s_m.sets);
    TEST_ASSERT_EQUAL(5, s_m.sets_ok);
    TEST_ASSERT_EQUAL_STRING("t_omni/local", s_m.log[0]);
    TEST_ASSERT_EQUAL_STRING("watch/clockcal", s_m.log[1]);
    TEST_ASSERT_EQUAL_STRING("watch/batrec", s_m.log[2]);
    TEST_ASSERT_EQUAL_STRING("watch/theme", s_m.log[3]);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[4]);
    TEST_ASSERT_EQUAL(8, s_m.count);
}

static void test_migrate_idempotent(void)
{
    fixture(&s_m);
    omni_boot(&s_m, false);
    fixture(&s_ref);
    omni_boot(&s_ref, false);
    kv_mem_reset_counts(&s_m);
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_NOT_NEEDED, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_NOT_NEEDED, r.shared);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_TRUE(kv_mem_equal(&s_m, &s_ref));
    /* the dw stub's boot after it: also nothing */
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_MIG_NOT_NEEDED, omni_migrate_shared(&kv, "dw", false));
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

/* Interrupted after each of the five writes (the power cut before the next commit), then a clean boot: the store
 * ends exactly as an uninterrupted migration leaves it. */
static void test_migrate_fault_each_step(void)
{
    fixture(&s_ref);
    omni_boot(&s_ref, false);
    for (int n = 0; n <= 4; n++) {
        char msg[32];
        snprintf(msg, sizeof msg, "fail after %d", n);
        fixture(&s_m);
        s_m.fail_after = n;
        omni_boot_t r = omni_boot(&s_m, false);
        TEST_ASSERT_EQUAL_MESSAGE(n == 0 ? OMNI_MIG_FAILED : OMNI_MIG_DONE, r.local, msg);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, r.shared, msg);
        TEST_ASSERT_EQUAL_MESSAGE(n, s_m.sets_ok, msg);
        TEST_ASSERT_NULL_MESSAGE(kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED), msg);   /* no marker */
        s_m.fail_after = -1;
        r = omni_boot(&s_m, false);
        TEST_ASSERT_EQUAL_MESSAGE(n == 0 ? OMNI_MIG_DONE : OMNI_MIG_NOT_NEEDED, r.local, msg);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_DONE, r.shared, msg);
        TEST_ASSERT_TRUE_MESSAGE(kv_mem_equal(&s_m, &s_ref), msg);
        assert_omni_unchanged(&s_m);
    }
}

static void test_migrate_no_source(void)
{
    kv_mem_free(&s_m);
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_NO_SOURCE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_NO_SOURCE, r.shared);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_EQUAL(0, s_m.count);
}

static void test_migrate_source_read_error(void)
{
    fixture(&s_m);
    s_m.fail_get_ns = OMNI_NS_OLD;
    s_m.fail_get_key = OMNI_KEY_SETTINGS;
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_FAILED, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_FAILED, r.shared);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    s_m.fail_get_ns = s_m.fail_get_key = NULL;
}

/* any read error is FAILED at once: the marker's, a device fact's */
static void test_migrate_other_read_errors(void)
{
    static const struct { const char *ns, *key; } K[] = {
        { OMNI_NS_WATCH, OMNI_KEY_SHARED }, { OMNI_NS_T_OMNI, OMNI_KEY_LOCAL }, { OMNI_NS_OLD, OMNI_KEY_CLOCKCAL },
        { OMNI_NS_WATCH, OMNI_KEY_BATREC },
    };
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) {
        fixture(&s_m);
        s_m.fail_get_ns = K[i].ns;
        s_m.fail_get_key = K[i].key;
        omni_kv_t kv = kv_mem_iface(&s_m);
        if (strcmp(K[i].ns, OMNI_NS_T_OMNI) == 0)
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, omni_migrate_omni_local(&kv, &k_lim, false), K[i].key);
        else
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, omni_migrate_shared(&kv, "omni", false), K[i].key);
        TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, K[i].key);
        s_m.fail_get_ns = s_m.fail_get_key = NULL;
    }
}

/* Stage 1's blob (28 B): its city is an index into Stage 1's table, resolved through cities_v1 (Los Angeles was 93
 * there; Longyearbyen, added since, sorts before it in today's table). */
static void test_migrate_v1_source(void)
{
    uint8_t b[OMNI_SETTINGS_V1_BLOB_SIZE];
    memset(b, 0, sizeof b);
    b[0] = 1;
    b[2] = 93;
    b[4] = 45; b[5] = 15; b[6] = 1; b[7] = 1; b[8] = 30; b[9] = 0; b[10] = OMNI_REVEAL_IRIS;
    const uint8_t car[] = { 3, 0, 5 };
    memcpy(&b[11], car, sizeof car);
    b[27] = sizeof car;
    kv_mem_free(&s_m);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, b, sizeof b));
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.shared);
    omni_shared_t sh;
    decode_shared(&s_m, &sh);
    TEST_ASSERT_EQUAL_STRING("Los Angeles", omni_city_at(sh.city)->name);
    TEST_ASSERT_EQUAL_STRING("US", omni_city_at(sh.city)->country);
    TEST_ASSERT_NOT_EQUAL(93, sh.city);
    TEST_ASSERT_EQUAL_UINT8(45, sh.brightness);
    TEST_ASSERT_TRUE(sh.h24);
    omni_local_t lo;
    decode_local(&s_m, &lo);
    TEST_ASSERT_EQUAL_UINT8(OMNI_REVEAL_IRIS, lo.reveal_style);
    TEST_ASSERT_EQUAL_UINT8(3, lo.carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(car, lo.carousel, 3);
    /* nothing to carry over: no watch/clockcal, no watch/batrec */
    TEST_ASSERT_NULL(kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL));
    TEST_ASSERT_NULL(kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_BATREC));
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
    assert_blob(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, b, sizeof b);
}

static void test_migrate_clockcal_present_in_watch(void)
{
    fixture(&s_m);
    uint8_t other[OMNI_CLK_BLOB_SIZE];
    memset(other, 0xab, sizeof other);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL, KV_T_BLOB, other, sizeof other));
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.shared);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL, other, sizeof other);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_BATREC, s_batrec, sizeof s_batrec);
    TEST_ASSERT_EQUAL(4, s_m.sets);   /* local, batrec, theme, shared */
    assert_omni_unchanged(&s_m);
}

static void test_migrate_force(void)
{
    fixture(&s_m);
    omni_boot(&s_m, false);
    /* omni/settings changed under an older build: brightness 60, reveal iris */
    omni_settings_t s;
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_LOADED, omni_settings_decode(&s, s_settings, sizeof s_settings));
    s.brightness = 60;
    s.reveal_style = OMNI_REVEAL_IRIS;
    uint8_t nb[OMNI_SETTINGS_BLOB_SIZE];
    omni_settings_encode(&s, nb);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, nb, sizeof nb));
    uint8_t other[OMNI_CLK_BLOB_SIZE];
    memset(other, 0x5a, sizeof other);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL, KV_T_BLOB, other, sizeof other));
    kv_mem_reset_counts(&s_m);

    omni_boot_t r = omni_boot(&s_m, false);   /* without force: the markers hold */
    TEST_ASSERT_EQUAL(OMNI_MIG_NOT_NEEDED, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_NOT_NEEDED, r.shared);
    TEST_ASSERT_EQUAL(0, s_m.sets);

    r = omni_boot(&s_m, true);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.shared);
    assert_owner_shared(&s_m, 60);
    assert_owner_local(&s_m, OMNI_REVEAL_IRIS);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL, other, sizeof other);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_BATREC, s_batrec, sizeof s_batrec);
    assert_str(&s_m, OMNI_NS_WATCH, OMNI_KEY_THEME, "omni");
    TEST_ASSERT_EQUAL(3, s_m.sets);   /* local, theme, shared */
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[2]);
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
    assert_blob(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, nb, sizeof nb);
}

/* O1: force keeps a newer shared record's appended tail (and, if present, its unknown city) */
static void keep_tail_case(bool unknown_city)
{
    fixture(&s_m);
    omni_boot(&s_m, false);
    omni_settings_t s;
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_LOADED, omni_settings_decode(&s, s_settings, sizeof s_settings));
    s.brightness = 60;
    uint8_t nb[OMNI_SETTINGS_BLOB_SIZE];
    omni_settings_encode(&s, nb);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, nb, sizeof nb));

    const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_EQUAL(46, e->n);
    uint8_t rec[56], tail[10] = { 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18, 0x29, 0x3a };
    memcpy(rec, e->val, 46);
    memcpy(&rec[46], tail, sizeof tail);
    rec[2] = 56; rec[3] = 0;   /* the header's size field */
    if (unknown_city) {
        memset(&rec[4], 0, OMNI_SHARED_CITY_BYTES);
        memcpy(&rec[4], "Atlantis", 8);
        memcpy(&rec[36], "AQ", 2);
    }
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED, KV_T_BLOB, rec, sizeof rec));

    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, omni_migrate_shared(&kv, "omni", true));
    e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_EQUAL(56, e->n);
    TEST_ASSERT_EQUAL_UINT8(60, e->val[40]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tail, &e->val[46], sizeof tail);
    if (unknown_city) TEST_ASSERT_EQUAL_UINT8_ARRAY(&rec[4], &e->val[4], OMNI_SHARED_CITY_BYTES);
}

static void test_migrate_force_keeps_tail(void) { keep_tail_case(false); }
static void test_migrate_force_keeps_tail_and_city(void) { keep_tail_case(true); }

/* No watch and no t_omni namespace at all (a watch before the first new boot): NOT_FOUND, not an error. */
static void test_migrate_absent_namespace(void)
{
    fixture(&s_m);
    for (int i = 0; i < s_m.count; i++) TEST_ASSERT_EQUAL_STRING(OMNI_NS_OLD, s_m.e[i].ns);
    omni_kv_t kv = kv_mem_iface(&s_m);
    uint8_t buf[8];
    size_t n = sizeof buf;
    TEST_ASSERT_EQUAL(OMNI_KV_NOT_FOUND, kv.get_blob(kv.ctx, OMNI_NS_WATCH, OMNI_KEY_SHARED, buf, &n));
    char str[8];
    TEST_ASSERT_EQUAL(OMNI_KV_NOT_FOUND, kv.get_str(kv.ctx, OMNI_NS_WATCH, OMNI_KEY_THEME, str, sizeof str));
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.shared);
}

static void test_kv_refuses_omni_set(void)
{
    fixture(&s_m);
    fixture(&s_ref);
    omni_kv_t kv = kv_mem_iface(&s_m);
    const uint8_t junk[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, kv.set_blob(kv.ctx, OMNI_NS_OLD, OMNI_KEY_SETTINGS, junk, sizeof junk));
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, kv.set_blob(kv.ctx, OMNI_NS_OLD, "newkey", junk, sizeof junk));
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, kv.set_str(kv.ctx, OMNI_NS_OLD, OMNI_KEY_THEME, "omni"));
    TEST_ASSERT_EQUAL(3, s_m.old_ns_sets);
    TEST_ASSERT_EQUAL(0, s_m.sets_ok);
    TEST_ASSERT_TRUE(kv_mem_equal(&s_m, &s_ref));
    /* and a value that does not fit the caller's buffer is ERR, as on the device */
    uint8_t small[8];
    size_t n = sizeof small;
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, kv.get_blob(kv.ctx, OMNI_NS_OLD, OMNI_KEY_SETTINGS, small, &n));
}

/* The dw stub boots first (shared only), then the Omni: its shared part finds the marker. */
static void test_migrate_dw_then_omni(void)
{
    fixture(&s_ref);
    omni_boot(&s_ref, false);
    fixture(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, omni_migrate_shared(&kv, "dw", false));
    TEST_ASSERT_EQUAL(4, s_m.sets);
    TEST_ASSERT_NULL(kv_mem_find(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL));
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_NOT_NEEDED, r.shared);
    const kv_entry_t *want = kv_mem_find(&s_ref, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED, want->val, want->n);
    assert_str(&s_m, OMNI_NS_WATCH, OMNI_KEY_THEME, "dw");
    assert_owner_local(&s_m, OMNI_REVEAL_FLIP);
    assert_omni_unchanged(&s_m);
}

/* omni/settings absent (never changed on that watch) but device facts to carry: the shared record is the defaults,
 * written with them, so the facts are not stranded behind a later marker. Under force, nothing (force never
 * overwrites a shared record with defaults). */
static void test_migrate_facts_without_settings(void)
{
    kv_mem_free(&s_m);
    load_values();
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_CLOCKCAL, KV_T_BLOB, s_clockcal, sizeof s_clockcal));
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_MIG_NO_SOURCE, omni_migrate_shared(&kv, "omni", true));
    TEST_ASSERT_EQUAL(OMNI_MIG_NO_SOURCE, omni_migrate_omni_local(&kv, &k_lim, true));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    omni_boot_t r = omni_boot(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_MIG_NO_SOURCE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.shared);
    assert_blob(&s_m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL, s_clockcal, sizeof s_clockcal);
    TEST_ASSERT_NULL(kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_BATREC));
    omni_shared_t sh, def;
    decode_shared(&s_m, &sh);
    omni_shared_defaults(&def);
    TEST_ASSERT_EQUAL_UINT16(def.city, sh.city);
    TEST_ASSERT_EQUAL_UINT8(def.brightness, sh.brightness);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[s_m.sets_ok - 1]);
}

/* A present but unusable omni/settings (a size no version has) is FAILED with 0 sets (ruling, R124 item 2: the boot
 * runs read-only), with or without device facts to carry, with or without force, in the Omni's and the dw stub's
 * sequence: marking the store migrated over an undecodable blob would make its loss permanent. */
static void test_migrate_unusable_settings(void)
{
    const uint8_t junk[10] = { 2, 0, 9, 9, 9, 9, 9, 9, 9, 9 };
    for (int facts = 0; facts <= 1; facts++) {
        for (int force = 0; force <= 1; force++) {
            char msg[32];
            snprintf(msg, sizeof msg, "facts %d force %d", facts, force);
            kv_mem_free(&s_m);
            load_values();
            TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, junk, sizeof junk));
            if (facts) {
                TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_CLOCKCAL, KV_T_BLOB, s_clockcal,
                                                sizeof s_clockcal));
                TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_BATREC, KV_T_BLOB, s_batrec,
                                                sizeof s_batrec));
            }
            omni_boot_t r = omni_boot(&s_m, force);
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, r.local, msg);
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, r.shared, msg);
            omni_kv_t kv = kv_mem_iface(&s_m);
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, omni_migrate_shared(&kv, "dw", force), msg);
            TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, msg);
            for (int i = 0; i < s_m.count; i++) TEST_ASSERT_EQUAL_STRING_MESSAGE(OMNI_NS_OLD, s_m.e[i].ns, msg);
        }
    }
}

/* The dw stub's sequence (clockcal, batrec, theme, shared) interrupted after each write, then a clean boot. */
static void test_migrate_fault_dw(void)
{
    fixture(&s_ref);
    omni_kv_t kv = kv_mem_iface(&s_ref);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, omni_migrate_shared(&kv, "dw", false));
    TEST_ASSERT_EQUAL(4, s_ref.sets);
    for (int n = 0; n <= 3; n++) {
        char msg[32];
        snprintf(msg, sizeof msg, "fail after %d", n);
        fixture(&s_m);
        s_m.fail_after = n;
        kv = kv_mem_iface(&s_m);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, omni_migrate_shared(&kv, "dw", false), msg);
        TEST_ASSERT_EQUAL_MESSAGE(n, s_m.sets_ok, msg);
        TEST_ASSERT_NULL_MESSAGE(kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED), msg);
        s_m.fail_after = -1;
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_DONE, omni_migrate_shared(&kv, "dw", false), msg);
        TEST_ASSERT_TRUE_MESSAGE(kv_mem_equal(&s_m, &s_ref), msg);
        assert_omni_unchanged(&s_m);
    }
}

/* A migrated store whose omni/settings changed under an older build (brightness 60, reveal iris) and whose
 * watch/clockcal and watch/batrec are gone, so force has all five writes to do. */
static void force_setup(kv_mem_t *m)
{
    fixture(m);
    omni_boot(m, false);
    omni_settings_t s;
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_LOADED, omni_settings_decode(&s, s_settings, sizeof s_settings));
    s.brightness = 60;
    s.reveal_style = OMNI_REVEAL_IRIS;
    uint8_t nb[OMNI_SETTINGS_BLOB_SIZE];
    omni_settings_encode(&s, nb);
    TEST_ASSERT_EQUAL(0, kv_mem_put(m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, nb, sizeof nb));
    TEST_ASSERT_EQUAL(0, kv_mem_del(m, OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL));
    TEST_ASSERT_EQUAL(0, kv_mem_del(m, OMNI_NS_WATCH, OMNI_KEY_BATREC));
    kv_mem_reset_counts(m);
}

/* Force interrupted after each of its five writes. The old marker still stands, so a plain boot does not repair it
 * (NOT_NEEDED); running force again does, ending exactly as an uninterrupted force (migrate.h). */
static void test_migrate_fault_force(void)
{
    force_setup(&s_ref);
    omni_boot_t r = omni_boot(&s_ref, true);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.local);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, r.shared);
    TEST_ASSERT_EQUAL(5, s_ref.sets);
    assert_owner_shared(&s_ref, 60);
    for (int n = 0; n <= 4; n++) {
        char msg[32];
        snprintf(msg, sizeof msg, "fail after %d", n);
        force_setup(&s_m);
        s_m.fail_after = n;
        r = omni_boot(&s_m, true);
        TEST_ASSERT_EQUAL_MESSAGE(n == 0 ? OMNI_MIG_FAILED : OMNI_MIG_DONE, r.local, msg);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, r.shared, msg);
        TEST_ASSERT_EQUAL_MESSAGE(n, s_m.sets_ok, msg);
        s_m.fail_after = -1;
        kv_mem_reset_counts(&s_m);
        r = omni_boot(&s_m, false);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_NOT_NEEDED, r.shared, msg);
        TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, msg);
        r = omni_boot(&s_m, true);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_DONE, r.local, msg);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_DONE, r.shared, msg);
        TEST_ASSERT_TRUE_MESSAGE(kv_mem_equal(&s_m, &s_ref), msg);
        TEST_ASSERT_EQUAL_MESSAGE(0, s_m.old_ns_sets, msg);
    }
}

/* store.c's fact_load and its writability check: a get into an exact-size buffer, then the policy */
static bool fact_writable_in(kv_mem_t *m, const char *key, size_t z, uint16_t ver)
{
    omni_kv_t kv = kv_mem_iface(m);
    uint8_t buf[OMNI_MIG_READ_MAX];
    size_t n = z;
    omni_kv_rc_t rc = kv.get_blob(kv.ctx, OMNI_NS_WATCH, key, buf, &n);
    return omni_fact_writable(rc, buf, n, z, ver);
}

/* Final review m3: a device fact of another version or size, or a read error, is not this build's to write (an older
 * firmware never overwrites a newer clockcal or batrec); absent, or exactly this build's format, is. */
static void test_fact_writable(void)
{
    load_values();
    static const struct { const char *key; const uint8_t *blob; size_t size; uint16_t ver; } F[] = {
        { OMNI_KEY_CLOCKCAL, s_clockcal, OMNI_CLK_BLOB_SIZE, OMNI_CLK_BLOB_VERSION },
        { OMNI_KEY_BATREC, s_batrec, OMNI_BATREC_BLOB_SIZE, OMNI_BATREC_VERSION },
    };
    for (size_t i = 0; i < 2; i++) {
        uint8_t big[OMNI_MIG_READ_MAX];
        const size_t z = F[i].size;
        const char *k = F[i].key;
        kv_mem_free(&s_m);
        TEST_ASSERT_TRUE_MESSAGE(fact_writable_in(&s_m, k, z, F[i].ver), k);            /* absent */
        TEST_ASSERT_EQUAL(F[i].ver, (uint16_t)(F[i].blob[0] | F[i].blob[1] << 8));
        TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, k, KV_T_BLOB, F[i].blob, z));
        TEST_ASSERT_TRUE_MESSAGE(fact_writable_in(&s_m, k, z, F[i].ver), k);            /* this build's format */
        memcpy(big, F[i].blob, z);
        big[0] = (uint8_t)(F[i].ver + 1);
        TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, k, KV_T_BLOB, big, z));
        TEST_ASSERT_FALSE_MESSAGE(fact_writable_in(&s_m, k, z, F[i].ver), k);           /* a newer version */
        memcpy(big, F[i].blob, z);
        memset(big + z, 0xA5, 8);
        TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, k, KV_T_BLOB, big, z + 8));
        TEST_ASSERT_FALSE_MESSAGE(fact_writable_in(&s_m, k, z, F[i].ver), k);           /* grown: the get is ERR */
        TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, k, KV_T_BLOB, F[i].blob, z - 2));
        TEST_ASSERT_FALSE_MESSAGE(fact_writable_in(&s_m, k, z, F[i].ver), k);           /* shrunk */
        TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, k, KV_T_STR, (const uint8_t *)"x", 1));
        TEST_ASSERT_FALSE_MESSAGE(fact_writable_in(&s_m, k, z, F[i].ver), k);           /* another type: ERR */
        TEST_ASSERT_FALSE(omni_fact_writable(OMNI_KV_ERR, F[i].blob, z, z, F[i].ver));  /* any read error */
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_migrate_omni_full);
    RUN_TEST(test_migrate_idempotent);
    RUN_TEST(test_migrate_fault_each_step);
    RUN_TEST(test_migrate_no_source);
    RUN_TEST(test_migrate_source_read_error);
    RUN_TEST(test_migrate_other_read_errors);
    RUN_TEST(test_migrate_v1_source);
    RUN_TEST(test_migrate_clockcal_present_in_watch);
    RUN_TEST(test_migrate_force);
    RUN_TEST(test_migrate_force_keeps_tail);
    RUN_TEST(test_migrate_force_keeps_tail_and_city);
    RUN_TEST(test_migrate_absent_namespace);
    RUN_TEST(test_kv_refuses_omni_set);
    RUN_TEST(test_migrate_dw_then_omni);
    RUN_TEST(test_migrate_facts_without_settings);
    RUN_TEST(test_migrate_unusable_settings);
    RUN_TEST(test_migrate_fault_dw);
    RUN_TEST(test_migrate_fault_force);
    RUN_TEST(test_fact_writable);
    kv_mem_free(&s_m);
    kv_mem_free(&s_ref);
    return UNITY_END();
}
