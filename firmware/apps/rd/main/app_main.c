/* The Mining Ship firmware (rd wave 1): watch_ui with the rd theme descriptor (rd_theme.c). */
#include "watch_theme.h"

extern const watch_theme_t RD_THEME;

void app_main(void) { watch_main(&RD_THEME); }
