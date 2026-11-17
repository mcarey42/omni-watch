#include "omni/settings_rows.h"

int omni_rows_order(uint8_t reveal_count, int theme_rows, int8_t out[], int max)
{
    int n = 0;
    for (int r = 0; r < OMNI_ROW__COUNT; r++) {
        if (r == OMNI_ROW_REVEAL && reveal_count <= 1) continue;
        if (r == OMNI_ROW_FACES)   /* the theme rows sit between Reduced motion and Faces */
            for (int k = 0; k < theme_rows && n < max; k++) out[n++] = (int8_t)(OMNI_ROW__COUNT + k);
        if (n < max) out[n++] = (int8_t)r;
    }
    return n;
}

omni_row_kind_t omni_rows_kind(int id)
{
    switch (id) {
    case OMNI_ROW_CITY: return OMNI_ROWK_JOG;
    case OMNI_ROW_BRIGHT: case OMNI_ROW_VOLUME: return OMNI_ROWK_LEVEL;
    case OMNI_ROW_FACES: case OMNI_ROW_WIFI: case OMNI_ROW_ABOUT: return OMNI_ROWK_LINK;
    case OMNI_ROW_THEME: return OMNI_ROWK_INFO;
    default: return OMNI_ROWK_OPTS;
    }
}

omni_row_link_t omni_rows_link(int id)
{
    switch (id) {
    case OMNI_ROW_CITY: return OMNI_LINK_FIND_CITY;
    case OMNI_ROW_FACES: return OMNI_LINK_FACES;
    case OMNI_ROW_WIFI: return OMNI_LINK_WIFI_SHEET;
    case OMNI_ROW_ABOUT: return OMNI_LINK_ABOUT_SHEET;
    default: return OMNI_LINK_NONE;
    }
}
