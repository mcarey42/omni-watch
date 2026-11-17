/* rd_core's preferences (rd/prefs.h) and face table (rd/faces.h), rd wave 1 Task 4: t_rd/local's extension (t_dw's
 * rules over rd's fields), the boot decision, the carousel offer, the lift rule, and the first rd boot over a synthetic
 * NVS image (PR6: the real boot policy, omni/boot.h, over the in-memory kv; no host store exists and the service's
 * after_store is device code, so its steps are run here as it runs them). Every count is asserted against the
 * constants, never a literal, so no face task edits this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "kv_mem.h"
#include "rd/faces.h"
#include "rd/prefs.h"
#include "omni/batrec.h"
#include "omni/boot.h"
#include "omni/clockcal.h"
#include "omni/local.h"
#include "omni/migrate.h"
#include "omni/settings.h"

void setUp(void) {}
void tearDown(void) {}

/* the defaults (wave 2: offered 0x3FFF, ids 0-13): 01 08 00 00 16 07 FF 3F */
static const uint8_t k_golden_v1[RD_EXT_V1_SIZE] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0xFF, 0x3F };
/* wave 1's defaults (offered 0x007F, ids 0-6): the record a wave-1 build left on the watch */
static const uint8_t k_w1_ext[RD_EXT_V1_SIZE] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0x7F, 0x00 };
/* the family order (spec section 6), written out: the wave-2 default carousel and the offer's list */
static const uint8_t k_family[] = { 0, 7, 8, 9, 10, 2, 1, 3, 11, 5, 12, 4, 13 };

static void assert_defaults(const rd_prefs_t *p)
{
    TEST_ASSERT_EQUAL_UINT8(RD_MODE_ALWAYS_ON, p->mode);
    TEST_ASSERT_EQUAL_UINT8(0, p->chime_on);
    TEST_ASSERT_EQUAL_UINT8(22, p->quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(7, p->quiet_to_h);
    TEST_ASSERT_EQUAL_HEX16(RD_OFFERED_DEF, p->offered);
    TEST_ASSERT_EQUAL_UINT8(0, p->tail_len);
}

static void test_rd_prefs_golden_v1(void)
{
    TEST_ASSERT_EQUAL_INT(1, RD_EXT_VERSION);
    TEST_ASSERT_EQUAL_INT(8, RD_EXT_V1_SIZE);
    TEST_ASSERT_EQUAL_INT(64, RD_EXT_TAIL_MAX);
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, RD_OFFERED_DEF);
    rd_prefs_t p;
    memset(&p, 0xee, sizeof p);
    rd_prefs_defaults(&p);
    assert_defaults(&p);
    TEST_ASSERT_FALSE(p.read_only);
    uint8_t buf[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(RD_EXT_V1_SIZE, rd_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_golden_v1, buf, RD_EXT_V1_SIZE);
    TEST_ASSERT_EQUAL(0, rd_prefs_encode(&p, buf, RD_EXT_V1_SIZE - 1));   /* too small a buffer: nothing */
    /* offered is little-endian at bytes 6-7, and the golden decodes back to the defaults */
    p.offered = 0x8102;
    TEST_ASSERT_EQUAL(RD_EXT_V1_SIZE, rd_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0x81, buf[7]);
    rd_prefs_t q;
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, rd_prefs_decode(&q, k_golden_v1, sizeof k_golden_v1));
    assert_defaults(&q);
    TEST_ASSERT_FALSE(q.read_only);
}

