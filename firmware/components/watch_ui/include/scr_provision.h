#pragma once
/* SCR_PROVISION: the Wi-Fi QR screen (faces-setup.js `wifi-qr`), its steps (phone linked -> joining -> time
 * set) and the error states in the same house style (wrong password, no network, sync failed, Wi-Fi did not start).
 * Same threading rules as shell.h: call with the LVGL lock; the screen posts EV_UI (UI_POP, UI_RESCAN). */
#include <stdbool.h>
#include <stdint.h>
#include "net.h"
#include "shell.h"

typedef struct {
    omni_net_state_t state;
    bool prov;           /* part of the provisioning (QR) flow, else a background / boot sync */
    bool offer_forget;   /* background sync: the password was refused twice in a row (NET_EV_FORGET) */
    char ssid[33];
    char name[16];       /* PROV_OMNI_xxxx */
    char qr[128];        /* QR payload (omni_prov_qr_json); "" = a new session's code is on its way (blank plate) */
    int64_t last_ok;     /* unix s of the last good sync, 0 = never */
    int64_t next_try;    /* unix s of the next attempt */
    bool had_saved;      /* appended: a network was saved when this phone session started (the spec 7.6 line) */
} scr_prov_info_t;

/* What the screen shows; it keeps a copy. Can be called before the screen is shown. */
void scr_provision_set(const scr_prov_info_t *info);

extern const omni_screen_vt_t SCR_PROVISION_VT;
