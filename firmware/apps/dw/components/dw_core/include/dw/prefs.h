#pragma once
/* The dw theme's own preferences: t_dw/local's extension, the tail of the record after the 23 B common prefix
 * (offset 23, spec §5, WD3). The core keeps that tail byte for byte (omni/boot.h); this file gives it a meaning.
 *
 * The extension starts with its own version and length, and fields are only appended (v1 grows by appending, WD24):
 *   0 ext_ver | 1 ext_len (the whole extension, these two bytes included) | 2 mode | 3 chime_on | 4 quiet_from_h |
 *   5 quiet_to_h | 6-7 offered (uint16 LE, wave 2) | 8.. bytes a newer build appended (kept verbatim, at most 64)
 * ext_len 6 is wave 1's record (no offered: it decodes LOADED with offered 0x00FF, and is written back at 8 only when
 * something is committed). ext_len 7, an ext_ver this build does not know, or a length that does not add up, is
 * read-only: the defaults in RAM, never written (DW_PREFS_READ_ONLY). A wave-1 build reads this build's 8 bytes as its
 * 6 and a 2-byte tail it keeps verbatim. Pure: no esp_*, freertos or LVGL (the omni_core purity rule). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/offer.h"   /* the offer and boot rules over any list (face_kit, rd wave 1 Task 1) */

#define DW_EXT_VERSION 1
#define DW_EXT_V1_SIZE 8   /* this build's: 0 ext_ver | 1 ext_len | 2 mode | 3 chime_on | 4 quiet_from_h | 5 quiet_to_h |
                              6-7 offered (uint16 LE: bit i, face id i has been offered to the carousel) */
#define DW_EXT_W1_SIZE 6   /* wave 1's record: bytes 0-5 only (read, never written) */
#define DW_EXT_TAIL_MAX 64 /* a newer build's appended bytes this build keeps; more is read-only */
#define DW_OFFERED_DEF 0x00FFu   /* ids 0-7: every face wave 1 had (a fresh record's, and a wave-1 record's) */

typedef enum { DW_MODE_ALWAYS_ON = 0, DW_MODE_FLIP_OPEN = 1 } dw_mode_t;

typedef struct {
    uint8_t mode;                     /* dw_mode_t */
    bool chime_on;
    uint8_t quiet_from_h, quiet_to_h; /* the chime's quiet window, local hours 0-23 */
    uint16_t offered;                 /* bit i: face id i was offered to the carousel once (WD24); never validated */
    uint8_t tail_len;                 /* bytes past V1 that this build does not understand, kept verbatim */
    uint8_t tail[DW_EXT_TAIL_MAX];
    bool read_only;                   /* decoded READ_ONLY: dw_prefs_encode refuses, so an extension this build does
                                         not know is never written over (only dw_prefs_decode sets it) */
} dw_prefs_t;

/* How t_dw/local's extension loaded: face_kit's fk_load_t (rd wave 1 Task 1), under dw's names.
 * LOADED: the known fields, nothing extra; TAIL_KEPT: a newer build's extra bytes are held in tail[] and re-encoded
 * verbatim; ABSENT: no extension (the Stage 4b stub's 23 B record): the defaults; READ_ONLY: an unknown ext_ver, a bad
 * length, or a tail over 64: the defaults, never written. */
typedef fk_load_t dw_prefs_load_t;
#define DW_PREFS_LOADED FK_LOAD_LOADED
#define DW_PREFS_TAIL_KEPT FK_LOAD_TAIL_KEPT
#define DW_PREFS_ABSENT FK_LOAD_ABSENT
#define DW_PREFS_READ_ONLY FK_LOAD_READ_ONLY

void dw_prefs_defaults(dw_prefs_t *p);                   /* ALWAYS_ON, chime off, 22, 7, offered 0x00FF */
bool dw_prefs_validate(dw_prefs_t *p);                   /* mode < 2, hours < 24; false if clamped */
/* ext: t_dw/local's tail (n bytes). The result is always validated; on READ_ONLY and ABSENT p is the defaults, and
 * on READ_ONLY p->read_only is set. */
dw_prefs_load_t dw_prefs_decode(dw_prefs_t *p, const uint8_t *ext, size_t n);
/* The extension's bytes (validated, the tail appended). 0: p->read_only, cap too small, or tail_len over 64. A 0 is
 * never committed: store_local_ext_commit refuses an empty commit over a kept tail (OMNI_TAIL_BAD_LEN). */
