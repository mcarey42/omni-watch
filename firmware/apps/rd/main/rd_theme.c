/* The Mining Ship theme's descriptor (watch_theme.h), rd waves 1 and 2 (spec section 6). Face ids are rd/faces.h's enum
 * rd_face, their keys, titles and groups its RD_FACE_TABLE. The shared Digital face is registered at RD_F_DIGITAL, and
 * (RW11) it stands in under every id whose face is not built yet (a placeholder) until each face task registers its
 * own. Wave 1 built ids 0-5 (Tasks 9-15). Wave 2's ids 7-13 start as placeholders (their RD_PENDING bits, rd/faces.h);
 * each of wave 2's face tasks (3-10) adds its BUILT row and clears its bit. A placeholder is an id with no BUILT entry
 * (rd_theme_placeholder); the _Static_asserts on BUILT's rows here, test_rd_no_placeholders on the host (RD_FACE_KEYS,
 * RD_FACES and RD_PENDING) and `rd faces` on the watch agree on which. They never reach a flashed watch. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "net.h"
#include "sfx.h"
#include "watch_ui.h"
#include "fk_face.h"
#include "rd/faces.h"
#include "rd/release.h"
#include "omni/timefmt.h"
#include "rd/prefs.h"
#include "rd_prefs_svc.h"
#include "rd_console.h"
#include "rd_chime.h"
#include "rd_asleep.h"
#include "store.h"
#include "mmap_generate_assets.h"   /* generated at build time from firmware/apps/rd/assets/ (main/CMakeLists.txt) */
#ifndef RD_MMAP_PER_BUILD_DIR   /* G3: only the build dir's own copy (main/CMakeLists.txt) carries this */
#error "mmap_generate_assets.h is not this build dir's copy: delete firmware/apps/rd/main/mmap_generate_assets.h"
#endif

#define RD_INFO_OF(id, key, title, group) [id] = {key, title, group, "rd_th_" key ".bin"},
static const watch_face_info_t FACES[RD_F__COUNT] = {RD_FACE_TABLE(RD_INFO_OF)};
#undef RD_INFO_OF
_Static_assert(RD_F__COUNT <= SHELL_FACE_MAX, "the shell hosts every rd face id");

static const char *const REVEAL_NAMES[] = {"Full", "Fade", "Instant"};
static const char *const REVEAL_SHORTS[] = {"FULL", "FADE", "NONE"};   /* the Reveal row's rim (spec section 9.4) */

/* The built faces' runtimes (fk_face.h): each id's trampolines over its rd_core ops */
FK_FACE_VT(rd_stasis, RD_FACE_STASIS);
FK_FACE_VT(rd_terminal, RD_FACE_TERMINAL);
FK_FACE_VT(rd_deck, RD_FACE_DECK);
FK_FACE_VT(rd_hull, RD_FACE_HULL);
FK_FACE_VT(rd_bunk, RD_FACE_BUNK);
FK_FACE_VT(rd_curry, RD_FACE_CURRY);
FK_FACE_VT(rd_bay, RD_FACE_BAY);   /* wave 2 Task 3 */
FK_FACE_VT(rd_drift, RD_FACE_DRIFT);   /* wave 2 Task 5 */
FK_FACE_VT(rd_console, RD_FACE_CONSOLE);   /* wave 2 Task 6 */
FK_FACE_VT(rd_scope, RD_FACE_SCOPE);   /* wave 2 Task 7 */
FK_FACE_VT(rd_vending, RD_FACE_VENDING);   /* wave 2 Task 8 */
FK_FACE_VT(rd_bowl, RD_FACE_BOWL);   /* wave 2 Task 9 */
FK_FACE_VT(rd_mopbot, RD_FACE_MOPBOT);   /* wave 2 Task 10 */

/* Every built face by id: its ops (the key's check), its runtime's bind and its vt. An id with no row is a placeholder
 * (the shared Digital face under its key, RW11): exactly the RD_PENDING ids. One list (BUILT_FACES) gives both the rows,
 * each at its own id, and the ids' mask, so a row given twice, one left out, or one whose RD_PENDING bit is still set
 * cannot compile (the Task 15 review): the mask must be every id but Digital and the pending ones, and the row count
 * must match it */
#define BUILT_FACES(X)                                                                                                \
    X(RD_F_STASIS, stasis, STASIS) X(RD_F_TERMINAL, terminal, TERMINAL) X(RD_F_DECK, deck, DECK)                      \
    X(RD_F_HULL, hull, HULL) X(RD_F_BUNK, bunk, BUNK) X(RD_F_CURRY, curry, CURRY) X(RD_F_BAY, bay, BAY)               \
    X(RD_F_DRIFT, drift, DRIFT) X(RD_F_CONSOLE, console, CONSOLE) X(RD_F_SCOPE, scope, SCOPE)                         \
    X(RD_F_VENDING, vending, VENDING) X(RD_F_BOWL, bowl, BOWL) X(RD_F_MOPBOT, mopbot, MOPBOT)
