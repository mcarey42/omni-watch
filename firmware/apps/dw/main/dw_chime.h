#pragma once
/* The dw theme's chime plan for face_kit's chime service (dw_chime.c; rd wave 1 Task 2). */
#include "fk_chime_svc.h"
#include "sdkconfig.h"

extern const fk_chime_theme_t DW_CHIME_THEME;   /* fk_chime_svc_init's, in dw_theme.c's after_store */
#if CONFIG_OMNI_DEBUG_CONSOLE
void dw_chime_print(void);   /* `dw chime`: the service's state, then the plan's (the chime's length, its misses) */
#endif
