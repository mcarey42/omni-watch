#include "assets.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_mmap_assets.h"
#include "theme.h"
#include "watch_theme.h"

static const char *TAG = "assets";
#define PARTITION "assets"

static mmap_assets_handle_t s_h;
static int s_n;

esp_err_t assets_init(void)
{
    if (s_h) return ESP_OK;
    size_t internal0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    /* Prove the mapping first: when esp_partition_mmap fails, esp_mmap_assets 2.0.1 munmaps an uninitialised
     * handle (tens of thousands of esp_mmu_unmap errors and a task-watchdog storm at boot). The probe maps the
     * same range the library will, then lets it go. */
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION);
    if (part == NULL) {
        ESP_LOGE(TAG, "no partition \"%s\"", PARTITION);
        return ESP_ERR_NOT_FOUND;
    }
    const void *probe;
    esp_partition_mmap_handle_t ph;
    esp_err_t err = esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &probe, &ph);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "partition \"%s\" (0x%lx + %lu KB) cannot be mapped: %s", PARTITION, (unsigned long)part->address,
                 (unsigned long)(part->size / 1024), esp_err_to_name(err));
        return err;
    }
    esp_partition_munmap(ph);
    /* The whole partition is mapped (12 224 KB: 191 of the data MMU's 64 KB pages, next to PSRAM's; R69). app_bin_check: the file count
     * and table checksum must match the ones this app was built with; metadata_check: each file's marker. Both
     * read only the table, so boot stays fast; the full payload checksum is the console's `assets`. */
    const mmap_assets_config_t cfg = {
        .partition_label = PARTITION,
        .max_files = watch_theme()->assets_files,     /* the app's MMAP_ASSETS_FILES / _CHECKSUM (its build's */
        .checksum = watch_theme()->assets_checksum,   /* mmap_generate_assets.h), through the descriptor */
        .flags = {.mmap_enable = 1, .app_bin_check = 1, .metadata_check = 1},
    };
    mmap_assets_handle_t h = NULL;
    err = mmap_assets_new(&cfg, &h);
    if (err != ESP_OK) {
        if (h) mmap_assets_del(h); /* a count/checksum mismatch still returns a handle */
        ESP_LOGE(TAG, "partition \"%s\" not mapped: %s", PARTITION, esp_err_to_name(err));
        return err;
    }
    s_h = h;
    s_n = mmap_assets_get_stored_files(h);
    ESP_LOGI(TAG, "%d files mapped (%lu KB; internal RAM %d B)", s_n, (unsigned long)(part->size / 1024),
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) - (int)internal0);
    return ESP_OK;
}

bool assets_ready(void) { return s_h != NULL; }

int assets_count(void) { return s_h ? s_n : 0; }

const char *assets_entry(int i, const void **data, size_t *size)
{
    if (!s_h || i < 0 || i >= s_n) return NULL;
    if (data) *data = mmap_assets_get_mem(s_h, i);
    if (size) *size = (size_t)mmap_assets_get_size(s_h, i);
    return mmap_assets_get_name(s_h, i);
}

const void *assets_raw(const char *name, size_t *size)
{
    for (int i = 0; i < assets_count(); i++) {
        const void *p;
        const char *n = assets_entry(i, &p, size);
        if (strcmp(n, name) == 0) return p;
    }
    if (size) *size = 0;
    return NULL;
}

bool assets_image(const char *name, lv_image_dsc_t *out)
{
    size_t size;
    const uint8_t *p = assets_raw(name, &size);
    lv_image_header_t hd;
    if (p == NULL || size < sizeof hd) return false;
    memcpy(&hd, p, sizeof hd);
    uint32_t need = (uint32_t)hd.stride * hd.h;
    if (hd.cf == LV_COLOR_FORMAT_RGB565A8) need += (uint32_t)hd.w * hd.h; /* the A8 plane follows */
    if (hd.magic != LV_IMAGE_HEADER_MAGIC || hd.w == 0 || hd.h == 0 ||
        hd.stride < lv_draw_buf_width_to_stride(hd.w, hd.cf) || size - sizeof hd < need) {
        ESP_LOGE(TAG, "%s: not an LVGL image", name);
        return false;
    }
    memset(out, 0, sizeof *out);
    out->header = hd;
    out->data = p + sizeof hd;
    out->data_size = need;
    return true;
}

lv_draw_buf_t *assets_image_psram(const char *name)
{
    lv_image_dsc_t d;
    if (!assets_image(name, &d)) return NULL;
    lv_draw_buf_t *b = theme_buf_create(d.header.w, d.header.h, d.header.cf);
    if (b == NULL) return NULL;
    if (b->header.stride != d.header.stride || b->data_size < d.data_size) { /* tools/assets.sh packs stride = w * bpp */
        ESP_LOGE(TAG, "%s: stride %u, buffer wants %u", name, (unsigned)d.header.stride, (unsigned)b->header.stride);
        theme_buf_destroy(b);
        return NULL;
    }
    memcpy(b->data, d.data, d.data_size);
    return b;
}

/* The packer's checksum (esp_mmap_assets spiffs_assets_gen.py): the byte sum, mod 2^16, of everything after the
 * 32-byte header (the table and every file). Read with esp_partition_read, so it checks the flash itself. */
bool assets_checksum_ok(void)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, PARTITION);
    struct { char magic[4]; uint32_t version, name_len, files, checksum, payload_len, reserved[2]; } hd;
    _Static_assert(sizeof hd == 32, "esp_mmap_assets header is 32 bytes");
    if (part == NULL || esp_partition_read(part, 0, &hd, sizeof hd) != ESP_OK || memcmp(hd.magic, "MMAP", 4) != 0 ||
        hd.files != watch_theme()->assets_files || hd.checksum != watch_theme()->assets_checksum || hd.payload_len > part->size - sizeof hd)
        return false;
    uint8_t buf[1024];
    uint32_t sum = 0;
    for (uint32_t off = 0; off < hd.payload_len; off += sizeof buf) {
        uint32_t n = hd.payload_len - off < sizeof buf ? hd.payload_len - off : sizeof buf;
        if (esp_partition_read(part, sizeof hd + off, buf, n) != ESP_OK) return false;
        for (uint32_t i = 0; i < n; i++) sum += buf[i];
    }
    return (sum & 0xFFFF) == hd.checksum;
}
