#pragma once
/* The Mining Ship preferences on the device (rd wave 1 Task 4; dw_prefs_svc's shape): t_rd/local's extension
 * (rd/prefs.h) read at boot, the theme's four Settings rows, and the lift face. The event loop (and the boot task
 * before it) is the only writer; other tasks read the published snapshot. */
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "watch_theme.h"
#include "rd/prefs.h"

#define RD_SETTINGS_COUNT 4   /* the rows the Settings screen shows: Mode, Chime, Quiet from, Quiet until */
#if CONFIG_OMNI_DEBUG_CONSOLE
/* A fifth row without get (so the screen leaves it out): the console's `rd offer reset` reaches the event loop through
 * it (UI_THEME_SET, R26/R27; R101: console builds only). Its value must be RD_OFFER_RESET_GO. */
#define RD_ROW_OFFER_RESET RD_SETTINGS_COUNT
#define RD_OFFER_RESET_GO 1
/* the event loop's last `rd offer reset` verdict, published for the console (any task): (seq << 8) | verdict
 * (rd_offer_verdict_t). seq counts the verdicts, so the console sees its own answer whatever the mask did; 0: none yet */
uint32_t rd_prefs_svc_offer_verdict(void);
#define RD_SETTINGS_ROWS (RD_SETTINGS_COUNT + 1)
#else
#define RD_SETTINGS_ROWS RD_SETTINGS_COUNT
#endif
extern const watch_setting_t RD_SETTINGS[RD_SETTINGS_ROWS];   /* Mode (Always on / Stasis), Chime (Off / On), Quiet
                                                                 from, Quiet until */

/* watch_theme_t.after_store: fk_prefs_boot over t_rd/local's extension, then on a writable KEEP or RESET_COMMIT the
 * carousel offer: a reset or an offer commits the carousel and the extension and saves at once (the boot task) */
void rd_prefs_after_store(void);
/* watch_theme_t.lift_face: rd_lift_index over the RAM prefs and settings()->carousel (the current face is
 * settings()->carousel[store_face()], PR1; the rule does not depend on it); event loop */
int rd_prefs_lift_face(void);
bool rd_prefs_take_lift(void);      /* the face's wake right after a Stasis lift_face: true once */
uint8_t rd_prefs_now(rd_pref_field_t f);   /* any task: the snapshot's field */
uint16_t rd_prefs_svc_offered(void);       /* any task: the offered mask as last published */
