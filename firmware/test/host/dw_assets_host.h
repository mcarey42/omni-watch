#pragma once
/* The dw assets on the host (dw wave 1 Task 11): the face harness's fk_asset_fn. Files are read from a base
 * directory (the generic set, firmware/apps/dw/assets) and, when an overlay directory is given (the private art
 * overlay's assets/), a file of the same name there replaces the base's: the staged private set, as the private
 * build's dw_stage.cmake stages it. Each file is held 64-aligned (the image's alignment). Not thread-safe; one set at
 * a time. */
#include <stdbool.h>
#include <stddef.h>

bool dw_assets_host_open(const char *dir, const char *overlay_dir);   /* overlay_dir NULL: the base alone */
void dw_assets_host_close(void);
/* fk_asset_fn: the named file, or NULL (and *size 0) if neither directory has it. */
const void *dw_assets_host_get(const char *name, size_t *size);
/* Which directory the last get found the file in: true for the overlay. */
bool dw_assets_host_from_overlay(const char *name);
