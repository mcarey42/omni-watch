#pragma once
/* The Mining Ship theme's chime plan for face_kit's chime service (rd_chime.c; rd wave 1 Task 8, spec section 11). */
#include <stdbool.h>
#include "fk_chime_svc.h"
#include "sdkconfig.h"

extern const fk_chime_theme_t RD_CHIME;   /* fk_chime_svc_init's, in rd_theme.c's after_store */
/* Every build: the theme's chime now through the service (fk_chime_svc_play_now: its gate, the quiet window's gain);
 * recipe plays rd_chime_r.pcm (the recipe as written) once instead of rd_chime.pcm, for the owner's listening check.
 * False if the service is not started or it did not sound. */
bool rd_chime_play_now(bool recipe);
#if CONFIG_OMNI_DEBUG_CONSOLE
void rd_chime_print(void);   /* `rd chime`: the service's state, then the plan's (its misses) */
#endif
