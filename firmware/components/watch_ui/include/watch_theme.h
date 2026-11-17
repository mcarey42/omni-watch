#pragma once
/* The theme descriptor: everything watch_ui needs to know about the firmware it runs in (spec §3.2, plan PD1/PD2).
 * watch_ui never includes an app header; each edge between them is a field here. An app's app_main is
 * `watch_main(&<ITS>_THEME)`. Face ids are the theme's own uint8_t (no cross-theme ranges). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/local.h"
struct _lv_obj_t;   /* lv_obj_t, without pulling lvgl.h into every includer */

#define WATCH_THUMB_PX 68   /* the side of a face's exported picker thumbnail (68 px RGB565A8, the face circle) */

typedef struct {
    const char *key;     /* `face <key>` on the console; NULL: not addressable there (a planned face) */
    const char *title;   /* the picker's title */
    const char *group;   /* the picker's group line */
    const char *thumb;   /* asset name of its WATCH_THUMB_PX thumbnail, or NULL (a plain tile) */
} watch_face_info_t;

/* A theme's own Settings row (appended, dw wave 1 Task 10, B14): drawn in the shared row style after "Reduced
 * motion". A choice row (n options; the window shows opts[v], the rim shorts[v]) or, with n 0 and hours, an hour dial
 * 0..23 shown "HH:00". The screen posts UI_THEME_SET (shell.h); the event loop then calls apply. */
#define WATCH_SETTINGS_MAX 8
typedef struct {
    const char *label;                       /* e.g. "Mode", "Chime", "Quiet from", "Quiet until" */
    uint8_t n; const char *const *opts, *const *shorts;   /* choice rows; n 0 with hours: an hour dial 0..23, "HH:00" */
    bool hours;
    uint8_t (*get)(void);                    /* any task: the current value */
    void (*apply)(uint8_t v);                /* event loop only (R27) */
} watch_setting_t;

typedef struct {
    const char *id;                    /* "omni" | "dw": watch/theme, namespace "t_<id>" (Task 6) */
    const char *name;                  /* "Omni" | "Time Traveller": Settings' Theme line and the swap toast (Task 6) */
    const watch_face_info_t *faces;    /* indexed by the theme's own face id */
    uint8_t face_count, fallback_face; /* fallback: shown for an unregistered face (the digital face's id) */
    omni_theme_limits_t limits;        /* Task 1: default carousel, reveal count and default */
    const char *const *reveal_names;   /* limits.reveal_count labels; Settings hides the row when count <= 1 */
    bool omni_legacy;                  /* the Omni only: run omni_migrate_omni_local (Task 6) */
    uint16_t assets_files, assets_checksum;   /* MMAP_ASSETS_FILES / _CHECKSUM of this app's own image */
    void (*register_faces)(bool assets_ok);   /* LVGL lock held: shell_register_face() for each face */
    void (*console)(void);                    /* the theme's console commands, console builds only; may be NULL */
    /* Console builds only (`assets`): print one line for an asset in one of the theme's own formats and return true,
     * or return false (not one of its own) to get the generic line. May be NULL. */
    bool (*asset_line)(const char *name, const void *data, size_t size);
    /* Appended (Stage 5; after any field the other track appends). LVGL lock, every glance refresh with no screen on
     * top: root is the theme's own container (empty at first, above the face, below the lens and pips). true = show
     * root instead of the face for this glance frame. NULL: never. */
    bool (*glance_override)(struct _lv_obj_t *root, int64_t now, bool h24);
    /* Appended (dw wave 1 Task 10; after Stage 5's field). All zero in the Omni. */
    const watch_setting_t *settings; uint8_t settings_count;   /* <= WATCH_SETTINGS_MAX; after Reduced motion */
    /* Event loop, a full wake (PA_WAKE_FULL) with the face on top (never over provisioning, Settings or a theme
     * screen: the pick closes every screen, B9): a carousel index to show before the face wakes, or -1. */
    int (*lift_face)(void);
    /* The boot task, after store_init and before the nav reads store_face(); again on the event loop after
     * store_migrate_force reloaded the records (F3). Boot-time store work only (R27). */
    void (*after_store)(void);
    /* Appended (dw wave 1 Task 11): the Reveal row's rim labels, indexed by reveal id as reveal_names (short caps,
     * e.g. "DOORS"). NULL: the Omni's IRIS/FLIP/NONE. */
    const char *const *reveal_shorts;
} watch_theme_t;

void watch_main(const watch_theme_t *t);      /* today's app_main body; returns after starting the tasks */
const watch_theme_t *watch_theme(void);       /* the descriptor watch_main was given (NULL before) */
