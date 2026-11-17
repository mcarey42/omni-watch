#include "ntp_client.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "net";

#define NTP_TRIES 8        /* requests per sync at most (a lost or rejected reply is tried again) */
#define NTP_REPLY_MS 1000  /* per request (a longer round trip is rejected anyway, OMNI_NTP_DELAY_MAX_US) */
#define NTP_GAP_MS 100     /* between requests */
#define NTP_MISSES 3       /* this many requests in a row with no usable reply: the next server name */

static int64_t wall_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* One request to `to` and its reply within NTP_REPLY_MS. A reply that is not to this request (a late one to an earlier
 * request: its originate differs; or from another address) is skipped. -1: no reply. */
static int query(int sock, const struct sockaddr_in *to, bool clock_set, omni_ntp_sample_t *out, ntp_client_result_t *r)
{
    uint8_t pkt[OMNI_NTP_PACKET + 4];
    const int64_t t1 = wall_us();
    omni_ntp_request(pkt, t1);
    if (sendto(sock, pkt, OMNI_NTP_PACKET, 0, (const struct sockaddr *)to, sizeof *to) != OMNI_NTP_PACKET) {
        r->unsent++;
        return -1;
    }
    const int64_t until = esp_timer_get_time() + (int64_t)NTP_REPLY_MS * 1000;
    for (;;) {
        const int64_t left = until - esp_timer_get_time();
        if (left <= 0) return -1;
        /* whole ms, rounded up (re-review N1): lwIP truncates to ms, and 0 would wait forever */
        const int64_t ms = omni_ntp_timeout_ms(left);
        struct timeval tv = {.tv_sec = (time_t)(ms / 1000), .tv_usec = (suseconds_t)(ms % 1000 * 1000)};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        struct sockaddr_in from;
        socklen_t flen = sizeof from;
        const int n = recvfrom(sock, pkt, sizeof pkt, 0, (struct sockaddr *)&from, &flen);
        const int64_t t4 = wall_us();   /* first, before any check */
        if (n < 0) return -1;
        if (from.sin_addr.s_addr != to->sin_addr.s_addr || from.sin_port != to->sin_port) {
            r->strays++;
            continue;
        }
        const omni_ntp_err_t e = omni_ntp_sample(pkt, (size_t)n, t1, t4, clock_set, out);
        if (e != OMNI_NTP_ORIGIN) return (int)e;
        r->strays++;
    }
}

static bool resolve(const char *server, struct sockaddr_in *to)
{
    const struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
    struct addrinfo *res = NULL;
    if (getaddrinfo(server, NULL, &hints, &res) != 0 || res == NULL) return false;
    memcpy(to, res->ai_addr, sizeof *to);
    to->sin_port = htons(OMNI_NTP_PORT);
    freeaddrinfo(res);
    return true;
}

void ntp_client_sample(const char *const *servers, int n_servers, bool clock_set, bool no_modem_sleep,
                       uint32_t budget_ms, ntp_client_result_t *r)
{
    memset(r, 0, sizeof *r);
    const int64_t until = now_ms() + budget_ms;
    omni_ntp_sample_t smp[NTP_CLIENT_SAMPLES];
    struct sockaddr_in to;
    bool named;
    int si = 0, misses = 0;
    while (!(named = resolve(servers[si], &to)) && now_ms() + 1000 < until) {
        si = (si + 1) % n_servers;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!named) ESP_LOGW(TAG, "sntp: no server name resolved");
    const int sock = named ? socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP) : -1;
    if (named && sock < 0) ESP_LOGW(TAG, "sntp: no socket");
    wifi_ps_type_t ps = WIFI_PS_MIN_MODEM;
    const bool ps_saved = sock >= 0 && no_modem_sleep && esp_wifi_get_ps(&ps) == ESP_OK;
    if (ps_saved && esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) ESP_LOGD(TAG, "sntp: modem sleep stays on");
    while (sock >= 0 && r->n < NTP_CLIENT_SAMPLES && r->tries < NTP_TRIES && now_ms() + NTP_REPLY_MS <= until) {
        if (r->tries) vTaskDelay(pdMS_TO_TICKS(NTP_GAP_MS));
        if (misses == NTP_MISSES && n_servers > 1) {   /* that server is not answering us: another of the pool */
            misses = 0;
            si = (si + 1) % n_servers;
            resolve(servers[si], &to);   /* on failure, the same address again */
        }
        r->tries++;
        const int e = query(sock, &to, clock_set, &smp[r->n], r);
        misses = e == OMNI_NTP_OK ? 0 : misses + 1;
        if (e == OMNI_NTP_OK) r->n++;
        else if (e < 0) r->lost++;
        else r->rejected[e]++;
    }
    if (ps_saved) esp_wifi_set_ps(ps);
    if (sock >= 0) close(sock);
    if (named) inet_ntoa_r(to.sin_addr, r->server, sizeof r->server);
    const int b = omni_ntp_best(smp, r->n);
    if (b >= 0) r->best = smp[b];
    for (int i = 0, k = 0; i < r->n && k < (int)sizeof r->delays; i++)
        k += snprintf(r->delays + k, sizeof r->delays - (size_t)k, "%s%lld", i ? "/" : "", (long long)(smp[i].delay_us / 1000));
}
