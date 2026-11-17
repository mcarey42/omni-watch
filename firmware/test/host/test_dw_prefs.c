/* dw_core's preferences (dw/prefs.h): t_dw/local's extension, the face ids, the lift rule and WD6's boot decision
 * (dw wave 1, Task 3; preflight F2/A5/C1/C2), and wave 2's Task 1: ids 8-15, the eras and Flip open's two homes, the
 * extension's `offered` mask (bytes 6-7) and the carousel offer (WD24, R147 PR2/PR12). The boot cases run the real
 * boot policy (omni/boot.h) over the in-memory kv, so "a read-only boot writes nothing" is checked as the store will
 * run it. Counts and lists are asserted against the constants, never literals, so no face task edits this file. */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "kv_mem.h"
#include "dw/prefs.h"
#include "omni/boot.h"
#include "omni/local.h"
#include "omni/migrate.h"

void setUp(void) {}
void tearDown(void) {}

static const omni_theme_limits_t k_dw_lim = { DW_F__COUNT, 1, 0, DW_DEF_CAROUSEL_LEN, DW_DEF_CAROUSEL };
/* wave 1's record (as a wave-1 watch holds it at wave 2's first flash): the defaults, 6 bytes */
static const uint8_t k_golden[DW_EXT_W1_SIZE] = { 0x01, 0x06, 0x00, 0x00, 0x16, 0x07 };
/* this build's: the defaults, 8 bytes, offered 0x00FF little-endian */
static const uint8_t k_golden_v1_8[DW_EXT_V1_SIZE] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0xFF, 0x00 };

static kv_mem_t s_m;     /* static: too large for the stack under ASan */
static omni_boot_t s_b;

static void assert_defaults(const dw_prefs_t *p)
{
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_ALWAYS_ON, p->mode);
    TEST_ASSERT_FALSE(p->chime_on);
    TEST_ASSERT_EQUAL_UINT8(22, p->quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(7, p->quiet_to_h);
    TEST_ASSERT_EQUAL_HEX16(0x00FF, p->offered);
    TEST_ASSERT_EQUAL_UINT8(0, p->tail_len);
}

/* WD6 as the dw app runs it at boot: the tail decoded, then the decision on the store's writable state. */
static dw_prefs_boot_t decide(dw_prefs_t *p, dw_prefs_load_t *r)
{
    *r = dw_prefs_decode(p, s_b.lo.tail, s_b.lo.tail_len);
    return fk_prefs_boot(omni_boot_local_writable(&s_b), *r);
}

static void boot_dw(bool no_write)
{
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_dw_lim, false, no_write);
}

/* a t_dw/local record as the Stage 4b stub left it: 23 B, carousel {0, 1}, no extension */
static void put_stub_record(void)
{
    static const uint8_t stub_car[] = { 0, 1 };
    const omni_theme_limits_t stub_lim = { 2, 1, 0, sizeof stub_car, stub_car };
    omni_local_t lo;
    omni_local_defaults(&lo, &stub_lim);
    uint8_t rec[OMNI_REC_MAX];
    size_t n = omni_local_encode(&lo, rec, sizeof rec);
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE, n);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
}

/* The defaults encode 01 08 00 00 16 07 FF 00: the extension v1 at 8 bytes (wave 2). */
static void test_prefs_golden_v1_8(void)
{
    TEST_ASSERT_EQUAL_INT(8, DW_EXT_V1_SIZE);
    TEST_ASSERT_EQUAL_INT(6, DW_EXT_W1_SIZE);
    TEST_ASSERT_EQUAL_INT(1, DW_EXT_VERSION);   /* appended within v1 (WD24): no version bump */
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    assert_defaults(&p);
    uint8_t buf[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(DW_EXT_V1_SIZE, dw_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_golden_v1_8, buf, DW_EXT_V1_SIZE);
    /* too small a buffer: nothing (no 6-byte form is ever written again) */
    TEST_ASSERT_EQUAL(0, dw_prefs_encode(&p, buf, DW_EXT_V1_SIZE - 1));
    TEST_ASSERT_EQUAL(0, dw_prefs_encode(&p, buf, DW_EXT_W1_SIZE));
    /* offered is little-endian at bytes 6-7 */
    p.offered = 0x8102;
    TEST_ASSERT_EQUAL(DW_EXT_V1_SIZE, dw_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x81, buf[7]);
    dw_prefs_t q;
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, dw_prefs_decode(&q, buf, DW_EXT_V1_SIZE));
    TEST_ASSERT_EQUAL_HEX16(0x8102, q.offered);
}

/* Wave 1's six bytes decode LOADED with offered 0x00FF (ids 0-7 already offered); re-encoding gives 8 bytes, the
 * first six unchanged but the length byte. */
static void test_prefs_w1_record_loaded(void)
{
    dw_prefs_t p;
    memset(&p, 0xee, sizeof p);
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, dw_prefs_decode(&p, k_golden, sizeof k_golden));
    assert_defaults(&p);
    TEST_ASSERT_FALSE(p.read_only);
    uint8_t buf[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(DW_EXT_V1_SIZE, dw_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_golden_v1_8, buf, DW_EXT_V1_SIZE);
    TEST_ASSERT_EQUAL_HEX8(k_golden[0], buf[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&k_golden[2], &buf[2], DW_EXT_W1_SIZE - 2);
    /* a wave-1 record with the owner's values keeps them */
    const uint8_t own[DW_EXT_W1_SIZE] = { 0x01, 0x06, 0x01, 0x01, 0x15, 0x06 };
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, dw_prefs_decode(&p, own, sizeof own));
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_FLIP_OPEN, p.mode);
    TEST_ASSERT_TRUE(p.chime_on);
    TEST_ASSERT_EQUAL_UINT8(21, p.quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(6, p.quiet_to_h);
    TEST_ASSERT_EQUAL_HEX16(0x00FF, p.offered);
    TEST_ASSERT_EQUAL_UINT8(0, p.tail_len);
    const uint8_t want[DW_EXT_V1_SIZE] = { 0x01, 0x08, 0x01, 0x01, 0x15, 0x06, 0xFF, 0x00 };
    TEST_ASSERT_EQUAL(DW_EXT_V1_SIZE, dw_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, buf, DW_EXT_V1_SIZE);
}

/* Golden bytes, no copy of wave 1's decoder: wave 1's record and this build's differ only in the length byte and the
 * two appended bytes (offered, LE). A wave-1 reader sees ext_len 8 > 6 as TAIL_KEPT with 2 tail bytes, which wave 1's
 * own tests pin (test_prefs_newer_ext_kept at 0458bfa), so a rollback keeps the mask byte for byte.
 * This rests on wave 1's decoder condition (dw_prefs_decode at 0458bfa, where DW_EXT_V1_SIZE was 6): a record is read,
 * not READ_ONLY, when ext_ver == 1, ext_len == n, ext_len >= 6 && n - 6 <= 64; its tail is then the n - 6 bytes past
 * byte 6, kept verbatim. Our 8 bytes pass it (8 >= 6, 2 <= 64), so wave 1 keeps offered as a 2-byte tail. */
static void test_prefs_w1_record_tail_kept_by_w1(void)
{
    TEST_ASSERT_EQUAL_HEX8(DW_EXT_W1_SIZE, k_golden[1]);
    TEST_ASSERT_EQUAL_HEX8(DW_EXT_V1_SIZE, k_golden_v1_8[1]);
    TEST_ASSERT_EQUAL_HEX8(k_golden[0], k_golden_v1_8[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&k_golden[2], &k_golden_v1_8[2], DW_EXT_W1_SIZE - 2);
    TEST_ASSERT_EQUAL_INT(2, DW_EXT_V1_SIZE - DW_EXT_W1_SIZE);
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    TEST_ASSERT_EQUAL_HEX16(p.offered, (uint16_t)(k_golden_v1_8[6] | k_golden_v1_8[7] << 8));
}

/* ext_len 7: neither wave 1's record nor this build's: READ_ONLY, never written. */
static void test_prefs_ext7_read_only(void)
{
    const uint8_t e7[7] = { 0x01, 0x07, 0x01, 0x01, 0x10, 0x05, 0xFF };
    dw_prefs_t p;
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, e7, sizeof e7));
    assert_defaults(&p);
    TEST_ASSERT_TRUE(p.read_only);
    uint8_t out[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(0, dw_prefs_encode(&p, out, sizeof out));
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, fk_prefs_boot(true, DW_PREFS_READ_ONLY));
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(true, DW_BOOT_RAM_ONLY));
}