static void test_rd_prefs_tail_kept(void)
{
    const uint8_t ext[10] = { 0x01, 0x0A, 0x01, 0x01, 0x15, 0x06, 0x7F, 0x00, 0xAA, 0xBB };
    rd_prefs_t p;
    TEST_ASSERT_EQUAL(FK_LOAD_TAIL_KEPT, rd_prefs_decode(&p, ext, sizeof ext));
    TEST_ASSERT_EQUAL_UINT8(RD_MODE_STASIS, p.mode);
    TEST_ASSERT_EQUAL_UINT8(1, p.chime_on);
    TEST_ASSERT_EQUAL_UINT8(21, p.quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(6, p.quiet_to_h);
    TEST_ASSERT_EQUAL_HEX16(0x007F, p.offered);
    TEST_ASSERT_EQUAL_UINT8(2, p.tail_len);
    TEST_ASSERT_FALSE(p.read_only);
    uint8_t buf[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(sizeof ext, rd_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ext, buf, sizeof ext);
    /* the longest tail kept (64) is still read and re-encoded byte for byte */
    uint8_t big[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    memcpy(big, k_golden_v1, RD_EXT_V1_SIZE);
    for (int i = RD_EXT_V1_SIZE; i < (int)sizeof big; i++) big[i] = (uint8_t)(i * 7);
    big[1] = sizeof big;
    TEST_ASSERT_EQUAL(FK_LOAD_TAIL_KEPT, rd_prefs_decode(&p, big, sizeof big));
    TEST_ASSERT_EQUAL(sizeof big, rd_prefs_encode(&p, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, buf, sizeof big);
}

static void assert_read_only(const uint8_t *ext, size_t n, const char *what)
{
    rd_prefs_t p;
    memset(&p, 0xee, sizeof p);
    TEST_ASSERT_EQUAL_MESSAGE(FK_LOAD_READ_ONLY, rd_prefs_decode(&p, ext, n), what);
    assert_defaults(&p);
    TEST_ASSERT_TRUE_MESSAGE(p.read_only, what);
    uint8_t buf[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL_MESSAGE(0, rd_prefs_encode(&p, buf, sizeof buf), what);
    /* an edit in RAM keeps the taint: still never written */
    TEST_ASSERT_TRUE(rd_prefs_set(&p, RD_PF_MODE, RD_MODE_STASIS));
    TEST_ASSERT_TRUE_MESSAGE(p.read_only, what);
    TEST_ASSERT_EQUAL_MESSAGE(0, rd_prefs_encode(&p, buf, sizeof buf), what);
}

static void test_rd_prefs_read_only(void)
{
    const uint8_t v2[RD_EXT_V1_SIZE] = { 0x02, 0x08, 0x01, 0x01, 0x15, 0x06, 0x7F, 0x00 };
    assert_read_only(v2, sizeof v2, "ext_ver 2");
    const uint8_t l5[5] = { 0x01, 0x05, 0x01, 0x01, 0x15 };
    assert_read_only(l5, sizeof l5, "ext_len 5");
    const uint8_t l7[7] = { 0x01, 0x07, 0x01, 0x01, 0x15, 0x06, 0x7F };
    assert_read_only(l7, sizeof l7, "ext_len 7");
    uint8_t t65[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX + 1];
    memset(t65, 0x5A, sizeof t65);
    memcpy(t65, k_golden_v1, RD_EXT_V1_SIZE);
    t65[1] = sizeof t65;
    assert_read_only(t65, sizeof t65, "a 65-byte tail");
    /* ext_len that is not the record's own length, and a single byte */
    const uint8_t lie[RD_EXT_V1_SIZE + 1] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0x7F, 0x00, 0x00 };
    assert_read_only(lie, sizeof lie, "ext_len 8 in 9 bytes");
    const uint8_t one[1] = { 0x01 };
    assert_read_only(one, sizeof one, "one byte");
    /* the store's over-long tail (n > cap: nothing copied) is read-only too; within cap it decodes */
    rd_prefs_t p;
    TEST_ASSERT_EQUAL(FK_LOAD_READ_ONLY, rd_prefs_load(&p, NULL, 300, 233));
    assert_defaults(&p);
    TEST_ASSERT_TRUE(p.read_only);
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, rd_prefs_load(&p, k_golden_v1, sizeof k_golden_v1, 233));
    TEST_ASSERT_FALSE(p.read_only);
    TEST_ASSERT_EQUAL(FK_LOAD_ABSENT, rd_prefs_load(&p, NULL, 0, 233));
    assert_defaults(&p);
    TEST_ASSERT_FALSE(p.read_only);
}

/* PR16: t_dw's rules exactly (dw's prefs.c:33-40): an out-of-range field resets to its default, not a clamp */
static void test_rd_prefs_validate(void)
{
    rd_prefs_t p;
    rd_prefs_defaults(&p);
    p.mode = 2;
    TEST_ASSERT_FALSE(rd_prefs_validate(&p));
    TEST_ASSERT_EQUAL_UINT8(RD_MODE_ALWAYS_ON, p.mode);
    p.quiet_from_h = 24;
    TEST_ASSERT_FALSE(rd_prefs_validate(&p));
    TEST_ASSERT_EQUAL_UINT8(22, p.quiet_from_h);
    p.quiet_to_h = 24;
    TEST_ASSERT_FALSE(rd_prefs_validate(&p));
    TEST_ASSERT_EQUAL_UINT8(7, p.quiet_to_h);
    p.quiet_from_h = 200;   /* far out of range: the default, never 23 */
    TEST_ASSERT_FALSE(rd_prefs_validate(&p));
    TEST_ASSERT_EQUAL_UINT8(22, p.quiet_from_h);
    /* a valid record: true and unchanged */
    rd_prefs_t v;
    rd_prefs_defaults(&v);
    v.mode = RD_MODE_STASIS, v.chime_on = 1, v.quiet_from_h = 0, v.quiet_to_h = 23, v.offered = 0x1234;
    rd_prefs_t w = v;
    TEST_ASSERT_TRUE(rd_prefs_validate(&v));
    TEST_ASSERT_EQUAL_MEMORY(&w, &v, sizeof v);
    /* decode validates: a stored mode 2 and hour 24 load as the defaults, re-encoded valid */
    const uint8_t bad[RD_EXT_V1_SIZE] = { 0x01, 0x08, 0x02, 0x01, 0x18, 0x18, 0x7F, 0x00 };
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, rd_prefs_decode(&p, bad, sizeof bad));
    TEST_ASSERT_EQUAL_UINT8(RD_MODE_ALWAYS_ON, p.mode);
    TEST_ASSERT_EQUAL_UINT8(22, p.quiet_from_h);
    TEST_ASSERT_EQUAL_UINT8(7, p.quiet_to_h);
    /* the rows' get and set: out of range refused, p unchanged */
    rd_prefs_defaults(&p);
    TEST_ASSERT_FALSE(rd_prefs_set(&p, RD_PF_MODE, 2));
    TEST_ASSERT_FALSE(rd_prefs_set(&p, RD_PF_CHIME, 2));
    TEST_ASSERT_FALSE(rd_prefs_set(&p, RD_PF_QUIET_FROM, 24));
    TEST_ASSERT_FALSE(rd_prefs_set(&p, RD_PF_QUIET_TO, 24));
    TEST_ASSERT_FALSE(rd_prefs_set(&p, RD_PF__COUNT, 0));
    assert_defaults(&p);
    TEST_ASSERT_TRUE(rd_prefs_set(&p, RD_PF_MODE, RD_MODE_STASIS));
    TEST_ASSERT_TRUE(rd_prefs_set(&p, RD_PF_CHIME, 1));
    TEST_ASSERT_TRUE(rd_prefs_set(&p, RD_PF_QUIET_FROM, 23));
    TEST_ASSERT_TRUE(rd_prefs_set(&p, RD_PF_QUIET_TO, 0));
    TEST_ASSERT_EQUAL_UINT8(RD_MODE_STASIS, rd_prefs_get(&p, RD_PF_MODE));
    TEST_ASSERT_EQUAL_UINT8(1, rd_prefs_get(&p, RD_PF_CHIME));
    TEST_ASSERT_EQUAL_UINT8(23, rd_prefs_get(&p, RD_PF_QUIET_FROM));
    TEST_ASSERT_EQUAL_UINT8(0, rd_prefs_get(&p, RD_PF_QUIET_TO));
    TEST_ASSERT_EQUAL_UINT8(0, rd_prefs_get(&p, RD_PF__COUNT));
}

static void test_rd_prefs_offer_once(void)
{
    uint8_t car[RD_CAROUSEL_MAX] = { 0, 2, 4 };
    uint8_t len = 3;
    uint16_t offered = 0x0015;
    TEST_ASSERT_TRUE(fk_offer_list(&offered, car, &len, RD_DEF_CAROUSEL, RD_DEF_CAROUSEL_LEN));
    /* the ids never offered, in the list's (family) order, after the owner's three */
    const uint8_t want[] = { 0, 2, 4, 7, 8, 9, 10, 1, 3, 11, 5, 12, 13 };
    TEST_ASSERT_EQUAL_UINT8(sizeof want, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, car, sizeof want);
    TEST_ASSERT_EQUAL_HEX16(0x3FBF, offered);
    TEST_ASSERT_EQUAL_HEX16(0, offered & (1u << RD_F_DIGITAL));   /* 6 is not in the list: untouched */
    uint8_t before[RD_CAROUSEL_MAX];
    memcpy(before, car, sizeof car);
    TEST_ASSERT_FALSE(fk_offer_list(&offered, car, &len, RD_DEF_CAROUSEL, RD_DEF_CAROUSEL_LEN));
    TEST_ASSERT_EQUAL_UINT8(sizeof want, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before, car, sizeof car);
    TEST_ASSERT_EQUAL_HEX16(0x3FBF, offered);
    /* a fresh record has offered every list id already: nothing to append */
    rd_prefs_t p;
    rd_prefs_defaults(&p);
    uint8_t c2[RD_CAROUSEL_MAX] = { RD_F_STASIS };
    uint8_t l2 = 1;
    TEST_ASSERT_FALSE(fk_offer_list(&p.offered, c2, &l2, RD_DEF_CAROUSEL, RD_DEF_CAROUSEL_LEN));
    TEST_ASSERT_EQUAL_UINT8(1, l2);
}

/* ---------------------------------------------------------------- wave 2: the family order and the offer (spec 5, 6) */
static void test_rd_prefs_w2_defaults(void)
{
    TEST_ASSERT_EQUAL_INT(14, RD_F__COUNT);
    TEST_ASSERT_EQUAL_INT(sizeof k_family, RD_DEF_CAROUSEL_LEN);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_family, RD_DEF_CAROUSEL, sizeof k_family);
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, RD_OFFERED_DEF);
    /* a fresh record (ABSENT, writable): RESET_COMMIT to the 13 ids and offered 0x3FFF, nothing left to offer */
    rd_prefs_t p;
    TEST_ASSERT_EQUAL(FK_LOAD_ABSENT, rd_prefs_decode(&p, NULL, 0));
    uint8_t car[RD_CAROUSEL_MAX];
    memset(car, 0xEE, sizeof car);
    uint8_t len = 0;
    const rd_after_store_t a = rd_prefs_after_store_decide(&p, FK_LOAD_ABSENT, true, car, &len);
    TEST_ASSERT_EQUAL(FK_BOOT_RESET_COMMIT, a.d);
    TEST_ASSERT_TRUE(a.reset);
    TEST_ASSERT_FALSE(a.offered);
    TEST_ASSERT_TRUE(a.save);
    TEST_ASSERT_EQUAL_UINT8(sizeof k_family, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_family, car, sizeof k_family);
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, p.offered);
    uint8_t ext[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(RD_EXT_V1_SIZE, rd_prefs_encode(&p, ext, sizeof ext));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_golden_v1, ext, RD_EXT_V1_SIZE);
}

