#include "omni/power.h"

/* Burn-in protection: a 9-minute ring of distinct points within +-4 px. */
static const int8_t RING[9][2] = {
    {0, 0}, {3, 1}, {4, -2}, {2, -4}, {-1, -3}, {-3, -1}, {-4, 2}, {-2, 4}, {1, 3},
};

void omni_orbit_offset(int64_t unix_s, int8_t *dx, int8_t *dy)
{
    int64_t m = unix_s / 60;
    if (unix_s % 60 < 0) m--; /* floor */
    int i = (int)(((m % 9) + 9) % 9);
    *dx = RING[i][0];
    *dy = RING[i][1];
}
