#pragma once
/* The Omni's face ids (plan PD2: a theme's own uint8_t, no cross-theme ranges), moved out of omni_core's settings.h.
 * They index OMNI_THEME.faces (omni_theme.c) and are what the carousel and the Omni's NVS records store, so the
 * values never change: append only. */
#include "shell.h"

enum omni_face {
    FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL,
    FACE_WORLD, FACE_ALMANAC, FACE_TIMER, FACE_GLOBE_SPIN, FACE__COUNT
};

/* The Omni's own face views (FACE_DIGITAL_VT is watch_ui's); registered only when the assets are mapped. */
extern const omni_face_vt_t FACE_GLOBE_LIVE_VT;   /* face_globe_live.c */
extern const omni_face_vt_t FACE_GLOBE_SPIN_VT;   /* face_globe_spin.c */
extern const omni_face_vt_t FACE_GLOBE_ANALOG_VT; /* face_globe_analog.c */
extern const omni_face_vt_t FACE_OMNI_VT;         /* face_omni.c (Stage 3) */
extern const omni_face_vt_t FACE_TIMER_VT;        /* face_timer.c (Stage 5) */
extern const omni_face_vt_t FACE_ALMANAC_VT;      /* face_almanac.c (Stage 5) */
extern const omni_face_vt_t FACE_WORLD_VT;        /* face_world.c (Stage 5) */
