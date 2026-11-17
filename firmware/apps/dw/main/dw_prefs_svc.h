#pragma once
/* The dw preferences on the device (dw wave 1 Task 10): t_dw/local's extension (dw/prefs.h) read at boot
 * (WD6), the theme's four Settings rows, and the lift face. The event loop (and the boot task before it) is the only
 * writer; other tasks read the published snapshot (B3). */
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "watch_theme.h"
#include "dw/prefs.h"

#define DW_SETTINGS_COUNT 4   /* the rows the Settings screen shows: Mode, Chime, Quiet from, Quiet until */
#if CONFIG_OMNI_DEBUG_CONSOLE
/* A fifth row without get (so the screen leaves it out, omni_theme_rows): the console's `dw offer reset` reaches the
 * event loop through it (UI_THEME_SET, R26/R27; R101: console builds only). Its value must be DW_OFFER_RESET_GO. */
#define DW_ROW_OFFER_RESET DW_SETTINGS_COUNT
#define DW_OFFER_RESET_GO 1
/* the event loop's last `dw offer reset` verdict, published for the console (any task): (seq << 8) | verdict
 * (dw_offer_verdict_t). seq counts the verdicts, so the console sees its own answer whatever the mask did; 0: none yet */
uint32_t dw_prefs_svc_offer_verdict(void);
#define DW_SETTINGS_ROWS (DW_SETTINGS_COUNT + 1)
#else
#define DW_SETTINGS_ROWS DW_SETTINGS_COUNT
#endif
extern const watch_setting_t DW_SETTINGS[DW_SETTINGS_ROWS];

/* watch_theme_t.after_store: WD6's boot decision (dw_prefs_boot), then on a writable KEEP or RESET_COMMIT the carousel
 * offer (WD24, dw_prefs_offer): a change commits the carousel and the extension and saves at once */
void dw_prefs_after_store(void);
/* watch_theme_t.lift_face: dw_lift_index over the RAM prefs, the current face settings()->carousel[store_face()]
 * (R147 PR1; event loop) */
int dw_prefs_lift_face(void);
bool dw_prefs_take_lift(void);     /* the face's wake right after a Flip-open lift_face: true once (WD7) */
uint8_t dw_prefs_now(dw_pref_field_t f);   /* any task: the snapshot's field (the chime timer, Task 16) */
/* any task: the offered mask as last published (R147 PR2(c): dw_prefs_pack's 32-bit word is full) */
uint16_t dw_prefs_svc_offered(void);
