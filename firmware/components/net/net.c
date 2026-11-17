#include "net.h"
#include "ntp_client.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "bootloader_random.h"
#include "esp_bt.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include "nvs.h"
#include "omni/clockcal.h"
#include "omni/join.h"
#include "omni/prov.h"
#include "omni/wipe.h"
#include "omni_events.h"
#include "power.h"
#include "board.h"

static const char *TAG = "net";

#define NET_STACK 6144
/* the pool, then its numbered names: another member when one does not answer (ntp_client.c) */
static const char *const SNTP_SERVERS[] = {"pool.ntp.org", "0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"};
#define JOIN_TIMEOUT_MS 20000   /* per sync attempt, all connect tries together */
#define JOIN_TRIES 3            /* connect calls per sync attempt before giving up */
#define SNTP_TIMEOUT_MS 15000   /* a whole sync: the name lookup and every request (ntp_client.c) */
#define PROV_CONN_ATTEMPTS 3    /* the manager's own connect tries before it reports CRED_FAIL */
#define TIME_VALID_S 1767225600 /* 2026-01-01: anything earlier is not a synced clock */

#define BIT_GOT_IP BIT0
#define BIT_FAIL BIT1
#define BIT_CANCEL BIT2         /* net_try_cancel: set directly (a queued command could not wake the attempt, D7) */
#define SCAN_MAX 20             /* records kept from a scan (the strongest) */
#define SCAN_MS 120             /* active scan dwell per channel */

/* netlist.h mirrors the driver's auth modes value for value (Task 6) */
#define WA_EQ(o, w) ((int)OMNI_WA_##o == (int)WIFI_AUTH_##w)
_Static_assert(WA_EQ(OPEN, OPEN) && WA_EQ(WEP, WEP) && WA_EQ(WPA_PSK, WPA_PSK) && WA_EQ(WPA2_PSK, WPA2_PSK) &&
               WA_EQ(WPA_WPA2_PSK, WPA_WPA2_PSK) && WA_EQ(ENTERPRISE, ENTERPRISE) && WA_EQ(WPA3_PSK, WPA3_PSK) &&
               WA_EQ(WPA2_WPA3_PSK, WPA2_WPA3_PSK) && WA_EQ(WAPI_PSK, WAPI_PSK) && WA_EQ(OWE, OWE) &&
               WA_EQ(WPA3_ENT_192, WPA3_ENT_192) && WA_EQ(WPA3_EXT_PSK, WPA3_EXT_PSK) &&
               WA_EQ(WPA3_EXT_PSK_MIXED, WPA3_EXT_PSK_MIXED_MODE) && WA_EQ(DPP, DPP) &&
               WA_EQ(WPA3_ENTERPRISE, WPA3_ENTERPRISE) && WA_EQ(WPA2_WPA3_ENTERPRISE, WPA2_WPA3_ENTERPRISE) &&
               WA_EQ(WPA_ENTERPRISE, WPA_ENTERPRISE) && WA_EQ(UNKNOWN, UNKNOWN),
               "omni/netlist.h OMNI_WA_* must equal wifi_auth_mode_t");
_Static_assert(OMNI_NETLIST_MAX <= SCAN_MAX, "the list shows at most what a scan keeps");
_Static_assert(sizeof(((wifi_ap_record_t *)0)->ssid) >= 32 && sizeof(((wifi_sta_config_t *)0)->ssid) == 32 &&
               sizeof(((wifi_sta_config_t *)0)->password) == OMNI_JOIN_PASS_MAX, "SSID and password sizes");

typedef enum {
    CMD_PROV, CMD_PROV_STOP, CMD_SYNC, CMD_FORGET, CMD_FORCE, CMD_SCAN, CMD_TRY_JOIN,
    /* from the system event task, so only the net task changes state */
    CMD_BLE_UP, CMD_BLE_DOWN, CMD_CRED_RECV, CMD_CRED_FAIL, CMD_CRED_OK, CMD_PROV_END,
} cmd_kind_t;

typedef struct {
    uint8_t kind;
    int arg;
    char ssid[33];
} cmd_t;

static QueueHandle_t s_q;
static TaskHandle_t s_task;
static EventGroupHandle_t s_bits;
static volatile omni_net_state_t s_state = NET_IDLE;
static volatile bool s_busy, s_prov, s_sync_active;
static volatile bool s_provisioned; /* a network is saved (cached: the driver is not up between attempts) */
static bool s_wifi_up;              /* esp_wifi_init done (net task only) */
static volatile uint8_t s_disc_reason;
static uint32_t s_prov_t0;          /* when the session started (net task) */
static uint8_t s_auth_fails;        /* consecutive background syncs refused with AUTH_FAIL (net task) */
static char s_ssid[33];
static char s_prev_ssid[33];        /* the saved network's name while a session runs (net task) */
static char s_name[16];
#define POP_BYTES 16            /* 128-bit PoP: security 1 lets a BLE MITM brute-force the PoP offline */
static char s_pop[POP_BYTES * 2 + 1];
static char s_qr[128];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_read_only;            /* the store's read-only boot: no join, no Forget (net_init) */
static volatile bool s_forget_lock; /* C2: the bench lock (RAM only) */
static volatile bool s_trial, s_scan_run; /* a trial join / a scan is running on the net task */
/* A scan or trial claimed (busy set, its command sent) and not yet handled on the net task (Task 12): a CMD_PROV queued
 * ahead of it whose start fails, or a session that ends first, must not clear busy under it (busy_left) */
static volatile bool s_claimed;
/* The trial: net's own copy of the network and the password (net_try_join), wiped when the attempt ends */
static char s_trial_pass[OMNI_JOIN_PASS_MAX + 1];
static uint8_t s_trial_pass_len, s_trial_ssid[32], s_trial_ssid_len, s_trial_auth;
static omni_net_state_t s_trial_res; /* this boot's last trial result (under s_lock) */
static char s_trial_res_ssid[33];
static bool s_trial_has_res;
/* The phone session's snapshot of the saved network (I1): taken in prov_start before network_prov_mgr_init, written
 * back by prov_end when the manager wrote and the session did not succeed; never logged, wiped at the session's end */