/* the offer over a carousel with offered as given (writable, LOADED): returns the decision */
static rd_after_store_t w2_offer(rd_prefs_t *p, uint16_t offered, uint8_t car[RD_CAROUSEL_MAX], uint8_t *len)
{
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, rd_prefs_decode(p, k_w1_ext, sizeof k_w1_ext));
    p->offered = offered;
    return rd_prefs_after_store_decide(p, FK_LOAD_LOADED, true, car, len);
}

/* A wave-1 record (ext 01 08 00 00 16 07 7F 00, carousel 0-5): LOADED; the offer appends 7-13 in the list's order and
 * sets every listed id's bit (0x3FFF: bit 6 was set already); a second offer changes nothing */
static void test_rd_prefs_w2_offer_w1_record(void)
{
    rd_prefs_t p;
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, rd_prefs_decode(&p, k_w1_ext, sizeof k_w1_ext));
    TEST_ASSERT_EQUAL_HEX16(0x007F, p.offered);
    TEST_ASSERT_FALSE(p.read_only);
    uint8_t car[RD_CAROUSEL_MAX] = { 0, 1, 2, 3, 4, 5 };   /* the store's bytes: zeros past carousel_len */
    uint8_t len = 6;
    rd_after_store_t a = rd_prefs_after_store_decide(&p, FK_LOAD_LOADED, true, car, &len);
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, a.d);
    TEST_ASSERT_FALSE(a.reset);
    TEST_ASSERT_TRUE(a.offered);
    TEST_ASSERT_TRUE(a.save);
    const uint8_t want[RD_CAROUSEL_MAX] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 0, 0, 0 };
    TEST_ASSERT_EQUAL_UINT8(13, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, car, RD_CAROUSEL_MAX);
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, p.offered);
    uint8_t ext[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    TEST_ASSERT_EQUAL(RD_EXT_V1_SIZE, rd_prefs_encode(&p, ext, sizeof ext));
    const uint8_t want_ext[RD_EXT_V1_SIZE] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0xFF, 0x3F };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_ext, ext, RD_EXT_V1_SIZE);
    /* the second call (the next boot): nothing to offer, every byte as it was */
    rd_prefs_t was = p;
    a = rd_prefs_after_store_decide(&p, FK_LOAD_LOADED, true, car, &len);
    TEST_ASSERT_FALSE(a.reset || a.offered || a.save);
    TEST_ASSERT_EQUAL_UINT8(13, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, car, RD_CAROUSEL_MAX);
    TEST_ASSERT_EQUAL_MEMORY(&was, &p, sizeof p);
    TEST_ASSERT_EQUAL(RD_EXT_V1_SIZE, rd_prefs_encode(&p, ext, sizeof ext));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_ext, ext, RD_EXT_V1_SIZE);
}

