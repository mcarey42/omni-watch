/* The boot and save policy (omni/boot.h) over the in-memory kv (kv_mem.h), which behaves as the device adapter must:
 * an absent namespace is NOT_FOUND, sets on `omni` are refused. The values are the golden fixture's (nvs/), the same
 * as test_migrate's. */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "kv_mem.h"
#include "omni/boot.h"
#include "omni/cities.h"
#include "omni/clockcal.h"
#include "omni/batrec.h"
#include "omni/local.h"
#include "omni/migrate.h"
#include "omni/settings.h"
#include "omni/shared.h"

void setUp(void) {}
void tearDown(void) {}

/* the Omni theme's limits (apps/omni/main/omni_theme.c): 8 faces, 3 reveals, the default order */
static const uint8_t k_def[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const omni_theme_limits_t k_lim = { 8, 3, 0, sizeof k_def, k_def };

static kv_mem_t s_m, s_ref;   /* static: KV_MEM_MAX entries are too large for the stack under ASan */
static omni_boot_t s_b;
static uint8_t s_settings[OMNI_SETTINGS_BLOB_SIZE], s_clockcal[OMNI_CLK_BLOB_SIZE], s_batrec[OMNI_BATREC_BLOB_SIZE];

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

/* the Stage 3 store: omni/settings, omni/clockcal, omni/batrec; no watch, no t_omni namespace */
static void fixture(kv_mem_t *m)
{
    kv_mem_free(m);
    TEST_ASSERT_EQUAL(sizeof s_settings, read_hex("settings.hex", s_settings, sizeof s_settings));
    TEST_ASSERT_EQUAL(sizeof s_clockcal, read_hex("clockcal.hex", s_clockcal, sizeof s_clockcal));
    TEST_ASSERT_EQUAL(sizeof s_batrec, read_hex("batrec.hex", s_batrec, sizeof s_batrec));
    TEST_ASSERT_EQUAL(0, kv_mem_put(m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, s_settings, sizeof s_settings));
    TEST_ASSERT_EQUAL(0, kv_mem_put(m, OMNI_NS_OLD, OMNI_KEY_CLOCKCAL, KV_T_BLOB, s_clockcal, sizeof s_clockcal));
    TEST_ASSERT_EQUAL(0, kv_mem_put(m, OMNI_NS_OLD, OMNI_KEY_BATREC, KV_T_BLOB, s_batrec, sizeof s_batrec));
}

/* test_migrate_omni_full's store: the fixture after the Omni's two migration parts */
static void reference(kv_mem_t *m)
{
    fixture(m);
    omni_kv_t kv = kv_mem_iface(m);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, omni_migrate_omni_local(&kv, &k_lim, false));
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, omni_migrate_shared(&kv, "omni", false));
    kv_mem_reset_counts(m);
}

static void boot(kv_mem_t *m)
{
    omni_kv_t kv = kv_mem_iface(m);
    omni_boot_run(&s_b, &kv, "omni", &k_lim, true, false);
}

static void assert_owner_view(const omni_settings_t *s, uint8_t brightness, uint8_t reveal)
{
    TEST_ASSERT_EQUAL_STRING("Columbus", omni_city_at(s->city)->name);
    TEST_ASSERT_EQUAL_STRING("US", omni_city_at(s->city)->country);
    TEST_ASSERT_EQUAL_UINT8(brightness, s->brightness);
    TEST_ASSERT_EQUAL_UINT8(15, s->sleep_s);
    TEST_ASSERT_FALSE(s->h24);
    TEST_ASSERT_TRUE(s->sound_on);
    TEST_ASSERT_EQUAL_UINT8(30, s->volume);
    TEST_ASSERT_FALSE(s->reduced_motion);
    TEST_ASSERT_EQUAL_UINT8(reveal, s->reveal_style);
    const uint8_t car[OMNI_CAROUSEL_MAX] = { 0, 1, 2, 3, 7 };
    TEST_ASSERT_EQUAL_UINT8(5, s->carousel_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(car, s->carousel, OMNI_CAROUSEL_MAX);
}

static void assert_default_view(const omni_settings_t *s)
{
    omni_shared_t sh;
    omni_local_t lo;
    omni_settings_t d;
    omni_shared_defaults(&sh);
    omni_local_defaults(&lo, &k_lim);
    omni_settings_join(&sh, &lo, &d);
    TEST_ASSERT_EQUAL_MEMORY(&d, s, sizeof d);
}

static void assert_str(const kv_mem_t *m, const char *ns, const char *key, const char *want)
{
    const kv_entry_t *e = kv_mem_find(m, ns, key);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, key);
    TEST_ASSERT_EQUAL_MESSAGE(KV_T_STR, e->type, key);
    TEST_ASSERT_EQUAL_MESSAGE(strlen(want), e->n, key);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, e->val, e->n, key);
}

