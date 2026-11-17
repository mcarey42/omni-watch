#pragma once
/* VBUS (USB power) as the firmware believes it, from the AXP2101's raw reads. A failed read is no information: it
 * changes nothing. A change needs OMNI_VBUS_AGREE consecutive good reads that disagree with the current state, so one
 * glitched read cannot look like an unplug (light sleep on, a battery session opened) and then a plug-in (a D+
 * re-enumeration pulse). The first good read sets the state directly.
 *
 * But a read that keeps failing must not freeze "plugged": light sleep would never come back on battery and the
 * watch would drain silently. After OMNI_VBUS_FAIL_MAX consecutive failures the filter falls back: state (what light
 * sleep follows) becomes absent, the battery-safe side, and failsafe is set. It leaves the fallback only after
 * OMNI_VBUS_AGREE good reads in a row that agree (a failure or a disagreeing read restarts the count), so failures in
 * bursts between single good reads stay in the fallback instead of flipping each burst (N3). The caller logs the
 * fallback (failsafe going true).
 *
 * confirmed is the value for the records (the battery session, the clock's power source): the fallback is an
 * assumption, not an edge, so confirmed keeps the last value good reads gave through it, and a fallback on USB opens
 * no battery session (which would overwrite the one kept). Outside the fallback, confirmed == state. Pure; the caller
 * serialises access. */
#include <stdbool.h>
#include <stdint.h>

#define OMNI_VBUS_AGREE 2
#define OMNI_VBUS_FAIL_MAX 50   /* consecutive failed reads: 5 s at the power manager's 10 Hz asleep */

typedef enum { OMNI_VBUS_READ_FAIL = -1, OMNI_VBUS_ABSENT = 0, OMNI_VBUS_PRESENT = 1 } omni_vbus_read_t;

typedef struct {
    bool known;     /* a good read has been seen */
    bool state;     /* the filtered VBUS (false until known) */
    bool cand;      /* the opposite value being confirmed */
    uint8_t count;  /* consecutive good reads of cand */
    uint16_t fails; /* consecutive failed reads */
    bool failsafe;  /* OMNI_VBUS_FAIL_MAX failures in a row: battery assumed until a good read */
    bool confirmed; /* the last value good reads established (not changed by the fallback) */
    uint8_t good;   /* in the fallback: consecutive agreeing good reads of cand */
} omni_vbus_filter_t;

/* Feeds one read; returns the filtered state. */
bool omni_vbus_feed(omni_vbus_filter_t *f, omni_vbus_read_t r);
/* N2: true when light sleep was last configured for no VBUS (cfg_vbus false) because the filter was in its fallback
 * (cfg_assumed), and VBUS is present now: USB may have been in fact plugged in through that light sleep, and a host
 * that tried to enumerate the sleeping chip has given up. The caller then re-pulses D+. A light sleep on a real
 * battery reading is the asleep poll's ordinary plug-in edge instead. */
bool omni_vbus_reenumerate(bool cfg_vbus, bool cfg_assumed, bool now);