typedef struct { const fk_face_ops_t *ops; void (*bind)(fk_face_rt_t *rt); const omni_face_vt_t *vt; } built_t;
#define BUILT_ROW(id, k, K) [id] = {&RD_FACE_##K, rd_##k##_bind, &rd_##k##_VT},
static const built_t BUILT[RD_F__COUNT] = {BUILT_FACES(BUILT_ROW)};
#undef BUILT_ROW
#define BUILT_BIT(id, k, K) | (1u << (id))
#define BUILT_ONE(id, k, K) + 1
_Static_assert((0u BUILT_FACES(BUILT_BIT)) == (((1u << RD_F__COUNT) - 1u) & ~(1u << RD_F_DIGITAL) & ~RD_PENDING),
               "RW11: every id but Digital and the pending ones has its BUILT row (a pending id has none)");
_Static_assert((0u BUILT_FACES(BUILT_ONE)) == RD_F__COUNT - 1u - RD_POPCOUNT16(RD_PENDING),
               "RW11: each built id's BUILT row once");
#undef BUILT_BIT
#undef BUILT_ONE

/* id's BUILT row, when its ops carry id's key (RD_FACE_TABLE's), or NULL */
static const built_t *built_of(uint8_t id)
{
    if (id >= RD_F__COUNT || id == RD_F_DIGITAL || BUILT[id].ops == NULL) return NULL;
    return strcmp(BUILT[id].ops->key, RD_FACE_KEYS[id]) == 0 ? &BUILT[id] : NULL;
}

bool rd_theme_placeholder(uint8_t id) { return id < RD_F__COUNT && id != RD_F_DIGITAL && built_of(id) == NULL; }

/* Every face's fk_face_rt_t, by face id, in PSRAM (R147 PR9: off internal .data); internal only when no PSRAM is free.
 * Allocated once and kept. The unbuilt ids' and Digital's slots are unused. */
static fk_face_rt_t *s_face_rt;

/* The Stasis tap's chime preview (C9, PR4): only with Chime on, the sound on and not provisioning
 * (rd_chime_preview_allowed), through the chime service (fk_chime_svc_play_now, declared in every build: the preview
 * works with the console off) */
static void chime_preview(void)
{
    bool on = false;
    uint8_t vol = 0;
    sfx_config_get(&on, &vol);
    if (rd_chime_preview_allowed(rd_prefs_now(RD_PF_CHIME) != 0, on, net_provisioning())) (void)rd_chime_play_now(false);
}

/* face_kit's hooks: a Stasis lift's wake (rd_prefs_take_lift); the time asleep (Task 9: the record at every rd face's
 * sleep, the wake's cause deciding whether the readout refreshes, the frozen duration in every input); the tap's
 * chime preview. No chime hook: fk_face fills the chime channels from fk_chime_svc_fill itself (PR15). */
/* fk_face calls woke at every wake before the first frame, so before a reveal binds: Stasis Release's lamp stays red on
 * an unset clock, and its ops see no face input, so it is told here (rd_rel_set_clock; Task 10) */
static void woke(omni_wake_cause_t cause, bool reveal, bool lift)
{
    rd_rel_set_clock(omni_time_is_set((int64_t)time(NULL)));
    /* the face waking is the one on top: a Stasis lift has moved nav to it already (ui_show_lift); the LVGL lock is held
     * here (shell_wake), as shell_nav needs */
    const bool on_stasis = lift || omni_nav_current_face(shell_nav(), settings()) == RD_F_STASIS;
    rd_asleep_woke(cause, reveal, lift, on_stasis);
}

static const fk_face_hooks_t HOOKS = {.take_lift = rd_prefs_take_lift, .chime_preview = chime_preview,
                                      .slept = rd_asleep_slept, .on_sleep = rd_asleep_on_sleep, .woke = woke};

/* The boot task, before the faces register (watch_main: after_store, then register_faces) and again after a migrate
 * force reloads the records (F3): the time-asleep record's boot (once: this boot's token, the cold-boot clear), face_kit's
 * hooks and the chime plan, then the prefs (whose sync starts the chime service). Setting them again is harmless (the
 * same pointers). */
static void after_store(void)
{
    rd_asleep_boot();
    fk_face_set_hooks(&HOOKS);
    fk_chime_svc_init(&RD_CHIME);
    rd_prefs_after_store();
}

