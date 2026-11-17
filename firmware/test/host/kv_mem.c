#include "kv_mem.h"
#include <stdlib.h>
#include <string.h>

static const char *const k_type_name[KV_T__COUNT] = {
    "blob", "str", "u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64"
};
static const size_t k_type_width[KV_T__COUNT] = { 0, 0, 1, 1, 2, 2, 4, 4, 8, 8 };   /* 0: variable */

void kv_mem_init(kv_mem_t *m)
{
    memset(m, 0, sizeof *m);
    m->fail_after = -1;
}

void kv_mem_free(kv_mem_t *m)
{
    for (int i = 0; i < m->count; i++) {
        free(m->e[i].val);
        free(m->e[i].line);
    }
    kv_mem_init(m);
}

void kv_mem_reset_counts(kv_mem_t *m)
{
    m->sets = m->sets_ok = m->old_ns_sets = 0;
    memset(m->log, 0, sizeof m->log);
}

static kv_entry_t *find(kv_mem_t *m, const char *ns, const char *key)
{
    for (int i = 0; i < m->count; i++)
        if (strcmp(m->e[i].ns, ns) == 0 && strcmp(m->e[i].key, key) == 0) return &m->e[i];
    return NULL;
}

const kv_entry_t *kv_mem_find(const kv_mem_t *m, const char *ns, const char *key)
{
    return find((kv_mem_t *)m, ns, key);
}

/* Stores a copy of val under (ns, key), replacing any value (and the line as read). */
static int store(kv_mem_t *m, const char *ns, const char *key, kv_type_t type, const uint8_t *val, size_t n)
{
    if (strlen(ns) >= KV_MEM_NAME || strlen(key) >= KV_MEM_NAME) return -1;
    uint8_t *copy = NULL;
    if (n) {
        copy = malloc(n);
        if (copy == NULL) return -1;
        memcpy(copy, val, n);
    }
    kv_entry_t *e = find(m, ns, key);
    if (e == NULL) {
        if (m->count == KV_MEM_MAX) { free(copy); return -1; }
        e = &m->e[m->count++];
        memset(e, 0, sizeof *e);
        strcpy(e->ns, ns);
        strcpy(e->key, key);
    }
    free(e->val);
    free(e->line);
    e->line = NULL;
    e->type = type;
    e->val = copy;
    e->n = n;
    return 0;
}

int kv_mem_put(kv_mem_t *m, const char *ns, const char *key, kv_type_t type, const uint8_t *val, size_t n)
{
    return store(m, ns, key, type, val, n);
}

int kv_mem_del(kv_mem_t *m, const char *ns, const char *key)
{
    kv_entry_t *e = find(m, ns, key);
    if (e == NULL) return -1;
    free(e->val);
    free(e->line);
    int i = (int)(e - m->e);
    memmove(&m->e[i], &m->e[i + 1], (size_t)(m->count - i - 1) * sizeof m->e[0]);
    m->count--;
    return 0;
}

static bool get_fails(const kv_mem_t *m, const char *ns, const char *key)
{
    return m->fail_get_ns != NULL && m->fail_get_key != NULL && strcmp(ns, m->fail_get_ns) == 0 &&
           strcmp(key, m->fail_get_key) == 0;
}

static omni_kv_rc_t get_blob(void *ctx, const char *ns, const char *key, uint8_t *buf, size_t *n)
{
    kv_mem_t *m = ctx;
    if (get_fails(m, ns, key)) return OMNI_KV_ERR;
    const kv_entry_t *e = find(m, ns, key);
    if (e == NULL) return OMNI_KV_NOT_FOUND;
    if (e->type != KV_T_BLOB || e->n > *n) return OMNI_KV_ERR;
    if (e->n) memcpy(buf, e->val, e->n);
    *n = e->n;
    return OMNI_KV_OK;
}

static omni_kv_rc_t get_str(void *ctx, const char *ns, const char *key, char *buf, size_t cap)
{
    kv_mem_t *m = ctx;
    if (get_fails(m, ns, key)) return OMNI_KV_ERR;
    const kv_entry_t *e = find(m, ns, key);
    if (e == NULL) return OMNI_KV_NOT_FOUND;
    if (e->type != KV_T_STR || e->n + 1 > cap) return OMNI_KV_ERR;
    if (e->n) memcpy(buf, e->val, e->n);
    buf[e->n] = '\0';
    return OMNI_KV_OK;
}

static omni_kv_rc_t set_any(kv_mem_t *m, const char *ns, const char *key, kv_type_t type, const uint8_t *val,
                            size_t n)
{
    m->sets++;
    if (strcmp(ns, OMNI_NS_OLD) == 0) { m->old_ns_sets++; return OMNI_KV_ERR; }
    if (m->fail_after >= 0 && m->sets > m->fail_after) return OMNI_KV_ERR;
    if (store(m, ns, key, type, val, n) != 0) return OMNI_KV_ERR;
    if (m->sets_ok < KV_MEM_LOG) snprintf(m->log[m->sets_ok], sizeof m->log[0], "%s/%s", ns, key);
    m->sets_ok++;
    return OMNI_KV_OK;
}