/* The owner's own order (3, 0, 5: wave-1 faces 1, 2 and 4 removed) stays first; 7-13 follow; 1, 2, 4 never come back */
static void test_rd_prefs_w2_offer_owner_order(void)
{
    rd_prefs_t p;
    uint8_t car[RD_CAROUSEL_MAX] = { 3, 0, 5 };
    uint8_t len = 3;
    const rd_after_store_t a = w2_offer(&p, 0x007F, car, &len);
    TEST_ASSERT_TRUE(a.offered && a.save);
    const uint8_t want[RD_CAROUSEL_MAX] = { 3, 0, 5, 7, 8, 9, 10, 11, 12, 13 };
    TEST_ASSERT_EQUAL_UINT8(10, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, car, RD_CAROUSEL_MAX);
    for (uint8_t i = 0; i < len; i++) {
        TEST_ASSERT_NOT_EQUAL(1, car[i]);
        TEST_ASSERT_NOT_EQUAL(2, car[i]);
        TEST_ASSERT_NOT_EQUAL(4, car[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, p.offered);
}

/* Bay Mouth (7) offered once already and removed by the owner (offered 0x00FF): 8-13 are appended, 7 is not */
static void test_rd_prefs_w2_offer_partial(void)
{
    rd_prefs_t p;
    uint8_t car[RD_CAROUSEL_MAX] = { 0, 1, 2, 3, 4, 5 };
    uint8_t len = 6;
    const rd_after_store_t a = w2_offer(&p, 0x00FF, car, &len);
    TEST_ASSERT_TRUE(a.offered && a.save);
    const uint8_t want[RD_CAROUSEL_MAX] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13 };
    TEST_ASSERT_EQUAL_UINT8(12, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, car, RD_CAROUSEL_MAX);
    for (uint8_t i = 0; i < len; i++) TEST_ASSERT_NOT_EQUAL(RD_F_BAY, car[i]);
    TEST_ASSERT_EQUAL_HEX16(0x3FFF, p.offered);
}

/* A READ_ONLY record, or a no-write boot (writable false), offers nothing: p, the carousel and the bytes unchanged */
static void test_rd_prefs_w2_offer_read_only(void)
{
    const uint8_t v2[RD_EXT_V1_SIZE] = { 0x02, 0x08, 0x00, 0x00, 0x16, 0x07, 0x7F, 0x00 };
    const uint8_t w1_car[RD_CAROUSEL_MAX] = { 0, 1, 2, 3, 4, 5 };
    uint8_t ext[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    for (int k = 0; k < 4; k++) {
        rd_prefs_t p;
        fk_load_t r;
        bool writable;
        if (k == 0) {   /* an unknown version: READ_ONLY (offered as wave 1 left it, in RAM, to show it stays) */
            r = rd_prefs_decode(&p, v2, sizeof v2);
            TEST_ASSERT_EQUAL(FK_LOAD_READ_ONLY, r);
            p.offered = 0x007F;
            writable = true;
        } else if (k == 1) {   /* the same on a no-write boot */
            r = rd_prefs_decode(&p, v2, sizeof v2);
            p.offered = 0x007F;
            writable = false;
        } else if (k == 2) {   /* a wave-1 record on a no-write boot */
            r = rd_prefs_decode(&p, k_w1_ext, sizeof k_w1_ext);
            TEST_ASSERT_EQUAL(FK_LOAD_LOADED, r);
            writable = false;
        } else {   /* no record on a no-write boot */
            r = rd_prefs_decode(&p, NULL, 0);
            p.offered = 0x007F;
            writable = false;
        }
        uint8_t car[RD_CAROUSEL_MAX];
        memcpy(car, w1_car, sizeof car);
        uint8_t len = 6;
        const rd_prefs_t was = p;
        const rd_after_store_t a = rd_prefs_after_store_decide(&p, r, writable, car, &len);
        TEST_ASSERT_FALSE_MESSAGE(a.reset || a.offered || a.save, "nothing decided");
        TEST_ASSERT_EQUAL(k == 2 ? FK_BOOT_KEEP : FK_BOOT_RAM_ONLY, a.d);
        TEST_ASSERT_EQUAL_UINT8(6, len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(w1_car, car, RD_CAROUSEL_MAX);
        TEST_ASSERT_EQUAL_MEMORY(&was, &p, sizeof p);
        if (k < 2) {
            TEST_ASSERT_EQUAL(0, rd_prefs_encode(&p, ext, sizeof ext));   /* never written */
        } else if (k == 2) {
            TEST_ASSERT_EQUAL(RD_EXT_V1_SIZE, rd_prefs_encode(&p, ext, sizeof ext));
            TEST_ASSERT_EQUAL_UINT8_ARRAY(k_w1_ext, ext, RD_EXT_V1_SIZE);   /* wave 1's bytes, as read */
        }
    }
}

static void test_rd_prefs_offer_reset_verdict(void)
{
    const uint8_t car[] = { RD_F_STASIS, RD_F_DECK };
    rd_prefs_t p;
    rd_prefs_defaults(&p);
    TEST_ASSERT_EQUAL(RD_OFFER_V_READ_ONLY, rd_prefs_offer_reset_verdict(&p, false, car, sizeof car));
    TEST_ASSERT_EQUAL_HEX16(RD_OFFERED_DEF, p.offered);
    rd_prefs_t ro = p;
    ro.read_only = true;
    TEST_ASSERT_EQUAL(RD_OFFER_V_READ_ONLY, rd_prefs_offer_reset_verdict(&ro, true, car, sizeof car));
    TEST_ASSERT_EQUAL_HEX16(RD_OFFERED_DEF, ro.offered);
    /* the list ids the carousel lacks lose their bit (Digital's, 6, is not in the list: kept) */
    TEST_ASSERT_EQUAL(RD_OFFER_V_RESET, rd_prefs_offer_reset_verdict(&p, true, car, sizeof car));
    TEST_ASSERT_EQUAL_HEX16((1u << RD_F_STASIS) | (1u << RD_F_DECK) | (1u << RD_F_DIGITAL), p.offered);
    TEST_ASSERT_EQUAL(RD_OFFER_V_NOTHING, rd_prefs_offer_reset_verdict(&p, true, car, sizeof car));
}

static void test_rd_prefs_boot_cases(void)
{
    TEST_ASSERT_EQUAL(FK_BOOT_RESET_COMMIT, fk_prefs_boot(true, FK_LOAD_ABSENT));
    TEST_ASSERT_EQUAL(FK_BOOT_RAM_ONLY, fk_prefs_boot(false, FK_LOAD_ABSENT));
    TEST_ASSERT_EQUAL(FK_BOOT_RAM_ONLY, fk_prefs_boot(true, FK_LOAD_READ_ONLY));
    TEST_ASSERT_EQUAL(FK_BOOT_RAM_ONLY, fk_prefs_boot(false, FK_LOAD_READ_ONLY));
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, fk_prefs_boot(true, FK_LOAD_LOADED));
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, fk_prefs_boot(true, FK_LOAD_TAIL_KEPT));
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(true, FK_BOOT_RAM_ONLY));
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(false, FK_BOOT_RAM_ONLY));
    TEST_ASSERT_FALSE(fk_prefs_boot_offer(false, FK_BOOT_KEEP));
    TEST_ASSERT_TRUE(fk_prefs_boot_offer(true, FK_BOOT_KEEP));
    TEST_ASSERT_TRUE(fk_prefs_boot_offer(true, FK_BOOT_RESET_COMMIT));
}

static void test_rd_lift_index(void)
{
    rd_prefs_t p;
    rd_prefs_defaults(&p);
    p.mode = RD_MODE_STASIS;
    const uint8_t c1[] = { 3, 0, 5 };
    TEST_ASSERT_EQUAL_INT(1, rd_lift_index(&p, c1, sizeof c1));
    const uint8_t c2[] = { 3, 5 };
    TEST_ASSERT_EQUAL_INT(-1, rd_lift_index(&p, c2, sizeof c2));
    TEST_ASSERT_EQUAL_INT(-1, rd_lift_index(&p, NULL, 0));
    p.mode = RD_MODE_ALWAYS_ON;
    TEST_ASSERT_EQUAL_INT(-1, rd_lift_index(&p, c1, sizeof c1));
}

