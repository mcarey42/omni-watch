#pragma once
/* The Mining Ship console (rd wave 1 Task 4, R101): `rd` and its sub-commands, console builds only. */
#include <stdbool.h>
#include <stdint.h>
void rd_console_register(void);   /* watch_theme_t.console */
/* rd_theme.c: true while id is still shown by the shared Digital face in its own place (RW11; `rd faces`) */
bool rd_theme_placeholder(uint8_t id);
