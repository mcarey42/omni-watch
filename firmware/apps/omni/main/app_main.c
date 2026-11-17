/* The Omni firmware: watch_ui with the Omni's theme descriptor (omni_theme.c). */
#include "watch_theme.h"

extern const watch_theme_t OMNI_THEME;

void app_main(void) { watch_main(&OMNI_THEME); }
