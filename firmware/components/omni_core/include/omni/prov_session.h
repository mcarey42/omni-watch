#pragma once
/* Provisioning session lifetime (I1). A session is BLE advertising plus the Wi-Fi radio scanning and joining for
 * the phone; it runs only while the QR screen is in use, so it never holds the radios on a sleeping watch:
 *  - it stops when the wearer leaves the QR screen or the watch goes to sleep with it on top (omni_prov_stop_on),
 *    except while credentials are being tried (the join's own timeout ends that phase within seconds);
 *  - an unprovisioned watch starts a new session (a new PoP) when it wakes with the QR screen on top;
 *  - a hard cap ends any session OMNI_PROV_MAX_MS after it started (omni_prov_expired).
 * Timeouts. The QR screen keeps the watch awake PROV_SLEEP_S (170 s) after the last touch, plus the 10 s glance:
 * 3 min to scan (the owner's Task 12 value), and the same again with the phone linked while the wearer picks the
 * network and types the password (omni_prov_long_timeout). Every other provisioning screen sleeps on the normal
 * timeout. So an idle session ends at the screen's sleep, about 3 min, and the 10 min cap covers what the screen
 * cannot: a session kept alive by touches or phone events. 10 min is past any real setup: 3 min scanning, 3 min in
 * the app, the join (3 tries) and the time sync (under 1 min), and one retry after a wrong password. */
#include <stdbool.h>
#include <stdint.h>

#define OMNI_PROV_MAX_MS 600000u

typedef enum {
    PROV_PH_NONE,     /* no session */
    PROV_PH_WAITING,  /* QR shown, no phone */
    PROV_PH_PHONE,    /* a phone is linked over BLE */
    PROV_PH_JOINING,  /* credentials received: joining, then setting the time */
    PROV_PH_DONE,     /* joined and synced (the manager stops the service by itself) */
    PROV_PH_FAILED,   /* the credentials were refused or the network not found; the phone may retry */
} omni_prov_phase_t;

typedef enum { PROV_WHY_LEFT, PROV_WHY_SLEEP } omni_prov_why_t;

/* Stop the session now that the wearer left the QR screen (LEFT) or the watch is going to sleep with it (SLEEP)? */
bool omni_prov_stop_on(omni_prov_phase_t ph, omni_prov_why_t why);
/* The hard cap: OMNI_PROV_MAX_MS since started_ms (wrap-safe). */
bool omni_prov_expired(uint32_t started_ms, uint32_t now_ms);
/* The QR screen's long sleep timeout applies in this phase (else the normal one). */
bool omni_prov_long_timeout(omni_prov_phase_t ph);

/* How a session ended (documentation only: the plan below does not depend on it, Minor 7). */
typedef enum { PROV_END_FAILED, PROV_END_CAP, PROV_END_LEFT, PROV_END_SLEEP, PROV_END_TYPE_INSTEAD,
               PROV_END_SUCCESS } omni_prov_end_t;
typedef struct { bool restore, provisioned, name_from_snapshot; } omni_prov_end_plan_t;
/* I1 (spec section 7.4): what net does with the saved-network snapshot it took at the session's start (before
 * network_prov_mgr_init), when the session ends, before Wi-Fi goes off.
 *  - restore = snap_has_ssid && cred_recv && !cred_ok. CRED_RECV is where the manager set FLASH storage and wrote the
 *    phone's credentials (manager_wrote); a CRED_FAIL's empty write follows it. An empty snapshot is never restored,
 *    and a session where the phone sent nothing has nothing to undo.
 *  - after a restore: provisioned, and the name is the snapshot's;
 *  - cred_ok: provisioned, the new name;
 *  - otherwise: provisioned as before the session (snap_has_ssid), the name the snapshot's when one was saved. */
omni_prov_end_plan_t omni_prov_end_plan(bool snap_has_ssid, bool cred_recv, bool cred_ok, omni_prov_end_t why);
/* I5: "Type it instead" is shown and active only while a session runs and omni_prov_stop_on(ph, PROV_WHY_LEFT) would
 * end it: never while joining, and not once it is done. */
bool omni_prov_type_instead_ok(omni_prov_phase_t ph);
/* R221: the manager wrote the phone's credentials this session: CRED_RECV seen when its event was dispatched, or the
 * driver's config, read after network_prov_mgr_deinit, is not the empty one the manager's session start left (only its
 * CRED_RECV write makes it non-empty; a CRED_FAIL that empties it again always follows a dispatched CRED_RECV). */
bool omni_prov_manager_wrote(bool cred_recv_seen, bool cfg_nonempty);
/* F4: a session may start only once the saved network was read from a fresh driver init (esp_wifi_init reads NVS);
 * otherwise nothing could undo the manager's writes, so the session is refused and nothing is written. */
bool omni_prov_start_ok(bool fresh, bool snap_read);
/* F5: provisioned after the end: the plan's, unless the restore it asked for failed (after one retry); then what the
 * driver reports (a saved SSID or not). */
bool omni_prov_end_provisioned(omni_prov_end_plan_t p, bool restore_ok, bool driver_has_ssid);
