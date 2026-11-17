#pragma once
/* SCR_KEYBOARD: the rim keyboard (spec 6; faces-setup.js `rim-keyboard`) in its three modes, with the join's result in
 * place (spec 7.2) and the city search's matches (spec 8.4). Same threading rules as shell.h: call with the LVGL lock
 * unless a line says otherwise. The typed password lives only in the screen's buffer (internal RAM) and reaches net only
 * through net_try_join; it is wiped on destroy, the 60 s close, sleep (the screen is popped), every back-out and when the
 * result view closes. */
#include <stdbool.h>
#include <stdint.h>
#include "shell.h"

typedef struct {
    uint8_t mode;                                         /* omni_kb_mode_t */
    uint8_t ssid[32]; uint8_t ssid_len; uint8_t authmode; /* password mode: the network (exact bytes) */
} scr_kb_open_t;

void scr_keyboard_prepare(const scr_kb_open_t *o);   /* LVGL lock: the next create opens this way */
void scr_keyboard_net(int ev);                       /* LVGL lock: an EV_NET with NET_EV_TRIAL */
bool scr_keyboard_secret_open(void);                 /* any task: open in password mode (the shot guard) */
void scr_keyboard_mask(void);                        /* LVGL lock: the eye's reveal off (every wake and the glance) */
/* LVGL lock: the keyboard's assets (the layout, the two Plex Mono fonts, the icons) loaded, once; false: the keyboard
 * does not open and the list offers only the phone path (spec 6.4). */
bool scr_keyboard_ready(void);
extern const omni_screen_vt_t SCR_KEYBOARD_VT;
