#pragma once
/* Battery session record (Task 14 measurement). Pure state machine; the app feeds it wake causes and VBUS
 * edges and persists the struct as one blob (store_batrec_*). One session = unplug .. replug. The record
 * from the last completed session stays until the next unplug.
 *
 * Wake totals are monotonic counters; a session's wakes are the difference between its snapshots. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { WAKE_LIFT, WAKE_TOUCH, WAKE_GLANCE } omni_wake_kind_t;

typedef struct {
    int64_t unix_s;          /* wall clock, display only; 0 = clock unset */
    uint32_t up_s;           /* monotonic seconds since this boot (esp_timer: counts through light sleep) */
    uint16_t boot;           /* boot number the up_s belongs to */
    int16_t mv;
    int8_t pct;              /* -1 unknown */
    uint8_t valid;
    uint16_t rsvd;           /* explicit padding: the layout is identical on host (8-byte int64) and Xtensa (4) */
    uint32_t lift, touch, glance;
} omni_bsnap_t;

typedef struct {
    omni_bsnap_t start, last, end;   /* last = latest checkpoint while on battery */
    uint32_t lift, touch, glance;    /* running totals since the record was created */
    uint8_t on_battery;
    uint8_t end_is_checkpoint;       /* closed from the last checkpoint (reboot / missed edge), not a live read */
    uint16_t boot;                   /* boot counter: incremented by omni_batrec_boot */
} omni_batrec_t;

/* Persisted blob: uint16 version, then the struct. Raw memcpy, so the size is pinned (static assert in
 * batrec.c) and any layout change must bump OMNI_BATREC_VERSION. */
#define OMNI_BATREC_VERSION 2
#define OMNI_BATREC_BLOB_SIZE (sizeof(uint16_t) + sizeof(omni_batrec_t))

typedef struct {
    bool have;               /* a session exists */
    bool in_progress;        /* on battery now: figures run to the latest checkpoint */
    bool partial;            /* end came from a checkpoint */
    int start_pct, end_pct, start_mv, end_mv;
    int64_t duration_s;      /* uptime difference within one boot; across a reboot the wall-clock difference, else 0 */
    uint32_t lift, touch, glance, wakes;   /* wakes = lift + touch */
    float hours;
    float hours_per_100_wakes;   /* -1 if there were no wakes or no duration */
    float pct_per_hour;          /* -1 if no duration */
} omni_bsession_report_t;

void omni_batrec_init(omni_batrec_t *r);
void omni_batrec_wake(omni_batrec_t *r, omni_wake_kind_t k);
/* Fills the snapshot's wake counters from the totals; the caller supplies time, pct and mv. */
omni_bsnap_t omni_batrec_snap(const omni_batrec_t *r, int64_t unix_s, uint32_t up_s, int pct, int mv);
/* A VBUS edge. Returns true if the record changed and should be saved. */
bool omni_batrec_vbus(omni_batrec_t *r, bool present, omni_bsnap_t now);
/* Boot: reconcile with the VBUS state seen at start. Returns true if it changed. */
bool omni_batrec_boot(omni_batrec_t *r, bool present, omni_bsnap_t now);
/* True if on battery and interval_s of uptime have passed since the last checkpoint (or it is from an earlier
 * boot). Pure and cheap: the caller reads the battery only when this is true. Independent of the wall clock. */
bool omni_batrec_checkpoint_due(const omni_batrec_t *r, uint32_t up_s, uint32_t interval_s);
/* Coarse checkpoint while on battery; true if one was taken (checkpoint_due). */
bool omni_batrec_checkpoint(omni_batrec_t *r, omni_bsnap_t now, uint32_t interval_s);
/* Blob codec for the store. decode returns false (and leaves r empty) on a size or version mismatch or when
 * the fields are out of range (flag bytes > 1, pct outside -1..100, on_battery without a start). */
void omni_batrec_encode(const omni_batrec_t *r, uint8_t *buf);   /* OMNI_BATREC_BLOB_SIZE bytes */
bool omni_batrec_decode(omni_batrec_t *r, const uint8_t *buf, size_t n);
void omni_batrec_report(const omni_batrec_t *r, omni_bsession_report_t *out);