static void test_rd_def_carousel_matches_count(void)
{
    TEST_ASSERT_EQUAL_INT(RD_F__COUNT - 1, RD_DEF_CAROUSEL_LEN);
    TEST_ASSERT_TRUE(RD_F__COUNT <= 16);
    uint16_t seen = 0;
    for (int i = 0; i < RD_DEF_CAROUSEL_LEN; i++) {
        const uint8_t id = RD_DEF_CAROUSEL[i];
        TEST_ASSERT_TRUE(id < RD_F__COUNT);
        TEST_ASSERT_NOT_EQUAL(RD_F_DIGITAL, id);
        TEST_ASSERT_EQUAL_HEX16(0, seen & (1u << id));   /* no repeat */
        seen |= (uint16_t)(1u << id);
    }
    /* the offered default is every built face id; the registry holds only built faces, each findable by its key */
    TEST_ASSERT_EQUAL_HEX16((1u << RD_F__COUNT) - 1u, RD_OFFERED_DEF);
    TEST_ASSERT_TRUE(RD_FACE_COUNT <= RD_F__COUNT);
    for (unsigned i = 0; i < RD_FACE_COUNT; i++) {
        TEST_ASSERT_NOT_NULL(RD_FACES[i]);
        TEST_ASSERT_EQUAL_PTR(RD_FACES[i], rd_face_by_key(RD_FACES[i]->key));
    }
    TEST_ASSERT_NULL(rd_face_by_key("no-such-face"));
    TEST_ASSERT_NULL(rd_face_by_key(NULL));
}

/* ------------------------------------------------------------- the first rd boot over a synthetic NVS image (PR6) */
static kv_mem_t s_m;      /* static: too large for the stack under ASan */
static omni_boot_t s_b;
/* the other two themes' limits as their descriptors declare them (omni_theme.c; dw_theme.c at wave 2) */
static const uint8_t k_omni_def[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const omni_theme_limits_t k_omni_lim = { 8, 3, 0, sizeof k_omni_def, k_omni_def };
/* Seed-only (the Task 4 review's M5): k_dw_def, k_dw_lim and k_dw_ext below are dw's values written out here so this
 * test does not depend on dw_core's headers. They only build the watch dw left (seed_omni_then_dw); no rd assert reads
 * them, and a dw change that moves them does not make this test stale for rd. */
static const uint8_t k_dw_def[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const omni_theme_limits_t k_dw_lim = { 16, 3, 0, sizeof k_dw_def, k_dw_def };
static const omni_theme_limits_t k_rd_lim = { RD_F__COUNT, 3, 0, RD_DEF_CAROUSEL_LEN, RD_DEF_CAROUSEL };
/* t_dw's extension as dw's first boot commits it (dw/prefs.h's defaults, offered 0xFFFF at 16 faces); seed-only */
static const uint8_t k_dw_ext[] = { 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0xFF, 0xFF };

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

/* The watch as the Omni then dw left it: the Stage 3 store (nvs/'s values, no Wi-Fi), the Omni's boot (the migration
 * to the watch namespace and t_omni/local), then dw's (t_dw/local and its extension committed and saved). watch/theme reads "dw". */
static void seed_omni_then_dw(void)
{
    static uint8_t settings[OMNI_SETTINGS_BLOB_SIZE], clockcal[OMNI_CLK_BLOB_SIZE], batrec[OMNI_BATREC_BLOB_SIZE];
    kv_mem_free(&s_m);
    TEST_ASSERT_EQUAL(sizeof settings, read_hex("settings.hex", settings, sizeof settings));
    TEST_ASSERT_EQUAL(sizeof clockcal, read_hex("clockcal.hex", clockcal, sizeof clockcal));
    TEST_ASSERT_EQUAL(sizeof batrec, read_hex("batrec.hex", batrec, sizeof batrec));
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_SETTINGS, KV_T_BLOB, settings, sizeof settings));
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_CLOCKCAL, KV_T_BLOB, clockcal, sizeof clockcal));
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, OMNI_NS_OLD, OMNI_KEY_BATREC, KV_T_BLOB, batrec, sizeof batrec));
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "omni", &k_omni_lim, true, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s_b.s));
    omni_boot_run(&s_b, &kv, "dw", &k_dw_lim, false, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, k_dw_ext, sizeof k_dw_ext));
    omni_settings_t s = s_b.s;
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    TEST_ASSERT_NOT_NULL(kv_mem_find(&s_m, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL));
    TEST_ASSERT_NOT_NULL(kv_mem_find(&s_m, "t_dw", OMNI_KEY_LOCAL));
    TEST_ASSERT_NULL(kv_mem_find(&s_m, "t_rd", OMNI_KEY_LOCAL));
    const kv_entry_t *th = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_THEME);
    TEST_ASSERT_NOT_NULL(th);
    TEST_ASSERT_EQUAL(2, th->n);
    TEST_ASSERT_EQUAL_MEMORY("dw", th->val, 2);
}

/* rd_prefs_after_store (main/rd_prefs_svc.c) as the boot task runs it, over the boot just run: decode, then the one
 * decision both run (rd_prefs_after_store_decide: the boot verdict, on RESET_COMMIT the default carousel, on a writable
 * KEEP or RESET_COMMIT the offer), then the store's side: on reset the reveal and face, and when anything changed the
 * extension committed and everything saved at once. Returns whether it saved. */
static bool rd_after_store(fk_load_t *r, fk_boot_t *d)
{
    rd_prefs_t p;
    *r = rd_prefs_load(&p, s_b.lo.tail, s_b.lo.tail_len, OMNI_BOOT_TAIL_MAX);
    const bool writable = omni_boot_local_writable(&s_b);
    omni_settings_t s = s_b.s;
    const rd_after_store_t a = rd_prefs_after_store_decide(&p, *r, writable, s.carousel, &s.carousel_len);
    *d = a.d;
    if (a.reset) {
        s.reveal_style = 0;
        s_b.lo.face_idx = 0;   /* store_face_commit(0) */
    }
    if (!a.save) return false;
    uint8_t ext[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    const size_t n = rd_prefs_encode(&p, ext, sizeof ext);
    TEST_ASSERT_EQUAL(RD_EXT_V1_SIZE, n);
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, ext, n));
    omni_kv_t kv = kv_mem_iface(&s_m);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    return true;
}

