#pragma once
/* A full-screen reveal as pure code (rd wave 1 Task 2): what the view (ui/fk_view.c) needs from one, so the view drives
 * any theme's reveal on the direct path (its two cores, its 16-row jobs and 8-row bands, its claims and its canvas
 * fallback) without knowing what the reveal draws. One fk_reveal_ops_t per renderer kind; a face names its own in
 * fk_face_ops_t.reveal_ops, and the face's reveal_arg is passed to every call as it is (the first theme: its door kind,
 * an fk_cols_kind_t; a theme with one kind: NULL).
 *
 * A reveal's life in the view: bind once at its start (the face's reveal_asset; rctx is rctx_bytes of zeroed PSRAM,
 * the reveal's own for its lifetime), then per frame: frame (the worker, or the LVGL timer through the canvas), the
 * view draws the face's interior into `need` at the frame's wake_ms and reveal_live, then band over every band of the
 * frame, on both cores (band reads rctx and the interior, never writes them). Pure: no esp_*, freertos or LVGL; no
 * allocation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/face.h"
#include "omni/geom.h"

/* A frame's plan. need: the interior's rect to redraw for this frame (empty: reuse what the interior holds; whole on
 * the frame that draws the settled interior); settled: the frame is the reveal's settled end, so the view ends the
 * reveal after it; live: the face's in->reveal_live while the interior is drawn; wake_ms: the interior's wake_ms
 * (UINT32_MAX settled). live and wake_ms are read only when need is not empty. */
typedef struct { omni_rect_t need; bool settled, live; uint32_t wake_ms; } fk_rframe_t;

/* A bind calls get at most this many times (rd wave 1 final review, M2): ui/fk_view.c's copy keeps one PSRAM copy per
 * call and refuses the one past this (a plain wake). Each theme's host tests count its reveals' binds against it. */
#define FK_REVEAL_BIND_ASSETS_MAX 8

typedef struct fk_reveal_ops {
    uint16_t len_ms, reduced_ms, pop_ms;    /* the whole reveal; its cross-fade; the latch sound's start */
    const char *pop_asset;                  /* played at pop_ms by the view; NULL: no sound */
    size_t rctx_bytes;                      /* the reveal's own PSRAM context (leaf, glow, maps) */
    /* asset: the face's reveal_asset, NULL meaning the ops' default. False: the reveal cannot run (a plain wake). */
    bool (*bind)(const void *arg, const char *asset, fk_asset_fn get, void *rctx);
    /* the plan at t_ms after the start (skip: a tap's end frame; reduced: the cross-fade) into *out; it may keep state
     * in rctx from frame to frame (the frames come in order) */
    void (*frame)(const void *arg, void *rctx, uint32_t t_ms, bool skip, bool reduced, fk_rframe_t *out);
    /* the band's pixels (row 0 = band.y0, column x at offset x, stride 466) over the interior (466 x 466, stride 466),
     * for the last frame's plan */
    void (*band)(const void *arg, const void *rctx, const uint16_t *interior, omni_rect_t band, uint16_t *out);
} fk_reveal_ops_t;
