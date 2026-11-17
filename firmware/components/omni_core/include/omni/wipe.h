#pragma once
/* Non-elidable zeroing for secrets (the rim keyboard's buffer, the password's copies): volatile stores, so the compiler
 * cannot drop them as dead stores before a free or a return. */
#include <stddef.h>

void omni_wipe(void *p, size_t n);
