#include "omni/netlist.h"
#include <string.h>

omni_auth_class_t omni_netlist_auth(uint8_t m)
{
    switch (m) {
    case OMNI_WA_OPEN: return OMNI_AUTH_OPEN;
    case OMNI_WA_WPA_PSK: case OMNI_WA_WPA2_PSK: case OMNI_WA_WPA_WPA2_PSK: return OMNI_AUTH_PSK;
    case OMNI_WA_WPA3_PSK: case OMNI_WA_WPA2_WPA3_PSK: case OMNI_WA_WPA3_EXT_PSK: case OMNI_WA_WPA3_EXT_PSK_MIXED:
        return OMNI_AUTH_SAE;
    default: return OMNI_AUTH_UNSUPPORTED;
    }
}

uint8_t omni_netlist_bars(int8_t r)
{
    return r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : r >= -85 ? 1 : 0;
}

static bool same(const uint8_t *a, uint8_t al, const uint8_t *b, uint8_t bl)
{
    return al == bl && memcmp(a, b, al) == 0;
}

int omni_netlist_build(const omni_scan_rec_t *in, int n, const uint8_t *saved, uint8_t saved_len,
                       omni_net_row_t out[OMNI_NETLIST_MAX])
{
    int k = 0;
    for (int i = 0; i < n; i++) {
        const omni_scan_rec_t *r = &in[i];
        if (r->ssid_len == 0 || r->ssid_len > 32) continue;
        int j = 0;
        while (j < k && !same(out[j].rec.ssid, out[j].rec.ssid_len, r->ssid, r->ssid_len)) j++;
        if (j < k) {                                   /* the same name: the strongest stays, in its first place */
            if (r->rssi > out[j].rec.rssi) out[j].rec = *r;
            continue;
        }
        if (k == OMNI_NETLIST_MAX) {                   /* full: drop the weakest (the latest of equals) if r beats it */
            int w = 0;
            for (int m = 1; m < k; m++) if (out[m].rec.rssi <= out[w].rec.rssi) w = m;
            if (r->rssi <= out[w].rec.rssi) continue;
            memmove(&out[w], &out[w + 1], (size_t)(k - 1 - w) * sizeof out[0]);
            k--;
        }
        out[k++].rec = *r;
    }
    for (int i = 1; i < k; i++) {                      /* insertion sort: stable, RSSI descending */
        omni_net_row_t t = out[i];
        int j = i;
        while (j > 0 && out[j - 1].rec.rssi < t.rec.rssi) { out[j] = out[j - 1]; j--; }
        out[j] = t;
    }
    for (int i = 0; i < k; i++) {
        out[i].auth = (uint8_t)omni_netlist_auth(out[i].rec.authmode);
        out[i].bars = omni_netlist_bars(out[i].rec.rssi);
        out[i].saved = saved && saved_len && same(out[i].rec.ssid, out[i].rec.ssid_len, saved, saved_len);
    }
    return k;
}