/* watch_theme_t.lift_face (the event loop, at a full wake): rd's lift rule (rd_prefs_lift_face). A Stasis lift from a
 * face whose runtime writes no time-asleep record (the shared Digital face, a placeholder, or every face without the
 * assets) discards the record a swipe away from an rd face left, so the wake shows none (rd_asleep_discard, PR17). The
 * face slept on is settings()->carousel[store_face()] (PR1). */
static int lift_face(void)
{
    const int i = rd_prefs_lift_face();
    if (i >= 0) {
        const omni_settings_t *s = settings();
        const uint8_t at = store_face();
        const uint8_t id = at < s->carousel_len && at < OMNI_CAROUSEL_MAX ? s->carousel[at] : RD_F_DIGITAL;
        const bool records = s_face_rt != NULL && id < RD_F__COUNT && id != RD_F_DIGITAL && !rd_theme_placeholder(id);
        if (!records) rd_asleep_discard();
    } else {
        /* no lift: the wake stays on the face slept on; on one these hooks never see, a held record goes (R162) */
        const omni_settings_t *s = settings();
        const uint8_t at = store_face();
        const uint8_t id = at < s->carousel_len && at < OMNI_CAROUSEL_MAX ? s->carousel[at] : RD_F_DIGITAL;
        if (s_face_rt == NULL || id >= RD_F__COUNT || id == RD_F_DIGITAL || rd_theme_placeholder(id))
            rd_asleep_discard_held();
    }
    return i;
}

static void register_faces(bool assets_ok)
{
    shell_register_face(RD_F_DIGITAL, &FACE_DIGITAL_VT);
    for (uint8_t id = 0; id < RD_F__COUNT; id++)
        if (rd_theme_placeholder(id)) shell_register_face(id, &FACE_DIGITAL_VT);
    /* the boot's face (carousel[store_face()], what the boot's first wake shows): only on the Stasis Clock does a soft
     * reset's time asleep show; on any other face the record goes now (rd_asleep_boot_on, the controller's ruling) */
    const omni_settings_t *s = settings();
    const uint8_t at = store_face();
    const bool on_stasis =
        assets_ok && at < s->carousel_len && at < OMNI_CAROUSEL_MAX && s->carousel[at] == RD_F_STASIS;
    if (!assets_ok) {   /* the rd faces' atlases live in the assets image */
        rd_asleep_boot_on(false);
        return;
    }
    if (s_face_rt == NULL) s_face_rt = heap_caps_calloc(RD_F__COUNT, sizeof *s_face_rt, MALLOC_CAP_SPIRAM);
    if (s_face_rt == NULL) s_face_rt = calloc(RD_F__COUNT, sizeof *s_face_rt);   /* no PSRAM free: internal */
    if (s_face_rt == NULL) {   /* the placeholders and Digital alone, as without the assets */
        ESP_LOGE("rd_theme", "no memory for the faces' state: Digital only");
        rd_asleep_boot_on(false);
        return;
    }
    for (uint8_t id = 0; id < RD_F__COUNT; id++) {
        const built_t *b = built_of(id);
        if (b == NULL) continue;
        b->bind(&s_face_rt[id]);
        shell_register_face(id, b->vt);
    }
    rd_asleep_boot_on(on_stasis);
}

const watch_theme_t RD_THEME = {
    .id = "rd",
    .name = "Mining Ship",
    .faces = FACES,
    .face_count = RD_F__COUNT,
    .fallback_face = RD_F_DIGITAL,
    .limits = {.face_count = RD_F__COUNT, .reveal_count = 3, .reveal_default = 0,
               .def_len = RD_DEF_CAROUSEL_LEN, .def_carousel = RD_DEF_CAROUSEL},
    .reveal_names = REVEAL_NAMES,
    .omni_legacy = false,
    .assets_files = MMAP_ASSETS_FILES,
    .assets_checksum = MMAP_ASSETS_CHECKSUM,
    .register_faces = register_faces,
#if CONFIG_OMNI_DEBUG_CONSOLE
    .console = rd_console_register,
#endif
    .settings = RD_SETTINGS,
    .settings_count = RD_SETTINGS_ROWS,
    .lift_face = lift_face,
    .after_store = after_store,
    .reveal_shorts = REVEAL_SHORTS,
};
_Static_assert(sizeof REVEAL_NAMES / sizeof REVEAL_NAMES[0] == 3, "a name for every reveal");
_Static_assert(sizeof REVEAL_SHORTS / sizeof REVEAL_SHORTS[0] == 3, "a rim label for every reveal");