static wifi_config_t s_snap;
static bool s_snap_has;
static volatile bool s_cred_recv, s_cred_ok; /* this session: the manager wrote the phone's credentials / they joined */
static volatile uint8_t s_prov_queued;       /* CMD_PROVs sent and not yet handled (under s_lock to change) */
/* Every busy clear: kept while a session runs or is queued, a scan or trial runs, or one is claimed (Task 12) */
static bool busy_left(void) { return omni_join_busy_left(s_prov, s_prov_queued != 0, s_scan_run || s_trial, s_claimed); }
static omni_scan_rec_t *s_scan_recs; /* SCAN_MAX records in PSRAM, made at the first scan (net task) */
static int s_scan_n;                 /* under s_lock */

static bool take_boot_unprov(bool read_only);

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

const char *net_state_name(omni_net_state_t st)
{
    static const char *const N[] = {"IDLE", "PROV_WAITING", "PROV_PHONE", "JOINING", "SYNCING", "OK",
                                    "ERR_PASSWORD", "ERR_NO_AP", "ERR_SYNC", "ERR_NO_MEM", "SCANNING"};
    return (unsigned)st < sizeof N / sizeof N[0] ? N[st] : "?";
}

static void set_ssid(const char *ssid)
{
    taskENTER_CRITICAL(&s_lock);
    strlcpy(s_ssid, ssid, sizeof s_ssid);
    taskEXIT_CRITICAL(&s_lock);
}

void net_ssid(char out[33])
{
    taskENTER_CRITICAL(&s_lock);
    memcpy(out, s_ssid, sizeof s_ssid);
    taskEXIT_CRITICAL(&s_lock);
}

static void set_state(omni_net_state_t st, int flags)
{
    s_state = st;
    board_heap_sample();
    board_heap_t hp = board_heap();
    ESP_LOGI(TAG, "%s%s, internal heap %u free (min %u), largest DMA block %u (min %u)", net_state_name(st),
             (flags & NET_EV_PROV) ? " (provisioning)" : "", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)hp.dma_largest,
             (unsigned)hp.dma_largest_min);
    omni_event_t e = {.kind = EV_NET, .t_ms = now_ms(), .net = (int)st | flags};
    omni_events_post(&e);
}

/* False if the queue was full and the command dropped. */
static bool send(cmd_kind_t kind, int arg, const char *ssid)
{
    if (s_q == NULL) return false;
    cmd_t c = {.kind = (uint8_t)kind, .arg = arg};
    if (ssid) strlcpy(c.ssid, ssid, sizeof c.ssid);
    if (xQueueSend(s_q, &c, 0) == pdTRUE) return true;
    ESP_LOGW(TAG, "command %d dropped", kind);
    return false;
}

/* ------------------------------------------------------------ system events (sys_evt task: keep it short) */
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        /* During provisioning the manager handles reconnects and reports CRED_FAIL itself. */
        if (!s_sync_active) return;
        s_disc_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        xEventGroupSetBits(s_bits, BIT_FAIL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_bits, BIT_GOT_IP);
    } else if (base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        if (id == PROTOCOMM_TRANSPORT_BLE_CONNECTED) send(CMD_BLE_UP, 0, NULL);
        else if (id == PROTOCOMM_TRANSPORT_BLE_DISCONNECTED) send(CMD_BLE_DOWN, 0, NULL);
    } else if (base == NETWORK_PROV_EVENT) {
        switch (id) {
        case NETWORK_PROV_WIFI_CRED_RECV:
            /* I1: the manager has just written the phone's credentials (manager.c 1570-1577); known at once, not
             * only when the queued command is handled, so a session stopped meanwhile still restores */
            if (s_prov) s_cred_recv = true;
            send(CMD_CRED_RECV, 0, (const char *)((wifi_sta_config_t *)data)->ssid);
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL:
            send(CMD_CRED_FAIL, *(network_prov_wifi_sta_fail_reason_t *)data, NULL);
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            /* F2: known at once too, so a stop (the cap) with CMD_CRED_OK still queued never undoes the join */
            if (s_prov) s_cred_ok = true;
            send(CMD_CRED_OK, 0, NULL);
            break;
        case NETWORK_PROV_END: send(CMD_PROV_END, 0, NULL); break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------ proof of possession */
/* A fresh 128-bit PoP for each provisioning session (m12: a QR photographed once is not valid for a later "Set
 * up"), from the hardware RNG. Not stored: a session's PoP lives only as long as the session.
 * Entropy: esp_fill_random is truly random on the S3 only while an RF radio is running or the SAR ADC entropy
 * source (bootloader_random_enable) is on, and that source must not be on while Wi-Fi or BT is running. Whether the
 * Wi-Fi driver is initialised says nothing about the radio, so this looks at the radios themselves: Wi-Fi is stopped
 * first if its driver is still up (callers run with no attempt in progress: boot, a session start), and the BT
 * controller's status is read. With both radios off the ADC source is switched on around the draw; if the BT
 * controller is still enabled (not expected: no session runs here) its RF is the entropy and the ADC stays off. */
static void new_pop(void)
{
    if (s_wifi_up) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(err));
    }
    bool bt_on = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
    if (bt_on) ESP_LOGW(TAG, "PoP drawn with the BT controller enabled (its RF is the entropy source)");
    uint8_t b[POP_BYTES];
    if (!bt_on) bootloader_random_enable();
    esp_fill_random(b, sizeof b);
    if (!bt_on) bootloader_random_disable();
    char pop[sizeof s_pop], qr[sizeof s_qr];
    for (int i = 0; i < POP_BYTES; i++) snprintf(pop + 2 * i, 3, "%02x", b[i]);
    omni_prov_qr_json(s_name, pop, qr, sizeof qr);
    taskENTER_CRITICAL(&s_lock);
    memcpy(s_pop, pop, sizeof s_pop);
    memcpy(s_qr, qr, sizeof s_qr);
    taskEXIT_CRITICAL(&s_lock);
}

/* ------------------------------------------------------------ helpers on the net task */
/* A background sync's join failure: only AUTH_FAIL reads as a refused password. Handshake timeouts also happen
 * with a weak signal, so they read as "no network" (m1). */
