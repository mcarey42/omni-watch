#pragma once
/* A theme's offer and boot rules (pure): the carousel offer over the theme's default list, and the boot decision from
 * how the theme's record loaded. The theme owns its record (its extension's layout, its defaults and its list); these
 * rules are the same for every theme. Pure: no esp_*, freertos or LVGL. */
#include <stdbool.h>
#include <stdint.h>

/* How a theme's record loaded: the known fields; a newer build's extra bytes kept verbatim; no extension (the
 * defaults); read-only (an unknown version, a bad length or an oversized tail: the defaults, never written). */
typedef enum { FK_LOAD_LOADED, FK_LOAD_TAIL_KEPT, FK_LOAD_ABSENT, FK_LOAD_READ_ONLY } fk_load_t;

/* The boot decision. writable: store_local_writable() (false on a no-write boot or a read-only record). RESET_COMMIT
 * only for ABSENT on a writable record (the carousel, face and reveal to the defaults and the extension committed once,
 * saved at once on the boot task); RAM_ONLY for ABSENT on a non-writable one, or READ_ONLY anywhere (the defaults in
 * RAM, nothing written); KEEP otherwise. */
typedef enum { FK_BOOT_KEEP, FK_BOOT_RESET_COMMIT, FK_BOOT_RAM_ONLY } fk_boot_t;
fk_boot_t fk_prefs_boot(bool writable, fk_load_t r);
/* May the boot offer? true only for KEEP or RESET_COMMIT on a writable record: a read-only boot, a read-only record or
 * an extension this build does not know is never offered, in RAM or NVS. */
bool fk_prefs_boot_offer(bool writable, fk_boot_t b);

/* The carousel offer: for each id of list[0 .. n) whose bit in *offered is clear and which the carousel lacks, append
 * it while *len < 16. Then set the bit of every list id, appended or not. true if carousel, *len or *offered changed.
 * Only appends, so a face index keeps pointing at the same face id. Ids past 15 (no bit to keep them by) are skipped. */
bool fk_offer_list(uint16_t *offered, uint8_t carousel[16], uint8_t *len, const uint8_t *list, uint8_t n);
/* Clear the bit of every list id that the carousel lacks, so the next boot's offer appends it again. true if *offered
 * changed. Ids past 15 are skipped. */
bool fk_offer_reset_list(uint16_t *offered, const uint8_t *carousel, uint8_t len, const uint8_t *list, uint8_t n);
