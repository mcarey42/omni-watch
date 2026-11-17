#pragma once
/* The countdown glance (Omni Stage 5 Task 6): the timer face's sleepFrame (faces-practical.js 1287), which takes the
 * glance on every face while a countdown runs or is paused (watch_theme.h glance_override). */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/* LVGL lock. true in COUNT and PAUSED: root then shows a thin brass arc of the time left (r 205), the minutes left
 * (ceil) and MIN LEFT, offset by omni_orbit_offset. Otherwise false (the face's own glance) and root is emptied. */
bool timer_glance_override(lv_obj_t *root, int64_t now, bool h24);

/* An lv_arc showing only its indicator (transparent background and track, no knob, not clickable): centre line r round
 * the screen's centre, w px, rgb at opa, from 12 o'clock clockwise over value 0..range. The timer face's hold ring and
 * the countdown glance share it. */
lv_obj_t *timer_arc_create(lv_obj_t *parent, int32_t r, int32_t w, uint32_t rgb, lv_opa_t opa, bool rounded,
                           int32_t range);
