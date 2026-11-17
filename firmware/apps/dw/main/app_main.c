/* The dw firmware (Stage 4b): watch_ui with the dw theme descriptor (dw_theme.c). */
#include "watch_theme.h"

extern const watch_theme_t DW_THEME;

void app_main(void) { watch_main(&DW_THEME); }
