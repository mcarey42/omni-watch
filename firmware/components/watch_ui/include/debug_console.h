#pragma once
#include <stdbool.h>
#include "sdkconfig.h"
#include "omni/sync.h"
#include "omni/batrec.h"
#include "omni/clockcal.h"

/* Start the esp_console REPL on USB-Serial-JTAG (shot, state, batsession, city, clock, net, reboot; with
 * CONFIG_OMNI_DEBUG_CONSOLE also inject, trace, settime, nvs, fps, refr, face, awake, setting, sfx, disp, assets, theme, pack,
 * pm, usbsim, net's debug forms (watch_ui/Kconfig) and the theme's own commands, watch_theme()->console). Never aborts. */
void debug_console_start(void);

/* Register one command (a theme's console hook calls this); a command that cannot be registered is logged and
 * skipped (m8: nothing here aborts). */
void debug_console_reg(const char *command, const char *help, int (*func)(int argc, char **argv));

/* Provided by watch_main.c for `state`: the event loop's stack high-water mark (bytes) and its sync state
 * (a racy copy; debug output only). */
unsigned app_events_stack_hwm(void);
omni_sync_t app_sync(void);
omni_batrec_t app_batrec(void);   /* the battery session record (racy copy) */
omni_clockcal_t app_clockcal(void); /* the clock calibration and SNTP correction log (racy copy) */
/* LVGL task, at the end of a screen's draw (spec 11): when the task's stack high-water mark is lower than any seen
 * before, log "lvgl stack: <n> B free after <tag>" (from an LVGL async call, off the draw's stack); `stacks` prints
 * each tag's lowest. A call on any other task (a console `shot`, the wake refresh) is ignored. tag: a string literal
 * ("settings/drum"). Console builds only; otherwise nothing. */
#if CONFIG_OMNI_DEBUG_CONSOLE
void ui_stack_note(const char *tag);
#else
static inline void ui_stack_note(const char *tag) { (void)tag; }
#endif
#if CONFIG_OMNI_DEBUG_CONSOLE
void app_hold_awake(bool hold); /* `awake hold|release`: true = the tick runs no glance/sleep timeout */
bool app_awake_hold(uint32_t *held_ms); /* `state`: the hold is set (and *held_ms since it was; racy copy) */
#endif