static void test_boot_first_migration(void)
{
    reference(&s_ref);
    fixture(&s_m);
    boot(&s_m);
    char line[64], buf[64];
    TEST_ASSERT_EQUAL_STRING("", s_b.prev);
    TEST_ASSERT_EQUAL_STRING("theme omni (was none)", omni_boot_theme_line(&s_b, "omni", line, sizeof line));
    TEST_ASSERT_FALSE(omni_boot_swapped(&s_b, "omni"));
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_FALSE(s_b.shared_ro);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, s_b.mig_local);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, s_b.mig_shared);
    TEST_ASSERT_TRUE(kv_mem_equal(&s_m, &s_ref));
    TEST_ASSERT_EQUAL(5, s_m.sets);   /* the migration's own: watch/theme already says omni, t_omni/local exists */
    assert_owner_view(&s_b.s, 45, OMNI_REVEAL_FLIP);
    TEST_ASSERT_EQUAL_STRING("migrated from omni v2", omni_boot_origin(&s_b, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("local done, shared done", omni_boot_migration(&s_b, buf, sizeof buf));

    /* the next boot: nothing to do, nothing written; the records as loaded */
    kv_mem_reset_counts(&s_m);
    boot(&s_m);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("omni", s_b.prev);
    TEST_ASSERT_EQUAL_STRING("theme omni (was omni)", omni_boot_theme_line(&s_b, "omni", line, sizeof line));
    TEST_ASSERT_FALSE(omni_boot_swapped(&s_b, "omni"));
    TEST_ASSERT_EQUAL_STRING("watch v1", omni_boot_origin(&s_b, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("not needed", omni_boot_migration(&s_b, buf, sizeof buf));
    assert_owner_view(&s_b.s, 45, OMNI_REVEAL_FLIP);
}

/* R124 item 2 (preflight B16): a FAILED part makes the boot read-only on omni/settings as decoded; a save then writes
 * nothing (no marker over the owner's settings), and a clean boot later migrates exactly as an uninterrupted one. */
static void test_boot_failed_migration_read_only(void)
{
    reference(&s_ref);
    static const struct { int fail_after; const char *what; } F[] = {
        { 0, "a set failing in the local part" },
        { 2, "a set failing in the shared part after watch/clockcal" },
    };
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        fixture(&s_m);
        s_m.fail_after = F[i].fail_after;
        boot(&s_m);
        TEST_ASSERT_TRUE_MESSAGE(s_b.no_write, F[i].what);
        if (F[i].fail_after == 0) {   /* the local part FAILED: the shared part never ran (review m2) */
            TEST_ASSERT_TRUE_MESSAGE(s_b.shared_skipped, F[i].what);
            TEST_ASSERT_EQUAL_MESSAGE(1, s_m.sets, F[i].what);   /* the one failed local set, nothing after it */
        } else {
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_MIG_FAILED, s_b.mig_shared, F[i].what);
        }
        TEST_ASSERT_NULL_MESSAGE(kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED), F[i].what);
        TEST_ASSERT_EQUAL_MESSAGE(F[i].fail_after, s_m.sets_ok, F[i].what);   /* the migration's writes only */
        assert_owner_view(&s_b.s, 45, OMNI_REVEAL_FLIP);
        char buf[64];
        TEST_ASSERT_EQUAL_STRING_MESSAGE("read-only: migration failed", omni_boot_origin(&s_b, buf, sizeof buf),
                                         F[i].what);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(F[i].fail_after == 0 ? "failed: local (shared skipped)" : "failed: shared",
                                         omni_boot_migration(&s_b, buf, sizeof buf), F[i].what);

        /* the fault cleared: a save with a changed brightness (and a face) still writes nothing this boot */
        s_m.fail_after = -1;
        kv_mem_reset_counts(&s_m);
        omni_settings_t s = s_b.s;
        s.brightness = 80;
        s_b.lo.face_idx = 2;
        omni_kv_t kv = kv_mem_iface(&s_m);
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s), F[i].what);
        TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, F[i].what);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(80, s_b.s.brightness, F[i].what);   /* applied in RAM */
        /* and force is refused: 0 sets */
        TEST_ASSERT_EQUAL_MESSAGE(OMNI_KV_ERR, omni_boot_force(&s_b, &kv, "omni", &k_lim, true), F[i].what);
        TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, F[i].what);

        /* the next boot, clean: the full migration's store */
        boot(&s_m);
        TEST_ASSERT_FALSE_MESSAGE(s_b.no_write, F[i].what);
        TEST_ASSERT_TRUE_MESSAGE(kv_mem_equal(&s_m, &s_ref), F[i].what);
        assert_owner_view(&s_b.s, 45, OMNI_REVEAL_FLIP);
    }
}

static void test_boot_source_unreadable(void)
{
    fixture(&s_m);
    s_m.fail_get_ns = OMNI_NS_OLD;
    s_m.fail_get_key = OMNI_KEY_SETTINGS;
    boot(&s_m);
    TEST_ASSERT_TRUE(s_b.no_write);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    assert_default_view(&s_b.s);
    omni_settings_t s = s_b.s;
    s.brightness = 90;
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_save(&s_b, &kv, &s);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    s_m.fail_get_ns = s_m.fail_get_key = NULL;
}

/* A read-only boot, then a save and a force: 0 sets in all, the view in RAM. */
static void assert_read_only_boot(const char *msg)
{
    TEST_ASSERT_TRUE_MESSAGE(s_b.no_write, msg);
    TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, msg);
    omni_settings_t s = s_b.s;
    s.brightness = 81;
    s.reveal_style = OMNI_REVEAL_IRIS;
    s_b.lo.face_idx = 1;
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL_MESSAGE(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s), msg);
    TEST_ASSERT_EQUAL_MESSAGE(OMNI_KV_ERR, omni_boot_force(&s_b, &kv, "omni", &k_lim, true), msg);
    TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, msg);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(81, s_b.s.brightness, msg);
}

/* Review m2: the local part FAILED (here a read error on t_omni/local, so the shared part would succeed): the shared
 * part is skipped and the boot writes nothing at all, not even watch/clockcal or the marker. */