static void test_prefs_upgrade_from_stub(void)
{
    dw_prefs_t p;
    memset(&p, 0xee, sizeof p);
    TEST_ASSERT_EQUAL(DW_PREFS_ABSENT, dw_prefs_decode(&p, NULL, 0));
    assert_defaults(&p);
    p.mode = DW_MODE_FLIP_OPEN;
    p.chime_on = true;
    uint8_t buf[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    size_t n = dw_prefs_encode(&p, buf, sizeof buf);
    TEST_ASSERT_EQUAL(DW_EXT_V1_SIZE, n);
    dw_prefs_t q;
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, dw_prefs_decode(&q, buf, n));
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_FLIP_OPEN, q.mode);
    TEST_ASSERT_TRUE(q.chime_on);
    TEST_ASSERT_EQUAL_UINT8(22, q.quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(7, q.quiet_to_h);
    TEST_ASSERT_EQUAL_HEX16(0x00FF, q.offered);
    TEST_ASSERT_EQUAL_UINT8(0, q.tail_len);
}

static void test_prefs_boot_absent_writable(void)
{
    TEST_ASSERT_EQUAL(DW_BOOT_RESET_COMMIT, fk_prefs_boot(true, DW_PREFS_ABSENT));
    /* the first wave-1 boot on a watch whose t_dw/local the boot just wrote (no extension) */
    kv_mem_free(&s_m);
    boot_dw(false);
    dw_prefs_t p;
    dw_prefs_load_t r;
    TEST_ASSERT_EQUAL(DW_BOOT_RESET_COMMIT, decide(&p, &r));
    TEST_ASSERT_EQUAL(DW_PREFS_ABSENT, r);
}

/* The stub's 23 B record decodes ABSENT, and the reset is committed once; the record it leaves (23 B and this build's
 * extension) is LOADED on the next boot, so it never resets again. */
static void test_prefs_boot_stub_record(void)
{
    kv_mem_free(&s_m);
    put_stub_record();
    boot_dw(false);
    TEST_ASSERT_TRUE(s_b.local_found);
    TEST_ASSERT_EQUAL(OMNI_REC_LOADED, s_b.local_load);
    TEST_ASSERT_EQUAL_UINT16(0, s_b.lo.tail_len);
    dw_prefs_t p;
    dw_prefs_load_t r;
    TEST_ASSERT_EQUAL(DW_BOOT_RESET_COMMIT, decide(&p, &r));
    TEST_ASSERT_EQUAL(DW_PREFS_ABSENT, r);
    assert_defaults(&p);

    uint8_t ext[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    size_t n = dw_prefs_encode(&p, ext, sizeof ext);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, ext, n));
    kv_mem_reset_counts(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_settings_t s = s_b.s;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE + DW_EXT_V1_SIZE, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->n);

    boot_dw(false);
    TEST_ASSERT_EQUAL(DW_BOOT_KEEP, decide(&p, &r));
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, r);
}

/* A newer t_dw/local (read-only): the core holds the defaults with no tail, which decodes ABSENT; nothing is reset
 * or written (C2). */
static void test_prefs_boot_local_ro(void)
{
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, fk_prefs_boot(false, DW_PREFS_ABSENT));
    kv_mem_free(&s_m);
    put_stub_record();
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    uint8_t rec[OMNI_REC_MAX];
    size_t n = e->n;
    memcpy(rec, e->val, n);
    rec[0] = 2;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
    boot_dw(false);
    TEST_ASSERT_TRUE(s_b.local_ro);
    dw_prefs_t p;
    dw_prefs_load_t r;
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, decide(&p, &r));
    TEST_ASSERT_EQUAL(DW_PREFS_ABSENT, r);
}

/* A no-write boot (NVS down, or a failed migration): RAM_ONLY, and the store writes nothing. */
static void test_prefs_boot_no_write(void)
{
    kv_mem_free(&s_m);
    put_stub_record();
    boot_dw(true);
    dw_prefs_t p;
    dw_prefs_load_t r;
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, decide(&p, &r));
    TEST_ASSERT_EQUAL(DW_PREFS_ABSENT, r);
    /* a failed migration is a no-write boot too */
    kv_mem_free(&s_m);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_STR, (const uint8_t *)"x", 1));
    boot_dw(false);
    TEST_ASSERT_TRUE(s_b.no_write);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, decide(&p, &r));
}

static void test_prefs_boot_newer(void)
{
    TEST_ASSERT_EQUAL(DW_BOOT_KEEP, fk_prefs_boot(true, DW_PREFS_TAIL_KEPT));
    TEST_ASSERT_EQUAL(DW_BOOT_KEEP, fk_prefs_boot(false, DW_PREFS_TAIL_KEPT));
    TEST_ASSERT_EQUAL(DW_BOOT_KEEP, fk_prefs_boot(true, DW_PREFS_LOADED));
    TEST_ASSERT_EQUAL(DW_BOOT_KEEP, fk_prefs_boot(false, DW_PREFS_LOADED));
}

static void test_prefs_boot_unknown(void)
{
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, fk_prefs_boot(true, DW_PREFS_READ_ONLY));
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, fk_prefs_boot(false, DW_PREFS_READ_ONLY));
}

/* ext_len 11: three bytes a newer build appended past offered, kept through a change and a re-encode (the tail now
 * starts at byte 8). */
static void test_prefs_newer_ext_kept(void)
{
    const uint8_t in[11] = { 0x01, 0x0b, 0x00, 0x01, 0x15, 0x06, 0x7f, 0x01, 0xaa, 0xbb, 0xcc };
    dw_prefs_t p;
    TEST_ASSERT_EQUAL(DW_PREFS_TAIL_KEPT, dw_prefs_decode(&p, in, sizeof in));
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_ALWAYS_ON, p.mode);
    TEST_ASSERT_TRUE(p.chime_on);
    TEST_ASSERT_EQUAL_UINT8(21, p.quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(6, p.quiet_to_h);
    TEST_ASSERT_EQUAL_HEX16(0x017f, p.offered);
    TEST_ASSERT_EQUAL_UINT8(3, p.tail_len);
    p.mode = DW_MODE_FLIP_OPEN;
    uint8_t out[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(sizeof in, dw_prefs_encode(&p, out, sizeof out));
    const uint8_t want[11] = { 0x01, 0x0b, 0x01, 0x01, 0x15, 0x06, 0x7f, 0x01, 0xaa, 0xbb, 0xcc };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, sizeof want);
    /* ext_len 9: one byte past offered */
    const uint8_t in9[9] = { 0x01, 0x09, 0x00, 0x00, 0x16, 0x07, 0xFF, 0x00, 0x42 };
    TEST_ASSERT_EQUAL(DW_PREFS_TAIL_KEPT, dw_prefs_decode(&p, in9, sizeof in9));
    TEST_ASSERT_EQUAL_UINT8(1, p.tail_len);
    TEST_ASSERT_EQUAL_HEX8(0x42, p.tail[0]);
    /* the largest kept tail (64), and one more is read-only */
    uint8_t big[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX + 1];
    memset(big, 0x5a, sizeof big);
    memcpy(big, k_golden_v1_8, DW_EXT_V1_SIZE);
    big[1] = DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX;
    TEST_ASSERT_EQUAL(DW_PREFS_TAIL_KEPT, dw_prefs_decode(&p, big, DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX));
    TEST_ASSERT_EQUAL_UINT8(DW_EXT_TAIL_MAX, p.tail_len);
    TEST_ASSERT_EQUAL(DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX, dw_prefs_encode(&p, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, out, DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX);
    big[1] = DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX + 1;
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, big, sizeof big));
    assert_defaults(&p);
}

