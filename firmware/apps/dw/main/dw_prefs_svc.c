/* dw_prefs_svc (dw_prefs_svc.h). The prefs live in s_prefs, written only on the boot task (after_store, before the
 * event loop starts) and then on the event loop (the rows' apply, after_store again after a migrate force, F3); every
 * change is published as one 32-bit word (dw_prefs_pack) for the other tasks: the Settings screen's get, the chime
 * timer (B3). The extension is committed through store_local_ext_commit (the 2 s debounced save, R27). */
#include "dw_prefs_svc.h"
#include "fk_chime_svc.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "store.h"

static const char *TAG = "dw_prefs";
static dw_prefs_t s_prefs;            /* boot task, then the event loop */
static volatile uint32_t s_snap;      /* dw_prefs_pack(&s_prefs): one aligned store, read by any task */
static volatile uint16_t s_offered;   /* s_prefs.offered, the same way (the console's `dw prefs`) */
_Static_assert(DW_CAROUSEL_MAX == OMNI_CAROUSEL_MAX, "dw_core's carousel is the store's");

static void publish(void)
{
    s_snap = dw_prefs_pack(&s_prefs);
    s_offered = s_prefs.offered;
}

uint8_t dw_prefs_now(dw_pref_field_t f) { return dw_prefs_packed(s_snap, f); }
uint16_t dw_prefs_svc_offered(void) { return s_offered; }

/* s_prefs into t_dw/local's tail. False when nothing was committed for the save to write. */
static bool commit(void)
{
    uint8_t buf[DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX];
    const size_t n = dw_prefs_encode(&s_prefs, buf, sizeof buf);
    if (n == 0) {   /* an extension this build does not know: never written over */
        ESP_LOGW(TAG, "prefs kept in RAM only: t_dw/local's extension is not this build's to write");
        return false;
    }
    switch (store_local_ext_commit(buf, n)) {
    case OMNI_TAIL_OK: return true;
    case OMNI_TAIL_BAD_LEN: ESP_LOGE(TAG, "prefs commit refused (%u B): nothing changed in NVS", (unsigned)n); return false;
    case OMNI_TAIL_READ_ONLY: ESP_LOGW(TAG, "prefs kept in RAM only: read-only boot or record"); return false;
    }
    return false;
}

void dw_prefs_after_store(void)
{
    uint8_t ext[STORE_LOCAL_EXT_MAX];
    const size_t n = store_local_ext(ext, sizeof ext);
    const dw_prefs_load_t r = dw_prefs_load(&s_prefs, ext, n, sizeof ext);   /* over-long: READ_ONLY */
    const bool writable = store_local_writable();
    const dw_prefs_boot_t d = fk_prefs_boot(writable, r);
    /* RAM_ONLY keeps the prefs as decoded (the defaults, with READ_ONLY's taint): never rebuilt from the defaults */
    if (d == DW_BOOT_RESET_COMMIT) {
        /* WD6: the stub's record (no extension): the wave's carousel, the first face, the Doors, the extension, saved
         * now (A6: the boot task, before the event loop; R27 allows boot-time store work) */
        omni_settings_t *s = settings_edit();
        memcpy(s->carousel, DW_DEF_CAROUSEL, DW_DEF_CAROUSEL_LEN);
        s->carousel_len = DW_DEF_CAROUSEL_LEN;
        s->reveal_style = 0;   /* "Doors" */
        store_face_commit(0);
    }
    /* WD24: every built face offered to the carousel once, appended at its end (the face index keeps its face). Only a
     * writable KEEP or RESET_COMMIT offers: a read-only boot, record or extension changes nothing, in RAM or NVS. */
    const uint8_t was_len = settings()->carousel_len;
    uint8_t car[OMNI_CAROUSEL_MAX];
    uint8_t len = was_len;
    memcpy(car, settings()->carousel, sizeof car);
    const bool offered = fk_prefs_boot_offer(writable, d) && dw_prefs_offer(&s_prefs, car, &len);
    if (offered) {
        omni_settings_t *s = settings_edit();
        memcpy(s->carousel, car, sizeof car);
        s->carousel_len = len;
    }
    publish();
    fk_chime_svc_sync();   /* the chime as the record says (the boot task; again after a migrate force, F3) */
    ESP_LOGI(TAG, "extension %u B: %s, %s; mode %u chime %u quiet %02u-%02u; offered %04x (%s, %u appended)",
             (unsigned)n,
             r == DW_PREFS_LOADED ? "loaded" : r == DW_PREFS_TAIL_KEPT ? "tail kept" : r == DW_PREFS_ABSENT ? "absent"
                                                                                                       : "read-only",
             d == DW_BOOT_KEEP ? "kept" : d == DW_BOOT_RESET_COMMIT ? "reset" : "RAM only", s_prefs.mode,
             s_prefs.chime_on, s_prefs.quiet_from_h, s_prefs.quiet_to_h, s_prefs.offered,
             offered ? "changed" : "unchanged", (unsigned)(len - was_len));
    if (d != DW_BOOT_RESET_COMMIT && !offered) return;   /* the second boot after an offer ends here: nothing written */
    commit();
    store_settings_save_now();   /* also brings the boot's view (store_face) up to the new carousel */
}

/* A Flip-open lift's mark for the face's wake that follows on this task (apply() calls the shell's wake straight
 * after lift_face): set when lift_face gives an index, taken once (WD7: the doors whatever the reveal style). */
