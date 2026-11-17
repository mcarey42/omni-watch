#include "omni/prov.h"
#include <string.h>

void omni_prov_name(const uint8_t mac[6], char out[16])
{
    static const char hex[] = "0123456789ABCDEF";
    memcpy(out, "PROV_OMNI_", 10);
    out[10] = hex[mac[4] >> 4];
    out[11] = hex[mac[4] & 15];
    out[12] = hex[mac[5] >> 4];
    out[13] = hex[mac[5] & 15];
    out[14] = '\0';
}

static size_t put(char *out, size_t n, size_t pos, const char *s)
{
    while (*s && pos + 1 < n) out[pos++] = *s++;
    return pos;
}

/* at most len bytes of s */
static size_t put_n(char *out, size_t n, size_t pos, const char *s, size_t len)
{
    while (len-- && *s && pos + 1 < n) out[pos++] = *s++;
    return pos;
}

void omni_prov_qr_json(const char *name, const char *pop, char *out, size_t n)
{
    if (!out || n == 0) return;
    size_t p = 0;
    p = put(out, n, p, "{\"ver\":\"v1\",\"name\":\"");
    p = put(out, n, p, name ? name : "");
    p = put(out, n, p, "\",\"pop\":\"");
    p = put(out, n, p, pop ? pop : "");
    p = put(out, n, p, "\",\"transport\":\"ble\"}");
    out[p] = '\0';
}

/* R151: the owner's rule is that the PoP is never printed. Everything up to the value of "pop":", then the marker,
 * then everything from the value's closing quote (nothing more if a truncated payload has none). */
void omni_prov_qr_redact(const char *json, char *out, size_t n)
{
    if (!out || n == 0) return;
    if (!json) json = "";
    static const char KEY[] = "\"pop\":\"";
    size_t p = 0;
    const char *k = strstr(json, KEY);
    if (k == NULL) {
        p = put(out, n, p, json);
    } else {
        const char *v = k + sizeof KEY - 1;
        p = put_n(out, n, p, json, (size_t)(v - json));
        p = put(out, n, p, OMNI_PROV_POP_REDACTED);
        const char *e = strchr(v, '"');
        if (e) p = put(out, n, p, e);
    }
    out[p] = '\0';
}