/* ext_ver 2, or a length that does not add up: READ_ONLY with the defaults. */
static void test_prefs_unknown_ext_read_only(void)
{
    const uint8_t v2[DW_EXT_W1_SIZE] = { 0x02, 0x06, 0x01, 0x01, 0x10, 0x05 };
    dw_prefs_t p;
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, v2, sizeof v2));
    assert_defaults(&p);
    TEST_ASSERT_TRUE(p.read_only);
    uint8_t out[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(0, dw_prefs_encode(&p, out, sizeof out));   /* never written */
    const uint8_t shortlen[DW_EXT_W1_SIZE] = { 0x01, 0x05, 0x01, 0x01, 0x10, 0x05 };   /* ext_len < 6 */
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, shortlen, sizeof shortlen));
    assert_defaults(&p);
    const uint8_t lying[DW_EXT_W1_SIZE + 1] = { 0x01, 0x06, 0x01, 0x01, 0x10, 0x05, 0x00 };   /* ext_len != n */
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, lying, sizeof lying));
    assert_defaults(&p);
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, k_golden, 1));   /* one byte: no length */
    assert_defaults(&p);
    /* this build's 8 bytes with a lying length, and ext_ver 2 at 8 bytes */
    uint8_t e8[DW_EXT_V1_SIZE];
    memcpy(e8, k_golden_v1_8, sizeof e8);
    e8[1] = DW_EXT_W1_SIZE;
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, e8, sizeof e8));
    assert_defaults(&p);
    e8[1] = DW_EXT_V1_SIZE;
    e8[0] = 2;
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, e8, sizeof e8));
    assert_defaults(&p);
}

static void test_prefs_validate(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    TEST_ASSERT_TRUE(dw_prefs_validate(&p));
    p.mode = 7;
    TEST_ASSERT_FALSE(dw_prefs_validate(&p));
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_ALWAYS_ON, p.mode);
    p.quiet_from_h = 24;
    p.quiet_to_h = 24;
    TEST_ASSERT_FALSE(dw_prefs_validate(&p));
    TEST_ASSERT_EQUAL_UINT8(22, p.quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(7, p.quiet_to_h);
    p.quiet_from_h = 23;
    p.quiet_to_h = 0;
    TEST_ASSERT_TRUE(dw_prefs_validate(&p));
    /* decode validates: mode 7 and hour 24 stored come back clamped */
    const uint8_t bad[DW_EXT_W1_SIZE] = { 0x01, 0x06, 0x07, 0x00, 0x18, 0x07 };
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, dw_prefs_decode(&p, bad, sizeof bad));
    assert_defaults(&p);
}

static void test_lift_flip_open_home(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.mode = DW_MODE_FLIP_OPEN;
    const uint8_t car[] = { DW_F_ROTOR, DW_F_CRYSTAL, DW_F_CIRCLES };
    TEST_ASSERT_EQUAL_INT(1, dw_lift_index(&p, car, sizeof car, DW_F_CIRCLES));
    TEST_ASSERT_EQUAL_INT(1, dw_lift_index(&p, car, sizeof car, DW_F_ROTOR));   /* Classic, no Room: Crystal */
    TEST_ASSERT_EQUAL_INT(0, dw_lift_index(&p, DW_DEF_CAROUSEL, DW_DEF_CAROUSEL_LEN, DW_F_CRYSTAL));
}

static void test_lift_always_on(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    const uint8_t car[] = { DW_F_ROTOR, DW_F_CRYSTAL, DW_F_CIRCLES };
    TEST_ASSERT_EQUAL_INT(-1, dw_lift_index(&p, car, sizeof car, DW_F_CRYSTAL));
}

static void test_lift_home_missing(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.mode = DW_MODE_FLIP_OPEN;
    const uint8_t car[] = { DW_F_ROUNDEL, DW_F_CALLBOX };
    TEST_ASSERT_EQUAL_INT(-1, dw_lift_index(&p, car, sizeof car, DW_F_ROUNDEL));
    TEST_ASSERT_EQUAL_INT(-1, dw_lift_index(&p, car, sizeof car, DW_F_CORAL));
    TEST_ASSERT_EQUAL_INT(-1, dw_lift_index(&p, NULL, 0, DW_F_CRYSTAL));
}

/* WD24's era homes: Classic goes to the Room (#2), Modern to the Crystal Chamber (#1). */
static void test_lift_era_classic_home(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.mode = DW_MODE_FLIP_OPEN;
    const uint8_t car[] = { DW_F_CRYSTAL, DW_F_ROUNDEL, DW_F_ROOM };
    TEST_ASSERT_EQUAL_INT(2, dw_lift_index(&p, car, sizeof car, DW_F_ROUNDEL));
    TEST_ASSERT_EQUAL_INT(2, dw_lift_index(&p, car, sizeof car, DW_F_ROOM));
    TEST_ASSERT_EQUAL_INT(2, dw_lift_index(&p, car, sizeof car, DW_F_BROADCAST));
}

static void test_lift_era_modern_home(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.mode = DW_MODE_FLIP_OPEN;
    const uint8_t car[] = { DW_F_CRYSTAL, DW_F_ROUNDEL, DW_F_ROOM };
    TEST_ASSERT_EQUAL_INT(0, dw_lift_index(&p, car, sizeof car, DW_F_CORAL));
    TEST_ASSERT_EQUAL_INT(0, dw_lift_index(&p, car, sizeof car, DW_F_CIRCLES));
    TEST_ASSERT_EQUAL_INT(0, dw_lift_index(&p, car, sizeof car, DW_F_DIGITAL));
    TEST_ASSERT_EQUAL_INT(0, dw_lift_index(&p, car, sizeof car, DW_F_AMBER));
}

/* A missing home falls back to the other home; neither in the carousel: no jump. */
static void test_lift_era_fallback(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.mode = DW_MODE_FLIP_OPEN;
    const uint8_t car_a[] = { DW_F_CRYSTAL, DW_F_ROTOR };
    TEST_ASSERT_EQUAL_INT(0, dw_lift_index(&p, car_a, sizeof car_a, DW_F_ROTOR));
    const uint8_t car_b[] = { DW_F_ROTOR, DW_F_CORAL };
    TEST_ASSERT_EQUAL_INT(-1, dw_lift_index(&p, car_b, sizeof car_b, DW_F_ROTOR));
    TEST_ASSERT_EQUAL_INT(-1, dw_lift_index(&p, car_b, sizeof car_b, DW_F_CORAL));
    const uint8_t car_c[] = { DW_F_CORAL, DW_F_ROOM };   /* Modern, no Crystal: the Room */
    TEST_ASSERT_EQUAL_INT(1, dw_lift_index(&p, car_c, sizeof car_c, DW_F_CORAL));
}

static void test_lift_always_on_any_era(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    const uint8_t car[] = { DW_F_CRYSTAL, DW_F_ROUNDEL, DW_F_ROOM };
    for (uint8_t id = 0; id < DW_F__ALL; id++) TEST_ASSERT_EQUAL_INT(-1, dw_lift_index(&p, car, sizeof car, id));
}

static void test_face_era_table(void)
{
    static const dw_era_t want[16] = {
        [DW_F_CRYSTAL] = DW_ERA_MODERN,  [DW_F_ROUNDEL] = DW_ERA_CLASSIC, [DW_F_CALLBOX] = DW_ERA_CLASSIC,
        [DW_F_ROTOR] = DW_ERA_CLASSIC,   [DW_F_CORAL] = DW_ERA_MODERN,    [DW_F_CIRCLES] = DW_ERA_MODERN,
        [DW_F_DIGITAL] = DW_ERA_MODERN,  [DW_F_LEARN] = DW_ERA_MODERN,    [DW_F_ROOM] = DW_ERA_CLASSIC,
        [DW_F_AMBER] = DW_ERA_MODERN,    [DW_F_FOB] = DW_ERA_MODERN,      [DW_F_CONSOLE] = DW_ERA_CLASSIC,
        [DW_F_WALL] = DW_ERA_CLASSIC,    [DW_F_DOORWAY] = DW_ERA_MODERN,  [DW_F_BROADCAST] = DW_ERA_CLASSIC,
        [DW_F_VORTEX] = DW_ERA_MODERN,
    };
    for (uint8_t id = 0; id < 16; id++) TEST_ASSERT_EQUAL_INT_MESSAGE(want[id], dw_face_era(id), "era");
    TEST_ASSERT_EQUAL_INT(DW_ERA_MODERN, dw_face_era(16));
    TEST_ASSERT_EQUAL_INT(DW_ERA_MODERN, dw_face_era(255));
}

