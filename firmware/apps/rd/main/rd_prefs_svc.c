/* rd_prefs_svc (rd_prefs_svc.h), dw_prefs_svc's shape. The prefs live in s_prefs, written only on the boot task
 * (after_store, before the event loop starts) and then on the event loop (the rows' apply, after_store again after a
 * migrate force, F3); every change is published as one 32-bit word (a byte per field) for the other tasks: the Settings
 * screen's get and the chime (Task 8). The extension is committed through store_local_ext_commit (the 2 s debounced
 * save, R27). The chime service (fk_chime_svc, with rd_chime.c's plan) follows the record: synced after every publish
 * at boot and on every Chime and Quiet row change, so the Chime row never shows On with no timer behind it. */
#include "rd_prefs_svc.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "fk_chime_svc.h"
#include "store.h"
#include "rd/faces.h"

static const char *TAG = "rd_prefs";
static rd_prefs_t s_prefs;            /* boot task, then the event loop */
static volatile uint32_t s_snap;      /* the fields, a byte each by rd_pref_field_t: one aligned store */
static volatile uint16_t s_offered;   /* s_prefs.offered, the same way (the console's `rd prefs`) */
_Static_assert(RD_CAROUSEL_MAX == OMNI_CAROUSEL_MAX, "rd_core's carousel is the store's");
_Static_assert(RD_PF__COUNT <= 4, "the snapshot holds a byte per field");

static void publish(void)
{
    uint32_t w = 0;
    for (int f = 0; f < RD_PF__COUNT; f++) w |= (uint32_t)rd_prefs_get(&s_prefs, (rd_pref_field_t)f) << (8 * f);
    s_snap = w;
    s_offered = s_prefs.offered;
}

uint8_t rd_prefs_now(rd_pref_field_t f) { return (unsigned)f < RD_PF__COUNT ? (uint8_t)(s_snap >> (8 * f)) : 0; }
uint16_t rd_prefs_svc_offered(void) { return s_offered; }

/* s_prefs into t_rd/local's tail. False when nothing was committed for the save to write. */
static bool commit(void)
{
    uint8_t buf[RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX];
    const size_t n = rd_prefs_encode(&s_prefs, buf, sizeof buf);
    if (n == 0) {   /* an extension this build does not know: never written over */
        ESP_LOGW(TAG, "prefs kept in RAM only: t_rd/local's extension is not this build's to write");
        return false;
    }
    switch (store_local_ext_commit(buf, n)) {
    case OMNI_TAIL_OK: return true;
    case OMNI_TAIL_BAD_LEN: ESP_LOGE(TAG, "prefs commit refused (%u B): nothing changed in NVS", (unsigned)n); return false;
    case OMNI_TAIL_READ_ONLY: ESP_LOGW(TAG, "prefs kept in RAM only: read-only boot or record"); return false;
    }
    return false;
}

void rd_prefs_after_store(void)
{
    uint8_t ext[STORE_LOCAL_EXT_MAX];
    const size_t n = store_local_ext(ext, sizeof ext);
    const fk_load_t r = rd_prefs_load(&s_prefs, ext, n, sizeof ext);   /* over-long: READ_ONLY */
    const bool writable = store_local_writable();
    /* the decision is rd_core's, pure (rd_prefs_after_store_decide; test_rd_prefs.c runs the same function): the boot
     * verdict, on RESET_COMMIT the default carousel (the first rd boot on a watch, spec section 5), and on a writable
     * KEEP or RESET_COMMIT every listed face offered to the carousel once, appended at its end (the face index keeps its
     * face). A read-only boot, record or extension changes nothing, in RAM or NVS; RAM_ONLY keeps the prefs as decoded
     * (the defaults, with READ_ONLY's taint). */
    const uint8_t was_len = settings()->carousel_len;
    uint8_t car[OMNI_CAROUSEL_MAX];
    uint8_t len = was_len;
    memcpy(car, settings()->carousel, sizeof car);
    const rd_after_store_t a = rd_prefs_after_store_decide(&s_prefs, r, writable, car, &len);
    const fk_boot_t d = a.d;
    const bool offered = a.offered;
    if (a.save) {
        /* the boot task, before the event loop; R27 allows boot-time store work */
        omni_settings_t *s = settings_edit();
        memcpy(s->carousel, car, sizeof car);
        s->carousel_len = len;
        if (a.reset) {
            s->reveal_style = 0;   /* "Full" */
            store_face_commit(0);
        }
    }
    publish();
    fk_chime_svc_sync();   /* the chime as the record says (the boot task; again after a migrate force, F3) */
    ESP_LOGI(TAG, "extension %u B: %s, %s; mode %u chime %u quiet %02u-%02u; offered %04x (%s, %u appended)",
             (unsigned)n,
             r == FK_LOAD_LOADED ? "loaded" : r == FK_LOAD_TAIL_KEPT ? "tail kept" : r == FK_LOAD_ABSENT ? "absent"
                                                                                                    : "read-only",
             d == FK_BOOT_KEEP ? "kept" : d == FK_BOOT_RESET_COMMIT ? "reset" : "RAM only", s_prefs.mode,
             s_prefs.chime_on, s_prefs.quiet_from_h, s_prefs.quiet_to_h, s_prefs.offered,
             offered ? "changed" : "unchanged", (unsigned)(len - (a.reset ? RD_DEF_CAROUSEL_LEN : was_len)));
    if (!a.save) return;   /* the second boot ends here: nothing written */
    commit();
    store_settings_save_now();   /* also brings the boot's view (store_face) up to the new carousel */
}