static bool is_auth_reason(uint8_t r) { return r == WIFI_REASON_AUTH_FAIL; }

/* The Wi-Fi driver is initialised only for an attempt or a provisioning session: its buffers live in
 * internal RAM, which the display's DMA and the console need (initialised at boot it starved them: boot loop). */
static esp_err_t wifi_up(void)
{
    if (s_wifi_up) return ESP_OK;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return err;
    }
    s_wifi_up = true;
    return ESP_OK;
}

static void wifi_off(void)
{
    if (!s_wifi_up) return;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(err));
    err = esp_wifi_deinit();
    if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_deinit: %s", esp_err_to_name(err));
    else s_wifi_up = false;
}

/* Ends a sync or provisioning attempt: Wi-Fi off first, so it is off by the time the result is seen. */
static void finish(omni_net_state_t st, int flags)
{
    if (!s_prov) {
        wifi_off();
        power_wifi_hold(false);
        s_busy = busy_left();   /* F1: a CMD_PROV queued behind keeps it (and a claim, Task 12) */
    }
    set_state(st, flags);
}

/* The wall clock minus esp_timer: constant unless the clock is set (libc on the high-resolution timer only,
 * sdkconfig.defaults), so its change over a sync is the step the sync applied. */
static int64_t clock_offset_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec - esp_timer_get_time();
}

static omni_clk_steps_t s_steps; /* under s_lock: summed until the event loop takes them (a dropped EV_NET) */

/* R109: best-of-N (ntp_client.c): the reply with the smallest round trip is applied with settimeofday, so the step is
 * measured as before (clock_offset_us) and the calibration sees it. Modem sleep is off for the queries (not under
 * provisioning: with BLE up, Wi-Fi/BT coexistence needs it). */
static bool sntp_sync(void)
{
    const int64_t before = clock_offset_us();
    ntp_client_result_t r;
    ntp_client_sample(SNTP_SERVERS, sizeof SNTP_SERVERS / sizeof SNTP_SERVERS[0], time(NULL) >= TIME_VALID_S, !s_prov,
                      SNTP_TIMEOUT_MS, &r);
    if (r.n > 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        const int64_t us = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec + r.best.offset_us;
        tv.tv_sec = (time_t)(us / 1000000);
        tv.tv_usec = (suseconds_t)(us % 1000000);
        settimeofday(&tv, NULL);
    }
    const int64_t step = clock_offset_us() - before;
    const time_t now = time(NULL);
    const bool ok = r.n > 0 && now >= TIME_VALID_S;
    ESP_LOGI(TAG, "sntp %s: %s, %d of %d replies (lost %d, unsent %d, skipped %d, rejected %d), delays %s ms, best %+lld ms "
             "at %lld ms (stratum %u), now %lld, step %+lld ms", ok ? "ok" : "failed", r.server[0] ? r.server : "-", r.n,
             r.tries, r.lost, r.unsent, r.strays, r.tries - r.n - r.lost,
             r.delays[0] ? r.delays : "-", (long long)(r.best.offset_us / 1000), (long long)(r.best.delay_us / 1000),
             r.best.stratum, (long long)now, (long long)(step / 1000));
    for (int e = 0; e < OMNI_NTP__COUNT; e++)
        if (r.rejected[e]) ESP_LOGW(TAG, "sntp: %u replies rejected (%s)", r.rejected[e], omni_ntp_err_name((omni_ntp_err_t)e));
    if (ok) {
        taskENTER_CRITICAL(&s_lock);
        omni_clk_steps_add(&s_steps, step);
        taskEXIT_CRITICAL(&s_lock);
    }
    return ok;
}

bool net_take_step(int64_t *step_us)
{
    taskENTER_CRITICAL(&s_lock);
    bool have = omni_clk_steps_take(&s_steps, step_us);
    taskEXIT_CRITICAL(&s_lock);
    return have;
}

/* Connect (JOIN_TRIES connects within JOIN_TIMEOUT_MS) and wait for an IP, a failure or, if asked, the cancel bit.
 * Returns BIT_GOT_IP, the cancel bit, or 0: failed or timed out (s_disc_reason holds the last reason, *errp the driver
 * error). Shared by the background sync and the trial join. */
static EventBits_t join_wait(EventBits_t cancel, esp_err_t *errp)
{
    xEventGroupClearBits(s_bits, BIT_GOT_IP | BIT_FAIL);
    s_disc_reason = 0;
    s_sync_active = true;
    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) err = esp_wifi_connect();
    EventBits_t got = 0;
    int tries = 1;
    int64_t until = esp_timer_get_time() / 1000 + JOIN_TIMEOUT_MS;
    while (err == ESP_OK) {
        int64_t left = until - esp_timer_get_time() / 1000;
        if (left <= 0) break;
        EventBits_t b = xEventGroupWaitBits(s_bits, BIT_GOT_IP | BIT_FAIL | cancel, pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(left));
        if (b & (BIT_GOT_IP | cancel)) {   /* an IP wins over a Cancel that came with it */
            got = (b & BIT_GOT_IP) ? BIT_GOT_IP : cancel;
            break;
        }
        if (!(b & BIT_FAIL) || tries >= JOIN_TRIES) break;
        ESP_LOGI(TAG, "join try %d failed, reason %u", tries, s_disc_reason);
        tries++;
        err = esp_wifi_connect();
    }
    s_sync_active = false;
    *errp = err;
    return got;
}

