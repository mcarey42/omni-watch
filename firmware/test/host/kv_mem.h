#pragma once
/* An in-memory omni_kv_t with fault injection, for test_migrate, migrate_host and Task 6's test_boot. It behaves as
 * the device adapter must: an absent key (and so an absent namespace) is NOT_FOUND, a set naming OMNI_NS_OLD is
 * refused (ERR, nothing written), a value larger than the caller's buffer or of another type is ERR. (Raw IDF 5.5.6
 * returns ESP_ERR_NVS_NOT_FOUND for a typed get of a key stored with another type, nvs_storage.cpp:257-271; store.c's
 * adapter turns that into ERR with nvs_find_key, so this model is the adapter's, review m5.)
 *
 * It also reads and writes migrate_host's line format, one entry per line:
 *   <ns> <key> <type> <hex>
 * <type> is one of blob str u8 i8 u16 i16 u32 i32 u64 i64; <hex> is the value's bytes (a string without its NUL, an
 * integer little-endian at its width), lower case, "-" when empty. A line no set touched is written back as read. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "omni/migrate.h"

#define KV_MEM_MAX 256
#define KV_MEM_NAME 16     /* NVS: 15 characters and the NUL */
#define KV_MEM_LOG 32

typedef enum {
    KV_T_BLOB, KV_T_STR, KV_T_U8, KV_T_I8, KV_T_U16, KV_T_I16, KV_T_U32, KV_T_I32, KV_T_U64, KV_T_I64, KV_T__COUNT
} kv_type_t;

typedef struct {
    char ns[KV_MEM_NAME], key[KV_MEM_NAME];
    kv_type_t type;
    uint8_t *val;          /* malloc'd; NULL when n == 0 */
    size_t n;
    char *line;            /* the line as read (no newline), until a set replaces the value; else NULL */
} kv_entry_t;

typedef struct {
    kv_entry_t e[KV_MEM_MAX];
    int count;
    int fail_after;        /* < 0: never; else every set after the Nth attempted set returns ERR without writing */
    int sets;              /* sets attempted, refused ones included */
    int sets_ok;
    int old_ns_sets;       /* sets naming OMNI_NS_OLD (each refused) */
    const char *fail_get_ns, *fail_get_key;   /* a get of this key returns ERR */
    char log[KV_MEM_LOG][2 * KV_MEM_NAME];    /* "<ns>/<key>" of each set that wrote, in order */
} kv_mem_t;

void kv_mem_init(kv_mem_t *m);
void kv_mem_free(kv_mem_t *m);
void kv_mem_reset_counts(kv_mem_t *m);
/* Puts an entry directly (fixtures): no refusal, no fault, no count. 0, or -1 if full or a name is too long. */
int kv_mem_put(kv_mem_t *m, const char *ns, const char *key, kv_type_t type, const uint8_t *val, size_t n);
/* Removes an entry directly (fixtures): 0, or -1 if absent. */
int kv_mem_del(kv_mem_t *m, const char *ns, const char *key);
const kv_entry_t *kv_mem_find(const kv_mem_t *m, const char *ns, const char *key);
omni_kv_t kv_mem_iface(kv_mem_t *m);
/* Same entries (namespace, key, type, bytes), in any order. */
bool kv_mem_equal(const kv_mem_t *a, const kv_mem_t *b);

/* The line format. load: 0, or -1 (a message on stderr naming the line). save: 0, or -1 on a write error. */
int kv_mem_load_lines(kv_mem_t *m, FILE *f);
int kv_mem_save_lines(const kv_mem_t *m, FILE *f);
