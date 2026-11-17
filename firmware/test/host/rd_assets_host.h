#pragma once
/* The Mining Ship assets on the host (rd wave 1 Task 7): files read from one directory (the committed set,
 * firmware/apps/rd/assets), dw_assets_host's shape without an overlay (rd has none, R153). Each file is held 64-aligned
 * (the image's alignment) until close. Not thread-safe; one set at a time. */
#include <stdbool.h>
#include <stddef.h>

bool rd_assets_host_open(const char *dir);
void rd_assets_host_close(void);
/* The named file, or NULL (and *size 0) if the directory has none. */
const void *rd_assets_host_get(const char *name, size_t *size);