size_t dw_prefs_encode(const dw_prefs_t *p, uint8_t *buf, size_t cap);

/* t_dw/local's tail as store_local_ext returned it: n its length, cap the buffer's size. n > cap (the store copied
 * nothing) is READ_ONLY with the taint, like any extension this build cannot read; otherwise dw_prefs_decode. */
dw_prefs_load_t dw_prefs_load(dw_prefs_t *p, const uint8_t *ext, size_t n, size_t cap);

/* The fields the theme's Settings rows show (Mode, Chime, Quiet from, Quiet until). get: the value (0 for an unknown
 * field); set: false (p unchanged) for a value out of range (mode < 2, chime < 2, hours < 24) or an unknown field. set
 * keeps read_only: an extension this build does not know stays unwritable after an edit in RAM. */
typedef enum { DW_PF_MODE, DW_PF_CHIME, DW_PF_QUIET_FROM, DW_PF_QUIET_TO, DW_PF__COUNT } dw_pref_field_t;
uint8_t dw_prefs_get(const dw_prefs_t *p, dw_pref_field_t f);
bool dw_prefs_set(dw_prefs_t *p, dw_pref_field_t f, uint8_t v);
/* The fields in one 32-bit word (a byte each, by field): the snapshot the event loop publishes for other tasks with
 * one aligned store (the Settings screen, the chime timer, B3). */
uint32_t dw_prefs_pack(const dw_prefs_t *p);
uint8_t dw_prefs_packed(uint32_t w, dw_pref_field_t f);

/* The face ids. Theme-local and append-only (WD5). Ids 8-15 are named now; a face exists in a build only once its
 * task raises DW_F__COUNT past it (each face task does, in spec order). */
enum dw_face { DW_F_CRYSTAL = 0, DW_F_ROUNDEL, DW_F_CALLBOX, DW_F_ROTOR, DW_F_CORAL, DW_F_CIRCLES,
               DW_F_DIGITAL, DW_F_LEARN,
               DW_F_ROOM = 8, DW_F_AMBER, DW_F_FOB, DW_F_CONSOLE, DW_F_WALL, DW_F_DOORWAY, DW_F_BROADCAST,
               DW_F_VORTEX, DW_F__ALL };              /* #2, #10, #13, #9, #6, #3, #8, #12 */
#define DW_F__COUNT 16                                /* T5 made it 9, T6 10, T7 11, T9 12, T10 13, T11 14, T12 15, T14 16:
                                                         every named id (the 16-face limit, WD23) */
#define DW_CAROUSEL_MAX 16                            /* OMNI_CAROUSEL_MAX (dw_prefs_svc.c asserts it) */
/* X(id, key, NAME, GROUP, thumb): dw_theme.c's FACES and DW_FACE_KEYS both expand it. NAME is the key upper-cased
 * (main/dw_names.h's DW_NAME_FACE_<NAME>, the title); GROUP names main/dw_names.h's DW_NAME_GROUP_<GROUP>; thumb is the
 * Settings thumbnail's asset file. One row per id, in id order. */
#define DW_FACE_TABLE(X)                                            \
    X(DW_F_CRYSTAL, crystal, CRYSTAL, MODERN, "dw_th_c1.bin")       \
    X(DW_F_ROUNDEL, roundel, ROUNDEL, CLASSIC, "dw_th_r4.bin")      \
    X(DW_F_CALLBOX, callbox, CALLBOX, CLASSIC, "dw_th_b5.bin")      \
    X(DW_F_ROTOR, rotor, ROTOR, CLASSIC, "dw_th_t7.bin")            \
    X(DW_F_CORAL, coral, CORAL, MODERN, "dw_th_k11.bin")            \
    X(DW_F_CIRCLES, circles, CIRCLES, CIRCLES, "dw_th_g14.bin")     \
    X(DW_F_DIGITAL, digital, DIGITAL, FACES, "dw_th_dig.bin")       \
    X(DW_F_LEARN, learn, LEARN, CIRCLES, "dw_th_g16.bin")           \
    X(DW_F_ROOM, room, ROOM, CLASSIC, "dw_th_r2.bin")               \
    X(DW_F_AMBER, amber, AMBER, MODERN, "dw_th_c10.bin")            \
    X(DW_F_FOB, fob, FOB, MODERN, "dw_th_f13.bin")                  \
    X(DW_F_CONSOLE, console, CONSOLE, CLASSIC, "dw_th_p9.bin")      \
    X(DW_F_WALL, wall, WALL, CLASSIC, "dw_th_w6.bin")               \
    X(DW_F_DOORWAY, doorway, DOORWAY, MODERN, "dw_th_d3.bin")       \
    X(DW_F_BROADCAST, broadcast, BROADCAST, CLASSIC, "dw_th_b8.bin") \
    X(DW_F_VORTEX, vortex, VORTEX, MODERN, "dw_th_v12.bin")
