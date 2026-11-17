#include "store.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "omni/boot.h"
#include "omni/cities.h"
#include "omni/migrate.h"
#include "omni/timefmt.h"
#include "power.h"
#include "omni_events.h"

static const char *TAG = "store";
#define COMMIT_DEBOUNCE_US 2000000
#define SAVE_RETRY_US 200000      /* the event queue was full: post the save again this much later */
#define NVS_ENTRY_B 32            /* one NVS entry; nvs_get_used_entry_count is the only per-namespace measure */

/* ------------------------------------------------------------ the NVS kv adapter (omni_kv_t, R124 item 3) */
static esp_err_t s_kv_err = ESP_OK;   /* the last NVS error a kv call saw (not NOT_FOUND), for store_migration */

/* ESP_OK -> OK; ESP_ERR_NVS_NOT_FOUND (an absent key, or from nvs_open an absent namespace: `watch` and `t_<id>` on a
 * watch that never ran this firmware) -> NOT_FOUND; anything else (a value over the buffer, ESP_ERR_NVS_INVALID_LENGTH;
 * another type, ESP_ERR_NVS_TYPE_MISMATCH; a flash error) -> ERR. */
static omni_kv_rc_t kv_rc(esp_err_t e)
{
    if (e == ESP_OK) return OMNI_KV_OK;
    if (e == ESP_ERR_NVS_NOT_FOUND) return OMNI_KV_NOT_FOUND;
    s_kv_err = e;
    return OMNI_KV_ERR;
}

/* A typed get of a key stored with another type returns ESP_ERR_NVS_NOT_FOUND on this IDF, not TYPE_MISMATCH:
 * Page::findItem returns TYPE_MISMATCH (v5.5.6 nvs_page.cpp:1018-1023), but Storage::findItem keeps only ESP_OK and
 * returns NOT_FOUND for anything else (nvs_storage.cpp:257-271). Absent means migrate-and-write, so a NOT_FOUND
 * from a get is confirmed with nvs_find_key (ItemType::ANY, nvs_storage.cpp:943): a key that exists under another
 * type is TYPE_MISMATCH, an ERR, and the record goes read-only instead of being written over (review m5). */
static esp_err_t typed_get_rc(nvs_handle_t h, const char *key, esp_err_t e)
{
    if (e != ESP_ERR_NVS_NOT_FOUND) return e;
    nvs_type_t t;
    return nvs_find_key(h, key, &t) == ESP_OK ? ESP_ERR_NVS_TYPE_MISMATCH : e;
}

static omni_kv_rc_t kv_get_blob(void *ctx, const char *ns, const char *key, uint8_t *buf, size_t *n)
{
    (void)ctx;
    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READONLY, &h);
    if (e == ESP_OK) {
        e = typed_get_rc(h, key, nvs_get_blob(h, key, buf, n));
        nvs_close(h);
    }
    return kv_rc(e);
}

static omni_kv_rc_t kv_get_str(void *ctx, const char *ns, const char *key, char *buf, size_t cap)
{
    (void)ctx;
    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READONLY, &h);
    if (e == ESP_OK) {
        size_t n = cap;
        e = typed_get_rc(h, key, nvs_get_str(h, key, buf, &n));
        nvs_close(h);
    }
    return kv_rc(e);
}

/* The real guard on namespace `omni` (preflight C1): it stays the older firmwares' rollback source and is only ever
 * read. A set naming it is refused before any nvs_open. */
static bool refuse_old(const char *ns, const char *key)
{
    if (strcmp(ns, OMNI_NS_OLD) != 0) return false;
    ESP_LOGE(TAG, "refused: a write to namespace %s (key %s); it is read-only", ns, key);
    s_kv_err = ESP_ERR_NOT_ALLOWED;
    return true;
}

static omni_kv_rc_t kv_set_blob(void *ctx, const char *ns, const char *key, const uint8_t *buf, size_t n)
{
    (void)ctx;
    if (refuse_old(ns, key)) return OMNI_KV_ERR;
    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, key, buf, n);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e == ESP_OK) return OMNI_KV_OK;
    s_kv_err = e;
    return OMNI_KV_ERR;   /* NOT_FOUND here is a failure too */
}