static omni_kv_rc_t set_blob(void *ctx, const char *ns, const char *key, const uint8_t *buf, size_t n)
{
    return set_any(ctx, ns, key, KV_T_BLOB, buf, n);
}

static omni_kv_rc_t set_str(void *ctx, const char *ns, const char *key, const char *s)
{
    return set_any(ctx, ns, key, KV_T_STR, (const uint8_t *)s, strlen(s));
}

omni_kv_t kv_mem_iface(kv_mem_t *m)
{
    return (omni_kv_t){ .ctx = m, .get_blob = get_blob, .set_blob = set_blob, .get_str = get_str, .set_str = set_str };
}

bool kv_mem_equal(const kv_mem_t *a, const kv_mem_t *b)
{
    if (a->count != b->count) return false;
    for (int i = 0; i < a->count; i++) {
        const kv_entry_t *x = &a->e[i];
        const kv_entry_t *y = kv_mem_find(b, x->ns, x->key);
        if (y == NULL || x->type != y->type || x->n != y->n) return false;
        if (x->n && memcmp(x->val, y->val, x->n) != 0) return false;
    }
    return true;
}

/* ------------------------------------------------------------ the line format */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;   /* upper case is not canonical: the writer would change the line */
}

/* Splits off the next space-separated token (in place); NULL when there is none. */
static char *token(char **p)
{
    while (**p == ' ') (*p)++;
    if (**p == '\0') return NULL;
    char *t = *p;
    while (**p != ' ' && **p != '\0') (*p)++;
    if (**p == ' ') *(*p)++ = '\0';
    return t;
}

static int parse_line(kv_mem_t *m, const char *line, int lineno)
{
    char *copy = strdup(line);
    if (copy == NULL) return -1;
    char *p = copy;
    char *ns = token(&p), *key = token(&p), *type = token(&p), *hex = token(&p);
    int rc = -1;
    uint8_t *val = NULL;
    if (hex == NULL || token(&p) != NULL) {
        fprintf(stderr, "kv line %d: expected <ns> <key> <type> <hex>\n", lineno);
        goto out;
    }
    int t = -1;
    for (int i = 0; i < KV_T__COUNT; i++)
        if (strcmp(type, k_type_name[i]) == 0) t = i;
    if (t < 0) { fprintf(stderr, "kv line %d: unknown type %s\n", lineno, type); goto out; }
    size_t hl = strcmp(hex, "-") == 0 ? 0 : strlen(hex);
    if (hl % 2 != 0) { fprintf(stderr, "kv line %d: odd hex length\n", lineno); goto out; }
    if (k_type_width[t] && hl / 2 != k_type_width[t]) {
        fprintf(stderr, "kv line %d: %s needs %u bytes\n", lineno, type, (unsigned)k_type_width[t]);
        goto out;
    }
    val = malloc(hl / 2 + 1);
    if (val == NULL) goto out;
    for (size_t i = 0; i < hl / 2; i++) {
        int h = hexval(hex[2 * i]), l = hexval(hex[2 * i + 1]);
        if (h < 0 || l < 0) { fprintf(stderr, "kv line %d: bad hex digit\n", lineno); goto out; }
        val[i] = (uint8_t)(h << 4 | l);
    }
    if (kv_mem_find(m, ns, key) != NULL) { fprintf(stderr, "kv line %d: duplicate %s/%s\n", lineno, ns, key); goto out; }
    if (store(m, ns, key, (kv_type_t)t, val, hl / 2) != 0) {
        fprintf(stderr, "kv line %d: name too long or store full\n", lineno);
        goto out;
    }
    kv_entry_t *e = find(m, ns, key);
    e->line = strdup(line);
    rc = e->line == NULL ? -1 : 0;
out:
    free(val);
    free(copy);
    return rc;
}

int kv_mem_load_lines(kv_mem_t *m, FILE *f)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int lineno = 0, rc = 0;
    while (rc == 0 && (len = getline(&line, &cap, f)) >= 0) {
        lineno++;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;
        rc = parse_line(m, line, lineno);
    }
    free(line);
    return rc;
}

int kv_mem_save_lines(const kv_mem_t *m, FILE *f)
{
    for (int i = 0; i < m->count; i++) {
        const kv_entry_t *e = &m->e[i];
        if (e->line != NULL) {
            if (fprintf(f, "%s\n", e->line) < 0) return -1;
            continue;
        }
        if (fprintf(f, "%s %s %s ", e->ns, e->key, k_type_name[e->type]) < 0) return -1;
        if (e->n == 0 && fputc('-', f) == EOF) return -1;
        for (size_t j = 0; j < e->n; j++)
            if (fprintf(f, "%02x", e->val[j]) < 0) return -1;
        if (fputc('\n', f) == EOF) return -1;
    }
    return ferror(f) ? -1 : 0;
}
