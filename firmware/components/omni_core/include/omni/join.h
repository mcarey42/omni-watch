#pragma once
/* Joining a network from the watch (spec sections 7.3 and 7.5; Task 10): the pure decisions behind net's trial join and
 * Forget. No driver, no clocks: net.c wraps each driver call one to one in omni_join_ops_t, so the order of the calls
 * that decide whether the saved network (nvs.net80211) is written is host-tested here (C1, R202).
 *
 * The rule: the saved network is written only after the new one has proved itself (associated, and an IP received).
 * Until then the driver runs on RAM storage, and every way out restores FLASH storage before the driver is
 * deinitialised, so the next esp_wifi_init reads the saved network from NVS again. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/input.h"
#include "omni/netlist.h"

#define OMNI_JOIN_PASS_MAX 64   /* the longest password (64 hex digits); net's buffer is one more */

typedef enum { OMNI_JOIN_OK, OMNI_JOIN_REFUSE_READ_ONLY, OMNI_JOIN_REFUSE_BUSY, OMNI_JOIN_REFUSE_PHONE,
               OMNI_JOIN_REFUSE_PASS, OMNI_JOIN_REFUSE_SSID, OMNI_JOIN_REFUSE_AUTH } omni_join_refuse_t;
/* Checked as read-only, busy, a phone session, then the name (SSID: length 0 or > 32), the network's security (AUTH:
 * an UNSUPPORTED class), then the password (PASS: omni_kb_pass_ok fails for the class, or longer than
 * OMNI_JOIN_PASS_MAX, or NULL with a length). The name and security come before the password because the password's
 * rule depends on the class: checked first, every unsupported network would read as a bad password. */
omni_join_refuse_t omni_join_check(bool read_only, bool busy, bool phone, uint8_t authmode,
                                   uint8_t ssid_len, const char *pass, uint8_t pass_len);
/* threshold.authmode for the scanned mode, never weaker: WPA/WPA2 stays WPA/WPA2 PSK; WPA3 EXT and its mixed mode ask
 * WPA3 PSK; every other mode as scanned. */
uint8_t omni_join_threshold(uint8_t authmode);
/* The trial only (Minor 17): reasons 2 (AUTH_EXPIRE), 15 (4WAY_HANDSHAKE_TIMEOUT), 202 (AUTH_FAIL) and 204
 * (HANDSHAKE_TIMEOUT) read as a wrong password. Background syncs keep net.c's is_auth_reason (AUTH_FAIL only). */
bool omni_join_auth_reason(uint8_t reason);

typedef enum { OMNI_TR_JOINING, OMNI_TR_SYNCING, OMNI_TR_DONE } omni_trial_phase_t;
typedef enum { OMNI_TE_GOT_IP, OMNI_TE_FAIL_AUTH, OMNI_TE_FAIL_NO_AP, OMNI_TE_TIMEOUT, OMNI_TE_CANCEL,
               OMNI_TE_SCREEN_GONE, OMNI_TE_SYNC_OK, OMNI_TE_SYNC_FAIL } omni_trial_ev_t;
/* SCREEN_GONE is documentation: under B2 net never learns that the screen went, so nothing produces it; the tests
 * prove that it would change nothing (Minor 7). */
typedef enum { OMNI_TA_CONTINUE, OMNI_TA_COMMIT, OMNI_TA_END } omni_trial_act_t;
typedef enum { OMNI_TRES_NONE, OMNI_TRES_OK, OMNI_TRES_PASSWORD, OMNI_TRES_NO_AP, OMNI_TRES_SYNC,
               OMNI_TRES_CANCELLED, OMNI_TRES_NO_MEM } omni_trial_res_t;   /* NO_MEM appended (Minor 8) */
typedef struct { uint8_t phase, res; bool committed; } omni_trial_t;

void omni_trial_start(omni_trial_t *t);
/* JOINING: GOT_IP -> COMMIT (committed, SYNCING); FAIL_AUTH -> END PASSWORD; FAIL_NO_AP, TIMEOUT -> END NO_AP;
 * CANCEL -> END CANCELLED; anything else (SCREEN_GONE) -> CONTINUE. SYNCING: SYNC_OK -> END OK; SYNC_FAIL -> END SYNC
 * (committed stays); anything else (CANCEL, SCREEN_GONE) -> CONTINUE. DONE: everything CONTINUE, nothing changes. */
omni_trial_act_t omni_trial_step(omni_trial_t *t, omni_trial_ev_t e);

/* The trial's driver calls (C1, R202). net.c supplies one function per driver call; omni_join_drive is the only
 * caller, so the order below is the order on the device. Every int is 0 on success. */
