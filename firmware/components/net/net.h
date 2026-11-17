#pragma once
/* Network: BLE Wi-Fi provisioning (network_provisioning, security 1 with a PoP, QR payload from omni_prov_qr_json)
 * and SNTP time sync from pool.ntp.org. Wi-Fi is on only during a provisioning session and a sync attempt, and off
 * again when each ends. A provisioning session ends on success, on net_stop_provisioning (the wearer left the QR
 * screen, or the watch is going to sleep with it: omni_prov_stop_on) and at the hard cap (OMNI_PROV_MAX_MS from its
 * start, checked on the net task); omni/prov_session.h has the timeouts and why.
 *
 * All calls are safe from any task: the work runs on the net task, which posts EV_NET on every state change.
 * EV_NET's .net is the new omni_net_state_t, ORed with NET_EV_PROV when it belongs to a provisioning session
 * (the QR flow) rather than a background sync, and with NET_EV_LOUD when a debug force asks for it to be
 * shown on screen. NET_IDLE | NET_EV_PROV: a provisioning session was stopped (not after a success). The device
 * name is built from the BASE MAC (esp_efuse_mac_get_default, which on the S3 is also the Wi-Fi STA MAC), e.g.
 * 02:00:00:12:34:56 -> PROV_OMNI_3456. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/netlist.h"
#include "omni/prov_session.h"

typedef enum {
    NET_IDLE, NET_PROV_WAITING, NET_PROV_PHONE, NET_JOINING, NET_SYNCING, NET_OK,
    NET_ERR_PASSWORD, NET_ERR_NO_AP, NET_ERR_SYNC,
    NET_ERR_NO_MEM,     /* the Wi-Fi driver could not start (esp_wifi_init: internal RAM) */
    NET_SCANNING,       /* appended (Task 10): a network scan runs (NET_EV_SCAN) */
} omni_net_state_t;

#define NET_EV_STATE(v) ((omni_net_state_t)((v) & 0xff))
#define NET_EV_PROV 0x100   /* the state is part of the provisioning (QR) flow */
#define NET_EV_LOUD 0x200   /* debug force: show it on screen like a user-started sync */
#define NET_EV_FORGET 0x400 /* background sync: the saved password was refused (AUTH_FAIL) twice in a row */
#define NET_EV_SCAN  0x800  /* a scan's state: NET_SCANNING, then NET_IDLE (results ready) or NET_ERR_NO_MEM */
#define NET_EV_TRIAL 0x1000 /* a trial join's state: JOINING, SYNCING, OK, ERR_PASSWORD, ERR_NO_AP, ERR_SYNC,
                               ERR_NO_MEM; NET_IDLE: cancelled */

/* Driver state, the device name and a first PoP; starts the net task. read_only: the store's read-only boot
 * (store_read_only): net writes nothing of its own to NVS (the boot-unprov flag is left as it is). */
void net_init(bool read_only);
bool net_is_provisioned(void);
bool net_start_provisioning(void);      /* BLE, network_provisioning, security1 with a new PoP (one per session); false if not queued */
void net_stop_provisioning(omni_prov_why_t why); /* ends the session if omni_prov_stop_on says so (net task) */
omni_prov_phase_t net_prov_phase(void); /* the session's phase, from the net state; PROV_PH_NONE without one */
void net_sync_now(void);                /* Wi-Fi on -> join -> SNTP (pool.ntp.org) -> Wi-Fi off; posts EV_NET */
/* Drops the saved network (esp_wifi_restore, on the net task). Refused, nothing done, under the bench lock and, by
 * net's own state when it runs (I4), on a read-only boot, while provisioning, or while a trial or a scan runs. */
void net_forget(void);
omni_net_state_t net_state(void);

/* A provisioning session or a sync attempt is in progress (set at once by the calls above). */
bool net_busy(void);
bool net_provisioning(void);            /* the provisioning service is up (BLE) */
void net_ssid(char out[33]);            /* network being joined / last joined, "" if none */
const char *net_prov_name(void);        /* "PROV_OMNI_3456" */
void net_qr_json(char *out, size_t n);  /* omni_prov_qr_json(name, pop); the PoP changes at each session start */
const char *net_state_name(omni_net_state_t st);
unsigned net_stack_hwm(void);           /* net task stack high-water mark, bytes */
/* The steps the successful syncs since the last call applied to the clock (after - before, us, summed: a sync
 * whose EV_NET was dropped is not lost), once: false if none. Read by the event loop on NET_OK (the clock
 * calibration). */
bool net_take_step(int64_t *step_us);

/* Debug: set the state and post EV_NET as if an attempt ended there (NET_EV_LOUD plus flags: NET_EV_PROV,
 * NET_EV_FORGET), to check the screens without a phone. Does not touch Wi-Fi. */
void net_debug_force(omni_net_state_t st, int flags);

/* Debug: the next boot (only) behaves as unprovisioned - QR and provisioning - without erasing the saved network
 * (one-shot NVS flag "watch"/"boot_unprov", cleared by net_init). EVENT LOOP ONLY (R27). read_only: refused,
 * ESP_ERR_INVALID_STATE, nothing written. Returns the NVS result. */
int net_debug_boot_unprovisioned(bool read_only);

/* ---- joining from the watch (spec section 7.3; Task 10). The saved network (nvs.net80211) is written only after the
 * new one has joined and received an IP: the trial runs on RAM storage, in the order omni_join_drive pins (C1). */
bool net_scan(void);                                   /* false (nothing done) while busy or provisioning */
int  net_scan_results(omni_scan_rec_t *out, int max);  /* the last scan's records (a copy), at most 20 */
/* omni_join_refuse_t (omni/join.h). OMNI_JOIN_OK: the password is copied into net's own buffer (never a queue item, an
 * event or a log line) and wiped when the attempt ends; the caller wipes its own copy. */
int  net_try_join(const uint8_t *ssid, uint8_t ssid_len, const char *pass, uint8_t pass_len, uint8_t authmode);
void net_try_cancel(void);                             /* sets the attempt's cancel bit; acts only before the commit */
bool net_trial_last(omni_net_state_t *st, char ssid[33]);   /* this boot's last trial result, for the Wi-Fi row */
/* C2: a RAM-only bench lock. CMD_FORGET checks it first and then does nothing ("FORGET REFUSED (bench lock)"). A
 * reboot clears it. */
void net_forget_lock(bool on);
bool net_forget_locked(void);