static void test_boot_local_failed_skips_shared(void)
{
    fixture(&s_m);
    s_m.fail_get_ns = OMNI_NS_T_OMNI;
    s_m.fail_get_key = OMNI_KEY_LOCAL;
    boot(&s_m);
    TEST_ASSERT_EQUAL(OMNI_MIG_FAILED, s_b.mig_local);
    TEST_ASSERT_TRUE(s_b.shared_skipped);
    char buf[64];
    TEST_ASSERT_EQUAL_STRING("failed: local (shared skipped)", omni_boot_migration(&s_b, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("read-only: migration failed", omni_boot_origin(&s_b, buf, sizeof buf));
    TEST_ASSERT_EQUAL(3, s_m.count);   /* still the three omni entries only */
    assert_owner_view(&s_b.s, 45, OMNI_REVEAL_FLIP);
    assert_read_only_boot("local get ERR");
    s_m.fail_get_ns = s_m.fail_get_key = NULL;
    /* cleared: the next boot migrates fully */
    reference(&s_ref);
    boot(&s_m);
    TEST_ASSERT_TRUE(kv_mem_equal(&s_m, &s_ref));
}

/* Review m3: a read error on watch/shared (a migrated store): the shared part's probe FAILS, so the boot is read-only
 * and nothing, the record included, is written by a save or by force. */
static void test_boot_shared_read_error(void)
{
    reference(&s_m);
    s_m.fail_get_ns = OMNI_NS_WATCH;
    s_m.fail_get_key = OMNI_KEY_SHARED;
    boot(&s_m);
    assert_read_only_boot("shared get ERR");
    s_m.fail_get_ns = s_m.fail_get_key = NULL;
}

/* Review m3: a read error on the theme's local record where no migration probes it (a theme without the legacy part):
 * local_ro, so a save writes watch/shared only and force is refused. */
static void test_boot_local_read_error_not_legacy(void)
{
    fixture(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_lim, false, false);   /* migrated, t_dw/local written */
    TEST_ASSERT_NOT_NULL(kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL));
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    uint8_t before[OMNI_REC_MAX];
    size_t n = e->n;
    memcpy(before, e->val, n);
    s_m.fail_get_ns = "t_dw";
    s_m.fail_get_key = OMNI_KEY_LOCAL;
    kv_mem_reset_counts(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_lim, false, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_TRUE(s_b.local_ro);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    char buf[64];
    TEST_ASSERT_EQUAL_STRING("read-only: t_dw/local unreadable", omni_boot_origin(&s_b, buf, sizeof buf));
    omni_settings_t s = s_b.s;
    s.brightness = 81;
    s.reveal_style = OMNI_REVEAL_IRIS;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[0]);
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, omni_boot_force(&s_b, &kv, "dw", &k_lim, false));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    s_m.fail_get_ns = s_m.fail_get_key = NULL;
    e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    TEST_ASSERT_EQUAL(n, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, e->val, n);
}

/* Review m3: a record of 257..512 B (longer than this build's 256 B cap, within the migration's 512 B probe) reads as
 * ERR: read-only, never written by a save or by force; the other record still saves. */
static void test_boot_oversize_records(void)
{
    static const size_t N[] = { OMNI_REC_MAX + 1, OMNI_MIG_READ_MAX };
    static const char *const NS[] = { OMNI_NS_WATCH, OMNI_NS_T_OMNI };
    static const char *const KEY[] = { OMNI_KEY_SHARED, OMNI_KEY_LOCAL };
    for (size_t r = 0; r < 2; r++) {
        for (size_t i = 0; i < 2; i++) {
            char msg[48];
            snprintf(msg, sizeof msg, "%s/%s %u B", NS[r], KEY[r], (unsigned)N[i]);
            reference(&s_m);
            uint8_t big[OMNI_MIG_READ_MAX];
            memset(big, 0x5a, sizeof big);
            big[0] = 1; big[1] = 0; big[2] = (uint8_t)(N[i] & 0xff); big[3] = (uint8_t)(N[i] >> 8);
            TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, NS[r], KEY[r], KV_T_BLOB, big, N[i]));
            boot(&s_m);
            TEST_ASSERT_FALSE_MESSAGE(s_b.no_write, msg);
            TEST_ASSERT_TRUE_MESSAGE(r == 0 ? s_b.shared_ro : s_b.local_ro, msg);
            TEST_ASSERT_EQUAL_MESSAGE(0, s_m.sets, msg);
            omni_settings_t s = s_b.s;
            s.brightness = 81;
            s.reveal_style = OMNI_REVEAL_IRIS;
            omni_kv_t kv = kv_mem_iface(&s_m);
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s), msg);
            TEST_ASSERT_EQUAL_MESSAGE(1, s_m.sets, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(r == 0 ? "t_omni/local" : "watch/shared", s_m.log[0], msg);
            TEST_ASSERT_EQUAL_MESSAGE(OMNI_KV_ERR, omni_boot_force(&s_b, &kv, "omni", &k_lim, true), msg);
            TEST_ASSERT_EQUAL_MESSAGE(1, s_m.sets, msg);
            const kv_entry_t *e = kv_mem_find(&s_m, NS[r], KEY[r]);
            TEST_ASSERT_EQUAL_MESSAGE(N[i], e->n, msg);
            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(big, e->val, N[i], msg);
        }
    }
}

/* Review m5: a record key holding another type (a string) on a migrated store is never taken as absent (which would
 * migrate and write over it): the adapter reports ERR, so the migration's probe FAILS and the boot writes nothing. */
static void test_boot_mistyped_record(void)
{
    static const char *const NS[] = { OMNI_NS_WATCH, OMNI_NS_T_OMNI };
    static const char *const KEY[] = { OMNI_KEY_SHARED, OMNI_KEY_LOCAL };
    for (size_t r = 0; r < 2; r++) {
        reference(&s_m);
        TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, NS[r], KEY[r], KV_T_STR, (const uint8_t *)"x", 1));
        boot(&s_m);
        assert_read_only_boot(KEY[r]);
        const kv_entry_t *e = kv_mem_find(&s_m, NS[r], KEY[r]);
        TEST_ASSERT_EQUAL_MESSAGE(KV_T_STR, e->type, KEY[r]);
    }
}

