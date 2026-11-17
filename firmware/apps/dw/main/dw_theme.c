/* The dw theme's descriptor (watch_theme.h), dw wave 1. Its display name and the faces' titles are dw_names.h's
 * (spec section 14; the public release: "Time Traveller"). Face ids are dw/prefs.h's enum dw_face (WD5). The shared
 * Digital face and the built dw faces are registered (Task 11: Crystal Chamber; Task 13: Roundel analog, Call box
 * digital; Task 14: Rotor, Coral; Task 15: Circle Script, Learning, and the numeral key as SCR_THEME); every other
 * carousel entry shows the fallback (Digital) until its own task registers it (WD6). */
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "watch_ui.h"
#include "dw/prefs.h"
#include "dw_prefs_svc.h"
#include "fk_face.h"
#include "dw/faces.h"
#include "dw_chime.h"
#include "store.h"
#include "dw/clock.h"
#include "omni/cities.h"
#include "dw_console.h"
#include "dw_names.h"
#include "mmap_generate_assets.h"   /* generated at build time from firmware/apps/dw/assets/ (main/CMakeLists.txt) */
#ifndef DW_MMAP_PER_BUILD_DIR   /* G3: only the build dir's own copy (main/CMakeLists.txt) carries this */
#error "mmap_generate_assets.h is not this build dir's copy: delete firmware/apps/dw/main/mmap_generate_assets.h"
#endif

/* Every face's key, title, Settings group and thumbnail, by id: DW_FACE_TABLE (dw/prefs.h) with dw_names.h's names */
#define FACE_INFO(id, key, NAME, GROUP, thumb) [id] = {#key, DW_NAME_FACE_##NAME, DW_NAME_GROUP_##GROUP, thumb},
static const watch_face_info_t FACES[DW_F__COUNT] = {DW_FACE_TABLE(FACE_INFO)};
#undef FACE_INFO
_Static_assert(DW_F__COUNT <= SHELL_FACE_MAX, "the shell hosts every dw face id");

static const char *const REVEAL_NAMES[] = {"Doors", "Fade", "Instant"};
static const char *const REVEAL_SHORTS[] = {"DOORS", "FADE", "NONE"};   /* the Reveal row's rim (Task 11) */

FK_FACE_VT(dw_crystal, DW_FACE_CRYSTAL);
FK_FACE_VT(dw_roundel, DW_FACE_ROUNDEL);
FK_FACE_VT(dw_callbox, DW_FACE_CALLBOX);
FK_FACE_VT(dw_rotor, DW_FACE_ROTOR);
FK_FACE_VT(dw_coral, DW_FACE_CORAL);
FK_FACE_VT(dw_circles, DW_FACE_CIRCLES);
FK_FACE_VT(dw_learn, DW_FACE_LEARN);
FK_FACE_VT(dw_room, DW_FACE_ROOM);
FK_FACE_VT(dw_amber, DW_FACE_AMBER);
FK_FACE_VT(dw_fob, DW_FACE_FOB);
FK_FACE_VT(dw_console, DW_FACE_CONSOLE);
FK_FACE_VT(dw_wall, DW_FACE_WALL);
FK_FACE_VT(dw_doorway, DW_FACE_DOORWAY);
FK_FACE_VT(dw_broadcast, DW_FACE_BROADCAST);
FK_FACE_VT(dw_vortex_face, DW_FACE_VORTEX);   /* not dw_vortex: dw/vortex.h has dw_vortex_bind */

/* Every face's fk_face_rt_t, by face id, in PSRAM (R147 PR9, WD36: off internal .data); internal only when no PSRAM
 * is free, as the Settings rows do (dw final review, M3). Allocated once and kept. Digital's slot is unused. */
static fk_face_rt_t *s_face_rt;

/* SCR_THEME (Task 15, scr_key.c): the numeral key, a pushed screen over an fk_view of DW_KEY; registered with the faces */
extern const omni_screen_vt_t SCR_KEY_VT;

/* face_kit's hooks (rd wave 1 Task 2): what face_generic.c called itself until then. Today's sunrise and sunset for the
 * home city (dw_sun_today, dw/clock.h); a Flip-open lift's wake (dw_prefs_take_lift: the doors whatever the style,
 * WD7). No chime preview, sleep record or slept time. */
static bool sun_today(int64_t now, int16_t *rise_m, int16_t *set_m)
{
    const omni_city_t *c = omni_city_at(settings()->city);
    return c && dw_sun_today(now, c->lat, c->lon, rise_m, set_m);
}
static const fk_face_hooks_t HOOKS = {.sun_today = sun_today, .take_lift = dw_prefs_take_lift};

/* The boot task, before the faces register (watch_main: after_store, then register_faces) and again after a migrate
 * force reloads the records (F3): face_kit's hooks and the chime plan first, then the prefs (whose sync starts the
 * chime service). Setting them again is harmless (the same pointers). */
