#pragma once
/* The Omni faces' fade (R28: never container opacity): every child of parent at opa, through each object's own style
 * opacity. *shown holds the opacity as applied; nothing is set (or invalidated) when opa equals it. LVGL lock held. */
#include "lvgl.h"

void face_fade_children(lv_obj_t *parent, lv_opa_t *shown, lv_opa_t opa);