static void do_sync(void)
{
    /* As prov_start (F4): a driver still up after a failed deinit may hold a trial's RAM config, not the saved network.
     * One more try for a fresh init (it reads NVS); still up: refused, never reused. */
    if (s_wifi_up) wifi_off();
    if (s_wifi_up) ESP_LOGE(TAG, "sync: driver not fresh, refused");
    if (s_wifi_up || wifi_up() != ESP_OK) { /* in practice ESP_ERR_NO_MEM: not the network's fault */
        finish(NET_ERR_NO_MEM, 0);
        return;
    }
    wifi_config_t cfg = {0};
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    set_ssid((const char *)cfg.sta.ssid);
    const bool saved = cfg.sta.ssid[0] != '\0';
    omni_wipe(&cfg, sizeof cfg);   /* the saved password's stack copy: the driver joins from its own config */
    if (!saved) { /* forgotten meanwhile */
        finish(NET_IDLE, 0);
        return;
    }
    power_wifi_hold(true);
    set_state(NET_JOINING, 0);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_err_t err;
    if (join_wait(0, &err) != BIT_GOT_IP) {
        ESP_LOGW(TAG, "join failed: %s, reason %u", esp_err_to_name(err), s_disc_reason);
        bool auth = is_auth_reason(s_disc_reason);
        s_auth_fails = auth ? (uint8_t)(s_auth_fails < UINT8_MAX ? s_auth_fails + 1 : s_auth_fails) : 0;
        finish(auth ? NET_ERR_PASSWORD : NET_ERR_NO_AP, auth && s_auth_fails >= 2 ? NET_EV_FORGET : 0);
        return;
    }
    s_auth_fails = 0;
    set_state(NET_SYNCING, 0);
    finish(sntp_sync() ? NET_OK : NET_ERR_SYNC, 0);
}

/* ------------------------------------------------------------ the scan (net task) */
/* Keeps the SCAN_MAX strongest of the driver's list: records are popped one at a time (no array of driver records). */
static int scan_keep(int n, const wifi_ap_record_t *ap)
{
    omni_scan_rec_t r = {.ssid_len = (uint8_t)strnlen((const char *)ap->ssid, 32), .rssi = ap->rssi,
                         .authmode = (uint8_t)ap->authmode, .channel = ap->primary};
    memcpy(r.ssid, ap->ssid, r.ssid_len);
    int at = n;
    if (n == SCAN_MAX) {   /* full: replace the weakest if this one is stronger */
        at = 0;
        for (int i = 1; i < n; i++)
            if (s_scan_recs[i].rssi < s_scan_recs[at].rssi) at = i;
        if (s_scan_recs[at].rssi >= r.rssi) return n;
    }
    taskENTER_CRITICAL(&s_lock);
    s_scan_recs[at] = r;
    if (at == n) s_scan_n = ++n;
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

static void do_scan(void)
{
    s_scan_run = true;
    set_state(NET_SCANNING, NET_EV_SCAN);
    if (!s_scan_recs) s_scan_recs = heap_caps_calloc(SCAN_MAX, sizeof *s_scan_recs, MALLOC_CAP_SPIRAM);
    power_wifi_hold(true);
    esp_err_t err = s_scan_recs ? wifi_up() : ESP_ERR_NO_MEM;
    int n = 0;
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        s_scan_n = 0;
        taskEXIT_CRITICAL(&s_lock);
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) err = esp_wifi_start();
        const wifi_scan_config_t sc = {.scan_type = WIFI_SCAN_TYPE_ACTIVE,
                                       .scan_time.active = {.min = SCAN_MS - 20, .max = SCAN_MS}};
        if (err == ESP_OK) err = esp_wifi_scan_start(&sc, true);   /* blocking: every channel */
        wifi_ap_record_t ap;
        while (err == ESP_OK && esp_wifi_scan_get_ap_record(&ap) == ESP_OK) n = scan_keep(n, &ap);
        esp_wifi_clear_ap_list();
    }
    wifi_off();
    power_wifi_hold(false);
    s_scan_run = false;
    s_busy = busy_left();   /* F1: a queued session keeps it (and a claim, Task 12) */
    ESP_LOGI(TAG, "scan: %s, %d records, net stack free %u", esp_err_to_name(err), n, net_stack_hwm());
    set_state(err == ESP_OK ? NET_IDLE : NET_ERR_NO_MEM, NET_EV_SCAN);
}

/* ------------------------------------------------------------ the trial join (net task; C1, R202, spec section 7.3) */
/* omni_join_drive is the only caller of these, so their order is the host-tested order (test_join_call_order). Each
 * wraps its driver calls one to one and logs numbers and verdicts only: never the password, never the SSID. */
typedef struct {
    omni_trial_t t;
    wifi_config_t cfg;   /* the trial config: net's copy of the password inside, wiped at the end */
    bool wrote;          /* the commit's set_config returned ESP_OK */
} trial_ctx_t;

/* 0 on ESP_OK; otherwise -1, the error logged (a name and a code, never a value) */
static int drv(esp_err_t err, const char *what)
{
    if (err != ESP_OK) ESP_LOGE(TAG, "trial: %s: %s", what, esp_err_to_name(err));
    return err == ESP_OK ? 0 : -1;
}

/* As prov_start (F4): a driver still up after a failed deinit holds the last attempt's config; one more try for a fresh
 * init, and if it is still up the trial ends here (NO_MEM, before RAM storage and set_config: C1's order holds). */
static int trial_up(void *c)
{
    (void)c;
    if (s_wifi_up) wifi_off();
    if (s_wifi_up) ESP_LOGE(TAG, "trial: driver not fresh, refused");
    return !s_wifi_up && wifi_up() == ESP_OK ? 0 : -1;
}

static int trial_ram(void *c)
{
    (void)c;
    return drv(esp_wifi_set_storage(WIFI_STORAGE_RAM), "storage RAM");
}

static void trial_ok_log(void *c)
{
    (void)c;
    ESP_LOGI(TAG, "trial: storage RAM ok");
}

/* The trial's first call and the commit's: the same config. STA mode is set first if needed (set_config wants it):
 * on the trial that runs under RAM storage, at the commit the mode is already STA. */
static int trial_set(void *c)
{
    trial_ctx_t *x = c;
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err == ESP_OK && mode != WIFI_MODE_STA) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &x->cfg);
    if (x->t.committed && err == ESP_OK) {   /* the commit wrote: the saved network is the new one */
        x->wrote = true;
        s_provisioned = true;
        s_auth_fails = 0;
        char name[33] = {0};
        memcpy(name, s_trial_ssid, s_trial_ssid_len);
        set_ssid(name);
    }
    return drv(err, "set_config");
}

