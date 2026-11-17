#pragma once
/* SCR_NETLIST: the Wi-Fi network list (spec 7.1; faces-setup.js `netlist`). Same threading rules as shell.h: call with
 * the LVGL lock. The screen posts EV_UI (UI_LIST_TO_PROV, UI_SYNC_NOW); it starts scans itself (net_scan) and never
 * joins (the keyboard does). */
#include "shell.h"

extern const omni_screen_vt_t SCR_NETLIST_VT;
/* LVGL lock: an EV_NET carrying NET_EV_SCAN (the event loop forwards each one). A finished scan's records are kept for
 * 60 s whether or not the list is shown. */
void scr_netlist_net(int ev);
/* The next create scans at once instead of showing the kept results ("Scan again" from the keyboard's result). */
void scr_netlist_rescan(void);
