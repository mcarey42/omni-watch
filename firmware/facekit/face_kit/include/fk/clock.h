#pragma once
/* The faces' view of the clock: local broken-down time, computed by the caller (the app owns the time zone and the
 * 12/24 h setting), so every face stays TZ-free and a face's pure code takes this struct and nothing else. Pure: no
 * esp_*. */
#include <stdbool.h>
#include <stdint.h>

typedef struct fk_clock {
    bool set, stale;               /* set: the time is known; stale: it is known but old */
    uint8_t h, m, s, mday, mon, wday;
    uint16_t ms;
    uint16_t yday;                 /* appended: 1..366, localtime_r's tm_yday + 1; 0 unknown (a still leaves it 0) */
} fk_clock_t;

/* The clock at unix_s (the shell's now) and ms (from the same read), in the caller's time zone (localtime_r: the app
 * applied TZ). set = omni_time_is_set(unix_s); stale is kept only when set. Unset: every other field 0. */
void fk_clock_from(int64_t unix_s, uint16_t ms, bool stale, fk_clock_t *c);
