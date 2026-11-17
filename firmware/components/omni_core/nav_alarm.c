#include "omni/nav_alarm.h"

omni_nav_effect_t omni_nav_alarm(omni_nav_t *n, const omni_settings_t *s, uint8_t face_id)
{
    if (omni_nav_in_stack(n, SCR_PROVISION)) return NAV_NONE;
    return omni_nav_show_id(n, s, face_id, OMNI_CAROUSEL_MAX);
}