static void after_store(void)
{
    fk_face_set_hooks(&HOOKS);
    fk_chime_svc_init(&DW_CHIME_THEME);
    dw_prefs_after_store();
}

static void register_faces(bool assets_ok)
{
    shell_register_face(DW_F_DIGITAL, &FACE_DIGITAL_VT);
    if (!assets_ok) return;   /* the dw faces' layers live in the assets image */
    if (s_face_rt == NULL) s_face_rt = heap_caps_calloc(DW_F__COUNT, sizeof *s_face_rt, MALLOC_CAP_SPIRAM);
    if (s_face_rt == NULL) s_face_rt = calloc(DW_F__COUNT, sizeof *s_face_rt);   /* no PSRAM free: internal (M3) */
    if (s_face_rt == NULL) {   /* Digital alone, as without the assets (the final fix wave: no abort) */
        ESP_LOGE("dw_theme", "no memory for the faces' state: Digital only");
        return;
    }
    dw_crystal_bind(&s_face_rt[DW_F_CRYSTAL]);
    dw_roundel_bind(&s_face_rt[DW_F_ROUNDEL]);
    dw_callbox_bind(&s_face_rt[DW_F_CALLBOX]);
    dw_rotor_bind(&s_face_rt[DW_F_ROTOR]);
    dw_coral_bind(&s_face_rt[DW_F_CORAL]);
    dw_circles_bind(&s_face_rt[DW_F_CIRCLES]);
    dw_learn_bind(&s_face_rt[DW_F_LEARN]);
    dw_room_bind(&s_face_rt[DW_F_ROOM]);
    dw_amber_bind(&s_face_rt[DW_F_AMBER]);
    dw_fob_bind(&s_face_rt[DW_F_FOB]);
    dw_console_bind(&s_face_rt[DW_F_CONSOLE]);
    dw_wall_bind(&s_face_rt[DW_F_WALL]);
    dw_doorway_bind(&s_face_rt[DW_F_DOORWAY]);
    dw_broadcast_bind(&s_face_rt[DW_F_BROADCAST]);
    dw_vortex_face_bind(&s_face_rt[DW_F_VORTEX]);
    shell_register_face(DW_F_CRYSTAL, &dw_crystal_VT);
    shell_register_face(DW_F_ROUNDEL, &dw_roundel_VT);
    shell_register_face(DW_F_CALLBOX, &dw_callbox_VT);
    shell_register_face(DW_F_ROTOR, &dw_rotor_VT);
    shell_register_face(DW_F_CORAL, &dw_coral_VT);
    shell_register_face(DW_F_CIRCLES, &dw_circles_VT);
    shell_register_face(DW_F_LEARN, &dw_learn_VT);
    shell_register_face(DW_F_ROOM, &dw_room_VT);
    shell_register_face(DW_F_AMBER, &dw_amber_VT);
    shell_register_face(DW_F_FOB, &dw_fob_VT);
    shell_register_face(DW_F_CONSOLE, &dw_console_VT);
    shell_register_face(DW_F_WALL, &dw_wall_VT);
    shell_register_face(DW_F_DOORWAY, &dw_doorway_VT);
    shell_register_face(DW_F_BROADCAST, &dw_broadcast_VT);
    shell_register_face(DW_F_VORTEX, &dw_vortex_face_VT);
    shell_register_screen(&SCR_KEY_VT);   /* SCR_THEME: the numeral key (UI_OPEN_THEME from #14 and #16) */
}

const watch_theme_t DW_THEME = {
    .id = "dw",
    .name = DW_NAME_THEME,
    .faces = FACES,
    .face_count = DW_F__COUNT,
    .fallback_face = DW_F_DIGITAL,
    .limits = {.face_count = DW_F__COUNT, .reveal_count = 3, .reveal_default = 0,
               .def_len = DW_DEF_CAROUSEL_LEN, .def_carousel = DW_DEF_CAROUSEL},
    .reveal_names = REVEAL_NAMES,
    .omni_legacy = false,
    .assets_files = MMAP_ASSETS_FILES,
    .assets_checksum = MMAP_ASSETS_CHECKSUM,
    .register_faces = register_faces,
#if CONFIG_OMNI_DEBUG_CONSOLE
    .console = dw_console_register,
#endif
    .settings = DW_SETTINGS,
    .settings_count = DW_SETTINGS_ROWS,
    .lift_face = dw_prefs_lift_face,
    .after_store = after_store,
    .reveal_shorts = REVEAL_SHORTS,
};
_Static_assert(sizeof REVEAL_NAMES / sizeof REVEAL_NAMES[0] == 3, "a name for every reveal");
_Static_assert(sizeof REVEAL_SHORTS / sizeof REVEAL_SHORTS[0] == 3, "a rim label for every reveal");
