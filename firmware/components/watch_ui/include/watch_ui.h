#pragma once
/* watch_ui, the UI every theme firmware shares (plan PD1): the shell and its pushed screens (provisioning, picker,
 * quick settings, settings), the digital face and the glance, the house style, the assets mapping, sound, the display
 * paths and the console core. One include for a theme's descriptor file; face files include what they use. */
#include "watch_theme.h"
#include "shell.h"
#include "theme.h"
#include "glance.h"
#include "assets.h"
#include "sfx.h"
#include "disp_stats.h"
#include "disp_direct.h"
#include "debug_console.h"