/* Connect and wait on an IP, a failure or the cancel bit; the disconnect reason is classed for the trial (Minor 17) */
static omni_trial_ev_t trial_wait(void *c)
{
    (void)c;
    set_state(NET_JOINING, NET_EV_TRIAL);
    esp_err_t err;
    EventBits_t got = join_wait(BIT_CANCEL, &err);
    ESP_LOGI(TAG, "trial: join %u, %s, reason %u", (unsigned)got, esp_err_to_name(err), s_disc_reason);
    if (got) return got == BIT_GOT_IP ? OMNI_TE_GOT_IP : OMNI_TE_CANCEL;
    return err == ESP_OK && omni_join_auth_reason(s_disc_reason) ? OMNI_TE_FAIL_AUTH : OMNI_TE_FAIL_NO_AP;
}

static int trial_flash(void *c)
{
    (void)c;
    return drv(esp_wifi_set_storage(WIFI_STORAGE_FLASH), "storage FLASH");
}

static bool trial_sntp(void *c)
{
    (void)c;
    set_state(NET_SYNCING, NET_EV_TRIAL);
    return sntp_sync();
}

static void trial_off(void *c)
{
    (void)c;
    wifi_off();
}

/* I11: a fresh init reads NVS; the saved SSID and password must equal the trial's. Logs the verdict only. */
static bool trial_persisted(void *c)
{
    trial_ctx_t *x = c;
    bool same = false;
    if (!s_wifi_up && wifi_up() == ESP_OK) {   /* only a fresh init (the deinit before it worked) reads NVS */
        wifi_config_t now = {0};
        same = esp_wifi_get_config(WIFI_IF_STA, &now) == ESP_OK &&
               memcmp(now.sta.ssid, x->cfg.sta.ssid, sizeof now.sta.ssid) == 0 &&
               memcmp(now.sta.password, x->cfg.sta.password, sizeof now.sta.password) == 0;
        omni_wipe(&now, sizeof now);
    }
    if (same) ESP_LOGI(TAG, "commit verified");
    else ESP_LOGE(TAG, "commit NOT persisted");
    return same;
}

static const omni_join_ops_t TRIAL_OPS = {
    .wifi_up = trial_up, .storage_ram = trial_ram, .storage_ok_log = trial_ok_log, .set_config = trial_set,
    .connect_wait = trial_wait, .storage_flash = trial_flash, .sntp = trial_sntp, .wifi_off = trial_off,
    .persisted = trial_persisted,
};

static void do_trial(void)
{
    static const omni_net_state_t ST[] = {
        [OMNI_TRES_NONE] = NET_ERR_NO_AP, [OMNI_TRES_OK] = NET_OK, [OMNI_TRES_PASSWORD] = NET_ERR_PASSWORD,
        [OMNI_TRES_NO_AP] = NET_ERR_NO_AP, [OMNI_TRES_SYNC] = NET_ERR_SYNC, [OMNI_TRES_CANCELLED] = NET_IDLE,
        [OMNI_TRES_NO_MEM] = NET_ERR_NO_MEM};
    trial_ctx_t x = {0};
    memcpy(x.cfg.sta.ssid, s_trial_ssid, s_trial_ssid_len);   /* the exact bytes */
    memcpy(x.cfg.sta.password, s_trial_pass, s_trial_pass_len);
    x.cfg.sta.threshold.authmode = (wifi_auth_mode_t)omni_join_threshold(s_trial_auth);   /* no downgrade */
    x.cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    s_trial = true;
    power_wifi_hold(true);
    bool persisted = false;
    omni_trial_res_t r = omni_join_drive(&TRIAL_OPS, &x, &x.t, &persisted);
    omni_wipe(&x.cfg, sizeof x.cfg);
    omni_wipe(s_trial_pass, sizeof s_trial_pass);
    s_trial_pass_len = 0;
    const omni_net_state_t st = (unsigned)r < sizeof ST / sizeof ST[0] ? ST[r] : NET_ERR_NO_AP;
    taskENTER_CRITICAL(&s_lock);
    memset(s_trial_res_ssid, 0, sizeof s_trial_res_ssid);
    memcpy(s_trial_res_ssid, s_trial_ssid, s_trial_ssid_len);
    s_trial_res = st;
    s_trial_has_res = true;
    taskEXIT_CRITICAL(&s_lock);
    power_wifi_hold(false);
    s_trial = false;
    s_busy = busy_left();   /* F1: a queued session keeps it (and a claim, Task 12) */
    ESP_LOGI(TAG, "trial: result %d, commit %d%d%d, net stack free %u", (int)r, x.t.committed, x.wrote, persisted,
             net_stack_hwm());
    set_state(st, NET_EV_TRIAL);
}

static void prov_start(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_prov_queued) s_prov_queued--;
    taskEXIT_CRITICAL(&s_lock);
    if (s_prov) { /* still up (after a failure the manager accepts new credentials): back to the QR */
        set_state(NET_PROV_WAITING, NET_EV_PROV);
        return;
    }
    network_prov_mgr_config_t cfg = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE, /* keep BLE re-initialisable for a re-scan */
        .app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
        .network_prov_wifi_conn_cfg = {.wifi_conn_attempts = PROV_CONN_ATTEMPTS},
    };
    s_busy = true;
    new_pop(); /* this session's PoP, before the manager starts the radios */
    power_wifi_hold(true); /* the manager runs Wi-Fi (scans, the join) for the whole session */
    if (s_wifi_up) wifi_off();     /* F4: a stale driver (an earlier deinit failed): one more try for a fresh init */
    const bool fresh = !s_wifi_up; /* a fresh esp_wifi_init reads the saved network from NVS */
    esp_err_t err = fresh ? wifi_up() : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        /* I1: the saved network, before the manager touches the config (its session start sets RAM storage and an
         * empty config, manager.c 2455-2467; the phone's credentials are written at 1570-1577). F4: without a
         * snapshot read from a fresh init the session is refused, nothing written. */
        s_cred_recv = s_cred_ok = false;
        const bool read = esp_wifi_get_config(WIFI_IF_STA, &s_snap) == ESP_OK;
        s_snap_has = read && s_snap.sta.ssid[0] != 0;
        if (!s_snap_has) omni_wipe(&s_snap, sizeof s_snap);
        err = omni_prov_start_ok(fresh, read) ? network_prov_mgr_init(cfg) : ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        char pop[sizeof s_pop];
        taskENTER_CRITICAL(&s_lock);
        memcpy(pop, s_pop, sizeof pop);
        taskEXIT_CRITICAL(&s_lock);
        err = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, (const void *)pop, s_name, NULL);
        if (err != ESP_OK) network_prov_mgr_deinit();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "provisioning start: %s", esp_err_to_name(err));
        omni_wipe(&s_snap, sizeof s_snap);   /* a failed start restores the old config itself (manager.c 2643-2650) */
        s_snap_has = false;
        wifi_off();
        power_wifi_hold(false);
        s_busy = busy_left();   /* a scan or trial claimed behind this CMD_PROV keeps it (Task 12) */
        set_state(NET_ERR_NO_MEM, NET_EV_PROV); /* internal RAM in practice, or no fresh snapshot (logged above) */
        return;
    }
    s_prov = true;
    s_prov_t0 = now_ms();
    net_ssid(s_prev_ssid);
    set_ssid("");
    set_state(NET_PROV_WAITING, NET_EV_PROV);
}