/* nvs_flash_init failed: nothing read, nothing written, the defaults */
static void test_boot_nvs_down(void)
{
    fixture(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "omni", &k_lim, true, true);
    TEST_ASSERT_TRUE(s_b.no_write);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    assert_default_view(&s_b.s);
    char buf[64];
    TEST_ASSERT_EQUAL_STRING("defaults (NVS not read)", omni_boot_origin(&s_b, buf, sizeof buf));
    omni_settings_t s = s_b.s;
    s.brightness = 90;
    omni_boot_save(&s_b, &kv, &s);
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

/* Task 1's ruling: a watch/shared this build cannot read as its own (a newer version, unusable bytes) is never written:
 * a save writes t_omni/local only, and force is refused. */
static void test_boot_unknown_shared_version(void)
{
    static const struct { uint8_t ver; int size_delta; const char *origin; } K[] = {
        { 2, 0, "read-only: watch/shared v2" },
        { 1, 1, "read-only: watch/shared unusable" },   /* the size field lies */
    };
    for (size_t i = 0; i < sizeof K / sizeof K[0]; i++) {
        reference(&s_m);
        const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
        uint8_t rec[OMNI_REC_MAX];
        size_t n = e->n;
        memcpy(rec, e->val, n);
        rec[0] = K[i].ver;
        rec[2] = (uint8_t)(rec[2] + K[i].size_delta);
        TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED, KV_T_BLOB, rec, n));
        boot(&s_m);
        TEST_ASSERT_TRUE_MESSAGE(s_b.shared_ro, K[i].origin);
        TEST_ASSERT_FALSE_MESSAGE(s_b.no_write, K[i].origin);
        char buf[64];
        TEST_ASSERT_EQUAL_STRING(K[i].origin, omni_boot_origin(&s_b, buf, sizeof buf));
        TEST_ASSERT_EQUAL(0, s_m.sets);

        omni_settings_t s = s_b.s;
        s.brightness = 80;                    /* shared: RAM only */
        s.reveal_style = OMNI_REVEAL_IRIS;    /* local: written */
        omni_kv_t kv = kv_mem_iface(&s_m);
        TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
        TEST_ASSERT_EQUAL(1, s_m.sets);
        TEST_ASSERT_EQUAL_STRING("t_omni/local", s_m.log[0]);
        TEST_ASSERT_EQUAL_UINT8(80, s_b.s.brightness);
        e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
        TEST_ASSERT_EQUAL(n, e->n);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, e->val, n);

        /* force respects it: refused, 0 sets, the newer record untouched */
        kv_mem_reset_counts(&s_m);
        TEST_ASSERT_EQUAL(OMNI_KV_ERR, omni_boot_force(&s_b, &kv, "omni", &k_lim, true));
        TEST_ASSERT_EQUAL(0, s_m.sets);
        e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, e->val, n);
        TEST_ASSERT_TRUE(s_b.shared_ro);
    }
}

/* The same for t_<id>/local: a newer local record is never written, by a save or by force. */
static void test_boot_unknown_local_version(void)
{
    reference(&s_m);
    const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL);
    uint8_t rec[OMNI_REC_MAX];
    size_t n = e->n;
    memcpy(rec, e->val, n);
    rec[0] = 2;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
    boot(&s_m);
    TEST_ASSERT_TRUE(s_b.local_ro);
    TEST_ASSERT_FALSE(s_b.shared_ro);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    omni_settings_t s = s_b.s;
    s.brightness = 80;
    s.reveal_style = OMNI_REVEAL_IRIS;
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[0]);
    e = kv_mem_find(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, e->val, n);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, omni_boot_force(&s_b, &kv, "omni", &k_lim, true));
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

static void test_boot_force(void)
{
    reference(&s_m);
    boot(&s_m);
    /* omni/settings changed under an older build: brightness 60, reveal iris */
    omni_settings_t s;
    TEST_ASSERT_EQUAL(OMNI_SETTINGS_LOADED, omni_settings_decode(&s, s_settings, sizeof s_settings));
    s.brightness = 60;
    s.reveal_style = OMNI_REVEAL_IRIS;
    uint8_t nb[OMNI_SETTINGS_BLOB_SIZE];
    omni_settings_encode(&s, nb);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, nb, sizeof nb));
    kv_mem_reset_counts(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_force(&s_b, &kv, "omni", &k_lim, true));
    TEST_ASSERT_EQUAL(3, s_m.sets);   /* t_omni/local, watch/theme, watch/shared */
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
    assert_owner_view(&s_b.s, 60, OMNI_REVEAL_IRIS);
    char buf[64];
    TEST_ASSERT_EQUAL_STRING("forced: local done, shared done", omni_boot_migration(&s_b, buf, sizeof buf));
    /* the reloaded view is what is stored: a save of it writes nothing */
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s_b.s));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    /* and a boot after it reads the same */
    boot(&s_m);
    assert_owner_view(&s_b.s, 60, OMNI_REVEAL_IRIS);
    TEST_ASSERT_EQUAL(0, s_m.sets);

    /* with no_write, refused with 0 sets */
    s_b.no_write = true;
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, omni_boot_force(&s_b, &kv, "omni", &k_lim, true));
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

/* Final review I1: watch/shared names a city this build's table does not know (one a newer theme added). The view
 * shows the default city, but no save and no force ever replaces the stored city bytes with the default's. */