static char *dump(size_t *len)
{
    char *buf = NULL;
    FILE *f = open_memstream(&buf, len);
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(0, kv_mem_save_lines(&s_m, f));
    TEST_ASSERT_EQUAL(0, fclose(f));
    return buf;
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    char *buf = malloc(65536);
    TEST_ASSERT_NOT_NULL(buf);
    *len = fread(buf, 1, 65535, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

static void test_rd_first_boot_nvs_image(void)
{
    seed_omni_then_dw();
    static kv_mem_t before;   /* the entries by value, to compare t_omni and t_dw byte for byte (static: ASan's stack) */
    size_t before_len;
    char *before_txt = dump(&before_len);
    kv_mem_init(&before);
    FILE *bf = fmemopen(before_txt, before_len, "r");
    TEST_ASSERT_NOT_NULL(bf);
    TEST_ASSERT_EQUAL(0, kv_mem_load_lines(&before, bf));
    fclose(bf);

    /* the first rd boot */
    kv_mem_reset_counts(&s_m);
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_TRUE(s_b.theme_written);
    TEST_ASSERT_EQUAL_STRING("dw", s_b.prev);
    fk_load_t r;
    fk_boot_t d;
    TEST_ASSERT_TRUE(rd_after_store(&r, &d));
    TEST_ASSERT_EQUAL(FK_LOAD_ABSENT, r);
    TEST_ASSERT_EQUAL(FK_BOOT_RESET_COMMIT, d);
    /* only t_rd/local and watch/theme were written */
    for (int i = 0; i < s_m.sets_ok && i < KV_MEM_LOG; i++)
        TEST_ASSERT_TRUE_MESSAGE(strcmp(s_m.log[i], "t_rd/local") == 0 || strcmp(s_m.log[i], "watch/theme") == 0,
                                 s_m.log[i]);
    /* every set attempted succeeded (so the log above saw them all), and none named the old namespace (M3) */
    TEST_ASSERT_EQUAL(s_m.sets, s_m.sets_ok);
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
    TEST_ASSERT_TRUE(s_m.sets_ok <= KV_MEM_LOG);
    const kv_entry_t *e = kv_mem_find(&s_m, "t_rd", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE + RD_EXT_V1_SIZE, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_golden_v1, &e->val[OMNI_LOCAL_V1_SIZE], RD_EXT_V1_SIZE);
    TEST_ASSERT_EQUAL_UINT8(RD_DEF_CAROUSEL_LEN, e->val[5]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RD_DEF_CAROUSEL, &e->val[6], RD_DEF_CAROUSEL_LEN);
    /* t_omni, t_dw, the old namespace and the shared records: untouched */
    static const char *const SAME[][2] = { { OMNI_NS_T_OMNI, OMNI_KEY_LOCAL }, { "t_dw", OMNI_KEY_LOCAL },
                                           { OMNI_NS_OLD, OMNI_KEY_SETTINGS }, { OMNI_NS_OLD, OMNI_KEY_CLOCKCAL },
                                           { OMNI_NS_OLD, OMNI_KEY_BATREC }, { OMNI_NS_WATCH, OMNI_KEY_SHARED },
                                           { OMNI_NS_WATCH, OMNI_KEY_CLOCKCAL }, { OMNI_NS_WATCH, OMNI_KEY_BATREC } };
    for (size_t i = 0; i < sizeof SAME / sizeof SAME[0]; i++) {
        const kv_entry_t *a = kv_mem_find(&before, SAME[i][0], SAME[i][1]), *b = kv_mem_find(&s_m, SAME[i][0],
                                                                                             SAME[i][1]);
        TEST_ASSERT_NOT_NULL_MESSAGE(a, SAME[i][1]);
        TEST_ASSERT_NOT_NULL_MESSAGE(b, SAME[i][1]);
        TEST_ASSERT_EQUAL_MESSAGE(a->n, b->n, SAME[i][0]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(a->val, b->val, a->n, SAME[i][0]);
    }
    TEST_ASSERT_EQUAL(before.count + 1, s_m.count);   /* one entry added: t_rd/local */

    /* the whole image against the committed expectation */
    size_t got_len, want_len;
    char *got = dump(&got_len);
    char *want = read_file(FIXTURES_DIR "/nvs_rd_first_boot.txt", &want_len);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, "fixtures/nvs_rd_first_boot.txt");

    /* the second boot: LOADED, KEEP, nothing offered, zero writes; a save with nothing changed writes nothing */
    kv_mem_reset_counts(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, false);
    TEST_ASSERT_FALSE(rd_after_store(&r, &d));
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, r);
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, d);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s_b.s));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    size_t again_len;
    char *again = dump(&again_len);
    TEST_ASSERT_EQUAL_STRING(got, again);

    /* a no-write boot over the same image changes nothing either */
    kv_mem_reset_counts(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, true);
    TEST_ASSERT_FALSE(rd_after_store(&r, &d));
    TEST_ASSERT_EQUAL(FK_BOOT_RAM_ONLY, d);
    TEST_ASSERT_EQUAL(0, s_m.sets);

    free(before_txt);
    free(got);
    free(want);
    free(again);
    kv_mem_free(&before);
    kv_mem_free(&s_m);
}

