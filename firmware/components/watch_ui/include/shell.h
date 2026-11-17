#pragma once
/* Screen host: owns the active screen, shows the current face (and its glance), the pip dots at 6 o'clock
 * and the red lens at 11 o'clock (blinking: clock not set; steady and dim: time sync stale, omni_sync_lens),
 * and the screens pushed over the face (nav's stack top).
 *
 * THREADING: every shell_* call needs the LVGL lock (bsp_display_lock) unless it runs in an LVGL callback.
 * Faces' create/update/destroy run with the lock held. Never call power_* or settings_edit/commit from here
 * or from a face (R26/R27): post an event to the queue instead. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "omni/input.h"
#include "omni/carousel_edit.h"
#include "omni/nav.h"
#include "omni/settings.h"
#include "omni/sync.h"

typedef struct {
    const char *name;
    void (*create)(lv_obj_t *parent);         /* build widgets once */
    void (*update)(int64_t now, bool glance); /* called every 1 s (and on show) */
    void (*destroy)(void);
    /* Optional hooks (NULL = unused). wake/sleep come in pairs: sleep only follows a wake. */
    void (*wake)(bool reveal);             /* visible and awake: start animating; reveal = play the spin-up. May
                                              come again while awake (BOOT double replays the reveal) */
    void (*sleep)(void);                   /* not visible or panel going off: stop all timers and submissions;
                                              never blocks */
    bool (*claim)(int16_t x0, int16_t y0); /* INPUT task, no LVGL lock, pure geometry: true = this touch is the
                                              face's drag (G_DRAG_*). Never touches LVGL */
    void (*gesture)(const omni_gesture_t *g, uint32_t t_ms); /* every gesture while the face is on top and awake */
    bool own_lens;                         /* true: the shell hides its 11 o'clock lens; the face shows it */
    /* Appended (Stage 5; after any field the other track appends). Event loop, LVGL lock held: true = a long press
     * at (x, y) is the face's own (no picker; spec §8 "non-interactive area"). Asked before the face's gesture hook
     * sees the press (preflight F1). NULL: never */
    bool (*holds)(int16_t x, int16_t y);
} omni_face_vt_t;

/* A screen pushed over the face (SCR_PROVISION now; the picker, quick settings and settings later). It gets
 * the gestures while it is on top, and asks for anything beyond its own drawing by posting EV_UI with one of
 * the UI_* codes below. */
typedef struct {
    omni_screen_t id;
    void (*create)(lv_obj_t *parent);
    void (*update)(int64_t now);                 /* every 1 s while shown */
    void (*destroy)(void);
    void (*gesture)(const omni_gesture_t *g, uint32_t t_ms); /* optional; t_ms = when the input task saw it */
    void (*dim)(bool on);                        /* optional: idle / glance dimming */
} omni_screen_vt_t;

/* EV_UI codes (omni_events.h): what a screen asks the event loop to do. The event's .ui is UI_EV(code, arg). */
enum {
    UI_POP = 1,         /* back from the top screen */
    UI_RESCAN,          /* back to the QR (a new session if none runs); the saved network stays */
    UI_SYNC_NOW,        /* time sync now, failures shown */
    UI_PICK,            /* arg = carousel index: show that face (omni_nav_pick) */
    UI_OPEN_PICKER,     /* push the face picker */
    UI_OPEN_QUICK,      /* push quick settings */
    UI_OPEN_SETTINGS,   /* open settings (omni_nav_open_settings: in the picker's place; never a second copy) */
    UI_WIFI_SETUP,      /* provisioning (QR); the saved network stays until a new one is accepted */
    UI_WIFI_FORGET,     /* erase the saved network: posted only by Settings' Forget hold (omni_forget_hold_step, C2) */
    UI_FORGET_RESCAN,   /* "Forget and re-scan" (offered after two refusals in a row): erase it, then the QR; posted
                           only by provisioning's pill hold (omni_forget_hold_step, I4) */
    /* settings, applied live by the event loop (settings_commit): arg = the new value */
    UI_SET_CITY, UI_SET_BRIGHTNESS, UI_SET_SLEEP, UI_SET_H24, UI_SET_SOUND, UI_SET_VOLUME, UI_SET_REVEAL,
    UI_SET_MOTION,
    UI_NVS_MIGRATE_FORCE,   /* console `nvs migrate force` (console builds): store_migrate_force on the loop (R27) */
    UI_NET_BOOT_UNPROV,     /* console `net boot-unprov` (console builds): the one-shot flag written on the loop (R27) */
    /* Appended (Stage 5; after any code the other track appends). Posted by a theme's alarm (the Omni's timer). */
    UI_ALARM,           /* arg = a face id: omni_nav_alarm's pick, then wake (omni_pwr_on_alarm, no reveal);
                           arg UI_ALARM_KEEP: activity only (omni_pwr_on_alarm_keep), never a wake */
    /* Appended (dw wave 1 Task 10; after Stage 5's codes). */
    UI_THEME_SET,       /* arg = row | value << 8: the event loop calls the theme's settings[row].apply(value) */
    UI_OPEN_THEME,      /* push SCR_THEME (the theme registers its screen in register_faces) */
    /* Appended (rim keyboard and Settings Task 4; the screens' tasks wire all but UI_SHOW_FACE and UI_HOME). */
    UI_OPEN_NETLIST, UI_OPEN_KEYBOARD, UI_OPEN_FACES, UI_SET_CAROUSEL, UI_PROV_TO_LIST, UI_LIST_TO_PROV,
    UI_SHOW_FACE,       /* arg = a face id: omni_nav_show_id (a carousel face is picked and saved; a hidden one is transient) */
    UI_HOME,            /* every screen closed (omni_nav_home) */
};
#define UI_ALARM_KEEP 0xFF
#define UI_EV(code, arg) ((int)(code) | ((int)(arg) << 8))
#define UI_EV_CODE(v) ((v) & 0xff)
#define UI_EV_ARG(v) ((v) >> 8)