/* WD5: the ids are fixed (append-only across waves): wave 1's, and wave 2's 8-15 named now. */
static void test_face_ids(void)
{
    TEST_ASSERT_EQUAL_INT(6, DW_F_DIGITAL);
    TEST_ASSERT_EQUAL_INT(7, DW_F_LEARN);
    TEST_ASSERT_EQUAL_INT(8, DW_F_ROOM);
    TEST_ASSERT_EQUAL_INT(9, DW_F_AMBER);
    TEST_ASSERT_EQUAL_INT(10, DW_F_FOB);
    TEST_ASSERT_EQUAL_INT(11, DW_F_CONSOLE);
    TEST_ASSERT_EQUAL_INT(12, DW_F_WALL);
    TEST_ASSERT_EQUAL_INT(13, DW_F_DOORWAY);
    TEST_ASSERT_EQUAL_INT(14, DW_F_BROADCAST);
    TEST_ASSERT_EQUAL_INT(15, DW_F_VORTEX);
    TEST_ASSERT_EQUAL_INT(16, DW_F__ALL);
    /* wave 1's carousel stays its head, in order (append-only) */
    const uint8_t wave1[7] = { 0, 1, 2, 3, 4, 5, 7 };
    TEST_ASSERT_TRUE(DW_DEF_CAROUSEL_LEN >= sizeof wave1);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wave1, DW_DEF_CAROUSEL, sizeof wave1);
}

/* R147 PR12: the count and the default list agree, whatever face task built them. */
static void test_prefs_def_carousel_matches_count(void)
{
    TEST_ASSERT_TRUE(DW_F__COUNT <= 16);
    TEST_ASSERT_TRUE(DW_F__COUNT <= DW_F__ALL);
    TEST_ASSERT_EQUAL_INT(DW_F__COUNT - 1, DW_DEF_CAROUSEL_LEN);   /* every built face but Digital */
    TEST_ASSERT_TRUE(DW_DEF_CAROUSEL_LEN <= DW_CAROUSEL_MAX);
    uint32_t seen = 0;
    for (int i = 0; i < DW_DEF_CAROUSEL_LEN; i++) {
        const uint8_t id = DW_DEF_CAROUSEL[i];
        TEST_ASSERT_TRUE_MESSAGE(id < DW_F__COUNT, "built");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(DW_F_DIGITAL, id, "Digital is shared, not offered");
        TEST_ASSERT_FALSE_MESSAGE(seen & (1u << id), "no repeat");
        seen |= 1u << id;
    }
    /* the defaults' mask covers wave 1's ids exactly */
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    TEST_ASSERT_EQUAL_HEX16(0x00FF, p.offered);
}

/* ------------------------------------------------------------------------------------- the carousel offer (WD24) */
static const uint8_t k_w1_car[] = { 0, 1, 2, 3, 4, 5, 7 };
static const uint8_t k_offer_list[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9 };   /* a later build's list (ids 8, 9 built) */
/* wave 1's build (wave 2 Task 5: this build has id 8 now, so a rollback is named, not this build's limits) */
static const omni_theme_limits_t k_w1_lim = { 8, 1, 0, sizeof k_w1_car, k_w1_car };

static void set_car(uint8_t car[DW_CAROUSEL_MAX], uint8_t *len, const uint8_t *src, uint8_t n)
{
    memset(car, 0xee, DW_CAROUSEL_MAX);
    memcpy(car, src, n);
    *len = n;
}

