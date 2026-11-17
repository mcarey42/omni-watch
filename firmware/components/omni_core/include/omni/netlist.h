#pragma once
/* The Wi-Fi scan rows' pure core (spec section 7.1; Task 6): auth classing, signal bars, and the dedup, sort and
 * saved marking of one scan. No driver, no clocks. SSIDs are raw bytes (non-ASCII allowed, not NUL-terminated):
 * every comparison is by length and bytes. */
#include <stdbool.h>
#include <stdint.h>

/* The raw auth modes mirror ESP-IDF v5.5.6's wifi_auth_mode_t in value and order (net.c asserts each, Task 10).
 * OMNI_WA_UNKNOWN is WIFI_AUTH_UNKNOWN (17), the scan's "unparseable security"; WIFI_AUTH_MAX (18) and above class
 * as unsupported like it. */
enum { OMNI_WA_OPEN, OMNI_WA_WEP, OMNI_WA_WPA_PSK, OMNI_WA_WPA2_PSK, OMNI_WA_WPA_WPA2_PSK, OMNI_WA_ENTERPRISE,
       OMNI_WA_WPA3_PSK, OMNI_WA_WPA2_WPA3_PSK, OMNI_WA_WAPI_PSK, OMNI_WA_OWE, OMNI_WA_WPA3_ENT_192,
       OMNI_WA_WPA3_EXT_PSK, OMNI_WA_WPA3_EXT_PSK_MIXED, OMNI_WA_DPP, OMNI_WA_WPA3_ENTERPRISE,
       OMNI_WA_WPA2_WPA3_ENTERPRISE, OMNI_WA_WPA_ENTERPRISE, OMNI_WA_UNKNOWN };
typedef enum { OMNI_AUTH_OPEN, OMNI_AUTH_PSK, OMNI_AUTH_SAE, OMNI_AUTH_UNSUPPORTED } omni_auth_class_t;
typedef struct { uint8_t ssid[32]; uint8_t ssid_len; int8_t rssi; uint8_t authmode; uint8_t channel; } omni_scan_rec_t;
typedef struct { omni_scan_rec_t rec; uint8_t auth; uint8_t bars; bool saved; } omni_net_row_t;
#define OMNI_NETLIST_MAX 16

/* OPEN; PSK: WPA, WPA2, WPA/WPA2; SAE: WPA3, WPA2/WPA3, WPA3 EXT and its mixed mode; everything else (WEP, every
 * enterprise, WAPI, OWE, DPP, unknown) UNSUPPORTED */
omni_auth_class_t omni_netlist_auth(uint8_t authmode);
uint8_t omni_netlist_bars(int8_t rssi);   /* 4 >= -55, 3 >= -65, 2 >= -75, 1 >= -85, else 0 */
/* Empty names dropped; one row per exact name bytes, the strongest kept; sorted by RSSI descending, stable; saved =
 * exact byte equality with saved (saved_len 0 or NULL: none); at most 16 (the 16 strongest). Returns the row count. */
int omni_netlist_build(const omni_scan_rec_t *in, int n, const uint8_t *saved, uint8_t saved_len,
                       omni_net_row_t out[OMNI_NETLIST_MAX]);