/* Post EV_UI UI_EV(code, arg) (any task). False if the queue was full and it was dropped. */
bool shell_post(int code, int arg);

/* Device status the faces show; the event loop refreshes it (shell_set_status) and faces read it. */
typedef struct {
    int battery_pct;    /* 0..100, -1 unknown */
    bool charging;
    omni_sync_t sync;   /* time sync state (never synced until the net task lands) */
} shell_status_t;

/* Takes over the active screen. nav is read (never written) by shell_show_current. */
void shell_init(const omni_nav_t *nav);
#define SHELL_FACE_MAX 16                   /* face ids are the theme's own, 0 .. SHELL_FACE_MAX-1 */
void shell_register_face(uint8_t id, const omni_face_vt_t *vt);
void shell_show_current(void);              /* uses omni_nav_current_face; an unregistered face shows the theme's
                                               fallback_face (watch_theme.h) */
void shell_register_screen(const omni_screen_vt_t *vt);
/* Shows nav's top: the face, or the registered screen for it. Returns false (showing the face) when no screen
 * is registered for the top id, so the caller can pop it. */
bool shell_show_top(void);
void shell_refresh(void);                   /* update the face or screen now (e.g. after a setting changed) */
const omni_nav_t *shell_nav(void);          /* read-only, under the LVGL lock */
bool shell_face_ready(uint8_t id);          /* a view is registered for id (others show the fallback face) */
void shell_gesture(const omni_gesture_t *g, uint32_t t_ms); /* to the screen on top, else the awake face's hook */
bool shell_face_holds(int16_t x, int16_t y); /* the awake face on top, no screen: its holds(x, y); else false */
void shell_dim(bool on);                    /* dims the screen on top (no effect on faces) */
void shell_set_glance(bool on);             /* also refreshes the face at once (call before the panel comes on);
                                               a screen on top dims instead */
/* The face's wake/sleep hooks follow these (the event loop calls them round power_apply, R26).
 * shell_wake: awake (after shell_set_glance(false)); the face on top wakes (reveal: with its spin-up, also for a
 * BOOT double), and its touch claim is set. shell_sleep: the panel is going off; the face sleeps and its claim is
 * cleared. A screen on top owns the touch claim (it sets and clears its own): the shell touches the claim only
 * while the face is on top. */
void shell_wake(bool reveal);
void shell_sleep(void);
void shell_show_pips(uint8_t idx, uint8_t count); /* carousel position dots at 6 o'clock for 1 s */
void shell_set_status(const shell_status_t *st);
const shell_status_t *shell_status(void);

extern const omni_face_vt_t FACE_DIGITAL_VT;      /* face_digital.c: needs no assets; every theme's fallback */

/* Pushed screens (scr_picker.c, scr_quick.c, scr_settings.c). The *_open calls post the EV_UI that makes the
 * event loop push them (any task). */
extern const omni_screen_vt_t SCR_PICKER_VT, SCR_QUICK_VT, SCR_SETTINGS_VT;
void scr_picker_open(void);
void scr_quick_open(void);
void scr_settings_open(void);
/* LVGL lock: the face is on top again (every screen closed), so Settings' next open starts on Home city, not on the row
 * a push left (the stack can collapse without re-creating Settings: a full wake, UI_HOME). Called by shell_show_top. */
void scr_settings_home(void);

/* Appended (rim keyboard and Settings Task 13): the carousel editor (scr_faces.c), pushed by UI_OPEN_FACES. */
extern const omni_screen_vt_t SCR_FACES_VT;
/* LVGL task (and the console): copies c into the event loop's mailbox under a spinlock and posts UI_SET_CAROUSEL; false
 * if the post was dropped (the mailbox keeps c: a later post, or one still queued, applies the latest). The loop checks it
 * with omni_local_validate, ignoring (and logging) any order it would change, and saves it (t_<id>/local, 2 s debounce). */
bool ui_carousel_post(const omni_carousel_t *c);