/* Each id's key (DW_FACE_TABLE's), by id: the console's face names and the registry's keys (dw_face_by_key) */
extern const char *const DW_FACE_KEYS[DW_F__COUNT];
_Static_assert(DW_F__COUNT <= DW_F__ALL, "a built face has a named id");
_Static_assert(DW_F__ALL <= 16, "offered has a bit for every id");
/* Every built face but the shared Digital, in carousel order: wave 1's, then wave 2's in spec order. A fresh record's
 * carousel, and the offer's list (each face task appends its id and raises the length). */
#define DW_DEF_CAROUSEL_LEN 15
_Static_assert(DW_DEF_CAROUSEL_LEN <= DW_CAROUSEL_MAX, "the default carousel fits");
extern const uint8_t DW_DEF_CAROUSEL[DW_DEF_CAROUSEL_LEN];

/* A face's era (WD24): CLASSIC for ROUNDEL, CALLBOX, ROTOR, ROOM, CONSOLE, WALL, BROADCAST; MODERN otherwise
 * (Circle Script and Digital included, and any id past 15). */
typedef enum { DW_ERA_MODERN = 0, DW_ERA_CLASSIC = 1 } dw_era_t;
dw_era_t dw_face_era(uint8_t id);

/* The carousel index a full-wake lift shows, or -1 (no jump). Flip open only. The home is ROOM for a Classic cur_face
 * and CRYSTAL otherwise; if the home is not in the carousel, the other home; if neither, -1 (WD24). */
int dw_lift_index(const dw_prefs_t *p, const uint8_t *carousel, uint8_t len, uint8_t cur_face);

/* The carousel offer (WD24): fk_offer_list over DW_DEF_CAROUSEL with &p->offered: for each id whose offered bit is
 * clear and which the carousel lacks, append it while *len < 16; then set the bit of every DW_DEF_CAROUSEL id,
 * appended or not. true if carousel, *len or offered changed. Only appends, so a face index keeps pointing at the same
 * face id. */
bool dw_prefs_offer(dw_prefs_t *p, uint8_t carousel[DW_CAROUSEL_MAX], uint8_t *len);
/* R147 PR2(b): fk_offer_reset_list over DW_DEF_CAROUSEL: clear the offered bit of every DW_DEF_CAROUSEL id that the
 * carousel lacks, so the next boot re-appends it. true if offered changed. */
bool dw_prefs_offer_reset(dw_prefs_t *p, const uint8_t *carousel, uint8_t len);
/* `dw offer reset`'s verdict, decided on the event loop and published for the console to print (never inferred from
 * the mask). READ_ONLY: a no-write boot (writable false: store_local_writable()) or a read-only record (p->read_only);
 * p is untouched, in RAM too. Otherwise dw_prefs_offer_reset: RESET if offered changed, NOTHING if not. RAM_ONLY is the
 * service's (the commit failed, the reset undone); PENDING (0) is the console's "no verdict yet". */
typedef enum {
    DW_OFFER_V_PENDING = 0, DW_OFFER_V_RESET, DW_OFFER_V_NOTHING, DW_OFFER_V_READ_ONLY, DW_OFFER_V_RAM_ONLY
} dw_offer_verdict_t;
dw_offer_verdict_t dw_prefs_offer_reset_verdict(dw_prefs_t *p, bool writable, const uint8_t *carousel, uint8_t len);

/* WD6's boot decision (R136 item 5, preflight F2/A5/C2) is face_kit's fk_prefs_boot (writable: store_local_writable(),
 * false on a no-write boot or a read-only t_dw/local), and whether after_store may offer its fk_prefs_boot_offer
 * (fk/offer.h); dw_prefs_boot_t is its fk_boot_t under dw's names. */
typedef fk_boot_t dw_prefs_boot_t;
#define DW_BOOT_KEEP FK_BOOT_KEEP
#define DW_BOOT_RESET_COMMIT FK_BOOT_RESET_COMMIT
#define DW_BOOT_RAM_ONLY FK_BOOT_RAM_ONLY