static void test_prefs_offer_appends_once(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    uint8_t car[DW_CAROUSEL_MAX], len;
    set_car(car, &len, k_w1_car, sizeof k_w1_car);
    TEST_ASSERT_TRUE(fk_offer_list(&p.offered, car, &len, k_offer_list, sizeof k_offer_list));
    const uint8_t want[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9 };
    TEST_ASSERT_EQUAL_UINT8(sizeof want, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, car, sizeof want);
    TEST_ASSERT_EQUAL_HEX8(0xee, car[len]);   /* nothing past len */
    TEST_ASSERT_EQUAL_HEX16(0x03FF, p.offered);
    /* again: nothing */
    const dw_prefs_t before = p;
    uint8_t car2[DW_CAROUSEL_MAX];
    memcpy(car2, car, sizeof car);
    TEST_ASSERT_FALSE(fk_offer_list(&p.offered, car, &len, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL_UINT8(sizeof want, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(car2, car, sizeof car);
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof p);
    /* the real list: this build's own faces were all offered by the defaults (T1: none new) */
    dw_prefs_defaults(&p);
    set_car(car, &len, DW_DEF_CAROUSEL, DW_DEF_CAROUSEL_LEN);
    dw_prefs_offer(&p, car, &len);
    TEST_ASSERT_FALSE(dw_prefs_offer(&p, car, &len));   /* idempotent whatever the build */
    TEST_ASSERT_EQUAL_UINT8(DW_DEF_CAROUSEL_LEN, len);
    for (int i = 0; i < DW_DEF_CAROUSEL_LEN; i++) TEST_ASSERT_TRUE(p.offered & (1u << DW_DEF_CAROUSEL[i]));
}

/* The owner removed 9 after it was offered: never re-added; order kept. */
static void test_prefs_offer_keeps_order_and_removed(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.offered = 0x03FF;
    uint8_t car[DW_CAROUSEL_MAX], len;
    const uint8_t own[] = { 8, 0, 3, 1 };
    set_car(car, &len, own, sizeof own);
    TEST_ASSERT_FALSE(fk_offer_list(&p.offered, car, &len, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL_UINT8(sizeof own, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(own, car, sizeof own);
    TEST_ASSERT_EQUAL_HEX16(0x03FF, p.offered);
}

/* 16 entries: nothing appended, but every list bit set (the offer happened; it found no room) */
static void test_prefs_offer_full_carousel(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    uint8_t car[DW_CAROUSEL_MAX], len;
    /* 16 ids without 8 and 9 must repeat two (the store never holds a repeat: this is the length guard alone) */
    const uint8_t full[DW_CAROUSEL_MAX] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 0, 1 };
    set_car(car, &len, full, sizeof full);
    TEST_ASSERT_TRUE(fk_offer_list(&p.offered, car, &len, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL_UINT8(DW_CAROUSEL_MAX, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(full, car, sizeof full);
    TEST_ASSERT_EQUAL_HEX16(0x03FF, p.offered);
    /* one slot: 8 goes in, 9 finds none */
    set_car(car, &len, full, DW_CAROUSEL_MAX - 1);
    dw_prefs_defaults(&p);
    TEST_ASSERT_TRUE(fk_offer_list(&p.offered, car, &len, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL_UINT8(DW_CAROUSEL_MAX, len);
    TEST_ASSERT_EQUAL_UINT8(8, car[DW_CAROUSEL_MAX - 1]);
    TEST_ASSERT_EQUAL_HEX16(0x03FF, p.offered);
}

/* 8 present with its bit clear: only the bit changes */
static void test_prefs_offer_present_not_duplicated(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.offered = 0x02FF;   /* 9 offered and removed; 8 never offered */
    uint8_t car[DW_CAROUSEL_MAX], len;
    const uint8_t own[] = { 0, 8, 1 };
    set_car(car, &len, own, sizeof own);
    TEST_ASSERT_TRUE(fk_offer_list(&p.offered, car, &len, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL_UINT8(sizeof own, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(own, car, sizeof own);
    TEST_ASSERT_EQUAL_HEX16(0x03FF, p.offered);
}

static void test_prefs_boot_offer_read_only(void)
{
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(true, DW_BOOT_RAM_ONLY));
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(false, DW_BOOT_RAM_ONLY));
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(false, DW_BOOT_KEEP));
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(false, DW_BOOT_RESET_COMMIT));
    TEST_ASSERT_TRUE(fk_prefs_boot_offer(true, DW_BOOT_KEEP));
    TEST_ASSERT_TRUE(fk_prefs_boot_offer(true, DW_BOOT_RESET_COMMIT));
}

/* R147 PR2(b): the recovery after a rollback lost faces. */
static void test_prefs_offer_reset(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.offered = 0x03FF;
    uint8_t car[DW_CAROUSEL_MAX], len;
    const uint8_t own[] = { 0, 1, 8 };
    set_car(car, &len, own, sizeof own);
    TEST_ASSERT_TRUE(fk_offer_reset_list(&p.offered, car, len, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL_HEX16(0x0143, p.offered);   /* 0, 1, 8 present; 6 not in the list */
    TEST_ASSERT_EQUAL_UINT8(sizeof own, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(own, car, sizeof own);
    TEST_ASSERT_TRUE(fk_offer_list(&p.offered, car, &len, k_offer_list, sizeof k_offer_list));
    const uint8_t want[] = { 0, 1, 8, 2, 3, 4, 5, 7, 9 };
    TEST_ASSERT_EQUAL_UINT8(sizeof want, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, car, sizeof want);
    TEST_ASSERT_EQUAL_HEX16(0x03FF, p.offered);
    /* the real list: the bits of this build's faces missing from the carousel */
    dw_prefs_defaults(&p);
    const uint8_t one[] = { DW_F_CRYSTAL };
    set_car(car, &len, one, sizeof one);
    TEST_ASSERT_TRUE(dw_prefs_offer_reset(&p, car, len));
    for (int i = 0; i < DW_DEF_CAROUSEL_LEN; i++)
        TEST_ASSERT_EQUAL(DW_DEF_CAROUSEL[i] == DW_F_CRYSTAL, (p.offered >> DW_DEF_CAROUSEL[i]) & 1u);
    TEST_ASSERT_TRUE(p.offered & (1u << DW_F_DIGITAL));   /* not in the list: untouched */
}

/* `dw offer reset`'s verdict (R147 PR2(b)), the event loop's and so the console's: never on a no-write boot or a
 * read-only record (p untouched, in RAM too); otherwise the reset, or nothing to reset. */
static void test_prefs_offer_reset_verdict(void)
{
    dw_prefs_t p;
    uint8_t car[DW_CAROUSEL_MAX], len;
    const uint8_t one[] = { DW_F_CRYSTAL };
    set_car(car, &len, one, sizeof one);
    dw_prefs_defaults(&p);
    dw_prefs_t before = p;
    TEST_ASSERT_EQUAL(DW_OFFER_V_READ_ONLY, dw_prefs_offer_reset_verdict(&p, false, car, len));   /* no-write boot */
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof p);
    p.read_only = true;
    before = p;
    TEST_ASSERT_EQUAL(DW_OFFER_V_READ_ONLY, dw_prefs_offer_reset_verdict(&p, true, car, len));    /* read-only record */
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof p);
    dw_prefs_defaults(&p);
    TEST_ASSERT_EQUAL(DW_OFFER_V_RESET, dw_prefs_offer_reset_verdict(&p, true, car, len));
    TEST_ASSERT_EQUAL_HEX16(DW_OFFERED_DEF & ~0x00BEu, p.offered);   /* 1-5 and 7 cleared; 0 present, 6 not offered */
    TEST_ASSERT_EQUAL(DW_OFFER_V_NOTHING, dw_prefs_offer_reset_verdict(&p, true, car, len));     /* already reset */
    dw_prefs_defaults(&p);
    set_car(car, &len, DW_DEF_CAROUSEL, DW_DEF_CAROUSEL_LEN);
    TEST_ASSERT_EQUAL(DW_OFFER_V_NOTHING, dw_prefs_offer_reset_verdict(&p, true, car, len));
    TEST_ASSERT_EQUAL_HEX16(DW_OFFERED_DEF, p.offered);
    TEST_ASSERT_EQUAL(0, DW_OFFER_V_PENDING);   /* the console's "no verdict yet" */
}

static void test_prefs_offer_reset_noop(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.offered = 0x03FF;
    uint8_t car[DW_CAROUSEL_MAX], len;
    set_car(car, &len, k_offer_list, sizeof k_offer_list);
    const dw_prefs_t before = p;
    TEST_ASSERT_FALSE(fk_offer_reset_list(&p.offered, car, len, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof p);
    /* the real list over the default carousel: nothing to reset */
    dw_prefs_defaults(&p);
    set_car(car, &len, DW_DEF_CAROUSEL, DW_DEF_CAROUSEL_LEN);
    TEST_ASSERT_FALSE(dw_prefs_offer_reset(&p, car, len));
    TEST_ASSERT_EQUAL_HEX16(0x00FF, p.offered);
}

/* ------------------------------------------------------------ the extension through core-field saves (review m5) */
static const uint8_t k_stub_car[] = { 0, 1 };
static const omni_theme_limits_t k_stub_lim = { 2, 1, 0, sizeof k_stub_car, k_stub_car };   /* the stub (rollback) */

/* t_dw/local: the dw defaults' 23 B prefix, then ext */
static void put_record_with_ext(const uint8_t *ext, size_t n)
{
    omni_local_t lo;
    omni_local_defaults(&lo, &k_dw_lim);
    memcpy(lo.tail, ext, n);
    lo.tail_len = (uint16_t)n;
    uint8_t rec[OMNI_REC_MAX];
    size_t rn = omni_local_encode(&lo, rec, sizeof rec);
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE + n, rn);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, rn));
}

static void assert_record_ext(const uint8_t *ext, size_t n, const char *what)
{
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, what);
    TEST_ASSERT_EQUAL_MESSAGE(OMNI_LOCAL_V1_SIZE + n, e->n, what);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(ext, &e->val[OMNI_LOCAL_V1_SIZE], n, what);
}

static omni_kv_rc_t save_settings(const omni_settings_t *s)
{
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_settings_t v = *s;
    return omni_boot_save(&s_b, &kv, &v);
}

/* A face change, a carousel change and a boot of the stub ({0, 1}, the rollback) each write t_dw/local; the
 * extension rides through byte for byte, and the wave-1 boot after that decides as before. */
static void core_saves_keep(const uint8_t *ext, size_t n, dw_prefs_load_t want_r, dw_prefs_boot_t want_d)
{
    kv_mem_free(&s_m);
    put_record_with_ext(ext, n);
    boot_dw(false);
    dw_prefs_t p;
    dw_prefs_load_t r;
    TEST_ASSERT_EQUAL(want_d, decide(&p, &r));
    TEST_ASSERT_EQUAL(want_r, r);
    /* a save with nothing changed writes only the absent watch/shared: t_dw/local's bytes are as read */
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_STRING("watch/shared", s_m.log[0]);

    kv_mem_reset_counts(&s_m);
    s_b.lo.face_idx = 2;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    TEST_ASSERT_EQUAL_UINT8(2, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->val[22]);
    assert_record_ext(ext, n, "face change");

    omni_settings_t s = s_b.s;
    s.carousel_len = 3;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s));
    TEST_ASSERT_EQUAL(2, s_m.sets);
    TEST_ASSERT_EQUAL_UINT8(3, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->val[5]);
    assert_record_ext(ext, n, "carousel change");

    /* the stub boots on it: carousel clipped to {0, 1}, its first save writes that, the extension kept */
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_stub_lim, false, false);
    TEST_ASSERT_FALSE(s_b.local_ro);
    TEST_ASSERT_EQUAL_UINT8(2, s_b.s.carousel_len);
    s_b.lo.face_idx = 1;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));
    TEST_ASSERT_EQUAL(3, s_m.sets);
    TEST_ASSERT_EQUAL_UINT8(2, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->val[5]);
    assert_record_ext(ext, n, "stub rollback");

    boot_dw(false);
    TEST_ASSERT_EQUAL(want_d, decide(&p, &r));
    TEST_ASSERT_EQUAL(want_r, r);
}

static void test_prefs_core_saves_keep_ext(void)
{
    core_saves_keep(k_golden, sizeof k_golden, DW_PREFS_LOADED, DW_BOOT_KEEP);   /* the 29 B record */
}

static void test_prefs_core_saves_keep_v1_8(void)
{
    core_saves_keep(k_golden_v1_8, sizeof k_golden_v1_8, DW_PREFS_LOADED, DW_BOOT_KEEP);   /* the 31 B record */
}

static void test_prefs_core_saves_keep_newer_ext(void)
{
    const uint8_t ext9[11] = { 0x01, 0x0b, 0x01, 0x01, 0x15, 0x06, 0xff, 0x00, 0xaa, 0xbb, 0xcc };   /* 34 B record */
    core_saves_keep(ext9, sizeof ext9, DW_PREFS_TAIL_KEPT, DW_BOOT_KEEP);
    /* and a dw change re-committed: the three unknown bytes kept in the record */
    dw_prefs_t p;
    TEST_ASSERT_EQUAL(DW_PREFS_TAIL_KEPT, dw_prefs_decode(&p, s_b.lo.tail, s_b.lo.tail_len));
    p.mode = DW_MODE_ALWAYS_ON;
    uint8_t buf[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    size_t n = dw_prefs_encode(&p, buf, sizeof buf);
    TEST_ASSERT_EQUAL(sizeof ext9, n);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, buf, n));
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    const uint8_t want[11] = { 0x01, 0x0b, 0x00, 0x01, 0x15, 0x06, 0xff, 0x00, 0xaa, 0xbb, 0xcc };
    assert_record_ext(want, sizeof want, "re-committed");
}

