#pragma once
/* Which Settings rows, in what order, of what kind. Pure. */
#include <stdint.h>

typedef enum { OMNI_ROW_CITY, OMNI_ROW_BRIGHT, OMNI_ROW_SLEEP, OMNI_ROW_CLOCK, OMNI_ROW_SOUND, OMNI_ROW_VOLUME,
               OMNI_ROW_REVEAL, OMNI_ROW_MOTION, OMNI_ROW_FACES, OMNI_ROW_WIFI, OMNI_ROW_THEME, OMNI_ROW_ABOUT,
               OMNI_ROW__COUNT } omni_row_id_t;       /* a theme row k is OMNI_ROW__COUNT + k */
typedef enum { OMNI_ROWK_JOG, OMNI_ROWK_LEVEL, OMNI_ROWK_OPTS, OMNI_ROWK_LINK, OMNI_ROWK_INFO } omni_row_kind_t;
typedef enum { OMNI_LINK_NONE, OMNI_LINK_FIND_CITY, OMNI_LINK_FACES, OMNI_LINK_WIFI_SHEET, OMNI_LINK_ABOUT_SHEET }
    omni_row_link_t;

/* The row ids in screen order, at most max: City, Brightness, Sleep, Time format, Sound, Volume, Reveal (only when
 * reveal_count > 1), Reduced motion, the theme_rows theme rows, Faces, Wi-Fi, Theme, About. Returns the count. */
int omni_rows_order(uint8_t reveal_count, int theme_rows, int8_t out[], int max);
/* City JOG; Brightness, Volume LEVEL; Faces, Wi-Fi, About LINK; Theme INFO; every other row, theme rows included, OPTS. */
omni_row_kind_t omni_rows_kind(int id);
/* The windowed row's tap: City FIND_CITY, Faces FACES, Wi-Fi WIFI_SHEET, About ABOUT_SHEET, else NONE. */
omni_row_link_t omni_rows_link(int id);