static omni_kv_rc_t kv_set_str(void *ctx, const char *ns, const char *key, const char *s)
{
    (void)ctx;
    if (refuse_old(ns, key)) return OMNI_KV_ERR;
    nvs_handle_t h;
    esp_err_t e = nvs_open(ns, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_str(h, key, s);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e == ESP_OK) return OMNI_KV_OK;
    s_kv_err = e;
    return OMNI_KV_ERR;
}

static const omni_kv_t KV = {
    .ctx = NULL, .get_blob = kv_get_blob, .set_blob = kv_set_blob, .get_str = kv_get_str, .set_str = kv_set_str,
};

/* ------------------------------------------------------------ boot state */
static omni_boot_t *s_b;              /* PSRAM (about 1.1 KB, kept off internal RAM and the boot task's stack) */
static omni_settings_t s_settings;    /* the RAM view the UI edits (settings_edit) and the saves write */
static esp_timer_handle_t s_save_timer;
static const char *s_id = "";
static const omni_theme_limits_t *s_lim;
static bool s_legacy;
static char s_origin[64] = "defaults (NVS not read)";
static char s_migration[64] = "not run";
/* s_origin and s_migration are rewritten on the event loop (a force) and read by the console's `state` (review m4):
 * both sides copy under this lock. The theme's extension (s_b->lo.tail) is read and replaced under it too. */
static portMUX_TYPE s_str_lock = portMUX_INITIALIZER_UNLOCKED;

static bool no_write(void) { return s_b == NULL || s_b->no_write; }

/* The two `state` strings from the boot state; a failure names the last NVS error. Composed outside the lock, then
 * copied under it. */
static void describe(void)
{
    if (s_b == NULL) return;
    char origin[sizeof s_origin], mig[sizeof s_migration];
    omni_boot_origin(s_b, origin, sizeof origin);
    omni_boot_migration(s_b, mig, sizeof mig);
    if (strstr(mig, "failed") && s_kv_err != ESP_OK) {
        size_t n = strlen(mig);
        snprintf(mig + n, sizeof mig - n, " %s", esp_err_to_name(s_kv_err));
    }
    taskENTER_CRITICAL(&s_str_lock);
    memcpy(s_origin, origin, sizeof s_origin);
    memcpy(s_migration, mig, sizeof s_migration);
    taskEXIT_CRITICAL(&s_str_lock);
}

void store_status(char *origin, size_t origin_cap, char *migration, size_t migration_cap)
{
    char o[sizeof s_origin], m[sizeof s_migration];
    taskENTER_CRITICAL(&s_str_lock);
    memcpy(o, s_origin, sizeof o);
    memcpy(m, s_migration, sizeof m);
    taskEXIT_CRITICAL(&s_str_lock);
    if (origin_cap) snprintf(origin, origin_cap, "%s", o);
    if (migration_cap) snprintf(migration, migration_cap, "%s", m);
}

/* A device fact (clockcal, batrec) from `watch`. On a read-only boot (a failed migration) a fact not yet copied is
 * read from `omni`, read only, so the clock and the battery session still start from what the watch learned.
 * *writable: whether watch/<key> is this build's to write (omni_fact_writable: absent, or exactly this build's size
 * and version); a newer or unreadable fact is read-only for the boot, like the records (final review m3). The gets
 * confirm NOT_FOUND with typed_get_rc, so a key under another type is an error, never "absent". */
static esp_err_t fact_load(const char *key, uint8_t *blob, size_t *n, uint16_t version, bool *writable)
{
    const size_t cap = *n;
    esp_err_t err = ESP_ERR_NVS_NOT_FOUND;
    nvs_handle_t h;
    esp_err_t oe = nvs_open(OMNI_NS_WATCH, NVS_READONLY, &h);
    if (oe == ESP_OK) {
        err = typed_get_rc(h, key, nvs_get_blob(h, key, blob, n));
        nvs_close(h);
    } else if (oe != ESP_ERR_NVS_NOT_FOUND) {
        err = oe;
    }
    const omni_kv_rc_t rc = err == ESP_OK ? OMNI_KV_OK : err == ESP_ERR_NVS_NOT_FOUND ? OMNI_KV_NOT_FOUND : OMNI_KV_ERR;
    *writable = omni_fact_writable(rc, blob, err == ESP_OK ? *n : 0, cap, version);
    if (err == ESP_ERR_NVS_NOT_FOUND && s_b != NULL && s_b->no_write && !s_b->nvs_down &&
        nvs_open(OMNI_NS_OLD, NVS_READONLY, &h) == ESP_OK) {
        *n = cap;
        err = typed_get_rc(h, key, nvs_get_blob(h, key, blob, n));
        nvs_close(h);
    }
    return err;
}

