#pragma once
#include <stddef.h>
#include <stdint.h>

/* "PROV_OMNI_" + uppercase hex of mac[4], mac[5], e.g. "PROV_OMNI_3456". */
void omni_prov_name(const uint8_t mac[6], char out[16]);
/* {"ver":"v1","name":"<name>","pop":"<pop>","transport":"ble"}; always NUL-terminated, truncated if n is small. */
void omni_prov_qr_json(const char *name, const char *pop, char *out, size_t n);
/* R151: json (an omni_prov_qr_json payload) with the PoP's value replaced by OMNI_PROV_POP_REDACTED, for anything
 * that prints it (the console's `net`). Every other byte as in json; a json with no "pop":" is copied as it is; a
 * truncated value (no closing quote) ends at the marker. Always NUL-terminated, truncated if n is small. */
#define OMNI_PROV_POP_REDACTED "<redacted>"
void omni_prov_qr_redact(const char *json, char *out, size_t n);