/* rd_prefs_after_store_decide (M1) on its own: the cases the service meets at boot. */
static void test_rd_after_store_decide(void)
{
    rd_prefs_t p;
    uint8_t car[RD_CAROUSEL_MAX];
    uint8_t len;

    /* the first rd boot (ABSENT, writable): reset to the default carousel (the bytes past it kept), saved */
    rd_prefs_defaults(&p);
    memset(car, 0xEE, sizeof car);
    car[0] = 9;
    len = 1;
    rd_after_store_t a = rd_prefs_after_store_decide(&p, FK_LOAD_ABSENT, true, car, &len);
    TEST_ASSERT_EQUAL(FK_BOOT_RESET_COMMIT, a.d);
    TEST_ASSERT_TRUE(a.reset);
    TEST_ASSERT_TRUE(a.save);
    TEST_ASSERT_FALSE(a.offered);   /* a fresh record has offered every built face */
    TEST_ASSERT_EQUAL(RD_DEF_CAROUSEL_LEN, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RD_DEF_CAROUSEL, car, RD_DEF_CAROUSEL_LEN);
    for (int i = RD_DEF_CAROUSEL_LEN; i < RD_CAROUSEL_MAX; i++) TEST_ASSERT_EQUAL_HEX8(0xEE, car[i]);
    TEST_ASSERT_EQUAL_HEX16(RD_OFFERED_DEF, p.offered);

    /* the second boot (LOADED, writable, everything offered): nothing to do, nothing saved */
    memcpy(car, RD_DEF_CAROUSEL, RD_DEF_CAROUSEL_LEN);
    len = RD_DEF_CAROUSEL_LEN;
    a = rd_prefs_after_store_decide(&p, FK_LOAD_LOADED, true, car, &len);
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, a.d);
    TEST_ASSERT_FALSE(a.reset || a.offered || a.save);
    TEST_ASSERT_EQUAL(RD_DEF_CAROUSEL_LEN, len);

    /* a KEEP with a face never offered: appended, the bit set, saved */
    p.offered = (uint16_t)(RD_OFFERED_DEF & ~(1u << RD_DEF_CAROUSEL[1]));
    uint8_t short_car[RD_CAROUSEL_MAX] = { RD_DEF_CAROUSEL[0] };
    len = 1;
    a = rd_prefs_after_store_decide(&p, FK_LOAD_TAIL_KEPT, true, short_car, &len);
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, a.d);
    TEST_ASSERT_TRUE(a.offered);
    TEST_ASSERT_TRUE(a.save);
    TEST_ASSERT_FALSE(a.reset);
    TEST_ASSERT_EQUAL(2, len);
    TEST_ASSERT_EQUAL(RD_DEF_CAROUSEL[1], short_car[1]);
    TEST_ASSERT_EQUAL_HEX16(RD_OFFERED_DEF, p.offered);

    /* read-only record, or a no-write boot: RAM only, nothing changes in RAM or NVS */
    const fk_load_t rs[] = { FK_LOAD_READ_ONLY, FK_LOAD_ABSENT, FK_LOAD_LOADED };
    const bool ws[] = { true, false, false };
    for (size_t i = 0; i < sizeof rs / sizeof rs[0]; i++) {
        rd_prefs_defaults(&p);
        p.offered = 0;
        uint8_t c2[RD_CAROUSEL_MAX] = { RD_DEF_CAROUSEL[0] };
        len = 1;
        a = rd_prefs_after_store_decide(&p, rs[i], ws[i], c2, &len);
        TEST_ASSERT_FALSE(a.reset || a.offered || a.save);
        TEST_ASSERT_EQUAL(1, len);
        TEST_ASSERT_EQUAL_HEX16(0, p.offered);
        TEST_ASSERT_EQUAL(rs[i] == FK_LOAD_LOADED ? FK_BOOT_KEEP : FK_BOOT_RAM_ONLY, a.d);
    }
}

/* M2: an rd boot over t_rd/local holding an extension this build does not know (ext_ver 2) is READ_ONLY and RAM_ONLY,
 * and writes nothing: zero sets, and the whole image byte for byte as it was. */
static void test_rd_read_only_ext_nvs_image(void)
{
    seed_omni_then_dw();
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, false);
    fk_load_t r;
    fk_boot_t d;
    TEST_ASSERT_TRUE(rd_after_store(&r, &d));   /* the first rd boot: t_rd/local with v1 */
    /* t_rd/local's extension made a newer build's: ext_ver 2, the length and every other byte as they were */
    const kv_entry_t *e = kv_mem_find(&s_m, "t_rd", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(OMNI_LOCAL_V1_SIZE + RD_EXT_V1_SIZE, e->n);
    uint8_t rec[OMNI_LOCAL_V1_SIZE + RD_EXT_V1_SIZE];
    memcpy(rec, e->val, sizeof rec);
    rec[OMNI_LOCAL_V1_SIZE] = RD_EXT_VERSION + 1;
    TEST_ASSERT_EQUAL(0, kv_mem_put(&s_m, "t_rd", OMNI_KEY_LOCAL, KV_T_BLOB, rec, sizeof rec));
    size_t before_len, after_len;
    char *before = dump(&before_len);

    kv_mem_reset_counts(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_FALSE(rd_after_store(&r, &d));
    TEST_ASSERT_EQUAL(FK_LOAD_READ_ONLY, r);
    TEST_ASSERT_EQUAL(FK_BOOT_RAM_ONLY, d);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s_b.s));   /* a save with nothing changed */
    TEST_ASSERT_EQUAL(0, s_m.sets);
    char *after = dump(&after_len);
    TEST_ASSERT_EQUAL_STRING(before, after);
    e = kv_mem_find(&s_m, "t_rd", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(sizeof rec, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rec, e->val, sizeof rec);
    free(before);
    free(after);
    kv_mem_free(&s_m);
}

/* ------------------------------------------- the wave-2 upgrade over the owner's wave-1 image (PR6's shape; spec 5) */
/* rd wave 1's limits as its descriptor declared them (ids 0-6, the default carousel 0-5): seed-only */
static const uint8_t k_rd_w1_def[] = { 0, 1, 2, 3, 4, 5 };
static const omni_theme_limits_t k_rd_w1_lim = { 7, 3, 0, sizeof k_rd_w1_def, k_rd_w1_def };
/* t_rd/local as a wave-1 build's first boot committed it (fixtures/nvs_rd_first_boot.txt at wave 1): the 23 B prefix
 * (version 1, size 31, reveal 0, carousel 0-5, face 0) and wave 1's extension */
static const uint8_t k_w1_local[OMNI_LOCAL_V1_SIZE + RD_EXT_V1_SIZE] = {
    0x01, 0x00, 0x1f, 0x00, 0x00, 0x06, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0x7f, 0x00 };
/* and as the first wave-2 boot leaves it: the carousel 0-5 then 7-13 (length 13), offered 0x3FFF; nothing else */
static const uint8_t k_w2_local[OMNI_LOCAL_V1_SIZE + RD_EXT_V1_SIZE] = {
    0x01, 0x00, 0x1f, 0x00, 0x00, 0x0d, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x16, 0x07, 0xff, 0x3f };

/* The watch as wave 1 left it: the Omni's and dw's boots (seed_omni_then_dw), then a wave-1 rd build's first boot (its
 * limits; its after_store as wave 1 ran it: RESET_COMMIT, the reveal and face to 0, wave 1's extension committed and
 * everything saved). watch/theme reads "rd". */
