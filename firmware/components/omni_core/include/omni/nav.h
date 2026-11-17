#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "omni/input.h"
#include "omni/settings.h"

/* Appended (dw wave 1 Task 10, A19/B7): SCR_THEME is the screen a theme pushes itself (UI_OPEN_THEME; the dw key).
 * SCR__COUNT is a sentinel, never pushed: tables indexed by screen id are sized by it. */
typedef enum { SCR_FACE, SCR_PICKER, SCR_QUICK, SCR_SETTINGS, SCR_PROVISION, SCR_THEME,
       SCR_KEYBOARD, SCR_NETLIST, SCR_FACES, /* appended (rim keyboard and Settings Task 4) */
       SCR__COUNT } omni_screen_t;

typedef struct {
    omni_screen_t stack[4];
    uint8_t depth;
    uint8_t face_idx;      /* into carousel */
    uint8_t transient;     /* appended: a face id + 1 shown transiently; 0 none (zero-init safe) */
} omni_nav_t;

typedef enum { NAV_NONE, NAV_FACE_CHANGED, NAV_SCREEN_PUSHED, NAV_SCREEN_POPPED, NAV_REPLAY_REVEAL,
       NAV_TRANSIENT /* appended: show the current face without committing face_idx, no pips */ } omni_nav_effect_t;

void omni_nav_init(omni_nav_t *n);
omni_nav_effect_t omni_nav_gesture(omni_nav_t *n, const omni_settings_t *s, const omni_gesture_t *g);
omni_nav_effect_t omni_nav_boot(omni_nav_t *n, const omni_settings_t *s, omni_press_t p);
omni_nav_effect_t omni_nav_push(omni_nav_t *n, omni_screen_t scr);
/* Back from the top screen (BOOT short, a screen's own "done"); the face is never popped (NAV_NONE then). */
omni_nav_effect_t omni_nav_pop(omni_nav_t *n);
omni_screen_t omni_nav_top(const omni_nav_t *n);
omni_nav_effect_t omni_nav_pick(omni_nav_t *n, const omni_settings_t *s, uint8_t face_idx);
/* omni_nav_pick returns NAV_FACE_CHANGED, not NAV_SCREEN_POPPED, when it cleared a transient face.
 * The face id shown: the transient face when set, else carousel[face_idx] (the theme's own id), or 0 for an empty
 * carousel. Swipes (step), omni_nav_pick and omni_nav_lift clear the transient; step moves from the saved face_idx. */
uint8_t omni_nav_current_face(const omni_nav_t *n, const omni_settings_t *s);

/* R144: swipe up on a face does nothing (the Omni's swipe-up timer shortcut, spec §8, is retired). */

/* Appended (dw wave 1 Task 11; B9, the Task 10 review's m1 and m2). A theme's lift face (watch_theme.h lift_face) as a
 * transient view: NAV_FACE_CHANGED when face_idx moved to idx (clamped to the carousel), and the caller shows that
 * face without committing face_idx and without the pips. NAV_NONE, with n untouched, when a screen is on top (the B9
 * guard: a lift never closes provisioning, Settings, the picker, quick settings or a theme screen), for idx < 0, an
 * empty carousel, or the face already shown. */
omni_nav_effect_t omni_nav_lift(omni_nav_t *n, const omni_settings_t *s, int idx);
/* UI_THEME_SET's argument (shell.h): row | value << 8, and back. Unpack is false, with *row and *value untouched,
 * when row is not below count (count 0: the theme has no rows, or NULL settings). */
int omni_theme_set_pack(uint8_t row, uint8_t value);
bool omni_theme_set_unpack(int arg, uint8_t count, uint8_t *row, uint8_t *value);
/* A theme's Settings row as the screen judges it (watch_theme.h watch_setting_t): get set; opts, shorts non-NULL;
 * hours; n options. */
typedef struct { bool get, opts, shorts, hours; uint8_t n; } omni_theme_row_t;
/* The rows the Settings screen keeps: the first min(count, max) of rows (NULL: none), each kept when it has get and is
 * either a choice row (n > 0 with opts and shorts) or an hour dial (n 0 and hours). keep[] gets the kept rows'
 * indices in order; returns how many (<= max). */
int omni_theme_rows(const omni_theme_row_t *rows, int count, int max, uint8_t keep[]);
/* Appended (dw wave 1 Task 11 fix round, M4): a pick (the picker's, an alarm's) saves face_idx when it changed the face,
 * or when it re-picked the shown face while that differs from the saved one (a transient lift showed it unsaved). */
bool omni_nav_pick_saves(omni_nav_effect_t eff, uint8_t face_idx, uint8_t saved);

/* Appended (rim keyboard and Settings Task 4). */
/* B1: pop the top and push scr in its place: NAV_SCREEN_PUSHED; NAV_NONE on the face. */
omni_nav_effect_t omni_nav_replace(omni_nav_t *n, omni_screen_t scr);
/* Every screen closed: NAV_SCREEN_POPPED; NAV_NONE at depth 1. */
omni_nav_effect_t omni_nav_home(omni_nav_t *n);
/* Show a face by its id. In the carousel: omni_nav_pick to its index (transient cleared). Not in it (id < face_count):
 * every screen closed, transient = id + 1, face_idx kept: NAV_TRANSIENT. id >= face_count: NAV_NONE. */
omni_nav_effect_t omni_nav_show_id(omni_nav_t *n, const omni_settings_t *s, uint8_t face_id, uint8_t face_count);
/* O4, on PA_WAKE_FULL: a tool screen on top -> popped down to depth 1 (to just above the topmost SCR_PROVISION when
 * one is in the stack: provisioning and anything below it is never closed here), transient cleared: NAV_SCREEN_POPPED;
 * else a transient face -> cleared: NAV_TRANSIENT; else NAV_NONE. Provisioning, the picker, quick and a theme screen on
 * top are never closed. */
omni_nav_effect_t omni_nav_wake_full(omni_nav_t *n);
/* SCR_SETTINGS, SCR_NETLIST, SCR_FACES, SCR_KEYBOARD */
bool omni_nav_is_tool(omni_screen_t s);
/* The top's sleep timeout: SCR_KEYBOARD 61 (its own close at 60 s comes first); Settings, list and editor
 * max(sleep_s, 30); anything else sleep_s. */
uint8_t omni_nav_sleep_s(const omni_nav_t *n, uint8_t sleep_s);
/* Fix round 1 (R212): the network list opened from anywhere. With SCR_PROVISION in the stack: pop down to the topmost
 * one and replace it with SCR_NETLIST; else push SCR_NETLIST. NAV_SCREEN_PUSHED (NAV_NONE when a push is refused). */
omni_nav_effect_t omni_nav_open_netlist(omni_nav_t *n);
/* scr is anywhere in the stack. */
bool omni_nav_in_stack(const omni_nav_t *n, omni_screen_t scr);
/* Final review M2: Settings opened (UI_OPEN_SETTINGS: the picker's Settings item). Already in the stack: popped down to
 * the topmost SCR_SETTINGS (NAV_SCREEN_POPPED; NAV_NONE when it is on top), never a second copy. Else the picker on top
 * is popped (Settings takes its place), then every tool screen above the top non-tool one (a list, the editor, the
 * keyboard: so the list is never left in the stack twice), and SCR_SETTINGS is pushed (NAV_SCREEN_PUSHED; NAV_NONE,
 * n untouched, when the push is refused). */
omni_nav_effect_t omni_nav_open_settings(omni_nav_t *n);
