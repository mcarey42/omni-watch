#pragma once
/* The Mining Ship theme's own preferences: t_rd/local's extension, the tail of the record after the 23 B common prefix
 * (offset 23, spec section 5). The core keeps that tail byte for byte (omni/boot.h); this file gives it a meaning. It
 * follows t_dw's rules exactly (dw/prefs.h), with no older record to read: rd's first build writes v1 at 8 bytes.
 *
 * The extension starts with its own version and length, and fields are only appended:
 *   0 ext_ver | 1 ext_len (the whole extension, these two bytes included) | 2 mode | 3 chime_on | 4 quiet_from_h |
 *   5 quiet_to_h | 6-7 offered (uint16 LE) | 8.. bytes a newer build appended (kept verbatim, at most 64)
 * An ext_ver this build does not know, an ext_len that is not n, an ext_len under 8, or a tail over 64 bytes is
 * read-only: the defaults in RAM, never written (FK_LOAD_READ_ONLY). Every load is validated. Pure: no esp_*, freertos
 * or LVGL (the omni_core purity rule). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/offer.h"   /* fk_load_t, fk_prefs_boot, the offer over a list (face_kit) */

#define RD_EXT_VERSION 1
#define RD_EXT_V1_SIZE 8   /* 0 ext_ver | 1 ext_len | 2 mode | 3 chime_on | 4 quiet_from_h | 5 quiet_to_h | 6-7 offered LE */
#define RD_EXT_TAIL_MAX 64 /* a newer build's appended bytes this build keeps; more is read-only */
#define RD_OFFERED_DEF 0x3FFFu   /* (1 << RD_F__COUNT) - 1, ids 0-13 (wave 2; wave 1 0x007F): a fresh record's */

typedef enum { RD_MODE_ALWAYS_ON = 0, RD_MODE_STASIS = 1 } rd_mode_t;

typedef struct {
    uint8_t mode, chime_on, quiet_from_h, quiet_to_h;   /* rd_mode_t; 0/1; the chime's quiet window, local hours 0-23 */
    uint16_t offered;                 /* bit i: face id i was offered to the carousel once; never validated */
    uint8_t tail[RD_EXT_TAIL_MAX];    /* bytes past V1 that this build does not understand, kept verbatim */
    uint8_t tail_len;
    bool read_only;                   /* decoded READ_ONLY: rd_prefs_encode refuses, so an extension this build does
                                         not know is never written over (only decode and load set it) */
} rd_prefs_t;

void rd_prefs_defaults(rd_prefs_t *p);   /* ALWAYS_ON, chime 0, 22, 7, offered 0x3FFF, no tail, not read-only */
/* mode < 2, hours < 24: an out-of-range field resets to its default (mode Always on, 22, 7), as t_dw's. false if any
 * did. chime_on is normalised to 0/1 by decode and set, so it is never out of range. */
bool rd_prefs_validate(rd_prefs_t *p);
/* ext: t_rd/local's tail (n bytes). The result is always validated; on READ_ONLY and ABSENT p is the defaults, and on
 * READ_ONLY p->read_only is set. LOADED: the known fields, nothing extra; TAIL_KEPT: a newer build's bytes in tail[]. */
fk_load_t rd_prefs_decode(rd_prefs_t *p, const uint8_t *ext, size_t n);
/* The extension's bytes (validated, the tail appended). 0: p->read_only, cap too small, or tail_len over 64. A 0 is
 * never committed: store_local_ext_commit refuses an empty commit over a kept tail (OMNI_TAIL_BAD_LEN). */
size_t rd_prefs_encode(const rd_prefs_t *p, uint8_t *buf, size_t cap);
/* t_rd/local's tail as store_local_ext returned it: n its length, cap the buffer's size. n > cap (the store copied
 * nothing) is READ_ONLY with the taint, like any extension this build cannot read; otherwise rd_prefs_decode. */
fk_load_t rd_prefs_load(rd_prefs_t *p, const uint8_t *ext, size_t n, size_t cap);

/* The fields the theme's Settings rows show (Mode, Chime, Quiet from, Quiet until). get: the value (0 for an unknown
 * field); set: false (p unchanged) for a value out of range (mode < 2, chime < 2, hours < 24) or an unknown field. set
 * keeps read_only: an extension this build does not know stays unwritable after an edit in RAM. */
typedef enum { RD_PF_MODE, RD_PF_CHIME, RD_PF_QUIET_FROM, RD_PF_QUIET_TO, RD_PF__COUNT } rd_pref_field_t;
uint8_t rd_prefs_get(const rd_prefs_t *p, rd_pref_field_t f);
bool rd_prefs_set(rd_prefs_t *p, rd_pref_field_t f, uint8_t v);

/* `rd offer reset`'s verdict, decided on the event loop and published for the console (never inferred from the mask).
 * READ_ONLY: a no-write boot (writable false: store_local_writable()) or a read-only record (p->read_only); p is
 * untouched. Otherwise fk_offer_reset_list over RD_DEF_CAROUSEL: RESET if offered changed, NOTHING if not. RAM_ONLY is
 * the service's (the commit failed, the reset undone); PENDING (0) is the console's "no verdict yet". */
typedef enum {
    RD_OFFER_V_PENDING = 0, RD_OFFER_V_RESET, RD_OFFER_V_NOTHING, RD_OFFER_V_READ_ONLY, RD_OFFER_V_RAM_ONLY
} rd_offer_verdict_t;
rd_offer_verdict_t rd_prefs_offer_reset_verdict(rd_prefs_t *p, bool writable, const uint8_t *carousel, uint8_t len);

/* after_store's decision (rd wave 1 Task 5, the Task 4 review's M1): one pure function that rd_prefs_svc.c's
 * rd_prefs_after_store and test_rd_prefs.c both run, so the host test cannot drift from the device. p: the extension as
 * rd_prefs_load decoded it (r); writable: store_local_writable(); carousel and *len: the store's carousel (16 bytes, as
 * fk_offer_list takes it), updated in place.
 *   d: fk_prefs_boot(writable, r).
 *   reset: d is RESET_COMMIT, so the first RD_DEF_CAROUSEL_LEN bytes are RD_DEF_CAROUSEL and *len its length (the
 *     bytes past it are kept); the caller also sets the reveal to Full (0) and the face to 0.
 *   offered: on a writable KEEP or RESET_COMMIT, fk_offer_list over RD_DEF_CAROUSEL changed the carousel or
 *     p->offered (a read-only boot, record or extension changes nothing, in RAM or NVS).
 *   save: reset or offered. The caller then commits the extension and saves at once; otherwise nothing is written
 *     (the second boot's zero writes). */
typedef struct {
    fk_boot_t d;
    bool reset, offered, save;
} rd_after_store_t;
rd_after_store_t rd_prefs_after_store_decide(rd_prefs_t *p, fk_load_t r, bool writable, uint8_t carousel[16],
                                             uint8_t *len);