static bool s_clk_writable = true, s_bat_writable = true;   /* set by the loads, before any save */

static esp_err_t fact_save(const char *key, const uint8_t *blob, size_t n, bool writable)
{
    if (no_write()) {
        ESP_LOGW(TAG, "%s save skipped: read-only boot (%s)", key, s_origin);
        return ESP_OK;
    }
    if (!writable) {
        ESP_LOGW(TAG, "%s save skipped: watch/%s is not this build's format (read-only)", key, key);
        return ESP_OK;
    }
    return kv_set_blob(NULL, OMNI_NS_WATCH, key, blob, n) == OMNI_KV_OK ? ESP_OK : s_kv_err;
}

esp_err_t store_batrec_load(omni_batrec_t *r)
{
    omni_batrec_init(r);
    uint8_t blob[OMNI_BATREC_BLOB_SIZE];
    size_t n = sizeof blob;
    esp_err_t err = fact_load(OMNI_KEY_BATREC, blob, &n, OMNI_BATREC_VERSION, &s_bat_writable);
    if (err == ESP_OK && !omni_batrec_decode(r, blob, n))
        ESP_LOGW(TAG, "stored battery record unusable (size %u): empty%s", (unsigned)n,
                 s_bat_writable ? "" : ", kept (read-only)");
    else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGW(TAG, "battery record unreadable (%s): empty, kept (read-only)", esp_err_to_name(err));
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t store_batrec_save(const omni_batrec_t *r)
{
    uint8_t blob[OMNI_BATREC_BLOB_SIZE];
    omni_batrec_encode(r, blob);
    return fact_save(OMNI_KEY_BATREC, blob, sizeof blob, s_bat_writable);
}

esp_err_t store_clock_load(omni_clockcal_t *c)
{
    omni_clk_init(c);
    uint8_t blob[OMNI_CLK_BLOB_SIZE];
    size_t n = sizeof blob;
    esp_err_t err = fact_load(OMNI_KEY_CLOCKCAL, blob, &n, OMNI_CLK_BLOB_VERSION, &s_clk_writable);
    uint16_t ver;
    if (err == ESP_OK && n == OMNI_CLK_BLOB_SIZE && (memcpy(&ver, blob, sizeof ver), ver == OMNI_CLK_BLOB_VERSION)) {
        omni_clk_saved_t saved;
        memcpy(&saved, blob + sizeof ver, sizeof saved);
        if (!omni_clk_restore(c, &saved)) ESP_LOGW(TAG, "stored clock calibration out of range: defaults");
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "stored clock calibration unusable (size %u): defaults%s", (unsigned)n,
                 s_clk_writable ? "" : ", kept (read-only)");
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "clock calibration unreadable (%s): defaults, kept (read-only)", esp_err_to_name(err));
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t store_clock_save(const omni_clockcal_t *c)
{
    uint8_t blob[OMNI_CLK_BLOB_SIZE];
    const uint16_t ver = OMNI_CLK_BLOB_VERSION;
    memcpy(blob, &ver, sizeof ver);
    memcpy(blob + sizeof ver, &c->saved, sizeof c->saved);
    return fact_save(OMNI_KEY_CLOCKCAL, blob, sizeof blob, s_clk_writable);
}

/* ------------------------------------------------------------ settings */
/* The boot policy over NVS (omni/boot.h): watch/theme read first, the migration, the records, the theme id. */
static void store_load(bool nvs_down)
{
    s_kv_err = ESP_OK;
    omni_boot_run(s_b, &KV, s_id, s_lim, s_legacy, nvs_down);
    s_settings = s_b->s;
    describe();
}

/* Both records, each written only if its bytes changed; nothing on a read-only boot. */
static esp_err_t store_save(void)
{
    if (s_b == NULL) return ESP_ERR_INVALID_STATE;
    s_kv_err = ESP_OK;
    omni_kv_rc_t rc = omni_boot_save(s_b, &KV, &s_settings);
    if (s_b->no_write) return ESP_ERR_INVALID_STATE;
    return rc == OMNI_KV_OK ? ESP_OK : (s_kv_err != ESP_OK ? s_kv_err : ESP_FAIL);
}

static void apply(const omni_settings_t *s)
{
    const omni_city_t *c = omni_city_at(s->city);
    if (c != NULL) omni_tz_apply(c->posix_tz);
    power_set_brightness(s->brightness);
}

/* esp_timer task: the save itself runs on the event loop (R27: it alone touches the settings), on EV_SAVE. */
static void save_cb(void *arg)
{
    (void)arg;
    omni_event_t e = {.kind = EV_SAVE, .t_ms = (uint32_t)(esp_timer_get_time() / 1000)};
    if (!omni_events_post(&e)) esp_timer_start_once(s_save_timer, SAVE_RETRY_US);
}

void store_settings_save_now(void)
{
    esp_err_t err = store_save();
    if (err == ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "save skipped: read-only boot (%s)", s_origin);
    else if (err != ESP_OK) ESP_LOGE(TAG, "save: %s", esp_err_to_name(err));
    else if (s_b->shared_ro || s_b->local_ro) ESP_LOGW(TAG, "saved; kept in RAM only: %s", s_origin);
    else ESP_LOGI(TAG, "saved");
}

static void arm_save(void)
{
    if (s_save_timer == NULL) return;
    esp_timer_stop(s_save_timer); /* restart the debounce; ESP_ERR_INVALID_STATE if idle is fine */
    esp_timer_start_once(s_save_timer, COMMIT_DEBOUNCE_US);
}

/* No RAM for the boot state: the defaults, read-only (no_write() holds while s_b is NULL). Out of line: its two
 * records (~0.5 KB) stay off store_init's frame on the small boot stack. */
static __attribute__((noinline)) void defaults_view(const omni_theme_limits_t *lim)
{
    ESP_LOGE(TAG, "no memory for the boot state: read-only boot on the defaults");
    omni_shared_t sh;
    omni_local_t lo;
    omni_shared_defaults(&sh);
    omni_local_defaults(&lo, lim);
    omni_settings_join(&sh, &lo, &s_settings);
}

esp_err_t store_init(const char *id, const omni_theme_limits_t *lim, bool omni_legacy)
{
    s_id = id;
    s_lim = lim;
    s_legacy = omni_legacy;
    /* Never erased (Stage 4b): a full or newer-format NVS boots read-only on the defaults instead of losing the
     * owner's settings and Wi-Fi; the older firmwares and a reflash of the same IDF can still read it. */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "nvs_flash_init: %s: read-only boot on the defaults (NVS kept)", esp_err_to_name(err));
    s_b = heap_caps_calloc(1, sizeof *s_b, MALLOC_CAP_SPIRAM);
    if (s_b == NULL) s_b = calloc(1, sizeof *s_b);
    if (s_b != NULL) {
        store_load(err != ESP_OK);
        ESP_LOGI(TAG, "migration: %s", s_migration);
        ESP_LOGI(TAG, "settings: %s", s_origin);
        if (!s_b->nvs_down) {
            char line[48];
            ESP_LOGI(TAG, "%s", omni_boot_theme_line(s_b, id, line, sizeof line));
            if (s_b->theme_written && s_b->theme_rc != OMNI_KV_OK)
                ESP_LOGE(TAG, "watch/theme write: %s", esp_err_to_name(s_kv_err));
        }
        if (s_b->no_write) ESP_LOGW(TAG, "read-only boot: nothing is written until a reboot");
    } else {
        defaults_view(lim);
    }
    const esp_timer_create_args_t args = {.callback = save_cb, .name = "settings_save"};
    if (esp_timer_create(&args, &s_save_timer) != ESP_OK) s_save_timer = NULL;
    apply(&s_settings);
    return err;
}