/* A Stasis lift's mark for the face's wake that follows on this task (apply() calls the shell's wake straight after
 * lift_face): set when lift_face gives an index, taken once. */
static volatile int64_t s_lift_us;   /* esp_timer us of the mark; 0: none. Stale after LIFT_MS (no wake came) */
#define LIFT_MS 1000

int rd_prefs_lift_face(void)
{
    /* the event loop, as apply() calls this; shell_nav() would need the LVGL lock */
    const omni_settings_t *s = settings();
    const int i = rd_lift_index(&s_prefs, s->carousel, s->carousel_len);
    s_lift_us = i >= 0 ? esp_timer_get_time() : 0;
    return i;
}

bool rd_prefs_take_lift(void)
{
    const int64_t t = s_lift_us;
    s_lift_us = 0;
    return t != 0 && esp_timer_get_time() - t < LIFT_MS * 1000LL;
}

/* The rows' apply (event loop, UI_THEME_SET). */
static void set_field(rd_pref_field_t f, uint8_t v)
{
    if (rd_prefs_get(&s_prefs, f) == v || !rd_prefs_set(&s_prefs, f, v)) return;
    publish();   /* the chime timer reads the quiet window from the snapshot at each fire */
    commit();
    if (f != RD_PF_MODE) fk_chime_svc_sync();   /* the Chime row starts or stops the service (Task 8) */
}

static uint8_t get_mode(void) { return rd_prefs_now(RD_PF_MODE); }
static uint8_t get_chime(void) { return rd_prefs_now(RD_PF_CHIME); }
static uint8_t get_from(void) { return rd_prefs_now(RD_PF_QUIET_FROM); }
static uint8_t get_to(void) { return rd_prefs_now(RD_PF_QUIET_TO); }
static void set_mode(uint8_t v) { set_field(RD_PF_MODE, v); }
static void set_chime(uint8_t v) { set_field(RD_PF_CHIME, v); }
static void set_from(uint8_t v) { set_field(RD_PF_QUIET_FROM, v); }
static void set_to(uint8_t v) { set_field(RD_PF_QUIET_TO, v); }

#if CONFIG_OMNI_DEBUG_CONSOLE
static volatile uint32_t s_offer_verdict;   /* (seq << 8) | rd_offer_verdict_t: the event loop's, read by the console */

uint32_t rd_prefs_svc_offer_verdict(void) { return s_offer_verdict; }

/* `rd offer reset` on the event loop: the offered bit of every listed face the carousel lacks cleared and the
 * extension committed (the 2 s debounced save), so the next boot appends those faces again. Never on a no-write boot
 * or a read-only record (nothing could be saved), and a failed commit undoes the reset in RAM: the verdict is what NVS
 * will hold, published for the console. */
static void offer_reset(uint8_t v)
{
    if (v != RD_OFFER_RESET_GO) return;
    const omni_settings_t *s = settings();
    const rd_prefs_t was = s_prefs;
    const bool writable = store_local_writable();
    rd_offer_verdict_t vd = rd_prefs_offer_reset_verdict(&s_prefs, writable, s->carousel, s->carousel_len);
    if (vd == RD_OFFER_V_RESET) {
        if (commit()) {
            publish();
        } else {
            s_prefs = was;   /* nothing saved: the mask stays as NVS has it */
            vd = RD_OFFER_V_RAM_ONLY;
        }
    }
    ESP_LOGI(TAG, "offer reset: %s (offered %04x)",
             vd == RD_OFFER_V_RESET       ? "done; the next boot appends the missing faces"
             : vd == RD_OFFER_V_NOTHING   ? "nothing to reset (every listed face is in the carousel or not yet offered)"
             : vd == RD_OFFER_V_READ_ONLY ? "refused: a no-write boot or a read-only record"
                                          : "refused: the commit failed; nothing changed",
             s_prefs.offered);
    s_offer_verdict = ((((s_offer_verdict >> 8) + 1) & 0xFFFFFFu) << 8) | (uint32_t)vd;
}
#endif

static const char *const MODE_O[] = {"Always on", "Stasis"}, *const MODE_S[] = {"ALWAYS", "STASIS"};
static const char *const CHIME_O[] = {"Off", "On"}, *const CHIME_S[] = {"OFF", "ON"};
_Static_assert(RD_MODE_ALWAYS_ON == 0 && RD_MODE_STASIS == 1, "Mode's options are rd_mode_t's order");

const watch_setting_t RD_SETTINGS[RD_SETTINGS_ROWS] = {
    {.label = "Mode", .n = 2, .opts = MODE_O, .shorts = MODE_S, .get = get_mode, .apply = set_mode},
    {.label = "Chime", .n = 2, .opts = CHIME_O, .shorts = CHIME_S, .get = get_chime, .apply = set_chime},
    {.label = "Quiet from", .hours = true, .get = get_from, .apply = set_from},
    {.label = "Quiet until", .hours = true, .get = get_to, .apply = set_to},
#if CONFIG_OMNI_DEBUG_CONSOLE
    [RD_ROW_OFFER_RESET] = {.label = "offer reset", .apply = offer_reset},   /* no get: never shown */
#endif
};
_Static_assert(RD_SETTINGS_ROWS <= WATCH_SETTINGS_MAX, "the Settings screen sees every row");
