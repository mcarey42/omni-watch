#pragma once
/* The alarm's face pick (Stage 5, preflight F3): what UI_ALARM does to nav before it wakes the watch. Theme-neutral:
 * face_id is the theme's own id (the Omni's FACE_TIMER); a theme that never posts UI_ALARM never calls it. */
#include <stdint.h>
#include "omni/nav.h"
#include "omni/settings.h"

/* NAV_NONE with n untouched when SCR_PROVISION is anywhere in the stack (the QR, also under a picker BOOT long pushed
 * over it: popping it would leave the provisioning session holding BLE and Wi-Fi with no screen); otherwise omni_nav_show_id: for a carousel face omni_nav_pick to its index (which closes any
 * screen): NAV_FACE_CHANGED, or NAV_SCREEN_POPPED when that face was already current. A face_id not in the carousel is omni_nav_show_id with the
 * shell's bound (OMNI_CAROUSEL_MAX): NAV_TRANSIENT, or NAV_NONE for an id at or above it. */
omni_nav_effect_t omni_nav_alarm(omni_nav_t *n, const omni_settings_t *s, uint8_t face_id);