const omni_settings_t *settings(void) { return &s_settings; }
omni_settings_t *settings_edit(void) { return &s_settings; }
const char *store_settings_origin(void) { return s_origin; }
bool store_read_only(void) { return no_write(); }
const char *store_migration(void) { return s_migration; }
const char *store_prev_theme(void) { return s_b ? s_b->prev : ""; }
bool store_theme_swapped(void) { return s_b != NULL && omni_boot_swapped(s_b, s_id); }
uint8_t store_face(void) { return s_b ? omni_boot_face(s_b) : 0; }

void store_face_commit(uint8_t idx)
{
    if (s_b == NULL) return;
    s_b->lo.face_idx = idx;
    arm_save();
}

_Static_assert(STORE_LOCAL_EXT_MAX == OMNI_BOOT_TAIL_MAX, "store and core agree on the tail cap");

size_t store_local_ext(uint8_t *buf, size_t cap)
{
    if (s_b == NULL) return 0;
    taskENTER_CRITICAL(&s_str_lock);
    const size_t n = omni_boot_local_tail(s_b, buf, cap);
    taskEXIT_CRITICAL(&s_str_lock);
    return n;
}

bool store_local_writable(void) { return s_b != NULL && omni_boot_local_writable(s_b); }

omni_tail_rc_t store_local_ext_commit(const uint8_t *buf, size_t n)
{
    if (s_b == NULL) return OMNI_TAIL_READ_ONLY;
    taskENTER_CRITICAL(&s_str_lock);
    const omni_tail_rc_t rc = omni_boot_commit_local_tail(s_b, buf, n);
    taskEXIT_CRITICAL(&s_str_lock);
    if (rc == OMNI_TAIL_BAD_LEN) {
        ESP_LOGE(TAG, "theme extension refused: %u B for %s/local (max %u, never emptied)", (unsigned)n, s_b->ns_local,
                 (unsigned)STORE_LOCAL_EXT_MAX);
        return rc;
    }
    arm_save();   /* a read-only boot or record: store_save skips it, and the save logs that */
    return rc;
}