/* An extension this build does not know (ext_ver 2) is never written: the decoded prefs are read-only, so encode
 * gives nothing, and the commit path refuses an empty commit over the kept tail; core-field saves keep its bytes. */
static void test_prefs_unknown_ext_never_written(void)
{
    const uint8_t v2[DW_EXT_W1_SIZE] = { 0x02, 0x06, 0x01, 0x01, 0x10, 0x05 };
    core_saves_keep(v2, sizeof v2, DW_PREFS_READ_ONLY, DW_BOOT_RAM_ONLY);
    dw_prefs_t p;
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_decode(&p, s_b.lo.tail, s_b.lo.tail_len));
    TEST_ASSERT_TRUE(p.read_only);
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, fk_prefs_boot(omni_boot_local_writable(&s_b), DW_PREFS_READ_ONLY));
    p.mode = DW_MODE_FLIP_OPEN;   /* a settings change in RAM */
    uint8_t buf[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    size_t n = dw_prefs_encode(&p, buf, sizeof buf);
    TEST_ASSERT_EQUAL(0, n);
    TEST_ASSERT_EQUAL(OMNI_TAIL_BAD_LEN, omni_boot_commit_local_tail(&s_b, buf, n));
    TEST_ASSERT_EQUAL_UINT16(sizeof v2, s_b.lo.tail_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v2, s_b.lo.tail, sizeof v2);
    kv_mem_reset_counts(&s_m);
    s_b.lo.face_idx = 3;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));
    TEST_ASSERT_EQUAL(1, s_m.sets);
    assert_record_ext(v2, sizeof v2, "after RAM_ONLY");
    /* defaults reset the taint only through dw_prefs_defaults; decode alone sets it */
    dw_prefs_defaults(&p);
    TEST_ASSERT_FALSE(p.read_only);
}

/* ---------------------------------------------------- the upgrade from wave 1's record (WD24, X9; R147 PR1/PR2) */
/* A build whose faces 8 and 9 exist (a later face task's): the limits keep ids < 10, the offer's list adds 8 and 9. */
static const uint8_t k_w2_def[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9 };
static const omni_theme_limits_t k_w2_lim = { 10, 1, 0, sizeof k_w2_def, k_w2_def };
/* the owner's carousel on wave 1 (two faces removed, the order changed) and the face shown (Roundel, index 2) */
static const uint8_t k_own_car[] = { DW_F_CRYSTAL, DW_F_LEARN, DW_F_ROUNDEL, DW_F_ROTOR, DW_F_CIRCLES };
#define OWN_FACE_IDX 2

/* t_dw/local as wave 1 left it on a watch: a saved carousel and face, wave 1's 6-byte extension */
static void put_owner_w1_record(void)
{
    omni_local_t lo;
    omni_local_defaults(&lo, &k_dw_lim);
    memset(lo.carousel, 0, sizeof lo.carousel);   /* as the store writes it: zeros past carousel_len */
    memcpy(lo.carousel, k_own_car, sizeof k_own_car);
    lo.carousel_len = sizeof k_own_car;
    lo.face_idx = OWN_FACE_IDX;
    memcpy(lo.tail, k_golden, sizeof k_golden);
    lo.tail_len = sizeof k_golden;
    uint8_t rec[OMNI_REC_MAX];
    const size_t n = omni_local_encode(&lo, rec, sizeof rec);
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE + DW_EXT_W1_SIZE, n);
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
}

static int local_writes(void)
{
    int k = 0;
    for (int i = 0; i < s_m.sets_ok && i < KV_MEM_LOG; i++) k += strcmp(s_m.log[i], "t_dw/local") == 0;
    return k;
}

/* dw_prefs_after_store's offer as the app runs it (dw_prefs_svc.c), over the boot just run: the decision, then on a
 * writable KEEP or RESET_COMMIT the offer of list over the settings' carousel, and when it changed anything the
 * carousel and the extension committed and saved at once. Returns whether it saved. */
static bool after_store_offer(dw_prefs_t *p, dw_prefs_load_t *r, dw_prefs_boot_t *d, const uint8_t *list, uint8_t n)
{
    *d = decide(p, r);
    omni_settings_t s = s_b.s;
    if (!fk_prefs_boot_offer(omni_boot_local_writable(&s_b), *d) ||
        !fk_offer_list(&p->offered, s.carousel, &s.carousel_len, list, n))
        return false;
    uint8_t ext[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    const size_t en = dw_prefs_encode(p, ext, sizeof ext);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, ext, en));
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s));
    return true;
}

/* A build with no new face (Task 1's; wave 2 Task 5 names its list, wave 1's, since this build's appends #2) on the
 * owner's wave-1 record: LOADED, offered 0x00FF; the offer finds every listed face already offered, so nothing is
 * written on the first boot or the second, and the 6-byte extension stays as it is until a Settings change
 * re-encodes it. */
static void test_prefs_upgrade_w1_this_build_writes_nothing(void)
{
    kv_mem_free(&s_m);
    put_owner_w1_record();
    for (int boot = 0; boot < 2; boot++) {
        boot_dw(false);
        kv_mem_reset_counts(&s_m);
        dw_prefs_t p;
        dw_prefs_load_t r;
        dw_prefs_boot_t d = decide(&p, &r);
        TEST_ASSERT_EQUAL(DW_PREFS_LOADED, r);
        TEST_ASSERT_EQUAL(DW_BOOT_KEEP, d);
        TEST_ASSERT_EQUAL_HEX16(0x00FF, p.offered);
        TEST_ASSERT_TRUE(fk_prefs_boot_offer(omni_boot_local_writable(&s_b), d));
        omni_settings_t s = s_b.s;
        TEST_ASSERT_FALSE(fk_offer_list(&p.offered, s.carousel, &s.carousel_len, k_w1_car, sizeof k_w1_car));
        TEST_ASSERT_EQUAL_UINT8(sizeof k_own_car, s.carousel_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(k_own_car, s.carousel, sizeof k_own_car);
        TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));   /* a save after the boot: t_dw/local as read */
        TEST_ASSERT_EQUAL(0, local_writes());
        assert_record_ext(k_golden, sizeof k_golden, "this build: wave 1's extension untouched");
        TEST_ASSERT_EQUAL_UINT8(OWN_FACE_IDX, omni_boot_face(&s_b));
    }
}

/* A build with faces 8 and 9 on the owner's wave-1 record: the first boot appends 8 and 9 once, at the end, and
 * writes t_dw/local once (the carousel, and the extension at 8 bytes with offered 0x03FF); the face index still
 * points at the same face; nothing else in the record changes. The second boot writes nothing. */