static void test_boot_unknown_city_kept(void)
{
    reference(&s_m);
    const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    uint8_t rec[OMNI_REC_MAX];
    size_t n = e->n;
    memcpy(rec, e->val, n);
    memset(&rec[4], 0, OMNI_SHARED_CITY_BYTES);
    memcpy(&rec[4], "Atlantis", 8);
    memcpy(&rec[36], "AQ", 2);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED, KV_T_BLOB, rec, n));
    kv_mem_reset_counts(&s_m);
    boot(&s_m);
    TEST_ASSERT_FALSE(s_b.shared_ro);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("London", omni_city_at(s_b.s.city)->name);   /* shown: the default city */
    char buf[64];
    TEST_ASSERT_EQUAL_STRING("watch v1, city unknown: kept, default shown", omni_boot_origin(&s_b, buf, sizeof buf));
    omni_kv_t kv = kv_mem_iface(&s_m);

    /* a face commit (local only): watch/shared not written */
    s_b.lo.face_idx = 3;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s_b.s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("t_omni/local", s_m.log[0]);
    e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_EQUAL(n, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, e->val, n);

    /* another shared field: written, with the city bytes identical */
    omni_settings_t s = s_b.s;
    s.brightness = 80;
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[0]);
    e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_EQUAL(n, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&rec[4], &e->val[4], OMNI_SHARED_CITY_BYTES);
    TEST_ASSERT_EQUAL_UINT8(80, e->val[40]);
    rec[40] = 80;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, e->val, n);
    /* a boot after it still keeps it */
    boot(&s_m);
    TEST_ASSERT_EQUAL(OMNI_REC_CITY_UNKNOWN, s_b.shared_load);
    TEST_ASSERT_EQUAL_UINT8(80, s_b.s.brightness);

    /* force: omni/settings re-imported (brightness back to 45), the stored city bytes identical */
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_force(&s_b, &kv, "omni", &k_lim, true));
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
    e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_EQUAL(n, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&rec[4], &e->val[4], OMNI_SHARED_CITY_BYTES);
    TEST_ASSERT_EQUAL_UINT8(45, e->val[40]);
    TEST_ASSERT_EQUAL_UINT8(45, s_b.s.brightness);
    TEST_ASSERT_EQUAL(OMNI_REC_CITY_UNKNOWN, s_b.shared_load);
    /* the forced view saves to the same bytes */
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s_b.s));
    TEST_ASSERT_EQUAL(0, s_m.sets);

    /* choosing a city in RAM replaces it */
    s = s_b.s;
    s.city = (uint16_t)omni_city_find("Columbus");
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    boot(&s_m);
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, s_b.shared_load);
    TEST_ASSERT_EQUAL_STRING("Columbus", omni_city_at(s_b.s.city)->name);
}

/* N1: boot an "Atlantis" watch/shared (a city this build does not know) and return its stored bytes. */
static size_t atlantis_boot(uint8_t *rec)
{
    reference(&s_m);
    const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    size_t n = e->n;
    memcpy(rec, e->val, n);
    memset(&rec[4], 0, OMNI_SHARED_CITY_BYTES);
    memcpy(&rec[4], "Atlantis", 8);
    memcpy(&rec[36], "AQ", 2);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED, KV_T_BLOB, rec, n));
    kv_mem_reset_counts(&s_m);
    boot(&s_m);
    TEST_ASSERT_EQUAL(OMNI_REC_CITY_UNKNOWN, s_b.shared_load);
    return n;
}

static void test_boot_save_unknown_city_then_default(void)
{
    uint8_t rec[OMNI_REC_MAX];
    size_t n = atlantis_boot(rec);
    omni_kv_t kv = kv_mem_iface(&s_m);
    const uint16_t def = s_b.s.city;   /* the default city is what the view shows */

    omni_settings_t s = s_b.s;
    s.city = (uint16_t)omni_city_find("Columbus");
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    s = s_b.s;
    s.city = def;
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(1, s_m.sets);   /* the second save wrote the default city */
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[0]);
    const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_EQUAL(n, e->n);
    omni_shared_t sh;
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, omni_shared_decode(&sh, e->val, e->n));
    TEST_ASSERT_EQUAL_UINT16(def, sh.city);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(&rec[4], &e->val[4], 8));
}

static void test_boot_save_unknown_city_untouched(void)
{
    uint8_t rec[OMNI_REC_MAX];
    size_t n = atlantis_boot(rec);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_settings_t s = s_b.s;   /* the city unchanged, another field changed */
    s.brightness = 80;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    TEST_ASSERT_EQUAL(n, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&rec[4], &e->val[4], OMNI_SHARED_CITY_BYTES);
}

/* A store migrated by the dw stub, booted as the Omni: the swap is seen and watch/theme follows. */
static void test_boot_swap(void)
{
    fixture(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, omni_migrate_shared(&kv, "dw", false));
    kv_mem_reset_counts(&s_m);
    boot(&s_m);
    char line[64];
    TEST_ASSERT_EQUAL_STRING("dw", s_b.prev);
    assert_str(&s_m, OMNI_NS_WATCH, OMNI_KEY_THEME, "omni");
    TEST_ASSERT_TRUE(omni_boot_swapped(&s_b, "omni"));
    TEST_ASSERT_EQUAL_STRING("theme omni (was dw)", omni_boot_theme_line(&s_b, "omni", line, sizeof line));
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, s_b.mig_local);
    TEST_ASSERT_EQUAL(OMNI_MIG_NOT_NEEDED, s_b.mig_shared);
    TEST_ASSERT_EQUAL(2, s_m.sets);   /* t_omni/local (migrated), watch/theme */
    TEST_ASSERT_EQUAL_STRING("watch/theme", s_m.log[1]);
    assert_owner_view(&s_b.s, 45, OMNI_REVEAL_FLIP);
}

/* A watch that never had settings: the local defaults written once, the shared record left to the first save, the
 * theme written; the next boot writes nothing. */
