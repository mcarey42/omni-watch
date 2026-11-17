#pragma once
/* City search over the built-in table (spec section 8, O3; Task 6). Pure: no state. */
#include <stdint.h>

#define OMNI_CITY_FIND_MAX 16
/* ASCII case-folded: names starting with q (table order), then names containing q elsewhere, then cities whose country
 * name (omni_country_name) starts with q, each city once, at most 16 table indices; "" or NULL gives 0 */
int omni_city_find_all(const char *q, uint16_t out[OMNI_CITY_FIND_MAX]);
