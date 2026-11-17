#include "face_fade.h"

void face_fade_children(lv_obj_t *parent, lv_opa_t *shown, lv_opa_t opa)
{
    if (opa == *shown) return;
    *shown = opa;
    const uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) lv_obj_set_style_opa(lv_obj_get_child(parent, (int32_t)i), opa, 0);
}
