#pragma once
/* The faces' text helpers (text.c; rd wave 1 Task 3): the time string in 12 or 24 h, the date line and the AM/PM
 * suffix, by spec section 10.0's rule (R154): 12 h hours have no leading zero (the face centres the string on its own
 * width); AM/PM only as the awake date line's suffix, never on the hero time or in a glance. Byte strings for the glyph
 * atlases (fk/compose.h): the suffix's middle dot is Latin-1 0xB7. Pure C, no allocation. */
#include <stdbool.h>
#include "fk/clock.h"

/* "10:09", "1:05" (12 h: 1..12, no leading zero), "09:59" (24 h: two digits), "--:--" unset; returns strlen */
int fk_time_str(const fk_clock_t *c, bool h24, char out[6]);
/* "SUN 01 NOV" (the day, the date in two digits, the month); unset "--- -- ---" */
void fk_date_str(const fk_clock_t *c, char out[16]);
/* " \267 AM" / " \267 PM" (space, middle dot, space) in 12 h; "" in 24 h or unset */
const char *fk_ampm(const fk_clock_t *c, bool h24);