static void test_prefs_upgrade_w1_first_boot_offers_once(void)
{
    kv_mem_free(&s_m);
    put_owner_w1_record();
    uint8_t before[OMNI_REC_MAX];
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    const size_t before_n = e->n;
    memcpy(before, e->val, before_n);

    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    kv_mem_reset_counts(&s_m);
    dw_prefs_t p;
    dw_prefs_load_t r;
    dw_prefs_boot_t d;
    TEST_ASSERT_TRUE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, r);
    TEST_ASSERT_EQUAL(DW_BOOT_KEEP, d);
    TEST_ASSERT_EQUAL(1, local_writes());
    e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE + DW_EXT_V1_SIZE, e->n);
    const uint8_t want_ext[DW_EXT_V1_SIZE] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0xFF, 0x03 };
    assert_record_ext(want_ext, sizeof want_ext, "offered");
    const uint8_t want_car[] = { DW_F_CRYSTAL, DW_F_LEARN, DW_F_ROUNDEL, DW_F_ROTOR, DW_F_CIRCLES, 8, 9 };
    TEST_ASSERT_EQUAL_UINT8(sizeof want_car, e->val[5]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_car, &e->val[6], sizeof want_car);
    TEST_ASSERT_EQUAL_UINT8(OWN_FACE_IDX, e->val[22]);
    TEST_ASSERT_EQUAL_UINT8(DW_F_ROUNDEL, e->val[6 + e->val[22]]);   /* the same face */
    /* nothing else changed: the version, the reveal, the owner's carousel as its head (bytes 2-3, the record's size,
     * grow by the two appended bytes) */
    TEST_ASSERT_EQUAL(before_n + DW_EXT_V1_SIZE - DW_EXT_W1_SIZE, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, e->val, 2);
    TEST_ASSERT_EQUAL_HEX8(before[4], e->val[4]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before + 6, e->val + 6, sizeof k_own_car);

    /* the second boot: nothing offered, nothing written */
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, r);
    TEST_ASSERT_EQUAL_HEX16(0x03FF, p.offered);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));   /* and a save with nothing changed: no t_dw/local */
    TEST_ASSERT_EQUAL(0, local_writes());
}

/* A read-only boot changes nothing: a no-write boot, and a read-only t_dw/local, never offer (RAM or NVS). */
static void test_prefs_upgrade_w1_read_only_boot(void)
{
    kv_mem_free(&s_m);
    put_owner_w1_record();
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, true);   /* no_write in */
    dw_prefs_t p;
    dw_prefs_load_t r;
    dw_prefs_boot_t d;
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, d);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    /* a migration failure (no_write for the boot) on a record that loads: KEEP, but not writable */
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_STR, (const uint8_t *)"x", 1));
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    TEST_ASSERT_TRUE(s_b.no_write);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    TEST_ASSERT_EQUAL_HEX16(0x00FF, p.offered);   /* not even in RAM */
    TEST_ASSERT_EQUAL(0, kv_mem_del(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS));
    /* t_dw/local newer than this build (record version 2): read-only, the defaults, never offered */
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    uint8_t rec[OMNI_REC_MAX];
    const size_t n = e->n;
    memcpy(rec, e->val, n);
    rec[0] = 2;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_dw", OMNI_KEY_LOCAL, KV_T_BLOB, rec, n));
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    TEST_ASSERT_TRUE(s_b.local_ro);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(0, local_writes());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL)->val, n);
}

/* An unknown extension (ext_len 7) on a writable record: RAM_ONLY, never offered, its bytes kept. */
static void test_prefs_upgrade_ext7_never_offered(void)
{
    const uint8_t e7[7] = { 0x01, 0x07, 0x01, 0x01, 0x10, 0x05, 0xFF };
    kv_mem_free(&s_m);
    put_record_with_ext(e7, sizeof e7);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    kv_mem_reset_counts(&s_m);
    dw_prefs_t p;
    dw_prefs_load_t r;
    dw_prefs_boot_t d;
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, r);
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, d);
    TEST_ASSERT_EQUAL(0, local_writes());
    assert_record_ext(e7, sizeof e7, "ext 7 kept");
}

/* The stub's record (RESET_COMMIT): the reset carousel, then the offer over it, committed and saved once; the next
 * boot writes nothing. */
static void test_prefs_upgrade_stub_reset_then_offer(void)
{
    kv_mem_free(&s_m);
    put_stub_record();
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    kv_mem_reset_counts(&s_m);
    dw_prefs_t p;
    dw_prefs_load_t r;
    dw_prefs_boot_t d = decide(&p, &r);
    TEST_ASSERT_EQUAL(DW_BOOT_RESET_COMMIT, d);
    TEST_ASSERT_TRUE(fk_prefs_boot_offer(omni_boot_local_writable(&s_b), d));
    omni_settings_t s = s_b.s;   /* the app's reset: wave 1's carousel here, the face 0 */
    memcpy(s.carousel, k_w1_car, sizeof k_w1_car);
    s.carousel_len = sizeof k_w1_car;
    s_b.lo.face_idx = 0;
    TEST_ASSERT_TRUE(fk_offer_list(&p.offered, s.carousel, &s.carousel_len, k_offer_list, sizeof k_offer_list));
    uint8_t ext[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    const size_t en = dw_prefs_encode(&p, ext, sizeof ext);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, ext, en));
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s));
    TEST_ASSERT_EQUAL(1, local_writes());
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    TEST_ASSERT_EQUAL_UINT8(sizeof k_offer_list, e->val[5]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_offer_list, &e->val[6], sizeof k_offer_list);
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(DW_BOOT_KEEP, d);
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

/* WD24's rollback cost and R147 PR2's recovery: a build with fewer faces (wave 1's limits) clips 8 and 9 and saves the
 * clipped carousel, keeping the extension (offered 0x03FF) byte for byte; back on the later build the faces are not
 * re-offered (the bits stay set) until `dw offer reset` clears them, and then the next boot appends them once. */
static void test_prefs_rollback_then_offer_reset(void)
{
    kv_mem_free(&s_m);
    put_owner_w1_record();
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    dw_prefs_t p;
    dw_prefs_load_t r;
    dw_prefs_boot_t d;
    TEST_ASSERT_TRUE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    /* the rollback build (wave 1's): its limits drop 8 and 9; its first save writes the clipped carousel */
    omni_boot_run(&s_b, &kv, "dw", &k_w1_lim, false, false);
    TEST_ASSERT_EQUAL_UINT8(sizeof k_own_car, s_b.s.carousel_len);
    s_b.lo.face_idx = 1;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));
    const uint8_t ext_w2[DW_EXT_V1_SIZE] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0xFF, 0x03 };
    assert_record_ext(ext_w2, sizeof ext_w2, "rollback keeps the mask");
    /* back on the later build: lost, not re-offered */
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    /* `dw offer reset` on the event loop: the bits of the missing ids cleared, the extension committed */
    TEST_ASSERT_TRUE(fk_offer_reset_list(&p.offered, s_b.s.carousel, s_b.s.carousel_len, k_offer_list,
                                               sizeof k_offer_list));
    uint8_t ext[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    const size_t en = dw_prefs_encode(&p, ext, sizeof ext);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, ext, en));
    TEST_ASSERT_EQUAL(OMNI_KV_OK, save_settings(&s_b.s));
    /* the next boot appends the lost faces once (and 2 and 4, which the owner had removed: the reset's documented
     * reach, every list id the carousel lacks) */
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    TEST_ASSERT_TRUE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    const kv_entry_t *e = kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL);
    const uint8_t want_car[] = { DW_F_CRYSTAL, DW_F_LEARN, DW_F_ROUNDEL, DW_F_ROTOR, DW_F_CIRCLES, 2, 4, 8, 9 };
    TEST_ASSERT_EQUAL_UINT8(sizeof want_car, e->val[5]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_car, &e->val[6], sizeof want_car);
    assert_record_ext(ext_w2, sizeof ext_w2, "offered again");
    omni_boot_run(&s_b, &kv, "dw", &k_w2_lim, false, false);
    kv_mem_reset_counts(&s_m);
    TEST_ASSERT_FALSE(after_store_offer(&p, &r, &d, k_offer_list, sizeof k_offer_list));
    TEST_ASSERT_EQUAL(0, s_m.sets);
}