typedef struct {
    int  (*wifi_up)(void *ctx);            /* esp_wifi_init */
    int  (*storage_ram)(void *ctx);        /* esp_wifi_set_storage(WIFI_STORAGE_RAM) */
    void (*storage_ok_log)(void *ctx);     /* logs "trial: storage RAM ok" */
    int  (*set_config)(void *ctx);         /* esp_wifi_set_config with the trial config net prepared */
    omni_trial_ev_t (*connect_wait)(void *ctx);  /* connect (JOIN_TRIES, JOIN_TIMEOUT_MS) and wait on GOT_IP, a
                                                    failure or the cancel bit: GOT_IP, FAIL_AUTH, FAIL_NO_AP, TIMEOUT
                                                    or CANCEL */
    int  (*storage_flash)(void *ctx);      /* esp_wifi_set_storage(WIFI_STORAGE_FLASH) */
    bool (*sntp)(void *ctx);
    void (*wifi_off)(void *ctx);           /* deinit: the next init reads NVS */
    bool (*persisted)(void *ctx);          /* I11: wifi_up, esp_wifi_get_config, the SSID and password compared with
                                              net's in-memory copy; true when equal. Logs only the verdict */
} omni_join_ops_t;
/* wifi_up (an error: NO_MEM, then only wifi_off); storage_ram (any error: NO_MEM, then only wifi_off: set_config is
 * never called); storage_ok_log; set_config (an error: NO_MEM); connect_wait, read through omni_trial_step. On COMMIT:
 * storage_flash, then set_config (the one write; skipped when storage_flash failed, since it would not persist), then
 * sntp. Every END path that reached RAM storage calls storage_flash before its final wifi_off. After a commit:
 * wifi_off, then persisted() (I11), then wifi_off. *persisted is false when nothing was committed. Returns t->res. */
omni_trial_res_t omni_join_drive(const omni_join_ops_t *ops, void *ctx, omni_trial_t *t, bool *persisted);

/* I4: net evaluates this with its own state at the moment it handles CMD_FORGET (its read-only flag, a phone session
 * up, a trial or a scan running), never the caller-set busy flag; the event loop's guard passes net_busy(). */
bool omni_join_forget_allowed(bool read_only, bool prov, bool trial_or_scan);
/* OK and SYNC are sync results (the clock's booking: the network joined); every other result is not. */
bool omni_join_books_sync(omni_trial_res_t r);

/* C2, R202: the Forget hold (1.5 s on the red key), the only path from a Forget key to UI_WIFI_FORGET or
 * UI_FORGET_RESCAN. G_DOWN on the key arms, G_DOWN off it disarms. True exactly once: the first G_HOLD on the key with
 * hold_ms >= OMNI_FORGET_HOLD_MS while armed and not fired. A G_HOLD off the key, G_TAP, G_LIFT_STILL, G_DRAG_*,
 * G_LONG_PRESS and every swipe disarm and return false. Once fired it stays fired until reset. BOOT, a tap outside,
 * Cancel and the timeout close the sheet and reset.
 * Kept apart from hold_key.h on purpose (do not merge them): this machine is C2's review gate for the only path to a
 * real Forget, with its own exhaustive tests (test_forget_hold_*); it also disarms on a G_HOLD off the key and keeps no
 * clock of its own, so a change to Restore default's key can never alter when Forget fires. */
#define OMNI_FORGET_HOLD_MS 1500
typedef struct { bool armed, fired; } omni_forget_hold_t;
void omni_forget_hold_reset(omni_forget_hold_t *h);
bool omni_forget_hold_step(omni_forget_hold_t *h, omni_gesture_kind_t k, uint16_t hold_ms, bool on_key);

/* F1: the net task's exclusion of a phone session. A queued scan or trial runs only outside a session (admit); at its
 * end busy stays set while a session runs or a CMD_PROV is queued behind it (keep_busy), so no later scan or trial can
 * be claimed in front of that session. */
bool omni_join_admit(bool prov);
bool omni_join_keep_busy(bool prov, bool prov_queued);
/* Task 12 (the claimed-but-not-started gap): every busy clear on the net task, and a dropped command's. Busy stays while a
 * session runs or a CMD_PROV is queued (keep_busy), a scan or trial runs, or one is claimed (net_scan / net_try_join set
 * busy and sent their command, and the net task has not handled it yet). Without the claim, a CMD_PROV queued ahead of
 * the claimed command whose start fails (or a session ending first) cleared busy under it, so a second net_try_join
 * could overwrite net's password copy before the first trial ran. */
bool omni_join_busy_left(bool prov, bool prov_queued, bool running, bool claimed);