static void seed_rd_w1(void)
{
    seed_omni_then_dw();
    omni_kv_t kv = kv_mem_iface(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_w1_lim, false, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    omni_settings_t s = s_b.s;
    memset(s.carousel, 0, sizeof s.carousel);
    memcpy(s.carousel, k_rd_w1_def, sizeof k_rd_w1_def);
    s.carousel_len = sizeof k_rd_w1_def;
    s.reveal_style = 0;
    s_b.lo.face_idx = 0;
    TEST_ASSERT_EQUAL(OMNI_TAIL_OK, omni_boot_commit_local_tail(&s_b, k_w1_ext, sizeof k_w1_ext));
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s));
    const kv_entry_t *e = kv_mem_find(&s_m, "t_rd", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(sizeof k_w1_local, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_w1_local, e->val, sizeof k_w1_local);
    const kv_entry_t *th = kv_mem_find(&s_m, OMNI_NS_WATCH, OMNI_KEY_THEME);
    TEST_ASSERT_NOT_NULL(th);
    TEST_ASSERT_EQUAL(2, th->n);
    TEST_ASSERT_EQUAL_MEMORY("rd", th->val, 2);
}

/* The first wave-2 boot on a wave-1 image (the watch namespace, t_omni, t_dw and a wave-1 t_rd/local): LOADED, KEEP, the
 * offer appends 7-13 once; only t_rd/local changes (its carousel bytes and offered), against
 * fixtures/nvs_rd_w2_upgrade.txt. A no-write boot before it, and the second boot after it, write nothing. */
static void test_rd_upgrade_nvs_image(void)
{
    seed_rd_w1();
    static kv_mem_t before;   /* static: ASan's stack */
    size_t before_len;
    char *before_txt = dump(&before_len);
    kv_mem_init(&before);
    FILE *bf = fmemopen(before_txt, before_len, "r");
    TEST_ASSERT_NOT_NULL(bf);
    TEST_ASSERT_EQUAL(0, kv_mem_load_lines(&before, bf));
    fclose(bf);
    TEST_ASSERT_NOT_NULL(kv_mem_find(&before, OMNI_NS_T_OMNI, OMNI_KEY_LOCAL));
    TEST_ASSERT_NOT_NULL(kv_mem_find(&before, "t_dw", OMNI_KEY_LOCAL));
    omni_kv_t kv = kv_mem_iface(&s_m);
    fk_load_t r;
    fk_boot_t d;

    /* a no-write wave-2 boot first: RAM only, nothing offered, zero writes, the image as it was */
    kv_mem_reset_counts(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, true);
    TEST_ASSERT_FALSE(rd_after_store(&r, &d));
    TEST_ASSERT_EQUAL(FK_BOOT_RAM_ONLY, d);
    TEST_ASSERT_EQUAL(0, s_m.sets);
    size_t nw_len;
    char *nw = dump(&nw_len);
    TEST_ASSERT_EQUAL_STRING(before_txt, nw);

    /* the first wave-2 boot */
    kv_mem_reset_counts(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, false);
    TEST_ASSERT_FALSE(s_b.no_write);
    TEST_ASSERT_EQUAL_STRING("rd", s_b.prev);
    TEST_ASSERT_TRUE(rd_after_store(&r, &d));
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, r);
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, d);
    /* only t_rd/local was written, and every set succeeded */
    TEST_ASSERT_TRUE(s_m.sets_ok > 0);
    TEST_ASSERT_TRUE(s_m.sets_ok <= KV_MEM_LOG);
    TEST_ASSERT_EQUAL(s_m.sets, s_m.sets_ok);
    TEST_ASSERT_EQUAL(0, s_m.old_ns_sets);
    for (int i = 0; i < s_m.sets_ok; i++) TEST_ASSERT_EQUAL_STRING_MESSAGE("t_rd/local", s_m.log[i], s_m.log[i]);
    const kv_entry_t *e = kv_mem_find(&s_m, "t_rd", OMNI_KEY_LOCAL);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL(sizeof k_w2_local, e->n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_w2_local, e->val, sizeof k_w2_local);
    /* every other entry byte for byte as it was, and none added */
    TEST_ASSERT_EQUAL(before.count, s_m.count);
    for (int i = 0; i < before.count; i++) {
        const kv_entry_t *a = &before.e[i];
        if (strcmp(a->ns, "t_rd") == 0 && strcmp(a->key, OMNI_KEY_LOCAL) == 0) continue;
        const kv_entry_t *b = kv_mem_find(&s_m, a->ns, a->key);
        TEST_ASSERT_NOT_NULL_MESSAGE(b, a->key);
        TEST_ASSERT_EQUAL_MESSAGE(a->n, b->n, a->key);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(a->val, b->val, a->n, a->key);
    }
    /* the whole image against the committed expectation */
    size_t got_len, want_len;
    char *got = dump(&got_len);
    char *want = read_file(FIXTURES_DIR "/nvs_rd_w2_upgrade.txt", &want_len);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, "fixtures/nvs_rd_w2_upgrade.txt");

    /* the second wave-2 boot: LOADED, KEEP, nothing offered, zero writes (and a save with nothing changed) */
    kv_mem_reset_counts(&s_m);
    omni_boot_run(&s_b, &kv, "rd", &k_rd_lim, false, false);
    TEST_ASSERT_FALSE(rd_after_store(&r, &d));
    TEST_ASSERT_EQUAL(FK_LOAD_LOADED, r);
    TEST_ASSERT_EQUAL(FK_BOOT_KEEP, d);
    TEST_ASSERT_EQUAL(OMNI_KV_OK, omni_boot_save(&s_b, &kv, &s_b.s));
    TEST_ASSERT_EQUAL(0, s_m.sets);
    size_t again_len;
    char *again = dump(&again_len);
    TEST_ASSERT_EQUAL_STRING(got, again);

    free(before_txt);
    free(nw);
    free(got);
    free(want);
    free(again);
    kv_mem_free(&before);
    kv_mem_free(&s_m);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rd_prefs_golden_v1);
    RUN_TEST(test_rd_prefs_tail_kept);
    RUN_TEST(test_rd_prefs_read_only);
    RUN_TEST(test_rd_prefs_validate);
    RUN_TEST(test_rd_prefs_offer_once);
    RUN_TEST(test_rd_prefs_w2_defaults);
    RUN_TEST(test_rd_prefs_w2_offer_w1_record);
    RUN_TEST(test_rd_prefs_w2_offer_owner_order);
    RUN_TEST(test_rd_prefs_w2_offer_partial);
    RUN_TEST(test_rd_prefs_w2_offer_read_only);
    RUN_TEST(test_rd_prefs_offer_reset_verdict);
    RUN_TEST(test_rd_prefs_boot_cases);
    RUN_TEST(test_rd_lift_index);
    RUN_TEST(test_rd_def_carousel_matches_count);
    RUN_TEST(test_rd_first_boot_nvs_image);
    RUN_TEST(test_rd_after_store_decide);
    RUN_TEST(test_rd_read_only_ext_nvs_image);
    RUN_TEST(test_rd_upgrade_nvs_image);
    return UNITY_END();
}