/* manager_wrote's second witness: the driver's config is not the empty one the manager's session start left (only the
 * manager's CRED_RECV write changes it, and only a CRED_FAIL handled after it empties it again). noinline: the config's
 * frame is not stacked under network_prov_mgr_deinit. */
static __attribute__((noinline)) bool config_set(char name[33])
{
    wifi_config_t cur = {0};
    bool set = esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK && cur.sta.ssid[0] != 0;
    if (name) {   /* F5: the name the driver holds, for the cached name (never logged) */
        memset(name, 0, 33);
        if (set) memcpy(name, cur.sta.ssid, 32);
    }
    omni_wipe(&cur, sizeof cur);
    return set;
}

/* Every way a session ends comes here (the cap, the screen left, sleep, a stop after a failure, the manager's own stop
 * after a success), on the net task. After network_prov_mgr_deinit the manager writes nothing more, so the restore
 * runs after its last write (1577, or a CRED_FAIL's empty config at 2715) and before wifi_off (I1, O1). */
static void prov_end(omni_prov_end_t why)
{
    if (!s_prov) return;
    network_prov_mgr_deinit();
    s_prov = false;
    ESP_LOGI(TAG, "provisioning service stopped");
    const bool wrote = omni_prov_manager_wrote(s_cred_recv, config_set(NULL));   /* R221 */
    const omni_prov_end_plan_t plan = omni_prov_end_plan(s_snap_has, wrote, s_cred_ok, why);
    bool restored = true;
    if (plan.restore) {   /* F5: one retry */
        esp_err_t err = ESP_FAIL;
        for (int i = 0; i < 2 && err != ESP_OK; i++) {
            err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
            if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &s_snap);
            ESP_LOGW(TAG, "provisioning: restore %s", esp_err_to_name(err));
        }
        restored = err == ESP_OK;
    }
    ESP_LOGI(TAG, "provisioning end %d: %d%d%d", (int)why, s_snap_has, wrote, (int)s_cred_ok);
    char name[33];
    const bool drv = !restored && config_set(name);
    s_provisioned = omni_prov_end_provisioned(plan, restored, drv);
    if (!restored) {   /* the cached state follows what the driver reports; the verdict only */
        set_ssid(name);
        ESP_LOGE(TAG, "provisioning: restore failed, driver holds a network %d", drv);
    } else if (plan.name_from_snapshot) set_ssid(s_prev_ssid);
    else if (!s_cred_ok) set_ssid("");   /* nothing saved: no name */
    omni_wipe(&s_snap, sizeof s_snap);
    s_snap_has = false;
    wifi_off();
    power_wifi_hold(false);
    s_busy = busy_left();   /* a scan or trial claimed before the session keeps it (Task 12) */
}

static omni_prov_phase_t phase_of(omni_net_state_t st)
{
    switch (st) {
    case NET_PROV_WAITING: return PROV_PH_WAITING;
    case NET_PROV_PHONE: return PROV_PH_PHONE;
    case NET_JOINING:
    case NET_SYNCING: return PROV_PH_JOINING;
    case NET_OK: return PROV_PH_DONE;
    default: return PROV_PH_FAILED;
    }
}

/* Ends the session before it succeeded (left, asleep, the cap) and tells the event loop: IDLE | PROV. */
static void prov_stop(const char *why, omni_prov_end_t end)
{
    if (!s_prov) return;
    ESP_LOGI(TAG, "provisioning stopped: %s (%lu s)", why, (unsigned long)((now_ms() - s_prov_t0) / 1000));
    prov_end(end); /* the saved network back (restored, or never touched) when the session did not succeed */
    set_state(NET_IDLE, NET_EV_PROV);
}