/* Task 10: the tail as store_local_ext hands it over. Longer than the buffer (nothing copied) is READ_ONLY with the
 * taint, never the defaults a later encode could write over the record's real extension. */
static void test_prefs_load_ext(void)
{
    dw_prefs_t p;
    uint8_t buf[8];
    memcpy(buf, k_golden, sizeof k_golden);
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, dw_prefs_load(&p, buf, sizeof k_golden, sizeof buf));
    TEST_ASSERT_FALSE(p.read_only);
    TEST_ASSERT_EQUAL(DW_PREFS_ABSENT, dw_prefs_load(&p, buf, 0, sizeof buf));
    memcpy(buf, k_golden_v1_8, sizeof k_golden_v1_8);
    TEST_ASSERT_EQUAL(DW_PREFS_LOADED, dw_prefs_load(&p, buf, sizeof k_golden_v1_8, sizeof buf));
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_load(&p, buf, sizeof k_golden_v1_8, sizeof k_golden_v1_8 - 1));
    memset(buf, 0xA5, sizeof buf);   /* what an uncopied buffer may hold */
    TEST_ASSERT_EQUAL(DW_PREFS_READ_ONLY, dw_prefs_load(&p, buf, 233, sizeof buf));
    TEST_ASSERT_TRUE(p.read_only);
    assert_defaults(&p);
    uint8_t enc[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL_size_t(0, dw_prefs_encode(&p, enc, sizeof enc));
    TEST_ASSERT_EQUAL(DW_BOOT_RAM_ONLY, fk_prefs_boot(true, DW_PREFS_READ_ONLY));
}

/* Task 10: the four Settings rows' fields (Mode, Chime, Quiet from, Quiet until). */
static void test_prefs_fields(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_ALWAYS_ON, dw_prefs_get(&p, DW_PF_MODE));
    TEST_ASSERT_EQUAL_UINT8(0, dw_prefs_get(&p, DW_PF_CHIME));
    TEST_ASSERT_EQUAL_UINT8(22, dw_prefs_get(&p, DW_PF_QUIET_FROM));
    TEST_ASSERT_EQUAL_UINT8(7, dw_prefs_get(&p, DW_PF_QUIET_TO));
    TEST_ASSERT_TRUE(dw_prefs_set(&p, DW_PF_MODE, DW_MODE_FLIP_OPEN));
    TEST_ASSERT_TRUE(dw_prefs_set(&p, DW_PF_CHIME, 1));
    TEST_ASSERT_TRUE(dw_prefs_set(&p, DW_PF_QUIET_FROM, 23));
    TEST_ASSERT_TRUE(dw_prefs_set(&p, DW_PF_QUIET_TO, 0));
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_FLIP_OPEN, p.mode);
    TEST_ASSERT_TRUE(p.chime_on);
    TEST_ASSERT_EQUAL_UINT8(23, p.quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(0, p.quiet_to_h);
    /* out of range or unknown: refused, nothing changes */
    const dw_prefs_t before = p;
    TEST_ASSERT_FALSE(dw_prefs_set(&p, DW_PF_MODE, 2));
    TEST_ASSERT_FALSE(dw_prefs_set(&p, DW_PF_CHIME, 2));
    TEST_ASSERT_FALSE(dw_prefs_set(&p, DW_PF_QUIET_FROM, 24));
    TEST_ASSERT_FALSE(dw_prefs_set(&p, DW_PF_QUIET_TO, 255));
    TEST_ASSERT_FALSE(dw_prefs_set(&p, DW_PF__COUNT, 0));
    TEST_ASSERT_EQUAL_MEMORY(&before, &p, sizeof p);
    TEST_ASSERT_EQUAL_UINT8(0, dw_prefs_get(&p, DW_PF__COUNT));
    /* the taint survives an edit: a read-only extension stays unwritable */
    p.read_only = true;
    TEST_ASSERT_TRUE(dw_prefs_set(&p, DW_PF_CHIME, 0));
    TEST_ASSERT_TRUE(p.read_only);
}

/* Task 10 (B3): the snapshot the loop publishes for other tasks, one 32-bit word. */
static void test_prefs_pack(void)
{
    dw_prefs_t p;
    dw_prefs_defaults(&p);
    p.mode = DW_MODE_FLIP_OPEN;
    p.chime_on = true;
    p.quiet_from_h = 21;
    p.quiet_to_h = 6;
    const uint32_t w = dw_prefs_pack(&p);
    TEST_ASSERT_EQUAL_UINT8(DW_MODE_FLIP_OPEN, dw_prefs_packed(w, DW_PF_MODE));
    TEST_ASSERT_EQUAL_UINT8(1, dw_prefs_packed(w, DW_PF_CHIME));
    TEST_ASSERT_EQUAL_UINT8(21, dw_prefs_packed(w, DW_PF_QUIET_FROM));
    TEST_ASSERT_EQUAL_UINT8(6, dw_prefs_packed(w, DW_PF_QUIET_TO));
    TEST_ASSERT_EQUAL_UINT8(0, dw_prefs_packed(w, DW_PF__COUNT));
    for (int f = 0; f < DW_PF__COUNT; f++) TEST_ASSERT_EQUAL_UINT8(dw_prefs_get(&p, f), dw_prefs_packed(w, f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_prefs_golden_v1_8);
    RUN_TEST(test_prefs_w1_record_loaded);
    RUN_TEST(test_prefs_w1_record_tail_kept_by_w1);
    RUN_TEST(test_prefs_ext7_read_only);
    RUN_TEST(test_prefs_upgrade_from_stub);
    RUN_TEST(test_prefs_boot_absent_writable);
    RUN_TEST(test_prefs_boot_stub_record);
    RUN_TEST(test_prefs_boot_local_ro);
    RUN_TEST(test_prefs_boot_no_write);
    RUN_TEST(test_prefs_boot_newer);
    RUN_TEST(test_prefs_boot_unknown);
    RUN_TEST(test_prefs_newer_ext_kept);
    RUN_TEST(test_prefs_unknown_ext_read_only);
    RUN_TEST(test_prefs_validate);
    RUN_TEST(test_lift_flip_open_home);
    RUN_TEST(test_lift_always_on);
    RUN_TEST(test_lift_home_missing);
    RUN_TEST(test_lift_era_classic_home);
    RUN_TEST(test_lift_era_modern_home);
    RUN_TEST(test_lift_era_fallback);
    RUN_TEST(test_lift_always_on_any_era);
    RUN_TEST(test_face_era_table);
    RUN_TEST(test_face_ids);
    RUN_TEST(test_prefs_def_carousel_matches_count);
    RUN_TEST(test_prefs_offer_appends_once);
    RUN_TEST(test_prefs_offer_keeps_order_and_removed);
    RUN_TEST(test_prefs_offer_full_carousel);
    RUN_TEST(test_prefs_offer_present_not_duplicated);
    RUN_TEST(test_prefs_boot_offer_read_only);
    RUN_TEST(test_prefs_offer_reset);
    RUN_TEST(test_prefs_offer_reset_noop);
    RUN_TEST(test_prefs_offer_reset_verdict);
    RUN_TEST(test_prefs_core_saves_keep_ext);
    RUN_TEST(test_prefs_core_saves_keep_v1_8);
    RUN_TEST(test_prefs_core_saves_keep_newer_ext);
    RUN_TEST(test_prefs_unknown_ext_never_written);
    RUN_TEST(test_prefs_upgrade_w1_this_build_writes_nothing);
    RUN_TEST(test_prefs_upgrade_w1_first_boot_offers_once);
    RUN_TEST(test_prefs_upgrade_w1_read_only_boot);
    RUN_TEST(test_prefs_upgrade_ext7_never_offered);
    RUN_TEST(test_prefs_upgrade_stub_reset_then_offer);
    RUN_TEST(test_prefs_rollback_then_offer_reset);
    RUN_TEST(test_prefs_load_ext);
    RUN_TEST(test_prefs_fields);
    RUN_TEST(test_prefs_pack);
    kv_mem_free(&s_m);
    return UNITY_END();
}