void settings_commit(void)
{
    if (s_b != NULL) omni_boot_validate(s_b, &s_settings);
    else omni_settings_validate(&s_settings);
    apply(&s_settings);
    arm_save();
}

static size_t ns_bytes(const char *ns)
{
    nvs_handle_t h;
    size_t n = 0;
    if (nvs_open(ns, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_used_entry_count(h, &n) != ESP_OK) n = 0;
        nvs_close(h);
    }
    return n * NVS_ENTRY_B;
}

esp_err_t store_nvs_stats(store_nvs_stats_t *out)
{
    nvs_stats_t st;
    esp_err_t err = nvs_get_stats(NULL, &st);
    if (err != ESP_OK) return err;
    out->used_entries = st.used_entries;
    out->free_entries = st.free_entries;
    out->total_entries = st.total_entries;
    out->watch_b = ns_bytes(OMNI_NS_WATCH);
    out->theme_b = s_b ? ns_bytes(s_b->ns_local) : 0;
    out->old_b = ns_bytes(OMNI_NS_OLD);
    return ESP_OK;
}

esp_err_t store_migrate_force(void)
{
    if (no_write()) {
        ESP_LOGW(TAG, "migrate force refused: read-only boot (a reboot retries the migration)");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_b->shared_ro || s_b->local_ro) {
        ESP_LOGW(TAG, "migrate force refused: %s; a newer record is never overwritten", s_origin);
        return ESP_ERR_NOT_ALLOWED;
    }
    /* only now, once force will run (review m1): a refused force keeps a pending save of the writable record */
    if (s_save_timer) esp_timer_stop(s_save_timer);   /* a pending save would write the old view back */
    s_kv_err = ESP_OK;
    omni_kv_rc_t rc = omni_boot_force(s_b, &KV, s_id, s_lim, s_legacy);
    s_settings = s_b->s;
    describe();
    apply(&s_settings);
    ESP_LOGI(TAG, "migrate force: %s; settings %s", s_migration, s_origin);
    return rc == OMNI_KV_OK ? ESP_OK : ESP_FAIL;
}