static void handle(const cmd_t *c)
{
    omni_net_state_t st = s_state;
    switch (c->kind) {
    case CMD_PROV: prov_start(); break;
    case CMD_PROV_STOP:
        if (s_prov && omni_prov_stop_on(phase_of(st), (omni_prov_why_t)c->arg))
            prov_stop(c->arg == PROV_WHY_SLEEP ? "watch asleep" : "screen left",
                      c->arg == PROV_WHY_SLEEP ? PROV_END_SLEEP : PROV_END_LEFT);
        break;
    case CMD_SYNC: do_sync(); break;
    case CMD_SCAN:
    case CMD_TRY_JOIN:
        /* F1: never inside a phone session (a CMD_PROV ahead of it in the queue): refused, busy left to the session,
         * the password copy wiped, and the refusal posted so the screen is not left waiting */
        if (omni_join_admit(s_prov)) {
            taskENTER_CRITICAL(&s_lock);   /* the run flag before the claim ends: busy_left always sees one */
            if (c->kind == CMD_SCAN) s_scan_run = true;
            else s_trial = true;
            s_claimed = false;
            taskEXIT_CRITICAL(&s_lock);
            if (c->kind == CMD_SCAN) do_scan();
            else do_trial();
            break;
        }
        s_claimed = false;   /* refused: busy stays the session's */
        if (c->kind == CMD_TRY_JOIN) {
            omni_wipe(s_trial_pass, sizeof s_trial_pass);
            s_trial_pass_len = 0;
        }
        ESP_LOGW(TAG, "%s refused: provisioning", c->kind == CMD_SCAN ? "scan" : "trial");
        set_state(NET_ERR_NO_MEM, c->kind == CMD_SCAN ? NET_EV_SCAN : NET_EV_TRIAL);
        break;
    case CMD_FORGET:
        /* C2: the bench lock first, then net's own state now (I4: never the caller-set s_busy). Refused: nothing
         * done (a phone session, which this refuses, is therefore never ended here: no restore to skip). */
        if (s_forget_lock) {
            ESP_LOGW(TAG, "FORGET REFUSED (bench lock)");
            break;
        }
        if (!omni_join_forget_allowed(s_read_only, s_prov, s_trial || s_scan_run)) {
            ESP_LOGW(TAG, "FORGET REFUSED (state %d%d%d)", s_read_only, s_prov, s_trial || s_scan_run);
            break;
        }
        if (wifi_up() == ESP_OK) {
            esp_wifi_restore(); /* drops the saved network (NVS) */
            wifi_off();
        }
        s_provisioned = false;
        s_auth_fails = 0;
        set_ssid("");
        s_busy = busy_left();   /* "Forget and re-scan": its CMD_PROV is queued (and a claim, Task 12) */
        set_state(NET_IDLE, 0);
        break;
    case CMD_FORCE: set_state((omni_net_state_t)(c->arg & 0xff), (c->arg & ~0xff) | NET_EV_LOUD); break;
    case CMD_BLE_UP:
        if (s_prov && (st == NET_PROV_WAITING || st == NET_ERR_PASSWORD || st == NET_ERR_NO_AP))
            set_state(NET_PROV_PHONE, NET_EV_PROV);
        break;
    case CMD_BLE_DOWN:
        if (s_prov && st == NET_PROV_PHONE) set_state(NET_PROV_WAITING, NET_EV_PROV);
        break;
    case CMD_CRED_RECV:
        if (!s_prov) break; /* a late event of a session that has ended */
        s_cred_recv = true;
        set_ssid(c->ssid);
        set_state(NET_JOINING, NET_EV_PROV);
        break;
    case CMD_CRED_FAIL:
        /* The manager erases the bad credentials and waits for new ones (the phone can retry). Its empty write
         * (manager.c 2715) erases the saved network too: prov_end writes the snapshot back if the session ends
         * without a success (I1). */
        if (!s_prov) break;
        network_prov_mgr_reset_wifi_sm_state_on_failure();
        s_provisioned = false;
        set_state(c->arg == NETWORK_PROV_WIFI_STA_AUTH_ERROR ? NET_ERR_PASSWORD : NET_ERR_NO_AP, NET_EV_PROV);
        break;
    case CMD_CRED_OK:
        /* Joined: set the clock now. Wi-Fi stays up until the service stops (its auto-stop gives the app
         * time to read the result), then prov_end turns it off. */
        if (!s_prov) break;
        s_cred_ok = true;
        s_provisioned = true;
        set_state(NET_SYNCING, NET_EV_PROV);
        set_state(sntp_sync() ? NET_OK : NET_ERR_SYNC, NET_EV_PROV);
        break;
    case CMD_PROV_END: prov_end(s_cred_ok ? PROV_END_SUCCESS : PROV_END_FAILED); break;
    default: break;
    }
}

static void net_task(void *arg)
{
    (void)arg;
    cmd_t c;
    for (;;) {
        /* while a session runs, wake each second for its hard cap */
        if (xQueueReceive(s_q, &c, s_prov ? pdMS_TO_TICKS(1000) : portMAX_DELAY) == pdTRUE) handle(&c);
        if (s_prov && omni_prov_expired(s_prov_t0, now_ms())) prov_stop("time limit", PROV_END_CAP);
    }
}

/* ------------------------------------------------------------ public */
void net_init(bool read_only)
{
    s_read_only = read_only;
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac); /* base MAC (= Wi-Fi STA MAC on the S3) */
    omni_prov_name(mac, s_name);
    new_pop(); /* radios off at boot; each session makes its own */

    /* A failure here leaves the watch running without network (no task, net_* do nothing) instead of a
     * boot loop. */
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    }
    if (err == ESP_OK && esp_netif_create_default_wifi_sta() == NULL) err = ESP_FAIL;
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed (%s): no network", esp_err_to_name(err));
        return;
    }
    s_bits = xEventGroupCreate();
    s_q = xQueueCreate(8, sizeof(cmd_t));

    if (wifi_up() == ESP_OK) { /* read the saved network once, then the driver goes again */
        wifi_config_t cfg = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) set_ssid((const char *)cfg.sta.ssid);
        s_provisioned = cfg.sta.ssid[0] != '\0';
        omni_wipe(&cfg, sizeof cfg);   /* the saved password's stack copy */
        wifi_off();
    }
    if (take_boot_unprov(read_only)) { /* debug: this boot runs the unprovisioned path; saved network kept */
        ESP_LOGW(TAG, "boot-unprov: acting unprovisioned this boot");
        s_provisioned = false;
    }
    xTaskCreate(net_task, "net", NET_STACK, NULL, 4, &s_task);
    ESP_LOGI(TAG, "%s, %s", s_name, net_is_provisioned() ? "provisioned" : "not provisioned");
}

bool net_is_provisioned(void) { return s_provisioned; }

/* s_busy is set at once (so a scheduled sync cannot slip in) and cleared again if the command was dropped (m9:
 * a lost CMD_SYNC used to leave it set for good: no syncs, no clock corrections). */
bool net_start_provisioning(void)
{
    if (s_q == NULL) return false;
    taskENTER_CRITICAL(&s_lock);
    s_busy = true;
    s_prov_queued++;
    taskEXIT_CRITICAL(&s_lock);
    if (send(CMD_PROV, 0, NULL)) return true;
    taskENTER_CRITICAL(&s_lock);
    s_prov_queued--;
    s_busy = busy_left();
    taskEXIT_CRITICAL(&s_lock);
    return false;
}