static volatile int64_t s_lift_us;   /* esp_timer us of the mark; 0: none. Stale after LIFT_MS (no wake came) */
#define LIFT_MS 1000

int dw_prefs_lift_face(void)
{
    /* PR1: the face the store says is current (store_face: clamped, moved by every swipe and pick), its era picking
     * the home. The event loop, as apply() calls this; shell_nav() would need the LVGL lock. */
    const omni_settings_t *s = settings();
    const uint8_t fi = store_face();
    const uint8_t cur = fi < s->carousel_len ? s->carousel[fi] : DW_F_CRYSTAL;
    const int i = dw_lift_index(&s_prefs, s->carousel, s->carousel_len, cur);
    s_lift_us = i >= 0 ? esp_timer_get_time() : 0;
    return i;
}

bool dw_prefs_take_lift(void)
{
    const int64_t t = s_lift_us;
    s_lift_us = 0;
    return t != 0 && esp_timer_get_time() - t < LIFT_MS * 1000LL;
}

/* The rows' apply (event loop, UI_THEME_SET). */
static void set_field(dw_pref_field_t f, uint8_t v)
{
    if (dw_prefs_get(&s_prefs, f) == v || !dw_prefs_set(&s_prefs, f, v)) return;
    publish();   /* the chime timer reads the quiet window from the snapshot at each fire */
    commit();
    if (f != DW_PF_MODE) fk_chime_svc_sync();   /* the Chime row starts or stops the service (Task 16) */
}

static uint8_t get_mode(void) { return dw_prefs_now(DW_PF_MODE); }
static uint8_t get_chime(void) { return dw_prefs_now(DW_PF_CHIME); }
static uint8_t get_from(void) { return dw_prefs_now(DW_PF_QUIET_FROM); }
static uint8_t get_to(void) { return dw_prefs_now(DW_PF_QUIET_TO); }
static void set_mode(uint8_t v) { set_field(DW_PF_MODE, v); }
static void set_chime(uint8_t v) { set_field(DW_PF_CHIME, v); }
static void set_from(uint8_t v) { set_field(DW_PF_QUIET_FROM, v); }
static void set_to(uint8_t v) { set_field(DW_PF_QUIET_TO, v); }

#if CONFIG_OMNI_DEBUG_CONSOLE
static volatile uint32_t s_offer_verdict;   /* (seq << 8) | dw_offer_verdict_t: the event loop's, read by the console */

uint32_t dw_prefs_svc_offer_verdict(void) { return s_offer_verdict; }

/* `dw offer reset` (R147 PR2(b)) on the event loop: the offered bit of every built face the carousel lacks cleared and
 * the extension committed (the 2 s debounced save), so the next boot appends those faces again. Never on a no-write
 * boot or a read-only record (nothing could be saved), and a failed commit undoes the reset in RAM: the verdict is
 * what NVS will hold, published for the console. */
static void offer_reset(uint8_t v)
{
    if (v != DW_OFFER_RESET_GO) return;
    const omni_settings_t *s = settings();
    const dw_prefs_t was = s_prefs;
    const bool writable = store_local_writable();
    dw_offer_verdict_t vd = dw_prefs_offer_reset_verdict(&s_prefs, writable, s->carousel, s->carousel_len);
    if (vd == DW_OFFER_V_RESET) {
        if (commit()) {
            publish();
        } else {
            s_prefs = was;   /* nothing saved: the mask stays as NVS has it */
            vd = DW_OFFER_V_RAM_ONLY;
        }
    }
    ESP_LOGI(TAG, "offer reset: %s (offered %04x)",
             vd == DW_OFFER_V_RESET       ? "done; the next boot appends the missing faces"
             : vd == DW_OFFER_V_NOTHING   ? "nothing to reset (every built face is in the carousel or not yet offered)"
             : vd == DW_OFFER_V_READ_ONLY ? "refused: a no-write boot or a read-only record"
                                          : "refused: the commit failed; nothing changed",
             s_prefs.offered);
    s_offer_verdict = ((((s_offer_verdict >> 8) + 1) & 0xFFFFFFu) << 8) | (uint32_t)vd;
}
#endif

static const char *const MODE_O[] = {"Always on", "Flip open"}, *const MODE_S[] = {"ALWAYS", "FLIP"};
static const char *const CHIME_O[] = {"Off", "On"}, *const CHIME_S[] = {"OFF", "ON"};
_Static_assert(DW_MODE_ALWAYS_ON == 0 && DW_MODE_FLIP_OPEN == 1, "Mode's options are dw_mode_t's order");

const watch_setting_t DW_SETTINGS[DW_SETTINGS_ROWS] = {
    {.label = "Mode", .n = 2, .opts = MODE_O, .shorts = MODE_S, .get = get_mode, .apply = set_mode},
    {.label = "Chime", .n = 2, .opts = CHIME_O, .shorts = CHIME_S, .get = get_chime, .apply = set_chime},
    {.label = "Quiet from", .hours = true, .get = get_from, .apply = set_from},
    {.label = "Quiet until", .hours = true, .get = get_to, .apply = set_to},
#if CONFIG_OMNI_DEBUG_CONSOLE
    [DW_ROW_OFFER_RESET] = {.label = "offer reset", .apply = offer_reset},   /* no get: never shown */
#endif
};
_Static_assert(DW_SETTINGS_ROWS <= WATCH_SETTINGS_MAX, "the Settings screen sees every row");