static void test_boot_fresh_watch(void)
{
    kv_mem_free(&s_m);
    boot(&s_m);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_EQUAL(OMNI_MIG_NO_SOURCE, s_b.mig_local);
    TEST_ASSERT_EQUAL(OMNI_MIG_NO_SOURCE, s_b.mig_shared);
    assert_default_view(&s_b.s);
    char buf[64];
    TEST_ASSERT_EQUAL_STRING("defaults (none saved)", omni_boot_origin(&s_b, buf, sizeof buf));
    TEST_ASSERT_EQUAL(2, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("t_omni/local", s_m.log[0]);
    TEST_ASSERT_EQUAL_STRING("watch/theme", s_m.log[1]);
    TEST_ASSERT_NULL(kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED));
    kv_mem_reset_counts(&s_m);
    boot(&s_m);
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

/* Saves write a record only when its bytes change: nothing changed, nothing written; a shared field, watch/shared
 * only; the face, t_omni/local only. */
static void test_boot_save_only_on_change(void)
{
    reference(&s_m);
    boot(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_settings_t s = s_b.s;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    s.brightness = 50;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[0]);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    s_b.lo.face_idx = 4;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(2, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("t_omni/local", s_m.log[1]);
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
    /* both persisted: a boot reads them back */
    boot(&s_m);
    TEST_ASSERT_EQUAL_UINT8(50, s_b.s.brightness);
    TEST_ASSERT_EQUAL_UINT8(4, omni_boot_face(&s_b));
    /* a failing set is ERR, and its record is retried by the next save */
    s.brightness = 55;
    s_m.fail_after = s_m.sets;
    TEST_ASSERT_EQUAL(OMNI_KV_ERR, omni_boot_save(&s_b, &kv, &s));
    s_m.fail_after = -1;
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[0]);
}

/* The face index is clamped to the carousel; validation covers the carousel and reveal against the limits. */
static void test_boot_face_and_validate(void)
{
    reference(&s_m);
    boot(&s_m);
    s_b.lo.face_idx = 200;
    TEST_ASSERT_EQUAL_UINT8(0, omni_boot_face(&s_b));
    omni_settings_t s = s_b.s;
    s.reveal_style = 9;
    s.carousel[1] = 99;
    s.brightness = 0;
    TEST_ASSERT_FALSE(omni_boot_validate(&s_b, &s));
    TEST_ASSERT_EQUAL_UINT8(OMNI_REVEAL_FLIP, s.reveal_style);
    TEST_ASSERT_EQUAL_UINT8(4, s.carousel_len);
    TEST_ASSERT_EQUAL_UINT8(5, s.brightness);
    TEST_ASSERT_TRUE(omni_boot_validate(&s_b, &s));
}

/* A theme without the legacy part (the dw stub): only the shared part runs; its local record is the defaults. */
static void test_boot_not_legacy(void)
{
    fixture(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_lim, false, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_EQUAL(OMNI_MIG_DONE, s_b.mig_shared);
    TEST_ASSERT_NULL(kv_mem_find(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL));
    TEST_ASSERT_NOT_NULL(kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL));
    TEST_ASSERT_EQUAL_UINT8(45, s_b.s.brightness);
    char buf[64];
    TEST_ASSERT_EQUAL_STRING("shared done", omni_boot_migration(&s_b, buf, sizeof buf));
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
}

/* The console prompt is "<id>> ", built at run time from the theme descriptor (dw wave 1, Task 2). */
static void test_prompt_from_id(void)
{
    char buf[OMNI_BOOT_ID_MAX + 2];
    TEST_ASSERT_EQUAL_STRING("omni> ", omni_boot_prompt("omni", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("dw> ", omni_boot_prompt("dw", buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("watch> ", omni_boot_prompt(NULL, buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("watch> ", omni_boot_prompt("", buf, sizeof buf));
    char small[4];
    TEST_ASSERT_EQUAL_STRING("omn", omni_boot_prompt("omni", small, sizeof small));
    TEST_ASSERT_EQUAL_PTR(small, omni_boot_prompt("omni", small, sizeof small));
    /* the longest id the namespace allows fits the buffer debug_console_start keeps */
    char longid[OMNI_BOOT_ID_MAX];
    memset(longid, 'x', sizeof longid - 1);
    longid[sizeof longid - 1] = 0;
    TEST_ASSERT_EQUAL_UINT(OMNI_BOOT_ID_MAX - 1 + 2, strlen(omni_boot_prompt(longid, buf, sizeof buf)));
    /* cap 1 leaves just the NUL; cap 0 touches nothing */
    char one[1] = {'z'};
    TEST_ASSERT_EQUAL_STRING("", omni_boot_prompt("omni", one, 1));
    char none[1] = {'z'};
    omni_boot_prompt("omni", none, 0);
    TEST_ASSERT_EQUAL_CHAR('z', none[0]);
}

/* ------------------------------------------------------------ the theme extension in t_<id>/local (dw wave 1, T3) */
/* A second theme's limits, as plain literals (this core test does not depend on any theme's tree): eight face ids,
 * one reveal, seven faces in the default order. The tail is any 6 bytes (the core gives them no meaning). */
static const uint8_t k_dw_def[] = { 0, 1, 2, 3, 4, 5, 7 };
static const omni_theme_limits_t k_dw_lim = { 8, 1, 0, sizeof k_dw_def, k_dw_def };
static const uint8_t k_ext[6] = { 0x01, 0x06, 0x00, 0x00, 0x16, 0x07 };

static void boot_dw(kv_mem_t *m, bool no_write)
{
    omni_kv_t kv = kv_mem_iface(m);
    omni_boot_run(&s_b, &kv, "dw", &k_dw_lim, false, no_write);
}

static omni_kv_rc_t save_view(kv_mem_t *m)
{
    omni_kv_t kv = kv_mem_iface(m);
    omni_settings_t s = s_b.s;
    return omni_boot_save(&s_b, &kv, &s);
}

/* fixtures/<name> (the record goldens test_records reads) */
static size_t read_fixture(const char *name, uint8_t *out, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", FIXTURES_DIR, name);
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

/* A tail set and saved is written after the 23 B prefix (one set: t_dw/local, 29 B) and read back by the next boot. */
static void test_boot_local_tail_roundtrip(void)
{
    fixture(&s_m);
    boot_dw(&s_m, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_FALSE(s_b.local_ro);
    TEST_ASSERT_EQUAL_UINT16(0, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("t_dw/local", s_m.log[0]);
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(29, e->n);
    TEST_ASSERT_EQUAL_UINT8(29, e->val[2]);
    TEST_ASSERT_EQUAL_UINT8(0, e->val[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_ext, &e->val[OMNI_LOCAL_V1_SIZE], sizeof k_ext);
    /* saved again unchanged: nothing written */
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(1, s_m.sets);

    kv_mem_reset_counts(&s_m);
    boot_dw(&s_m, false);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_TRUE(s_b.local_found);
    TEST_ASSERT_FALSE(s_b.local_ro);
    TEST_ASSERT_EQUAL(OMNI_REC_TAIL_KEPT, s_b.local_load);
    TEST_ASSERT_EQUAL_UINT16(sizeof k_ext, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_ext, s_b.lo.tail, sizeof k_ext);
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
}

/* A read-only t_dw/local (a version this build does not know) or a no-write boot: the tail applies in RAM and a save
 * writes nothing; the record keeps its bytes. */
static void test_boot_local_tail_read_only(void)
{
    fixture(&s_m);
    boot_dw(&s_m, false);
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    uint8_t rec[OMNI_REC_MAX];
    size_t n = e->n;
    memcpy(rec, e->val, n);
    rec[0] = 2;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
    kv_mem_reset_counts(&s_m);
    boot_dw(&s_m, false);
    TEST_ASSERT_TRUE(s_b.local_ro);
    TEST_ASSERT_EQUAL(OMNI_TAIL_READ_ONLY, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    TEST_ASSERT_EQUAL_UINT16(sizeof k_ext, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_ext, s_b.lo.tail, sizeof k_ext);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    TEST_ASSERT_EQUAL(n, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, e->val, n);

    /* no-write: the same */
    fixture(&s_m);
    boot_dw(&s_m, true);
    TEST_ASSERT_EQUAL(OMNI_TAIL_READ_ONLY, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    TEST_ASSERT_EQUAL_UINT16(sizeof k_ext, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_NULL(kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL));
}

/* 234 bytes cannot follow the 23 B prefix under OMNI_REC_MAX: refused, nothing changes; 233 fits exactly. */
static void test_boot_local_tail_too_long(void)
{
    fixture(&s_m);
    boot_dw(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    uint8_t big[OMNI_REC_MAX - OMNI_LOCAL_V1_SIZE + 1];
    TEST_ASSERT_EQUAL(234, sizeof big);
    memset(big, 0xa5, sizeof big);
    TEST_ASSERT_EQUAL(OMNI_TAIL_BAD_LEN, omni_boot_commit_local_tail(&s_b, big, sizeof big));
    TEST_ASSERT_EQUAL_UINT16(sizeof k_ext, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_ext, s_b.lo.tail, sizeof k_ext);
    TEST_ASSERT_EQUAL_UINT8(0, s_b.lo.tail[sizeof k_ext]);
    /* the largest tail: a 256 B record */
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, big, sizeof big - 1));
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(OMNI_REC_MAX, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->n);
    /* and none over a kept tail: refused (it would drop the kept bytes), the tail and the record unchanged */
    TEST_ASSERT_EQUAL(OMNI_TAIL_BAD_LEN, omni_boot_commit_local_tail(&s_b, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(sizeof big - 1, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(OMNI_REC_MAX, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->n);
}

/* The Omni's record has no tail: migrated, and saved again through the face, it is Stage 4b's golden byte for byte. */
static void test_boot_omni_no_tail(void)
{
    uint8_t want[OMNI_REC_MAX];
    size_t wn = read_fixture("local_v1.hex", want, sizeof want);
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE, wn);
    reference(&s_m);
    boot(&s_m);
    TEST_ASSERT_EQUAL_UINT16(0, s_b.lo.tail_len);
    const kv_entry_t *e = kv_mem_find(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL);
    TEST_ASSERT_EQUAL(wn, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, e->val, wn);
    s_b.lo.face_idx = 1;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    s_b.lo.face_idx = 0;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(2, s_m.sets);
    e = kv_mem_find(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL);
    TEST_ASSERT_EQUAL(wn, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, e->val, wn);
    TEST_ASSERT_NULL(kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL));   /* never another theme's namespace */
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
}

/* store_local_writable's policy: false with local_ro, false with no_write, true otherwise (a read-only watch/shared
 * alone leaves t_<id>/local writable). */
static void test_boot_local_writable(void)
{
    reference(&s_m);
    boot(&s_m);
    TEST_ASSERT_TRUE(omni_boot_local_writable(&s_b));
    boot_dw(&s_m, false);
    TEST_ASSERT_TRUE(omni_boot_local_writable(&s_b));
    boot_dw(&s_m, true);
    TEST_ASSERT_FALSE(omni_boot_local_writable(&s_b));
    /* a failed migration: no_write for the boot */
    fixture(&s_m);
    s_m.fail_after = 0;
    boot_dw(&s_m, false);
    s_m.fail_after = -1;
    TEST_ASSERT_TRUE(s_b.no_write);
    TEST_ASSERT_FALSE(omni_boot_local_writable(&s_b));
    /* a newer t_dw/local */
    fixture(&s_m);
    boot_dw(&s_m, false);
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    uint8_t rec[OMNI_REC_MAX];
    size_t n = e->n;
    memcpy(rec, e->val, n);
    rec[0] = 2;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
    boot_dw(&s_m, false);
    TEST_ASSERT_TRUE(s_b.local_ro);
    TEST_ASSERT_FALSE(omni_boot_local_writable(&s_b));
    /* a newer watch/shared only */
    fixture(&s_m);
    boot_dw(&s_m, false);
    e = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED);
    n = e->n;
    memcpy(rec, e->val, n);
    rec[0] = 2;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_WATCH, OMNI_KEY_SHARED, KV_T_BLOB, rec, n));
    boot_dw(&s_m, false);
    TEST_ASSERT_TRUE(s_b.shared_ro);
    TEST_ASSERT_TRUE(omni_boot_local_writable(&s_b));
}

/* The tail read: its length always; its bytes only when they all fit (never a truncated copy). */
static void test_boot_local_tail_read(void)
{
    TEST_ASSERT_EQUAL(233, OMNI_BOOT_TAIL_MAX);
    fixture(&s_m);
    boot_dw(&s_m, false);
    uint8_t buf[OMNI_BOOT_TAIL_MAX];
    memset(buf, 0xee, sizeof buf);
    TEST_ASSERT_EQUAL(0, omni_boot_local_tail(&s_b, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(0xee, buf[0]);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    TEST_ASSERT_EQUAL(sizeof k_ext, omni_boot_local_tail(&s_b, buf, sizeof k_ext));   /* exactly fits */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_ext, buf, sizeof k_ext);
    memset(buf, 0xee, sizeof buf);
    TEST_ASSERT_EQUAL(sizeof k_ext, omni_boot_local_tail(&s_b, buf, sizeof k_ext - 1));   /* too small: nothing */
    for (size_t i = 0; i < sizeof k_ext; i++) TEST_ASSERT_EQUAL_UINT8(0xee, buf[i]);
    TEST_ASSERT_EQUAL(sizeof k_ext, omni_boot_local_tail(&s_b, NULL, 0));
}

/* The commit path's three outcomes: OK (written by the save), BAD_LEN (RAM unchanged), READ_ONLY (RAM only). */
static void test_boot_commit_local_tail(void)
{
    fixture(&s_m);
    boot_dw(&s_m, false);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, NULL, 0));   /* empty over empty: a no-op */
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL(29, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->n);
    /* too long, and empty over a kept tail: refused, RAM unchanged, nothing written */
    uint8_t big[OMNI_BOOT_TAIL_MAX + 1];
    memset(big, 0xa5, sizeof big);
    TEST_ASSERT_EQUAL(OMNI_TAIL_BAD_LEN, omni_boot_commit_local_tail(&s_b, big, sizeof big));
    TEST_ASSERT_EQUAL(OMNI_TAIL_BAD_LEN, omni_boot_commit_local_tail(&s_b, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(sizeof k_ext, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_ext, s_b.lo.tail, sizeof k_ext);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    /* the largest fits */
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, big, OMNI_BOOT_TAIL_MAX));

    /* a read-only t_dw/local: applied in RAM, never written */
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    uint8_t rec[OMNI_REC_MAX];
    size_t n = e->n;
    memcpy(rec, e->val, n);
    rec[0] = 2;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
    boot_dw(&s_m, false);
    TEST_ASSERT_TRUE(s_b.local_ro);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_TAIL_READ_ONLY, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    TEST_ASSERT_EQUAL_UINT16(sizeof k_ext, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->val, n);
    /* a no-write boot: the same */
    boot_dw(&s_m, true);
    TEST_ASSERT_EQUAL(OMNI_TAIL_READ_ONLY, omni_boot_commit_local_tail(&s_b, k_ext, sizeof k_ext));
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_view(&s_m));
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_prompt_from_id);
    RUN_TEST(test_boot_first_migration);
    RUN_TEST(test_boot_failed_migration_read_only);
    RUN_TEST(test_boot_source_unreadable);
    RUN_TEST(test_boot_nvs_down);
    RUN_TEST(test_boot_unknown_shared_version);
    RUN_TEST(test_boot_unknown_local_version);
    RUN_TEST(test_boot_force);
    RUN_TEST(test_boot_unknown_city_kept);
    RUN_TEST(test_boot_save_unknown_city_then_default);
    RUN_TEST(test_boot_save_unknown_city_untouched);
    RUN_TEST(test_boot_swap);
    RUN_TEST(test_boot_fresh_watch);
    RUN_TEST(test_boot_save_only_on_change);
    RUN_TEST(test_boot_face_and_validate);
    RUN_TEST(test_boot_not_legacy);
    RUN_TEST(test_boot_local_failed_skips_shared);
    RUN_TEST(test_boot_shared_read_error);
    RUN_TEST(test_boot_local_read_error_not_legacy);
    RUN_TEST(test_boot_oversize_records);
    RUN_TEST(test_boot_mistyped_record);
    RUN_TEST(test_boot_local_tail_roundtrip);
    RUN_TEST(test_boot_local_tail_read_only);
    RUN_TEST(test_boot_local_tail_too_long);
    RUN_TEST(test_boot_omni_no_tail);
    RUN_TEST(test_boot_local_writable);
    RUN_TEST(test_boot_local_tail_read);
    RUN_TEST(test_boot_commit_local_tail);
    kv_mem_free(&s_m);
    kv_mem_free(&s_ref);
    return UNITY_END();
}