void net_stop_provisioning(omni_prov_why_t why) { send(CMD_PROV_STOP, (int)why, NULL); }

omni_prov_phase_t net_prov_phase(void) { return s_prov ? phase_of(s_state) : PROV_PH_NONE; }

void net_sync_now(void)
{
    if (s_q == NULL) return;
    taskENTER_CRITICAL(&s_lock);   /* checked and set together, as claim_busy (no claim: a sync's end clears busy) */
    const bool ok = !s_busy && !s_prov;   /* one attempt at a time; provisioning syncs on its own */
    if (ok) s_busy = true;
    taskEXIT_CRITICAL(&s_lock);
    if (!ok || send(CMD_SYNC, 0, NULL)) return;
    taskENTER_CRITICAL(&s_lock);   /* dropped: busy follows what is left (a claim or a session queued meanwhile) */
    s_busy = busy_left();
    taskEXIT_CRITICAL(&s_lock);
}

void net_forget(void) { send(CMD_FORGET, 0, NULL); }

void net_forget_lock(bool on)
{
    s_forget_lock = on;
}
bool net_forget_locked(void) { return s_forget_lock; }

/* busy is checked and set together, so two callers cannot both start */
static bool claim_busy(void)
{
    taskENTER_CRITICAL(&s_lock);
    bool ok = !s_busy && !s_prov;
    if (ok) s_busy = s_claimed = true;
    taskEXIT_CRITICAL(&s_lock);
    return ok;
}

/* The claim's command was not sent: it ends, and busy follows what is left */
static void unclaim(void)
{
    taskENTER_CRITICAL(&s_lock);
    s_claimed = false;
    s_busy = busy_left();
    taskEXIT_CRITICAL(&s_lock);
}

bool net_scan(void)
{
    if (s_q == NULL || !claim_busy()) return false;
    if (send(CMD_SCAN, 0, NULL)) return true;
    unclaim();
    return false;
}

int net_scan_results(omni_scan_rec_t *out, int max)
{
    taskENTER_CRITICAL(&s_lock);
    int n = s_scan_recs ? s_scan_n : 0;
    if (n > max) n = max;
    if (n > 0) memcpy(out, s_scan_recs, (size_t)n * sizeof *out);
    taskEXIT_CRITICAL(&s_lock);
    return n < 0 ? 0 : n;
}

int net_try_join(const uint8_t *ssid, uint8_t ssid_len, const char *pass, uint8_t pass_len, uint8_t authmode)
{
    omni_join_refuse_t r = omni_join_check(s_read_only, s_busy, s_prov, authmode, ssid_len, pass, pass_len);
    if (r != OMNI_JOIN_OK) return r;
    if (s_q == NULL || ssid == NULL || !claim_busy()) return OMNI_JOIN_REFUSE_BUSY;
    xEventGroupClearBits(s_bits, BIT_CANCEL);   /* a stale Cancel of an earlier attempt */
    /* net's own copy (the net task reads it after the queue hands it the command) */
    memcpy(s_trial_ssid, ssid, ssid_len);
    s_trial_ssid_len = ssid_len;
    s_trial_auth = authmode;
    memset(s_trial_pass, 0, sizeof s_trial_pass);
    if (pass_len) memcpy(s_trial_pass, pass, pass_len);
    s_trial_pass_len = pass_len;
    if (send(CMD_TRY_JOIN, 0, NULL)) return OMNI_JOIN_OK;
    omni_wipe(s_trial_pass, sizeof s_trial_pass);
    s_trial_pass_len = 0;
    unclaim();
    return OMNI_JOIN_REFUSE_BUSY;
}

void net_try_cancel(void)
{
    if (s_bits) xEventGroupSetBits(s_bits, BIT_CANCEL);
}

bool net_trial_last(omni_net_state_t *st, char ssid[33])
{
    taskENTER_CRITICAL(&s_lock);
    bool have = s_trial_has_res;
    if (have) {
        *st = s_trial_res;
        memcpy(ssid, s_trial_res_ssid, 33);
    }
    taskEXIT_CRITICAL(&s_lock);
    return have;
}
omni_net_state_t net_state(void) { return s_state; }
bool net_busy(void) { return s_busy; }
bool net_provisioning(void) { return s_prov; }
const char *net_prov_name(void) { return s_name; }
void net_qr_json(char *out, size_t n)
{
    taskENTER_CRITICAL(&s_lock);
    strlcpy(out, s_qr, n);
    taskEXIT_CRITICAL(&s_lock);
}
unsigned net_stack_hwm(void) { return s_task ? (unsigned)uxTaskGetStackHighWaterMark(s_task) : 0; }

/* The one-shot flag lives in namespace `watch` (Stage 4b: `omni` is read-only, the older firmwares' rollback source). */
#define BOOT_UNPROV_NS "watch"
#define BOOT_UNPROV_KEY "boot_unprov"

int net_debug_boot_unprovisioned(bool read_only)
{
    if (read_only) return ESP_ERR_INVALID_STATE;   /* a read-only boot writes nothing (R124 item 2) */
    nvs_handle_t h;
    esp_err_t err = nvs_open(BOOT_UNPROV_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, BOOT_UNPROV_KEY, 1);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* Reads and clears the one-shot flag (cleared as 0, not erased: the firmware has no erase call at all). Read-only
 * unless it is set, so an ordinary boot writes nothing here. On a read-only boot it is neither read nor cleared: a set
 * flag then fires once on the next writable boot (no write now, and no boot left stuck unprovisioned). */
static bool take_boot_unprov(bool read_only)
{
    if (read_only) return false;
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(BOOT_UNPROV_NS, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t err = nvs_get_u8(h, BOOT_UNPROV_KEY, &v);
    nvs_close(h);
    if (err != ESP_OK || v == 0) return false;
    if (nvs_open(BOOT_UNPROV_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, BOOT_UNPROV_KEY, 0);
        nvs_commit(h);
        nvs_close(h);
    }
    return true;
}

void net_debug_force(omni_net_state_t st, int flags) { send(CMD_FORCE, (int)st | (flags & ~0xff), NULL); }
