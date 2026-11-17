#pragma once
/* The Omni's sound effects (Stage 3 Task 13, tools/gen_sfx.py) by id, mapped to their assets for watch_ui's player
 * (sfx.h: sfx_play_asset). The names come from the asset manifest (assets_meta.h), so a renamed effect fails the build. */
#include "sfx.h"
#include "ui/assets_meta.h"

typedef enum { SFX_LATCH, SFX_WHOOSH, SFX_SURGE, SFX_CHIME, SFX_ALARM /* Stage 5: the timer's bell */, SFX__COUNT } sfx_id_t;

static inline void sfx_play(sfx_id_t id)
{
    static const char *const NAME[SFX__COUNT] = {ASSET_SFX_LATCH, ASSET_SFX_WHOOSH, ASSET_SFX_SURGE, ASSET_SFX_CHIME,
                                                ASSET_SFX_ALARM};
    if ((unsigned)id < SFX__COUNT) sfx_play_asset(NAME[id]);
}
