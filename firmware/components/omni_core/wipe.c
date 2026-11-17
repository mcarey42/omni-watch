#include "omni/wipe.h"
#include <stdint.h>

void omni_wipe(void *p, size_t n)
{
    volatile uint8_t *q = (volatile uint8_t *)p;
    while (n--) *q++ = 0;
}
