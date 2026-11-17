#pragma once
/* The fonts only the Omni's faces use (tools/fonts.sh writes them to apps/omni/main/ui/fonts); the shared ones are
 * watch_ui's (theme.h). family_weight_size: Cormorant SC = FD, IBM Plex Sans = FS, IBM Plex Mono = FM. */
#include "lvgl.h"

/* globe-live face */
LV_FONT_DECLARE(font_plex_sans_500_50);   /* digits, ':' and '-' only ("--:--" when the clock is unset) */
LV_FONT_DECLARE(font_plex_sans_600_13);
LV_FONT_DECLARE(font_plex_sans_500_11);   /* also globe-spin */
LV_FONT_DECLARE(font_cormorant_700_19);   /* the date (with its middle dot) */
LV_FONT_DECLARE(font_plex_mono_500_16);   /* digits, ':', '-', 'a', 'p', 'm' and the em dash only: the sun margins */
/* globe-spin face */
LV_FONT_DECLARE(font_cormorant_700_24);
LV_FONT_DECLARE(font_cormorant_700_28);
/* omni dial (faces-omni.js: the travel caption; Task 8's glance) */
LV_FONT_DECLARE(font_cormorant_700_13);   /* A-Z, 0-9 and space only */
/* globe-analog face */
LV_FONT_DECLARE(font_plex_sans_600_16);   /* digits and '-' only (the date window; "--" when the clock is unset) */
/* Stage 5 faces: digit subsets only */
LV_FONT_DECLARE(font_plex_sans_500_21);   /* world cartouche: digits, ':' and '-' ("--:--" when the clock is unset) */
LV_FONT_DECLARE(font_plex_sans_500_30);   /* almanac day length: digits, 'h', 'm', space and '-' */
LV_FONT_DECLARE(font_plex_sans_500_40);   /* almanac sun panel altitude: digits, the degree sign and U+2212 minus */
LV_FONT_DECLARE(font_plex_mono_500_26);   /* stopwatch hundredths: digits and '.' */
