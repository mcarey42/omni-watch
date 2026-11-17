#pragma once
/* The dw theme's display names: the theme's (Settings' Theme line and the swap toast), each face's title by
 * DW_FACE_TABLE's NAME (dw/prefs.h) and each Settings group's by its GROUP. These are the generic names. A build may
 * define DW_NAMES_OVERLAY and supply dw_names_overlay.h, which defines any of the same macros first; every name it
 * leaves out keeps the generic value below. Keys and ids never change here (dw/prefs.h). */
#ifdef DW_NAMES_OVERLAY
#include "dw_names_overlay.h"
#endif

#ifndef DW_NAME_THEME
#define DW_NAME_THEME "Time Traveller"
#endif

#ifndef DW_NAME_FACE_CRYSTAL
#define DW_NAME_FACE_CRYSTAL "Crystal Chamber"
#endif
#ifndef DW_NAME_FACE_ROUNDEL
#define DW_NAME_FACE_ROUNDEL "Roundel analog"
#endif
#ifndef DW_NAME_FACE_CALLBOX
#define DW_NAME_FACE_CALLBOX "Call box digital"
#endif
#ifndef DW_NAME_FACE_ROTOR
#define DW_NAME_FACE_ROTOR "Rotor"
#endif
#ifndef DW_NAME_FACE_CORAL
#define DW_NAME_FACE_CORAL "Coral"
#endif
#ifndef DW_NAME_FACE_CIRCLES
#define DW_NAME_FACE_CIRCLES "Circle Script"
#endif
#ifndef DW_NAME_FACE_DIGITAL
#define DW_NAME_FACE_DIGITAL "Digital"
#endif
#ifndef DW_NAME_FACE_LEARN
#define DW_NAME_FACE_LEARN "Learning"
#endif
#ifndef DW_NAME_FACE_ROOM
#define DW_NAME_FACE_ROOM "Roundel Room"
#endif
#ifndef DW_NAME_FACE_AMBER
#define DW_NAME_FACE_AMBER "Crystal"
#endif
#ifndef DW_NAME_FACE_FOB
#define DW_NAME_FACE_FOB "Fob watch"
#endif
#ifndef DW_NAME_FACE_CONSOLE
#define DW_NAME_FACE_CONSOLE "Console panel"
#endif
#ifndef DW_NAME_FACE_WALL
#define DW_NAME_FACE_WALL "Roundel wall"
#endif
#ifndef DW_NAME_FACE_DOORWAY
#define DW_NAME_FACE_DOORWAY "Through the Doors"
#endif
#ifndef DW_NAME_FACE_BROADCAST
#define DW_NAME_FACE_BROADCAST "Broadcast"
#endif
#ifndef DW_NAME_FACE_VORTEX
#define DW_NAME_FACE_VORTEX "Vortex"
#endif

#ifndef DW_NAME_GROUP_MODERN
#define DW_NAME_GROUP_MODERN "Modern"
#endif
#ifndef DW_NAME_GROUP_CLASSIC
#define DW_NAME_GROUP_CLASSIC "Classic"
#endif
#ifndef DW_NAME_GROUP_CIRCLES
#define DW_NAME_GROUP_CIRCLES "Circle Script"
#endif
#ifndef DW_NAME_GROUP_FACES
#define DW_NAME_GROUP_FACES "Faces"
#endif
